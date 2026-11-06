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
#include "assets/TextureCook.h"
#include "editing/PaintedPlace.h"
#include "editing/PlaceBlockout.h"
#include "places/Brief.h"
#include "places/Compile.h"
#include "places/Ontology.h"
#include "places/Planner.h"
#include "places/Solver.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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

// A place planned from its Brief alone, as the editor does with the AI off: the fallback answers, expanded, tidied,
// resolved, compiled and solved. `edit` changes the Spec before it's compiled.
Planned PlanPlace(const Ontology& ontology, json brief, const std::string& id, CompileOptions options,
                  const std::function<void(json&)>& edit = {}) {
    Planned out;
    out.brief = CompleteBrief(ontology, brief);
    json spec = SpecFromRoot(ontology, out.brief, FallbackRootAnswer(ontology, out.brief), id);
    const json root = spec.value("root", json::object());
    for (const auto& zone : ZoneIds(ontology, spec)) {
        if (json* node = SpecNode(spec, zone)) ApplyZoneAnswer(ontology, out.brief, spec, zone, FallbackZoneAnswer(ontology, *node, root));
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

// A section's plan put into the world's where the world placed it (its box at ox, oy): its ground, its things, its
// fences and walls, its areas; what the world had in its box goes.
void PutSection(json& world, const json& section, int ox, int oy, const std::string& prefix) {
    const json wb = world["grid"]["bounds"], sb = section["grid"]["bounds"];
    const int W = wb[2], H = wb[3], SW = sb[2], SH = sb[3];
    auto inside = [&](int x, int y) { return x >= ox && y >= oy && x < ox + SW && y < oy + SH; };
    std::vector<std::string> cells = TerrainCells(world, W, H);
    const std::vector<std::string> own = TerrainCells(section, SW, SH);
    for (int y = 0; y < SH; ++y) {
        for (int x = 0; x < SW; ++x) {
            if (ox + x < W && oy + y < H && !own[static_cast<size_t>(y) * SW + x].empty()) cells[static_cast<size_t>(oy + y) * W + ox + x] = own[static_cast<size_t>(y) * SW + x];
        }
    }
    SetTerrain(world, cells, W, H);
    json objects = json::array();
    for (const auto& o : world.value("objects", json::array())) {
        if (!inside(o.value("x", 0), o.value("y", 0))) objects.push_back(o);
    }
    for (json o : section.value("objects", json::array())) {
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
        for (auto& e : l["edges"]) e[0] = e[0].get<int>() + ox, e[1] = e[1].get<int>() + oy;
        l["id"] = prefix + "/" + l.value("id", "");
        world["lines"].push_back(l);
    }
    for (const auto& b : section["terrain"].value("bridges", json::array())) world["terrain"]["bridges"].push_back({b[0].get<int>() + ox, b[1].get<int>() + oy});
    for (json r : section.value("regions", json::array())) {
        std::vector<int> moved;
        for (int c : DecodeCells(r.value("cells", json::array()), SW)) moved.push_back((c / SW + oy) * W + c % SW + ox);
        r["cells"] = EncodeCells(moved, W);
        if (const json bounds = r.value("bounds", json()); bounds.is_array() && bounds.size() == 4) {
            r["bounds"] = {bounds[0].get<int>() + ox, bounds[1].get<int>() + oy, bounds[2], bounds[3]};
        }
        r["id"] = prefix + "/" + r.value("id", "");
        world["regions"].push_back(r);
    }
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
    brief.erase("test");
    // Each site in a settled zone of its own (a heartland each), so the world spreads them over its land, apart, with
    // its forests, hills, mountains and water between them.
    const Planned world = PlanPlace(ontology, brief, "test-place", {}, [&](json& spec) {
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
            const int around = static_cast<int>(side * 1.5) + 2 * 18;
            h["quantity"] = {{"sites", {{"count", 1}}}, {"area", {{"area", around * around}}}};
            h["children"] = json::array({json{{"id", h["id"].get<std::string>() + "/site"}, {"module", "site"},
                                              {"quality", {{"build-type", kind}, {"scale-class", cls}, {"name", s.value("name", "")}}},
                                              {"quantity", json::object()}, {"children", json::array()}}});
            kept.push_back(h);
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

    // Each site a section: planned as a place of its own kind in its box, then put into the world.
    json together = world.solved;
    json sections = json::array();
    for (const auto& r : world.solved.value("regions", json::array())) {
        if (r.value("module", "") != "site") continue;
        json spec = world.spec;
        const json* node = SpecNode(spec, r.value("node", ""));
        if (node == nullptr) continue;
        const json q = node->value("quality", json::object());
        const std::string kind = q.value("build-type", "settlement"), cls = q.value("scale-class", ""), name = q.value("name", "");
        const json bounds = r["bounds"];
        const int x = bounds[0], y = bounds[1], w = bounds[2], h = bounds[3];
        json sectionBrief = {{"buildType", kind}, {"name", name}};
        // Its parts, as the test names them (else the planner's).
        const std::string n = r.value("node", "");
        const int index = n.rfind("heartland-", 0) == 0 ? std::atoi(n.substr(10).c_str()) - 1 : -1;
        if (index >= 0 && index < static_cast<int>(sites.size()) && sites[index].contains("components")) sectionBrief["components"] = sites[index]["components"];
        if (!cls.empty() && ontology.Build(kind) && ontology.Build(kind)->Scale(cls)) sectionBrief["scale"] = {{"class", cls}};
        CompileOptions options;
        options.side = std::min(w, h);
        options.fixedSide = true;
        options.seed = std::hash<std::string>{}(r.value("id", "")) % 1000003; // each section its own layout
        const std::string id = r.value("id", "site");
        Planned section = PlanPlace(ontology, sectionBrief, id, options);
        for (const auto& p : section.problems) problems << id << " " << p << "\n";
        std::string file = id;
        std::replace(file.begin(), file.end(), '/', '-');
        std::replace(file.begin(), file.end(), '#', '-');
        Draw(ontology, section.solved, out / (file + ".png"), 2000);
        int buildings = 0;
        for (const auto& o : section.solved.value("objects", json::array())) buildings += o.value("layer", "") == "building" && o.value("primary", false);
        int zones = 0;
        for (const auto& c : section.spec["root"].value("children", json::array())) {
            const ModuleDef* m = ontology.Module(c.value("module", ""));
            zones += m && m->level == PlaceLevel::Zone;
        }
        PutSection(together, section.solved, x, y, id);
        sections.push_back({{"id", id}, {"name", name}, {"kind", kind}, {"class", cls}, {"bounds", bounds}, {"zones", zones}, {"buildings", buildings},
                            {"problems", section.problems.size()}});
        std::cout << "  " << id << " " << name << ": " << cls << " " << kind << " " << w << "x" << h << ", " << zones << " zones, "
                  << buildings << " buildings, " << section.problems.size() << " problems\n";
    }
    std::ofstream(out / "sections.json") << sections.dump(2);
    std::ofstream(out / "together.solved.json") << together.dump(2);
    Draw(ontology, together, out / "together.png", 5000);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[1]) == "--brief") return FromBrief(argv[2], argv[3]);
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
