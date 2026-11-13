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
#include "places/WorldSections.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
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

// The AI's answers kept (AIGE_AI_CACHE: a folder) by what was asked, so a rerun plans the same and asks nothing.
std::string AiCacheFile(const std::string& path, const json& body) {
    const char* dir = std::getenv("AIGE_AI_CACHE");
    if (dir == nullptr || *dir == 0) return "";
    const std::string text = path + body.dump();
    uint64_t h = 1469598103934665603ull;
    for (unsigned char ch : text) h = (h ^ ch) * 1099511628211ull;
    char name[32];
    std::snprintf(name, sizeof name, "%016llx.json", static_cast<unsigned long long>(h));
    std::filesystem::create_directories(dir);
    return (std::filesystem::path(dir) / name).string();
}

json AskAi(const std::string& path, const json& body) {
    if (gAiPort <= 0) return json();
    const std::string cached = AiCacheFile(path, body);
    if (!cached.empty() && std::filesystem::exists(cached)) {
        std::ifstream in(cached);
        try {
            return json::parse(in);
        } catch (const std::exception&) {
        }
    }
    httplib::Client client("127.0.0.1", gAiPort);
    client.set_read_timeout(600);
    const auto r = client.Post(path, body.dump(), "application/json");
    if (!r || r->status != 200) return json();
    try {
        const json answer = json::parse(r->body);
        json out = answer.value("ok", true) && answer.contains("answer") ? answer["answer"] : json();
        if (!cached.empty() && !out.is_null()) std::ofstream(cached) << out.dump();
        return out;
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
    const std::string aiWhat = brief.value("test", json::object()).value("ai", "");
    const bool aiSections = aiWhat == "sections" || aiWhat == "all";
    const bool aiWorld = aiWhat == "all"; // the world plan's root and zones by the AI too
    const bool reuse = brief.value("test", json::object()).value("reuse", false); // the sections planned last time
    const json testBrief = brief.value("test", json::object());
    gAiPort = aiSections ? brief.value("test", json::object()).value("port", 8765) : 0;
    // The world's side, when the test sets it (else from its area and what it holds).
    CompileOptions worldOptions;
    worldOptions.side = brief.value("test", json::object()).value("side", 0);
    worldOptions.fixedSide = worldOptions.side > 0;
    // Its own seed (as the editor's build id gives one): each world its own coast, sea and ridges; test.seed tries another.
    worldOptions.seed = brief.value("test", json::object()).value("seed", static_cast<uint64_t>(std::hash<std::string>{}(brief.value("name", "world")) % 1000003));
    brief.erase("test");
    // Each site in a settled zone of its own (a heartland each), so the world spreads them over its land, apart, with
    // its forests, hills, mountains and water between them.
    // The test's sites are the world Brief's places (as the Confirm page sets them: name, type, size), keyed 1, 2, ...
    // so each is "heartland-<n>"; the engine puts them in the world plan (PlacesIntoWorld). The test's spacing and
    // sizes go where the Confirm page puts them: the Brief's root settings and each place's size.
    if (testBrief.contains("spacing")) brief["root"]["spacing"] = brief["quality"]["spacing"] = testBrief["spacing"];
    if (testBrief.contains("settlementSize")) brief["root"]["settlementSize"] = brief["quality"]["settlementSize"] = testBrief["settlementSize"];
    if (!sites.empty() && !brief.contains("places")) {
        json places = json::object();
        int n = 0;
        const json placeSizes = testBrief.value("placeSizes", json::object());
        for (const auto& s : sites) {
            const std::string key = std::to_string(++n);
            json p = {{"name", s.value("name", "")}, {"type", s.value("build-type", "settlement")}};
            if (placeSizes.contains(key)) p["size"] = placeSizes[key];
            else if (s.contains("size")) p["size"] = s["size"];
            else if (testBrief.contains("settlementSize")) p["size"] = testBrief["settlementSize"];
            else if (s.contains("scale-class") && !brief.value("quality", json::object()).contains("settlementSize")) p["class"] = s["scale-class"];
            places[key] = p;
        }
        brief["places"] = places;
    }
    const Planned world = PlanPlace(ontology, brief, "test-place", worldOptions, [&](json& spec) {
        if (testBrief.contains("spacing")) spec["root"]["quality"]["spacing"] = testBrief["spacing"]; // (the AI's root step may leave it out)
        if (sites.empty()) return;
        json& children = spec["root"]["children"];
        json kept = children;
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
    }, aiWorld);
    for (const auto& p : world.problems) problems << "world " << p << "\n";
    std::ofstream(out / "brief.json") << world.brief.dump(2);
    std::ofstream(out / "spec.json") << world.spec.dump(2);
    std::ofstream(out / "world.solved.json") << world.solved.dump(2);
    Draw(ontology, world.solved, out / "world.png", 3000);
    const json b = world.solved["grid"]["bounds"];
    std::cout << world.brief.value("buildType", "") << ": " << b.dump() << ", " << world.problems.size() << " problems\n";

    // The world plan alone (the test's worldOnly): its layout, to check before the sections are planned.
    if (testBrief.value("worldOnly", false)) {
        std::map<std::string, std::string> names;
        for (const auto& r : world.solved.value("regions", json::array())) {
            const std::string rid = r.value("id", "");
            if (rid.rfind("heartland-", 0) != 0 || rid.find('/') != std::string::npos) continue;
            const int index = std::atoi(rid.substr(10).c_str()) - 1;
            if (index >= 0 && index < static_cast<int>(sites.size())) names[rid] = sites[index].value("name", rid);
        }
        DrawLayout(ontology, world.spec, world.solved, world.brief.value("name", "The world") + ": the world plan", out / "world-layout.png", 2400, names);
        return 0;
    }
    // Each place a section of its own shape: its land in the world plan (the organic zone it was given), planned as a
    // place of its kind on that land's square (what isn't its land left unplanned, its ways in on its land's edge),
    // given the world's ground round it, then put into the world.
    json together = world.solved;
    json sections = json::array();
    const int WW = b[2], WH = b[3];
    // Its sections, as the Builder plans them (WorldSections): each its land, ways in, the world's roads, its own Brief.
    const std::vector<std::string> worldGroundCells = TerrainCells(world.solved, WW, WH);
    for (const SectionJob& job : WorldSectionJobs(ontology, world.brief, world.spec, world.solved)) {
        const std::string& id = job.id;
        const std::string &kind = job.kind, &cls = job.cls, &name = job.name;
        const int x = job.x, y = job.y, w = job.side, h = job.side;
        Planned section;
        std::string saved = id;
        std::replace(saved.begin(), saved.end(), '/', '-');
        std::replace(saved.begin(), saved.end(), '#', '-');
        if (reuse && fs::exists(out / (saved + "-solved.json")) && fs::exists(out / (saved + "-spec.json"))) {
            std::ifstream a(out / (saved + "-solved.json")), b2(out / (saved + "-spec.json"));
            section.solved = json::parse(a);
            section.spec = json::parse(b2);
        } else {
            section = PlanPlace(ontology, job.brief, id, job.options, [&](json& spec) { ApplySectionSpec(job, spec); }, aiSections);
        }
        for (const auto& p : section.problems) problems << id << " " << p << "\n";
        std::string file = saved;
        int buildings = 0;
        for (const auto& o : section.solved.value("objects", json::array())) buildings += o.value("layer", "") == "building" && o.value("primary", false);
        int zones = 0;
        for (const auto& c : section.spec["root"].value("children", json::array())) {
            const ModuleDef* m = ontology.Module(c.value("module", ""));
            zones += m && m->level == PlaceLevel::Zone;
        }
        PutSection(together, section.solved, job, worldGroundCells);
        Draw(ontology, section.solved, out / (file + ".png"), 2000);
        std::ofstream(out / (file + "-spec.json")) << section.spec.dump(2);
        std::ofstream(out / (file + "-solved.json")) << section.solved.dump(2);
        DrawLayout(ontology, section.spec, section.solved, (name.empty() ? id : name) + " (" + kind + ")", out / (file + "-layout.png"), 1400);
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
    FinishTogether(together); // its world roads as ordinary roads (their own colour is the world plan's alone)
    DrawLayout(ontology, world.spec, together, world.brief.value("name", "The world") + ": the world with its sections", out / "together-layout.png", 3600,
               worldNames);
    std::ofstream(out / "together.solved.json") << together.dump(2);
    Draw(ontology, together, out / "together.png", 5000);
    return 0;
}

} // namespace

// A place's solved plan prepared for painting as the editor's ④ Paint does (PrepareBlockout: the painted things, the
// id map, the legend), at `cellPx` pixels across a cell.
int Prepare(const fs::path& solvedFile, const fs::path& out, int cellPx, bool landOnly) {
    Ontology ontology;
    ontology.Load({EnginePlacesDir()});
    json solved;
    {
        std::ifstream in(solvedFile);
        solved = json::parse(in);
    }
    const json b = solved["grid"]["bounds"];
    const int width = (b[2].get<int>() + b[3].get<int>()) * cellPx / 2;
    if (landOnly) {
        // The land alone: its ground, roads, water and what grows on it (trees, bushes, plants); no buildings, fences,
        // walls or other things.
        static const std::set<std::string> kGrows = {"tree", "bush", "hedge", "shrub", "grass", "flower", "crop", "plant", "reed", "fern",
                                                     "herb", "weed"};
        json kept = json::array();
        for (const auto& o : solved.value("objects", json::array())) {
            if (o.value("layer", "") != "building" && kGrows.contains(o.value("slot", ""))) kept.push_back(o);
        }
        solved["objects"] = kept;
        solved["lines"] = json::array();
        // Its ground clean: only the main roads (and the square) stay roads; the lanes, yards and plots, and the
        // world's site boxes round it, are grass.
        const int W = b[2], H = b[3];
        std::vector<std::string> ground = TerrainCells(solved, W, H);
        std::vector<uint8_t> main(static_cast<size_t>(W) * H, 0);
        for (const auto& p : solved.value("paths", json::array())) {
            if (p.value("class", "") != "main") continue;
            const int r = std::max(1, p.value("width", 2)) / 2 + 1;
            for (const auto& c : p.value("cells", json::array())) {
                for (int dy = -r; dy <= r; ++dy) {
                    for (int dx = -r; dx <= r; ++dx) {
                        const int x = c[0].get<int>() + dx, y = c[1].get<int>() + dy;
                        if (x >= 0 && y >= 0 && x < W && y < H) main[static_cast<size_t>(y) * W + x] = 1;
                    }
                }
            }
        }
        for (int c = 0; c < W * H; ++c) {
            std::string& g = ground[c];
            if (g == "road") g = "dirt"; // the world's roads round it: roads (as a road looks)
            else if (g == "forest-floor") g = "grass"; // under the trees: plain ground
            else if ((g == "dirt" || g == "tilled" || g == "plaza") && !main[c]) g = "grass";
        }
        SetTerrain(solved, ground, W, H);
    }
    const json painting = PrepareBlockout(solved, &ontology, out, width);
    std::cout << "block-out " << painting["size"].dump() << " at " << cellPx << " px a cell\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[1]) == "--brief") return FromBrief(argv[2], argv[3]);
    if (argc >= 5 && std::string(argv[1]) == "--prepare") return Prepare(argv[2], argv[3], std::atoi(argv[4]), argc >= 6 && std::string(argv[5]) == "--land");
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
