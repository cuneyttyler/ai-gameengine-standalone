#include "places/Solver.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <queue>
#include <set>
#include <sstream>

namespace aige {

using json = nlohmann::json;

namespace {

// --- Randomness: one stream per statement --------------------------------------------

uint64_t Fnv(const std::string& text, uint64_t seed) {
    uint64_t h = 1469598103934665603ull ^ (seed * 0x9E3779B97F4A7C15ull);
    for (const unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

struct Rng {
    uint64_t state;
    explicit Rng(uint64_t s) : state(s ? s : 0x1234567ull) {}
    uint64_t Next() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double Unit() { return static_cast<double>(Next() >> 11) / 9007199254740992.0; }
    int Range(int lo, int hi) { return hi <= lo ? lo : lo + static_cast<int>(Next() % static_cast<uint64_t>(hi - lo + 1)); }
};

// --- Layers ---------------------------------------------------------------------------

enum Layer { kGround = 0, kDecal, kProp, kBuilding, kOverhead, kLayers };

int LayerOf(const std::string& name) {
    if (name == "decal") return kDecal;
    if (name == "prop") return kProp;
    if (name == "building") return kBuilding;
    if (name == "overhead") return kOverhead;
    return kGround;
}
const char* LayerName(int layer) {
    static const char* names[] = {"ground", "decal", "prop", "building", "overhead"};
    return names[layer];
}
bool Solid(int layer) { return layer == kProp || layer == kBuilding; }

struct IntRange {
    int lo = 0, hi = 0;
};
IntRange RangeOf(const json& v, int fallback) {
    if (v.is_number()) return {static_cast<int>(std::lround(v.get<double>())), static_cast<int>(std::lround(v.get<double>()))};
    if (v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number()) {
        int a = static_cast<int>(std::lround(v[0].get<double>())), b = static_cast<int>(std::lround(v[1].get<double>()));
        return {std::min(a, b), std::max(a, b)};
    }
    return {fallback, fallback};
}

char Opposite(char side) {
    switch (side) {
        case 'n': return 's';
        case 's': return 'n';
        case 'e': return 'w';
        case 'w': return 'e';
    }
    return 's';
}

// --- The solver ---------------------------------------------------------------------------

struct Region {
    std::string id, node, statement;
    int parent = -1;
    int depth = 0;
    std::vector<int> cells;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // bounds, exclusive
    char facing = 0;                    // toward its street (parcels)
    std::vector<int> street;            // the street cells it fronts
    std::string module;
};

struct Object {
    std::string id, statement, node, slot, module;
    int x = 0, y = 0, w = 1, h = 1;
    char facing = 's';
    std::string faced; // what set its facing ("layout": its module's layout), so it isn't turned again
    int layer = kProp;
    bool primary = false;
    // A shaped building (building-shape "rounded" or "fitted"): the cells it covers, if not its whole box (a
    // fitted one's), and its shape; its outline is drawn from them.
    std::vector<int> cells;
    std::string shape;
};

// A footprint's hull: the convex outline round its cells' corners, clockwise on screen (y down).
static std::vector<std::pair<double, double>> CellHull(const std::vector<int>& cells, int W) {
    std::vector<std::pair<double, double>> pts;
    for (int c : cells) {
        const double x = c % W, y = c / W;
        pts.insert(pts.end(), {{x, y}, {x + 1, y}, {x, y + 1}, {x + 1, y + 1}});
    }
    std::sort(pts.begin(), pts.end());
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    if (pts.size() < 3) return pts;
    auto cross = [](const auto& o, const auto& a, const auto& b) { return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first); };
    std::vector<std::pair<double, double>> hull(2 * pts.size());
    size_t k = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) --k;
        hull[k++] = pts[i];
    }
    for (size_t i = pts.size() - 1, lower = k + 1; i-- > 0;) {
        while (k >= lower && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) --k;
        hull[k++] = pts[i];
    }
    hull.resize(k - 1);
    return hull;
}

static double HullArea(const std::vector<std::pair<double, double>>& hull) {
    double area = 0;
    for (size_t i = 0; i < hull.size(); ++i) {
        const auto& a = hull[i];
        const auto& b = hull[(i + 1) % hull.size()];
        area += a.first * b.second - b.first * a.second;
    }
    return std::abs(area) / 2;
}

// A footprint's outline, smooth: its hull (a fitted footprint is kept near convex: a wedge, a trapezoid, a box),
// each corner rounded (radius in cells). [x0, y0, x1, y1, …] in cells.
static std::vector<double> SmoothOutline(const std::vector<int>& cells, int W, double radius) {
    const std::vector<std::pair<double, double>> corners = CellHull(cells, W);
    if (corners.size() < 3) return {};
    // Each corner an arc (a quadratic curve through it), as round as its two sides allow.
    std::vector<double> out;
    for (size_t i = 0; i < corners.size(); ++i) {
        const auto& p = corners[(i + corners.size() - 1) % corners.size()];
        const auto& v = corners[i];
        const auto& n = corners[(i + 1) % corners.size()];
        const double lp = std::hypot(p.first - v.first, p.second - v.second), ln = std::hypot(n.first - v.first, n.second - v.second);
        const double r = std::min({radius, lp * 0.45, ln * 0.45});
        const double ax = v.first + (p.first - v.first) / lp * r, ay = v.second + (p.second - v.second) / lp * r;
        const double bx = v.first + (n.first - v.first) / ln * r, by = v.second + (n.second - v.second) / ln * r;
        for (int k = 0; k <= 4; ++k) {
            const double t = k / 4.0, u = 1 - t;
            out.push_back(u * u * ax + 2 * u * t * v.first + t * t * bx);
            out.push_back(u * u * ay + 2 * u * t * v.second + t * t * by);
        }
    }
    return out;
}

struct PathResult {
    std::string id, node, cls, terrain;
    std::string edge; // the terrain of the outer cells of its width (a street's sidewalks), or none
    std::vector<int> cells;
    int width = 1;
};

struct LineResult {
    std::string id, node, slot;
    std::vector<std::array<int, 3>> edges; // x, y, side (0 = north edge of (x,y), 1 = west edge)
    std::vector<std::array<int, 3>> gaps;
    bool closed = true;
};

struct AnchorResult {
    std::string id, node, kind;
    int cell = -1;
};

class Solver {
public:
    Solver(const Ontology& ontology, const json& ir, const SolveOptions& options)
        : ontology_(ontology), ir_(ir), options_(options) {}

    SolvedLayout Run();

private:
    // Grid.
    int W = 0, H = 0;
    std::vector<int> owner_;                      // the deepest region claiming each cell (-1: none)
    std::array<std::vector<int>, kLayers> occ_;   // object index + 1 per layer
    std::vector<uint8_t> road_;                   // street class rank: 0 none, 1 trail, 2 lane, 3 street, 4 main
    std::vector<uint8_t> water_;
    // Spaces (7.10): rock between a dungeon's rooms, cells kept clear (doorways,
    // windows), walls on cell edges (interiors), stairs linking cells of two levels.
    std::vector<uint8_t> solid_;
    std::vector<uint8_t> keepFree_;
    std::set<long> wallEdges_;
    std::vector<std::pair<int, int>> links_;
    std::map<std::string, std::vector<int>> doorsOf_; // a room's cells at its doors
    std::map<std::string, int> stairsOf_;             // a floorplan's stairs cell
    std::vector<json> graphs_;                        // each dungeon level's rooms, edges, keys
    json connectors_ = json::array();
    std::map<std::string, std::vector<int>> spreadGroups_; // regions spread apart, by group
    std::map<std::string, double> msByOp_;                 // time spent per op (stats)
    int Cell(int x, int y) const { return y * W + x; }
    bool In(int x, int y) const { return x >= 0 && y >= 0 && x < W && y < H; }

    // Results.
    std::vector<Region> regions_;
    std::map<std::string, int> regionById_;
    std::map<std::string, int> zoneSeeds_; // each zone's seed cell (written out, for a revision to keep)
    std::vector<Object> objects_;
    std::map<std::string, std::vector<int>> objectsByStatement_;
    std::map<std::string, int> objectById_;
    std::vector<PathResult> paths_;
    std::vector<std::pair<int, std::string>> groundUnder_; // a patch's own ground (a wood's forest floor)
    std::vector<LineResult> lines_;
    std::map<std::string, AnchorResult> anchors_;
    std::map<std::string, int> instancesOf_; // statement id -> how many it produced
    std::vector<json> problems_;
    std::vector<json> relationScores_;
    std::vector<json> checks_;

    // The document.
    std::vector<const json*> statements_;
    std::deque<json> instanceStatements_; // instance statements' copies (x[*] run once per instance); a deque keeps them in place
    std::map<std::string, std::vector<const json*>> byContainer_;
    std::vector<const json*> relations_;
    std::map<std::string, std::vector<const json*>> relationsOf_; // by their first member
    uint64_t seed_ = 7;

    const Ontology& ontology_;
    const json& ir_;
    SolveOptions options_;

    void Problem(const std::string& code, const json& s, const std::string& message) {
        problems_.push_back({{"code", code}, {"statement", s.value("id", "")}, {"node", s.value("node", "")}, {"message", message}});
    }
    Rng StreamFor(const json& s) const { return Rng(Fnv(s.value("id", ""), seed_)); }

    // References.
    std::string ContainerOf(const json& s) const;
    std::vector<int> Cells(const std::string& ref, const json* context = nullptr);
    std::vector<int> CellsOne(std::string ref);
    std::optional<std::array<double, 2>> CenterOf(const std::string& ref);
    int RegionIndex(const std::string& id) const {
        auto it = regionById_.find(id);
        return it == regionById_.end() ? -1 : it->second;
    }

    // Steps.
    void SolveRegion(int index);
    // The partial solve: whether a node is laid out again, and the previous layout restored.
    bool Redo(const std::string& node) const;
    bool Locked(const std::string& node) const;
    bool StatementRedone(const std::string& statementId, const std::string& node) const;
    void Restore(const json& previous);
    std::map<std::string, const json*> statementById_;
    void Path(const json& s);
    void Network(const json& s, int region);
    void Partition(const json& s, int region);
    void Subdivide(const json& s, int region);
    void Place(const json& s, int region);
    void NestedRegion(const json& s, int region);
    void Anchor(const json& s);
    void Line(const json& s);
    void Rows(const json& s, int region, bool grid);
    void Scatter(const json& s, int region);
    void Rooms(const json& s, int region);
    void Floorplan(const json& s, int region);
    void Connect(const json& s, int region);
    void Flood(const json& s, int region);
    json Terrain();
    void Checks();
    void SpaceChecks(const json& s, json& result);
    json SpacesJson() const;
    void RestoreSpaces(const json& previous);
    void MarkWall(int x, int y, int side);
    bool Crosses(int a, int b) const;
    std::vector<uint8_t> Reached(bool throughProps);
    // Whether a solid thing on these cells leaves a room's floor joined to its doors.
    bool KeepsWay(int container, int x, int y, int w, int h) const;
    std::vector<int> Blob(int x, int y, int w, int h, Rng& rng);

    // Helpers.
    int AddRegion(const std::string& id, const std::string& node, const std::string& statement, int parent, std::vector<int> cells,
                  const std::string& module = {});
    int AddObject(Object o);
    bool Free(int cell, int container, bool solid) const;
    std::vector<int> AStar(int from, int to, const std::function<double(int)>& cost);
    std::vector<int> AStarIn(int from, int to, const std::function<double(int)>& cost, int x0, int y0, int x1, int y1);
    void MarkRoad(const std::vector<int>& cells, int width, uint8_t rank, int container);
    double Score(const std::string& memberId, int x, int y, int w, int h, int container, char facing);
    // A relation member's cells while one object or region is being placed (the other members stay put).
    std::map<std::string, std::vector<int>> scoreCache_;
    const std::vector<int>& Cached(const std::string& ref) {
        auto it = scoreCache_.find(ref);
        if (it == scoreCache_.end()) it = scoreCache_.emplace(ref, Cells(ref)).first;
        return it->second;
    }
};

std::string Solver::ContainerOf(const json& s) const {
    for (const char* key : {"in", "along"}) {
        if (s.contains(key) && s[key].is_string()) {
            std::string ref = s[key].get<std::string>();
            ref = ref.substr(0, ref.find_first_of(" |&"));
            // An instance reference (x[*]) lives where the statement making x does.
            if (const size_t bracket = ref.find('['); bracket != std::string::npos) {
                const std::string maker = ref.substr(0, bracket);
                if (auto it = statementById_.find(maker); it != statementById_.end() && it->second != &s) return ContainerOf(*it->second);
            }
            // The region a reference names: the longest known prefix.
            while (!ref.empty()) {
                if (regionById_.contains(ref)) return ref;
                // By id (a map: a city has many thousands of statements).
                const auto found = statementById_.find(ref);
                const json* const* maker = found != statementById_.end() ? &found->second : nullptr;
                if (maker != nullptr) {
                    // Next to an object (a house's front): where the object stands,
                    // after it (scatters come after places in a region).
                    const std::string op = (*maker)->value("op", "");
                    const bool object = op == "place" || op == "scatter" || op == "line" || op == "grid" ||
                                        (op == "rows" && !(*maker)->value("regions", false));
                    return object && *maker != &s ? ContainerOf(**maker) : ref;
                }
                const size_t cut = ref.find_last_of('.');
                if (cut == std::string::npos) break;
                ref = ref.substr(0, cut);
            }
            return ref;
        }
    }
    return {};
}

int Solver::AddRegion(const std::string& id, const std::string& node, const std::string& statement, int parent,
                      std::vector<int> cells, const std::string& module) {
    Region r;
    r.id = id;
    r.node = node;
    r.statement = statement;
    r.parent = parent;
    r.depth = parent >= 0 ? regions_[parent].depth + 1 : 0;
    r.module = module;
    std::sort(cells.begin(), cells.end());
    r.cells = std::move(cells);
    r.x0 = W;
    r.y0 = H;
    for (int c : r.cells) {
        r.x0 = std::min(r.x0, c % W);
        r.y0 = std::min(r.y0, c / W);
        r.x1 = std::max(r.x1, c % W + 1);
        r.y1 = std::max(r.y1, c / W + 1);
    }
    const int index = static_cast<int>(regions_.size());
    for (int c : r.cells) owner_[c] = index;
    regionById_[id] = index;
    regions_.push_back(std::move(r));
    return index;
}

int Solver::AddObject(Object o) {
    const int index = static_cast<int>(objects_.size());
    if (!o.cells.empty()) {
        for (int c : o.cells) occ_[o.layer][c] = index + 1; // angled: only what it covers
    } else {
        for (int y = o.y; y < o.y + o.h; ++y) {
            for (int x = o.x; x < o.x + o.w; ++x) {
                if (In(x, y)) occ_[o.layer][Cell(x, y)] = index + 1;
            }
        }
    }
    objectsByStatement_[o.statement].push_back(index);
    objectById_[o.id] = index;
    objects_.push_back(std::move(o));
    return index;
}

bool Solver::Free(int cell, int container, bool solid) const {
    if (container >= 0 && owner_[cell] != container) return false;
    if (water_[cell] || solid_[cell]) return false;
    if (solid && keepFree_[cell]) return false;
    if (solid) {
        if (road_[cell] || occ_[kProp][cell] || occ_[kBuilding][cell]) return false;
    }
    return true;
}

// --- References -----------------------------------------------------------------------------

std::vector<int> Solver::Cells(const std::string& ref, const json* context) {
    (void)context;
    // Region expressions: a | b, a & b, a - b (left to right).
    std::vector<int> result;
    std::string op = "|";
    size_t i = 0;
    bool first = true;
    while (i <= ref.size()) {
        size_t next = ref.size();
        std::string nextOp;
        for (size_t j = i; j < ref.size(); ++j) {
            const char c = ref[j];
            if ((c == '|' || c == '&' || (c == '-' && j > 0 && ref[j - 1] == ' ' && j + 1 < ref.size() && ref[j + 1] == ' '))) {
                next = j;
                nextOp = std::string(1, c);
                break;
            }
        }
        std::string part = ref.substr(i, next - i);
        part.erase(0, part.find_first_not_of(' '));
        part.erase(part.find_last_not_of(' ') + 1);
        std::vector<int> cells = CellsOne(part);
        std::sort(cells.begin(), cells.end());
        cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
        if (first) {
            result = cells;
            first = false;
        } else {
            std::vector<int> out;
            if (op == "|") std::set_union(result.begin(), result.end(), cells.begin(), cells.end(), std::back_inserter(out));
            else if (op == "&") std::set_intersection(result.begin(), result.end(), cells.begin(), cells.end(), std::back_inserter(out));
            else std::set_difference(result.begin(), result.end(), cells.begin(), cells.end(), std::back_inserter(out));
            result = out;
        }
        if (next >= ref.size()) break;
        op = nextOp;
        i = next + 1;
    }
    return result;
}

std::vector<int> Solver::CellsOne(std::string ref) {
    std::vector<int> out;
    if (ref.empty()) return out;
    // Split "base.selector(args)".
    std::string base = ref, selector, args;
    while (!base.empty() && !regionById_.contains(base) && !objectById_.contains(base) && !anchors_.contains(base) &&
           !objectsByStatement_.contains(base)) {
        bool pathHit = std::any_of(paths_.begin(), paths_.end(), [&](const PathResult& p) { return p.id == base; });
        if (pathHit) break;
        const size_t dot = base.find_last_of('.');
        if (dot == std::string::npos) return out;
        selector = base.substr(dot + 1) + (selector.empty() ? "" : "." + selector);
        base = base.substr(0, dot);
    }
    if (const size_t open = selector.find('('); open != std::string::npos) {
        args = selector.substr(open + 1, selector.find(')') - open - 1);
        selector = selector.substr(0, open);
    }
    if (const int r = RegionIndex(base); r >= 0) {
        const Region& region = regions_[r];
        auto inRegion = [&](int x, int y) { return In(x, y) && owner_[Cell(x, y)] >= 0 && std::binary_search(region.cells.begin(), region.cells.end(), Cell(x, y)); };
        if (selector.empty() || selector == "cells") return region.cells;
        if (selector == "inner" || selector == "border") {
            for (int c : region.cells) {
                const int x = c % W, y = c / W;
                const bool inner = inRegion(x - 1, y) && inRegion(x + 1, y) && inRegion(x, y - 1) && inRegion(x, y + 1);
                if (inner == (selector == "inner")) out.push_back(c);
            }
            return out;
        }
        if (selector == "center") {
            out.push_back(Cell((region.x0 + region.x1) / 2, (region.y0 + region.y1) / 2));
            return out;
        }
        if (selector == "street") return region.street;
        if (selector == "side") {
            const char side = args.empty() ? region.facing : args[0];
            for (int c : region.cells) {
                const int x = c % W, y = c / W;
                const bool on = (side == 'n' && y == region.y0) || (side == 's' && y == region.y1 - 1) ||
                                (side == 'w' && x == region.x0) || (side == 'e' && x == region.x1 - 1);
                if (on) out.push_back(c);
            }
            return out;
        }
        return region.cells;
    }
    if (auto it = objectById_.find(base); it != objectById_.end() || objectsByStatement_.contains(base)) {
        std::vector<int> indices = it != objectById_.end() ? std::vector<int>{it->second} : objectsByStatement_[base];
        for (int index : indices) {
            const Object& o = objects_[index];
            if (selector.empty() || selector == "cells") {
                for (int y = o.y; y < o.y + o.h; ++y) {
                    for (int x = o.x; x < o.x + o.w; ++x) out.push_back(Cell(x, y));
                }
                continue;
            }
            // The cells just outside one side: front (its facing), back, left, right; door: front middle.
            char side = o.facing;
            if (selector == "back") side = Opposite(o.facing);
            else if (selector == "left") side = o.facing == 'n' ? 'w' : o.facing == 's' ? 'e' : o.facing == 'e' ? 'n' : 's';
            else if (selector == "right") side = Opposite(o.facing == 'n' ? 'w' : o.facing == 's' ? 'e' : o.facing == 'e' ? 'n' : 's');
            std::vector<int> ring;
            if (side == 'n' || side == 's') {
                const int y = side == 'n' ? o.y - 1 : o.y + o.h;
                for (int x = o.x; x < o.x + o.w; ++x) {
                    if (In(x, y)) ring.push_back(Cell(x, y));
                }
            } else {
                const int x = side == 'w' ? o.x - 1 : o.x + o.w;
                for (int y = o.y; y < o.y + o.h; ++y) {
                    if (In(x, y)) ring.push_back(Cell(x, y));
                }
            }
            if (selector == "door" && !ring.empty()) ring = {ring[ring.size() / 2]};
            out.insert(out.end(), ring.begin(), ring.end());
        }
        return out;
    }
    if (auto it = anchors_.find(base); it != anchors_.end()) {
        if (it->second.cell >= 0) out.push_back(it->second.cell);
        return out;
    }
    for (const auto& p : paths_) {
        if (p.id == base) return p.cells;
    }
    return out;
}

std::optional<std::array<double, 2>> Solver::CenterOf(const std::string& ref) {
    const auto cells = Cells(ref);
    if (cells.empty()) return std::nullopt;
    double sx = 0, sy = 0;
    for (int c : cells) {
        sx += c % W + 0.5;
        sy += c / W + 0.5;
    }
    return std::array<double, 2>{sx / cells.size(), sy / cells.size()};
}

// --- Relations ------------------------------------------------------------------------------

// The score of every relation `memberId` takes part in, for a candidate
// rectangle, against members already solved (unsolved ones are skipped).
double Solver::Score(const std::string& memberId, int x, int y, int w, int h, int container, char facing) {
    (void)facing;
    double total = 0;
    const double cx = x + w / 2.0, cy = y + h / 2.0;
    auto rectDistance = [&](const std::vector<int>& cells) {
        int best = std::numeric_limits<int>::max();
        for (int c : cells) {
            const int px = c % W, py = c / W;
            const int dx = px < x ? x - px : (px >= x + w ? px - (x + w - 1) : 0);
            const int dy = py < y ? y - py : (py >= y + h ? py - (y + h - 1) : 0);
            best = std::min(best, std::max(dx, dy));
        }
        return best == std::numeric_limits<int>::max() ? -1 : best;
    };
    const auto mine = relationsOf_.find(memberId);
    if (mine == relationsOf_.end()) return 0;
    for (const json* rel : mine->second) {
        const json& members = rel->at("members");
        if (members.empty() || members[0] != memberId) continue;
        const std::string type = rel->value("type", "");
        const double weight = rel->value("weight", 1.0);
        const json args = rel->value("args", json::object());
        double score = 1.0;
        std::string other = members.size() > 1 && members[1].is_string() ? members[1].get<std::string>() : "";
        if (type == "center") {
            const Region& r = regions_[container];
            const double px = (r.x0 + r.x1) / 2.0, py = (r.y0 + r.y1) / 2.0;
            const double half = std::hypot(r.x1 - r.x0, r.y1 - r.y0) / 2.0;
            score = 1.0 - std::min(1.0, std::hypot(cx - px, cy - py) / std::max(1.0, half));
        } else if (type == "edge") {
            const Region& r = regions_[container];
            const int d = std::min({x - r.x0, y - r.y0, r.x1 - (x + w), r.y1 - (y + h)});
            score = std::max(0.0, 1.0 - d / 4.0);
        } else if (!other.empty()) {
            const auto& cells = Cached(other);
            if (cells.empty()) continue; // not solved yet, or nothing there
            const int d = rectDistance(cells);
            if (type == "adjacent") {
                score = d <= 1 ? 1.0 : std::max(0.0, 1.0 - d / 2.0);
            } else if (type == "near") {
                const double d0 = args.value("d0", 1.0), d1 = args.value("d1", 6.0);
                score = d <= d0 ? 1.0 : d >= d1 ? 0.0 : 1.0 - (d - d0) / (d1 - d0);
            } else if (type == "far" || type == "away-from") {
                score = std::min(1.0, d / std::max(1.0, args.value("d", 3.0)));
            } else if (type == "behind" || type == "in-front-of" || type == "beside") {
                // In the other's facing frame (objects), else its region's.
                char f = 's';
                std::string base = other.substr(0, other.find('.'));
                if (auto it = objectById_.find(base); it != objectById_.end()) f = objects_[it->second].facing;
                else if (const int r = RegionIndex(base); r >= 0 && regions_[r].facing) f = regions_[r].facing;
                std::optional<std::array<double, 2>> oc;
                if (!cells.empty()) {
                    double sx = 0, sy = 0;
                    for (int c : cells) {
                        sx += c % W + 0.5;
                        sy += c / W + 0.5;
                    }
                    oc = std::array<double, 2>{sx / cells.size(), sy / cells.size()};
                }
                if (!oc) continue;
                double fx = 0, fy = 0;
                if (f == 'n') fy = -1;
                if (f == 's') fy = 1;
                if (f == 'e') fx = 1;
                if (f == 'w') fx = -1;
                const double forward = (cx - (*oc)[0]) * fx + (cy - (*oc)[1]) * fy;
                const double lateral = std::abs((cx - (*oc)[0]) * fy - (cy - (*oc)[1]) * fx);
                if (type == "behind") score = forward < 0 ? std::max(0.0, 1.0 - lateral / 6.0) : 0.0;
                else if (type == "in-front-of") score = forward > 0 ? std::max(0.0, 1.0 - lateral / 6.0) : 0.0;
                else score = std::abs(forward) < 2 ? 1.0 : 0.0;
            } else if (type == "along") {
                score = std::max(0.0, 1.0 - std::abs(d - args.value("offset", 0.0)) / 3.0);
            }
        }
        total += weight * score;
    }
    return total;
}

// --- Paths ------------------------------------------------------------------------------------

std::vector<int> Solver::AStarIn(int from, int to, const std::function<double(int)>& cost, int x0, int y0, int x1, int y1) {
    // Within the window [x0, x1) x [y0, y1) only: arrays of the window's size (a ward's lanes in a city).
    x0 = std::max(0, x0), y0 = std::max(0, y0), x1 = std::min(W, x1), y1 = std::min(H, y1);
    const int ww = x1 - x0, wh = y1 - y0;
    auto local = [&](int c) { return (c / W - y0) * ww + (c % W - x0); };
    auto inside = [&](int x, int y) { return x >= x0 && y >= y0 && x < x1 && y < y1; };
    if (ww <= 0 || wh <= 0 || !inside(from % W, from / W) || !inside(to % W, to / W)) return {};
    std::vector<double> dist(static_cast<size_t>(ww) * wh, std::numeric_limits<double>::infinity());
    std::vector<int> prev(static_cast<size_t>(ww) * wh, -1);
    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> open;
    const int tx = to % W, ty = to / W;
    dist[local(from)] = 0;
    open.push({0, from});
    while (!open.empty()) {
        const auto [f, c] = open.top();
        open.pop();
        if (c == to) break;
        const int x = c % W, y = c / W;
        const double g = dist[local(c)];
        if (f - (std::abs(x - tx) + std::abs(y - ty)) * 0.1 > g + 1e-9) continue;
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx[k], ny = y + dy[k];
            if (!inside(nx, ny)) continue;
            const int n = Cell(nx, ny);
            const double step = cost(n);
            if (!std::isfinite(step)) continue;
            if (g + step < dist[local(n)]) {
                dist[local(n)] = g + step;
                prev[local(n)] = c;
                open.push({dist[local(n)] + (std::abs(nx - tx) + std::abs(ny - ty)) * 0.1, n});
            }
        }
    }
    std::vector<int> path;
    if (!std::isfinite(dist[local(to)])) return path;
    for (int c = to; c != -1; c = prev[local(c)]) path.push_back(c);
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<int> Solver::AStar(int from, int to, const std::function<double(int)>& cost) {
    std::vector<double> dist(W * H, std::numeric_limits<double>::infinity());
    std::vector<int> prev(W * H, -1);
    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> open;
    const int tx = to % W, ty = to / W;
    dist[from] = 0;
    open.push({0, from});
    while (!open.empty()) {
        const auto [f, c] = open.top();
        open.pop();
        if (c == to) break;
        const int x = c % W, y = c / W;
        const double g = dist[c];
        if (f - (std::abs(x - tx) + std::abs(y - ty)) * 0.1 > g + 1e-9) continue;
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
            const int nx = x + dx[k], ny = y + dy[k];
            if (!In(nx, ny)) continue;
            const int n = Cell(nx, ny);
            const double step = cost(n);
            if (!std::isfinite(step)) continue;
            if (g + step < dist[n]) {
                dist[n] = g + step;
                prev[n] = c;
                open.push({dist[n] + (std::abs(nx - tx) + std::abs(ny - ty)) * 0.1, n});
            }
        }
    }
    std::vector<int> path;
    if (!std::isfinite(dist[to])) return path;
    for (int c = to; c != -1; c = prev[c]) path.push_back(c);
    std::reverse(path.begin(), path.end());
    return path;
}

void Solver::MarkRoad(const std::vector<int>& cells, int width, uint8_t rank, int container) {
    const int r = std::max(0, (width - 1) / 2), extra = std::max(0, width - 1 - r);
    for (int c : cells) {
        const int x = c % W, y = c / W;
        for (int dy = -r; dy <= extra; ++dy) {
            for (int dx = -r; dx <= extra; ++dx) {
                const int nx = x + dx, ny = y + dy;
                if (!In(nx, ny)) continue;
                const int n = Cell(nx, ny);
                if (container >= 0 && owner_[n] != container && !road_[n]) continue;
                if (occ_[kBuilding][n]) continue;
                road_[n] = std::max(road_[n], rank);
            }
        }
    }
}

uint8_t RankOf(const std::string& cls) {
    if (cls == "main") return 4;
    if (cls == "street") return 3;
    if (cls == "lane") return 2;
    return 1;
}

void Solver::Path(const json& s) {
    const std::string cls = s.value("class", "street");
    Rng rng = StreamFor(s);
    std::vector<std::string> stops = {s.value("from", "")};
    for (const auto& v : s.value("via", json::array())) {
        if (v.is_string()) stops.push_back(v.get<std::string>());
    }
    stops.push_back(s.value("to", ""));
    const bool river = cls == "river";
    const double reuse = s.value("reuse", 0.1);
    // A little noise per cell makes rivers meander and old roads wander.
    std::vector<float> noise(W * H);
    for (auto& n : noise) n = static_cast<float>(rng.Unit());
    const double wiggle = river ? 3.0 : s.value("wiggle", 0.3);
    auto cost = [&](int c) -> double {
        if (occ_[kBuilding][c]) return std::numeric_limits<double>::infinity();
        if (river) return 1.0 + wiggle * noise[c];
        if (water_[c]) return 6.0; // a bridge
        if (road_[c]) return reuse;
        return 1.0 + wiggle * noise[c];
    };
    PathResult result;
    result.id = s.value("id", "");
    result.node = s.value("node", "");
    result.cls = cls;
    result.terrain = s.value("terrain", river ? "water" : "dirt");
    result.edge = s.value("edge", "");
    result.width = std::max(1, s.value("width", 1));
    for (size_t i = 0; i + 1 < stops.size(); ++i) {
        const auto a = Cells(stops[i]), b = Cells(stops[i + 1]);
        if (a.empty() || b.empty()) {
            Problem("E-REF", s, "can't find " + (a.empty() ? stops[i] : stops[i + 1]));
            return;
        }
        auto leg = AStar(a[a.size() / 2], b[b.size() / 2], cost);
        if (leg.empty()) {
            Problem("S-UNSAT", s, "no way from " + stops[i] + " to " + stops[i + 1]);
            return;
        }
        if (!result.cells.empty() && leg.front() == result.cells.back()) leg.erase(leg.begin());
        result.cells.insert(result.cells.end(), leg.begin(), leg.end());
    }
    if (river) {
        const int r = std::max(0, (result.width - 1) / 2), extra = std::max(0, result.width - 1 - r);
        for (int c : result.cells) {
            for (int dy = -r; dy <= extra; ++dy) {
                for (int dx = -r; dx <= extra; ++dx) {
                    if (In(c % W + dx, c / W + dy)) water_[Cell(c % W + dx, c / W + dy)] = 1;
                }
            }
        }
    } else {
        MarkRoad(result.cells, result.width, RankOf(cls), -1);
    }
    paths_.push_back(result);
    instancesOf_[result.id] = 1;
}

void Solver::Network(const json& s, int region) {
    const Region& r = regions_[region];
    const std::string style = s.value("style", "organic");
    const int spacing = std::max(4, s.value("spacing", 10));
    const uint8_t rank = RankOf(s.value("class", "street"));
    const int width = std::max(1, s.value("width", 1)); // a city's streets: lanes and their sidewalks (Compile's network hint)
    Rng rng = StreamFor(s);
    std::vector<int> streets;
    auto inZone = [&](int c) { return owner_[c] == region && !water_[c]; };
    const std::vector<uint8_t> before = road_; // the roads there were before this network
    auto connectToRoads = [&](int from, bool existingOnly = false) {
        // The nearest road cell (in or around the zone), by search.
        std::vector<int> seen(W * H, 0);
        std::deque<int> todo = {from};
        seen[from] = 1;
        int target = -1;
        while (!todo.empty() && target < 0) {
            const int c = todo.front();
            todo.pop_front();
            if ((existingOnly ? before[c] : road_[c]) && c != from) target = c;
            static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k) {
                const int nx = c % W + dx[k], ny = c / W + dy[k];
                if (!In(nx, ny) || seen[Cell(nx, ny)] || water_[Cell(nx, ny)]) continue;
                seen[Cell(nx, ny)] = 1;
                todo.push_back(Cell(nx, ny));
            }
        }
        if (target < 0) return std::vector<int>{};
        return AStar(from, target, [&](int c) -> double {
            if (water_[c] || occ_[kBuilding][c]) return std::numeric_limits<double>::infinity();
            if (road_[c]) return 0.1;
            return inZone(c) ? 1.0 + 0.5 * rng.Unit() : 4.0;
        });
    };
    // Lanes start where the zone is furthest from any street, one at a time,
    // until every cell is within half a block of one. Each heads for the
    // nearest street, but running alongside one costs: lanes meet streets,
    // they don't shadow them (the land between is for plots). Every style but
    // the grid ends with it: a linear ward far from the main road still gets lanes.
    auto serveEveryCell = [&] {
        const int half = std::max(2, spacing / 2);
        const int maxLanes = std::max(1, static_cast<int>(4 * r.cells.size() / (spacing * spacing)));
        // The zone's window (a margin for the roads round it): the distance field and the lanes' search stay in it.
        const int wx0 = std::max(0, r.x0 - 2 * spacing), wy0 = std::max(0, r.y0 - 2 * spacing);
        const int wx1 = std::min(W, r.x1 + 2 * spacing), wy1 = std::min(H, r.y1 + 2 * spacing);
        auto inWindow = [&](int x, int y) { return x >= wx0 && y >= wy0 && x < wx1 && y < wy1; };
        std::vector<float> tie(W * H, 0.0f);
        for (int c : r.cells) tie[c] = static_cast<float>(rng.Unit());
        // Each cell's distance to the nearest road: from the roads there are, then from each new lane
        // (distances only ever shrink, so a lane's cells are relaxed from, not the whole field again).
        std::vector<int> near(W * H, 1 << 20);
        std::deque<int> todo;
        auto relax = [&] {
            while (!todo.empty()) {
                const int c = todo.front();
                todo.pop_front();
                for (const auto& [ddx, ddy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    const int nx = c % W + ddx, ny = c / W + ddy;
                    if (!inWindow(nx, ny) || near[Cell(nx, ny)] <= near[c] + 1) continue;
                    near[Cell(nx, ny)] = near[c] + 1;
                    todo.push_back(Cell(nx, ny));
                }
            }
        };
        for (int y = wy0; y < wy1; ++y) {
            for (int x = wx0; x < wx1; ++x) {
                if (road_[Cell(x, y)]) {
                    near[Cell(x, y)] = 0;
                    todo.push_back(Cell(x, y));
                }
            }
        }
        relax();
        for (int laneCount = 0; laneCount < maxLanes; ++laneCount) {
            int target = -1;
            int p = -1;
            for (int c : r.cells) {
                if (!inZone(c) || road_[c]) continue;
                if (p < 0 || near[c] + tie[c] > near[p] + tie[p]) p = c;
            }
            if (p < 0 || near[p] < half) break; // every cell is served by a street
            // The nearest road cell from p: walk down the distance field.
            target = p;
            for (int guard = 0; guard < W * H && near[target] > 0; ++guard) {
                int next = target;
                for (const auto& [ddx, ddy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    const int nx = target % W + ddx, ny = target / W + ddy;
                    if (In(nx, ny) && near[Cell(nx, ny)] < near[next]) next = Cell(nx, ny);
                }
                if (next == target) break;
                target = next;
            }
            auto lane = AStarIn(p, target, [&](int c) -> double {
                if (water_[c] || occ_[kBuilding][c]) return std::numeric_limits<double>::infinity();
                if (road_[c]) return 0.1;
                const double alongside = near[c] < half ? 3.0 * (half - near[c]) / half : 0.0;
                return (inZone(c) ? 1.0 : 4.0) + 0.6 * rng.Unit() + alongside;
            }, wx0, wy0, wx1, wy1);
            if (lane.empty()) break;
            MarkRoad(lane, width, rank, -1);
            streets.insert(streets.end(), lane.begin(), lane.end());
            for (int c = 0; c < static_cast<int>(lane.size()); ++c) {
                // The lane's cells (and what MarkRoad widened round them) are roads now.
                const int x = lane[c] % W, y = lane[c] / W;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!inWindow(x + dx, y + dy)) continue;
                        const int n = Cell(x + dx, y + dy);
                        if (road_[n] && near[n] != 0) {
                            near[n] = 0;
                            todo.push_back(n);
                        }
                    }
                }
            }
            relax();
        }
    };
    if (style == "grid") {
        // A shared lattice (the layout form's street-grid "shared"): the same lines in every zone, so streets run
        // on across zones and meet the main roads squarely. Else this zone's own grid.
        const json lattice = s.value("lattice", json());
        const bool shared = lattice.is_object() && lattice.contains("origin") && lattice.contains("block");
        const int ox = shared ? lattice["origin"][0].get<int>() : r.x0 + rng.Range(0, spacing / 2);
        const int oy = shared ? lattice["origin"][1].get<int>() : r.y0 + rng.Range(0, spacing / 2);
        const int sx = shared ? std::max(4, lattice["block"][0].get<int>()) : spacing, sy = shared ? std::max(4, lattice["block"][1].get<int>()) : spacing;
        for (int c : r.cells) {
            const int x = c % W, y = c / W;
            if (((x - ox) % sx + sx) % sx == 0 || ((y - oy) % sy + sy) % sy == 0) streets.push_back(c);
        }
        MarkRoad(streets, width, rank, region);
        if (!streets.empty()) {
            auto link = connectToRoads(streets[streets.size() / 2]);
            MarkRoad(link, width, rank, -1);
            streets.insert(streets.end(), link.begin(), link.end());
        }
    } else if (style == "radial" && s.value("lattice", json()).is_object()) {
        // A shared radial network (street-grid "shared"): spokes from the place's centre and rings at each block's
        // distance, the same in every zone, so they run on across zones.
        const json lattice = s["lattice"];
        const double cx = lattice["origin"][0].get<double>(), cy = lattice["origin"][1].get<double>();
        const double step = std::max(6, lattice["block"][0].get<int>());
        const double radius = std::hypot(W, H);
        const int spokes = 8;
        for (int k = 0; k < spokes; ++k) {
            const double a = k * 2 * 3.14159265 / spokes;
            for (double t = 0; t < radius; t += 0.5) {
                const int x = static_cast<int>(cx + std::cos(a) * t), y = static_cast<int>(cy + std::sin(a) * t);
                if (In(x, y) && inZone(Cell(x, y))) streets.push_back(Cell(x, y));
            }
        }
        for (double ring = step; ring < radius; ring += step) {
            for (double a = 0; a < 2 * 3.14159265; a += 0.5 / ring) {
                const int x = static_cast<int>(cx + std::cos(a) * ring), y = static_cast<int>(cy + std::sin(a) * ring);
                if (In(x, y) && inZone(Cell(x, y))) streets.push_back(Cell(x, y));
            }
        }
        MarkRoad(streets, width, rank, region);
        if (!streets.empty()) {
            auto link = connectToRoads(streets.front());
            MarkRoad(link, width, rank, -1);
        }
    } else if (style == "radial") {
        const double cx = (r.x0 + r.x1) / 2.0, cy = (r.y0 + r.y1) / 2.0;
        const double radius = std::max(r.x1 - r.x0, r.y1 - r.y0);
        const int spokes = 6;
        const double turn = rng.Unit();
        for (int k = 0; k < spokes; ++k) {
            const double a = (k + turn) * 2 * 3.14159265 / spokes;
            for (double t = 0; t < radius; t += 0.5) {
                const int x = static_cast<int>(cx + std::cos(a) * t), y = static_cast<int>(cy + std::sin(a) * t);
                if (In(x, y) && inZone(Cell(x, y))) streets.push_back(Cell(x, y));
            }
        }
        const double ring = std::sqrt(r.cells.size() / 3.14159265) * 0.55;
        for (double a = 0; a < 2 * 3.14159265; a += 0.5 / std::max(1.0, ring)) {
            const int x = static_cast<int>(cx + std::cos(a) * ring), y = static_cast<int>(cy + std::sin(a) * ring);
            if (In(x, y) && inZone(Cell(x, y))) streets.push_back(Cell(x, y));
        }
        MarkRoad(streets, width, rank, region);
        if (!streets.empty()) {
            auto link = connectToRoads(streets.front());
            MarkRoad(link, width, rank, -1);
        }
        serveEveryCell();
    } else if (style == "linear-along-road" || style == "linear-along-river" || style == "linear") {
        // Lanes off the main roads crossing the zone, every `spacing` cells.
        std::vector<int> mains;
        for (int c : r.cells) {
            if (road_[c] >= 3) mains.push_back(c);
        }
        for (int y = r.y0; y < r.y1; ++y) {
            for (int x = r.x0; x < r.x1; ++x) {
                const int c = Cell(x, y);
                if (!inZone(c) || road_[c]) continue;
                bool nearMain = false;
                for (int k = 1; k <= spacing / 2 && !nearMain; ++k) {
                    for (const auto& [dx, dy] : {std::pair{k, 0}, std::pair{-k, 0}, std::pair{0, k}, std::pair{0, -k}}) {
                        if (In(x + dx, y + dy) && road_[Cell(x + dx, y + dy)] >= 3) nearMain = true;
                    }
                }
                if (nearMain && ((x + y) % (spacing * 2) == 0)) streets.push_back(c);
            }
        }
        MarkRoad(streets, width, rank, region);
        if (mains.empty() && !r.cells.empty()) MarkRoad(connectToRoads(r.cells[r.cells.size() / 2]), width, rank, -1);
        serveEveryCell();
    } else {
        // Organic: a winding main street along the zone's long axis, joined to
        // the roads, then lanes grown toward it from points spread over the zone.
        const bool wide = (r.x1 - r.x0) >= (r.y1 - r.y0);
        auto nearestInZone = [&](double tx, double ty) {
            int best = -1;
            double bestD = 1e18;
            for (int c : r.cells) {
                if (!inZone(c)) continue;
                const double d = std::hypot(c % W - tx, c / W - ty);
                if (d < bestD) {
                    bestD = d;
                    best = c;
                }
            }
            return best;
        };
        const double my = (r.y0 + r.y1) / 2.0, mx = (r.x0 + r.x1) / 2.0;
        const int a = wide ? nearestInZone(r.x0, my) : nearestInZone(mx, r.y0);
        const int b = wide ? nearestInZone(r.x1 - 1, my) : nearestInZone(mx, r.y1 - 1);
        if (a >= 0 && b >= 0 && a != b) {
            std::vector<float> noise(W * H);
            for (auto& n : noise) n = static_cast<float>(rng.Unit());
            auto spine = AStar(a, b, [&](int c) -> double {
                if (water_[c] || occ_[kBuilding][c]) return std::numeric_limits<double>::infinity();
                if (road_[c]) return 0.2;
                return inZone(c) ? 1.0 + 1.5 * noise[c] : std::numeric_limits<double>::infinity();
            });
            MarkRoad(spine, width, rank, region);
            streets.insert(streets.end(), spine.begin(), spine.end());
            if (!spine.empty()) {
                auto link = connectToRoads(spine[spine.size() / 2], true);
                MarkRoad(link, width, rank, -1);
            }
        }
        serveEveryCell();
    }
    PathResult result;
    result.id = s.value("id", "");
    result.node = s.value("node", "");
    result.cls = s.value("class", "street");
    result.terrain = s.value("terrain", "dirt");
    result.edge = s.value("edge", "");
    for (int c : r.cells) {
        if (road_[c]) result.cells.push_back(c);
    }
    paths_.push_back(result);
    instancesOf_[result.id] = 1;
}

// --- Areas ---------------------------------------------------------------------------------------

void Solver::Partition(const json& s, int region) {
    const Region parent = regions_[region];
    struct Zone {
        std::string id, node, module;
        double quota = 0;
        bool flexible = false; // no buildings: takes what's left
        json prefers;
        json rel;          // [{type, to}]: near the river, south of the docks
        int keep = -1;     // the cell it was seeded at last time, to stay there
        int seed = -1;
        std::vector<int> cells;
        std::array<int, 4> box{-1, -1, -1, -1}; // drawn by the user (x0, y0, x1, y1): it takes that land, exactly
        std::vector<int> painted;                // painted by the user: those cells, exactly
        std::vector<uint8_t> sketched;           // the AI's sketch of where it goes (a guide, not its border)
    };
    std::vector<uint8_t> anySketch(W * H, 0);
    std::vector<Zone> zones;
    double wanted = 0;
    for (const auto& z : s.value("into", json::array())) {
        Zone zone;
        zone.id = z.value("id", "");
        zone.node = z.value("node", zone.id);
        zone.module = z.value("module", "");
        zone.quota = RangeOf(z.value("area", json()), 400).lo > 0 ? (RangeOf(z.value("area", json()), 400).lo + RangeOf(z.value("area", json()), 400).hi) / 2.0 : 400;
        zone.prefers = z.value("prefers", json::object());
        zone.flexible = z.value("flexible", false);
        zone.rel = z.value("rel", json::array());
        if (const json k = z.value("seed", json()); k.is_array() && k.size() == 2 && In(k[0].get<int>(), k[1].get<int>())) {
            zone.keep = Cell(k[0].get<int>(), k[1].get<int>());
        }
        if (const json b = z.value("box", json()); b.is_array() && b.size() == 4) {
            zone.box = {std::max(0, b[0].get<int>()), std::max(0, b[1].get<int>()), std::min(W, b[2].get<int>()), std::min(H, b[3].get<int>())};
            zone.quota = std::max(1, (zone.box[2] - zone.box[0]) * (zone.box[3] - zone.box[1]));
        }
        if (const json paint = z.value("paint", json()); paint.is_array() && !paint.empty()) {
            for (int c : DecodeCells(paint, W)) {
                if (c >= 0 && c < W * H) zone.painted.push_back(c);
            }
            zone.box = {0, 0, 0, 0}; // drawn: it doesn't grow
            zone.quota = std::max<double>(1, zone.painted.size());
        }
        if (const json sketch = z.value("sketch", json()); sketch.is_array() && !sketch.empty() && zone.painted.empty()) {
            zone.sketched.assign(W * H, 0);
            for (int c : DecodeCells(sketch, W)) {
                if (c >= 0 && c < W * H) zone.sketched[c] = anySketch[c] = 1;
            }
        }
        wanted += zone.quota;
        zones.push_back(zone);
    }
    std::vector<int> available;
    for (int c : parent.cells) {
        if (owner_[c] == region && !road_[c] && !water_[c] && !occ_[kBuilding][c]) available.push_back(c);
    }
    if (zones.empty() || available.empty()) return;
    // Quotas scaled to what's there (open land is left between zones). Zones
    // without buildings (farmland, a green) give way first: the wards' parcels
    // need their land, a field can be smaller.
    const double room = 0.92 * available.size();
    if (wanted > room) {
        double built = 0, flexible = 0;
        for (const auto& z : zones) (z.flexible ? flexible : built) += z.quota;
        if (built <= room) {
            const double left = room - built;
            for (auto& z : zones) {
                if (z.flexible) z.quota *= flexible > 0 ? left / flexible : 0;
            }
        } else {
            const double keep = 0.1 * room; // a little for the fields, the rest for the wards
            for (auto& z : zones) z.quota *= z.flexible ? (flexible > 0 ? keep / flexible : 0) : (room - (flexible > 0 ? keep : 0)) / built;
        }
    }
    // Planned zones (the layout form's zone-shape "blocks"): rectangles cut along the main roads (rows and
    // columns that are mostly road), each sliced among the zones that go there, the central ones to the zones
    // that prefer the centre; cuts on the shared street lattice. Each zone then takes its rectangle as if
    // drawn. Zones left over (more zones than rectangles) grow as usual.
    if (s.value("method", "") == "blocks") {
        int bx0 = W, by0 = H, bx1 = 0, by1 = 0;
        std::vector<uint8_t> avail(W * H, 0);
        for (int c : available) {
            avail[c] = 1;
            bx0 = std::min(bx0, c % W), by0 = std::min(by0, c / W), bx1 = std::max(bx1, c % W + 1), by1 = std::max(by1, c / W + 1);
        }
        auto mostlyRoad = [&](int a, bool row) {
            int n = 0, r = 0;
            for (int b = row ? bx0 : by0; b < (row ? bx1 : by1); ++b) {
                const int c = row ? Cell(b, a) : Cell(a, b);
                ++n;
                r += road_[c] != 0;
            }
            return n > 0 && r * 10 >= n * 6;
        };
        auto spans = [&](int a, int b, bool row) {
            std::vector<std::pair<int, int>> out;
            int start = -1;
            for (int i = a; i < b; ++i) {
                if (mostlyRoad(i, row)) {
                    if (start >= 0) out.push_back({start, i});
                    start = -1;
                } else if (start < 0) {
                    start = i;
                }
            }
            if (start >= 0) out.push_back({start, b});
            return out;
        };
        struct Rect {
            int x0, y0, x1, y1;
            double area, centre;
        };
        std::vector<Rect> rects;
        for (const auto& [ry0, ry1] : spans(by0, by1, true)) {
            for (const auto& [rx0, rx1] : spans(bx0, bx1, false)) {
                int area = 0;
                for (int y = ry0; y < ry1; ++y) {
                    for (int x = rx0; x < rx1; ++x) area += avail[Cell(x, y)];
                }
                if (area >= 16) rects.push_back({rx0, ry0, rx1, ry1, static_cast<double>(area), std::hypot((rx0 + rx1) / 2.0 - W / 2.0, (ry0 + ry1) / 2.0 - H / 2.0)});
            }
        }
        std::sort(rects.begin(), rects.end(), [](const Rect& a, const Rect& b) { return a.centre < b.centre; });
        std::vector<size_t> order;
        for (size_t k = 0; k < zones.size(); ++k) {
            if (zones[k].box[0] < 0 && zones[k].painted.empty()) order.push_back(k);
        }
        auto centreWish = [&](size_t k) { return zones[k].prefers.value("centre", 0.0) - zones[k].prefers.value("edge", 0.0); };
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (zones[a].flexible != zones[b].flexible) return !zones[a].flexible;
            if (centreWish(a) != centreWish(b)) return centreWish(a) > centreWish(b);
            return zones[a].quota > zones[b].quota;
        });
        double total = 0, capacity = 0;
        for (size_t k : order) total += zones[k].quota;
        for (const auto& r : rects) capacity += r.area;
        const bool cover = s.value("cover", false);
        if (total > 0 && (cover || total > capacity)) {
            for (size_t k : order) zones[k].quota *= capacity / total;
        }
        const json lattice = s.value("lattice", json());
        const int lox = lattice.is_object() ? lattice["origin"][0].get<int>() : 0, loy = lattice.is_object() ? lattice["origin"][1].get<int>() : 0;
        const int lbw = lattice.is_object() ? lattice["block"][0].get<int>() : 0, lbh = lattice.is_object() ? lattice["block"][1].get<int>() : 0;
        auto snap = [](int v, int origin, int step, int lo, int hi) {
            if (step <= 0) return v;
            const int k = static_cast<int>(std::lround((v - origin) / static_cast<double>(step)));
            const int snapped = origin + k * step;
            return snapped > lo && snapped < hi ? snapped : v;
        };
        size_t next = 0;
        for (const auto& r : rects) {
            if (next >= order.size()) break;
            std::vector<size_t> group;
            double left = r.area, sum = 0;
            while (next < order.size()) {
                const double q = zones[order[next]].quota;
                if (!group.empty() && q > left * 1.3) break;
                group.push_back(order[next]);
                sum += q;
                left -= q;
                ++next;
                if (left <= r.area * 0.05) break;
            }
            // The last rectangle takes whoever is left.
            if (&r == &rects.back()) {
                while (next < order.size()) group.push_back(order[next]), sum += zones[order[next++]].quota;
            }
            const bool alongX = (r.x1 - r.x0) >= (r.y1 - r.y0);
            const int a = alongX ? r.x0 : r.y0, b = alongX ? r.x1 : r.y1;
            const int end = cover ? b : a + static_cast<int>(std::lround((b - a) * std::min(1.0, sum / r.area)));
            int pos = a;
            double acc = 0;
            for (size_t i = 0; i < group.size(); ++i) {
                acc += zones[group[i]].quota;
                int cut = i + 1 == group.size() ? end : a + static_cast<int>(std::lround((end - a) * acc / std::max(1.0, sum)));
                if (i + 1 < group.size()) cut = snap(cut, alongX ? lox : loy, alongX ? lbw : lbh, pos, end);
                cut = std::max(cut, pos + 1);
                zones[group[i]].box = alongX ? std::array<int, 4>{pos, r.y0, cut, r.y1} : std::array<int, 4>{r.x0, pos, r.x1, cut};
                pos = cut;
            }
        }
    }
    // Interest of each cell for each zone (Emilien et al.): its site preferences.
    std::vector<int> nearRoad(W * H, 1 << 20), nearWater(W * H, 1 << 20);
    auto distanceField = [&](std::vector<int>& field, const std::function<bool(int)>& source) {
        std::deque<int> todo;
        for (int c = 0; c < W * H; ++c) {
            if (source(c)) {
                field[c] = 0;
                todo.push_back(c);
            }
        }
        while (!todo.empty()) {
            const int c = todo.front();
            todo.pop_front();
            static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k) {
                const int nx = c % W + dx[k], ny = c / W + dy[k];
                if (!In(nx, ny) || field[Cell(nx, ny)] <= field[c] + 1) continue;
                field[Cell(nx, ny)] = field[c] + 1;
                todo.push_back(Cell(nx, ny));
            }
        }
    };
    distanceField(nearRoad, [&](int c) { return road_[c] >= 3; });
    distanceField(nearWater, [&](int c) { return water_[c] != 0; });
    const double cx = (parent.x0 + parent.x1) / 2.0, cy = (parent.y0 + parent.y1) / 2.0;
    const double half = std::max(1.0, std::hypot(parent.x1 - parent.x0, parent.y1 - parent.y0) / 2.0);
    // What a relation names: another zone (by its seed, once placed) or a path
    // (the river, a road: by the distance to it).
    std::map<std::string, size_t> zoneIndex;
    for (size_t k = 0; k < zones.size(); ++k) zoneIndex[zones[k].id] = k;
    std::map<std::string, std::vector<int>> pathDistance;
    auto distanceToPath = [&](const std::string& name) -> const std::vector<int>* {
        if (auto it = pathDistance.find(name); it != pathDistance.end()) return it->second.empty() ? nullptr : &it->second;
        std::vector<int> field;
        std::set<int> cells;
        for (const auto& p : paths_) {
            if (p.id == name || p.node == name || (name == "river" && p.cls == "river") || (name == "road" && p.cls == "main")) {
                cells.insert(p.cells.begin(), p.cells.end());
            }
        }
        if (!cells.empty()) {
            field.assign(W * H, 1 << 20);
            std::deque<int> todo;
            for (int c : cells) {
                field[c] = 0;
                todo.push_back(c);
            }
            while (!todo.empty()) {
                const int c = todo.front();
                todo.pop_front();
                for (const auto& [ddx, ddy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    const int nx = c % W + ddx, ny = c / W + ddy;
                    if (!In(nx, ny) || field[Cell(nx, ny)] <= field[c] + 1) continue;
                    field[Cell(nx, ny)] = field[c] + 1;
                    todo.push_back(Cell(nx, ny));
                }
            }
        }
        auto& stored = pathDistance[name] = std::move(field);
        return stored.empty() ? nullptr : &stored;
    };
    std::map<std::string, std::pair<std::map<int, double>, std::map<int, double>>> pathAxes; // name -> (x by row, y by column)
    auto axesOf = [&](const std::string& name) -> const std::pair<std::map<int, double>, std::map<int, double>>& {
        if (auto it = pathAxes.find(name); it != pathAxes.end()) return it->second;
        std::map<int, std::pair<double, int>> rows, cols;
        for (const auto& p : paths_) {
            if (!(p.id == name || p.node == name || (name == "river" && p.cls == "river"))) continue;
            for (int pc : p.cells) {
                rows[pc / W].first += pc % W, rows[pc / W].second++;
                cols[pc % W].first += pc / W, cols[pc % W].second++;
            }
        }
        auto& axes = pathAxes[name];
        for (const auto& [row, sum] : rows) axes.first[row] = sum.first / sum.second;
        for (const auto& [col, sum] : cols) axes.second[col] = sum.first / sum.second;
        return axes;
    };
    // The nearest entry of an axis map (a path's x at the nearest row it crosses).
    auto nearestOf = [](const std::map<int, double>& axis, int at, double fallback) {
        if (axis.empty()) return fallback;
        auto it = axis.lower_bound(at);
        if (it == axis.end()) return std::prev(it)->second;
        if (it == axis.begin() || it->first == at) return it->second;
        auto before = std::prev(it);
        return at - before->first <= it->first - at ? before->second : it->second;
    };
    auto interest = [&](const Zone& z, int c) {
        const double x = c % W + 0.5, y = c / W + 0.5;
        double score = 0;
        // The AI's sketch: its own sketched land draws it, another zone's sketched land doesn't.
        if (!z.sketched.empty()) score += z.sketched[c] ? 4.0 : anySketch[c] ? -2.0 : -0.5;
        // Its relations weigh more than its general preferences: someone asked for them.
        for (const auto& r : z.rel) {
            const std::string type = r.value("type", "near"), to = r.value("to", "");
            // In a part of the map (its north, its west): by where the cell is in the site.
            if (type.rfind("in-", 0) == 0) {
                const double w = std::max(1.0, (parent.x1 - parent.x0) / 2.0), h = std::max(1.0, (parent.y1 - parent.y0) / 2.0);
                const double fx = (x - cx) / w, fy = (y - cy) / h; // -1..1 from the middle
                double v = 0;
                if (type == "in-north") v = -fy;
                else if (type == "in-south") v = fy;
                else if (type == "in-west") v = -fx;
                else if (type == "in-east") v = fx;
                else continue;
                score += std::max(0.1, r.value("weight", 1.0)) * 3.0 * std::clamp(v * 1.5, -1.0, 1.0);
                continue;
            }
            double tx = 0, ty = 0, distance = 0;
            bool known = false, path = false;
            const double weight = std::max(0.1, r.value("weight", 1.0));
            if (auto it = zoneIndex.find(to); it != zoneIndex.end() && zones[it->second].seed >= 0 && it->second != static_cast<size_t>(&z - &zones[0])) {
                const int seed = zones[it->second].seed;
                tx = seed % W + 0.5, ty = seed / W + 0.5;
                distance = std::hypot(x - tx, y - ty);
                known = true;
            } else if (const std::vector<int>* field = distanceToPath(to)) {
                distance = (*field)[c];
                // A direction from a path: from its nearest point, along the field's gradient isn't kept, so its middle.
                double sx = 0, sy = 0;
                int n = 0;
                for (const auto& p : paths_) {
                    if (p.id == to || p.node == to || (to == "river" && p.cls == "river")) {
                        for (int pc : p.cells) sx += pc % W, sy += pc / W, ++n;
                    }
                }
                tx = n ? sx / n : x, ty = n ? sy / n : y;
                // A bank: west or east of the river where this cell is, not of its middle.
                const auto& axes = axesOf(to);
                tx = nearestOf(axes.first, c / W, tx) + 0.5;
                ty = nearestOf(axes.second, c % W, ty) + 0.5;
                known = true;
                path = true;
            }
            if (!known) continue;
            double v = 0;
            const double reach = std::max(4.0, half * 0.35);
            // Along a path is a strip beside it: a few cells, not the zone's reach.
            if (type == "along" || (path && type == "adjacent")) v = 1 - std::min(2.0, distance / 5.0);
            else if (type == "near" || type == "adjacent" || type == "along" || type == "beside") v = 1 - std::min(2.0, distance / reach);
            else if (type == "far" || type == "away-from") v = std::min(1.0, distance / half) * 2 - 1;
            else if (type == "south-of") v = std::clamp((y - ty) / reach, -1.0, 1.0);
            else if (type == "north-of") v = std::clamp((ty - y) / reach, -1.0, 1.0);
            else if (type == "east-of") v = std::clamp((x - tx) / reach, -1.0, 1.0);
            else if (type == "west-of") v = std::clamp((tx - x) / reach, -1.0, 1.0);
            else continue;
            // A direction still wants it close by (south of the docks, not the far
            // south), and weighs more than a "near": on the wrong side is a miss.
            if (type.ends_with("-of")) {
                const double side = v;
                v = 0.7 * v + 0.3 * (1 - std::min(2.0, distance / (2 * reach)));
                score += weight * (3.0 * v - (side < 0 ? 3.0 : 0.0));
                continue;
            }
            score += weight * 2.0 * v;
        }
        for (const auto& [criterion, weightJson] : z.prefers.items()) {
            const double weight = weightJson.is_number() ? weightJson.get<double>() : 0;
            double v = 0; // in [-1, 1]
            if (criterion == "centre" || criterion == "center") v = 1 - 2 * std::hypot(x - cx, y - cy) / half;
            else if (criterion == "edge") v = 2 * std::hypot(x - cx, y - cy) / half - 1;
            else if (criterion == "main-road") v = 1 - std::min(2.0, nearRoad[c] / 8.0);
            else if (criterion == "water" || criterion == "downstream") v = 1 - std::min(2.0, nearWater[c] / 10.0);
            else if (criterion == "high-ground") v = 1 - 2 * (y - parent.y0) / std::max(1, parent.y1 - parent.y0); // north stands for high ground until there's a height map
            score += weight * std::clamp(v, -1.0, 1.0);
        }
        return score;
    };
    // Seeds: the zones with the strongest preferences choose first, apart from each other.
    std::vector<size_t> order(zones.size());
    std::iota(order.begin(), order.end(), 0);
    auto strength = [&](const Zone& z) {
        double s = 0;
        for (const auto& [k, v] : z.prefers.items()) s += v.is_number() ? std::abs(v.get<double>()) : 0;
        return s;
    };
    auto dependsOnZone = [&](const Zone& z) {
        for (const auto& r : z.rel) {
            if (zoneIndex.contains(r.value("to", ""))) return true;
        }
        return false;
    };
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        // Kept seeds first, then zones free of other zones, then the strongest preferences.
        if ((zones[a].keep >= 0) != (zones[b].keep >= 0)) return zones[a].keep >= 0;
        if (dependsOnZone(zones[a]) != dependsOnZone(zones[b])) return !dependsOnZone(zones[a]);
        return strength(zones[a]) > strength(zones[b]);
    });
    std::vector<uint8_t> open(W * H, 0);
    for (int c : available) open[c] = 1;
    Rng rng = StreamFor(s);
    const double minApart = std::sqrt(room / zones.size()) * 0.8;
    // The land drawn, painted or kept for zones is theirs before any seed is placed; a seed goes
    // on the free land that's left, in a stretch of it big enough for its zone (not a pocket shut
    // in by kept zones, the river and a road).
    std::vector<uint8_t> claimed(W * H, 0);
    for (const auto& z : zones) {
        for (int c : z.painted) claimed[c] = 1;
        if (z.box[0] >= 0) {
            for (int y = z.box[1]; y < z.box[3]; ++y) {
                for (int x = z.box[0]; x < z.box[2]; ++x) claimed[Cell(x, y)] = 1;
            }
        }
    }
    std::vector<int> stretch(W * H, -1);
    std::vector<int> stretchSize;
    for (int start : available) {
        if (claimed[start] || stretch[start] >= 0) continue;
        const int label = static_cast<int>(stretchSize.size());
        stretchSize.push_back(0);
        std::vector<int> stack = {start};
        stretch[start] = label;
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            ++stretchSize[label];
            static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; ++d) {
                int nx = c % W + dx[d], ny = c / W + dy[d];
                for (int hop = 0; hop < 4 && In(nx, ny) && road_[Cell(nx, ny)]; ++hop) nx += dx[d], ny += dy[d]; // across a road, as growth does
                if (!In(nx, ny)) continue;
                const int n = Cell(nx, ny);
                if (!open[n] || claimed[n] || stretch[n] >= 0) continue;
                stretch[n] = label;
                stack.push_back(n);
            }
        }
    }
    const int widest = stretchSize.empty() ? 0 : *std::max_element(stretchSize.begin(), stretchSize.end());
    auto seedable = [&](const Zone& z, int c) {
        if (claimed[c] || stretch[c] < 0) return false;
        return stretchSize[stretch[c]] >= std::min(0.7 * z.quota, 0.9 * widest);
    };
    for (size_t k : order) {
        if (zones[k].box[0] >= 0) continue; // drawn or painted: its seed is its land's
        // Where it was last time, if that's still open land (a revision keeps the zones it isn't about).
        if (zones[k].keep >= 0) {
            int best = -1;
            double nearest = 1e18;
            for (int c : available) {
                if (!seedable(zones[k], c)) continue;
                const double d = std::hypot(c % W - zones[k].keep % W, c / W - zones[k].keep / W);
                if (d < nearest) nearest = d, best = c;
                if (d == 0) break;
            }
            zones[k].seed = best;
            continue;
        }
        // Sketched: from the middle of its sketch (the open cell of it nearest its centre).
        if (!zones[k].sketched.empty()) {
            double sx = 0, sy = 0, n = 0;
            for (int c = 0; c < W * H; ++c) {
                if (zones[k].sketched[c]) sx += c % W, sy += c / W, ++n;
            }
            int best = -1;
            double nearest = 1e18;
            for (int c : available) {
                if (!seedable(zones[k], c)) continue;
                const double d = std::hypot(c % W - sx / std::max(1.0, n), c / W - sy / std::max(1.0, n)) - (zones[k].sketched[c] ? 1000 : 0);
                bool used = false;
                for (const auto& z : zones) used = used || z.seed == c;
                if (!used && d < nearest) nearest = d, best = c;
            }
            if (best >= 0) {
                zones[k].seed = best;
                continue;
            }
        }
        double best = -1e18;
        for (size_t i = 0; i < available.size(); i += std::max<size_t>(1, available.size() / 3000)) {
            const int c = available[i];
            if (!seedable(zones[k], c)) continue;
            double apart = 1e9;
            for (const auto& z : zones) {
                if (z.seed >= 0) apart = std::min(apart, std::hypot(c % W - z.seed % W, c / W - z.seed / W));
            }
            const double v = interest(zones[k], c) - (apart < minApart ? 4.0 * (1 - apart / minApart) : 0) + 0.01 * rng.Unit();
            if (v > best) {
                best = v;
                zones[k].seed = c;
            }
        }
    }
    // Growth: each zone takes the best cell on its frontier, the one furthest from its quota first.
    std::vector<int> taken(W * H, -1);
    using Item = std::pair<double, int>;
    std::vector<std::priority_queue<Item>> frontier(zones.size());
    std::vector<uint8_t> usable(W * H, 0);
    for (int c : available) usable[c] = 1;
    // Drawn zones first: the free land in their boxes is theirs, and they don't grow beyond it.
    for (size_t k = 0; k < zones.size(); ++k) {
        Zone& z = zones[k];
        if (z.box[0] < 0) continue;
        for (int c : z.painted) {
            if (!usable[c] || taken[c] >= 0) continue;
            taken[c] = static_cast<int>(k);
            z.cells.push_back(c);
        }
        for (int y = z.box[1]; y < z.box[3]; ++y) {
            for (int x = z.box[0]; x < z.box[2]; ++x) {
                const int c = Cell(x, y);
                if (!usable[c] || taken[c] >= 0) continue;
                taken[c] = static_cast<int>(k);
                z.cells.push_back(c);
            }
        }
        z.quota = static_cast<double>(z.cells.size());
        if (!z.cells.empty()) z.seed = z.cells[z.cells.size() / 2];
    }
    // A seed that fell in a drawn zone moves to the nearest land that's free.
    for (auto& z : zones) {
        if (z.box[0] >= 0 || z.seed < 0 || taken[z.seed] < 0) continue;
        int best = -1;
        double nearest = 1e18;
        for (int c : available) {
            if (taken[c] >= 0) continue;
            const double d = std::hypot(c % W - z.seed % W, c / W - z.seed / W);
            if (d < nearest) nearest = d, best = c;
        }
        z.seed = best;
    }
    for (size_t k = 0; k < zones.size(); ++k) {
        if (zones[k].seed >= 0 && zones[k].box[0] < 0) frontier[k].push({0, zones[k].seed});
    }
    while (true) {
        int pick = -1;
        double lowest = 1e18;
        for (size_t k = 0; k < zones.size(); ++k) {
            if (frontier[k].empty() || zones[k].cells.size() >= zones[k].quota) continue;
            const double filled = zones[k].cells.size() / std::max(1.0, zones[k].quota);
            if (filled < lowest) {
                lowest = filled;
                pick = static_cast<int>(k);
            }
        }
        if (pick < 0) break;
        Zone& z = zones[pick];
        auto& queue = frontier[pick];
        while (!queue.empty() && taken[queue.top().second] >= 0) queue.pop();
        if (queue.empty()) continue;
        const int c = queue.top().second;
        queue.pop();
        taken[c] = pick;
        z.cells.push_back(c);
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d) {
            int nx = c % W + dx[d], ny = c / W + dy[d];
            if (!In(nx, ny)) continue;
            // A zone reaches across a road (up to 4 cells wide): roads run through
            // wards, they don't end them. The road cells stay nobody's.
            for (int hop = 0; hop < 4 && In(nx, ny) && road_[Cell(nx, ny)]; ++hop) {
                nx += dx[d];
                ny += dy[d];
            }
            if (!In(nx, ny)) continue;
            const int n = Cell(nx, ny);
            if (!usable[n] || taken[n] >= 0) continue;
            // Compact zones: closeness to the seed matters most; interest breaks ties.
            // A zone with relations grows by them too (along the river: a strip).
            const double distance = std::hypot(nx - z.seed % W, ny - z.seed / W);
            const bool related = !z.rel.empty() || !z.sketched.empty();
            queue.push({-(related ? 0.45 : 1.0) * distance + (related ? 1.5 : 0.5) * interest(z, n), n});
        }
    }
    for (const auto& z : zones) {
        if (z.cells.empty()) {
            Problem("S-UNSAT", s, "no room for zone " + z.id);
            continue;
        }
        AddRegion(z.id, z.node, s.value("id", ""), region, z.cells, z.module);
        if (z.seed >= 0) zoneSeeds_[z.id] = z.seed;
    }
    instancesOf_[s.value("id", "")] = static_cast<int>(zones.size());
}

void Solver::Subdivide(const json& s, int region) {
    const Region parent = regions_[region];
    Rng rng = StreamFor(s);
    // Free cells: in the zone, not road, water or built; a summed-area table answers "is this rectangle free" at once.
    std::vector<int> sat((W + 1) * (H + 1), 0), roadSat((W + 1) * (H + 1), 0);
    std::vector<uint8_t> free(W * H, 0);
    auto rebuild = [&] {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const int c = Cell(x, y);
                const int v = free[c] ? 1 : 0, rv = road_[c] ? 1 : 0;
                sat[(y + 1) * (W + 1) + x + 1] = v + sat[y * (W + 1) + x + 1] + sat[(y + 1) * (W + 1) + x] - sat[y * (W + 1) + x];
                roadSat[(y + 1) * (W + 1) + x + 1] = rv + roadSat[y * (W + 1) + x + 1] + roadSat[(y + 1) * (W + 1) + x] - roadSat[y * (W + 1) + x];
            }
        }
    };
    auto sum = [&](const std::vector<int>& t, int x, int y, int w, int h) {
        if (x < 0 || y < 0 || x + w > W || y + h > H || w <= 0 || h <= 0) return -1;
        return t[(y + h) * (W + 1) + x + w] - t[y * (W + 1) + x + w] - t[(y + h) * (W + 1) + x] + t[y * (W + 1) + x];
    };
    for (int c : parent.cells) {
        if (owner_[c] == region && !road_[c] && !water_[c] && !occ_[kBuilding][c] && !occ_[kProp][c]) free[c] = 1;
    }
    rebuild();
    // Cells next to a street, by the side the street is on: where a parcel's front can be.
    std::vector<std::pair<int, char>> fronts;
    for (int c : parent.cells) {
        if (!free[c]) continue;
        const int x = c % W, y = c / W;
        if (In(x, y - 1) && road_[Cell(x, y - 1)]) fronts.push_back({c, 'n'});
        if (In(x, y + 1) && road_[Cell(x, y + 1)]) fronts.push_back({c, 's'});
        if (In(x - 1, y) && road_[Cell(x - 1, y)]) fronts.push_back({c, 'w'});
        if (In(x + 1, y) && road_[Cell(x + 1, y)]) fronts.push_back({c, 'e'});
    }
    const double cx = (parent.x0 + parent.x1) / 2.0, cy = (parent.y0 + parent.y1) / 2.0;
    int placed = 0;
    json leftovers = json::array();
    if (s.value("lots", "") == "rows") {
        // Rows (the layout form's lot-layout "rows"): each block between the streets (a connected piece of free
        // land) is cut into rows of equal lots along its long sides, back to back, one row if it's shallow. With
        // block-use "grouped", a block takes one kind of lot; the filler lots take what's left.
        std::vector<int> block(W * H, -1);
        struct Block {
            int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1, cells = 0;
        };
        std::vector<Block> blocks;
        for (int c : parent.cells) {
            if (!free[c] || block[c] >= 0) continue;
            Block b;
            std::deque<int> todo = {c};
            block[c] = static_cast<int>(blocks.size());
            while (!todo.empty()) {
                const int v = todo.front();
                todo.pop_front();
                ++b.cells;
                b.x0 = std::min(b.x0, v % W), b.y0 = std::min(b.y0, v / W), b.x1 = std::max(b.x1, v % W + 1), b.y1 = std::max(b.y1, v / W + 1);
                for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    const int nx = v % W + dx, ny = v / W + dy;
                    if (In(nx, ny) && free[Cell(nx, ny)] && block[Cell(nx, ny)] < 0) {
                        block[Cell(nx, ny)] = block[c];
                        todo.push_back(Cell(nx, ny));
                    }
                }
            }
            blocks.push_back(b);
        }
        // Central blocks first (density falls outward).
        std::vector<size_t> byCentre(blocks.size());
        for (size_t i = 0; i < blocks.size(); ++i) byCentre[i] = i;
        std::sort(byCentre.begin(), byCentre.end(), [&](size_t a, size_t b) {
            return std::hypot((blocks[a].x0 + blocks[a].x1) / 2.0 - cx, (blocks[a].y0 + blocks[a].y1) / 2.0 - cy) <
                   std::hypot((blocks[b].x0 + blocks[b].x1) / 2.0 - cx, (blocks[b].y0 + blocks[b].y1) / 2.0 - cy);
        });
        // The items in groups (one kind each, in order; the fillers last), or one group of all when mixed.
        const bool grouped = s.value("grouped", false);
        std::vector<std::vector<json>> groups;
        std::map<std::string, size_t> groupOf;
        for (const auto& item : s.value("into", json::array())) {
            if (options_.previous && regionById_.contains(item.value("id", ""))) {
                ++placed;
                continue;
            }
            // Filler by kind too (when grouped): each filler kind its own blocks, not mixed into one.
            const std::string key = item.value("filler", false) ? "~filler:" + (grouped ? item.value("module", "") : std::string())
                                                                : grouped ? item.value("module", "") : "";
            if (!groupOf.contains(key)) groupOf[key] = groups.size(), groups.push_back({});
            groups[groupOf[key]].push_back(item);
        }
        std::stable_sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) {
            return !a.front().value("filler", false) && b.front().value("filler", false);
        });
        auto streetSide = [&](int x0, int y0, int x1, int y1, char side) {
            int n = 0;
            if (side == 'n') n = sum(roadSat, x0, y0 - 1, x1 - x0, 1);
            if (side == 's') n = sum(roadSat, x0, y1, x1 - x0, 1);
            if (side == 'w') n = sum(roadSat, x0 - 1, y0, 1, y1 - y0);
            if (side == 'e') n = sum(roadSat, x1, y0, 1, y1 - y0);
            return n;
        };
        // Lots of one block for one group: rows along its long sides, each lot the group's frontage.
        auto cut = [&](const Block& b, const std::vector<json>& items, size_t& next) {
            const bool alongX = (b.x1 - b.x0) >= (b.y1 - b.y0);
            const int length = alongX ? b.x1 - b.x0 : b.y1 - b.y0, deep = alongX ? b.y1 - b.y0 : b.x1 - b.x0;
            const char sideA = alongX ? 'n' : 'w', sideB = alongX ? 's' : 'e';
            const bool hasA = streetSide(b.x0, b.y0, b.x1, b.y1, sideA) > 0, hasB = streetSide(b.x0, b.y0, b.x1, b.y1, sideB) > 0;
            const IntRange want = RangeOf(items[std::min(next, items.size() - 1)].value("size", json::object()).value("h", json()), 6);
            const bool two = hasA && hasB && deep >= 2 * std::max(4, want.lo * 2 / 3);
            std::vector<std::pair<char, std::pair<int, int>>> rows; // side, [offset, depth] across the block
            if (two) {
                rows.push_back({sideA, {0, deep / 2}});
                rows.push_back({sideB, {deep / 2, deep - deep / 2}});
            } else {
                rows.push_back({hasA || !hasB ? sideA : sideB, {0, deep}});
            }
            for (const auto& [side, span] : rows) {
                if (next >= items.size()) break;
                const IntRange fw = RangeOf(items[next].value("size", json::object()).value("w", json()), 5);
                const int front = std::max(3, (fw.lo + fw.hi) / 2);
                const int n = std::max(1, length / front);
                for (int k = 0; k < n && next < items.size(); ++k) {
                    const int a = k * length / n, e = (k + 1) * length / n; // equal lots, the remainder spread
                    int x = alongX ? b.x0 + a : b.x0 + span.first, y = alongX ? b.y0 + span.first : b.y0 + a;
                    int w = alongX ? e - a : span.second, h = alongX ? span.second : e - a;
                    std::vector<int> cells;
                    for (int yy = y; yy < y + h; ++yy) {
                        for (int xx = x; xx < x + w; ++xx) {
                            if (In(xx, yy) && free[Cell(xx, yy)]) cells.push_back(Cell(xx, yy));
                        }
                    }
                    if (static_cast<int>(cells.size()) * 10 < w * h * 7) continue; // mostly not this block's
                    const json& item = items[next++];
                    for (int c : cells) free[c] = 0;
                    const int index = AddRegion(item.value("id", ""), item.value("node", ""), s.value("id", ""), region, cells, item.value("module", ""));
                    Region& r = regions_[index];
                    r.facing = side;
                    for (int t = 0; t < (side == 'n' || side == 's' ? w : h); ++t) {
                        int sx = x, sy = y;
                        if (side == 'n') sx += t, sy -= 1;
                        if (side == 's') sx += t, sy += h;
                        if (side == 'w') sx -= 1, sy += t;
                        if (side == 'e') sx += w, sy += t;
                        if (In(sx, sy) && road_[Cell(sx, sy)]) r.street.push_back(Cell(sx, sy));
                    }
                    ++placed;
                }
            }
        };
        // A block that isn't a rectangle (a radial plan's wedge, a bent one): its street-front cells in order round
        // it, split into runs one lot's frontage long; each cell of the block joins its nearest run, so the lots
        // meet in the middle, each facing its own stretch of street.
        auto edgeLots = [&](size_t bi, const std::vector<json>& items, size_t& next) {
            const Block& b = blocks[bi];
            std::vector<int> cellsOf, front;
            for (int y = b.y0; y < b.y1; ++y) {
                for (int x = b.x0; x < b.x1; ++x) {
                    const int c = Cell(x, y);
                    if (block[c] != static_cast<int>(bi) || !free[c]) continue;
                    cellsOf.push_back(c);
                    bool onStreet = false;
                    for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                        onStreet = onStreet || (In(x + dx, y + dy) && road_[Cell(x + dx, y + dy)]);
                    }
                    if (onStreet) front.push_back(c);
                }
            }
            if (front.empty()) return;
            double bx = 0, by = 0;
            for (int c : cellsOf) bx += c % W, by += c / W;
            bx /= cellsOf.size(), by /= cellsOf.size();
            std::sort(front.begin(), front.end(), [&](int a, int c) { return std::atan2(a / W - by, a % W - bx) < std::atan2(c / W - by, c % W - bx); });
            std::vector<int> label(W * H, -1);
            std::vector<std::vector<int>> runs;
            for (size_t i = 0; i < front.size() && next + runs.size() < items.size();) {
                const IntRange fw = RangeOf(items[next + runs.size()].value("size", json::object()).value("w", json()), 5);
                const size_t length = static_cast<size_t>(std::max(3, (fw.lo + fw.hi) / 2));
                std::vector<int> run(front.begin() + static_cast<std::ptrdiff_t>(i), front.begin() + static_cast<std::ptrdiff_t>(std::min(front.size(), i + length)));
                if (run.size() * 2 < length && !runs.empty()) {
                    for (int c : run) runs.back().push_back(c); // a short end joins the last lot
                } else {
                    runs.push_back(run);
                }
                i += length;
            }
            std::deque<int> todo;
            for (size_t k = 0; k < runs.size(); ++k) {
                for (int c : runs[k]) label[c] = static_cast<int>(k), todo.push_back(c);
            }
            while (!todo.empty()) {
                const int c = todo.front();
                todo.pop_front();
                for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    const int nx = c % W + dx, ny = c / W + dy;
                    if (!In(nx, ny)) continue;
                    const int n = Cell(nx, ny);
                    if (block[n] == static_cast<int>(bi) && free[n] && label[n] < 0) label[n] = label[c], todo.push_back(n);
                }
            }
            std::vector<std::vector<int>> lots(runs.size());
            for (int c : cellsOf) {
                if (label[c] >= 0) lots[label[c]].push_back(c);
            }
            for (size_t k = 0; k < runs.size() && next < items.size(); ++k) {
                if (lots[k].size() < 6) continue;
                const json& item = items[next++];
                for (int c : lots[k]) free[c] = 0;
                const int index = AddRegion(item.value("id", ""), item.value("node", ""), s.value("id", ""), region, lots[k], item.value("module", ""));
                Region& r = regions_[index];
                // It faces the side its street is on, mostly; its street: the road cells along its run.
                std::map<char, int> sides;
                for (int c : runs[k]) {
                    const int x = c % W, y = c / W;
                    if (In(x, y - 1) && road_[Cell(x, y - 1)]) ++sides['n'], r.street.push_back(Cell(x, y - 1));
                    if (In(x, y + 1) && road_[Cell(x, y + 1)]) ++sides['s'], r.street.push_back(Cell(x, y + 1));
                    if (In(x - 1, y) && road_[Cell(x - 1, y)]) ++sides['w'], r.street.push_back(Cell(x - 1, y));
                    if (In(x + 1, y) && road_[Cell(x + 1, y)]) ++sides['e'], r.street.push_back(Cell(x + 1, y));
                }
                r.facing = std::max_element(sides.begin(), sides.end(), [](const auto& a, const auto& c) { return a.second < c.second; })->first;
                ++placed;
            }
        };
        std::vector<uint8_t> used(blocks.size(), 0);
        std::vector<std::string> kindOf(blocks.size()); // grouped: the kind each block holds
        for (const auto& items : groups) {
            size_t next = 0;
            const bool filler = items.front().value("filler", false);
            const std::string kind = items.front().value("module", "");
            for (size_t bi : byCentre) {
                if (next >= items.size()) break;
                // Grouped: a block for one kind; a filler kind may finish a block of its own kind. Mixed: shared.
                if (used[bi] && grouped && kindOf[bi] != kind) continue;
                if (used[bi] && !grouped && !filler) continue;
                if (blocks[bi].cells < 12) continue;
                const Block& b = blocks[bi];
                if (b.cells * 10 < (b.x1 - b.x0) * (b.y1 - b.y0) * 8) edgeLots(bi, items, next); // not a rectangle
                else cut(b, items, next);
                used[bi] = 1;
                if (kindOf[bi].empty()) kindOf[bi] = kind;
                if (!grouped && !filler) used[bi] = 0; // mixed: the next kinds share it
            }
        }
        rebuild();
        // What the rows couldn't place (blocks that aren't rectangles: a radial plan's, an old town's) is fitted
        // the packed way below.
        json rest = json::array();
        for (const auto& item : s.value("into", json::array())) {
            if (!regionById_.contains(item.value("id", ""))) rest.push_back(item);
        }
        leftovers = rest;
        fronts.clear();
        for (int c : parent.cells) {
            if (!free[c]) continue;
            const int x = c % W, y = c / W;
            if (In(x, y - 1) && road_[Cell(x, y - 1)]) fronts.push_back({c, 'n'});
            if (In(x, y + 1) && road_[Cell(x, y + 1)]) fronts.push_back({c, 's'});
            if (In(x - 1, y) && road_[Cell(x - 1, y)]) fronts.push_back({c, 'w'});
            if (In(x + 1, y) && road_[Cell(x + 1, y)]) fronts.push_back({c, 'e'});
        }
    }
    for (const auto& item : (s.value("lots", "") == "rows" ? leftovers : s.value("into", json::array()))) {
        // Kept from the previous layout (a locked part's lot): already there.
        if (options_.previous && regionById_.contains(item.value("id", ""))) {
            ++placed;
            continue;
        }
        const IntRange fw = RangeOf(item.value("size", json::object()).value("w", json()), 5);
        const IntRange fh = RangeOf(item.value("size", json::object()).value("h", json()), 6);
        const int w0 = rng.Range(fw.lo, fw.hi), h0 = rng.Range(fh.lo, fh.hi); // frontage, depth
        struct Best {
            double score = -1e18;
            int x = 0, y = 0, rw = 0, rh = 0;
            char facing = 's';
        } best;
        // The lot as planned; if no street has room for it, smaller (to two thirds
        // of each side), and at the smallest with less of its front on the street.
        const int minW = std::min(w0, std::max(4, (2 * w0 + 2) / 3)), minH = std::min(h0, std::max(4, (2 * h0 + 2) / 3));
        auto search = [&](int w, int h, int frontPercent) {
            for (const auto& [c, side] : fronts) {
                if (!free[c]) continue;
                const int fx = c % W, fy = c / W;
                const bool across = side == 'n' || side == 's';
                const int rw = across ? w : h, rh = across ? h : w;
                // The parcel's front row touches the street; slide it along the front.
                for (int shift = 0; shift < (across ? rw : rh); ++shift) {
                    int x = across ? fx - shift : (side == 'w' ? fx : fx - rw + 1);
                    int y = across ? (side == 'n' ? fy : fy - rh + 1) : fy - shift;
                    if (sum(sat, x, y, rw, rh) != rw * rh) continue;
                    // At least 60% of the front faces street.
                    int frontRoad = 0;
                    if (side == 'n') frontRoad = sum(roadSat, x, y - 1, rw, 1);
                    if (side == 's') frontRoad = sum(roadSat, x, y + rh, rw, 1);
                    if (side == 'w') frontRoad = sum(roadSat, x - 1, y, 1, rh);
                    if (side == 'e') frontRoad = sum(roadSat, x + rw, y, 1, rh);
                    const int frontage = across ? rw : rh;
                    if (frontRoad * 100 < frontage * frontPercent) continue;
                    // Pack tightly: sides touching built or taken cells score; central first (density falls outward).
                    int contact = 0;
                    for (int k = 0; k < rw; ++k) {
                        contact += (!In(x + k, y - 1) || !free[Cell(x + k, y - 1)]);
                        contact += (!In(x + k, y + rh) || !free[Cell(x + k, y + rh)]);
                    }
                    for (int k = 0; k < rh; ++k) {
                        contact += (!In(x - 1, y + k) || !free[Cell(x - 1, y + k)]);
                        contact += (!In(x + rw, y + k) || !free[Cell(x + rw, y + k)]);
                    }
                    const double centre = std::hypot(x + rw / 2.0 - cx, y + rh / 2.0 - cy);
                    const double score = contact - 0.15 * centre + 0.01 * rng.Unit();
                    if (score > best.score) best = {score, x, y, rw, rh, side};
                }
            }
        };
        search(w0, h0, 60);
        for (int step = 1; best.rw == 0 && (w0 - step >= minW || h0 - step >= minH); ++step) {
            search(std::max(minW, w0 - step), std::max(minH, h0 - step), 60);
        }
        if (best.rw == 0) search(minW, minH, 30);
        if (best.rw == 0) {
            // No room left. Many alike lots are reported by the count check; a
            // single one (the temple, the inn) by name.
            const std::string lot = item.value("id", "");
            if (lot.find('#') == std::string::npos) {
                Problem("S-UNSAT", {{"id", lot}, {"node", item.value("node", "")}},
                        "no room for the " + item.value("module", "lot") + " (" + std::to_string(w0) + "×" + std::to_string(h0) +
                            ") along " + parent.id + "'s streets");
            }
            continue;
        }
        if (s.value("style", "") == "block") {
            // Built through (the layout form's block-fill "full"): the lot runs back to the middle of its block,
            // where the lot from the street behind meets it; no open land left inside the block.
            const bool across = best.facing == 'n' || best.facing == 's';
            const int depth = across ? best.rh : best.rw;
            int run = 0; // free cells from the lot's front to the street behind, through its middle
            const int mx = best.x + best.rw / 2, my = best.y + best.rh / 2;
            for (int k = 0; k < W + H; ++k) {
                const int x = best.facing == 'w' ? best.x + k : best.facing == 'e' ? best.x + best.rw - 1 - k : mx;
                const int y = best.facing == 'n' ? best.y + k : best.facing == 's' ? best.y + best.rh - 1 - k : my;
                if (!In(x, y) || owner_[Cell(x, y)] != region || road_[Cell(x, y)] || water_[Cell(x, y)]) break;
                if (k >= depth && !free[Cell(x, y)]) break;
                ++run;
            }
            const int want = std::max(depth, (run + 1) / 2);
            for (int grow = want - depth; grow > 0; --grow) {
                int x = best.x, y = best.y, rw = best.rw, rh = best.rh;
                if (best.facing == 'n') rh += grow;
                if (best.facing == 's') y -= grow, rh += grow;
                if (best.facing == 'w') rw += grow;
                if (best.facing == 'e') x -= grow, rw += grow;
                if (sum(sat, x, y, rw, rh) == rw * rh) {
                    best.x = x, best.y = y, best.rw = rw, best.rh = rh;
                    break;
                }
            }
        }
        std::vector<int> cells;
        for (int y = best.y; y < best.y + best.rh; ++y) {
            for (int x = best.x; x < best.x + best.rw; ++x) {
                cells.push_back(Cell(x, y));
                free[Cell(x, y)] = 0;
            }
        }
        rebuild();
        const int index = AddRegion(item.value("id", ""), item.value("node", ""), s.value("id", ""), region, cells, item.value("module", ""));
        Region& r = regions_[index];
        r.facing = best.facing;
        // Its street cells: the row just outside its front.
        for (int k = 0; k < (best.facing == 'n' || best.facing == 's' ? best.rw : best.rh); ++k) {
            int x = best.x, y = best.y;
            if (best.facing == 'n') x += k, y -= 1;
            if (best.facing == 's') x += k, y += best.rh;
            if (best.facing == 'w') x -= 1, y += k;
            if (best.facing == 'e') x += best.rw, y += k;
            if (In(x, y) && road_[Cell(x, y)]) r.street.push_back(Cell(x, y));
        }
        ++placed;
    }
    instancesOf_[s.value("id", "")] = placed;
}

void Solver::NestedRegion(const json& s, int container) {
    scoreCache_.clear();
    Rng rng = StreamFor(s);
    const Region parent = regions_[container];
    // Painted (7.8): where it was drawn, the container's own cells of that rectangle.
    if (const json at = s.value("at", json()); at.is_array() && at.size() == 2) {
        const IntRange fw = RangeOf(s.value("size", json::object()).value("w", json()), 4);
        const IntRange fh = RangeOf(s.value("size", json::object()).value("h", json()), 4);
        std::vector<int> cells;
        for (int y = at[1].get<int>(); y < at[1].get<int>() + fh.hi; ++y) {
            for (int x = at[0].get<int>(); x < at[0].get<int>() + fw.hi; ++x) {
                // Roads stay out of a drawn part, unless it's laid over them (a castle's moat and ward).
                const bool overRoads = s.value("overRoads", false);
                if (In(x, y) && owner_[Cell(x, y)] == container && (overRoads || !road_[Cell(x, y)]) && !water_[Cell(x, y)]) cells.push_back(Cell(x, y));
            }
        }
        if (cells.empty()) {
            if (s.value("essential", true)) Problem("S-UNSAT", s, "nothing free where it was drawn, in " + parent.id);
            return;
        }
        AddRegion(s.value("id", ""), s.value("node", ""), s.value("id", ""), container, cells, s.value("module", ""));
        instancesOf_[s.value("id", "")] = 1;
        return;
    }
    // The root region: placed where it says.
    const IntRange fw = RangeOf(s.value("size", json::object()).value("w", json()), 4);
    const IntRange fh = RangeOf(s.value("size", json::object()).value("h", json()), 4);
    int w = rng.Range(fw.lo, fw.hi), h = rng.Range(fh.lo, fh.hi);
    const std::string id = s.value("id", "");
    const double spread = s.value("spread", 0.0);
    const std::string group = s.value("group", "");
    // Shrink until it fits, within its own minimum (the plot may be smaller than the module's default).
    for (int attempt = 0; attempt < 6; ++attempt) {
        std::vector<int> sat((W + 1) * (H + 1), 0);
        for (int y = parent.y0; y < parent.y1; ++y) {
            for (int x = parent.x0; x < parent.x1; ++x) {
                const int c = Cell(x, y);
                const int v = Free(c, container, true) ? 1 : 0;
                sat[(y + 1) * (W + 1) + x + 1] = v + sat[y * (W + 1) + x + 1] + sat[(y + 1) * (W + 1) + x] - sat[y * (W + 1) + x];
            }
        }
        double best = -1e18;
        int bx = -1, by = -1;
        int tried = 0;
        for (int y = parent.y0; y + h <= parent.y1; ++y) {
            for (int x = parent.x0; x + w <= parent.x1; ++x) {
                const int free = sat[(y + h) * (W + 1) + x + w] - sat[y * (W + 1) + x + w] - sat[(y + h) * (W + 1) + x] + sat[y * (W + 1) + x];
                if (free != w * h) continue;
                if (++tried > options_.maxCandidates) break;
                double score = Score(id, x, y, w, h, container, 's') + 0.001 * rng.Unit();
                if (spread > 0) {
                    // Away from the group's others (and the container's edge), up to `spread` cells.
                    double nearest = spread;
                    for (int other : spreadGroups_[group]) {
                        const Region& g = regions_[other];
                        const double dx = std::max({0, g.x0 - (x + w), x - g.x1}), dy = std::max({0, g.y0 - (y + h), y - g.y1});
                        nearest = std::min(nearest, std::max(dx, dy));
                    }
                    const double edge = std::min({x - parent.x0, y - parent.y0, parent.x1 - (x + w), parent.y1 - (y + h)});
                    score += 2.0 * nearest / spread + 0.5 * std::min(1.0, edge / std::max(1.0, spread / 2));
                }
                if (score > best) {
                    best = score;
                    bx = x;
                    by = y;
                }
            }
        }
        if (bx >= 0) {
            std::vector<int> cells;
            for (int y = by; y < by + h; ++y) {
                for (int x = bx; x < bx + w; ++x) cells.push_back(Cell(x, y));
            }
            const int index = AddRegion(id, s.value("node", ""), id, container, cells, s.value("module", ""));
            regions_[index].facing = regions_[container].facing;
            instancesOf_[id] = 1;
            if (!group.empty()) spreadGroups_[group].push_back(index);
            return;
        }
        w = std::max(1, w - 1);
        h = std::max(1, h - 1);
        if (w * h < s.value("minArea", 2)) break;
    }
    if (s.value("essential", true)) Problem("S-UNSAT", s, "no room for it in " + parent.id);
    instancesOf_[id] = 0;
}

// --- Objects ---------------------------------------------------------------------------------------

void Solver::Place(const json& s, int container) {
    scoreCache_.clear();
    Rng rng = StreamFor(s);
    const Region parent = regions_[container];
    const IntRange cw = RangeOf(s.value("footprint", json::object()).value("w", json()), 1);
    const IntRange ch = RangeOf(s.value("footprint", json::object()).value("h", json()), 1);
    const int layer = LayerOf(s.value("layer", "prop"));
    const bool solid = Solid(layer);
    const std::string id = s.value("id", "");
    const IntRange count = RangeOf(s.value("count", json()), 1);
    const int n = rng.Range(count.lo, count.hi);
    const std::string facingRule = s.value("facing", "street");
    int made = 0;
    for (int k = 0; k < n; ++k) {
        char facing = facingRule.size() == 1 ? facingRule[0] : (parent.facing ? parent.facing : 's');
        int w = rng.Range(cw.lo, cw.hi), h = rng.Range(ch.lo, ch.hi);
        if (s.contains("fit") && s.value("primary", false) && parent.facing) {
            // Fitted (building-cover "fit"): as wide as the lot's front and as deep as the lot, less its setback,
            // by its kind's cover.
            const double cover = std::clamp(s.value("fit", 0.7), 0.2, 1.0);
            const bool across = parent.facing == 'n' || parent.facing == 's';
            const int front = across ? parent.x1 - parent.x0 : parent.y1 - parent.y0, deep = across ? parent.y1 - parent.y0 : parent.x1 - parent.x0;
            w = std::max(2, static_cast<int>(std::lround(front * std::sqrt(cover))));
            h = std::max(2, static_cast<int>(std::lround((deep - s.value("setback", 1)) * std::sqrt(cover))));
            if (!across) std::swap(w, h); // undone below: the width runs along the front
        }
        // The footprint's width runs along its front.
        if (facing == 'e' || facing == 'w') std::swap(w, h);
        const std::string objectId = n == 1 ? id : id + "[" + std::to_string(k) + "]";
        std::optional<std::pair<int, int>> at;
        // Fitted (building-shape "fitted"): the lot itself, less its setback from the street and a cell from its
        // neighbours, cut from the back to its kind's cover, so its front follows the street, curved or not. Or, in a
        // lot that isn't a rectangle, the largest rectangle in it.
        const std::string shape = s.value("shape", "");
        if (s.value("primary", false) && parent.facing && layer == kBuilding &&
            (shape == "fitted" || (s.contains("fit") && static_cast<int>(parent.cells.size()) * 100 < (parent.x1 - parent.x0) * (parent.y1 - parent.y0) * 85))) {
            std::set<int> lot(parent.cells.begin(), parent.cells.end());
            if (shape == "fitted") {
                // Each cell's steps from the street.
                std::map<int, int> steps;
                std::deque<int> todo;
                for (int c : parent.cells) {
                    const int x = c % W, y = c / W;
                    bool front = false;
                    for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                        front = front || (In(x + dx, y + dy) && road_[Cell(x + dx, y + dy)]);
                    }
                    if (front) steps[c] = 1, todo.push_back(c);
                }
                while (!todo.empty()) {
                    const int c = todo.front();
                    todo.pop_front();
                    for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                        const int nx = c % W + dx, ny = c / W + dy;
                        if (!In(nx, ny) || !lot.contains(Cell(nx, ny)) || steps.contains(Cell(nx, ny))) continue;
                        steps[Cell(nx, ny)] = steps[c] + 1;
                        todo.push_back(Cell(nx, ny));
                    }
                }
                const int setback = s.value("setback", 1);
                std::vector<int> body;
                for (int c : parent.cells) {
                    const int x = c % W, y = c / W;
                    if (!steps.contains(c) || steps[c] <= setback || !Free(c, container, solid) || occ_[layer][c]) continue;
                    bool edge = false; // a cell from the lot's other neighbours (not the street)
                    for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                        const int nx = x + dx, ny = y + dy;
                        edge = edge || !In(nx, ny) || (!lot.contains(Cell(nx, ny)) && !road_[Cell(nx, ny)]);
                    }
                    if (!edge) body.push_back(c);
                }
                // Cut from the back to its cover; then its largest piece, its loose cells trimmed.
                const size_t target = static_cast<size_t>(parent.cells.size() * std::clamp(s.value("fit", 0.7), 0.2, 1.0));
                std::stable_sort(body.begin(), body.end(), [&](int a, int b) { return steps[a] < steps[b]; });
                if (body.size() > target) body.resize(target);
                std::set<int> keep(body.begin(), body.end());
                for (bool trimmed = true; trimmed;) {
                    trimmed = false;
                    for (auto it = keep.begin(); it != keep.end();) {
                        const int x = *it % W, y = *it / W;
                        int around = 0;
                        for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                            around += In(x + dx, y + dy) && keep.contains(Cell(x + dx, y + dy));
                        }
                        if (around < 2) it = keep.erase(it), trimmed = true;
                        else ++it;
                    }
                }
                std::vector<int> piece;
                std::set<int> done;
                for (int c : keep) {
                    if (done.contains(c)) continue;
                    std::vector<int> part{c};
                    done.insert(c);
                    for (size_t i = 0; i < part.size(); ++i) {
                        for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                            const int nx = part[i] % W + dx, ny = part[i] / W + dy;
                            if (In(nx, ny) && keep.contains(Cell(nx, ny)) && !done.contains(Cell(nx, ny))) done.insert(Cell(nx, ny)), part.push_back(Cell(nx, ny));
                        }
                    }
                    if (part.size() > piece.size()) piece = part;
                }
                if (piece.size() >= 6 && piece.size() >= 0.8 * HullArea(CellHull(piece, W))) {
                    Object o;
                    o.id = objectId;
                    o.statement = id;
                    o.node = s.value("node", "");
                    o.slot = s.value("slot", "");
                    o.module = s.value("module", "");
                    int bx0 = W, by0 = H, bx1 = 0, by1 = 0;
                    for (int c : piece) bx0 = std::min(bx0, c % W), by0 = std::min(by0, c / W), bx1 = std::max(bx1, c % W + 1), by1 = std::max(by1, c / W + 1);
                    o.x = bx0, o.y = by0, o.w = bx1 - bx0, o.h = by1 - by0;
                    o.cells = piece;
                    o.shape = shape;
                    o.facing = parent.facing;
                    o.layer = layer;
                    o.primary = true;
                    AddObject(o);
                    ++made;
                    at = std::pair{o.x, o.y};
                }
            }
            if (!at) {
                // A lot that isn't a rectangle: the largest free rectangle inside the lot, cut to its kind's cover, kept to the
                // street side.
                const int bw = parent.x1 - parent.x0, bh = parent.y1 - parent.y0;
                std::vector<int> run(bw, 0);
                int best = 0, rx = 0, ry = 0, rw = 0, rh = 0;
                for (int y = parent.y0; y < parent.y1; ++y) {
                    for (int x = parent.x0; x < parent.x1; ++x) {
                        const int c = Cell(x, y);
                        run[x - parent.x0] = lot.contains(c) && Free(c, container, solid) && !occ_[layer][c] ? run[x - parent.x0] + 1 : 0;
                    }
                    for (int i = 0; i < bw; ++i) { // the widest rectangle ending on this row, for each height
                        int height = run[i];
                        for (int j = i; j < bw && height > 0; ++j) {
                            height = std::min(height, run[j]);
                            const int cw = j - i + 1, area = cw * height;
                            if (area > best && cw >= 2 && height >= 2 && cw <= 3 * height && height <= 3 * cw) {
                                best = area, rx = parent.x0 + i, ry = y - height + 1, rw = cw, rh = height;
                            }
                        }
                    }
                }
                (void)bh;
                if (best >= 4) {
                    const double target = parent.cells.size() * std::clamp(s.value("fit", 0.7), 0.2, 1.0);
                    if (best > target) {
                        const double k = std::sqrt(target / best);
                        const int nw = std::max(2, static_cast<int>(std::lround(rw * k))), nh = std::max(2, static_cast<int>(std::lround(rh * k)));
                        if (parent.facing == 's') ry += rh - nh;
                        if (parent.facing == 'e') rx += rw - nw;
                        if (parent.facing == 'n' || parent.facing == 's') rx += (rw - nw) / 2;
                        else ry += (rh - nh) / 2;
                        rw = nw, rh = nh;
                    }
                    Object o;
                    o.id = objectId;
                    o.statement = id;
                    o.node = s.value("node", "");
                    o.slot = s.value("slot", "");
                    o.module = s.value("module", "");
                    o.x = rx, o.y = ry, o.w = rw, o.h = rh;
                    o.facing = parent.facing;
                    o.layer = layer;
                    o.primary = true;
                    o.shape = s.value("shape", "");
                    AddObject(o);
                    ++made;
                    at = std::pair{o.x, o.y};
                }
            }
            if (at) continue;
        }
        // A primary object in a parcel stands at the front, at its setback, centred
        // or as near the centre as puts its door on the street (a lot whose front
        // is only partly on the street).
        if (s.value("primary", false) && parent.facing) {
            const int setback = s.value("setback", 1);
            const bool across = parent.facing == 'n' || parent.facing == 's';
            const int along = across ? parent.x1 - parent.x0 - w : parent.y1 - parent.y0 - h; // room to slide
            std::optional<std::pair<int, int>> offStreet;
            for (int back = std::min(setback, std::max(0, (across ? parent.y1 - parent.y0 : parent.x1 - parent.x0) - (facing == 'n' || facing == 's' ? h : w)));
                 back >= 0 && !at; --back) {
                for (int step = 0; step <= 2 * std::max(0, along) && !at; ++step) {
                    const int shift = (step % 2 ? 1 : -1) * ((step + 1) / 2);
                    int x = (parent.x0 + parent.x1 - w) / 2, y = (parent.y0 + parent.y1 - h) / 2;
                    if (across) x += shift;
                    else y += shift;
                    if (parent.facing == 'n') y = parent.y0 + back;
                    if (parent.facing == 's') y = parent.y1 - h - back;
                    if (parent.facing == 'w') x = parent.x0 + back;
                    if (parent.facing == 'e') x = parent.x1 - w - back;
                    bool fits = x >= parent.x0 && y >= parent.y0 && x + w <= parent.x1 && y + h <= parent.y1;
                    for (int yy = y; fits && yy < y + h; ++yy) {
                        for (int xx = x; fits && xx < x + w; ++xx) fits = Free(Cell(xx, yy), container, solid);
                    }
                    if (!fits) continue;
                    // The street just outside the lot, in front of the door.
                    int dx = x + w / 2, dy = y + h / 2;
                    if (parent.facing == 'n') dy = parent.y0 - 1;
                    if (parent.facing == 's') dy = parent.y1;
                    if (parent.facing == 'w') dx = parent.x0 - 1;
                    if (parent.facing == 'e') dx = parent.x1;
                    if (In(dx, dy) && road_[Cell(dx, dy)]) at = std::pair{x, y};
                    else if (!offStreet) offStreet = std::pair{x, y};
                }
            }
            if (!at) at = offStreet;
        }
        if (!at) {
            // Anywhere it fits, by the relations' scores (or the centre, if asked).
            double best = -1e18;
            int tried = 0;
            const std::string position = s.value("position", "");
            const bool centre = position == "center";
            std::vector<std::tuple<double, int, int>> candidates;
            // The cells a "gate" stands by: the road where it crosses the container's border.
            std::vector<int> gateCells;
            if (position == "gate") {
                for (int c : parent.cells) {
                    const int px = c % W, py = c / W;
                    if (road_[c] && (px == parent.x0 || py == parent.y0 || px == parent.x1 - 1 || py == parent.y1 - 1)) gateCells.push_back(c);
                }
            }
            const auto doorIt = doorsOf_.find(parent.id);
            // Unless the statement says where (docs/build-fixes.md): the role's own place (its look's
            // "at"), and beside the other things of its set (a hearth's chairs by the hearth, a desk's
            // chair at the desk, a bar's stools at the counter).
            const std::string role = s.value("slot", "").substr(0, s.value("slot", "").find('|'));
            const json roleAt = position.empty() ? ontology_.RoleLook(role, "at") : json();
            std::vector<int> setCells;
            for (const auto& o : objects_) {
                if (!ontology_.IsA(parent.module, "Room") || setCells.size() > 200) break; // a room's sets, not a zone's hundreds
                if (o.node.empty() || o.node != s.value("node", "") || o.statement == id) continue;
                for (int yy = o.y; yy < o.y + o.h; ++yy) {
                    for (int xx = o.x; xx < o.x + o.w; ++xx) setCells.push_back(Cell(xx, yy));
                }
            }
            for (int y = parent.y0; y + h <= parent.y1; ++y) {
                for (int x = parent.x0; x + w <= parent.x1; ++x) {
                    bool fits = true;
                    for (int yy = y; fits && yy < y + h; ++yy) {
                        for (int xx = x; fits && xx < x + w; ++xx) {
                            const int c = Cell(xx, yy);
                            fits = Free(c, -1, solid) && owner_[c] == container &&
                                   (!solid || !occ_[layer][c]) && !occ_[layer][c];
                        }
                    }
                    if (!fits) continue;
                    if (++tried > options_.maxCandidates) break;
                    double score = Score(objectId, x, y, w, h, container, facing) + Score(id, x, y, w, h, container, facing);
                    if (centre) {
                        score -= std::hypot(x + w / 2.0 - (parent.x0 + parent.x1) / 2.0, y + h / 2.0 - (parent.y0 + parent.y1) / 2.0);
                    }
                    const int toEdge = std::min({x - parent.x0, y - parent.y0, parent.x1 - (x + w), parent.y1 - (y + h)});
                    double fromDoor = 0;
                    if (doorIt != doorsOf_.end()) {
                        fromDoor = 1e9;
                        for (int c : doorIt->second) fromDoor = std::min(fromDoor, std::hypot(c % W - (x + w / 2.0), c / W - (y + h / 2.0)));
                    }
                    if (position == "corners") {
                        // Object k at corner k: the container's corners in turn.
                        const int corner = k % 4;
                        const double tx = corner == 1 || corner == 2 ? parent.x1 - w : parent.x0, ty = corner >= 2 ? parent.y1 - h : parent.y0;
                        score -= std::hypot(x - tx, y - ty);
                    } else if (position == "gate" && !gateCells.empty()) {
                        double d = 1e9;
                        for (int c : gateCells) d = std::min(d, std::hypot(c % W - (x + w / 2.0), c / W - (y + h / 2.0)));
                        score -= d;
                    } else if (position == "wall") {
                        score -= toEdge * 2.0;
                    } else if (position == "far-wall") {
                        score -= toEdge * 2.0;
                        score += 0.5 * std::min(fromDoor, 12.0);
                    } else if (position == "corner") {
                        const int toX = std::min(x - parent.x0, parent.x1 - (x + w)), toY = std::min(y - parent.y0, parent.y1 - (y + h));
                        score -= (toX + toY) * 1.5;
                    } else if (position == "facing-door") {
                        score -= std::abs(std::min(fromDoor, 12.0) - 2.0) + toEdge * 0.2;
                    } else if (roleAt.is_string()) {
                        const std::string where = roleAt.get<std::string>();
                        if (where == "wall") score -= toEdge * 1.5;
                        else if (where == "corner") score -= (std::min(x - parent.x0, parent.x1 - (x + w)) + std::min(y - parent.y0, parent.y1 - (y + h))) * 1.2;
                        else if (where == "centre") score -= 0.6 * std::hypot(x + w / 2.0 - (parent.x0 + parent.x1) / 2.0, y + h / 2.0 - (parent.y0 + parent.y1) / 2.0);
                    }
                    if (!setCells.empty()) {
                        int d = std::numeric_limits<int>::max();
                        for (int c : setCells) {
                            const int px = c % W, py = c / W;
                            const int dx = px < x ? x - px : (px >= x + w ? px - (x + w - 1) : 0);
                            const int dy = py < y ? y - py : (py >= y + h ? py - (y + h - 1) : 0);
                            d = std::min(d, std::max(dx, dy));
                        }
                        score += 2.0 * std::max(0.0, 1.0 - (d - 1) / 5.0);
                    }
                    score += 0.001 * rng.Unit();
                    candidates.push_back({score, x, y});
                }
            }
            // The best that leaves the room's floor joined to its doors (a room's own rule).
            std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return std::get<0>(a) > std::get<0>(b); });
            int checked = 0;
            for (const auto& [score, cx, cy] : candidates) {
                if (solid && ++checked <= 60 && !KeepsWay(container, cx, cy, w, h)) continue;
                if (solid && checked > 60) break;
                best = score;
                at = std::pair{cx, cy};
                break;
            }
        }
        if (!at) {
            if (s.value("essential", true)) Problem("S-UNSAT", s, "no room for " + s.value("slot", "it") + " in " + parent.id);
            break;
        }
        Object o;
        o.id = objectId;
        o.statement = id;
        o.node = s.value("node", "");
        o.slot = s.value("slot", "");
        o.module = s.value("module", "");
        o.x = at->first;
        o.y = at->second;
        o.w = w;
        o.h = h;
        o.facing = facing;
        o.layer = layer;
        o.primary = s.value("primary", false);
        if (layer == kBuilding) o.shape = s.value("shape", "");
        AddObject(o);
        ++made;
    }
    instancesOf_[id] = made;
}

void Solver::Anchor(const json& s) {
    AnchorResult a;
    a.id = s.value("id", "");
    a.node = s.value("node", "");
    a.kind = s.value("kind", "marker");
    const json at = s.value("at", json());
    if (at.is_array() && at.size() == 2) {
        const int x = std::clamp(at[0].get<int>(), 0, W - 1), y = std::clamp(at[1].get<int>(), 0, H - 1);
        a.cell = Cell(x, y);
    } else if (at.is_string()) {
        const auto cells = Cells(at.get<std::string>());
        if (!cells.empty()) a.cell = cells[cells.size() / 2];
    }
    if (a.cell < 0) Problem("E-REF", s, "can't find where it is");
    anchors_[a.id] = a;
}

void Solver::Line(const json& s) {
    LineResult line;
    line.id = s.value("id", "");
    line.node = s.value("node", "");
    line.slot = s.value("slot", "");
    std::string along = s.value("along", "");
    const size_t dot = along.rfind(".edge");
    const std::string base = dot == std::string::npos ? along : along.substr(0, dot);
    const int r = RegionIndex(base);
    if (r < 0) {
        Problem("E-REF", s, "can't find " + base);
        return;
    }
    const Region& region = regions_[r];
    std::set<int> cells(region.cells.begin(), region.cells.end());
    auto has = [&](int x, int y) { return In(x, y) && cells.contains(Cell(x, y)); };
    // Its boundary edges: the north edge of (x, y) is (x, y, 0); the west edge (x, y, 1).
    for (int c : region.cells) {
        const int x = c % W, y = c / W;
        if (!has(x, y - 1)) line.edges.push_back({x, y, 0});
        if (!has(x, y + 1)) line.edges.push_back({x, y + 1, 0});
        if (!has(x - 1, y)) line.edges.push_back({x, y, 1});
        if (!has(x + 1, y)) line.edges.push_back({x + 1, y, 1});
    }
    // Gaps: the middle of a side ("front", "side:n", "toward:<ref>") or the edge nearest an anchor.
    auto sideMiddle = [&](char side, int width) {
        std::vector<std::array<int, 3>> out;
        std::vector<std::array<int, 3>> onSide;
        for (const auto& e : line.edges) {
            const bool horizontal = e[2] == 0;
            if ((side == 'n' && horizontal && e[1] == region.y0) || (side == 's' && horizontal && e[1] == region.y1) ||
                (side == 'w' && !horizontal && e[0] == region.x0) || (side == 'e' && !horizontal && e[0] == region.x1)) {
                onSide.push_back(e);
            }
        }
        std::sort(onSide.begin(), onSide.end());
        if (onSide.empty()) return out;
        const int mid = static_cast<int>(onSide.size()) / 2;
        for (int k = 0; k < width && mid - width / 2 + k < static_cast<int>(onSide.size()); ++k) {
            if (mid - width / 2 + k >= 0) out.push_back(onSide[mid - width / 2 + k]);
        }
        return out;
    };
    for (const auto& g : s.value("gaps", json::array())) {
        if (!g.is_string()) continue;
        const std::string gap = g.get<std::string>();
        std::vector<std::array<int, 3>> remove;
        if (gap == "front") {
            remove = sideMiddle(region.facing ? region.facing : 's', 1);
        } else if (gap.rfind("side:", 0) == 0 && gap.size() > 5) {
            remove = sideMiddle(gap[5], 1);
        } else if (gap.rfind("toward:", 0) == 0) {
            if (auto c = CenterOf(gap.substr(7))) {
                const double dx = (*c)[0] - (region.x0 + region.x1) / 2.0, dy = (*c)[1] - (region.y0 + region.y1) / 2.0;
                const char side = std::abs(dx) > std::abs(dy) ? (dx > 0 ? 'e' : 'w') : (dy > 0 ? 's' : 'n');
                remove = sideMiddle(side, 1);
            }
        } else {
            // Near an anchor or a path: every edge its cells cross.
            const auto near = Cells(gap);
            std::set<int> nearSet(near.begin(), near.end());
            for (const auto& e : line.edges) {
                const int a = In(e[0], e[1]) ? Cell(e[0], e[1]) : -1;
                const int b = e[2] == 0 ? (In(e[0], e[1] - 1) ? Cell(e[0], e[1] - 1) : -1) : (In(e[0] - 1, e[1]) ? Cell(e[0] - 1, e[1]) : -1);
                if ((a >= 0 && nearSet.contains(a)) || (b >= 0 && nearSet.contains(b))) remove.push_back(e);
            }
        }
        for (const auto& e : remove) {
            auto it = std::find(line.edges.begin(), line.edges.end(), e);
            if (it != line.edges.end()) {
                line.gaps.push_back(*it);
                line.edges.erase(it);
            }
        }
    }
    // Lines never run across a street: those edges become gaps too.
    for (auto it = line.edges.begin(); it != line.edges.end();) {
        const auto& e = *it;
        const int a = In(e[0], e[1]) ? Cell(e[0], e[1]) : -1;
        const int b = e[2] == 0 ? (In(e[0], e[1] - 1) ? Cell(e[0], e[1] - 1) : -1) : (In(e[0] - 1, e[1]) ? Cell(e[0] - 1, e[1]) : -1);
        const bool roadBoth = a >= 0 && b >= 0 && road_[a] && road_[b];
        if (roadBoth || (a >= 0 && water_[a] && b >= 0 && water_[b])) {
            line.gaps.push_back(e);
            it = line.edges.erase(it);
        } else {
            ++it;
        }
    }
    std::sort(line.edges.begin(), line.edges.end());
    line.closed = true; // a region's boundary is a closed loop; it opens only at its gaps
    lines_.push_back(line);
    instancesOf_[line.id] = 1;
}

void Solver::Rows(const json& s, int container, bool grid) {
    Rng rng = StreamFor(s);
    const Region parent = regions_[container];
    const std::string id = s.value("id", "");
    std::vector<int> free;
    for (int c : parent.cells) {
        if (Free(c, -1, true) && std::binary_search(parent.cells.begin(), parent.cells.end(), c) && !occ_[LayerOf(s.value("layer", "decal"))][c]) {
            free.push_back(c);
        }
    }
    std::set<int> freeSet(free.begin(), free.end());
    const int layer = LayerOf(s.value("layer", grid ? "prop" : "decal"));
    const bool asRegions = s.value("regions", false);
    int made = 0;
    auto emit = [&](int x, int y, int w, int h) {
        const std::string instance = id + "[" + std::to_string(made) + "]";
        if (asRegions) {
            std::vector<int> cells;
            for (int yy = y; yy < y + h; ++yy) {
                for (int xx = x; xx < x + w; ++xx) cells.push_back(Cell(xx, yy));
            }
            AddRegion(instance, s.value("node", ""), id, container, cells, s.value("module", ""));
        } else {
            Object o;
            o.id = instance;
            o.statement = id;
            o.node = s.value("node", "");
            o.slot = s.value("slot", "");
            o.x = x;
            o.y = y;
            o.w = w;
            o.h = h;
            o.layer = layer;
            AddObject(o);
        }
        ++made;
    };
    int margin = s.value("margin", grid ? 0 : 1);
    // A small area keeps its rows, without the margin.
    if (parent.x1 - parent.x0 - 2 * margin < 2 || parent.y1 - parent.y0 - 2 * margin < 2) margin = 0;
    const int x0 = parent.x0 + margin, y0 = parent.y0 + margin, x1 = parent.x1 - margin, y1 = parent.y1 - margin;
    if (x1 <= x0 || y1 <= y0) {
        instancesOf_[id] = 0;
        return;
    }
    auto allFree = [&](int x, int y, int w, int h) {
        for (int yy = y; yy < y + h; ++yy) {
            for (int xx = x; xx < x + w; ++xx) {
                if (!freeSet.contains(Cell(xx, yy))) return false;
            }
        }
        return true;
    };
    if (grid) {
        const int sx = std::max(1, RangeOf(s.value("spacing", json::object()).value("x", json()), 2).lo);
        const int sy = std::max(1, RangeOf(s.value("spacing", json::object()).value("y", json()), 2).lo);
        const IntRange count = RangeOf(s.value("count", json()), 1000000);
        const int limit = rng.Range(count.lo, count.hi);
        for (int y = y0; y < y1 && made < limit; y += sy) {
            for (int x = x0; x < x1 && made < limit; x += sx) {
                if (allFree(x, y, 1, 1)) emit(x, y, 1, 1);
            }
        }
    } else {
        // Rows along the long axis, one cell wide with a cell between.
        const bool alongX = s.value("orientation", "long-axis") == "short-axis" ? (x1 - x0) < (y1 - y0) : (x1 - x0) >= (y1 - y0);
        const IntRange count = RangeOf(s.value("count", json()), 1000);
        const int wanted = rng.Range(count.lo, count.hi);
        const int span = alongX ? y1 - y0 : x1 - x0;
        const int room = (span + 1) / 2;
        const int rows = std::min(wanted, room);
        const int step = rows > 0 ? std::max(2, span / std::max(1, rows)) : 2;
        for (int k = 0; k < rows; ++k) {
            const int at = (alongX ? y0 : x0) + k * step;
            if (alongX && at < y1 && allFree(x0, at, x1 - x0, 1)) emit(x0, at, x1 - x0, 1);
            else if (!alongX && at < x1 && allFree(at, y0, 1, y1 - y0)) emit(at, y0, 1, y1 - y0);
        }
    }
    instancesOf_[id] = made;
}

void Solver::Scatter(const json& s, int container) {
    Rng rng = StreamFor(s);
    const std::string id = s.value("id", "");
    const int layer = LayerOf(s.value("layer", "prop"));
    const bool solid = Solid(layer);
    std::vector<int> area = Cells(s.value("in", ""));
    // Solid things keep off the doors (a doorstep's pots beside the door, not on it).
    std::set<int> doors;
    if (solid) {
        for (const auto& o : objects_) {
            if (!o.primary || o.layer != kBuilding) continue;
            for (int c : Cells(o.id + ".door")) doors.insert(c);
        }
    }
    std::vector<int> free;
    const bool own = s.value("own", false);
    const bool onRoad = s.value("onRoad", false); // vehicles: road cells only, and only them
    std::vector<int> near;
    if (s.contains("near") && s["near"].is_string()) near = Cells(s["near"].get<std::string>());
    const int radius = s.value("radius", 2);
    for (int c : area) {
        if (doors.contains(c)) continue;
        if (own && owner_[c] != container) continue;
        if (!near.empty()) {
            bool close = false;
            for (int n : near) close = close || (std::abs(n % W - c % W) <= radius && std::abs(n / W - c / W) <= radius);
            if (!close) continue;
        }
        if (onRoad) {
            // The carriageway only: not a road cell at the rim (its sidewalk, where people walk past).
            if (!road_[c] || water_[c] || occ_[layer][c] || occ_[kBuilding][c]) continue;
            bool rim = false;
            for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                rim = rim || !In(c % W + dx, c / W + dy) || !road_[Cell(c % W + dx, c / W + dy)];
            }
            if (!rim) free.push_back(c);
            continue;
        }
        if (Free(c, -1, true) && !occ_[layer][c] && (!solid || (!occ_[kProp][c] && !occ_[kBuilding][c]))) free.push_back(c);
    }
    // Its module's layout for it (the ontology's: docs/places/ontology-in-use.md). "bays": rows along the area's long
    // side, each "length" cells deep (a car's length: it stands in the cell by its aisle, the rest its tail's); the first
    // against the edge, then an aisle ("aisle" cells) and two rows back to back, over and over; a cross aisle at each end
    // ("ends"). Things only in the bays, side by side, each facing its aisle: every car can drive out.
    std::map<int, char> bayFacing;
    {
        const std::string slotName = s.value("slot", "");
        const ModuleDef* module = container >= 0 ? ontology_.Module(regions_[container].module) : nullptr;
        const json layout = module ? module->layout.value(slotName.substr(0, slotName.find('|')), json()) : json();
        if (layout.is_object() && layout.value("pattern", "") == "bays" && !area.empty()) {
            int x0 = W, y0 = H, x1 = -1, y1 = -1;
            for (int c : area) x0 = std::min(x0, c % W), y0 = std::min(y0, c / W), x1 = std::max(x1, c % W), y1 = std::max(y1, c / W);
            const bool rowsAlongX = x1 - x0 >= y1 - y0;
            const int deep = std::max(1, layout.value("length", 2)), aisle = std::max(1, layout.value("aisle", 2)), ends = layout.value("ends", 1);
            const int length = rowsAlongX ? x1 - x0 : y1 - y0, span = rowsAlongX ? y1 - y0 : x1 - x0;
            // Across the rows: +1, a bay whose aisle is after it; -1, before it; 0, not a bay (an aisle, a tail).
            auto bayAt = [&](int t) {
                int toAisle = 0;
                if (t < deep) {
                    toAisle = t == deep - 1 ? 1 : 0;
                } else {
                    const int u = (t - deep) % (aisle + 2 * deep);
                    toAisle = u == aisle ? -1 : u == aisle + 2 * deep - 1 ? 1 : 0;
                }
                return toAisle > 0 && t + aisle > span ? 0 : toAisle; // its whole aisle inside the area, not past its edge
            };
            std::vector<int> bays;
            for (int c : free) {
                const int along = rowsAlongX ? c % W - x0 : c / W - y0, t = rowsAlongX ? c / W - y0 : c % W - x0;
                if (length > 2 * ends + 2 && (along < ends || along > length - ends)) continue; // the cross aisles
                const int toAisle = bayAt(t);
                if (toAisle == 0) continue;
                bays.push_back(c);
                bayFacing[c] = layout.value("facing", "aisle") != "aisle" ? 's' : rowsAlongX ? (toAisle > 0 ? 's' : 'n') : (toAisle > 0 ? 'e' : 'w');
            }
            free = bays;
        }
    }
    const double density = std::clamp(s.value("density", 0.1), 0.0, 1.0);
    int target = static_cast<int>(std::lround(density * free.size()));
    if (s.contains("count")) {
        const IntRange count = RangeOf(s["count"], 1);
        target = std::min(static_cast<int>(free.size()), rng.Range(count.lo, count.hi)); // so many, not a share
    }
    target = std::max(target, std::min(static_cast<int>(free.size()), s.value("min", 0)));
    // In bays, side by side.
    const int spacing = bayFacing.empty() ? std::max(0, RangeOf(s.value("spacing", json::object()).value("min", json()), solid ? 1 : 0).lo) : 0;
    for (size_t i = free.size(); i > 1; --i) std::swap(free[i - 1], free[rng.Next() % i]);
    // Where a thing belongs (docs/build-fixes.md; its role's look): in a room, against a wall, in a
    // corner, in the middle, or beside another kind of thing (a chair by a table); out of doors, what
    // grows wild (a "vary" role) in groves and thickets with clearings between, from a slow noise,
    // never an even sprinkle. The free cells are ranked so (still a little shuffled).
    {
        const std::string slot = s.value("slot", "");
        const std::string role = slot.substr(0, slot.find('|'));
        // The plan's arrangement first (its position: ApplyArrangement), else the role's own place.
        const json at = s.contains("position") ? s["position"] : ontology_.RoleLook(role, "at");
        const json nearRole = ontology_.RoleLook(role, "near"), vary = ontology_.RoleLook(role, "vary");
        const bool inRoom = container >= 0 && ontology_.IsA(regions_[container].module, "Room");
        auto walls = [&](int c) {
            int n = 0;
            for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const int x = c % W + dx, y = c / W + dy;
                if (!In(x, y)) {
                    ++n;
                    continue;
                }
                const int nc = Cell(x, y);
                n += owner_[nc] != container || solid_[nc] || occ_[kBuilding][nc];
            }
            return n;
        };
        std::vector<double> rank(W * H, 0.0);
        bool ranked = false;
        if (inRoom && at.is_string()) {
            const std::string where = at.get<std::string>();
            for (int c : free) {
                const int n = walls(c);
                rank[c] = where == "wall" ? (n >= 1 ? 1.5 : 0.0) : where == "corner" ? (n >= 2 ? 2.0 : n == 1 ? 1.0 : 0.0) : (n == 0 ? 1.5 : 0.0);
                rank[c] += rng.Unit() * 1.5; // mostly where it belongs, not every corner of every room
            }
            ranked = true;
        }
        if (inRoom && nearRole.is_string()) {
            const std::string other = nearRole.get<std::string>();
            std::vector<uint8_t> by(W * H, 0);
            for (const auto& o : objects_) {
                if (o.slot.substr(0, o.slot.find('|')) != other) continue;
                for (int dy = -1; dy <= o.h; ++dy) {
                    for (int dx = -1; dx <= o.w; ++dx) {
                        if (In(o.x + dx, o.y + dy)) by[Cell(o.x + dx, o.y + dy)] = 1;
                    }
                }
            }
            for (int c : free) rank[c] += by[c] ? 4.0 : 0.0;
            ranked = true;
        }
        if (bayFacing.empty() && s.value("ordered", false) && !s.contains("cluster") && target > 0 && !free.empty()) {
            // Ordered (the layout form's arrangement): on a regular pattern spaced for the number wanted, anchored
            // on the container; along an edge (a lamp, a bench on a plot's border) evenly spaced points; a cell
            // whose mirror across the container's centre is free too first, so things come in matching pairs.
            int x0 = W, y0 = H, x1 = 0, y1 = 0;
            for (int c : free) x0 = std::min(x0, c % W), y0 = std::min(y0, c / W), x1 = std::max(x1, c % W), y1 = std::max(y1, c / W);
            const bool edge = s.value("in", "").find(".border") != std::string::npos || s.value("onRoad", false);
            const int step = std::max(spacing + 1, static_cast<int>(std::lround(edge ? static_cast<double>(free.size()) / target
                                                                                       : std::sqrt(static_cast<double>(free.size()) / target))));
            std::vector<uint8_t> isFree(W * H, 0);
            for (int c : free) isFree[c] = 1;
            for (int c : free) {
                const int x = c % W - x0, y = c / W - y0;
                const bool onPattern = edge ? (x + y) % step == 0 : (x % step == step / 2 % step && y % step == step / 2 % step);
                const int mx = x0 + x1 - c % W, my = y0 + y1 - c / W;
                const bool paired = In(mx, my) && isFree[Cell(mx, my)];
                rank[c] = (onPattern ? 10.0 : 0.0) + (paired ? 2.0 : 0.0) + 0.001 * rng.Unit();
            }
            ranked = true;
        } else if (!inRoom && vary.is_boolean() && vary.get<bool>() && !s.contains("cluster")) {
            // A value noise about ten cells across, smoothed: high where a grove grows.
            auto lattice = [&](int x, int y) { return static_cast<double>((static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^
                                                                         static_cast<uint32_t>(std::hash<std::string>{}(id))) % 1000u) / 999.0; };
            auto noise = [&](int x, int y) {
                const double fx = x / 10.0, fy = y / 10.0;
                const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
                const double tx = fx - ix, ty = fy - iy, sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
                const double a = lattice(ix, iy) + (lattice(ix + 1, iy) - lattice(ix, iy)) * sx;
                const double b = lattice(ix, iy + 1) + (lattice(ix + 1, iy + 1) - lattice(ix, iy + 1)) * sx;
                return a + (b - a) * sy;
            };
            for (int c : free) rank[c] = std::pow(noise(c % W, c / W), 2.0) + rng.Unit() * 0.25;
            ranked = true;
        }
        if (ranked) std::stable_sort(free.begin(), free.end(), [&](int a, int b) { return rank[a] > rank[b]; });
    }
    // In patches ("cluster": {size: [lo, hi], fill}): a flower bed, a thicket, a grove, a lawn.
    // Each patch grows from a seed over neighbouring free cells, and most of its cells get one;
    // the same density as scattered, gathered as real plants grow. Patches stay apart.
    if (const json cluster = s.value("cluster", json()); cluster.is_object() && target > 0 && !free.empty()) {
        const IntRange size = RangeOf(cluster.value("size", json::array({4, 16})), 8);
        const double fill = std::clamp(cluster.value("fill", 0.8), 0.1, 1.0);
        // Woods at the edge of the place (place-refine.md §7): the seeds nearest the site's
        // border first (a little shuffled), so the land round a village is wooded and its
        // middle open, as in a strategy game's map.
        if (cluster.value("prefer", "") == "edge") {
            std::vector<std::pair<double, int>> ranked;
            for (int c : free) {
                const int x = c % W, y = c / W;
                ranked.push_back({std::min(std::min(x, y), std::min(W - 1 - x, H - 1 - y)) + rng.Unit() * 6.0, c});
            }
            std::sort(ranked.begin(), ranked.end());
            for (size_t i = 0; i < ranked.size(); ++i) free[i] = ranked[i].second;
        }
        const std::string under = cluster.value("terrain", "");
        std::vector<uint8_t> isFree(W * H, 0), used(W * H, 0);
        for (int c : free) isFree[c] = 1;
        std::vector<int> chosen;
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int seed : free) {
            if (static_cast<int>(chosen.size()) >= target) break;
            if (used[seed]) continue;
            // Not touching another patch: a clear gap between them.
            bool near = false;
            for (int d = 0; d < 4 && !near; ++d) {
                const int x = seed % W + dx[d], y = seed / W + dy[d];
                near = In(x, y) && used[Cell(x, y)];
            }
            if (near) continue;
            const int want = rng.Range(std::max(1, size.lo), std::max(size.lo, size.hi));
            std::vector<int> grown = {seed}, frontier = {seed};
            used[seed] = 1;
            while (static_cast<int>(grown.size()) < want && !frontier.empty()) {
                // A random cell of the frontier: a blob, not a diamond.
                const size_t at = rng.Next() % frontier.size();
                const int c = frontier[at];
                frontier[at] = frontier.back();
                frontier.pop_back();
                for (int d = 0; d < 4; ++d) {
                    const int x = c % W + dx[d], y = c / W + dy[d];
                    if (!In(x, y)) continue;
                    const int n = Cell(x, y);
                    if (!isFree[n] || used[n]) continue;
                    used[n] = 1;
                    grown.push_back(n);
                    frontier.push_back(n);
                    if (static_cast<int>(grown.size()) >= want) break;
                }
            }
            for (int c : grown) {
                if (rng.Unit() < fill) chosen.push_back(c);
                if (!under.empty()) groundUnder_.push_back({c, under});
            }
        }
        free = chosen;
        target = static_cast<int>(chosen.size());
    }
    std::vector<int> placed;
    std::vector<uint8_t> blocked(W * H, 0);
    std::vector<std::string> mix;
    std::vector<double> weights;
    const auto mixGiven = s.value("mix", json::object());
    for (const auto& [concept_, weight] : mixGiven.items()) {
        mix.push_back(concept_);
        weights.push_back(weight.is_number() ? weight.get<double>() : 0);
    }
    int made = 0;
    for (int c : free) {
        if (made >= target) break;
        if (blocked[c]) continue;
        if (solid && owner_[c] >= 0 && !KeepsWay(owner_[c], c % W, c / W, 1, 1)) continue;
        Object o;
        o.id = id + "[" + std::to_string(made) + "]";
        o.statement = id;
        o.node = s.value("node", "");
        o.slot = s.value("slot", "");
        if (!mix.empty()) {
            // Each instance shows one concept of the mix, by weight.
            double pick = rng.Unit() * std::accumulate(weights.begin(), weights.end(), 0.0), run = 0;
            for (size_t k = 0; k < mix.size(); ++k) {
                run += weights[k];
                if (pick <= run) {
                    o.module = mix[k];
                    break;
                }
            }
        }
        o.x = c % W;
        o.y = c / W;
        o.layer = layer;
        if (const auto bay = bayFacing.find(c); bay != bayFacing.end()) o.facing = bay->second, o.faced = "layout";
        AddObject(o);
        ++made;
        // Poisson-disk: nothing else within `spacing`.
        for (int dy = -spacing; dy <= spacing; ++dy) {
            for (int dx = -spacing; dx <= spacing; ++dx) {
                if (In(o.x + dx, o.y + dy)) blocked[Cell(o.x + dx, o.y + dy)] = 1;
            }
        }
    }
    instancesOf_[id] = made;
}

// --- Spaces: dungeons, caves and interiors (7.10) ------------------------------------------------

long EdgeKey(int x, int y, int side) { return (static_cast<long>(y) * 65536L + x) * 2 + side; }

void Solver::MarkWall(int x, int y, int side) { wallEdges_.insert(EdgeKey(x, y, side)); }

bool Solver::KeepsWay(int container, int x, int y, int w, int h) const {
    if (container < 0) return true;
    const Region& r = regions_[container];
    const auto doors = doorsOf_.find(r.id);
    if (doors == doorsOf_.end() || doors->second.empty() || !ontology_.IsA(r.module, "Room")) return true;
    auto open = [&](int c) {
        const int cx = c % W, cy = c / W;
        if (cx >= x && cy >= y && cx < x + w && cy < y + h) return false;
        return owner_[c] == container && !occ_[kProp][c] && !occ_[kBuilding][c] && !solid_[c];
    };
    int total = 0;
    for (int c : r.cells) total += open(c);
    std::vector<int> todo;
    std::set<int> seen;
    // From one door: the others, and all the floor, must be reached from it.
    for (int d : doors->second) {
        if (!open(d)) continue;
        seen.insert(d);
        todo.push_back(d);
        break;
    }
    if (todo.empty()) return false; // it would stand in the doorway
    for (size_t k = 0; k < todo.size(); ++k) {
        const int c = todo[k];
        for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            if (!In(c % W + dx, c / W + dy)) continue;
            const int n = Cell(c % W + dx, c / W + dy);
            if (seen.contains(n) || !open(n) || Crosses(c, n)) continue;
            seen.insert(n);
            todo.push_back(n);
        }
    }
    // Every door joined to the others, and no floor cut off.
    bool doorsJoined = true;
    for (int d : doors->second) doorsJoined = doorsJoined && (seen.contains(d) || !open(d));
    return doorsJoined && static_cast<int>(seen.size()) == total;
}

bool Solver::Crosses(int a, int b) const {
    if (wallEdges_.empty()) return false;
    const int ax = a % W, ay = a / W, bx = b % W, by = b / W;
    if (ay == by) return wallEdges_.contains(EdgeKey(std::max(ax, bx), ay, 1)); // the west edge of the right one
    return wallEdges_.contains(EdgeKey(ax, std::max(ay, by), 0));              // the north edge of the lower one
}

std::vector<uint8_t> Solver::Reached(bool throughProps) {
    std::vector<uint8_t> walk(W * H, 0), seen(W * H, 0);
    for (int c = 0; c < W * H; ++c) {
        walk[c] = (!water_[c] || road_[c]) && !solid_[c] && !occ_[kBuilding][c] && (throughProps || !occ_[kProp][c]);
    }
    std::deque<int> todo;
    for (const auto& [id, a] : anchors_) {
        if ((a.kind == "entrance" || a.kind == "gate") && a.cell >= 0) {
            seen[a.cell] = 1;
            todo.push_back(a.cell);
        }
    }
    std::multimap<int, int> links;
    for (const auto& [a, b] : links_) {
        links.insert({a, b});
        links.insert({b, a});
        walk[a] = walk[b] = 1; // the stairs are stepped on (their object stands on the cell)
    }
    while (!todo.empty()) {
        const int c = todo.front();
        todo.pop_front();
        for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const int nx = c % W + dx, ny = c / W + dy;
            if (!In(nx, ny)) continue;
            const int n = Cell(nx, ny);
            if (seen[n] || !walk[n] || Crosses(c, n)) continue;
            seen[n] = 1;
            todo.push_back(n);
        }
        auto [lo, hi] = links.equal_range(c);
        for (auto it = lo; it != hi; ++it) {
            if (!seen[it->second]) {
                seen[it->second] = 1;
                todo.push_back(it->second);
            }
        }
    }
    return seen;
}

// A room of a dungeon level: a rectangle, or a cave's blob inside one.
std::vector<int> Solver::Blob(int x, int y, int w, int h, Rng& rng) {
    std::vector<uint8_t> on(static_cast<size_t>(w) * h, 0);
    const double cx = (w - 1) / 2.0, cy = (h - 1) / 2.0;
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            const double dx = (i - cx) / std::max(1.0, w / 2.0), dy = (j - cy) / std::max(1.0, h / 2.0);
            on[j * w + i] = dx * dx + dy * dy <= 0.75 + 0.45 * rng.Unit();
        }
    }
    // Two rounds of smoothing (cellular automata): a cell with 5 or more open neighbours opens.
    for (int round = 0; round < 2; ++round) {
        std::vector<uint8_t> next = on;
        for (int j = 0; j < h; ++j) {
            for (int i = 0; i < w; ++i) {
                int n = 0;
                for (int dj = -1; dj <= 1; ++dj) {
                    for (int di = -1; di <= 1; ++di) {
                        if ((di || dj) && i + di >= 0 && j + dj >= 0 && i + di < w && j + dj < h) n += on[(j + dj) * w + i + di];
                    }
                }
                next[j * w + i] = n >= 5 || (on[j * w + i] && n >= 4);
            }
        }
        on = next;
    }
    // The largest connected piece.
    std::vector<int> best, label(on.size(), -1);
    for (size_t start = 0; start < on.size(); ++start) {
        if (!on[start] || label[start] >= 0) continue;
        std::vector<int> piece;
        std::deque<int> todo = {static_cast<int>(start)};
        label[start] = static_cast<int>(start);
        while (!todo.empty()) {
            const int k = todo.front();
            todo.pop_front();
            piece.push_back(k);
            const int i = k % w, j = k / w;
            for (const auto& [di, dj] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                if (i + di < 0 || j + dj < 0 || i + di >= w || j + dj >= h) continue;
                const int n = (j + dj) * w + i + di;
                if (on[n] && label[n] < 0) {
                    label[n] = static_cast<int>(start);
                    todo.push_back(n);
                }
            }
        }
        if (piece.size() > best.size()) best = piece;
    }
    std::vector<int> cells;
    if (static_cast<int>(best.size()) * 2 < w * h) { // too ragged: the rectangle
        for (int j = 0; j < h; ++j) {
            for (int i = 0; i < w; ++i) cells.push_back(Cell(x + i, y + j));
        }
        return cells;
    }
    for (int k : best) cells.push_back(Cell(x + k % w, y + k / w));
    return cells;
}

void Solver::Rooms(const json& s, int container) {
    Rng rng = StreamFor(s);
    const Region parent = regions_[container];
    const std::string id = s.value("id", "");
    const bool cave = s.value("style", "rooms") == "cave";
    const std::string start = s.value("start", ""), goal = s.value("goal", "");
    struct Room {
        std::string id, node, module;
        int x = 0, y = 0, w = 0, h = 0, region = -1;
        bool secret = false, cave = false;
        double cx() const { return x + w / 2.0; }
        double cy() const { return y + h / 2.0; }
    };
    std::vector<Room> rooms;
    for (const auto& r : s.value("into", json::array())) {
        Room room;
        room.id = r.value("id", "");
        room.node = r.value("node", "");
        room.module = r.value("module", "");
        room.secret = r.value("secret", false) && room.id != start && room.id != goal;
        room.cave = cave || r.value("cave", false);
        const IntRange w = RangeOf(r.value("size", json::object()).value("w", json()), 5);
        const IntRange h = RangeOf(r.value("size", json::object()).value("h", json()), 5);
        room.w = rng.Range(w.lo, w.hi);
        room.h = rng.Range(h.lo, h.hi);
        rooms.push_back(room);
    }
    // The start first (near the west side), the goal next (far from it), then the largest.
    std::stable_sort(rooms.begin(), rooms.end(), [&](const Room& a, const Room& b) {
        auto rank = [&](const Room& r) { return r.id == start ? 0 : r.id == goal ? 1 : r.secret ? 3 : 2; };
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        return a.w * a.h > b.w * b.h;
    });
    std::vector<uint8_t> taken(W * H, 0); // rooms and their margins
    const int margin = cave ? 2 : 3; // and corridors between them a real walk (place-refine.md §12)
    int made = 0;
    for (auto& room : rooms) {
        bool placed = false;
        for (int shrink = 0; shrink < 4 && !placed; ++shrink) {
            const int w = std::max(3, room.w - shrink), h = std::max(3, room.h - shrink);
            double best = -1e18;
            int bx = -1, by = -1, tried = 0;
            for (int y = parent.y0 + 1; y + h <= parent.y1 - 1; ++y) {
                for (int x = parent.x0 + 1; x + w <= parent.x1 - 1; ++x) {
                    bool fits = true;
                    for (int yy = y; fits && yy < y + h; ++yy) {
                        for (int xx = x; fits && xx < x + w; ++xx) fits = !taken[Cell(xx, yy)] && owner_[Cell(xx, yy)] == container;
                    }
                    if (!fits) continue;
                    if (++tried > options_.maxCandidates) break;
                    double score = 0.01 * rng.Unit();
                    const double cx = x + w / 2.0, cy = y + h / 2.0;
                    if (room.id == start) {
                        score -= cx - parent.x0 + std::abs(cy - (parent.y0 + parent.y1) / 2.0) * 0.3;
                    } else {
                        // Near the rooms so far, but not on top of them: a compact level.
                        double nearest = 1e9;
                        for (const auto& other : rooms) {
                            if (other.region < 0) continue;
                            nearest = std::min(nearest, std::hypot(other.cx() - cx, other.cy() - cy));
                        }
                        score -= std::abs(nearest - ((w + h) * 1.3 + 4.0));
                        if (room.id == goal && !rooms.empty() && rooms.front().region >= 0) {
                            score += 0.6 * std::hypot(rooms.front().cx() - cx, rooms.front().cy() - cy); // far from the start
                        }
                    }
                    if (score > best) best = score, bx = x, by = y;
                }
            }
            if (bx < 0) continue;
            room.x = bx, room.y = by, room.w = w, room.h = h;
            std::vector<int> cells = room.cave ? Blob(bx, by, w, h, rng) : std::vector<int>{};
            if (!room.cave) {
                for (int yy = by; yy < by + h; ++yy) {
                    for (int xx = bx; xx < bx + w; ++xx) cells.push_back(Cell(xx, yy));
                }
            }
            for (int yy = by - margin; yy < by + h + margin; ++yy) {
                for (int xx = bx - margin; xx < bx + w + margin; ++xx) {
                    if (In(xx, yy)) taken[Cell(xx, yy)] = 1;
                }
            }
            room.region = AddRegion(room.id, room.node, id, container, cells, room.module);
            placed = true;
            ++made;
        }
        if (!placed) Problem("S-UNSAT", s, "no room for " + room.id + " in " + parent.id);
    }
    std::vector<int> placedRooms;
    for (int k = 0; k < static_cast<int>(rooms.size()); ++k) {
        if (rooms[k].region >= 0) placedRooms.push_back(k);
    }
    instancesOf_[id] = made;
    if (placedRooms.empty()) return;

    // The space graph: a spanning tree from the start (Prim), loops, secrets last as leaves.
    struct Edge {
        int a, b;
        double length;
    };
    std::vector<Edge> edges;
    auto dist = [&](int a, int b) { return std::hypot(rooms[a].cx() - rooms[b].cx(), rooms[a].cy() - rooms[b].cy()); };
    std::vector<uint8_t> inTree(rooms.size(), 0);
    inTree[placedRooms.front()] = 1;
    double treeLength = 0;
    for (bool grew = true; grew;) {
        grew = false;
        double best = 1e18;
        int ba = -1, bb = -1;
        for (int a : placedRooms) {
            if (!inTree[a] || rooms[a].secret) continue;
            for (int b : placedRooms) {
                if (inTree[b] || rooms[b].secret) continue;
                if (dist(a, b) < best) best = dist(a, b), ba = a, bb = b;
            }
        }
        if (ba >= 0) {
            inTree[bb] = 1;
            edges.push_back({ba, bb, best});
            treeLength += best;
            grew = true;
        }
    }
    const int treeEdges = static_cast<int>(edges.size());
    // Loops: the shortest edges not in the tree, not much longer than the tree's own.
    {
        std::vector<Edge> extra;
        for (size_t i = 0; i < placedRooms.size(); ++i) {
            for (size_t j = i + 1; j < placedRooms.size(); ++j) {
                const int a = placedRooms[i], b = placedRooms[j];
                if (rooms[a].secret || rooms[b].secret) continue;
                const bool there = std::any_of(edges.begin(), edges.end(), [&](const Edge& e) { return (e.a == a && e.b == b) || (e.a == b && e.b == a); });
                if (!there) extra.push_back({a, b, dist(a, b)});
            }
        }
        std::sort(extra.begin(), extra.end(), [](const Edge& x, const Edge& y) { return x.length < y.length; });
        const double average = treeEdges > 0 ? treeLength / treeEdges : 1e9;
        int loops = s.value("loops", 1);
        for (const auto& e : extra) {
            if (loops <= 0) break;
            if (e.length > average * 1.6) break;
            edges.push_back(e);
            --loops;
        }
    }
    for (int k : placedRooms) {
        if (!rooms[k].secret) continue;
        int nearest = -1;
        for (int other : placedRooms) {
            if (other != k && !rooms[other].secret && (nearest < 0 || dist(k, other) < dist(k, nearest))) nearest = other;
        }
        if (nearest >= 0) edges.push_back({nearest, k, dist(nearest, k)});
    }

    // The corridors: from a door cell just outside one room to one just outside the other.
    std::vector<uint8_t> corridor(W * H, 0);
    std::vector<float> noise(W * H);
    for (auto& n : noise) n = static_cast<float>(rng.Unit());
    auto roomAt = [&](int c) {
        const int o = owner_[c];
        for (int k : placedRooms) {
            if (rooms[k].region == o) return k;
        }
        return -1;
    };
    // The cell outside room a's border nearest to point (px, py), with the border cell inside it.
    auto doorOf = [&](int a, double px, double py) -> std::pair<int, int> {
        double best = 1e18;
        std::pair<int, int> out{-1, -1};
        for (int c : regions_[rooms[a].region].cells) {
            const int x = c % W, y = c / W;
            for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                if (!In(x + dx, y + dy)) continue;
                const int n = Cell(x + dx, y + dy);
                if (owner_[n] != container || roomAt(n) >= 0) continue;
                if (x + dx <= parent.x0 || y + dy <= parent.y0 || x + dx >= parent.x1 - 1 || y + dy >= parent.y1 - 1) continue;
                const double d = std::hypot(x + dx + 0.5 - px, y + dy + 0.5 - py);
                if (d < best) best = d, out = {n, c};
            }
        }
        return out;
    };
    json graph = {{"statement", id}, {"node", s.value("node", "")}, {"start", start}, {"goal", goal}, {"rooms", json::array()}, {"edges", json::array()}};
    for (int k : placedRooms) graph["rooms"].push_back({{"id", rooms[k].id}, {"secret", rooms[k].secret}});
    int connectorIndex = 0;
    for (size_t e = 0; e < edges.size(); ++e) {
        const int a = edges[e].a, b = edges[e].b;
        const auto [outA, inA] = doorOf(a, rooms[b].cx(), rooms[b].cy());
        const auto [outB, inB] = doorOf(b, rooms[a].cx(), rooms[a].cy());
        if (outA < 0 || outB < 0) continue;
        auto cost = [&](int c) -> double {
            if (owner_[c] != container) return std::numeric_limits<double>::infinity();
            const int x = c % W, y = c / W;
            if (x <= parent.x0 || y <= parent.y0 || x >= parent.x1 - 1 || y >= parent.y1 - 1) return std::numeric_limits<double>::infinity();
            if (corridor[c]) return 0.4;
            // Beside a room that isn't one of the two: a corridor skirts it.
            for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                if (!In(x + dx, y + dy)) continue;
                const int k = roomAt(Cell(x + dx, y + dy));
                if (k >= 0 && k != a && k != b) return 6.0;
            }
            return 1.0 + (cave ? 2.5 : 0.25) * noise[c];
        };
        const auto path = AStar(outA, outB, cost);
        if (path.empty()) {
            Problem("S-UNSAT", s, "no way between " + rooms[a].id + " and " + rooms[b].id);
            continue;
        }
        for (int c : path) corridor[c] = 1;
        // The doors: at each end, where the corridor meets the room.
        const bool secret = rooms[a].secret || rooms[b].secret;
        const std::string kind = secret ? "secret-passage" : cave ? "opening" : "door";
        int k = 0;
        for (const auto& [out, in] : {std::pair{outA, inA}, std::pair{outB, inB}}) {
            const std::string connector = id + "/c" + std::to_string(connectorIndex++);
            connectors_.push_back({{"id", connector}, {"statement", id}, {"node", s.value("node", "")}, {"kind", kind},
                                   {"from", rooms[k == 0 ? a : b].id}, {"to", rooms[k == 0 ? b : a].id}, {"edge", static_cast<int>(e)},
                                   {"cell", {out % W, out / W}}, {"inside", {in % W, in / W}}});
            keepFree_[out] = keepFree_[in] = 1;
            doorsOf_[rooms[k == 0 ? a : b].id].push_back(in);
            ++k;
        }
        graph["edges"].push_back({{"a", rooms[a].id}, {"b", rooms[b].id}, {"kind", kind}, {"tree", static_cast<int>(e) < treeEdges}});
    }
    std::vector<int> corridorCells;
    for (int c = 0; c < W * H; ++c) {
        if (corridor[c]) corridorCells.push_back(c);
    }
    if (!corridorCells.empty()) AddRegion(id + "/corridors", s.value("node", ""), id, container, corridorCells);
    // Everything else in the level is rock.
    for (int c : parent.cells) {
        if (owner_[c] == container) solid_[c] = 1;
    }

    // The mission: locks on the way from the start to the goal, each key before its lock.
    std::map<std::string, std::vector<std::pair<std::string, int>>> adjacent; // room -> (room, edge)
    for (size_t e = 0; e < graph["edges"].size(); ++e) {
        const json& edge = graph["edges"][e];
        if (edge["kind"] == "secret-passage") continue;
        adjacent[edge["a"]].push_back({edge["b"], static_cast<int>(e)});
        adjacent[edge["b"]].push_back({edge["a"], static_cast<int>(e)});
    }
    auto bfs = [&](const std::set<int>& closed) {
        std::map<std::string, int> depth;
        std::map<std::string, std::pair<std::string, int>> prev;
        std::deque<std::string> todo;
        if (start.empty() || !regionById_.contains(start)) return std::pair{depth, prev};
        depth[start] = 0;
        todo.push_back(start);
        while (!todo.empty()) {
            const std::string r = todo.front();
            todo.pop_front();
            for (const auto& [n, e] : adjacent[r]) {
                if (closed.contains(e) || depth.contains(n)) continue;
                depth[n] = depth[r] + 1;
                prev[n] = {r, e};
                todo.push_back(n);
            }
        }
        return std::pair{depth, prev};
    };
    const int locks = std::max(0, s.value("locks", 0));
    json keys = json::array();
    if (locks > 0 && !goal.empty()) {
        const auto [depth, prev] = bfs({});
        std::vector<int> pathEdges;
        std::set<std::string> onPath;
        for (std::string r = goal; prev.contains(r); r = prev.at(r).first) {
            pathEdges.push_back(prev.at(r).second);
            onPath.insert(r);
        }
        onPath.insert(start);
        std::reverse(pathEdges.begin(), pathEdges.end());
        // Evenly along the way, never the first step out of the start.
        std::vector<int> lockEdges;
        const int n = static_cast<int>(pathEdges.size());
        for (int k = 0; k < locks && n >= 2; ++k) {
            const int at = std::clamp((k + 1) * n / (locks + 1), 1, n - 1);
            if (std::find(lockEdges.begin(), lockEdges.end(), pathEdges[at]) == lockEdges.end()) lockEdges.push_back(pathEdges[at]);
        }
        std::set<std::string> holding;
        for (size_t k = 0; k < lockEdges.size(); ++k) {
            std::set<int> closed(lockEdges.begin() + static_cast<long>(k), lockEdges.end());
            const auto [reach, unused] = bfs(closed);
            (void)unused;
            // The key: the deepest room reachable before this lock, off the main way if there's one.
            std::string best;
            double bestScore = -1;
            for (const auto& [r, d] : reach) {
                if (holding.contains(r)) continue;
                const double score = d + (onPath.contains(r) ? 0 : 100) + (r == start ? -50 : 0);
                if (score > bestScore) bestScore = score, best = r;
            }
            if (best.empty()) best = start;
            holding.insert(best);
            const int e = lockEdges[k];
            graph["edges"][e]["kind"] = "locked-door";
            graph["edges"][e]["key"] = id + "/key" + std::to_string(k);
            for (auto& c : connectors_) {
                if (c.value("statement", "") == id && c.value("edge", -1) == e) {
                    c["kind"] = "locked-door";
                    c["key"] = id + "/key" + std::to_string(k);
                }
            }
            keys.push_back({{"id", id + "/key" + std::to_string(k)}, {"room", best}, {"edge", e}});
        }
    }
    graph["keys"] = keys;
    // The doors, keys and the way in, as objects and anchors.
    const std::string node = s.value("node", "");
    for (const auto& c : connectors_) {
        if (c.value("statement", "") != id || c.value("kind", "") == "opening") continue;
        const std::string kind = c.value("kind", "door");
        const std::string slot = s.value(kind == "locked-door" ? "lockedSlot" : kind == "secret-passage" ? "secretSlot" : "doorSlot", "door");
        Object o;
        o.id = c.value("id", "");
        o.statement = id;
        o.node = node;
        o.slot = slot;
        o.x = c["cell"][0].get<int>();
        o.y = c["cell"][1].get<int>();
        const int dx = c["inside"][0].get<int>() - o.x, dy = c["inside"][1].get<int>() - o.y;
        o.facing = dx > 0 ? 'e' : dx < 0 ? 'w' : dy > 0 ? 's' : 'n';
        o.layer = kDecal;
        AddObject(o);
    }
    for (const auto& k : keys) {
        const int r = RegionIndex(k.value("room", ""));
        if (r < 0) continue;
        // The free cell nearest the room's middle.
        const double mx = (regions_[r].x0 + regions_[r].x1) / 2.0, my = (regions_[r].y0 + regions_[r].y1) / 2.0;
        int best = -1;
        double bestD = 1e18;
        for (int c : regions_[r].cells) {
            if (keepFree_[c] || occ_[kDecal][c]) continue;
            const double d = std::hypot(c % W + 0.5 - mx, c / W + 0.5 - my) + 0.5; // off the very middle, where the room's own thing goes
            if (std::abs(d - 2.0) < bestD) bestD = std::abs(d - 2.0), best = c;
        }
        if (best < 0) continue;
        Object o;
        o.id = k.value("id", "");
        o.statement = id;
        o.node = node;
        o.slot = s.value("keySlot", "key");
        o.x = best % W;
        o.y = best / W;
        o.layer = kDecal;
        AddObject(o);
        keepFree_[best] = 1;
    }
    // The way in: the dungeon's mouth on the first level, the stairs from the level above on the others.
    auto centreOf = [&](const std::string& room) {
        const int r = RegionIndex(room);
        if (r < 0) return -1;
        const Region& g = regions_[r];
        int best = g.cells.empty() ? -1 : g.cells.front();
        double bestD = 1e18;
        for (int c : g.cells) {
            const double d = std::hypot(c % W + 0.5 - (g.x0 + g.x1) / 2.0, c / W + 0.5 - (g.y0 + g.y1) / 2.0);
            if (d < bestD) bestD = d, best = c;
        }
        return best;
    };
    const std::string from = s.value("stairsFrom", "");
    if (!from.empty() && regionById_.contains(from) && regionById_.contains(start)) {
        const int a = centreOf(from), b = centreOf(start);
        if (a >= 0 && b >= 0) {
            links_.push_back({a, b});
            connectors_.push_back({{"id", id + "/stairs"}, {"statement", id}, {"node", node}, {"kind", "stairs"}, {"from", from}, {"to", start},
                                   {"cell", {a % W, a / W}}, {"inside", {b % W, b / W}}});
            graph["stairsFrom"] = from;
        }
    } else if (!start.empty() && regionById_.contains(start)) {
        const int c = centreOf(start);
        if (c >= 0) anchors_[id + "/way-in"] = {id + "/way-in", node, "entrance", c};
    }
    graphs_.push_back(graph);
}

void Solver::Floorplan(const json& s, int container) {
    Rng rng = StreamFor(s);
    const Region parent = regions_[container];
    const std::string id = s.value("id", ""), node = s.value("node", "");
    const int x0 = parent.x0, y0 = parent.y0, x1 = parent.x1, y1 = parent.y1;
    const int fw = x1 - x0, fh = y1 - y0;
    struct Room {
        std::string id, node, module;
        double area = 9;
        bool pub = false;
        int x = 0, y = 0, w = 0, h = 0;
    };
    std::vector<Room> rooms;
    for (const auto& r : s.value("into", json::array())) {
        Room room;
        room.id = r.value("id", "");
        room.node = r.value("node", "");
        room.module = r.value("module", "");
        const IntRange w = RangeOf(r.value("size", json::object()).value("w", json()), 3);
        const IntRange h = RangeOf(r.value("size", json::object()).value("h", json()), 3);
        room.area = std::max(4.0, ((w.lo + w.hi) / 2.0) * ((h.lo + h.hi) / 2.0));
        room.pub = r.value("public", false);
        rooms.push_back(room);
    }
    if (rooms.empty() || fw < 3 || fh < 3) {
        instancesOf_[id] = 0;
        return;
    }
    const bool horizontal = fw >= fh; // the hall runs along the long side
    const bool hall = rooms.size() >= 3 && (horizontal ? fh : fw) >= 5;
    // A strip [a, b) of the long axis cut among rooms by their areas (each at least 2 cells).
    auto cut = [&](std::vector<Room*>& list, int a, int b, int cross0, int cross1) {
        double total = 0;
        for (auto* r : list) total += r->area;
        int at = a;
        for (size_t k = 0; k < list.size(); ++k) {
            const int left = static_cast<int>(list.size() - k - 1);
            int len = k + 1 == list.size() ? b - at : static_cast<int>(std::lround((b - a) * list[k]->area / std::max(1.0, total)));
            len = std::clamp(len, 2, std::max(2, b - at - 2 * left));
            Room& r = *list[k];
            if (horizontal) r.x = at, r.w = len, r.y = cross0, r.h = cross1 - cross0;
            else r.y = at, r.h = len, r.x = cross0, r.w = cross1 - cross0;
            at += len;
        }
    };
    const int longA = horizontal ? x0 : y0, longB = horizontal ? x1 : y1;
    const int crossA = horizontal ? y0 : x0, crossB = horizontal ? y1 : x1;
    int hallLine = -1; // the row (or column) of the hall
    std::vector<int> hallCells;
    if (hall) {
        hallLine = crossA + (crossB - crossA) / 2;
        // Two bands either side; public rooms on the side the front door is on (the near band).
        std::vector<Room*> nearBand, farBand;
        double nearArea = 0, farArea = 0;
        std::vector<Room*> order;
        for (auto& r : rooms) order.push_back(&r);
        std::stable_sort(order.begin(), order.end(), [](const Room* a, const Room* b) {
            if (a->pub != b->pub) return a->pub;
            return a->area > b->area;
        });
        for (auto* r : order) {
            const bool toNear = nearArea * (crossB - hallLine - 1) <= farArea * (hallLine - crossA);
            (toNear ? nearBand : farBand).push_back(r);
            (toNear ? nearArea : farArea) += r->area;
        }
        if (farBand.empty() && nearBand.size() > 1) {
            farBand.push_back(nearBand.back());
            nearBand.pop_back();
        }
        cut(nearBand, longA, longB, hallLine + 1, crossB);
        if (!farBand.empty()) cut(farBand, longA, longB, crossA, hallLine);
        for (int t = longA; t < longB; ++t) hallCells.push_back(horizontal ? Cell(t, hallLine) : Cell(hallLine, t));
    } else {
        std::vector<Room*> all;
        for (auto& r : rooms) all.push_back(&r);
        cut(all, longA, longB, crossA, crossB);
    }
    // The regions: the hall, then each room.
    int hallRegion = -1;
    if (!hallCells.empty()) hallRegion = AddRegion(id + "/hall", node, id, container, hallCells, "");
    int made = 0;
    for (auto& r : rooms) {
        std::vector<int> cells;
        for (int y = r.y; y < r.y + r.h; ++y) {
            for (int x = r.x; x < r.x + r.w; ++x) {
                if (In(x, y) && owner_[Cell(x, y)] == container) cells.push_back(Cell(x, y));
            }
        }
        if (cells.size() < 4) {
            Problem("S-UNSAT", s, "no room for " + r.id + " on " + parent.id);
            continue;
        }
        AddRegion(r.id, r.node, id, container, cells, r.module);
        ++made;
    }
    instancesOf_[id] = made;
    // Walls: every edge between two different spaces, and round the outside.
    auto spaceOf = [&](int x, int y) { return In(x, y) && owner_[Cell(x, y)] >= 0 && (owner_[Cell(x, y)] == container || regions_[owner_[Cell(x, y)]].parent == container) ? owner_[Cell(x, y)] : -1; };
    std::set<std::array<int, 3>> walls;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int here = (x < x1 && y < y1) ? spaceOf(x, y) : -1;
            if (x < x1 && here != ((y - 1 >= y0) ? spaceOf(x, y - 1) : -1)) walls.insert({x, y, 0});
            if (y < y1 && here != ((x - 1 >= x0) ? spaceOf(x - 1, y) : -1)) walls.insert({x, y, 1});
        }
    }
    // Doors: each room onto the hall (its middle), or onto the next room; the front door outside.
    std::vector<std::array<int, 3>> doors;
    auto door = [&](int ax, int ay, int bx, int by) {
        // The edge between cell a and the neighbour b.
        std::array<int, 3> e = ay == by ? std::array<int, 3>{std::max(ax, bx), ay, 1} : std::array<int, 3>{ax, std::max(ay, by), 0};
        if (walls.erase(e)) doors.push_back(e);
        for (const auto& [cx, cy] : {std::pair{ax, ay}, std::pair{bx, by}}) {
            if (In(cx, cy)) keepFree_[Cell(cx, cy)] = 1;
        }
        if (In(ax, ay) && owner_[Cell(ax, ay)] >= 0) doorsOf_[regions_[owner_[Cell(ax, ay)]].id].push_back(Cell(ax, ay));
        if (In(bx, by) && owner_[Cell(bx, by)] >= 0) doorsOf_[regions_[owner_[Cell(bx, by)]].id].push_back(Cell(bx, by));
    };
    if (hall) {
        for (const auto& r : rooms) {
            const int mid = horizontal ? r.x + r.w / 2 : r.y + r.h / 2;
            if (horizontal) {
                const bool above = r.y < hallLine;
                door(mid, above ? r.y + r.h - 1 : r.y, mid, hallLine);
            } else {
                const bool left = r.x < hallLine;
                door(left ? r.x + r.w - 1 : r.x, mid, hallLine, mid);
            }
        }
    } else {
        for (size_t k = 0; k + 1 < rooms.size(); ++k) {
            const Room& a = rooms[k];
            if (horizontal) door(a.x + a.w - 1, a.y + a.h / 2, a.x + a.w, a.y + a.h / 2);
            else door(a.x + a.w / 2, a.y + a.h - 1, a.x + a.w / 2, a.y + a.h);
        }
    }
    // The front door: at the hall's near end (or the first room's), onto the way in outside.
    int stairsCell = -1;
    if (!s.value("entrance", "").empty()) {
        int ix = x0, iy = hall && horizontal ? hallLine : y0 + fh / 2;
        if (hall && !horizontal) ix = hallLine, iy = y1 - 1;
        if (!hall) ix = rooms.front().x, iy = rooms.front().y + rooms.front().h / 2;
        const int ox = hall && !horizontal ? ix : ix - 1, oy = hall && !horizontal ? iy + 1 : iy;
        door(ix, iy, ox, oy);
        if (In(ox, oy)) anchors_[id + "/front"] = {id + "/front", node, "entrance", Cell(ox, oy)};
        Object o;
        o.id = id + "/front-door";
        o.statement = id;
        o.node = node;
        o.slot = s.value("doorSlot", "door");
        o.x = ix, o.y = iy;
        o.facing = ox < ix ? 'w' : ox > ix ? 'e' : oy > iy ? 's' : 'n';
        o.layer = kDecal;
        AddObject(o);
    }
    for (const auto& e : doors) {
        if (e == doors.back() && !s.value("entrance", "").empty()) break; // the front door is placed above
        Object o;
        o.id = id + "/door" + std::to_string(&e - doors.data());
        o.statement = id;
        o.node = node;
        o.slot = s.value("doorSlot", "door");
        o.x = e[0], o.y = e[1];
        o.facing = e[2] == 0 ? 'n' : 'w';
        o.layer = kDecal;
        AddObject(o);
    }
    // The stairs: at the far end of the hall (the same place on every floor), linked to the floor below.
    if (s.value("stairs", false)) {
        const int sx = hall ? (horizontal ? x1 - 1 : hallLine) : x1 - 1, sy = hall ? (horizontal ? hallLine : y0) : y0;
        if (In(sx, sy)) {
            stairsCell = Cell(sx, sy);
            Object o;
            o.id = id + "/stairs";
            o.statement = id;
            o.node = node;
            o.slot = s.value("stairsSlot", "stairs");
            o.x = sx, o.y = sy;
            o.layer = kDecal;
            AddObject(o);
            keepFree_[stairsCell] = 1;
            stairsOf_[id] = stairsCell;
            const std::string below = s.value("stairsFrom", "");
            if (auto it = stairsOf_.find(below); it != stairsOf_.end()) {
                links_.push_back({it->second, stairsCell});
                connectors_.push_back({{"id", id + "/stairs"}, {"statement", id}, {"node", node}, {"kind", "stairs"}, {"from", below}, {"to", id},
                                       {"cell", {it->second % W, it->second / W}}, {"inside", {sx, sy}}});
            }
        }
    }
    // Windows: along the outside walls of the rooms, every few cells, kept clear inside.
    for (const auto& r : rooms) {
        int k = 0;
        for (int t = 1; t + 1 < (horizontal ? r.w : r.h); ++t) {
            const bool outside = horizontal ? (r.y == y0 || r.y + r.h == y1) : (r.x == x0 || r.x + r.w == x1);
            if (!outside || ++k % 3 != 0) continue;
            const int wx = horizontal ? r.x + t : (r.x == x0 ? r.x : r.x + r.w - 1);
            const int wy = horizontal ? (r.y == y0 ? r.y : r.y + r.h - 1) : r.y + t;
            if (!In(wx, wy) || keepFree_[Cell(wx, wy)]) continue;
            Object o;
            o.id = id + "/window" + std::to_string(objectsByStatement_[id].size());
            o.statement = id;
            o.node = s.value("wallNode", node);
            o.slot = s.value("windowSlot", "window");
            o.x = wx, o.y = wy;
            o.facing = horizontal ? (r.y == y0 ? 'n' : 's') : (r.x == x0 ? 'w' : 'e');
            o.layer = kDecal;
            AddObject(o);
            keepFree_[Cell(wx, wy)] = 1;
        }
    }
    LineResult line;
    line.id = id + "/walls";
    line.node = s.value("wallNode", node);
    line.slot = s.value("wallSlot", "wall");
    for (const auto& e : walls) {
        line.edges.push_back(e);
        MarkWall(e[0], e[1], e[2]);
    }
    for (const auto& e : doors) line.gaps.push_back(e);
    std::sort(line.edges.begin(), line.edges.end());
    lines_.push_back(line);
    instancesOf_[line.id] = 1;
    (void)rng;
    (void)hallRegion;
}

void Solver::Connect(const json& s, int container) {
    // A network between places (trails to points of interest, roads between a
    // region's sites): a tree from the entrances by Prim, each leg an A* path.
    std::vector<std::string> members;
    for (const auto& m : s.value("members", json::array())) {
        if (m.is_string() && regionById_.contains(m.get<std::string>())) members.push_back(m.get<std::string>());
    }
    if (members.empty()) {
        instancesOf_[s.value("id", "")] = 0;
        return;
    }
    std::vector<int> joined; // cells already on the network
    for (const auto& f : s.value("from", json::array())) {
        for (int c : Cells(f.get<std::string>())) joined.push_back(c);
    }
    const std::string cls = s.value("class", "trail");
    const int width = std::max(1, s.value("width", 1));
    const std::string terrain = s.value("terrain", "dirt");
    Rng rng = StreamFor(s);
    std::vector<float> noise(W * H);
    for (auto& n : noise) n = static_cast<float>(rng.Unit());
    std::set<std::string> left(members.begin(), members.end());
    int legs = 0;
    auto entry = [&](const std::string& m) {
        // The middle of its side facing the network so far.
        const Region& g = regions_[RegionIndex(m)];
        return Cell((g.x0 + g.x1) / 2, std::min(H - 1, g.y1));
    };
    if (joined.empty()) {
        joined.push_back(entry(*left.begin()));
    }
    while (!left.empty()) {
        // The nearest member to the network.
        std::string best;
        double bestD = 1e18;
        int bestFrom = -1;
        for (const auto& m : left) {
            const Region& g = regions_[RegionIndex(m)];
            const double mx = (g.x0 + g.x1) / 2.0, my = (g.y0 + g.y1) / 2.0;
            for (size_t k = 0; k < joined.size(); k += std::max<size_t>(1, joined.size() / 400)) {
                const double d = std::hypot(joined[k] % W - mx, joined[k] / W - my);
                if (d < bestD) bestD = d, best = m, bestFrom = joined[k];
            }
        }
        left.erase(best);
        const int target = RegionIndex(best);
        auto cost = [&](int c) -> double {
            if (occ_[kBuilding][c] || solid_[c]) return std::numeric_limits<double>::infinity();
            const int o = owner_[c];
            if (o >= 0 && o != target && regions_[o].parent >= 0 && std::find(members.begin(), members.end(), regions_[o].id) != members.end()) return 12.0;
            if (water_[c]) return 8.0;
            if (road_[c]) return 0.2;
            return 1.0 + 0.6 * noise[c];
        };
        // To the member's nearest border cell.
        const Region& g = regions_[target];
        int goal = g.cells.front();
        double goalD = 1e18;
        for (int c : g.cells) {
            const double d = std::hypot(c % W - bestFrom % W, c / W - bestFrom / W);
            if (d < goalD) goalD = d, goal = c;
        }
        auto path = AStar(bestFrom, goal, cost);
        if (path.empty()) {
            Problem("S-UNSAT", s, "no way to " + best);
            continue;
        }
        PathResult p;
        p.id = s.value("id", "") + "/" + std::to_string(legs++);
        p.node = s.value("node", "");
        p.cls = cls;
        p.terrain = terrain;
        p.width = width;
        // Up to its edge: the member keeps its own ground.
        while (!path.empty() && owner_[path.back()] == target) path.pop_back();
        p.cells = path;
        MarkRoad(path, width, RankOf(cls), -1);
        joined.insert(joined.end(), path.begin(), path.end());
        // Its front faces the way it's reached.
        if (!path.empty()) {
            const int last = path.back(), lx = last % W, ly = last / W;
            Region& r = regions_[target];
            r.facing = ly < r.y0 ? 'n' : ly >= r.y1 ? 's' : lx < r.x0 ? 'w' : 'e';
            r.street.push_back(last);
        }
        paths_.push_back(p);
    }
    instancesOf_[s.value("id", "")] = legs;
}

void Solver::Flood(const json& s, int container) {
    // A moat: the container's own cells (not its sub-regions) become water; a road over it is its bridge.
    for (int c : regions_[container].cells) {
        if (owner_[c] == container) water_[c] = 1;
    }
    instancesOf_[s.value("id", "")] = 1;
}

void Solver::SpaceChecks(const json& s, json& result) {
    const std::string predicate = s.value("predicate", "");
    if (predicate == "completable") {
        // Start at the first way in; collect keys; open what they open; reach the goal.
        std::map<std::string, std::vector<std::tuple<std::string, std::string, std::string>>> adjacent; // room -> (room, kind, key)
        std::map<std::string, std::vector<std::string>> keysIn;                                           // room -> keys
        std::string start, goal;
        for (const auto& g : graphs_) {
            if (start.empty() && !g.contains("stairsFrom")) start = g.value("start", "");
            goal = g.value("goal", "").empty() ? goal : g.value("goal", "");
            for (const auto& e : g["edges"]) {
                const std::string kind = e.value("kind", "door");
                if (kind == "secret-passage") continue;
                adjacent[e["a"]].push_back({e["b"], kind, e.value("key", "")});
                adjacent[e["b"]].push_back({e["a"], kind, e.value("key", "")});
            }
            for (const auto& k : g["keys"]) keysIn[k.value("room", "")].push_back(k.value("id", ""));
            if (g.contains("stairsFrom")) {
                adjacent[g["stairsFrom"]].push_back({g["start"], "stairs", ""});
                adjacent[g["start"]].push_back({g["stairsFrom"], "stairs", ""});
            }
        }
        const json args = s.value("args", json::object());
        if (args.contains("goal") && args["goal"].is_string()) goal = args["goal"];
        std::set<std::string> reached, keys;
        bool grew = !start.empty();
        if (grew) reached.insert(start);
        while (grew) {
            grew = false;
            for (const auto& r : std::vector<std::string>(reached.begin(), reached.end())) {
                for (const auto& k : keysIn[r]) grew = keys.insert(k).second || grew; // a key found opens its lock on the next pass
                for (const auto& [n, kind, key] : adjacent[r]) {
                    if (reached.contains(n) || (kind == "locked-door" && !keys.contains(key))) continue;
                    reached.insert(n);
                    grew = true;
                }
            }
        }
        result["ok"] = !goal.empty() && reached.contains(goal);
        result["detail"] = (reached.contains(goal) ? "the goal is reached" : "the goal can't be reached") + std::string(" with ") +
                           std::to_string(keys.size()) + " key(s) found";
    } else if (predicate == "rooms-reachable") {
        // Every room but the secret ones has a cell a walker reaches (doors, stairs, round the furniture).
        const auto seen = Reached(false);
        std::set<std::string> secret;
        for (const auto& g : graphs_) {
            for (const auto& r : g["rooms"]) {
                if (r.value("secret", false)) secret.insert(r.value("id", ""));
            }
        }
        const json args = s.value("args", json::object());
        const std::string cls = args.value("class", "Room");
        int rooms = 0, reached = 0;
        json missing = json::array();
        for (const auto& r : regions_) {
            if (!ontology_.IsA(r.module, cls) || secret.contains(r.id)) continue;
            ++rooms;
            const bool hit = std::any_of(r.cells.begin(), r.cells.end(), [&](int c) { return seen[c] != 0; });
            reached += hit;
            if (!hit && missing.size() < 20) missing.push_back(r.id);
        }
        result["ok"] = rooms > 0 && reached == rooms;
        result["detail"] = std::to_string(reached) + " of " + std::to_string(rooms) + " rooms reachable";
        if (!missing.empty()) result["unreached"] = missing;
    } else if (predicate == "groups-reachable") {
        // Each furniture group's anchor can be walked up to.
        const auto seen = Reached(false);
        int groups = 0, reached = 0;
        json missing = json::array();
        for (const auto& statement : s.value("args", json::object()).value("anchors", json::array())) {
            auto it = objectsByStatement_.find(statement.get<std::string>());
            if (it == objectsByStatement_.end()) continue;
            for (int index : it->second) {
                const Object& o = objects_[index];
                ++groups;
                bool hit = false;
                for (int y = o.y - 1; y <= o.y + o.h && !hit; ++y) {
                    for (int x = o.x - 1; x <= o.x + o.w && !hit; ++x) {
                        if (In(x, y) && (x < o.x || y < o.y || x >= o.x + o.w || y >= o.y + o.h)) hit = seen[Cell(x, y)] != 0;
                    }
                }
                reached += hit;
                if (!hit && missing.size() < 20) missing.push_back(o.id);
            }
        }
        result["ok"] = reached == groups;
        result["detail"] = std::to_string(reached) + " of " + std::to_string(groups) + " furniture groups can be walked to";
        if (!missing.empty()) result["unreached"] = missing;
    } else if (predicate == "doors-free") {
        int blocked = 0;
        for (int c = 0; c < W * H; ++c) blocked += keepFree_[c] && (occ_[kProp][c] || occ_[kBuilding][c]);
        result["ok"] = blocked == 0;
        result["detail"] = std::to_string(blocked) + " doorway or window cells blocked";
    }
}

json Solver::SpacesJson() const {
    json solid = json::array(), free = json::array();
    std::vector<int> solidCells, freeCells;
    for (int c = 0; c < W * H; ++c) {
        if (solid_[c]) solidCells.push_back(c);
        if (keepFree_[c]) freeCells.push_back(c);
    }
    json walls = json::array();
    for (long k : wallEdges_) walls.push_back({static_cast<int>((k / 2) % 65536), static_cast<int>((k / 2) / 65536), static_cast<int>(k % 2)});
    json links = json::array();
    for (const auto& [a, b] : links_) links.push_back({{a % W, a / W}, {b % W, b / W}});
    json doors = json::object();
    for (const auto& [r, cells] : doorsOf_) {
        for (int c : cells) doors[r].push_back({c % W, c / W});
    }
    json stairs = json::object();
    for (const auto& [st, c] : stairsOf_) stairs[st] = {c % W, c / W};
    return {{"solid", EncodeCells(solidCells, W)}, {"keepFree", EncodeCells(freeCells, W)}, {"walls", walls}, {"links", links},
            {"doors", doors}, {"stairs", stairs}, {"graphs", graphs_}};
}

void Solver::RestoreSpaces(const json& previous) {
    const json spaces = previous.value("spaces", json::object());
    if (spaces.empty()) return;
    // What a rooms or floorplan statement made stays when the statement isn't redone.
    for (int c : DecodeCells(spaces.value("solid", json::array()), W)) solid_[c] = 1;
    for (int c : DecodeCells(spaces.value("keepFree", json::array()), W)) keepFree_[c] = 1;
    for (const auto& e : spaces.value("walls", json::array())) MarkWall(e[0].get<int>(), e[1].get<int>(), e[2].get<int>());
    for (const auto& l : spaces.value("links", json::array())) {
        links_.push_back({Cell(l[0][0].get<int>(), l[0][1].get<int>()), Cell(l[1][0].get<int>(), l[1][1].get<int>())});
    }
    const json doors = spaces.value("doors", json::object()), stairs = spaces.value("stairs", json::object());
    for (const auto& [r, cells] : doors.items()) {
        for (const auto& c : cells) doorsOf_[r].push_back(Cell(c[0].get<int>(), c[1].get<int>()));
    }
    for (const auto& [st, c] : stairs.items()) stairsOf_[st] = Cell(c[0].get<int>(), c[1].get<int>());
    for (const auto& g : spaces.value("graphs", json::array())) graphs_.push_back(g);
    for (const auto& c : previous.value("connectors", json::array())) connectors_.push_back(c);
}

// --- The region tree ----------------------------------------------------------------------------

int StepOf(const json& s) {
    const std::string op = s.value("op", "");
    if (op == "path" || op == "network") return 1;
    if (op == "partition" || op == "subdivide" || op == "rooms" || op == "floorplan") return 2;
    if (op == "place" && s.value("primary", false)) return 3;
    if (op == "region" || op == "anchor") return 4;
    if (op == "connect" || op == "flood") return 5;
    if (op == "place") return 6;
    if (op == "line") return 7;
    if (op == "rows" || op == "grid") return 8;
    if (op == "scatter") return 9;
    return 99;
}

void Solver::SolveRegion(int index) {
    const std::string id = regions_[index].id;
    auto it = byContainer_.find(id);
    std::vector<const json*> list = it == byContainer_.end() ? std::vector<const json*>{} : it->second;
    // Rivers before roads, main roads before streets; large before small.
    auto pathRank = [](const json* s) {
        const std::string cls = s->value("class", "street");
        return cls == "river" ? 0 : 5 - RankOf(cls);
    };
    auto area = [](const json* s) {
        const json fp = s->value("footprint", s->value("size", json::object()));
        return RangeOf(fp.value("w", json()), 1).hi * RangeOf(fp.value("h", json()), 1).hi;
    };
    std::stable_sort(list.begin(), list.end(), [&](const json* a, const json* b) {
        if (StepOf(*a) != StepOf(*b)) return StepOf(*a) < StepOf(*b);
        if (StepOf(*a) == 1 && pathRank(a) != pathRank(b)) return pathRank(a) < pathRank(b);
        if (area(a) != area(b)) return area(a) > area(b);
        return a->value("id", "") < b->value("id", "");
    });
    auto run = [&](const json& s) {
        const std::string op = s.value("op", "");
        const auto began = std::chrono::steady_clock::now();
        struct Timed {
            std::map<std::string, double>& into;
            std::string op;
            std::chrono::steady_clock::time_point began;
            ~Timed() { into[op] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count(); }
        } timed{msByOp_, op, began};
        // A partial solve: what was kept (a locked part) isn't made again.
        if (options_.previous && ((op == "region" && regionById_.contains(s.value("id", ""))) ||
                                  (op == "place" && (objectById_.contains(s.value("id", "")) || objectsByStatement_.contains(s.value("id", "")))))) {
            return;
        }
        if (op == "path") Path(s);
        else if (op == "network") Network(s, index);
        else if (op == "partition") Partition(s, index);
        else if (op == "subdivide") Subdivide(s, index);
        else if (op == "place") Place(s, index);
        else if (op == "region") NestedRegion(s, index);
        else if (op == "anchor") Anchor(s);
        else if (op == "line") Line(s);
        else if (op == "rows") Rows(s, index, false);
        else if (op == "grid") Rows(s, index, true);
        else if (op == "scatter") Scatter(s, index);
        else if (op == "rooms") Rooms(s, index);
        else if (op == "floorplan") Floorplan(s, index);
        else if (op == "connect") Connect(s, index);
        else if (op == "flood") Flood(s, index);
    };
    for (const json* s : list) {
        const std::string in = s->value("in", "");
        const size_t star = in.find("[*]");
        if (star == std::string::npos) {
            run(*s);
            continue;
        }
        // An instance statement: once per instance there is (x[0], x[1], ...), as x[i].
        const std::string prefix = in.substr(0, star) + "[", rest = in.substr(star + 3);
        std::vector<std::string> instances;
        for (const auto& r : regions_) {
            if (r.id.rfind(prefix, 0) != 0) continue;
            const size_t close = r.id.find(']', prefix.size());
            if (close == std::string::npos || r.id.substr(close + 1) != rest) continue;
            instances.push_back(r.id.substr(prefix.size(), close - prefix.size()));
        }
        for (const auto& k : instances) {
            json copy = *s;
            copy["in"] = prefix + k + "]" + rest;
            std::string id = s->value("id", "");
            if (const size_t at = id.find("[*]"); at != std::string::npos) id.replace(at, 3, "[" + k + "]");
            copy["id"] = id;
            instanceStatements_.push_back(std::move(copy));
            run(instanceStatements_.back());
        }
    }
    // Then its sub-regions, large first.
    std::vector<int> children;
    for (int r = 0; r < static_cast<int>(regions_.size()); ++r) {
        if (regions_[r].parent == index) children.push_back(r);
    }
    std::stable_sort(children.begin(), children.end(), [&](int a, int b) {
        if (regions_[a].cells.size() != regions_[b].cells.size()) return regions_[a].cells.size() > regions_[b].cells.size();
        return regions_[a].id < regions_[b].id;
    });
    for (int c : children) SolveRegion(c);
}

json Solver::Terrain() {
    std::vector<std::string> palette = {""};
    std::vector<int> grid(W * H, 0);
    auto indexOf = [&](const std::string& t) {
        auto it = std::find(palette.begin(), palette.end(), t);
        if (it != palette.end()) return static_cast<int>(it - palette.begin());
        palette.push_back(t);
        return static_cast<int>(palette.size() - 1);
    };
    // Fills, outer regions first; then paths and water over them.
    std::vector<const json*> fills;
    for (const json* s : statements_) {
        if (s->value("op", "") == "fill") fills.push_back(s);
    }
    auto depthOf = [&](const json* s) {
        const int r = RegionIndex(ContainerOf(*s));
        return r >= 0 ? regions_[r].depth : 0;
    };
    // Deeper regions over their parents; patterns (a park's footpaths) over the ground they cross.
    std::stable_sort(fills.begin(), fills.end(), [&](const json* a, const json* b) {
        const bool pa = a->contains("pattern"), pb = b->contains("pattern");
        return depthOf(a) != depthOf(b) ? depthOf(a) < depthOf(b) : !pa && pb;
    });
    for (const json* s : fills) {
        const int t = indexOf(s->value("terrain", "grass"));
        const std::vector<int> cells = Cells(s->value("in", ""));
        if (s->value("pattern", "") == "cross" && !cells.empty()) {
            // Footpaths crossing the region through its middle, one each way.
            int x0 = W, y0 = H, x1 = 0, y1 = 0;
            for (int c : cells) x0 = std::min(x0, c % W), y0 = std::min(y0, c / W), x1 = std::max(x1, c % W), y1 = std::max(y1, c / W);
            const int width = std::max(1, s->value("width", 1)), mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
            for (int c : cells) {
                if (std::abs(c % W - mx) * 2 < width + 1 || std::abs(c / W - my) * 2 < width + 1) grid[c] = t;
            }
            continue;
        }
        for (int c : cells) grid[c] = t;
    }
    for (const auto& [c, t] : groundUnder_) grid[c] = indexOf(t); // a wood's floor
    // Road cells take the surface of their class (the first path of that class says it).
    std::array<int, 5> surface{};
    for (const auto& p : paths_) {
        if (p.cls != "river" && surface[RankOf(p.cls)] == 0) surface[RankOf(p.cls)] = indexOf(p.terrain);
    }
    const int dirt = indexOf("dirt");
    for (int c = 0; c < W * H; ++c) {
        if (road_[c]) grid[c] = surface[road_[c]] ? surface[road_[c]] : dirt;
    }
    // Sidewalks: a class's edge terrain on its road cells that touch land (the outer cells of its width).
    std::array<int, 5> edge{};
    for (const auto& p : paths_) {
        if (p.cls != "river" && !p.edge.empty() && edge[RankOf(p.cls)] == 0) edge[RankOf(p.cls)] = indexOf(p.edge);
    }
    for (int c = 0; c < W * H; ++c) {
        if (!road_[c] || edge[road_[c]] == 0) continue;
        const int x = c % W, y = c / W;
        bool rim = false;
        for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            rim = rim || (In(x + dx, y + dy) && !road_[Cell(x + dx, y + dy)] && !water_[Cell(x + dx, y + dy)]);
        }
        if (rim) grid[c] = edge[road_[c]];
    }
    const int water = indexOf("water");
    for (int c = 0; c < W * H; ++c) {
        if (water_[c] && !road_[c]) grid[c] = water;
    }
    json rows = json::array();
    for (int y = 0; y < H; ++y) {
        json runs = json::array();
        int x = 0;
        while (x < W) {
            const int t = grid[Cell(x, y)];
            int run = 1;
            while (x + run < W && grid[Cell(x + run, y)] == t) ++run;
            runs.push_back({run, t});
            x += run;
        }
        rows.push_back(runs);
    }
    json bridges = json::array();
    for (int c = 0; c < W * H; ++c) {
        if (water_[c] && road_[c]) bridges.push_back({c % W, c / W});
    }
    return {{"palette", palette}, {"rows", rows}, {"bridges", bridges}};
}

void Solver::Checks() {
    // Walkable: not water (bridges are), not under a building or a solid prop.
    std::vector<uint8_t> walk(W * H, 0);
    for (int c = 0; c < W * H; ++c) walk[c] = (!water_[c] || road_[c]) && !solid_[c] && !occ_[kBuilding][c] && !occ_[kProp][c];
    for (const json* s : statements_) {
        if (s->value("op", "") != "check") continue;
        const std::string predicate = s->value("predicate", "");
        const json args = s->value("args", json::object());
        json result = {{"id", s->value("id", "")}, {"node", s->value("node", "")}, {"predicate", predicate}, {"ok", true}};
        if (predicate == "count") {
            const std::string ref = args.value("ref", "");
            const int got = instancesOf_.contains(ref) ? instancesOf_[ref] : 0;
            const IntRange want = RangeOf(args.value("range", json()), got);
            result["ok"] = got >= want.lo && got <= want.hi;
            result["detail"] = std::to_string(got) + " of " + std::to_string(want.lo) + (want.hi != want.lo ? "-" + std::to_string(want.hi) : "");
        } else if (predicate == "closed") {
            const std::string ref = args.value("ref", "");
            auto it = std::find_if(lines_.begin(), lines_.end(), [&](const LineResult& l) { return l.id == ref; });
            result["ok"] = it != lines_.end() && it->closed && !it->edges.empty();
        } else if (predicate == "reachable") {
            // From the entrances, every door.
            std::vector<uint8_t> seen(W * H, 0);
            std::deque<int> todo;
            for (const auto& [id, a] : anchors_) {
                if ((a.kind == "entrance" || a.kind == "gate") && a.cell >= 0) {
                    seen[a.cell] = 1;
                    todo.push_back(a.cell);
                }
            }
            while (!todo.empty()) {
                const int c = todo.front();
                todo.pop_front();
                for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    const int nx = c % W + dx, ny = c / W + dy;
                    if (!In(nx, ny) || seen[Cell(nx, ny)] || !walk[Cell(nx, ny)]) continue;
                    seen[Cell(nx, ny)] = 1;
                    todo.push_back(Cell(nx, ny));
                }
            }
            int doors = 0, unreached = 0;
            json missing = json::array();
            for (const auto& o : objects_) {
                if (!o.primary || o.layer != kBuilding) continue;
                ++doors;
                const auto door = Cells(o.id + ".door");
                if (door.empty() || !seen[door.front()]) {
                    ++unreached;
                    if (missing.size() < 20) missing.push_back(o.id);
                }
            }
            result["ok"] = unreached == 0;
            result["detail"] = std::to_string(doors - unreached) + " of " + std::to_string(doors) + " doors reachable";
            if (!missing.empty()) result["unreached"] = missing;
        } else if (predicate == "footprint-density") {
            const auto cells = Cells(args.value("ref", ""));
            int built = 0;
            for (int c : cells) built += occ_[kBuilding][c] ? 1 : 0;
            const double density = cells.empty() ? 0 : static_cast<double>(built) / cells.size();
            const json band = args.value("band", json::array({0, 1}));
            result["ok"] = density >= band[0].get<double>() - 1e-9 && density <= band[1].get<double>() + 1e-9;
            result["detail"] = density;
        } else if (predicate == "filled") {
            result["ok"] = true; // assets are chosen when building (7.7)
        } else {
            SpaceChecks(*s, result);
        }
        if (!result["ok"].get<bool>()) {
            problems_.push_back({{"code", "C-FAIL"}, {"statement", result["id"]}, {"node", result["node"]},
                                 {"message", predicate + " failed" + (result.contains("detail") ? ": " + (result["detail"].is_string() ? result["detail"].get<std::string>() : result["detail"].dump()) : "")}});
        }
        checks_.push_back(result);
    }
}

namespace {
bool Under(const std::set<std::string>& roots, const std::string& node) {
    for (const auto& r : roots) {
        if (node == r || node.rfind(r + "/", 0) == 0 || node.rfind(r + "#", 0) == 0) return true;
    }
    return false;
}
} // namespace

bool Solver::Redo(const std::string& node) const { return Under(options_.resolve, node); }
bool Solver::Locked(const std::string& node) const { return Under(options_.locked, node); }

bool Solver::StatementRedone(const std::string& statementId, const std::string& node) const {
    if (!options_.previous) return true;
    std::string n = node;
    if (n.empty()) {
        // An instance's statement (x[3]/crops): the one it was made from (x[*]/crops).
        std::string id = statementId;
        for (size_t open = id.find('['); open != std::string::npos; open = id.find('[', open + 1)) {
            const size_t close = id.find(']', open);
            if (close == std::string::npos) break;
            id.replace(open, close - open + 1, "[*]");
            open += 2;
        }
        auto it = statementById_.find(statementId);
        if (it == statementById_.end()) it = statementById_.find(id);
        n = it != statementById_.end() ? it->second->value("node", "") : "";
    }
    // Not under a lock by its node (a template) nor by its own id (one locked instance's).
    return Redo(n) && !Locked(n) && !Locked(statementId);
}

void Solver::Restore(const json& previous) {
    // Everything a statement that isn't redone made, as it was: paths, regions, objects, lines.
    auto made = [&](const json& thing) {
        const std::string statement = thing.value("statement", thing.value("id", ""));
        const std::string node = thing.value("node", "");
        // A locked part stays, even where its maker is redone (a whole template, or one instance).
        if (Locked(node) || Locked(thing.value("id", ""))) return true;
        auto it = statementById_.find(statement);
        const std::string maker = it != statementById_.end() ? it->second->value("node", "") : node;
        return !StatementRedone(statement, maker) && !(Redo(node) && !Locked(node) && it == statementById_.end());
    };
    for (const auto& p : previous.value("paths", json::array())) {
        if (!made(p)) continue;
        PathResult r;
        r.id = p.value("id", "");
        r.node = p.value("node", "");
        r.cls = p.value("class", "street");
        r.terrain = p.value("terrain", "dirt");
        r.width = p.value("width", 1);
        for (const auto& c : p.value("cells", json::array())) {
            const int x = c[0].get<int>(), y = c[1].get<int>();
            if (!In(x, y)) continue;
            r.cells.push_back(Cell(x, y));
        }
        instancesOf_[r.id] = 1;
        paths_.push_back(std::move(r));
    }
    // The roads and the water as they were (the cells, with their ranks).
    for (const auto& c : previous.value("roads", json::array())) {
        const int x = c[0].get<int>(), y = c[1].get<int>();
        if (In(x, y)) road_[Cell(x, y)] = static_cast<uint8_t>(c[2].get<int>());
    }
    for (const auto& p : paths_) {
        if (p.cls == "river") {
            for (int c : p.cells) water_[c] = 1, road_[c] = 0;
        }
    }
    for (const auto& r : previous.value("regions", json::array())) {
        const std::string id = r.value("id", "");
        if (regionById_.contains(id) || !made(r)) continue;
        const json parentId = r.value("parent", json());
        const int parent = parentId.is_string() ? RegionIndex(parentId.get<std::string>()) : -1;
        if (parentId.is_string() && parent < 0) continue; // its container is redone
        const int index = AddRegion(id, r.value("node", ""), r.value("statement", ""), parent, DecodeCells(r.value("cells", json::array()), W),
                                    r.value("module", ""));
        Region& g = regions_[index];
        const std::string facing = r.value("facing", "");
        g.facing = facing.empty() ? 0 : facing[0];
        if (g.facing) {
            // Its street: the road cells just outside its front.
            for (int c : g.cells) {
                const int x = c % W, y = c / W;
                const int fx = g.facing == 'e' ? x + 1 : g.facing == 'w' ? x - 1 : x;
                const int fy = g.facing == 's' ? y + 1 : g.facing == 'n' ? y - 1 : y;
                if (In(fx, fy) && road_[Cell(fx, fy)] && owner_[Cell(fx, fy)] != index) g.street.push_back(Cell(fx, fy));
            }
        }
        if (const json seed = r.value("seed", json()); seed.is_array() && seed.size() == 2) zoneSeeds_[id] = Cell(seed[0].get<int>(), seed[1].get<int>());
        instancesOf_[g.statement]++;
    }
    for (const auto& o : previous.value("objects", json::array())) {
        if (!made(o)) continue;
        Object b;
        b.id = o.value("id", "");
        b.statement = o.value("statement", "");
        b.node = o.value("node", "");
        b.slot = o.value("slot", "");
        b.module = o.value("shows", "");
        b.x = o.value("x", 0), b.y = o.value("y", 0), b.w = o.value("w", 1), b.h = o.value("h", 1);
        b.layer = LayerOf(o.value("layer", "prop"));
        b.primary = o.value("primary", false);
        const std::string facing = o.value("facing", "s");
        b.facing = facing.empty() ? 's' : facing[0];
        AddObject(b);
        instancesOf_[b.statement]++;
    }
    for (const auto& l : previous.value("lines", json::array())) {
        if (!made(l)) continue;
        LineResult r;
        r.id = l.value("id", "");
        r.node = l.value("node", "");
        r.slot = l.value("slot", "");
        r.closed = l.value("closed", true);
        for (const auto& e : l.value("edges", json::array())) r.edges.push_back({e[0].get<int>(), e[1].get<int>(), e[2] == "n" ? 0 : 1});
        for (const auto& e : l.value("gaps", json::array())) r.gaps.push_back({e[0].get<int>(), e[1].get<int>(), e[2] == "n" ? 0 : 1});
        instancesOf_[r.id] = 1;
        lines_.push_back(std::move(r));
    }
}

SolvedLayout Solver::Run() {
    SolvedLayout out;
    if (!ir_.is_object() || ir_.value("schema", "") != "aige.ir/1") {
        out.problems.push_back({{"code", "E-SCHEMA"}, {"message", "not an aige.ir/1 document"}});
        return out;
    }
    seed_ = ir_.value("seed", 7);
    const json bounds = ir_.value("grid", json::object()).value("bounds", json::array({0, 0, 64, 64}));
    W = std::max(1, bounds[2].get<int>());
    H = std::max(1, bounds[3].get<int>());
    owner_.assign(W * H, -1);
    for (auto& layer : occ_) layer.assign(W * H, 0);
    road_.assign(W * H, 0);
    water_.assign(W * H, 0);
    solid_.assign(W * H, 0);
    keepFree_.assign(W * H, 0);
    // Pointers into the document itself (not a copy): they're kept for the whole solve.
    if (ir_.contains("statements") && ir_["statements"].is_array()) {
        for (const auto& s : ir_["statements"]) {
            if (s.is_object()) statements_.push_back(&s);
        }
    }
    // The root regions (no "in"), givens and the container of everything else.
    std::vector<int> roots;
    for (const json* s : statements_) {
        const std::string op = s->value("op", "");
        if (op == "region" && !s->contains("in")) {
            const json at = s->value("at", json::array({0, 0}));
            const IntRange w = RangeOf(s->value("size", json::object()).value("w", json()), W);
            const IntRange h = RangeOf(s->value("size", json::object()).value("h", json()), H);
            std::vector<int> cells;
            for (int y = at[1].get<int>(); y < std::min(H, at[1].get<int>() + h.hi); ++y) {
                for (int x = at[0].get<int>(); x < std::min(W, at[0].get<int>() + w.hi); ++x) cells.push_back(Cell(x, y));
            }
            roots.push_back(AddRegion(s->value("id", ""), s->value("node", ""), s->value("id", ""), -1, cells, s->value("module", "")));
        } else if (op == "relation") {
            relations_.push_back(s);
            const json& members = s->contains("members") ? s->at("members") : json::array();
            if (!members.empty() && members[0].is_string()) relationsOf_[members[0].get<std::string>()].push_back(s);
        }
    }
    for (const json* s : statements_) statementById_[s->value("id", "")] = s;
    if (options_.previous) {
        Restore(*options_.previous);
        RestoreSpaces(*options_.previous);
    }
    for (const json* s : statements_) {
        const std::string op = s->value("op", "");
        if ((op == "region" && !s->contains("in")) || op == "relation" || op == "check" || op == "fill" || op == "objective") continue;
        // A partial solve runs only what it redoes (anchors are placed again anyway: they're givens).
        if (options_.previous && op != "anchor" && !StatementRedone(s->value("id", ""), s->value("node", ""))) continue;
        if (op == "anchor" && s->contains("at") && s->at("at").is_array()) {
            Anchor(*s);
            continue;
        }
        std::string container = ContainerOf(*s);
        if (container.empty() && !roots.empty()) container = regions_[roots.front()].id;
        byContainer_[container].push_back(s);
    }
    // Anchors on the root's sides come first: paths start from them.
    for (int r : roots) {
        auto& list = byContainer_[regions_[r].id];
        for (auto it = list.begin(); it != list.end();) {
            if ((*it)->value("op", "") == "anchor") {
                Anchor(**it);
                it = list.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (int r : roots) SolveRegion(r);
    const json terrain = Terrain();
    Checks();

    // Relation scores, as solved.
    scoreCache_.clear();
    for (const json* rel : relations_) {
        const auto members = rel->value("members", json::array());
        if (members.empty() || !members[0].is_string()) continue;
        const std::string first = members[0].get<std::string>();
        double score = -1;
        if (auto it = objectById_.find(first); it != objectById_.end()) {
            const Object& o = objects_[it->second];
            const int container = owner_[Cell(o.x, o.y)];
            score = container >= 0 ? Score(first, o.x, o.y, o.w, o.h, container, o.facing) / std::max(1e-9, rel->value("weight", 1.0)) : -1;
        } else if (const int r = RegionIndex(first); r >= 0) {
            const Region& g = regions_[r];
            score = g.parent >= 0 ? Score(first, g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0, g.parent, g.facing) / std::max(1e-9, rel->value("weight", 1.0)) : -1;
        }
        relationScores_.push_back({{"id", rel->value("id", "")}, {"type", rel->value("type", "")}, {"score", score}});
    }

    json regions = json::array(), objects = json::array(), paths = json::array(), lines = json::array(), anchors = json::array();
    for (const auto& r : regions_) {
        json one = {{"id", r.id}, {"node", r.node}, {"statement", r.statement}, {"parent", r.parent >= 0 ? json(regions_[r.parent].id) : json()},
                    {"bounds", {r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0}}, {"area", r.cells.size()}, {"cells", EncodeCells(r.cells, W)}};
        if (auto seed = zoneSeeds_.find(r.id); seed != zoneSeeds_.end() && r.statement.ends_with("zones")) one["seed"] = {seed->second % W, seed->second / W};
        if (r.facing) one["facing"] = std::string(1, r.facing);
        if (!r.module.empty()) one["module"] = r.module;
        regions.push_back(one);
    }
    for (const auto& o : objects_) {
        json one = {{"id", o.id}, {"statement", o.statement}, {"node", o.node}, {"slot", o.slot}, {"x", o.x}, {"y", o.y},
                    {"w", o.w}, {"h", o.h}, {"facing", std::string(1, o.facing)}, {"layer", LayerName(o.layer)}};
        if (!o.faced.empty()) one["faced"] = o.faced;
        if (!o.shape.empty()) {
            // Its outline: a fitted one's cells, or its box, with rounded corners.
            std::vector<int> cells = o.cells;
            if (cells.empty()) {
                for (int y = o.y; y < o.y + o.h; ++y) {
                    for (int x = o.x; x < o.x + o.w; ++x) cells.push_back(Cell(x, y));
                }
            }
            const std::vector<double> outline = SmoothOutline(cells, W, std::clamp(std::min(o.w, o.h) * 0.3, 0.6, 2.5));
            one["shape"] = o.shape;
            if (!outline.empty()) one["outline"] = outline;
        }
        if (o.primary) one["primary"] = true;
        if (!o.module.empty()) one["shows"] = o.module;
        objects.push_back(one);
    }
    for (const auto& p : paths_) {
        json cells = json::array();
        for (int c : p.cells) cells.push_back({c % W, c / W});
        paths.push_back({{"id", p.id}, {"node", p.node}, {"class", p.cls}, {"width", p.width}, {"terrain", p.terrain}, {"cells", cells}});
    }
    for (const auto& l : lines_) {
        json edges = json::array(), gaps = json::array();
        for (const auto& e : l.edges) edges.push_back({e[0], e[1], e[2] == 0 ? "n" : "w"});
        for (const auto& e : l.gaps) gaps.push_back({e[0], e[1], e[2] == 0 ? "n" : "w"});
        lines.push_back({{"id", l.id}, {"node", l.node}, {"slot", l.slot}, {"edges", edges}, {"gaps", gaps}, {"closed", l.closed}});
    }
    for (const auto& [id, a] : anchors_) {
        anchors.push_back({{"id", id}, {"node", a.node}, {"kind", a.kind}, {"cell", a.cell >= 0 ? json::array({a.cell % W, a.cell / W}) : json()}});
    }
    json roadCells = json::array();
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (road_[Cell(x, y)]) roadCells.push_back({x, y, road_[Cell(x, y)]});
        }
    }
    out.document = {{"schema", "aige.solved/1"},
                    {"place", ir_.value("place", "")},
                    {"seed", seed_},
                    {"grid", ir_.value("grid", json::object())},
                    {"regions", regions},
                    {"objects", objects},
                    {"paths", paths},
                    {"roads", roadCells},
                    {"lines", lines},
                    {"anchors", anchors},
                    {"terrain", terrain},
                    {"relations", relationScores_},
                    {"checks", checks_},
                    {"connectors", connectors_},
                    {"spaces", SpacesJson()},
                    {"problems", problems_},
                    {"stats", {{"regions", regions_.size()}, {"objects", objects_.size()}, {"paths", paths_.size()}, {"lines", lines_.size()}}}};
    // The time per op: beside the document, which stays the same for the same seed.
    out.timing = json::object();
    for (const auto& [op, t] : msByOp_) out.timing[op] = std::lround(t);
    out.problems = problems_;
    return out;
}

} // namespace

bool SolvedLayout::ok() const {
    return std::none_of(problems.begin(), problems.end(), [](const json& p) {
        const std::string code = p.value("code", "");
        return code.rfind("S-", 0) == 0 || code.rfind("E-", 0) == 0;
    });
}

json EncodeCells(const std::vector<int>& cells, int width) {
    json runs = json::array();
    std::vector<int> sorted = cells;
    std::sort(sorted.begin(), sorted.end());
    for (size_t i = 0; i < sorted.size();) {
        const int y = sorted[i] / width, x0 = sorted[i] % width;
        size_t j = i + 1;
        while (j < sorted.size() && sorted[j] == sorted[j - 1] + 1 && sorted[j] / width == y) ++j;
        runs.push_back({y, x0, x0 + static_cast<int>(j - i)});
        i = j;
    }
    return runs;
}

std::vector<int> DecodeCells(const json& runs, int width) {
    std::vector<int> cells;
    for (const auto& r : runs) {
        if (!r.is_array() || r.size() != 3) continue;
        for (int x = r[1].get<int>(); x < r[2].get<int>(); ++x) cells.push_back(r[0].get<int>() * width + x);
    }
    return cells;
}

SolvedLayout SolveLayout(const Ontology& ontology, const json& ir, const SolveOptions& options) {
    Solver solver(ontology, ir, options);
    return solver.Run();
}

} // namespace aige
