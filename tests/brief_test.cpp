#include "assets/ArtAnalysis.h"
#include "assets/Catalog.h"
#include "editing/BuildWindow.h"
#include "editing/PlaceBuild.h"
#include "places/Brief.h"
#include "places/PlaceValidate.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

// development_plan.md 7.4: Interpret and Confirm.
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

json Draft(const std::string& reading, const std::string& cls, json count, bool garden, const std::string& density,
           json confidence = json::object()) {
    json components = {{"residential-ward", {{"enabled", true}, {"count", {1, 2}}}}, {"market-square", {{"enabled", true}, {"count", 1}}}};
    if (garden) components["garden"] = {{"enabled", true}, {"density", density}};
    return {{"reading", reading},
            {"brief", {{"schema", "aige.brief/1"}, {"buildType", "settlement"}, {"name", "Riverford"},
                       {"scale", {{"class", cls}, {"buildings", {{"count", count}}}}},
                       {"style", {{"biome", "temperate"}, {"condition", "lived-in"}}},
                       {"shape", {{"layout", "organic"}}},
                       {"features", json::array({{{"module", "river"}}})},
                       {"components", components}}},
            {"confidence", confidence},
            {"assumptions", json::array({"no walls: the request didn't mention them"})},
            {"questions", json::array()}};
}

bool HasError(const std::vector<PlaceProblem>& problems) {
    return std::any_of(problems.begin(), problems.end(), [](const PlaceProblem& p) { return !p.warning; });
}

std::string Describe(const std::vector<PlaceProblem>& problems) {
    std::string out;
    for (const auto& p : problems) out += "  " + p.where + ": " + p.message + "\n";
    return out;
}

} // namespace

TEST(BriefTest, InterpretContextDescribesWhatCanBeBuilt) {
    const json context = InterpretContext(Library());
    // The settlement, and the other build types (7.10): region, castle, interior, courtyard, dungeon, farmland, wilderness, camp.
    ASSERT_EQ(context["buildTypes"].size(), 10u); // and the modern city
    json settlement;
    for (const auto& b : context["buildTypes"]) {
        if (b["id"] == "settlement") settlement = b;
    }
    ASSERT_TRUE(settlement.is_object());
    EXPECT_EQ(settlement["id"], "settlement");
    EXPECT_EQ(settlement["scale"].size(), 5u);
    const auto ward = std::find_if(settlement["components"].begin(), settlement["components"].end(),
                                   [](const json& c) { return c["id"] == "residential-ward"; });
    ASSERT_NE(ward, settlement["components"].end());
    EXPECT_TRUE((*ward)["fields"].contains("wealth"));            // asked
    EXPECT_FALSE((*ward)["fields"].contains("footprint-density")); // not asked
    EXPECT_EQ((*ward)["fields"]["wealth"]["values"], json::array({"poor", "common", "rich"}));
    EXPECT_TRUE(context["vocabularies"].contains("biome"));
    EXPECT_TRUE(settlement["rootFields"].contains("buildings"));
}

TEST(BriefTest, BuildRequestsAreRecognised) {
    EXPECT_TRUE(LooksLikeBuildRequest(Library(), "Build a relatively large village with roads, trees, houses etc"));
    EXPECT_TRUE(LooksLikeBuildRequest(Library(), "a small hamlet with a well"));
    EXPECT_TRUE(LooksLikeBuildRequest(Library(), "Make me a walled town by the coast"));
    EXPECT_FALSE(LooksLikeBuildRequest(Library(), "make the skeletons tougher"));
    EXPECT_FALSE(LooksLikeBuildRequest(Library(), "move the village well two cells left"));
    EXPECT_FALSE(LooksLikeBuildRequest(Library(), "add a quest about the missing sheep"));
    // A long message that only mentions places is not a build.
    EXPECT_FALSE(LooksLikeBuildRequest(Library(), "This part looks bad, can you revise it?\n\nIII. METHOD Every build runs the same "
                                                  "procedure. Use generated png for a big village layout and final generated "
                                                  "content for a small village"));
    EXPECT_TRUE(LooksLikeBuildRequest(Library(), "build a large village by river with several sections (marketplace, accodomations etc)"));
    EXPECT_TRUE(LooksLikeBuildRequest(Library(), "Build a walled town. It should have a market, a temple and lots of gardens around the houses, and a river through the middle with two bridges."));
}

TEST(BriefTest, VagueQueryDraftsDisagreeAndAreMarked) {
    // "a village by a river": three readings.
    const BriefMerge merge = MergeBriefs({Draft("literal", "village", json::array({15, 20}), false, ""),
                                          Draft("typical", "village", json::array({25, 35}), true, "common",
                                                {{"components.garden.density", 0.4}}),
                                          Draft("generous", "large-village", json::array({60, 80}), true, "abundant")});
    const BriefField* cls = merge.Field("scale.class");
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->value, "village");
    EXPECT_TRUE(cls->disputed);
    ASSERT_EQ(cls->alternatives.size(), 1u);
    EXPECT_EQ(cls->alternatives[0], "large-village");
    EXPECT_EQ(cls->readings[0], "generous");
    // Three different numbers: the typical reading's.
    EXPECT_EQ(BriefValue(merge.brief, "scale.buildings.count"), json::array({25, 35}));
    // Gardens: on in two of three.
    EXPECT_EQ(BriefValue(merge.brief, "components.garden.enabled"), true);
    EXPECT_TRUE(merge.Field("components.garden.enabled")->disputed);
    const BriefField* density = merge.Field("components.garden.density");
    ASSERT_NE(density, nullptr);
    EXPECT_EQ(density->value, "common");
    EXPECT_TRUE(density->disputed);
    EXPECT_TRUE(density->low()); // the typical reading guessed it
    // What they agreed on isn't marked.
    EXPECT_FALSE(merge.Field("shape.layout")->disputed);
    EXPECT_EQ(merge.assumptions.size(), 1u);
}

TEST(BriefTest, DetailedQueryDraftsAgree) {
    // "a village of 25 to 35 buildings by a river, with common gardens and a market square"
    const json d = Draft("x", "village", json::array({25, 35}), true, "common");
    json literal = d, typical = d, generous = d;
    literal["reading"] = "literal";
    typical["reading"] = "typical";
    generous["reading"] = "generous";
    const BriefMerge merge = MergeBriefs({literal, typical, generous});
    for (const auto& f : merge.fields) EXPECT_FALSE(f.disputed) << f.path;
    const json brief = CompleteBrief(Library(), merge.brief);
    const auto problems = ValidateBrief(Library(), brief);
    EXPECT_FALSE(HasError(problems)) << Describe(problems);
}

TEST(BriefTest, CompletionFollowsTheScaleTableAndTheProjectsAnswers) {
    json brief = {{"buildType", "settlement"}, {"scale", {{"class", "town"}}},
                  {"components", {{"garden", json::object()}, {"wall", {{"enabled", false}, {"count", 3}}}}}};
    const json defaults = {{"settlement", {{"components", {{"garden", {{"assets", {{"source", "generate"}}}}}}},
                                           {"style", {{"biome", "coast"}}}}}};
    brief = CompleteBrief(Library(), brief, defaults);
    EXPECT_EQ(brief["schema"], "aige.brief/1");
    EXPECT_EQ(BriefValue(brief, "scale.buildings.count"), json::array({150, 500}));
    EXPECT_EQ(BriefValue(brief, "scale.area.area"), json::array({180 * 180, 300 * 300}));
    EXPECT_EQ(BriefValue(brief, "components.garden.assets.source"), "generate"); // the earlier answer
    EXPECT_EQ(BriefValue(brief, "components.wall"), json({{"enabled", false}}));  // nothing but off
    EXPECT_EQ(BriefValue(brief, "style.biome"), "coast");

    // The numbers win: 200 buildings make a town of a "village".
    json numbers = {{"buildType", "settlement"}, {"scale", {{"class", "village"}, {"buildings", {{"count", {180, 220}}}}}}};
    numbers = CompleteBrief(Library(), numbers);
    EXPECT_EQ(BriefValue(numbers, "scale.class"), "town");
    // A class picked in the window brings its numbers.
    SetBriefValue(numbers, "scale.class", "hamlet");
    FollowScale(Library(), numbers, "scale.class");
    EXPECT_EQ(BriefValue(numbers, "scale.buildings.count"), json::array({5, 15}));

    // The confirmed answers become the next build's defaults.
    const json after = DefaultsAfter(brief, json::object());
    EXPECT_EQ(after["settlement"]["components"]["garden"]["assets"]["source"], "generate");
}

TEST(BriefTest, WindowOutputValidatesAsABrief) {
    const BriefMerge merge = MergeBriefs({Draft("literal", "village", json::array({15, 20}), false, ""),
                                          Draft("typical", "village", json::array({25, 35}), true, "common"),
                                          Draft("generous", "large-village", json::array({60, 80}), true, "abundant")});
    json brief = CompleteBrief(Library(), merge.brief);
    const json round1 = BriefWindow(Library(), brief, &merge, nullptr, true, 1);
    EXPECT_TRUE(round1["marks"].contains("scale.class"));
    EXPECT_TRUE(round1["marks"]["scale.class"]["disputed"].get<bool>());
    EXPECT_EQ(round1["size"]["classes"].size(), 5u);
    EXPECT_GE(round1["components"].size(), 20u); // every component the build type allows
    EXPECT_EQ(round1["components"][0]["inBrief"], true);
    EXPECT_TRUE(round1["problems"].empty()) << round1["problems"].dump();

    // The user's edits, as the window sends them.
    ApplyWindowEdit(Library(), brief, "scale.buildings", {{"count", {40, 50}}});
    ApplyWindowEdit(Library(), brief, "components.wall.enabled", true);
    ApplyWindowEdit(Library(), brief, "components.wall.params.material", "stone");
    ApplyWindowEdit(Library(), brief, "components.garden.params.kind", "flower");
    ApplyWindowEdit(Library(), brief, "components.garden.params.flower-mix", {{"mix", {{"rose", 0.6}, {"tulip", 0.4}}}});
    ApplyWindowEdit(Library(), brief, "components.market-square.enabled", false);
    ApplyWindowEdit(Library(), brief, "features", json::array({"river", "lake"}));
    ApplyWindowEdit(Library(), brief, "walled", "stone");
    EXPECT_EQ(BriefValue(brief, "scale.class"), "village");
    EXPECT_EQ(BriefValue(brief, "components.market-square"), json({{"enabled", false}}));
    EXPECT_EQ(BriefValue(brief, "components.wall.assets.source"), "auto");
    auto problems = ValidateBrief(Library(), brief);
    EXPECT_FALSE(HasError(problems)) << Describe(problems);

    const json round2 = BriefWindow(Library(), brief, &merge, nullptr, true, 2);
    bool asked = false;
    for (const auto& c : round2["components"]) {
        if (c["id"] != "garden") continue;
        for (const auto& p : c["params"]) asked = asked || p["name"] == "flower-mix"; // shown now that kind is flower
    }
    EXPECT_TRUE(asked);

    // And a bad edit is reported against its field.
    ApplyWindowEdit(Library(), brief, "components.garden.params.kind", "cactus");
    problems = ValidateBrief(Library(), brief);
    ASSERT_TRUE(HasError(problems));
    EXPECT_EQ(problems.front().where, "components.garden.params.kind");
}

TEST(BriefTest, AssetPlanProposesSources) {
    // A project with a cottage and nothing for flowers.
    ImageData house;
    house.width = house.height = 128;
    house.rgba.assign(128 * 128 * 4, 0);
    for (int y = 40; y < 110; ++y) {
        for (int x = 24; x < 104; ++x) {
            uint8_t* p = &house.rgba[(y * 128 + x) * 4];
            p[0] = 200;
            p[1] = 170;
            p[2] = 120;
            p[3] = 255;
        }
    }
    AssetInfo cottage;
    cottage.path = "assets/town/cottage.png";
    cottage.width = cottage.height = 128;
    cottage.analysis = {{kShapeKey, FormatShape(AnalyzeShape(house))}, {"kind", "building"}, {"tags", "house, cottage"},
                        {"roles", "house"}};
    CatalogOptions options;
    options.isoGrid = false;
    options.pixelsPerCell = [](const std::string&) { return 32.0f; };
    Catalog catalog;
    catalog.entries = CatalogEntriesFor(cottage, &Library(), options);

    json brief = CompleteBrief(Library(), {{"buildType", "settlement"}, {"scale", {{"class", "village"}}},
                                           {"components", {{"residential-ward", json::object()},
                                                           {"garden", {{"params", {{"kind", "flower"}}}}}}}});
    const auto needs = AssetNeeds(Library(), brief);
    auto find = [&](const std::string& role) {
        return std::find_if(needs.begin(), needs.end(), [&](const AssetNeed& n) { return n.role == role; });
    };
    ASSERT_NE(find("house"), needs.end()); // from the ward's house plots
    ASSERT_NE(find("flower"), needs.end()); // the garden's plants, now that it's a flower garden
    EXPECT_EQ(find("crop"), needs.end());  // not a vegetable garden

    const json plan = AssetPlan(Library(), catalog, brief, false);
    bool house_ = false, flower = false;
    for (const auto& row : plan["rows"]) {
        if (row["role"] == "house") {
            house_ = true;
            EXPECT_EQ(row["source"], "project");
            EXPECT_EQ(row["candidates"][0]["id"], "assets/town/cottage.png");
        }
        if (row["role"] == "flower") {
            flower = true;
            EXPECT_EQ(row["source"], "generate");
        }
    }
    EXPECT_TRUE(house_ && flower);
    EXPECT_GT(plan["summary"]["generate"].get<int>(), 0);
}

TEST(BriefTest, ABuildFromRequestToConfirmedBrief) {
    const fs::path root = fs::temp_directory_path() / "aige_build_flow";
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
    ASSERT_EQ(build.Start("Build a village by a river"), "");
    EXPECT_EQ(build.Id(), "village-by-river");
    EXPECT_EQ(build.Stage(), "interpreting");
    // The three readings, a request each (literal, typical, generous), side by side.
    ASSERT_EQ(sent.size(), 3u);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(sent[i].first, "/places/interpret");
        EXPECT_EQ(sent[i].second["drafts"], 1);
    }
    EXPECT_EQ(sent[0].second["reading"], "literal");
    EXPECT_EQ(sent[2].second["reading"], "generous");

    // One draft names a component that doesn't exist: it's repaired once.
    json bad = Draft("generous", "village", json::array({30, 40}), true, "common");
    bad["brief"]["components"]["walls"] = {{"enabled", true}};
    json drafts = json::array({Draft("literal", "village", json::array({20, 30}), true, "some"),
                               Draft("typical", "village", json::array({30, 40}), true, "common"), bad});
    for (auto& d : drafts) {
        d["thinking"] = "a village by a river: twenty to forty houses";
        d["mode"] = "two-pass";
    }
    for (int i = 0; i < 3; ++i) build.OnResponse(i + 1, {{"ok", true}, {"drafts", json::array({drafts[i]})}});
    ASSERT_EQ(sent.size(), 4u);
    EXPECT_EQ(sent[3].first, "/places/repair-brief");
    EXPECT_EQ(sent[3].second["problems"][0]["where"], "components.walls");
    EXPECT_EQ(build.Stage(), "interpreting");
    json fixed = bad;
    fixed["brief"]["components"].erase("walls");
    fixed["brief"]["components"]["wall"] = {{"enabled", true}};
    build.OnResponse(4, {{"ok", true}, {"brief", fixed["brief"]}});
    EXPECT_EQ(build.Stage(), "request");
    EXPECT_EQ(BriefValue(build.Brief(), "scale.class"), "village");

    const json state = build.State();
    EXPECT_EQ(state["window"]["round"], 1);
    EXPECT_EQ(build.Card()["text"], "① Request: confirm what to build");

    EXPECT_EQ(build.Step("assets"), "");
    EXPECT_EQ(build.State()["window"]["round"], 2);
    EXPECT_TRUE(build.State()["window"].contains("assetPlan"));
    EXPECT_EQ(build.Confirm(), "");
    EXPECT_EQ(build.Stage(), "planning");
    EXPECT_TRUE(fs::exists(root / "places" / "village-by-river" / "brief.json"));
    EXPECT_TRUE(fs::exists(root / "places" / "defaults.json"));

    // Every call is in the log, thinking included.
    std::ifstream log(root / "places" / "village-by-river" / "log.jsonl");
    std::string line, all;
    int lines = 0;
    while (std::getline(log, line)) {
        all += line;
        ++lines;
    }
    EXPECT_GE(lines, 6); // start, three drafts, a repair, merged, confirmed
    EXPECT_NE(all.find("twenty to forty houses"), std::string::npos);
    EXPECT_NE(all.find("\"step\":\"repair\""), std::string::npos);

    // The next build of the same request gets its own folder.
    log.close();
    EXPECT_EQ(PlaceIdFor(root, "Build a village by a river"), "village-by-river-2");
    fs::remove_all(root);
}

TEST(BriefTest, NoAiStillABrief) {
    PlaceBuild::Host host;
    host.ontology = []() -> const Ontology& { return Library(); };
    host.request = [](const std::string&, const json&, int) { return 0; };
    host.projectRoot = fs::temp_directory_path() / "aige_build_none";
    fs::remove_all(host.projectRoot);
    PlaceBuild build(host);
    EXPECT_EQ(build.Start("a village"), "");
    EXPECT_EQ(build.Stage(), "request"); // from the request's words, and said so
    EXPECT_EQ(BriefValue(build.Brief(), "scale.class"), "village");
    EXPECT_FALSE(build.Notices().empty());
    EXPECT_NE(build.Start("  "), "");
    fs::remove_all(host.projectRoot);
}


TEST(BriefTest, LayoutCanCombinePatterns) {
    json brief = CompleteBrief(Library(), {{"buildType", "settlement"}, {"scale", {{"class", "village"}}},
                                           {"shape", {{"layout", "organic"}}}, {"components", json::object()}});
    EXPECT_FALSE(HasError(ValidateBrief(Library(), brief)));
    EXPECT_EQ(BriefWindow(Library(), brief, nullptr, nullptr, true, 1)["layout"]["value"], json({{"organic", 1.0}}));

    // A grid core with organic outskirts, as the window sends it.
    ApplyWindowEdit(Library(), brief, "shape.layout", {{"grid", 0.4}, {"organic", 0.6}});
    EXPECT_EQ(BriefValue(brief, "shape.layout"), json({{"mix", {{"grid", 0.4}, {"organic", 0.6}}}}));
    EXPECT_FALSE(HasError(ValidateBrief(Library(), brief)));
    // Back to one pattern: stored as just its name.
    ApplyWindowEdit(Library(), brief, "shape.layout", {{"grid", 1.0}, {"organic", 0.0}});
    EXPECT_EQ(BriefValue(brief, "shape.layout"), "grid");

    SetBriefValue(brief, "shape.layout", json{{"mix", {{"grid", 0.4}, {"organic", 0.4}}}});
    auto problems = ValidateBrief(Library(), brief);
    ASSERT_TRUE(HasError(problems));
    EXPECT_EQ(problems.front().where, "shape.layout");
    EXPECT_NE(problems.front().message.find("add up to 1"), std::string::npos);
    SetBriefValue(brief, "shape.layout", "spiral");
    EXPECT_TRUE(HasError(ValidateBrief(Library(), brief)));
}
