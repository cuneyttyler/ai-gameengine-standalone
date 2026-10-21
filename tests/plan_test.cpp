#include "editing/PlaceBuild.h"
#include "places/Brief.h"
#include "places/PlaceValidate.h"
#include "places/Planner.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>

// development_plan.md 7.5: Plan (the Spec).
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

std::vector<PlaceProblem> Errors(const std::vector<PlaceProblem>& problems) {
    std::vector<PlaceProblem> out;
    for (const auto& p : problems) {
        if (!p.warning) out.push_back(p);
    }
    return out;
}

json BriefFor(const std::string& cls) {
    return CompleteBrief(Library(), {{"buildType", "settlement"}, {"name", "Riverford"}, {"scale", {{"class", cls}}},
                                     {"shape", {{"layout", {{"mix", {{"grid", 0.4}, {"organic", 0.6}}}}}}},
                                     {"walled", cls == "town" || cls == "city" ? "stone" : "none"},
                                     {"components", {{"residential-ward", json::object()},
                                                     {"market-ward", json::object()},
                                                     {"garden", {{"density", "common"}, {"params", {{"kind", "flower"}}}}}}}});
}

// What a model would answer at the root: a market and some residential wards.
json RootAnswer(int wards, int area) {
    json children = json::array({{{"id", "Market"}, {"module", "market-ward"},
                                  {"quantity", {{"plots", {{"count", 6}}}, {"area", {{"area", area / 8}}}}},
                                  {"quality", {{"bustle", "busy"}}},
                                  {"rationale", "the centre, on the main road"}}});
    for (int i = 0; i < wards; ++i) {
        children.push_back({{"id", "Ward " + std::to_string(i + 1)}, {"module", "residential-ward"},
                            {"quantity", {{"plots", {{"count", 20}}}, {"area", {{"area", area / (2 * wards)}}}}},
                            {"quality", {{"wealth", "common"}, {"street-style", i % 2 ? "grid" : "organic"}}},
                            {"rel", json::array({{{"type", "near"}, {"to", "Market"}}})}});
    }
    children.push_back({{"id", "main road"}, {"module", "road"}, {"quality", {{"class", "main"}}}});
    children.push_back({{"id", "spaceport"}, {"module", "spaceport"}}); // not a module: dropped
    return {{"quantity", {{"buildings", {{"count", 1}}}}}, {"children", children}, {"rationale", "a market centre with wards around it"}};
}

json WardAnswer(int houses) {
    return {{"quantity", {{"plots", {{"count", houses}}}}},
            {"children", json::array({{{"id", "cottages"}, {"module", "house-plot"}, {"repeat", houses},
                                       {"quality", {{"house-kind", "cottage"}}}},
                                      {{"id", "well"}, {"module", "well-plaza"}}})}};
}

json MarketAnswer() {
    return {{"children", json::array({{{"id", "square"}, {"module", "market-square"}, {"quantity", {{"stalls", {{"count", 8}}}}}},
                                      {{"id", "shops"}, {"module", "shop-plot"}, {"repeat", 4}}})}};
}

json PlanFor(const std::string& cls, const json& brief) {
    const BuildType* build = Library().Build("settlement");
    const ScaleClass* scale = build->Scale(cls);
    const int wards = std::max(1, (scale->zones.first + scale->zones.second) / 2 - 1);
    const int area = (scale->side.first + scale->side.second) * (scale->side.first + scale->side.second) / 4;
    json spec = SpecFromRoot(Library(), brief, RootAnswer(wards, area), "riverford");
    for (const auto& zone : ZoneIds(Library(), spec)) {
        const json* node = SpecNode(spec, zone);
        ApplyZoneAnswer(Library(), brief, spec, zone, (*node)["module"] == "market-ward" ? MarketAnswer() : WardAnswer(20));
    }
    ExpandSpec(Library(), brief, spec);
    RebalanceBuildings(Library(), brief, spec);
    return spec;
}

} // namespace

TEST(PlanTest, SpecsForEverySettlementScaleValidate) {
    for (const char* cls : {"hamlet", "village", "large-village", "town", "city"}) {
        const json brief = BriefFor(cls);
        const json spec = PlanFor(cls, brief);
        const auto errors = Errors(ValidateSpec(Library(), spec, SpecStage::Final, "settlement"));
        EXPECT_TRUE(errors.empty()) << cls << ":\n" << Describe(errors);
        // The buildings add up to the Brief's range, and the root says so.
        const auto range = BriefValue(brief, "scale.buildings.count");
        const int count = CountBuildings(Library(), spec);
        EXPECT_GE(count, range[0].get<int>()) << cls;
        EXPECT_LE(count, range[1].get<int>()) << cls;
        EXPECT_EQ(BriefValue(spec["root"], "quantity.buildings.count"), count) << cls;
    }
}

TEST(PlanTest, TheRootAnswerIsCheckedAndCompleted) {
    json brief = BriefFor("town"); // walled: stone
    brief["components"]["slum"] = {{"enabled", false}};
    json answer = RootAnswer(3, 40000);
    answer["children"].push_back({{"id", "slum"}, {"module", "slum"}}); // turned off in the Brief
    const json spec = SpecFromRoot(Library(), brief, answer, "riverford");
    std::set<std::string> modules, ids;
    for (const auto& c : spec["root"]["children"]) {
        modules.insert(c["module"]);
        EXPECT_TRUE(ids.insert(c["id"].get<std::string>()).second) << c["id"];
    }
    EXPECT_FALSE(modules.contains("spaceport"));
    EXPECT_FALSE(modules.contains("slum"));
    EXPECT_TRUE(modules.contains("wall"));   // walled: added
    EXPECT_TRUE(modules.contains("ground")); // the root needs it
    // Ids made safe, relations follow them.
    const json* ward = SpecNode(const_cast<json&>(spec), "ward-1");
    ASSERT_NE(ward, nullptr);
    EXPECT_EQ((*ward)["rel"][0]["to"], "market");
    // The Brief's choices win over the answer's.
    EXPECT_EQ(BriefValue(spec["root"], "quality.layout"), json({{"mix", {{"grid", 0.4}, {"organic", 0.6}}}}));
    EXPECT_EQ(BriefValue(spec["root"], "quality.walled"), "stone");
    EXPECT_NE(BriefValue(spec["root"], "quantity.buildings.count"), 1); // the answer's 1 is outside the Brief's range
}

TEST(PlanTest, ExpansionRulesFillTheLowerLevels) {
    const json brief = BriefFor("village");
    const json spec = PlanFor("village", brief);
    const json* plots = SpecNode(const_cast<json&>(spec), "ward-1/cottages");
    ASSERT_NE(plots, nullptr);
    std::map<std::string, json> byModule;
    for (const auto& c : (*plots)["children"]) byModule[c["module"]] = c;
    // Gardens are "common" in the Brief: 60% of plots, flower gardens as asked.
    ASSERT_TRUE(byModule.contains("garden"));
    EXPECT_DOUBLE_EQ(byModule["garden"]["share"].get<double>(), 0.6);
    EXPECT_EQ(byModule["garden"]["quality"]["kind"], "flower");
    EXPECT_TRUE(byModule.contains("front-yard")); // the module's typical children
    EXPECT_FALSE(byModule.contains("fence-line")); // only when fenced
    // A flower garden has no vegetable beds; dressing waits for the compile step.
    bool beds = false, dressing = false;
    for (const auto& c : byModule["garden"]["children"]) {
        beds = beds || c["module"] == "vegetable-bed";
    }
    for (const auto& c : (*plots)["children"]) dressing = dressing || c["module"] == "clutter";
    EXPECT_FALSE(beds);
    EXPECT_FALSE(dressing);
    // The market square the market ward must have is there.
    const json* market = SpecNode(const_cast<json&>(spec), "market/square");
    ASSERT_NE(market, nullptr);
}

TEST(PlanTest, BuildingCountsAreRebalancedToTheBrief) {
    const json brief = BriefFor("village"); // 15-60
    json spec = SpecFromRoot(Library(), brief, RootAnswer(2, 6400), "riverford");
    for (const auto& zone : ZoneIds(Library(), spec)) {
        ApplyZoneAnswer(Library(), brief, spec, zone, SpecNode(spec, zone)->at("module") == "market-ward" ? MarketAnswer() : WardAnswer(90));
    }
    EXPECT_GT(CountBuildings(Library(), spec), 60);
    const int after = RebalanceBuildings(Library(), brief, spec);
    EXPECT_GE(after, 15);
    EXPECT_LE(after, 60);
    // Each ward's plots follow its parcels.
    const json* ward = SpecNode(spec, "ward-1");
    int parcels = 0;
    for (const auto& c : (*ward)["children"]) {
        if (Library().IsA(c["module"], "BuildingParcel")) parcels += c.value("repeat", 1);
    }
    EXPECT_EQ(BriefValue(*ward, "quantity.plots.count"), parcels);
}

TEST(PlanTest, TheSketchFillsTheSiteByArea) {
    const json brief = BriefFor("large-village");
    const json spec = PlanFor("large-village", brief);
    const json sketch = PlanSketch(Library(), spec, 135);
    ASSERT_GE(sketch["rects"].size(), 3u);
    double total = 0;
    for (const auto& r : sketch["rects"]) {
        EXPECT_GE(r["x"].get<double>(), 0);
        EXPECT_GE(r["y"].get<double>(), 0);
        EXPECT_LE(r["x"].get<double>() + r["w"].get<double>(), 135.5);
        EXPECT_LE(r["y"].get<double>() + r["h"].get<double>(), 135.5);
        total += r["w"].get<double>() * r["h"].get<double>();
    }
    EXPECT_NEAR(total, 0.85 * 135 * 135, 0.05 * 135 * 135);
    const json summary = PlanSummary(Library(), spec);
    EXPECT_EQ(summary["totals"]["buildings"], CountBuildings(Library(), spec));
    EXPECT_FALSE(summary["rows"][0]["rationale"].get<std::string>().empty());
}

TEST(PlanTest, ABuildIsPlannedAndABrokenNodeRepaired) {
    const fs::path root = fs::temp_directory_path() / "aige_plan_flow";
    fs::remove_all(root);
    fs::create_directories(root);
    std::vector<std::pair<std::string, json>> sent;
    PlaceBuild::Host host;
    host.ontology = []() -> const Ontology& { return Library(); };
    host.catalog = [] { return Catalog{}; };
    host.isoGrid = [] { return true; };
    host.request = [&](const std::string& path, const json& body, int) {
        sent.push_back({path, body});
        return static_cast<int>(sent.size());
    };
    host.projectRoot = root;
    PlaceBuild build(host);
    ASSERT_EQ(build.Start("a village by a river"), "");
    json draft = {{"reading", "typical"}, {"brief", BriefFor("village")}};
    build.OnResponse(1, {{"ok", true}, {"drafts", json::array({draft})}});
    // The other two readings: no answer (no model); the one draft decides.
    for (int i : {2, 3}) build.OnResponse(i, {{"ok", false}, {"errors", json::array({"No AI provider configured"})}});
    ASSERT_EQ(build.Stage(), "request");
    ASSERT_EQ(build.Step("assets"), "");
    ASSERT_EQ(build.Confirm(), "");
    EXPECT_EQ(build.Stage(), "planning");
    ASSERT_EQ(sent.back().first, "/places/plan-root");
    EXPECT_TRUE(sent.back().second["context"]["modules"].size() > 3);

    // The root answer has a ward with a wealth that doesn't exist (dropped, no
    // AI needed) and a market with too many plots: that one is repaired once.
    json answer = RootAnswer(2, 6400);
    answer["children"][1]["quality"]["wealth"] = "golden";
    answer["children"][0]["quantity"]["plots"]["count"] = 5000;
    build.OnResponse(static_cast<int>(sent.size()), {{"ok", true}, {"answer", answer}, {"thinking", "two wards and a market"}});
    ASSERT_EQ(sent.back().first, "/places/repair-node");
    EXPECT_EQ(sent.back().second["node"]["id"], "market");
    EXPECT_EQ(sent.back().second["problems"][0]["where"], "market.plots");
    EXPECT_FALSE(sent.back().second["node"].contains("children"));
    build.OnResponse(static_cast<int>(sent.size()), {{"ok", true}, {"answer", {{"quantity", {{"plots", {{"count", 8}}}}}}}});

    // Then a step per zone.
    std::vector<std::pair<int, std::string>> zones;
    for (int i = 0; i < static_cast<int>(sent.size()); ++i) {
        if (sent[i].first == "/places/plan-zone") zones.push_back({i + 1, sent[i].second["zone"]["module"]});
    }
    ASSERT_EQ(zones.size(), 3u); // the market and two wards
    EXPECT_TRUE(sent[zones[0].first - 1].second.contains("siblings"));
    for (const auto& [id, module] : zones) {
        EXPECT_EQ(build.Stage(), "planning");
        build.OnResponse(id, {{"ok", true}, {"answer", module == "market-ward" ? MarketAnswer() : WardAnswer(12)}});
    }
    ASSERT_EQ(build.Stage(), "plan") << build.State().dump();
    json spec = build.Spec();
    EXPECT_NE(BriefValue(*SpecNode(spec, "ward-1"), "quality.wealth"), "golden");
    EXPECT_LT(BriefValue(*SpecNode(spec, "market"), "quantity.plots.count").get<int>(), 100);
    EXPECT_TRUE(Errors(build.PlanProblems()).empty()) << Describe(build.PlanProblems());
    const json state = build.State();
    EXPECT_TRUE(state.contains("plan"));
    EXPECT_TRUE(state.contains("sketch"));
    EXPECT_TRUE(fs::exists(build.Folder() / "spec.json"));
    EXPECT_EQ(build.Approve(), "");
    // Solved, then the AI looks at the map.
    ASSERT_EQ(sent.back().first, "/places/review-layout");
    build.OnResponse(static_cast<int>(sent.size()), {{"ok", true}, {"answer", {{"summary", "Looks right."}, {"changes", json::array()}}}});
    EXPECT_EQ(build.Stage(), "planned");
    // Solved, with its map.
    EXPECT_TRUE(fs::exists(build.Folder() / "layout.solved.json"));
    EXPECT_TRUE(fs::exists(build.Folder() / "layout.png"));
    EXPECT_TRUE(build.State().contains("image"));
    fs::remove_all(root);
}
