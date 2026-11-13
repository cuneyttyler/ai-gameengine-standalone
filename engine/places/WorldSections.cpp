#include "places/WorldSections.h"

#include "places/Solver.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <functional>
#include <map>

namespace aige {
using json = nlohmann::json;

std::vector<std::string> TerrainCells(const json& solved, int W, int H) {
    std::vector<std::string> cells(static_cast<size_t>(W) * H, "");
    if (!solved.contains("terrain")) return cells;
    const json palette = solved["terrain"].value("palette", json::array());
    const json rows = solved["terrain"].value("rows", json::array());
    for (int y = 0; y < static_cast<int>(rows.size()) && y < H; ++y) {
        int x = 0;
        for (const auto& run : rows[y]) {
            const int n = run[0].get<int>(), i = run[1].get<int>();
            for (int k = 0; k < n && x < W; ++k, ++x) cells[static_cast<size_t>(y) * W + x] = i >= 0 && i < static_cast<int>(palette.size()) ? palette[i].get<std::string>() : "";
        }
    }
    return cells;
}

void SetTerrain(json& solved, const std::vector<std::string>& cells, int W, int H) {
    json palette = json::array();
    std::map<std::string, int> index;
    json rows = json::array();
    for (int y = 0; y < H; ++y) {
        json row = json::array();
        for (int x = 0; x < W;) {
            const std::string& t = cells[static_cast<size_t>(y) * W + x];
            int n = 1;
            while (x + n < W && cells[static_cast<size_t>(y) * W + x + n] == t) ++n;
            if (!index.contains(t)) index[t] = static_cast<int>(palette.size()), palette.push_back(t);
            row.push_back({n, index[t]});
            x += n;
        }
        rows.push_back(row);
    }
    solved["terrain"]["palette"] = palette;
    solved["terrain"]["rows"] = rows;
}

namespace {

// Smooth noise in [-1, 1] (value noise, two octaves): a section's ragged outline.
double Noise(double x, double y, uint32_t seed) {
    auto lattice = [&](int ix, int iy) {
        uint32_t h = static_cast<uint32_t>(ix) * 374761393u + static_cast<uint32_t>(iy) * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return (h & 0xffffff) / static_cast<double>(0xffffff) * 2.0 - 1.0;
    };
    auto value = [&](double vx, double vy) {
        const int x0 = static_cast<int>(std::floor(vx)), y0 = static_cast<int>(std::floor(vy));
        auto s = [](double v) { return v * v * (3 - 2 * v); };
        const double fx = s(vx - x0), fy = s(vy - y0);
        const double p = lattice(x0, y0), q = lattice(x0 + 1, y0), r = lattice(x0, y0 + 1), u = lattice(x0 + 1, y0 + 1);
        return (p + (q - p) * fx) * (1 - fy) + (r + (u - r) * fx) * fy;
    };
    return 0.65 * value(x / 14.0, y / 14.0) + 0.35 * value(x / 5.0, y / 5.0);
}

// The section's plan into the world's where the world placed it (its box at ox, oy): what's built and the land it keeps
// round it, inside its land's ragged edge; beyond, the world's own land.
void PutInto(json& world, const json& section, int ox, int oy, const std::string& prefix, const std::vector<uint8_t>* landMask) {
    const json wb = world["grid"]["bounds"], sb = section["grid"]["bounds"];
    const int W = wb[2], H = wb[3], SW = sb[2], SH = sb[3];
    std::vector<int> dist(static_cast<size_t>(SW) * SH, 1 << 20);
    std::deque<int> queue;
    for (const auto& o : section.value("objects", json::array())) {
        if (o.value("dressing", "") == "landscape" || o.value("id", "").find("landscape-wild") != std::string::npos) continue;
        if (o.value("layer", "") != "building" && o.value("layer", "") != "prop") continue;
        const int x0 = o.value("x", 0), y0 = o.value("y", 0), w = std::max(1, o.value("w", 1)), h = std::max(1, o.value("h", 1));
        for (int y = y0; y < y0 + h; ++y) {
            for (int x = x0; x < x0 + w; ++x) {
                if (x < 0 || y < 0 || x >= SW || y >= SH || dist[static_cast<size_t>(y) * SW + x] == 0) continue;
                dist[static_cast<size_t>(y) * SW + x] = 0;
                queue.push_back(y * SW + x);
            }
        }
    }
    while (!queue.empty()) {
        const int c = queue.front();
        queue.pop_front();
        const int x = c % SW, y = c / SW;
        for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= SW || ny >= SH) continue;
            int& d = dist[static_cast<size_t>(ny) * SW + nx];
            if (d > dist[c] + 1) d = dist[c] + 1, queue.push_back(ny * SW + nx);
        }
    }
    const uint32_t seed = static_cast<uint32_t>(std::hash<std::string>{}(prefix));
    std::vector<int> inside(static_cast<size_t>(SW) * SH, 1 << 20);
    {
        std::deque<int> q;
        for (int y = 0; y < SH; ++y) {
            for (int x = 0; x < SW; ++x) {
                const size_t c = static_cast<size_t>(y) * SW + x;
                const bool out = landMask ? !(*landMask)[c] : false;
                const bool rim = x == 0 || y == 0 || x == SW - 1 || y == SH - 1;
                if (out) inside[c] = 0, q.push_back(static_cast<int>(c));
                else if (rim) inside[c] = 1, q.push_back(static_cast<int>(c));
            }
        }
        while (!q.empty()) {
            const int c = q.front();
            q.pop_front();
            const int x = c % SW, y = c / SW;
            for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= SW || ny >= SH) continue;
                int& d = inside[static_cast<size_t>(ny) * SW + nx];
                if (d > inside[c] + 1) d = inside[c] + 1, q.push_back(ny * SW + nx);
            }
        }
    }
    std::vector<uint8_t> mask(static_cast<size_t>(SW) * SH, 0);
    for (int y = 0; y < SH; ++y) {
        for (int x = 0; x < SW; ++x) {
            const double edge = inside[static_cast<size_t>(y) * SW + x];
            const double band = 6.0 * (1.0 + 0.57 * Noise(x, y, seed));
            const double reach = 3.0 * (1.0 + 0.5 * Noise(x + 500, y, seed));
            const bool its = !landMask || (*landMask)[static_cast<size_t>(y) * SW + x];
            mask[static_cast<size_t>(y) * SW + x] = its && (edge > band || dist[static_cast<size_t>(y) * SW + x] <= reach);
        }
    }
    auto in = [&](int sx, int sy) { return sx >= 0 && sy >= 0 && sx < SW && sy < SH && mask[static_cast<size_t>(sy) * SW + sx]; };
    std::vector<std::string> cells = TerrainCells(world, W, H);
    const std::vector<std::string> own = TerrainCells(section, SW, SH);
    for (int y = 0; y < SH; ++y) {
        for (int x = 0; x < SW; ++x) {
            if (ox + x >= W || oy + y >= H) continue;
            if (in(x, y) && !own[static_cast<size_t>(y) * SW + x].empty()) cells[static_cast<size_t>(oy + y) * W + ox + x] = own[static_cast<size_t>(y) * SW + x];
        }
    }
    SetTerrain(world, cells, W, H);
    json objects = json::array();
    for (const auto& o : world.value("objects", json::array())) {
        const int lx = o.value("x", 0) - ox, ly = o.value("y", 0) - oy;
        const bool marker = o.value("layer", "") == "building" && lx >= -2 && ly >= -2 && lx < SW + 2 && ly < SH + 2;
        const bool gateway = o.value("slot", "").rfind("gateway", 0) == 0; // a way into another place: the world's, kept
        if ((!in(lx, ly) && !marker) || gateway) objects.push_back(o);
    }
    for (json o : section.value("objects", json::array())) {
        if (!in(o.value("x", 0), o.value("y", 0))) continue;
        o["x"] = o.value("x", 0) + ox, o["y"] = o.value("y", 0) + oy;
        o["id"] = prefix + "/" + o.value("id", "");
        if (const json outline = o.value("outline", json()); outline.is_array()) {
            json moved = json::array();
            for (size_t i = 0; i < outline.size(); ++i) moved.push_back(outline[i].get<double>() + (i % 2 == 0 ? ox : oy));
            o["outline"] = moved;
        }
        objects.push_back(o);
    }
    world["objects"] = objects;
    if (!world.contains("lines")) world["lines"] = json::array();
    for (json l : section.value("lines", json::array())) {
        json edges = json::array();
        for (const auto& e : l["edges"]) {
            if (in(e[0].get<int>(), e[1].get<int>())) edges.push_back({e[0].get<int>() + ox, e[1].get<int>() + oy, e[2]});
        }
        if (edges.empty()) continue;
        l["edges"] = edges;
        l["id"] = prefix + "/" + l.value("id", "");
        world["lines"].push_back(l);
    }
    if (section.contains("terrain")) {
        for (const auto& b : section["terrain"].value("bridges", json::array())) {
            if (in(b[0].get<int>(), b[1].get<int>())) world["terrain"]["bridges"].push_back({b[0].get<int>() + ox, b[1].get<int>() + oy});
        }
    }
    if (!world.contains("paths")) world["paths"] = json::array();
    for (json p : section.value("paths", json::array())) {
        json cells = json::array();
        for (const auto& c : p.value("cells", json::array())) {
            if (in(c[0].get<int>(), c[1].get<int>())) cells.push_back({c[0].get<int>() + ox, c[1].get<int>() + oy});
        }
        if (cells.empty()) continue;
        p["cells"] = cells;
        p["id"] = prefix + "/" + p.value("id", "");
        world["paths"].push_back(p);
    }
    for (json r : section.value("regions", json::array())) {
        std::vector<int> moved;
        for (int c : DecodeCells(r.value("cells", json::array()), SW)) {
            if (in(c % SW, c / SW)) moved.push_back((c / SW + oy) * W + c % SW + ox);
        }
        if (moved.empty()) continue;
        r["cells"] = EncodeCells(moved, W);
        if (const json bounds = r.value("bounds", json()); bounds.is_array() && bounds.size() == 4) {
            r["bounds"] = {bounds[0].get<int>() + ox, bounds[1].get<int>() + oy, bounds[2], bounds[3]};
        }
        r["id"] = prefix + "/" + r.value("id", "");
        world["regions"].push_back(r);
    }
}

} // namespace

std::vector<SectionJob> WorldSectionJobs(const Ontology& ontology, const json& worldBrief, const json& worldSpec, const json& worldSolved) {
    std::vector<SectionJob> jobs;
    if (!worldSolved.contains("grid")) return jobs;
    const json b = worldSolved["grid"]["bounds"];
    const int WW = b[2], WH = b[3];
    // The world's roads (its paths but rivers, and its road cells): where they cross a section's land's edge are its ways in.
    std::vector<uint8_t> worldRoad(static_cast<size_t>(WW) * WH, 0);
    for (const auto& p : worldSolved.value("paths", json::array())) {
        if (p.value("class", "") == "river" || p.value("terrain", "") == "water") continue;
        for (const auto& c : p.value("cells", json::array())) {
            const int cx = c[0], cy = c[1];
            if (cx >= 0 && cy >= 0 && cx < WW && cy < WH) worldRoad[static_cast<size_t>(cy) * WW + cx] = 1;
        }
    }
    for (const auto& c : worldSolved.value("roads", json::array())) {
        const int cx = c[0], cy = c[1];
        if (cx >= 0 && cy >= 0 && cx < WW && cy < WH) worldRoad[static_cast<size_t>(cy) * WW + cx] = 1;
    }
    const BuildType* worldType = ontology.Build(worldBrief.value("buildType", ""));
    const json sectionsDef = worldType && worldType->sections.is_object() ? worldType->sections : json::object();
    const json kinds = sectionsDef.value("kinds", json::object());
    const json withoutParts = sectionsDef.value("withoutParts", json::array());
    const json places = worldBrief.value("places", json::object());
    int forestCount = 0;
    static const char* kWoodNames[] = {"Mirkwold", "the Greenhollow", "Ashenleigh Wood", "the Thornwood", "Elderglade", "Fernmoor Wood",
                                       "the Old Weald", "Brackenridge", "Mossdeep", "the Silverbirches"};
    // Each site's class and the place it is, from the world's Spec.
    std::map<std::string, json> siteQuality;
    for (const auto& c : worldSpec.value("root", json::object()).value("children", json::array())) {
        for (const auto& s : c.value("children", json::array())) siteQuality[c.value("id", "")] = s.value("quality", json::object());
    }
    for (const auto& r : worldSolved.value("regions", json::array())) {
        const std::string landId = r.value("id", "");
        if (landId.find('/') != std::string::npos) continue;
        SectionJob job;
        job.land = landId;
        json components = json::object();
        if (landId.rfind("heartland-", 0) == 0) {
            const json q = siteQuality.contains(landId) ? siteQuality[landId] : json::object();
            job.place = landId.substr(10);
            const json p = places.is_object() ? places.value(job.place, json::object()) : json::object();
            job.kind = q.value("build-type", p.value("type", "settlement"));
            job.cls = q.value("scale-class", "");
            job.name = q.value("name", p.value("name", ""));
            // Its own components (the Confirm page's, for this place), and none of what no section has (forests).
            components = p.value("components", json::object());
            for (const auto& part : withoutParts) components[part.get<std::string>()] = {{"enabled", false}};
        } else if (const std::string module = r.value("module", ""); kinds.contains(module)) {
            job.kind = kinds[module];
            job.name = kWoodNames[forestCount++ % 10];
            job.forest = true;
        } else {
            continue;
        }
        std::vector<int> landCells = DecodeCells(r.value("cells", json::array()), WW);
        if (landCells.empty()) continue;
        int lx0 = WW, ly0 = WH, lx1 = 0, ly1 = 0;
        for (int c : landCells) lx0 = std::min(lx0, c % WW), ly0 = std::min(ly0, c / WW), lx1 = std::max(lx1, c % WW + 1), ly1 = std::max(ly1, c / WW + 1);
        const int side = std::max(lx1 - lx0, ly1 - ly0);
        job.x = lx0, job.y = ly0, job.side = side;
        job.id = landId + "/site";
        if (job.cls.empty()) {
            if (const BuildType* bt = ontology.Build(job.kind)) {
                for (const auto& s : bt->scale) {
                    if (side >= s.side.first) job.cls = s.id; // the biggest its land reaches
                }
            }
        }
        job.landMask.assign(static_cast<size_t>(side) * side, 0);
        for (int c : landCells) job.landMask[static_cast<size_t>(c / WW - ly0) * side + c % WW - lx0] = 1;
        const auto& land = job.landMask;
        std::vector<int> outsideCells;
        for (int c = 0; c < side * side; ++c) {
            if (!land[c]) outsideCells.push_back(c);
        }
        job.brief = {{"schema", "aige.brief/1"}, {"buildType", job.kind}, {"name", job.name}};
        if (!components.empty()) job.brief["components"] = components;
        if (!job.cls.empty() && ontology.Build(job.kind) && ontology.Build(job.kind)->Scale(job.cls)) job.brief["scale"] = {{"class", job.cls}};
        CompileOptions& options = job.options;
        options.side = side;
        options.fixedSide = true;
        options.seed = std::hash<std::string>{}(landId) % 1000003; // each section its own layout
        options.outside = EncodeCells(outsideCells, side);
        auto edgeOn = [&](int row, bool west) {
            for (int dy = 0; dy < side; ++dy) {
                for (const int yy : {row + dy, row - dy}) {
                    if (yy < 0 || yy >= side) continue;
                    for (int k = 0; k < side; ++k) {
                        const int xx = west ? k : side - 1 - k;
                        if (land[static_cast<size_t>(yy) * side + xx]) return json{xx, yy};
                    }
                }
            }
            return json{west ? 0 : side - 1, row};
        };
        // Its ways in: where the world's roads cross its land's edge, one per crossing; the farthest two its road's ends,
        // the rest to its middle. None: its land's westmost and eastmost.
        std::vector<std::pair<int, int>> crossings;
        for (int yy = 0; yy < side; ++yy) {
            for (int xx = 0; xx < side; ++xx) {
                if (!land[static_cast<size_t>(yy) * side + xx]) continue;
                const int wx = lx0 + xx, wy = ly0 + yy;
                if (wx >= WW || wy >= WH || !worldRoad[static_cast<size_t>(wy) * WW + wx]) continue;
                bool edge = false;
                for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    const int nx = xx + dx, ny = yy + dy, nwx = wx + dx, nwy = wy + dy;
                    const bool outsideLand = nx < 0 || ny < 0 || nx >= side || ny >= side || !land[static_cast<size_t>(ny) * side + nx];
                    const bool road = nwx >= 0 && nwy >= 0 && nwx < WW && nwy < WH && worldRoad[static_cast<size_t>(nwy) * WW + nwx];
                    edge = edge || (outsideLand && road);
                }
                if (!edge) continue;
                bool close = false;
                for (const auto& [cx, cy] : crossings) close = close || std::abs(cx - xx) + std::abs(cy - yy) < 8;
                if (!close) crossings.push_back({xx, yy});
            }
        }
        json ways = {{"w", edgeOn(side / 2, true)}, {"e", edgeOn(side / 2 + side / 10, false)}};
        if (!crossings.empty()) {
            size_t a = 0, bIndex = crossings.size() > 1 ? 1 : 0;
            double farthest = -1;
            for (size_t i = 0; i < crossings.size(); ++i) {
                for (size_t j = i + 1; j < crossings.size(); ++j) {
                    const double d = std::hypot(crossings[i].first - crossings[j].first, crossings[i].second - crossings[j].second);
                    if (d > farthest) farthest = d, a = i, bIndex = j;
                }
            }
            if (crossings[a].first > crossings[bIndex].first) std::swap(a, bIndex);
            ways["w"] = {crossings[a].first, crossings[a].second};
            if (crossings.size() > 1) ways["e"] = {crossings[bIndex].first, crossings[bIndex].second};
            json more = json::array();
            for (size_t i = 0; i < crossings.size(); ++i) {
                if (i != a && i != bIndex) more.push_back({crossings[i].first, crossings[i].second});
            }
            if (!more.empty()) ways["more"] = more;
        }
        options.entrances = ways;
        // The world's roads through its land, kept as they are (each run inside the land, the longest first: its main road).
        json carried = json::array();
        for (const auto& p : worldSolved.value("paths", json::array())) {
            if (p.value("class", "") == "river" || p.value("terrain", "") == "water") continue;
            json run = json::array();
            auto inner = [&](const json& c) {
                const int lx = c[0], ly = c[1];
                for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    const int nx = lx + dx, ny = ly + dy;
                    if (nx < 0 || ny < 0 || nx >= side || ny >= side || !land[static_cast<size_t>(ny) * side + nx]) return false;
                }
                return true;
            };
            auto flush = [&] {
                if (run.size() >= 3) {
                    json open = json::array();
                    if (inner(run.front())) open.push_back("start");
                    if (inner(run.back())) open.push_back("end");
                    carried.push_back({{"cells", run}, {"width", p.value("width", 2)}, {"terrain", "dirt"}, {"open", open}});
                }
                run = json::array();
            };
            for (const auto& c : p.value("cells", json::array())) {
                const int lx = c[0].get<int>() - lx0, ly = c[1].get<int>() - ly0;
                if (lx >= 0 && ly >= 0 && lx < side && ly < side && land[static_cast<size_t>(ly) * side + lx]) run.push_back({lx, ly});
                else flush();
            }
            flush();
        }
        std::sort(carried.begin(), carried.end(), [](const json& a, const json& b2) { return a["cells"].size() > b2["cells"].size(); });
        options.roads = carried;
        job.rootFields = sectionsDef.value("fields", json::object());
        job.withoutParts = withoutParts;
        jobs.push_back(std::move(job));
    }
    return jobs;
}

json SectionJobJson(const SectionJob& job) {
    return {{"id", job.id}, {"land", job.land}, {"place", job.place}, {"name", job.name}, {"kind", job.kind}, {"class", job.cls},
            {"x", job.x}, {"y", job.y}, {"side", job.side}, {"brief", job.brief}, {"outside", job.options.outside},
            {"entrances", job.options.entrances}, {"roads", job.options.roads}, {"seed", job.options.seed},
            {"rootFields", job.rootFields}, {"withoutParts", job.withoutParts}, {"forest", job.forest}, {"world", job.world}};
}

SectionJob SectionJobFrom(const json& saved) {
    SectionJob job;
    job.id = saved.value("id", ""), job.land = saved.value("land", ""), job.place = saved.value("place", "");
    job.name = saved.value("name", ""), job.kind = saved.value("kind", ""), job.cls = saved.value("class", "");
    job.x = saved.value("x", 0), job.y = saved.value("y", 0), job.side = saved.value("side", 0);
    job.brief = saved.value("brief", json::object());
    job.options.side = job.side;
    job.options.fixedSide = true;
    job.options.outside = saved.value("outside", json());
    job.options.entrances = saved.value("entrances", json());
    job.options.roads = saved.value("roads", json());
    job.options.seed = saved.value("seed", static_cast<uint64_t>(7));
    job.rootFields = saved.value("rootFields", json::object());
    job.withoutParts = saved.value("withoutParts", json::array());
    job.forest = saved.value("forest", false);
    job.world = saved.value("world", "");
    // Its land: its square but what's outside it.
    job.landMask.assign(static_cast<size_t>(job.side) * job.side, 1);
    for (int c : DecodeCells(job.options.outside, job.side)) {
        if (c >= 0 && c < job.side * job.side) job.landMask[c] = 0;
    }
    return job;
}

void ApplySectionSpec(const SectionJob& job, json& spec) {
    if (job.forest || !spec.contains("root")) return;
    for (const auto& [k, v] : job.rootFields.items()) spec["root"]["quality"][k] = v;
    json kept = json::array();
    for (const auto& c : spec["root"].value("children", json::array())) {
        bool out = false;
        for (const auto& part : job.withoutParts) out = out || c.value("module", "") == part.get<std::string>();
        if (!out) kept.push_back(c);
    }
    spec["root"]["children"] = kept;
}

void PutSection(json& world, json section, const SectionJob& job, const std::vector<std::string>& worldGround) {
    const json wb = world["grid"]["bounds"];
    const int WW = wb[2], WH = wb[3];
    // Round its land, the world's ground (the land it sits in).
    std::vector<std::string> ground = TerrainCells(section, job.side, job.side);
    for (int c = 0; c < job.side * job.side; ++c) {
        if (job.landMask[c]) continue;
        const int wx = job.x + c % job.side, wy = job.y + c / job.side;
        ground[c] = wx < WW && wy < WH && !worldGround.empty() ? worldGround[static_cast<size_t>(wy) * WW + wx] : "grass";
    }
    SetTerrain(section, ground, job.side, job.side);
    PutInto(world, section, job.x, job.y, job.id, &job.landMask);
}

void FinishTogether(json& together) {
    const json b = together["grid"]["bounds"];
    const int W = b[2], H = b[3];
    std::vector<std::string> ground = TerrainCells(together, W, H);
    for (auto& g : ground) {
        if (g == "road") g = "dirt";
    }
    SetTerrain(together, ground, W, H);
}

} // namespace aige
