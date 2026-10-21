// The Build panel's steps, fast (no AI service: its answers given, or none): every kind of place
// read, laid out, planned and built; the layout and the plan revised as a user asks
// (docs/test.md, place-refine.md §9).
#include "assets/AssetDatabase.h"
#include "assets/Catalog.h"
#include "assets/Prefab.h"
#include "assets/ProjectTemplate.h"
#include "editing/Autotile.h"
#include "editing/PlaceBuild.h"
#include "editing/PlaceScene.h"
#include "places/Brief.h"
#include "places/Ontology.h"
#include "places/Solver.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <set>

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

// A build with a stand-in for the AI service: what it's sent, answered by the test (or not at all).
struct Bench {
    fs::path root;
    std::vector<std::pair<std::string, json>> sent;
    std::set<int> answered;
    bool ai = true;
    PlaceBuild::Host host;
    std::unique_ptr<PlaceBuild> build;

    explicit Bench(const std::string& name, bool withAi) : ai(withAi) {
        root = fs::temp_directory_path() / ("aige_flow_" + name);
        fs::remove_all(root);
        EXPECT_FALSE(CreateProject(root, std::nullopt, false).has_value());
        host.ontology = []() -> const Ontology& { return Library(); };
        host.catalog = [] { return Catalog{}; };
        host.isoGrid = [] { return true; };
        host.request = [this](const std::string& path, const json& body, int) {
            if (!ai) return 0; // the AI is off
            sent.push_back({path, body});
            return static_cast<int>(sent.size());
        };
        host.projectRoot = root;
        host.layoutStep = true;
        build = std::make_unique<PlaceBuild>(host);
    }
    ~Bench() {
        build.reset();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    // The requests to `path` not answered yet.
    std::vector<int> Waiting(const std::string& path) {
        std::vector<int> out;
        for (int i = 0; i < static_cast<int>(sent.size()); ++i) {
            if (sent[i].first == path && !answered.contains(i + 1)) out.push_back(i + 1);
        }
        return out;
    }
    void Answer(int id, const json& body) {
        answered.insert(id);
        build->OnResponse(id, body);
    }
    void FailAll(const std::string& path) {
        for (int id : Waiting(path)) Answer(id, {{"ok", false}, {"errors", json::array({"No AI provider configured"})}});
    }
};

// The zones' land: region id -> its cells.
std::map<std::string, std::set<int>> ZoneLand(const json& solved) {
    std::map<std::string, std::set<int>> out;
    const int w = solved["grid"]["bounds"][2].get<int>();
    for (const auto& r : solved.value("regions", json::array())) {
        if (r.value("statement", "") != "site/zones") continue;
        for (int c : DecodeCells(r.value("cells", json::array()), w)) out[r.value("id", "")].insert(c);
    }
    return out;
}

std::pair<double, double> Middle(const std::set<int>& cells, int w) {
    double x = 0, y = 0;
    for (int c : cells) x += c % w, y += c / w;
    return {x / std::max<size_t>(1, cells.size()), y / std::max<size_t>(1, cells.size())};
}

// The things a zone holds, where they stand: "id@x,y".
std::set<std::string> ThingsIn(const json& solved, const std::string& zone) {
    std::set<std::string> out;
    for (const auto& o : solved.value("objects", json::array())) {
        const std::string node = o.value("node", "");
        if (node == zone || node.rfind(zone + "/", 0) == 0 || node.rfind(zone + "#", 0) == 0) {
            out.insert(o.value("id", "") + "@" + std::to_string(o.value("x", 0)) + "," + std::to_string(o.value("y", 0)));
        }
    }
    return out;
}

int Buildings(const json& solved, const std::string& zone) {
    int n = 0;
    for (const auto& o : solved.value("objects", json::array())) {
        const std::string node = o.value("node", "");
        const bool in = node == zone || node.rfind(zone + "/", 0) == 0 || node.rfind(zone + "#", 0) == 0;
        n += in && o.value("layer", "") == "building" && o.value("primary", false);
    }
    return n;
}

json VillageBrief() {
    return CompleteBrief(Library(), {{"buildType", "settlement"}, {"name", "Riverford"}, {"scale", {{"class", "village"}}},
                                     {"components", {{"residential-ward", json::object()}, {"market-ward", json::object()}}}});
}

json RootAnswer() {
    return {{"children", json::array({{{"id", "Market"}, {"module", "market-ward"}, {"quantity", {{"plots", {{"count", 6}}}, {"area", {{"area", 500}}}}}},
                                      {{"id", "Ward 1"}, {"module", "residential-ward"}, {"quantity", {{"plots", {{"count", 12}}}, {"area", {{"area", 700}}}}}},
                                      {{"id", "Ward 2"}, {"module", "residential-ward"}, {"quantity", {{"plots", {{"count", 12}}}, {"area", {{"area", 700}}}}}},
                                      {{"id", "main road"}, {"module", "road"}, {"quality", {{"class", "main"}}}}})}};
}

json WardAnswer(int houses) {
    return {{"quantity", {{"plots", {{"count", houses}}}}},
            {"children", json::array({{{"id", "cottages"}, {"module", "house-plot"}, {"repeat", houses}, {"quality", {{"house-kind", "cottage"}}}},
                                      {{"id", "well"}, {"module", "well-plaza"}}})}};
}

json MarketAnswer() {
    return {{"children", json::array({{{"id", "square"}, {"module", "market-square"}, {"quantity", {{"stalls", {{"count", 6}}}}}},
                                      {{"id", "shops"}, {"module", "shop-plot"}, {"repeat", 3}}})}};
}

// A village at its layout: the request read (one reading), the zones planned and laid out.
void ToLayout(Bench& b) {
    ASSERT_EQ(b.build->Start("a village by a river with a market"), "");
    const auto readings = b.Waiting("/places/interpret");
    ASSERT_FALSE(readings.empty());
    b.Answer(readings[0], {{"ok", true}, {"drafts", json::array({{{"reading", "typical"}, {"brief", VillageBrief()}}})}});
    for (size_t i = 1; i < readings.size(); ++i) b.Answer(readings[i], {{"ok", false}, {"errors", json::array({"No AI provider configured"})}});
    b.FailAll("/places/repair-brief");
    for (int id : b.Waiting("/places/plan-root")) b.Answer(id, {{"ok", true}, {"answer", RootAnswer()}});
    b.FailAll("/places/questions");
    b.FailAll("/places/repair-node");
    ASSERT_EQ(b.build->Stage(), "layout") << b.build->State().value("status", "");
}

// On to the plan, each zone's parcels answered, then approved (the review answered: fine).
void ToPlanned(Bench& b) {
    ASSERT_EQ(b.build->PlanFromLayout(), "");
    for (int round = 0; round < 3; ++round) {
        for (int id : b.Waiting("/places/plan-zone")) {
            const std::string module = b.sent[id - 1].second["zone"].value("module", "");
            b.Answer(id, {{"ok", true}, {"answer", module == "market-ward" ? MarketAnswer() : WardAnswer(8)}});
        }
        b.FailAll("/places/repair-node");
    }
    ASSERT_EQ(b.build->Stage(), "plan") << b.build->State().value("status", "");
    ASSERT_EQ(b.build->Approve(), "");
    for (int id : b.Waiting("/places/review-layout")) b.Answer(id, {{"ok", true}, {"answer", {{"summary", "Fine."}, {"changes", json::array()}}}});
    ASSERT_EQ(b.build->Stage(), "planned");
}

// A revision, answered with these changes (and facts to check).
void Revise(Bench& b, const std::string& text, const json& changes, const json& expect = json::array()) {
    ASSERT_EQ(b.build->Revise(text), "");
    const auto ids = b.Waiting("/places/revise-plan");
    if (ids.empty()) return; // done without the AI (a swap)
    b.Answer(ids.back(), {{"ok", true}, {"answer", {{"summary", "As asked."}, {"changes", changes}, {"expect", expect}}}});
    b.FailAll("/places/repair-node");
    for (int round = 0; round < 3; ++round) {
        for (int id : b.Waiting("/places/plan-zone")) b.Answer(id, {{"ok", false}, {"errors", json::array({"No AI provider configured"})}});
    }
}

} // namespace

// Every kind of place, from its words alone (the AI off): read as its kind, laid out, planned,
// and built into a scene with something in it, nothing thrown.
TEST(PlaceFlowTest, EveryKindOfPlaceIsReadLaidOutPlannedAndBuilt) {
    const std::vector<std::pair<std::string, std::string>> places = {
        {"a village by a river", "settlement"},          {"a stone castle with a keep", "castle"},
        {"a small courtyard with an inner chamber", "courtyard"}, {"the inside of a tavern", "interior"},
        {"a dungeon with a treasure room", "dungeon"},    {"a cave with an underground lake", "dungeon"},
        {"a bandit camp in a forest clearing", "camp"},   {"a farm with fields and an orchard", "farmland"},
        {"a misty swamp with ruins", "wilderness"}};
    int k = 0;
    for (const auto& [prompt, type] : places) {
        SCOPED_TRACE(prompt);
        Bench b("kind" + std::to_string(k++), false);
        std::string where = "start";
        try {
        ASSERT_EQ(b.build->Start(prompt), "");
        EXPECT_EQ(b.build->Brief().value("buildType", ""), type);
        for (int step = 0; step < 6; ++step) {
            const std::string stage = b.build->Stage();
            where = "step " + stage;
            if (stage == "request") ASSERT_EQ(b.build->Confirm(), "");
            else if (stage == "layout") ASSERT_EQ(b.build->PlanFromLayout(), "");
            else if (stage == "plan") ASSERT_EQ(b.build->Approve(), "");
            else break;
        }
        where = "planned";
        ASSERT_EQ(b.build->Stage(), "planned") << b.build->State().value("status", "");
        // Its art (none in a new project: placeholders), and the scene.
        PrefabLibrary prefabs(Project{b.root});
        AssetDatabase assets(Project{b.root});
        assets.Refresh();
        PlaceSceneOptions options;
        options.place = b.build->Id();
        where = "assets";
        const json plan = PlanPlaceAssets(Library(), b.build->Spec(), b.build->Solved(), BuildCatalog(assets, &Library(), {}), prefabs, assets,
                                          ProjectTerrainSet(prefabs, Project{b.root}), options, nullptr);
        where = "terrain set";
        const TerrainSet terrains = ProjectTerrainSet(prefabs, Project{b.root});
        where = "scene";
        Registry registry;
        json report;
        BuildPlaceScene(registry, prefabs, b.build->Solved(), plan, terrains, options, &report);
        where = "report";
        int placed = 0;
        for (const auto& [layer, n] : report.value("objects", json::object()).items()) placed += n.get<int>();
        EXPECT_GT(placed + report.value("pieces", 0), 0) << report.dump();
        } catch (const std::exception& e) {
            ADD_FAILURE() << prompt << ": " << where << ": " << e.what();
        }
    }
}

// ② Layout: two zones change places, exactly, by the request's words alone.
TEST(PlaceFlowTest, LayoutSwapTradesTheZonesLand) {
    Bench b("swap", true);
    ToLayout(b);
    const auto before = ZoneLand(b.build->Solved());
    ASSERT_TRUE(before.contains("market") && before.contains("ward-1"));
    const size_t sent = b.sent.size();
    Revise(b, "move ward 1 to where market is now and move market to where ward 1 is now", json::array());
    EXPECT_EQ(b.sent.size(), sent); // no AI needed
    const auto after = ZoneLand(b.build->Solved());
    auto share = [](const std::set<int>& a, const std::set<int>& b) {
        int same = 0;
        for (int c : a) same += b.contains(c);
        return static_cast<double>(same) / std::max<size_t>(1, a.size());
    };
    EXPECT_GT(share(after.at("market"), before.at("ward-1")), 0.85);
    EXPECT_GT(share(after.at("ward-1"), before.at("market")), 0.85);
}

// ② Layout: a zone made bigger.
TEST(PlaceFlowTest, LayoutZoneMadeBigger) {
    Bench b("bigger", true);
    ToLayout(b);
    const size_t was = ZoneLand(b.build->Solved()).at("ward-2").size();
    Revise(b, "make ward 2 bigger", json::array({{{"op", "set"}, {"node", "ward-2"}, {"quantity", {{"area", {{"area", static_cast<int>(was * 1.6)}}}}}}}));
    EXPECT_GT(ZoneLand(b.build->Solved()).at("ward-2").size(), was * 1.25);
}

// ② Layout: a zone put north, south, east or west of another.
TEST(PlaceFlowTest, LayoutZonePutByDirection) {
    for (const auto& [word, type] : std::vector<std::pair<std::string, std::string>>{
             {"north", "north-of"}, {"south", "south-of"}, {"east", "east-of"}, {"west", "west-of"}}) {
        SCOPED_TRACE(word);
        Bench b("dir-" + word, true);
        ToLayout(b);
        Revise(b, "put ward 2 " + word + " of the market", json::array({{{"op", "move"}, {"node", "ward-2"}, {"rel", json::array({{{"type", type}, {"to", "market"}}})}}}),
               json::array({{{"node", "ward-2"}, {"type", type}, {"to", "market"}}}));
        const json solved = b.build->Solved();
        const int w = solved["grid"]["bounds"][2].get<int>();
        const auto land = ZoneLand(solved);
        const auto [wx, wy] = Middle(land.at("ward-2"), w);
        const auto [mx, my] = Middle(land.at("market"), w);
        if (word == "north") EXPECT_LT(wy, my);
        if (word == "south") EXPECT_GT(wy, my);
        if (word == "east") EXPECT_GT(wx, mx);
        if (word == "west") EXPECT_LT(wx, mx);
    }
}

// ③ Plan: ten more cottages asked for are all there; the other zones are untouched.
TEST(PlaceFlowTest, PlanReviseAddsWhatWasAskedAndNothingElse) {
    Bench b("add", true);
    ToLayout(b);
    ToPlanned(b);
    const json before = b.build->Solved();
    const int houses = Buildings(before, "ward-1");
    const auto market = ThingsIn(before, "market"), other = ThingsIn(before, "ward-2");
    Revise(b, "add 10 more cottages to ward 1",
           json::array({{{"op", "zone"}, {"node", "ward-1"},
                         {"children", json::array({{{"id", "cottages"}, {"module", "house-plot"}, {"repeat", houses + 10}, {"quality", {{"house-kind", "cottage"}}}},
                                                   {{"id", "well"}, {"module", "well-plaza"}}})}}}));
    const json after = b.build->Solved();
    EXPECT_GE(Buildings(after, "ward-1"), houses + 10) << b.build->State()["revisions"].back().dump();
    EXPECT_EQ(ThingsIn(after, "market"), market);
    EXPECT_EQ(ThingsIn(after, "ward-2"), other);
}

// ③ Plan: a thing taken out goes, and only it.
TEST(PlaceFlowTest, PlanReviseRemovesOnlyWhatWasNamed) {
    Bench b("remove", true);
    ToLayout(b);
    ToPlanned(b);
    const json before = b.build->Solved();
    auto rest = ThingsIn(before, "ward-1");
    std::erase_if(rest, [](const std::string& t) { return t.find("/well") != std::string::npos; });
    const auto market = ThingsIn(before, "market");
    Revise(b, "remove the well from ward 1", json::array({{{"op", "remove"}, {"node", "ward-1/well"}}}));
    const json after = b.build->Solved();
    for (const auto& t : ThingsIn(after, "ward-1")) EXPECT_EQ(t.find("/well"), std::string::npos) << t;
    auto kept = ThingsIn(after, "ward-1");
    EXPECT_EQ(kept, rest);
    EXPECT_EQ(ThingsIn(after, "market"), market);
}

// ③ Plan: one parcel moved; the other zones stay exactly as they were.
TEST(PlaceFlowTest, PlanReviseMovesOneThingAndLeavesTheRest) {
    Bench b("move", true);
    ToLayout(b);
    ToPlanned(b);
    const json before = b.build->Solved();
    const auto market = ThingsIn(before, "market"), other = ThingsIn(before, "ward-2");
    Revise(b, "move the well of ward 1 to the north of ward 1",
           json::array({{{"op", "set"}, {"node", "ward-1/well"}, {"rel", json::array({{{"type", "in-north"}, {"to", "ward-1"}}})}}}));
    const json after = b.build->Solved();
    EXPECT_EQ(ThingsIn(after, "market"), market);
    EXPECT_EQ(ThingsIn(after, "ward-2"), other);
}
