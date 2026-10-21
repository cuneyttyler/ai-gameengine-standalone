#include "editing/BuildWindow.h"
#include "editing/PlaceBuild.h"
#include "places/Brief.h"
#include "places/Compile.h"
#include "places/PlaceValidate.h"
#include "places/Planner.h"
#include "places/Solver.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

// development_plan.md 7.10: the build types beyond the settlement (region,
// castle, interior, dungeon and cave, farmland, wilderness, camp), each on the
// same procedure: a request, its Brief, the plan, the layout, its checks.
using namespace aige;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

const Ontology& Library() {
    static Ontology o = [] {
        Ontology ontology;
        ontology.Load({EnginePlacesDir()});
        return ontology;
    }();
    return o;
}

std::string Describe(const std::vector<PlaceProblem>& problems) {
    std::string out;
    for (const auto& p : problems) out += (p.warning ? "  (warning) " : "  ") + p.where + ": " + p.message + "\n";
    return out;
}

// A build with the AI off: everything from the request's words and the module rules.
struct Offline {
    fs::path root;
    explicit Offline(const std::string& name) : root(fs::temp_directory_path() / name) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Offline() {
        if (!std::getenv("AIGE_KEEP_BUILDS")) fs::remove_all(root); // AIGE_KEEP_BUILDS=1: look at its log afterwards
    }
    PlaceBuild::Host Host() {
        PlaceBuild::Host host;
        host.ontology = []() -> const Ontology& { return Library(); };
        host.catalog = [] { return Catalog{}; };
        host.isoGrid = [] { return false; };
        host.request = [](const std::string&, const json&, int) { return 0; };
        host.projectRoot = root;
        return host;
    }
};

// Runs a request to its laid-out plan; the solved layout.
json RunToLayout(PlaceBuild& build, const std::string& query, const std::string& type, bool started = false) {
    if (!started) EXPECT_EQ(build.Start(query), "") << query;
    EXPECT_EQ(build.Brief().value("buildType", ""), type) << query;
    EXPECT_EQ(build.Step("assets"), "") << query;
    EXPECT_EQ(build.Confirm(), "") << query;
    EXPECT_EQ(build.Stage(), "plan") << query << "\n" << build.State().dump().substr(0, 2000);
    std::vector<PlaceProblem> errors;
    for (const auto& p : build.PlanProblems()) {
        if (!p.warning) errors.push_back(p);
    }
    EXPECT_TRUE(errors.empty()) << query << "\n" << Describe(build.PlanProblems());
    return build.Solved();
}

std::string Failed(const json& solved) {
    std::string out;
    for (const auto& c : solved.value("checks", json::array())) {
        if (!c.value("ok", true)) {
            out += "  check " + c.dump() + "\n";
            if (c.value("predicate", "") == "completable" || c.value("predicate", "") == "rooms-reachable") {
                for (const auto& g : solved["spaces"]["graphs"]) out += "    graph " + g.dump() + "\n";
                for (const auto& k : solved["connectors"]) out += "    connector " + k.dump() + "\n";
            }
        }
    }
    for (const auto& p : solved.value("problems", json::array())) out += "  " + p.dump() + "\n";
    return out;
}

// A dungeon as text: # rock, . floor, , corridor, o a solid thing, d a flat one.
std::string MapText(const json& solved) {
    const int W = solved["grid"]["bounds"][2], H = solved["grid"]["bounds"][3];
    std::vector<std::string> map(H, std::string(W, '#'));
    for (const auto& r : solved["regions"]) {
        if (r["id"] == "site" || r.value("module", "") == "dungeon-level") continue;
        for (int c : DecodeCells(r["cells"], W)) map[c / W][c % W] = r["id"].get<std::string>().find("corridors") != std::string::npos ? ',' : '.';
    }
    for (const auto& o : solved["objects"]) map[o["y"].get<int>()][o["x"].get<int>()] = o["layer"] == "decal" ? 'd' : 'o';
    std::string out;
    for (const auto& line : map) out += line + "\n";
    return out;
}

int Regions(const json& solved, const std::string& cls) {
    int n = 0;
    for (const auto& r : solved["regions"]) n += Library().IsA(r.value("module", ""), cls);
    return n;
}

const json* Check(const json& solved, const std::string& id) {
    for (const auto& c : solved["checks"]) {
        if (c.value("id", "") == id) return &c;
    }
    return nullptr;
}

} // namespace

TEST(BuildTypesTest, EveryBuildTypeLoadsWithItsModules) {
    EXPECT_TRUE(Library().Check().empty()) << [] {
        std::string out;
        for (const auto& p : Library().Check()) out += "  " + p + "\n";
        return out;
    }();
    for (const char* id : {"settlement", "farmland", "wilderness", "camp", "castle", "region", "dungeon", "interior"}) {
        const BuildType* build = Library().Build(id);
        ASSERT_NE(build, nullptr) << id;
        EXPECT_NE(Library().Module(build->root), nullptr) << id;
        EXPECT_FALSE(build->scale.empty()) << id;
        for (const auto& c : build->components) EXPECT_NE(Library().Module(c), nullptr) << id << ": " << c;
    }
    EXPECT_EQ(Library().DefaultBuild()->id, "settlement");
    EXPECT_EQ(Library().Build("dungeon")->unit.label, "rooms");
    EXPECT_EQ(Library().Build("dungeon")->layout, "dungeon");
    EXPECT_EQ(Library().Build("interior")->layout, "interior");
}

TEST(BuildTypesTest, ARequestNamesItsBuildType) {
    const std::map<std::string, std::string> requests = {
        {"a village by a river", "settlement"},
        {"a walled town with a market", "settlement"},
        {"some farmland with fields and pastures", "farmland"},
        {"a wilderness with ruins and a shrine", "wilderness"},
        {"a bandit camp in the woods", "camp"},
        {"a castle with a moat", "castle"},
        {"a kingdom with towns and castles", "region"},
        {"a crypt dungeon with a boss", "dungeon"},
        {"a cave with goblins", "dungeon"},
        {"the interior of an inn", "interior"},
    };
    for (const auto& [query, type] : requests) {
        const json draft = FallbackDraft(Library(), query);
        EXPECT_EQ(draft["brief"].value("buildType", ""), type) << query;
        EXPECT_TRUE(LooksLikeBuildRequest(Library(), "build " + query)) << query;
        const json brief = CompleteBrief(Library(), draft["brief"]);
        EXPECT_TRUE(brief["scale"].contains("class")) << query;
    }
}

TEST(BuildTypesTest, EachRunsOnTheSameProcedureToCheckedLayout) {
    struct Case {
        const char* query;
        const char* type;
        const char* unit; // the class it's counted in
        int least;        // at least so many of them laid out
    };
    for (const auto& c : {Case{"Build some farmland with fields, an orchard and pastures", "farmland", "BuildingParcel", 2},
                          Case{"Make a wilderness with ruins, standing stones and a shrine", "wilderness", "PointOfInterest", 2},
                          Case{"A military camp with a palisade", "camp", "BuildingParcel", 5},
                          Case{"Build a castle with a moat", "castle", "BuildingParcel", 3},
                          Case{"Make a region with villages and a castle", "region", "Site", 3},
                          Case{"Build a crypt dungeon", "dungeon", "Room", 6},
                          Case{"Make a cave", "dungeon", "Room", 6},
                          Case{"The interior of an inn", "interior", "Room", 3}}) {
        Offline f(std::string("aige_buildtype_") + c.type);
        PlaceBuild build(f.Host());
        const auto start = std::chrono::steady_clock::now();
        const json solved = RunToLayout(build, c.query, c.type);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        ASSERT_TRUE(solved.is_object() && solved.contains("regions")) << c.query;
        std::cout << "[ " << c.type << " ] " << c.query << ": " << ms << " ms, " << solved["stats"].dump() << ", "
                  << Regions(solved, c.unit) << " " << Library().Build(c.type)->unit.label << std::endl;
        EXPECT_GE(Regions(solved, c.unit) + (std::string(c.unit) == "BuildingParcel" ? 0 : 0), c.least) << c.query << "\n" << Failed(solved);
        // Every check passes; nothing was left out for want of room.
        EXPECT_TRUE(Failed(solved).empty()) << c.query << "\n" << Failed(solved);
        // Verify: each row as planned.
        for (const auto& row : VerifyLayout(Library(), build.Brief(), build.Spec(), solved)) {
            EXPECT_NE(row.value("status", ""), "missing") << c.query << ": " << row.dump();
        }
        // And it can be approved: its map is drawn.
        ASSERT_EQ(build.Approve(), "") << c.query;
        EXPECT_TRUE(fs::exists(build.Folder() / "layout.png")) << c.query;
    }
}

namespace {

json SolveSpec(const json& brief, const json& rootAnswer) {
    std::string step = "spec";
    try {
        json spec = SpecFromRoot(Library(), brief, rootAnswer, "place");
        for (const auto& zone : ZoneIds(Library(), spec)) {
            step = "zone " + zone;
            ApplyZoneAnswer(Library(), brief, spec, zone, FallbackZoneAnswer(Library(), *SpecNode(spec, zone), spec["root"]));
        }
        step = "resolve";
        ResolveSpec(Library(), brief, spec);
        step = "expand";
        ExpandSpec(Library(), brief, spec);
        step = "rebalance";
        RebalanceBuildings(Library(), brief, spec);
        step = "compile";
        const json ir = CompileSpec(Library(), spec, {.seed = 3});
        step = "solve";
        const SolvedLayout solved = SolveLayout(Library(), ir);
        return {{"spec", spec}, {"solved", solved.document}};
    } catch (const std::exception& e) {
        ADD_FAILURE() << "at " << step << ": " << e.what();
        throw;
    }
}

} // namespace

TEST(BuildTypesTest, ADungeonsLocksHaveTheirKeysBeforeThem) {
    for (const int levels : {1, 2}) {
        const json brief = CompleteBrief(Library(), {{"buildType", "dungeon"}, {"scale", {{"class", "large"}}},
                                                     {"style", {{"theme", "crypt"}}}, {"components", {{"dungeon-level", {{"count", levels}}}}}});
        json root = {{"quantity", {{"levels", {{"count", levels}}}, {"locks", {{"count", 2}}}, {"secrets", {{"count", 1}}}}},
                     {"quality", {{"theme", "crypt"}, {"goal", "boss"}, {"boss", true}}}};
        root["children"] = json::array();
        for (int k = 0; k < levels; ++k) {
            root["children"].push_back({{"id", "level-" + std::to_string(k + 1)}, {"module", "dungeon-level"}, {"quantity", {{"rooms", {{"count", 9}}}}}});
        }
        const json result = SolveSpec(brief, root);
        const json& solved = result["solved"];
        EXPECT_TRUE(Failed(solved).empty()) << Failed(solved);
        const json* completable = Check(solved, "site/completable");
        ASSERT_NE(completable, nullptr);
        EXPECT_TRUE(completable->value("ok", false)) << completable->dump();
        // Independently: each key lies in a room reached without crossing its lock or a later one.
        int locked = 0;
        for (const auto& g : solved["spaces"]["graphs"]) {
            std::vector<int> lockEdges;
            for (size_t e = 0; e < g["edges"].size(); ++e) {
                if (g["edges"][e]["kind"] == "locked-door") lockEdges.push_back(static_cast<int>(e));
            }
            locked += static_cast<int>(lockEdges.size());
            for (const auto& key : g["keys"]) {
                const int lock = key["edge"];
                std::set<int> closed;
                bool from = false;
                for (int e : lockEdges) {
                    if (e == lock) from = true;
                    if (from) closed.insert(e);
                }
                std::set<std::string> reached = {g["start"].get<std::string>()};
                for (bool grew = true; grew;) {
                    grew = false;
                    for (size_t e = 0; e < g["edges"].size(); ++e) {
                        const json& edge = g["edges"][e];
                        if (closed.contains(static_cast<int>(e)) || edge["kind"] == "secret-passage") continue;
                        const bool a = reached.contains(edge["a"]), b = reached.contains(edge["b"]);
                        if (a != b) {
                            reached.insert(a ? edge["b"].get<std::string>() : edge["a"].get<std::string>());
                            grew = true;
                        }
                    }
                }
                EXPECT_TRUE(reached.contains(key["room"].get<std::string>())) << "key " << key.dump() << " behind its lock";
            }
        }
        EXPECT_GE(locked, 1) << "levels " << levels;
        // Doors and keys are objects of the level; a level below is reached by stairs.
        int doors = 0, keys = 0, stairs = 0;
        for (const auto& o : solved["objects"]) {
            doors += o["slot"] == "door" || o["slot"] == "locked-door";
            keys += o["slot"] == "key";
        }
        for (const auto& c : solved["connectors"]) stairs += c["kind"] == "stairs";
        EXPECT_GT(doors, 4);
        EXPECT_EQ(keys, locked);
        EXPECT_EQ(stairs, levels - 1);
        EXPECT_EQ(solved["grid"]["bounds"][2].get<int>() > solved["grid"]["bounds"][3].get<int>(), levels > 1);
    }
}

TEST(BuildTypesTest, ACaveHasRaggedChambersAndOpenings) {
    const json brief = CompleteBrief(Library(), {{"buildType", "dungeon"}, {"scale", {{"class", "medium"}}}, {"style", {{"theme", "cave"}}}});
    const json root = {{"quantity", {{"locks", {{"count", 0}}}}}, {"quality", {{"theme", "cave"}}},
                       {"children", json::array({{{"id", "caves"}, {"module", "dungeon-level"}, {"quantity", {{"rooms", {{"count", 8}}}}}}})}};
    const json result = SolveSpec(brief, root);
    const json& solved = result["solved"];
    EXPECT_TRUE(Failed(solved).empty()) << Failed(solved) << MapText(solved);
    int ragged = 0, rooms = 0;
    for (const auto& r : solved["regions"]) {
        if (!Library().IsA(r.value("module", ""), "Room")) continue;
        ++rooms;
        const json b = r["bounds"];
        ragged += r["area"].get<int>() < b[2].get<int>() * b[3].get<int>();
    }
    EXPECT_GE(rooms, 6);
    EXPECT_GE(ragged, rooms / 2) << "a cave's chambers aren't rectangles";
    for (const auto& c : solved["connectors"]) EXPECT_NE(c["kind"], "door") << "a cave has openings, not doors";
    // Rock between them: the terrain has it, and it's solid in the scene.
    const json palette = solved["terrain"]["palette"];
    EXPECT_NE(std::find(palette.begin(), palette.end(), "rock-wall"), palette.end());
    EXPECT_NE(std::find(palette.begin(), palette.end(), "cave-floor"), palette.end());
}

TEST(BuildTypesTest, AnInteriorHasRoomsDoorsStairsAndFurniture) {
    const json brief = CompleteBrief(Library(), {{"buildType", "interior"}, {"scale", {{"class", "hall"}}}});
    const json root = {{"quantity", {{"floors", {{"count", 2}}}}}, {"quality", {{"kind", "inn"}}},
                       {"children", json::array({{{"id", "ground"}, {"module", "floor-level"}, {"quantity", {{"rooms", {{"count", 4}}}}},
                                                  {"quality", {{"level", "ground"}}}},
                                                 {{"id", "upstairs"}, {"module", "floor-level"}, {"quantity", {{"rooms", {{"count", 4}}}}},
                                                  {"quality", {{"level", "upper"}}}}})}};
    const json result = SolveSpec(brief, root);
    const json& solved = result["solved"];
    EXPECT_TRUE(Failed(solved).empty()) << Failed(solved);
    // The inn's rooms: a tavern hall below, guest rooms above.
    std::set<std::string> modules;
    for (const auto& r : solved["regions"]) modules.insert(r.value("module", ""));
    EXPECT_TRUE(modules.contains("tavern-hall"));
    EXPECT_TRUE(modules.contains("guest-room"));
    EXPECT_EQ(Regions(solved, "Room"), 8);
    // Walls with doors in them, stairs linking the floors, windows.
    int walls = 0, gaps = 0;
    for (const auto& l : solved["lines"]) {
        if (l["slot"] != "wall") continue;
        walls += static_cast<int>(l["edges"].size());
        gaps += static_cast<int>(l["gaps"].size());
    }
    EXPECT_GT(walls, 40);
    EXPECT_GE(gaps, 8); // a door per room, and the front door
    EXPECT_EQ(solved["spaces"]["links"].size(), 1u);
    int windows = 0, anchors = 0;
    for (const auto& o : solved["objects"]) {
        windows += o["slot"] == "window";
        anchors += o["statement"].get<std::string>().find("-set/") != std::string::npos;
    }
    EXPECT_GT(windows, 2);
    EXPECT_GT(anchors, 4) << "furniture groups";
    for (const char* id : {"site/rooms-reachable", "site/groups-reachable", "site/doors-free"}) {
        const json* c = Check(solved, id);
        ASSERT_NE(c, nullptr) << id;
        EXPECT_TRUE(c->value("ok", false)) << c->dump();
    }
}

TEST(BuildTypesTest, ACastleHasAMoatAWallWithTowersAndAGate) {
    const json brief = CompleteBrief(Library(), {{"buildType", "castle"}, {"scale", {{"class", "castle"}}}});
    const json root = {{"children", json::array({{{"id", "inner-bailey"}, {"module", "bailey"}, {"quantity", {{"plots", {{"count", 6}}}}}},
                                                 {{"id", "moat"}, {"module", "moat"}},
                                                 {{"id", "curtain-wall"}, {"module", "curtain-wall"}}})}};
    const json result = SolveSpec(brief, root);
    const json& solved = result["solved"];
    EXPECT_TRUE(Failed(solved).empty()) << Failed(solved);
    const json* wall = nullptr;
    for (const auto& l : solved["lines"]) {
        if (l["id"] == "curtain-wall") wall = &l;
    }
    ASSERT_NE(wall, nullptr);
    EXPECT_FALSE((*wall)["gaps"].empty()) << "a gate where the road comes in";
    int towers = 0, gatehouses = 0;
    for (const auto& o : solved["objects"]) {
        towers += o["slot"] == "tower";
        gatehouses += o["slot"] == "gatehouse";
    }
    EXPECT_EQ(towers, 4);
    EXPECT_EQ(gatehouses, 1);
    const json palette = solved["terrain"]["palette"];
    EXPECT_NE(std::find(palette.begin(), palette.end(), "water"), palette.end()) << "the moat";
    EXPECT_FALSE(solved["terrain"]["bridges"].empty()) << "the road crosses the moat";
}

TEST(BuildTypesTest, ARegionsSitesAreJoinedByRoads) {
    const json brief = CompleteBrief(Library(), {{"buildType", "region"}, {"scale", {{"class", "valley"}}}});
    const json root = {{"quantity", {{"sites", {{"count", 4}}}}},
                       {"children", json::array({{{"id", "heartland"}, {"module", "heartland"}, {"quantity", {{"sites", {{"count", 4}}}}}},
                                                 {{"id", "woods"}, {"module", "biome-zone"}, {"quality", {{"biome", "forest"}}}},
                                                 {{"id", "south"}, {"module", "biome-zone"}, {"quality", {{"biome", "swamp"}}}}})}};
    const json result = SolveSpec(brief, root);
    const json& solved = result["solved"];
    EXPECT_TRUE(Failed(solved).empty()) << Failed(solved);
    EXPECT_EQ(Regions(solved, "Site"), 4);
    // Each site has a road reaching its edge.
    std::set<std::pair<int, int>> road;
    for (const auto& p : solved["paths"]) {
        for (const auto& c : p["cells"]) road.insert({c[0].get<int>(), c[1].get<int>()});
    }
    const int W = solved["grid"]["bounds"][2];
    for (const auto& r : solved["regions"]) {
        if (!Library().IsA(r.value("module", ""), "Site")) continue;
        bool reached = false;
        for (int c : DecodeCells(r["cells"], W)) {
            for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                reached = reached || road.contains({c % W + dx, c / W + dy});
            }
        }
        EXPECT_TRUE(reached) << r["id"];
    }
    // The swamp's ground is mud.
    const json palette = solved["terrain"]["palette"];
    EXPECT_NE(std::find(palette.begin(), palette.end(), "mud"), palette.end());
}

// The Phase 7 exit: a walled city from its request to its layout, with hundreds
// of buildings in wards, roads, walls with gates, and every check passing.
TEST(BuildTypesTest, AWalledCityFromItsRequest) {
    Offline f("aige_buildtype_city");
    PlaceBuild build(f.Host());
    const auto start = std::chrono::steady_clock::now();
    ASSERT_EQ(build.Start("Build a walled city by a river with markets, a temple and docks"), "");
    // The confirmation window gives the size in numbers.
    const json window = build.State().value("window", json::object());
    EXPECT_EQ(BriefValue(build.Brief(), "scale.class"), "city");
    const json counts = BriefValue(build.Brief(), "scale.buildings.count");
    ASSERT_TRUE(counts.is_array()) << build.Brief().dump();
    EXPECT_GE(counts[0].get<int>(), 500);
    const json solved = RunToLayout(build, "the city", "settlement", true);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    int buildings = 0;
    for (const auto& o : solved["objects"]) buildings += o["layer"] == "building" && o.value("primary", false);
    std::cout << "[ city ] " << ms << " ms, " << solved["stats"].dump() << ", " << buildings << " buildings" << std::endl;
    EXPECT_GE(buildings, 300);
    bool gated = false;
    for (const auto& l : solved["lines"]) gated = gated || (l["slot"] == "wall" && !l["gaps"].empty());
    EXPECT_TRUE(gated) << "walls with gates";
    // Every hard check passes (each ward's plots placed, walls closed, every door reached);
    // the soft ones (a ward's built density) are shown, as Verify shows them.
    std::string hard, soft;
    for (const auto& c : solved["checks"]) {
        if (c.value("ok", true)) continue;
        (c.value("predicate", "") == "footprint-density" ? soft : hard) += "  " + c.dump() + "\n";
    }
    EXPECT_TRUE(hard.empty()) << hard;
    if (!soft.empty()) std::cout << "[ city ] soft checks:\n" << soft;
    for (const auto& p : solved["problems"]) EXPECT_NE(p.value("code", "").substr(0, 2), "S-") << p.dump();
    (void)window;
}

// The Build panel's choices: structured input from the ontology, beside the words.
TEST(BuildTypesTest, TheChoicesComeFromTheOntology) {
    const json options = BuildChoices(Library());
    std::map<std::string, json> byId;
    for (const auto& t : options["types"]) byId[t["id"]] = t;
    ASSERT_EQ(byId.size(), 8u);
    EXPECT_EQ(options["types"][0]["id"], "settlement"); // the default first
    auto paths = [&](const std::string& type) {
        std::set<std::string> out;
        for (const auto& f : byId[type]["fields"]) out.insert(f["path"]);
        return out;
    };
    auto parts = [&](const std::string& type) {
        std::set<std::string> out;
        for (const auto& p : byId[type]["parts"]) out.insert(p["id"]);
        return out;
    };
    EXPECT_TRUE(paths("settlement").contains("walled"));
    EXPECT_TRUE(paths("settlement").contains("shape.layout"));
    EXPECT_TRUE(paths("dungeon").contains("root.levels"));
    EXPECT_TRUE(paths("dungeon").contains("root.goal"));
    EXPECT_TRUE(paths("dungeon").contains("style.theme"));
    EXPECT_TRUE(paths("interior").contains("root.floors"));
    EXPECT_TRUE(paths("interior").contains("root.kind"));
    EXPECT_TRUE(parts("settlement").contains("market-ward"));
    EXPECT_FALSE(parts("settlement").contains("house-plot")) << "the many alike aren't offered";
    EXPECT_TRUE(parts("castle").contains("moat"));
    EXPECT_TRUE(parts("dungeon").contains("boss-arena"));
    for (const auto& [id, t] : byId) {
        EXPECT_FALSE(t["classes"].empty()) << id;
        EXPECT_LE(t["parts"].size(), 24u) << id;
    }
}

TEST(BuildTypesTest, ChoicesWinOverTheWords) {
    // The words read a village; the choices make it a castle with a moat.
    json brief = CompleteBrief(Library(), FallbackDraft(Library(), "a village by a river with a market")["brief"]);
    ASSERT_EQ(brief["buildType"], "settlement");
    brief = ApplyChoices(Library(), brief, {{"buildType", "castle"}, {"scale.class", "fortress"}, {"components.moat.enabled", true},
                                            {"style.condition", "ruined"}});
    EXPECT_EQ(brief["buildType"], "castle");
    EXPECT_EQ(BriefValue(brief, "scale.class"), "fortress");
    EXPECT_EQ(BriefValue(brief, "components.moat.enabled"), true);
    EXPECT_FALSE(brief["components"].contains("market-ward")) << "not a castle's";
    EXPECT_EQ(BriefValue(brief, "style.condition"), "ruined");
    std::vector<PlaceProblem> errors;
    for (const auto& p : ValidateBrief(Library(), brief)) {
        if (!p.warning) errors.push_back(p);
    }
    EXPECT_TRUE(errors.empty()) << Describe(errors);
    // Numbers over a class: the class follows them.
    const json numbers = ApplyChoices(Library(), brief, {{"scale.buildings", {{"count", json::array({2, 3})}}}});
    EXPECT_EQ(BriefValue(numbers, "scale.class"), "tower-house");
    EXPECT_EQ(ChoicesQuery(Library(), {{"buildType", "castle"}, {"components.moat.enabled", true}}), "Build a castle with moat");
}

TEST(BuildTypesTest, ABuildFromChoicesAlone) {
    Offline f("aige_buildtype_choices");
    PlaceBuild build(f.Host());
    const json choices = {{"buildType", "dungeon"}, {"scale.class", "medium"}, {"style.theme", "cave"}, {"root.levels", 2},
                          {"root.goal", "boss"}, {"components.boss-arena.enabled", true}};
    ASSERT_EQ(build.Start("", choices), "");
    EXPECT_EQ(build.Brief()["buildType"], "dungeon");
    EXPECT_EQ(BriefValue(build.Brief(), "root.levels"), 2);
    EXPECT_EQ(BriefValue(build.Brief(), "style.theme"), "cave");
    EXPECT_EQ(build.Choices(), choices);
    const json solved = RunToLayout(build, "", "dungeon", true);
    int levels = 0;
    for (const auto& c : build.Spec()["root"]["children"]) levels += c["module"] == "dungeon-level";
    EXPECT_EQ(levels, 2);
    EXPECT_EQ(build.Spec()["root"]["quality"].value("goal", ""), "boss");
    bool arena = false;
    for (const auto& r : solved["regions"]) arena = arena || r.value("module", "") == "boss-arena";
    EXPECT_TRUE(arena);
    EXPECT_TRUE(Failed(solved).empty()) << Failed(solved) << MapText(solved);
    if (std::getenv("AIGE_DUMP_SOLVED")) std::ofstream(std::getenv("AIGE_DUMP_SOLVED")) << solved.dump();
}

TEST(BuildTypesTest, WordsAndChoicesTogether) {
    Offline f("aige_buildtype_both");
    PlaceBuild build(f.Host());
    ASSERT_EQ(build.Start("a village by a river", {{"scale.class", "town"}, {"components.temple-ward.enabled", true}}), "");
    EXPECT_EQ(BriefValue(build.Brief(), "scale.class"), "town");
    EXPECT_EQ(BriefValue(build.Brief(), "components.temple-ward.enabled"), true);
    EXPECT_NE(build.Brief().value("features", json::array()).dump().find("river"), std::string::npos) << "the words still count";
}

// ③ Layout: confirming lays out the zones alone; the user revises them there, then the plan follows.
TEST(BuildTypesTest, TheLayoutStepComesBeforeThePlan) {
    Offline f("aige_buildtype_layout");
    PlaceBuild::Host host = f.Host();
    host.layoutStep = true;
    PlaceBuild build(host);
    ASSERT_EQ(build.Start("a village by a river with a market and a temple"), "");
    // ① Confirm: straight to the layout after reading the request, the questions beside it.
    ASSERT_EQ(build.Stage(), "layout") << build.State()["status"];
    ASSERT_FALSE(build.Questions().empty());
    for (const auto& q : build.Questions()) EXPECT_EQ(q.value("about", ""), "placement") << q.dump();
    build.SetDirections("the market in the middle, the temple to the north");
    EXPECT_EQ(build.State()["directions"], "the market in the middle, the temple to the north");
    ASSERT_EQ(build.ReplanLayout(), "");
    ASSERT_EQ(build.Stage(), "layout") << build.State()["status"];
    const json state = build.State();
    ASSERT_TRUE(state.contains("layout"));
    int zones = 0;
    for (const auto& z : state["layout"]["zones"]) {
        if (z["level"] != "zone") continue;
        ++zones;
        EXPECT_FALSE(z.value("description", "").empty()) << z.dump();
        EXPECT_FALSE(z.value("where", "").empty()) << "where it lies: " << z.dump();
    }
    EXPECT_GE(zones, 2);
    EXPECT_TRUE(fs::exists(build.Folder() / "layout.png"));
    // Nothing inside the zones yet.
    for (const auto& r : build.Solved()["regions"]) EXPECT_NE(r.value("statement", "").find("/plots"), 0u);
    for (const auto& o : build.Solved()["objects"]) EXPECT_NE(o.value("layer", ""), "building") << o.dump();
    // The Contents tab is there too; a change to it makes the layout out of date.
    ASSERT_TRUE(state.contains("window"));
    build.Edit("walled", "palisade");
    EXPECT_TRUE(build.State()["layout"]["stale"].get<bool>());
    EXPECT_NE(build.PlanFromLayout(), "") << "planned from an out-of-date layout";
    ASSERT_EQ(build.ReplanLayout(), "");
    EXPECT_FALSE(build.State()["layout"]["stale"].get<bool>());
    // A zone drawn where the user wants it takes exactly that land; a zone's own pattern and size.
    std::string ward;
    const json zonesNow = build.State()["layout"]["zones"]; // named: iterated by reference
    for (const auto& z : zonesNow) {
        if (ward.empty() && z["level"] == "zone" && z.value("hasStyle", false)) ward = z["id"];
    }
    ASSERT_FALSE(ward.empty()) << build.State()["layout"]["zones"].dump().substr(0, 3000);
    ASSERT_EQ(build.DrawZone(ward, {{"x", 2}, {"y", 2}, {"w", 20}, {"h", 14}}), "");
    for (const auto& r : build.Solved()["regions"]) {
        if (r["id"] != ward) continue;
        EXPECT_GE(r["bounds"][0].get<int>(), 2);
        EXPECT_LE(r["bounds"][0].get<int>() + r["bounds"][2].get<int>(), 22);
        EXPECT_GT(r["area"].get<int>(), 200);
    }
    ASSERT_EQ(build.SetZoneField(ward, "street-style", "radial"), "");
    EXPECT_EQ(SpecNode(const_cast<json&>(build.Spec()), ward)->at("quality")["street-style"], "radial");
    EXPECT_NE(build.SetZoneField(ward, "street-style", "zigzag"), "") << "not a pattern";
    bool spaced = false;
    const json zonesAfter = build.State()["layout"]["zones"];
    for (const auto& z : zonesAfter) spaced = spaced || z.contains("space");
    EXPECT_TRUE(spaced) << "each zone's room is checked";
    // Then the plan: the zones kept near where the layout put them.
    std::map<std::string, std::pair<int, int>> before;
    const int side = build.Solved()["grid"]["bounds"][2];
    for (const auto& r : build.Solved()["regions"]) {
        if (r.value("statement", "") == "site/zones") before[r["id"]] = {r["seed"][0].get<int>(), r["seed"][1].get<int>()};
    }
    ASSERT_EQ(build.PlanFromLayout(), "");
    ASSERT_EQ(build.Stage(), "plan");
    const int after = build.Solved()["grid"]["bounds"][2];
    for (const auto& r : build.Solved()["regions"]) {
        if (r.value("statement", "") != "site/zones" || !before.contains(r["id"])) continue;
        const double k = static_cast<double>(after) / side;
        EXPECT_NEAR(r["seed"][0].get<int>(), before[r["id"]].first * k, 2.0) << r["id"];
        EXPECT_NEAR(r["seed"][1].get<int>(), before[r["id"]].second * k, 2.0) << r["id"];
    }
    // Back to the layout from the plan.
    ASSERT_EQ(build.Step("layout"), "");
    EXPECT_EQ(build.Stage(), "layout");
}

// The painting tool: a zone takes exactly the cells painted for it, taken from any other zone.
TEST(BuildTypesTest, AZoneIsPaintedCellByCell) {
    Offline f("aige_buildtype_paint");
    PlaceBuild::Host host = f.Host();
    host.layoutStep = true;
    PlaceBuild build(host);
    ASSERT_EQ(build.Start("a village by a river with a market and a temple"), "");
    ASSERT_EQ(build.Stage(), "layout");
    const json zones = build.State()["layout"]["zones"];
    std::vector<std::string> ids;
    for (const auto& z : zones) {
        if (z["level"] == "zone") ids.push_back(z["id"]);
    }
    ASSERT_GE(ids.size(), 2u);
    json cells = json::array();
    for (int y = 4; y < 14; ++y) {
        for (int x = 4; x < 16; ++x) cells.push_back({x, y});
    }
    ASSERT_EQ(build.PaintZone(ids[0], cells, false), "");
    ASSERT_EQ(build.PaintZone(ids[1], json::array({{6, 6}, {7, 6}}), false), ""); // two of them for the other
    const int W = build.Solved()["grid"]["bounds"][2];
    std::set<int> first;
    for (const auto& r : build.Solved()["regions"]) {
        if (r["id"] == ids[0]) {
            for (int c : DecodeCells(r["cells"], W)) first.insert(c);
        }
    }
    EXPECT_FALSE(first.contains(6 * W + 6)) << "repainted for the other zone";
    EXPECT_FALSE(first.contains(6 * W + 7)) << "repainted for the other zone";
    // A stroke adds to the land the zone has: every painted cell is its own.
    for (int y = 4; y < 14; ++y) {
        for (int x = 4; x < 16; ++x) {
            if (y == 6 && (x == 6 || x == 7)) continue;
            EXPECT_TRUE(first.contains(y * W + x)) << x << "," << y;
        }
    }
    ASSERT_EQ(build.PaintZone(ids[0], json::array({{10, 10}}), true), ""); // erased
    for (const auto& r : build.Solved()["regions"]) {
        if (r["id"] != ids[0]) continue;
        for (int c : DecodeCells(r["cells"], W)) EXPECT_NE(c, 10 * W + 10);
    }
    ASSERT_EQ(build.PlanFromLayout(), "");
    EXPECT_EQ(build.Stage(), "plan");
}

// The AI's relations, as the layout knows them.
TEST(BuildTypesTest, CompoundAndMapDirectionsAreNormalised) {
    const json out = NormalizeRelations(json::array({{{"type", "north-west-of"}, {"to", "market-ward"}},
                                                     {{"type", "below"}, {"to", "temple-ward"}},
                                                     {{"type", "north-west"}, {"to", "site"}},
                                                     {{"type", "along"}, {"to", "river"}}}));
    std::vector<std::string> got;
    for (const auto& r : out) got.push_back(r.value("type", "") + " " + r.value("to", ""));
    const std::vector<std::string> want = {"north-of market-ward", "west-of market-ward", "south-of temple-ward",
                                           "in-north site", "in-west site", "along river"};
    EXPECT_EQ(got, want);
}

// Reading a request: the AI is given the one build type it names, not all eight.
TEST(BuildTypesTest, ARequestIsReadWithItsBuildTypeAlone) {
    const json all = InterpretContext(Library());
    const json village = InterpretContext(Library(), NamedBuildType(Library(), "build a village by river with several parts"));
    ASSERT_EQ(village["buildTypes"].size(), 1u);
    EXPECT_EQ(village["buildTypes"][0]["id"], "settlement");
    EXPECT_EQ(NamedBuildType(Library(), "something nice"), "");
    std::cout << "[ context ] all types " << all.dump().size() << " bytes, the village's " << village.dump().size() << " bytes" << std::endl;
    EXPECT_LT(village.dump().size() * 2, all.dump().size());
}
