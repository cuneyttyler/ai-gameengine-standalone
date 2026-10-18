#include "places/Solver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
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
    int layer = kProp;
    bool primary = false;
};

struct PathResult {
    std::string id, node, cls, terrain;
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
    int Cell(int x, int y) const { return y * W + x; }
    bool In(int x, int y) const { return x >= 0 && y >= 0 && x < W && y < H; }

    // Results.
    std::vector<Region> regions_;
    std::map<std::string, int> regionById_;
    std::vector<Object> objects_;
    std::map<std::string, std::vector<int>> objectsByStatement_;
    std::map<std::string, int> objectById_;
    std::vector<PathResult> paths_;
    std::vector<LineResult> lines_;
    std::map<std::string, AnchorResult> anchors_;
    std::map<std::string, int> instancesOf_; // statement id -> how many it produced
    std::vector<json> problems_;
    std::vector<json> relationScores_;
    std::vector<json> checks_;

    // The document.
    std::vector<const json*> statements_;
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
    json Terrain();
    void Checks();

    // Helpers.
    int AddRegion(const std::string& id, const std::string& node, const std::string& statement, int parent, std::vector<int> cells,
                  const std::string& module = {});
    int AddObject(Object o);
    bool Free(int cell, int container, bool solid) const;
    std::vector<int> AStar(int from, int to, const std::function<double(int)>& cost);
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
            // The region a reference names: the longest known prefix.
            while (!ref.empty()) {
                if (std::any_of(statements_.begin(), statements_.end(), [&](const json* t) {
                        return t->value("id", "") == ref;
                    }) ||
                    regionById_.contains(ref)) {
                    return ref;
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
    for (int y = o.y; y < o.y + o.h; ++y) {
        for (int x = o.x; x < o.x + o.w; ++x) {
            if (In(x, y)) occ_[o.layer][Cell(x, y)] = index + 1;
        }
    }
    objectsByStatement_[o.statement].push_back(index);
    objectById_[o.id] = index;
    objects_.push_back(std::move(o));
    return index;
}

bool Solver::Free(int cell, int container, bool solid) const {
    if (container >= 0 && owner_[cell] != container) return false;
    if (water_[cell]) return false;
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
    if (style == "grid") {
        const int ox = r.x0 + rng.Range(0, spacing / 2), oy = r.y0 + rng.Range(0, spacing / 2);
        for (int c : r.cells) {
            const int x = c % W, y = c / W;
            if ((x - ox) % spacing == 0 || (y - oy) % spacing == 0) streets.push_back(c);
        }
        MarkRoad(streets, 1, rank, region);
        if (!streets.empty()) {
            auto link = connectToRoads(streets[streets.size() / 2]);
            MarkRoad(link, 1, rank, -1);
            streets.insert(streets.end(), link.begin(), link.end());
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
        MarkRoad(streets, 1, rank, region);
        if (!streets.empty()) {
            auto link = connectToRoads(streets.front());
            MarkRoad(link, 1, rank, -1);
        }
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
        MarkRoad(streets, 1, rank, region);
        if (mains.empty() && !r.cells.empty()) MarkRoad(connectToRoads(r.cells[r.cells.size() / 2]), 1, rank, -1);
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
            MarkRoad(spine, 1, rank, region);
            streets.insert(streets.end(), spine.begin(), spine.end());
            if (!spine.empty()) {
                auto link = connectToRoads(spine[spine.size() / 2], true);
                MarkRoad(link, 1, rank, -1);
            }
        }
        // Lanes start where the zone is furthest from any street, one at a time,
        // until every cell is within half a block of one. Each heads for the
        // nearest street, but running alongside one costs: lanes meet streets,
        // they don't shadow them (the land between is for plots).
        const int half = std::max(2, spacing / 2);
        const int maxLanes = std::max(1, static_cast<int>(4 * r.cells.size() / (spacing * spacing)));
        std::vector<float> tie(W * H);
        for (auto& t : tie) t = static_cast<float>(rng.Unit());
        for (int laneCount = 0; laneCount < maxLanes; ++laneCount) {
            std::vector<int> near(W * H, 1 << 20);
            std::deque<int> todo;
            for (int c = 0; c < W * H; ++c) {
                if (road_[c]) {
                    near[c] = 0;
                    todo.push_back(c);
                }
            }
            int target = -1;
            while (!todo.empty()) {
                const int c = todo.front();
                todo.pop_front();
                for (const auto& [ddx, ddy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    const int nx = c % W + ddx, ny = c / W + ddy;
                    if (!In(nx, ny) || near[Cell(nx, ny)] <= near[c] + 1) continue;
                    near[Cell(nx, ny)] = near[c] + 1;
                    todo.push_back(Cell(nx, ny));
                }
            }
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
            auto lane = AStar(p, target, [&](int c) -> double {
                if (water_[c] || occ_[kBuilding][c]) return std::numeric_limits<double>::infinity();
                if (road_[c]) return 0.1;
                const double alongside = near[c] < half ? 3.0 * (half - near[c]) / half : 0.0;
                return (inZone(c) ? 1.0 : 4.0) + 0.6 * rng.Unit() + alongside;
            });
            MarkRoad(lane, 1, rank, -1);
            streets.insert(streets.end(), lane.begin(), lane.end());
        }
    }
    PathResult result;
    result.id = s.value("id", "");
    result.node = s.value("node", "");
    result.cls = s.value("class", "street");
    result.terrain = s.value("terrain", "dirt");
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
        json prefers;
        int seed = -1;
        std::vector<int> cells;
    };
    std::vector<Zone> zones;
    double wanted = 0;
    for (const auto& z : s.value("into", json::array())) {
        Zone zone;
        zone.id = z.value("id", "");
        zone.node = z.value("node", zone.id);
        zone.module = z.value("module", "");
        zone.quota = RangeOf(z.value("area", json()), 400).lo > 0 ? (RangeOf(z.value("area", json()), 400).lo + RangeOf(z.value("area", json()), 400).hi) / 2.0 : 400;
        zone.prefers = z.value("prefers", json::object());
        wanted += zone.quota;
        zones.push_back(zone);
    }
    std::vector<int> available;
    for (int c : parent.cells) {
        if (owner_[c] == region && !road_[c] && !water_[c] && !occ_[kBuilding][c]) available.push_back(c);
    }
    if (zones.empty() || available.empty()) return;
    // Quotas scaled to what's there (open land is left between zones).
    const double room = 0.92 * available.size();
    if (wanted > room) {
        for (auto& z : zones) z.quota *= room / wanted;
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
    auto interest = [&](const Zone& z, int c) {
        const double x = c % W + 0.5, y = c / W + 0.5;
        double score = 0;
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
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return strength(zones[a]) > strength(zones[b]); });
    Rng rng = StreamFor(s);
    const double minApart = std::sqrt(room / zones.size()) * 0.8;
    for (size_t k : order) {
        double best = -1e18;
        for (size_t i = 0; i < available.size(); i += std::max<size_t>(1, available.size() / 3000)) {
            const int c = available[i];
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
    for (size_t k = 0; k < zones.size(); ++k) {
        if (zones[k].seed >= 0) frontier[k].push({0, zones[k].seed});
    }
    std::vector<uint8_t> usable(W * H, 0);
    for (int c : available) usable[c] = 1;
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
            const double distance = std::hypot(nx - z.seed % W, ny - z.seed / W);
            queue.push({-distance + 0.5 * interest(z, n), n});
        }
    }
    for (const auto& z : zones) {
        if (z.cells.empty()) {
            Problem("S-UNSAT", s, "no room for zone " + z.id);
            continue;
        }
        AddRegion(z.id, z.node, s.value("id", ""), region, z.cells, z.module);
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
    for (const auto& item : s.value("into", json::array())) {
        const IntRange fw = RangeOf(item.value("size", json::object()).value("w", json()), 5);
        const IntRange fh = RangeOf(item.value("size", json::object()).value("h", json()), 6);
        const int w = rng.Range(fw.lo, fw.hi), h = rng.Range(fh.lo, fh.hi); // frontage, depth
        struct Best {
            double score = -1e18;
            int x = 0, y = 0, rw = 0, rh = 0;
            char facing = 's';
        } best;
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
                if (frontRoad * 10 < frontage * 6) continue;
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
        if (best.rw == 0) {
            continue; // no room left: reported by the count check
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
    // The root region: placed where it says.
    const IntRange fw = RangeOf(s.value("size", json::object()).value("w", json()), 4);
    const IntRange fh = RangeOf(s.value("size", json::object()).value("h", json()), 4);
    int w = rng.Range(fw.lo, fw.hi), h = rng.Range(fh.lo, fh.hi);
    const std::string id = s.value("id", "");
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
                const double score = Score(id, x, y, w, h, container, 's') + 0.001 * rng.Unit();
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
        // The footprint's width runs along its front.
        if (facing == 'e' || facing == 'w') std::swap(w, h);
        const std::string objectId = n == 1 ? id : id + "[" + std::to_string(k) + "]";
        std::optional<std::pair<int, int>> at;
        // A primary object in a parcel stands at the front, centred, at its setback.
        if (s.value("primary", false) && parent.facing) {
            const int setback = s.value("setback", 1);
            for (int back = std::min(setback, std::max(0, (parent.facing == 'n' || parent.facing == 's' ? parent.y1 - parent.y0 : parent.x1 - parent.x0) - (facing == 'n' || facing == 's' ? h : w)));
                 back >= 0 && !at; --back) {
                int x = (parent.x0 + parent.x1 - w) / 2, y = (parent.y0 + parent.y1 - h) / 2;
                if (parent.facing == 'n') y = parent.y0 + back;
                if (parent.facing == 's') y = parent.y1 - h - back;
                if (parent.facing == 'w') x = parent.x0 + back;
                if (parent.facing == 'e') x = parent.x1 - w - back;
                bool fits = x >= parent.x0 && y >= parent.y0 && x + w <= parent.x1 && y + h <= parent.y1;
                for (int yy = y; fits && yy < y + h; ++yy) {
                    for (int xx = x; fits && xx < x + w; ++xx) fits = Free(Cell(xx, yy), container, solid);
                }
                if (fits) at = std::pair{x, y};
            }
        }
        if (!at) {
            // Anywhere it fits, by the relations' scores (or the centre, if asked).
            double best = -1e18;
            int tried = 0;
            const bool centre = s.value("position", "") == "center";
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
                    score += 0.001 * rng.Unit();
                    if (score > best) {
                        best = score;
                        at = std::pair{x, y};
                    }
                }
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
    const int margin = s.value("margin", grid ? 0 : 1);
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
    std::vector<int> free;
    for (int c : area) {
        if (Free(c, -1, true) && !occ_[layer][c] && (!solid || (!occ_[kProp][c] && !occ_[kBuilding][c]))) free.push_back(c);
    }
    const double density = std::clamp(s.value("density", 0.1), 0.0, 1.0);
    const int target = static_cast<int>(std::lround(density * free.size()));
    const int spacing = std::max(0, RangeOf(s.value("spacing", json::object()).value("min", json()), solid ? 1 : 0).lo);
    for (size_t i = free.size(); i > 1; --i) std::swap(free[i - 1], free[rng.Next() % i]);
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

// --- The region tree ----------------------------------------------------------------------------

int StepOf(const json& s) {
    const std::string op = s.value("op", "");
    if (op == "path" || op == "network") return 1;
    if (op == "partition" || op == "subdivide") return 2;
    if (op == "place" && s.value("primary", false)) return 3;
    if (op == "region" || op == "anchor") return 4;
    if (op == "place") return 5;
    if (op == "line") return 6;
    if (op == "rows" || op == "grid") return 7;
    if (op == "scatter") return 8;
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
    for (const json* s : list) {
        const std::string op = s->value("op", "");
        if (op == "path") Path(*s);
        else if (op == "network") Network(*s, index);
        else if (op == "partition") Partition(*s, index);
        else if (op == "subdivide") Subdivide(*s, index);
        else if (op == "place") Place(*s, index);
        else if (op == "region") NestedRegion(*s, index);
        else if (op == "anchor") Anchor(*s);
        else if (op == "line") Line(*s);
        else if (op == "rows") Rows(*s, index, false);
        else if (op == "grid") Rows(*s, index, true);
        else if (op == "scatter") Scatter(*s, index);
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
    std::stable_sort(fills.begin(), fills.end(), [&](const json* a, const json* b) { return depthOf(a) < depthOf(b); });
    for (const json* s : fills) {
        const int t = indexOf(s->value("terrain", "grass"));
        for (int c : Cells(s->value("in", ""))) grid[c] = t;
    }
    // Road cells take the surface of their class (the first path of that class says it).
    std::array<int, 5> surface{};
    for (const auto& p : paths_) {
        if (p.cls != "river" && surface[RankOf(p.cls)] == 0) surface[RankOf(p.cls)] = indexOf(p.terrain);
    }
    const int dirt = indexOf("dirt");
    for (int c = 0; c < W * H; ++c) {
        if (road_[c]) grid[c] = surface[road_[c]] ? surface[road_[c]] : dirt;
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
    for (int c = 0; c < W * H; ++c) walk[c] = (!water_[c] || road_[c]) && !occ_[kBuilding][c] && !occ_[kProp][c];
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
        }
        if (!result["ok"].get<bool>()) {
            problems_.push_back({{"code", "C-FAIL"}, {"statement", result["id"]}, {"node", result["node"]},
                                 {"message", predicate + " failed" + (result.contains("detail") ? ": " + (result["detail"].is_string() ? result["detail"].get<std::string>() : result["detail"].dump()) : "")}});
        }
        checks_.push_back(result);
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
    for (const json* s : statements_) {
        const std::string op = s->value("op", "");
        if ((op == "region" && !s->contains("in")) || op == "relation" || op == "check" || op == "fill" || op == "objective") continue;
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
                    {"bounds", {r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0}}, {"cells", EncodeCells(r.cells, W)}};
        if (r.facing) one["facing"] = std::string(1, r.facing);
        if (!r.module.empty()) one["module"] = r.module;
        regions.push_back(one);
    }
    for (const auto& o : objects_) {
        json one = {{"id", o.id}, {"statement", o.statement}, {"node", o.node}, {"slot", o.slot}, {"x", o.x}, {"y", o.y},
                    {"w", o.w}, {"h", o.h}, {"facing", std::string(1, o.facing)}, {"layer", LayerName(o.layer)}};
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
                    {"problems", problems_},
                    {"stats", {{"regions", regions_.size()}, {"objects", objects_.size()}, {"paths", paths_.size()}, {"lines", lines_.size()}}}};
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
