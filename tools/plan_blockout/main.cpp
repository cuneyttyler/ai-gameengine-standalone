// A dungeon's saved spec planned again with each packing (apart, packed), no AI, and its whole level drawn as the
// Paint method's block-out: what an image model is tested on (builder_tests/bfl_paint).
//
//     plan_blockout <spec.json> <out dir>
//
// Writes <packing>-solved.json, <packing>-blockout.png, <packing>-blockout-ids.png, <packing>-blockout.legend.json and
// <packing>-blockout-plain.png (no things in it); and <packing>-editor/ as the editor's Paint method prepares it.
//
//     plan_blockout --brief <brief.json> <out dir>
//
// A place planned from a Brief alone, as the editor does with the AI off (docs/places/tiling.md: a world from its
// definitions): the ontology's load problems, the spec, the solved plan and its block-out (spec.json, solved.json,
// blockout.png, problems.txt).
#include "assets/LayoutImage.h"
#include "assets/TextureCook.h"
#include "editing/PaintedPlace.h"
#include "editing/PlaceBlockout.h"
#include "places/Brief.h"
#include "places/Compile.h"
#include "places/Ontology.h"
#include "places/Planner.h"
#include "places/Solver.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <fstream>
#include <iostream>
#include <sstream>

using nlohmann::json;
namespace fs = std::filesystem;
using namespace aige;

namespace {

struct Planned {
    json brief, spec, solved;
    std::vector<std::string> problems;
};

// The AI service (its port), when the plan is the AI's: the Build panel's root and zone steps.
int gAiPort = 0;

json AskAi(const std::string& path, const json& body) {
    if (gAiPort <= 0) return json();
    httplib::Client client("127.0.0.1", gAiPort);
    client.set_read_timeout(600);
    const auto r = client.Post(path, body.dump(), "application/json");
    if (!r || r->status != 200) return json();
    try {
        const json answer = json::parse(r->body);
        return answer.value("ok", true) && answer.contains("answer") ? answer["answer"] : json();
    } catch (const std::exception&) {
        return json();
    }
}

// A place planned from its Brief: by the AI's root and zone steps when it's on (gAiPort; each step falling back to the
// rules when it can't answer), else as the editor does with the AI off: the fallback answers. Then expanded, tidied,
// resolved, compiled and solved. `edit` changes the Spec before it's compiled.
Planned PlanPlace(const Ontology& ontology, json brief, const std::string& id, CompileOptions options,
                  const std::function<void(json&)>& edit = {}, bool ai = false) {
    Planned out;
    out.brief = CompleteBrief(ontology, brief);
    json rootAnswer = ai ? AskAi("/places/plan-root", {{"brief", out.brief}, {"context", PlanRootContext(ontology, out.brief)}}) : json();
    if (ai) out.problems.push_back(rootAnswer.is_null() ? "ai: the root step fell back to the rules" : "ai: the root step is the AI's");
    if (rootAnswer.is_null()) rootAnswer = FallbackRootAnswer(ontology, out.brief);
    json spec = SpecFromRoot(ontology, out.brief, rootAnswer, id);
    const json root = spec.value("root", json::object());
    json siblings = json::array();
    for (const auto& c : spec["root"].value("children", json::array())) {
        siblings.push_back({{"id", c.value("id", "")}, {"module", c.value("module", "")}, {"quantity", c.value("quantity", json())},
                            {"quality", c.value("quality", json())}});
    }
    for (const auto& zone : ZoneIds(ontology, spec)) {
        json* node = SpecNode(spec, zone);
        if (node == nullptr) continue;
        json asked = *node;
        asked.erase("children");
        json answer = ai ? AskAi("/places/plan-zone", {{"brief", out.brief}, {"zone", asked}, {"siblings", siblings},
                                                       {"context", PlanZoneContext(ontology, out.brief, asked.value("module", ""))}})
                         : json();
        if (ai && answer.is_null()) out.problems.push_back("ai: zone " + zone + " fell back to the rules");
        if (answer.is_null()) answer = FallbackZoneAnswer(ontology, *node, root);
        ApplyZoneAnswer(ontology, out.brief, spec, zone, answer);
    }
    ExpandSpec(ontology, out.brief, spec);
    if (edit) edit(spec);
    for (const auto& p : TidySpec(ontology, spec)) out.problems.push_back("tidy: " + p.message);
    for (const auto& p : ResolveSpec(ontology, out.brief, spec)) out.problems.push_back("resolve: " + p.message);
    out.spec = spec;
    const SolvedLayout solved = SolveLayout(ontology, CompileSpec(ontology, spec, options));
    out.solved = solved.document;
    for (const auto& p : solved.problems) out.problems.push_back("solve: " + p.dump());
    return out;
}

// A solved plan's terrain as one name per cell, and back.
std::vector<std::string> TerrainCells(const json& solved, int W, int H) {
    std::vector<std::string> cells(static_cast<size_t>(W) * H, "");
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

// A section's plan put into the world's where the world placed it (its box at ox, oy). Only the section itself goes
// in: its buildings and the things, streets, yards and fields with them, and a ragged margin round them; beyond it the
// world's own land carries on, so the place has the outline of what's built, not of its box.
void PutSection(json& world, const json& section, int ox, int oy, const std::string& prefix, const std::vector<uint8_t>* landMask = nullptr) {
    const json wb = world["grid"]["bounds"], sb = section["grid"]["bounds"];
    const int W = wb[2], H = wb[3], SW = sb[2], SH = sb[3];
    // What's built: every object of the section but the wild land's dressing (its landscape scatter).
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
    // How far each cell is inside the section: from its land's edge (a section of its own shape), else its box's.
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
            // The whole section (its place and the land it keeps round it: its woods, meadows, water), but a ragged band
            // along its box's edge, 2.6 to 9.4 cells, where the world's land carries on: no line where they meet. What's
            // built (and a little round it) always goes in.
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
    // The section's own land inside its ragged edge; the rest stays the world's (its roads too).
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
        // The world's marker for the site (its building with a door) goes wherever it stood in the box.
        const bool marker = o.value("layer", "") == "building" && lx >= -2 && ly >= -2 && lx < SW + 2 && ly < SH + 2;
        if (!in(lx, ly) && !marker) objects.push_back(o);
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
    for (const auto& b : section["terrain"].value("bridges", json::array())) {
        if (in(b[0].get<int>(), b[1].get<int>())) world["terrain"]["bridges"].push_back({b[0].get<int>() + ox, b[1].get<int>() + oy});
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

// The plan drawn as the Build panel's Layout page draws it (top-down: its zones named, its buildings, roads, water),
// `names` the zones' labels by region id (else each its module's name).
void DrawLayout(const Ontology& ontology, const json& spec, const json& solved, const std::string& title, const fs::path& file, int width,
                std::map<std::string, std::string> names = {}) {
    LayoutImageOptions image;
    image.targetSize = width;
    image.title = title;
    std::function<void(const json&)> walk = [&](const json& node) {
        image.modules[node.value("id", "")] = node.value("module", "");
        for (const auto& c : node.value("children", json::array())) walk(c);
    };
    if (spec.contains("root")) walk(spec["root"]);
    for (const auto& [id, m] : ontology.Modules()) image.moduleNames[id] = m.name;
    for (const auto& r : solved.value("regions", json::array())) {
        const ModuleDef* m = ontology.Module(r.value("module", ""));
        if (m == nullptr || m->level != PlaceLevel::Zone) continue;
        const std::string id = r.value("id", "");
        if (!names.contains(id)) names[id] = m->name;
    }
    image.names = names;
    WritePng(file, RenderLayoutImage(solved, image));
}

void Draw(const Ontology& ontology, const json& solved, const fs::path& file, int width) {
    const json b = solved["grid"]["bounds"];
    json legend;
    WritePng(file, RenderPlanBlockout(solved, b[0], b[1], b[2], b[3], width, &ontology, &legend));
}

int FromBrief(const fs::path& briefFile, const fs::path& out) {
    fs::create_directories(out);
    Ontology ontology;
    std::ofstream problems(out / "problems.txt");
    for (const auto& e : ontology.Load({EnginePlacesDir()})) problems << "ontology: " << e << "\n", std::cout << "ontology: " << e << "\n";
    json brief;
    {
        std::ifstream in(briefFile);
        brief = json::parse(in);
    }
    // The test's own sites (a world's: each its build type, scale class and name), in place of the planner's.
    const json sites = brief.value("test", json::object()).value("sites", json::array());
    // The AI plans the sections when the test asks ("ai": "sections"), on the service's port.
    const bool aiSections = brief.value("test", json::object()).value("ai", "") == "sections";
    const bool reuse = brief.value("test", json::object()).value("reuse", false); // the sections planned last time
    const json testBrief = brief.value("test", json::object());
    gAiPort = aiSections ? brief.value("test", json::object()).value("port", 8765) : 0;
    // The world's side, when the test sets it (else from its area and what it holds).
    CompileOptions worldOptions;
    worldOptions.side = brief.value("test", json::object()).value("side", 0);
    worldOptions.fixedSide = worldOptions.side > 0;
    brief.erase("test");
    // Each site in a settled zone of its own (a heartland each), so the world spreads them over its land, apart, with
    // its forests, hills, mountains and water between them.
    const Planned world = PlanPlace(ontology, brief, "test-place", worldOptions, [&](json& spec) {
        if (sites.empty()) return;
        json& children = spec["root"]["children"];
        json kept = json::array();
        json heart;
        for (const auto& c : children) {
            if (c.value("module", "") == "heartland") {
                if (heart.is_null()) heart = c;
                continue;
            }
            kept.push_back(c);
        }
        if (heart.is_null()) heart = {{"module", "heartland"}, {"quality", json::object()}, {"quantity", json::object()}};
        int k = 0;
        for (const auto& s : sites) {
            ++k;
            json h = heart;
            h["id"] = "heartland-" + std::to_string(k);
            const std::string kind = s.value("build-type", "settlement"), cls = s.value("scale-class", "village");
            int side = 60;
            if (const BuildType* bt = ontology.Build(kind)) {
                const ScaleClass* sc = bt->Scale(cls);
                if (sc == nullptr && !bt->scale.empty()) sc = &bt->scale[bt->scale.size() / 2];
                if (sc) side = (sc->side.first + sc->side.second) / 2;
            }
            // Room round it: its fields, its approach, a little wild land of its own.
            const int around = static_cast<int>(side * 1.35 * 1.2) + 2 * 12;
            h["quantity"] = {{"sites", {{"count", 1}}}, {"area", {{"area", around * around}}}};
            h["children"] = json::array({json{{"id", h["id"].get<std::string>() + "/site"}, {"module", "site"},
                                              {"quality", {{"build-type", kind}, {"scale-class", cls}, {"name", s.value("name", "")}}},
                                              {"quantity", json::object()}, {"children", json::array()}}});
            kept.push_back(h);
        }
        // The test's forests: that many forest zones in the world plan, each to become a section of its own.
        const int forests = testBrief.value("forests", 0);
        if (forests > 0) {
            json others = json::array();
            for (const auto& c : kept) {
                if (c.value("module", "") != "forest") others.push_back(c);
            }
            for (int f = 1; f <= forests; ++f) {
                others.push_back({{"id", "forest-" + std::to_string(f)}, {"module", "forest"}, {"quality", json::object()},
                                  {"quantity", {{"area", {{"area", 9000 + 4000 * (f % 3)}}}}}, {"children", json::array()}});
            }
            kept = others;
        }
        // The Brief's settings for its parts (a component's quality: the mountains' snowy peaks), on their nodes.
        for (auto& c : kept) {
            const json q = brief.value("components", json::object()).value(c.value("module", ""), json::object()).value("quality", json::object());
            for (const auto& [k, v] : q.items()) c["quality"][k] = v;
        }
        children = kept;
    });
    for (const auto& p : world.problems) problems << "world " << p << "\n";
    std::ofstream(out / "brief.json") << world.brief.dump(2);
    std::ofstream(out / "spec.json") << world.spec.dump(2);
    std::ofstream(out / "world.solved.json") << world.solved.dump(2);
    Draw(ontology, world.solved, out / "world.png", 3000);
    const json b = world.solved["grid"]["bounds"];
    std::cout << world.brief.value("buildType", "") << ": " << b.dump() << ", " << world.problems.size() << " problems\n";

    // Each place a section of its own shape: its land in the world plan (the organic zone it was given), planned as a
    // place of its kind on that land's square (what isn't its land left unplanned, its ways in on its land's edge),
    // given the world's ground round it, then put into the world.
    json together = world.solved;
    json sections = json::array();
    const int WW = b[2], WH = b[3];
    const std::vector<std::string> worldGround = TerrainCells(world.solved, WW, WH);
    // The world's roads (its paths but rivers, and its road cells): where they cross a section's land's edge are its
    // ways in.
    std::vector<uint8_t> worldRoad(static_cast<size_t>(WW) * WH, 0);
    for (const auto& p : world.solved.value("paths", json::array())) {
        if (p.value("class", "") == "river" || p.value("terrain", "") == "water") continue;
        for (const auto& c : p.value("cells", json::array())) {
            const int cx = c[0], cy = c[1];
            if (cx >= 0 && cy >= 0 && cx < WW && cy < WH) worldRoad[static_cast<size_t>(cy) * WW + cx] = 1;
        }
    }
    for (const auto& c : world.solved.value("roads", json::array())) {
        const int cx = c[0], cy = c[1];
        if (cx >= 0 && cy >= 0 && cx < WW && cy < WH) worldRoad[static_cast<size_t>(cy) * WW + cx] = 1;
    }
    // The world type's sections (the ontology's): which zones of the plan are sections of their own and built as what
    // (a forest as a forest), and what no section has in it (forests: they're sections of their own).
    const BuildType* worldType = ontology.Build(world.brief.value("buildType", ""));
    const json sectionsDef = worldType && worldType->sections.is_object() ? worldType->sections : json::object();
    const json kinds = sectionsDef.value("kinds", json::object());
    const json withoutParts = sectionsDef.value("withoutParts", json::array());
    int forestCount = 0;
    static const char* kWoodNames[] = {"Mirkwold", "the Greenhollow", "Ashenleigh Wood", "the Thornwood", "Elderglade", "Fernmoor Wood",
                                       "the Old Weald", "Brackenridge", "Mossdeep", "the Silverbirches"};
    for (const auto& r : world.solved.value("regions", json::array())) {
        const std::string landId = r.value("id", "");
        if (landId.find('/') != std::string::npos) continue;
        json site;
        if (landId.rfind("heartland-", 0) == 0) {
            const int index = std::atoi(landId.substr(10).c_str()) - 1;
            if (index < 0 || index >= static_cast<int>(sites.size())) continue;
            site = sites[index];
            // No forests in it: the world's forests are sections of their own.
            json components = site.value("components", json::object());
            for (const auto& part : withoutParts) components[part.get<std::string>()] = {{"enabled", false}};
            site["components"] = components;
        } else if (const std::string module = r.value("module", ""); kinds.contains(module)) {
            site = {{"build-type", kinds[module]}, {"name", kWoodNames[forestCount++ % 10]}};
        } else {
            continue;
        }
        std::string kind = site.value("build-type", "settlement"), cls = site.value("scale-class", ""), name = site.value("name", "");
        std::vector<int> landCells = DecodeCells(r.value("cells", json::array()), WW);
        if (landCells.empty()) continue;
        int lx0 = WW, ly0 = WH, lx1 = 0, ly1 = 0;
        for (int c : landCells) lx0 = std::min(lx0, c % WW), ly0 = std::min(ly0, c / WW), lx1 = std::max(lx1, c % WW + 1), ly1 = std::max(ly1, c / WW + 1);
        const int side = std::max(lx1 - lx0, ly1 - ly0);
        const int x = lx0, y = ly0, w = side, h = side;
        if (cls.empty()) {
            if (const BuildType* bt = ontology.Build(kind)) {
                for (const auto& s : bt->scale) {
                    if (side >= s.side.first) cls = s.id; // the biggest its land reaches
                }
            }
        }
        std::vector<uint8_t> land(static_cast<size_t>(side) * side, 0);
        for (int c : landCells) land[static_cast<size_t>(c / WW - ly0) * side + c % WW - lx0] = 1;
        std::vector<int> outsideCells;
        for (int c = 0; c < side * side; ++c) {
            if (!land[c]) outsideCells.push_back(c);
        }
        json sectionBrief = {{"buildType", kind}, {"name", name}};
        if (site.contains("components")) sectionBrief["components"] = site["components"]; // its parts, as the test names them
        if (!cls.empty() && ontology.Build(kind) && ontology.Build(kind)->Scale(cls)) sectionBrief["scale"] = {{"class", cls}};
        CompileOptions options;
        options.side = side;
        options.fixedSide = true;
        options.seed = std::hash<std::string>{}(landId) % 1000003; // each section its own layout
        options.outside = EncodeCells(outsideCells, side);
        // Its ways in: its land's westmost cell on its middle row, its eastmost a little below.
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
        // Its ways in: where the world's roads cross its land's edge (a road cell of its land beside a road cell that
        // isn't its land), one per crossing; the farthest two its road's ends, the rest to its middle. A road that
        // only touches it gives one; none: its land's westmost and eastmost.
        std::vector<std::pair<int, int>> crossings;
        for (int yy = 0; yy < side; ++yy) {
            for (int xx = 0; xx < side; ++xx) {
                if (!land[static_cast<size_t>(yy) * side + xx]) continue;
                const int wx = x + xx, wy = y + yy;
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
            if (crossings[a].first > crossings[bIndex].first) std::swap(a, bIndex); // w the western of the two
            ways["w"] = {crossings[a].first, crossings[a].second};
            if (crossings.size() > 1) ways["e"] = {crossings[bIndex].first, crossings[bIndex].second};
            json more = json::array();
            for (size_t i = 0; i < crossings.size(); ++i) {
                if (i != a && i != bIndex) more.push_back({crossings[i].first, crossings[i].second});
            }
            if (!more.empty()) ways["more"] = more;
        }
        options.entrances = ways;
        // The world's roads through its land, kept as they are: each road's run of cells inside the land (in the road's
        // own order), the longest first: its main road.
        json carried = json::array();
        for (const auto& p : world.solved.value("paths", json::array())) {
            if (p.value("class", "") == "river" || p.value("terrain", "") == "water") continue;
            json run = json::array();
            // An end of a run that's inside the land, not on its edge (the world's road ended there): it goes on to the
            // place's middle.
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
                    // In the planned place, an ordinary road (the world's road colour is only for the world plan).
                    carried.push_back({{"cells", run}, {"width", p.value("width", 2)}, {"terrain", "dirt"}, {"open", open}});
                }
                run = json::array();
            };
            for (const auto& c : p.value("cells", json::array())) {
                const int lx = c[0].get<int>() - x, ly = c[1].get<int>() - y;
                if (lx >= 0 && ly >= 0 && lx < side && ly < side && land[static_cast<size_t>(ly) * side + lx]) run.push_back({lx, ly});
                else flush();
            }
            flush();
        }
        std::sort(carried.begin(), carried.end(), [](const json& a, const json& b2) { return a["cells"].size() > b2["cells"].size(); });
        options.roads = carried;
        const std::string id = landId + "/site";
        Planned section;
        std::string saved = id;
        std::replace(saved.begin(), saved.end(), '/', '-');
        std::replace(saved.begin(), saved.end(), '#', '-');
        if (reuse && fs::exists(out / (saved + "-solved.json")) && fs::exists(out / (saved + "-spec.json"))) {
            std::ifstream a(out / (saved + "-solved.json")), b2(out / (saved + "-spec.json"));
            section.solved = json::parse(a);
            section.spec = json::parse(b2);
        } else {
            // A section's own root as the world type says (sections.fields: no woods of its own); a forest is its woods.
            const json fields = sectionsDef.value("fields", json::object());
            const bool forest = kinds.contains(r.value("module", ""));
            section = PlanPlace(ontology, sectionBrief, id, options, [&](json& spec) {
                if (forest) return;
                for (const auto& [k, v] : fields.items()) spec["root"]["quality"][k] = v;
                // What no section has (its forests: sections of their own), whoever planned it (the AI too).
                json kept = json::array();
                for (const auto& c : spec["root"].value("children", json::array())) {
                    bool out = false;
                    for (const auto& part : withoutParts) out = out || c.value("module", "") == part.get<std::string>();
                    if (!out) kept.push_back(c);
                }
                spec["root"]["children"] = kept;
            }, aiSections);
        }
        for (const auto& p : section.problems) problems << id << " " << p << "\n";
        // Round its land, the world's ground (for its block-out and background: the land it sits in).
        {
            std::vector<std::string> ground = TerrainCells(section.solved, side, side);
            for (int c = 0; c < side * side; ++c) {
                if (land[c]) continue;
                const int wx = x + c % side, wy = y + c / side;
                ground[c] = wx < WW && wy < WH ? worldGround[static_cast<size_t>(wy) * WW + wx] : "grass";
            }
            SetTerrain(section.solved, ground, side, side);
        }
        std::string file = id;
        std::replace(file.begin(), file.end(), '/', '-');
        std::replace(file.begin(), file.end(), '#', '-');
        Draw(ontology, section.solved, out / (file + ".png"), 2000);
        std::ofstream(out / (file + "-spec.json")) << section.spec.dump(2);
        std::ofstream(out / (file + "-solved.json")) << section.solved.dump(2);
        DrawLayout(ontology, section.spec, section.solved, (name.empty() ? id : name) + " (" + kind + ")", out / (file + "-layout.png"), 1400);
        int buildings = 0;
        for (const auto& o : section.solved.value("objects", json::array())) buildings += o.value("layer", "") == "building" && o.value("primary", false);
        int zones = 0;
        for (const auto& c : section.spec["root"].value("children", json::array())) {
            const ModuleDef* m = ontology.Module(c.value("module", ""));
            zones += m && m->level == PlaceLevel::Zone;
        }
        PutSection(together, section.solved, x, y, id, &land);
        sections.push_back({{"id", id}, {"name", name}, {"kind", kind}, {"class", cls}, {"bounds", json{x, y, w, h}}, {"zones", zones}, {"buildings", buildings},
                            {"problems", section.problems.size()}});
        std::cout << "  " << id << " " << name << ": " << cls << " " << kind << " " << w << "x" << h << ", " << zones << " zones, "
                  << buildings << " buildings, " << section.problems.size() << " problems\n";
    }
    std::ofstream(out / "sections.json") << sections.dump(2);
    // The world's layout: its zones (each settled land named by its place), then the whole world with its sections in.
    std::map<std::string, std::string> worldNames;
    for (const auto& r : world.solved.value("regions", json::array())) {
        const std::string id = r.value("id", "");
        if (id.rfind("heartland-", 0) != 0 || id.find('/') != std::string::npos) continue;
        const int index = std::atoi(id.substr(10).c_str()) - 1;
        if (index >= 0 && index < static_cast<int>(sites.size())) worldNames[id] = sites[index].value("name", id) + "'s land";
    }
    DrawLayout(ontology, world.spec, world.solved, world.brief.value("name", "The world") + ": the world plan", out / "world-layout.png", 2400, worldNames);
    // The planned world: its world roads as ordinary roads (their own colour is the world plan's alone).
    {
        std::vector<std::string> ground = TerrainCells(together, WW, WH);
        for (auto& g : ground) {
            if (g == "road") g = "dirt";
        }
        SetTerrain(together, ground, WW, WH);
    }
    DrawLayout(ontology, world.spec, together, world.brief.value("name", "The world") + ": the world with its sections", out / "together-layout.png", 3600,
               worldNames);
    std::ofstream(out / "together.solved.json") << together.dump(2);
    Draw(ontology, together, out / "together.png", 5000);
    return 0;
}

} // namespace

// A place's solved plan prepared for painting as the editor's ④ Paint does (PrepareBlockout: the painted things, the
// id map, the legend), at `cellPx` pixels across a cell.
int Prepare(const fs::path& solvedFile, const fs::path& out, int cellPx) {
    Ontology ontology;
    ontology.Load({EnginePlacesDir()});
    json solved;
    {
        std::ifstream in(solvedFile);
        solved = json::parse(in);
    }
    const json b = solved["grid"]["bounds"];
    const int width = (b[2].get<int>() + b[3].get<int>()) * cellPx / 2;
    const json painting = PrepareBlockout(solved, &ontology, out, width);
    std::cout << "block-out " << painting["size"].dump() << " at " << cellPx << " px a cell\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[1]) == "--brief") return FromBrief(argv[2], argv[3]);
    if (argc >= 5 && std::string(argv[1]) == "--prepare") return Prepare(argv[2], argv[3], std::atoi(argv[4]));
    if (argc < 3) {
        std::cerr << "plan_blockout <spec.json> <out dir>  |  plan_blockout --brief <brief.json> <out dir>\n";
        return 2;
    }
    json source;
    {
        std::ifstream in(argv[1]);
        source = json::parse(in);
    }
    const fs::path out = argv[2];
    fs::create_directories(out);
    Ontology ontology;
    ontology.Load({EnginePlacesDir()});
    for (const std::string packing : {"apart", "packed"}) {
        json spec = source;
        spec["root"]["quality"]["packing"] = packing;
        spec["root"].erase("layoutSide");
        CompileOptions options;
        if (const json s = spec["root"].value("layoutSeed", json()); s.is_number_unsigned()) options.seed = s.get<uint64_t>();
        const json ir = CompileSpec(ontology, spec, options);
        const SolvedLayout solved = SolveLayout(ontology, ir);
        std::ofstream(out / (packing + "-solved.json")) << solved.document.dump(2);
        for (const auto& p : solved.problems) std::cout << "  " << packing << " problem: " << p.dump() << "\n";
        const json b = solved.document["grid"]["bounds"];
        json legend;
        ImageData ids;
        const ImageData image = RenderPlanBlockout(solved.document, b[0], b[1], b[2], b[3], 2000, &ontology, &legend, &ids);
        WritePng(out / (packing + "-blockout.png"), image);
        WritePng(out / (packing + "-blockout-ids.png"), ids);
        std::ofstream(out / (packing + "-blockout.legend.json")) << legend.dump(2);
        // And with no things at all: the rooms and walls alone (nothing an image model could read a thing from).
        json bare = solved.document;
        bare["objects"] = json::array();
        json bareLegend;
        ImageData bareIds;
        WritePng(out / (packing + "-blockout-plain.png"), RenderPlanBlockout(bare, b[0], b[1], b[2], b[3], 2000, &ontology, &bareLegend, &bareIds));
        // As the editor's Paint method prepares it (PrepareBlockout): only the things painted in, in their own colours.
        PrepareBlockout(solved.document, &ontology, out / (packing + "-editor"));
        std::cout << packing << ": bounds " << b.dump() << ", " << solved.document.value("regions", json::array()).size()
                  << " regions\n";
    }
    return 0;
}
