#include "editing/PlaceBuild.h"
#include "places/Brief.h"
#include "places/PlaceValidate.h"
#include "places/Planner.h"
#include "places/Where.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <iostream>
#include <set>

// A build handles its own failures (retries, fallbacks, fixes that need no
// AI), never leaves a plan unapprovable, and can be reopened later.
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

// A project folder and a fake AI service: `on` false, it's unavailable.
struct Fixture {
    fs::path root;
    bool on = true;
    std::vector<std::pair<std::string, json>> sent;
    std::set<int> answered;

    explicit Fixture(const std::string& name) : root(fs::temp_directory_path() / name) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Fixture() { fs::remove_all(root); }

    PlaceBuild::Host Host() {
        PlaceBuild::Host host;
        host.ontology = []() -> const Ontology& { return Library(); };
        host.catalog = [] { return Catalog{}; };
        host.isoGrid = [] { return true; };
        host.request = [this](const std::string& path, const json& body, int) {
            if (!on) return 0;
            sent.push_back({path, body});
            return static_cast<int>(sent.size());
        };
        host.projectRoot = root;
        return host;
    }
    // Answers every request not answered yet with `body`, until none is left.
    void AnswerAll(PlaceBuild& build, const json& body) {
        for (int guard = 0; guard < 200; ++guard) {
            int next = 0;
            for (int i = 1; i <= static_cast<int>(sent.size()); ++i) {
                if (!answered.contains(i)) {
                    next = i;
                    break;
                }
            }
            if (next == 0) return;
            answered.insert(next);
            build.OnResponse(next, body);
        }
    }
    int Count(const std::string& path) const {
        int n = 0;
        for (const auto& [p, b] : sent) n += p == path;
        return n;
    }
};

} // namespace

TEST(BuildRobustTest, WithTheAiOffABuildStillRunsToItsLayout) {
    Fixture f("aige_build_offline");
    f.on = false;
    PlaceBuild build(f.Host());
    ASSERT_EQ(build.Start("Build a large village by a river with gardens and a market"), "");
    ASSERT_EQ(build.Stage(), "request");
    EXPECT_FALSE(build.Notices().empty());
    EXPECT_EQ(BriefValue(build.Brief(), "scale.class"), "large-village");
    EXPECT_TRUE(build.Brief().value("features", json::array()).dump().find("river") != std::string::npos) << build.Brief().dump();
    EXPECT_TRUE(BriefValue(build.Brief(), "components.garden.enabled") == true) << build.Brief().dump();

    ASSERT_EQ(build.Step("assets"), "");
    ASSERT_EQ(build.Confirm(), "");
    ASSERT_EQ(build.Stage(), "plan") << build.State().dump();
    EXPECT_TRUE(Errors(build.PlanProblems()).empty()) << Describe(build.PlanProblems());
    EXPECT_GE(ZoneIds(Library(), build.Spec()).size(), 2u);
    EXPECT_GT(CountBuildings(Library(), build.Spec()), 20);
    ASSERT_EQ(build.Approve(), "");
    EXPECT_EQ(build.Stage(), "planned");
    EXPECT_TRUE(fs::exists(build.Folder() / "layout.png"));
    EXPECT_TRUE(build.State()["notices"].size() >= 2) << build.State()["notices"].dump();
}

TEST(BuildRobustTest, FailedCallsAreRetriedThenFallBack) {
    Fixture f("aige_build_retry");
    PlaceBuild build(f.Host());
    const json failed = {{"ok", false}, {"errors", json::array({"The model took too long"})}};
    ASSERT_EQ(build.Start("a village by a river"), "");
    f.answered.insert(1);
    build.OnResponse(1, failed);
    ASSERT_EQ(f.Count("/places/interpret"), 2); // asked again
    EXPECT_EQ(build.Stage(), "interpreting");
    f.AnswerAll(build, failed);
    EXPECT_EQ(f.Count("/places/interpret"), 3); // three times in all, then the request's words
    ASSERT_EQ(build.Stage(), "request");
    EXPECT_FALSE(build.Notices().empty());

    ASSERT_EQ(build.Step("assets"), "");
    ASSERT_EQ(build.Confirm(), "");
    f.AnswerAll(build, failed); // every planning call fails too
    EXPECT_EQ(f.Count("/places/plan-root"), 3);
    EXPECT_GE(f.Count("/places/plan-zone"), 3);
    ASSERT_EQ(build.Stage(), "plan") << build.State().dump();
    EXPECT_TRUE(Errors(build.PlanProblems()).empty()) << Describe(build.PlanProblems());
    EXPECT_EQ(build.Approve(), "");
}

TEST(BuildRobustTest, AServiceWithoutAModelIsNotAskedAgain) {
    Fixture f("aige_build_hopeless");
    PlaceBuild build(f.Host());
    ASSERT_EQ(build.Start("a hamlet"), "");
    f.AnswerAll(build, {{"ok", false}, {"errors", json::array({"No AI provider configured"})}});
    EXPECT_EQ(f.Count("/places/interpret"), 1);
    EXPECT_EQ(build.Stage(), "request");
}

// What a user's build got wrong (October 2026): a docks zone with one dock but
// three piers, a garden's crop mix with a crop the vocabulary lacked, and a
// pasture (it needs a fence) with fence lines off in the Brief.
TEST(BuildRobustTest, ABrokenPlanIsFixedOrKeptAndCanBeApproved) {
    Fixture f("aige_build_broken");
    PlaceBuild build(f.Host());
    ASSERT_EQ(build.Start("a large village by a river"), "");
    json brief = {{"buildType", "settlement"},
                  {"name", "Riverside Haven"},
                  {"features", json::array({{{"module", "river"}}})},
                  {"scale", {{"class", "large-village"}}},
                  {"shape", {{"layout", {{"mix", {{"linear-along-river", 0.4}, {"organic", 0.6}}}}}}},
                  {"walled", "none"},
                  {"components",
                   {{"residential-ward", {{"enabled", true}, {"count", 1}}},
                    {"market-ward", {{"enabled", true}, {"count", 1}}},
                    {"docks", {{"enabled", true}, {"count", 1}}},
                    {"outskirts", {{"enabled", true}, {"count", 1}}},
                    {"farmstead", {{"enabled", true}, {"params", {{"farm-kind", "mixed"}}}}},
                    {"garden", {{"enabled", true}, {"params", {{"fenced", true}, {"kind", "vegetable"}}}}},
                    {"house-plot", {{"enabled", true}}},
                    {"fence-line", {{"enabled", false}}},
                    {"road", {{"enabled", true}}}}}};
    f.answered.insert(1);
    build.OnResponse(1, {{"ok", true}, {"drafts", json::array({{{"reading", "typical"}, {"brief", brief}}})}});
    ASSERT_EQ(build.Stage(), "request");
    f.AnswerAll(build, {{"ok", false}, {"errors", json::array({"No AI provider configured"})}}); // draft repairs, if any
    ASSERT_EQ(build.Step("assets"), "") << build.State()["window"]["problems"].dump();
    ASSERT_EQ(build.Confirm(), "");

    const json root = {{"children", json::array({
                                        {{"id", "market-ward"}, {"module", "market-ward"}, {"quantity", {{"plots", {{"count", 12}}}}}},
                                        {{"id", "residential-ward"}, {"module", "residential-ward"}, {"quantity", {{"plots", {{"count", 40}}}}}},
                                        {{"id", "docks"}, {"module", "docks"}, {"quantity", {{"piers", 3}, {"plots", json::array({8, 15})}}},
                                         {"rel", json::array({{{"type", "along"}, {"to", "river"}}})}},
                                        {{"id", "outskirts"}, {"module", "outskirts"}, {"quantity", {{"plots", {{"count", 12}}}}}},
                                        {{"id", "main-road"}, {"module", "road"}, {"quality", {{"class", "main"}}}},
                                    })}};
    ASSERT_EQ(f.sent.back().first, "/places/plan-root");
    f.answered.insert(static_cast<int>(f.sent.size()));
    build.OnResponse(static_cast<int>(f.sent.size()), {{"ok", true}, {"answer", root}});

    const json garden = {{"id", "garden-2"}, {"module", "garden"}, {"repeat", 30},
                         {"quality", {{"crop-mix", {{"cabbage", 1}, {"carrot", 1}, {"onion", 1}}}, {"fenced", true}, {"kind", "vegetable"}}}};
    for (int guard = 0; guard < 50 && build.Stage() == "planning"; ++guard) {
        int next = 0;
        for (int i = 1; i <= static_cast<int>(f.sent.size()); ++i) {
            if (!f.answered.contains(i)) {
                next = i;
                break;
            }
        }
        if (next == 0) break;
        f.answered.insert(next);
        const auto& [path, body] = f.sent[next - 1];
        const std::string module = path == "/places/plan-zone" ? body["zone"].value("module", "") : "";
        json answer;
        if (module == "residential-ward") {
            answer = {{"children", json::array({{{"id", "cottages"}, {"module", "house-plot"}, {"repeat", 40}}, garden})}};
        } else if (module == "outskirts") {
            answer = {{"children", json::array({{{"id", "farmstead"}, {"module", "farmstead"}, {"repeat", 3}, {"quality", {{"farm-kind", "mixed"}}}},
                                                {{"id", "house-plot"}, {"module", "house-plot"}, {"repeat", 9}}})}};
        }
        build.OnResponse(next, answer.is_null() ? json{{"ok", false}, {"errors", json::array({"No AI provider configured"})}}
                                                : json{{"ok", true}, {"answer", answer}});
    }
    ASSERT_EQ(build.Stage(), "plan") << build.State().dump();
    // Nothing is left as an error, so Approve works.
    EXPECT_TRUE(Errors(build.PlanProblems()).empty()) << Describe(build.PlanProblems());
    EXPECT_TRUE(Errors(ValidateSpec(Library(), build.Spec(), SpecStage::Final, "settlement")).empty())
        << Describe(ValidateSpec(Library(), build.Spec(), SpecStage::Final, "settlement"));
    json spec = build.Spec();
    if (std::getenv("AIGE_PRINT_PLAN")) printf("%s\n%s\n", Describe(build.PlanProblems()).c_str(), SpecNode(spec, "docks")->dump().c_str());
    // The onion is a crop now.
    const json* g = SpecNode(spec, "residential-ward/garden-2");
    ASSERT_NE(g, nullptr) << spec.dump().substr(0, 4000);
    EXPECT_TRUE((*g)["quality"]["crop-mix"].contains("onion")) << g->dump();
    ASSERT_EQ(build.Approve(), "");
    EXPECT_EQ(build.Stage(), "revising"); // the AI's review of the map
    f.AnswerAll(build, {{"ok", false}, {"errors", json::array({"No AI provider configured"})}});
    EXPECT_EQ(build.Stage(), "planned"); // without it, the layout stands
}

TEST(BuildRobustTest, ABuildIsReopenedWhereItWasLeft) {
    Fixture f("aige_build_reopen");
    f.on = false;
    std::string first, second;
    {
        PlaceBuild build(f.Host());
        ASSERT_EQ(build.Start("a village with gardens"), "");
        ASSERT_EQ(build.Step("assets"), "");
        build.Edit("name", "Elmstead");
        first = build.Id();
    }
    {
        PlaceBuild build(f.Host());
        ASSERT_EQ(build.Open(first), "");
        EXPECT_EQ(build.Stage(), "assets");
        EXPECT_EQ(build.Brief().value("name", ""), "Elmstead");
        EXPECT_FALSE(build.Notices().empty()); // the AI was off: still said
        ASSERT_EQ(build.Confirm(), "");
        ASSERT_EQ(build.Stage(), "plan");
    }
    {
        PlaceBuild build(f.Host());
        ASSERT_EQ(build.Start("a hamlet"), "");
        second = build.Id();
    }
    const json builds = ListBuilds(f.root);
    ASSERT_EQ(builds.size(), 2u);
    std::set<std::string> ids = {builds[0]["id"], builds[1]["id"]};
    EXPECT_TRUE(ids.contains(first) && ids.contains(second));
    for (const auto& b : builds) {
        if (b["id"] == first) {
            EXPECT_EQ(b["stage"], "plan");
            EXPECT_EQ(b["name"], "Elmstead");
        }
    }
    {
        // Back at the plan, and approvable.
        PlaceBuild build(f.Host());
        ASSERT_EQ(build.Open(first), "");
        EXPECT_EQ(build.Stage(), "plan");
        EXPECT_TRUE(build.Spec().is_object());
        EXPECT_EQ(build.Approve(), "");
    }
    {
        // A build from before sessions were kept: its stage from its files.
        fs::remove(f.root / "places" / first / "session.json");
        PlaceBuild build(f.Host());
        ASSERT_EQ(build.Open(first), "");
        EXPECT_EQ(build.Stage(), "planned");
        EXPECT_TRUE(build.State().contains("image"));
    }
    PlaceBuild none(f.Host());
    EXPECT_FALSE(none.Open("no-such-build").empty());
}

TEST(BuildRobustTest, AStepThatWasWaitingOnTheAiStartsAgain) {
    Fixture f("aige_build_resume");
    std::string id;
    {
        PlaceBuild build(f.Host());
        ASSERT_EQ(build.Start("a town with a wall"), "");
        id = build.Id(); // the editor closes before the answer comes
    }
    PlaceBuild build(f.Host());
    ASSERT_EQ(build.Open(id), "");
    EXPECT_EQ(build.Stage(), "interpreting");
    EXPECT_EQ(f.Count("/places/interpret"), 2);
    EXPECT_TRUE(build.Owns(2));
}

TEST(BuildRobustTest, VerifyShowsAWardWithoutItsTemple) {
    const json brief = CompleteBrief(Library(), {{"buildType", "settlement"}, {"scale", {{"class", "village"}}},
                                                 {"components", {{"temple-ward", {{"enabled", true}}}, {"garden", {{"enabled", true}}}}}});
    json spec = {{"root", {{"id", "v"}, {"module", "settlement"},
                           {"children", json::array({{{"id", "temple-ward"}, {"module", "temple-ward"},
                                                      {"children", json::array({{{"id", "temple-ward/temple"}, {"module", "temple-plot"}},
                                                                                {{"id", "temple-ward/cottage"}, {"module", "house-plot"}, {"repeat", 6}}})}}})}}}};
    json solved = {{"objects", json::array()}, {"regions", json::array()}};
    for (int k = 0; k < 6; ++k) {
        solved["objects"].push_back({{"layer", "building"}, {"primary", true}, {"node", "temple-ward/cottage"}, {"shows", "house-plot"}});
    }
    const json rows = VerifyLayout(Library(), brief, spec, solved);
    const json* ward = nullptr;
    for (const auto& r : rows) {
        if (r["id"] == "temple-ward") ward = &r;
    }
    ASSERT_NE(ward, nullptr) << rows.dump();
    EXPECT_EQ((*ward)["status"], "short");
    bool templeMissing = false;
    for (const auto& p : (*ward)["parts"]) templeMissing = templeMissing || (p["id"] == "temple-ward/temple" && p["status"] == "missing");
    EXPECT_TRUE(templeMissing) << ward->dump();
    // The garden the Brief asked for isn't in the plan.
    bool garden = false;
    for (const auto& r : rows) garden = garden || (r["id"] == "components.garden" && r["status"] == "missing");
    EXPECT_TRUE(garden) << rows.dump();
}

TEST(BuildRobustTest, APlanIsRevisedReviewedAndUndone) {
    Fixture f("aige_build_revise");
    PlaceBuild build(f.Host());
    const json off = {{"ok", false}, {"errors", json::array({"No AI provider configured"})}};
    ASSERT_EQ(build.Start("a large village by a river"), "");
    f.AnswerAll(build, off); // the Brief from the words
    ASSERT_EQ(build.Step("assets"), "");
    ASSERT_EQ(build.Confirm(), "");
    f.AnswerAll(build, off); // the plan from the rules
    ASSERT_EQ(build.Stage(), "plan") << build.State().dump();
    ASSERT_EQ(build.Approve(), "");
    // Laid out, then the AI looks at the map.
    ASSERT_EQ(f.sent.back().first, "/places/review-layout");
    EXPECT_EQ(f.sent.back().second["image"], "places/" + build.Id() + "/layout.png");
    EXPECT_TRUE(f.sent.back().second["verify"].size() > 2);
    EXPECT_EQ(build.Stage(), "revising");
    f.answered.insert(static_cast<int>(f.sent.size()));
    build.OnResponse(static_cast<int>(f.sent.size()), {{"ok", true}, {"answer", {{"summary", "Looks right."}, {"changes", json::array()}}}});
    ASSERT_EQ(build.Stage(), "planned");
    ASSERT_EQ(build.Revisions().size(), 1u);
    EXPECT_TRUE(build.State().contains("verify"));

    // The user asks for a temple ward.
    ASSERT_EQ(build.Revise("add a temple ward with its temple and a few cottages"), "");
    ASSERT_EQ(f.sent.back().first, "/places/revise-plan");
    EXPECT_FALSE(f.sent.back().second["plan"]["zones"].empty());
    EXPECT_TRUE(f.sent.back().second["context"].contains("what each zone can hold"));
    const json changes = json::array({{{"op", "add"},
                                       {"node", {{"id", "temple-ward"}, {"module", "temple-ward"},
                                                 {"children", json::array({{{"id", "temple"}, {"module", "temple-plot"}},
                                                                           {{"id", "cottages"}, {"module", "house-plot"}, {"repeat", 4}}})}}}},
                                      {{"op", "remove"}, {"node", "no-such-zone"}}});
    f.answered.insert(static_cast<int>(f.sent.size()));
    build.OnResponse(static_cast<int>(f.sent.size()),
                     {{"ok", true}, {"answer", {{"summary", "Added a temple ward."}, {"changes", changes}}}});
    f.AnswerAll(build, off); // a final repair, if any
    ASSERT_EQ(build.Stage(), "planned") << build.State()["status"];
    json spec = build.Spec();
    ASSERT_NE(SpecNode(spec, "temple-ward"), nullptr);
    ASSERT_NE(SpecNode(spec, "temple-ward/temple"), nullptr);
    const json& last = build.Revisions().back();
    EXPECT_EQ(last["summary"], "Added a temple ward.");
    EXPECT_NE(last["notes"].dump().find("no-such-zone"), std::string::npos); // what it couldn't do, said
    bool templeBuilt = false;
    for (const auto& o : build.Solved()["objects"]) templeBuilt = templeBuilt || o.value("node", "") == "temple-ward/temple";
    EXPECT_TRUE(templeBuilt);

    // Undo: back to the plan before it, laid out again.
    ASSERT_EQ(build.Undo(), "");
    spec = build.Spec();
    EXPECT_EQ(SpecNode(spec, "temple-ward"), nullptr);
    EXPECT_EQ(build.Stage(), "planned");
    EXPECT_TRUE(build.Revisions().back()["undone"].get<bool>());

    // Kept across sessions.
    PlaceBuild again(f.Host());
    ASSERT_EQ(again.Open(build.Id()), "");
    EXPECT_EQ(again.Revisions().size(), 2u);
}

// What a user's village (Test1, October 2026) lost between plan and layout:
// zone areas adding up to more than the site, a farmland ring that took the
// wards' land, gardens and a graveyard planned in their wards, farmsteads
// counted by the ring's field. The plan is laid out at once and fitted: Verify
// finds everything planned.
TEST(BuildRobustTest, ThePlanIsLaidOutAsPlanned) {
    Fixture f("aige_build_fit");
    PlaceBuild build(f.Host());
    const json off = {{"ok", false}, {"errors", json::array({"No AI provider configured"})}};
    ASSERT_EQ(build.Start("a village by a river"), "");
    json brief = {{"buildType", "settlement"}, {"name", "Riverside"}, {"features", json::array({{{"module", "river"}}})},
                  {"scale", {{"class", "village"}, {"area", {{"area", json::array({3600, 10000})}}}, {"buildings", {{"count", json::array({30, 50})}}}}},
                  {"walled", "none"},
                  {"components", {{"residential-ward", {{"enabled", true}}}, {"market-ward", {{"enabled", true}}}, {"temple-ward", {{"enabled", true}}},
                                  {"farmland-ring", {{"enabled", true}}}, {"farmstead", {{"enabled", true}}}, {"garden", {{"enabled", true}}},
                                  {"graveyard", {{"enabled", true}}}, {"house-plot", {{"enabled", true}}}, {"road", {{"enabled", true}}}}}};
    f.answered.insert(1);
    build.OnResponse(1, {{"ok", true}, {"drafts", json::array({{{"reading", "typical"}, {"brief", brief}}})}});
    f.AnswerAll(build, off);
    ASSERT_EQ(build.Step("assets"), "") << build.State()["window"]["problems"].dump();
    ASSERT_EQ(build.Confirm(), "");
    const json root = {{"children", json::array({
                                        {{"id", "market-ward"}, {"module", "market-ward"}, {"quantity", {{"plots", {{"count", 5}}}, {"area", {{"area", 1200}}}}}},
                                        {{"id", "residential-ward"}, {"module", "residential-ward"}, {"quantity", {{"plots", {{"count", 18}}}, {"area", {{"area", 1500}}}}}},
                                        {{"id", "temple-ward"}, {"module", "temple-ward"}, {"quantity", {{"plots", {{"count", 3}}}, {"area", {{"area", 600}}}}}},
                                        {{"id", "farmland-ring"}, {"module", "farmland-ring"}, {"quantity", {{"area", {{"area", 5000}}}, {"farmsteads", 3}}}},
                                        {{"id", "main-road"}, {"module", "road"}, {"quality", {{"class", "main"}}}},
                                    })}};
    f.answered.insert(static_cast<int>(f.sent.size()));
    build.OnResponse(static_cast<int>(f.sent.size()), {{"ok", true}, {"answer", root}});
    for (int guard = 0; guard < 50 && build.Stage() == "planning"; ++guard) {
        int next = 0;
        for (int i = 1; i <= static_cast<int>(f.sent.size()); ++i) {
            if (!f.answered.contains(i)) {
                next = i;
                break;
            }
        }
        if (next == 0) break;
        f.answered.insert(next);
        const auto& [path, body] = f.sent[next - 1];
        const std::string module = path == "/places/plan-zone" ? body["zone"].value("module", "") : "";
        json answer;
        if (module == "residential-ward") {
            answer = {{"children", json::array({{{"id", "houses"}, {"module", "house-plot"}, {"repeat", 17}},
                                                {{"id", "gardens"}, {"module", "garden"}, {"repeat", 10}}})}};
        } else if (module == "temple-ward") {
            answer = {{"children", json::array({{{"id", "temple"}, {"module", "temple-plot"}},
                                                {{"id", "houses"}, {"module", "house-plot"}, {"repeat", 2}},
                                                {{"id", "graveyard"}, {"module", "graveyard"}}})}};
        }
        build.OnResponse(next, answer.is_null() ? off : json{{"ok", true}, {"answer", answer}});
    }
    ASSERT_EQ(build.Stage(), "plan") << build.State()["status"];
    // Laid out already: ③ Plan shows the map it will be.
    ASSERT_TRUE(build.Solved().is_object());
    EXPECT_TRUE(build.State().contains("image"));
    json spec = build.Spec();
    std::string report;
    for (const auto& row : VerifyLayout(Library(), build.Brief(), spec, build.Solved())) {
        if (row["id"] == "area") continue; // the site may grow for its zones
        if (row["status"] != "ok") report += "  " + row.dump() + "\n";
    }
    EXPECT_TRUE(report.empty()) << report;
    // The farmsteads the ring's field counts are in the plan.
    const json* ring = SpecNode(spec, "farmland-ring");
    ASSERT_NE(ring, nullptr);
    bool farmsteads = false;
    for (const auto& c : (*ring)["children"]) farmsteads = farmsteads || (c["module"] == "farmstead" && c.value("repeat", 1) == 3);
    EXPECT_TRUE(farmsteads) << ring->dump();
    // The plan's areas are the layout's.
    for (const auto& r : build.Solved()["regions"]) {
        if (r.value("statement", "") != "site/zones") continue;
        const json* zone = SpecNode(spec, r["id"]);
        ASSERT_NE(zone, nullptr);
        EXPECT_EQ((*zone)["quantity"]["area"]["area"], r["area"]);
    }
}

namespace {

std::map<std::string, std::pair<int, int>> ZoneSeeds(const json& solved) {
    std::map<std::string, std::pair<int, int>> out;
    for (const auto& r : solved["regions"]) {
        if (r.value("statement", "") == "site/zones" && r.contains("seed")) out[r["id"]] = {r["seed"][0].get<int>(), r["seed"][1].get<int>()};
    }
    return out;
}

double ToRiver(const json& solved, std::pair<int, int> at) {
    double best = 1e9;
    for (const auto& p : solved["paths"]) {
        if (p.value("class", "") != "river") continue;
        for (const auto& c : p["cells"]) best = std::min(best, std::hypot(c[0].get<int>() - at.first, c[1].get<int>() - at.second));
    }
    return best;
}

} // namespace

// "Put the residential ward below the docks, near the river" (a user's
// request): the ward moves there; the zones it doesn't name stay where they
// were; a re-plan of a zone it doesn't name is left out.
TEST(BuildRobustTest, ARevisionMovesWhatItsAskedToAndKeepsTheRest) {
    Fixture f("aige_build_move");
    PlaceBuild build(f.Host());
    const json off = {{"ok", false}, {"errors", json::array({"No AI provider configured"})}};
    ASSERT_EQ(build.Start("a village by a river with a market and a temple ward"), "");
    f.AnswerAll(build, off);
    ASSERT_EQ(build.Step("assets"), "");
    ASSERT_EQ(build.Confirm(), "");
    f.AnswerAll(build, off);
    ASSERT_EQ(build.Stage(), "plan") << build.State()["status"];
    const json before = build.Solved();
    const auto seeds = ZoneSeeds(before);
    ASSERT_TRUE(seeds.contains("residential-ward") && seeds.contains("market-ward") && seeds.contains("temple-ward"))
        << json(seeds).dump();

    ASSERT_EQ(build.Revise("put the residential ward south of the market ward, near the river"), "");
    const json changes = json::array({{{"op", "move"}, {"node", "residential-ward"},
                                       {"rel", json::array({{{"type", "south-of"}, {"to", "market-ward"}}, {{"type", "near"}, {"to", "river"}}})}},
                                      {{"op", "zone"}, {"node", "temple-ward"}, {"children", json::array()}}});
    f.answered.insert(static_cast<int>(f.sent.size()));
    build.OnResponse(static_cast<int>(f.sent.size()), {{"ok", true}, {"answer", {{"summary", "Moved it."}, {"changes", changes}}}});
    f.AnswerAll(build, off);
    ASSERT_EQ(build.Stage(), "plan");
    const json after = build.Solved();
    const auto moved = ZoneSeeds(after);
    // The others where they were.
    EXPECT_EQ(moved.at("market-ward"), seeds.at("market-ward"));
    EXPECT_EQ(moved.at("temple-ward"), seeds.at("temple-ward"));
    // The ward south of the market, by the river.
    EXPECT_GT(moved.at("residential-ward").second, moved.at("market-ward").second);
    EXPECT_LT(ToRiver(after, moved.at("residential-ward")), std::max(8.0, ToRiver(before, seeds.at("residential-ward"))));
    // The temple ward's re-plan wasn't asked for.
    json spec = build.Spec();
    EXPECT_FALSE((*SpecNode(spec, "temple-ward"))["children"].empty());
    EXPECT_NE(build.Revisions().back()["notes"].dump().find("temple-ward: left as it was"), std::string::npos)
        << build.Revisions().back().dump();
    // Undo: back where it was.
    ASSERT_EQ(build.Undo(), "");
    EXPECT_EQ(ZoneSeeds(build.Solved()).at("residential-ward"), seeds.at("residential-ward"));
}

// "Make the residential ward lie along the river on the left side and put
// farmland to the left of it" (a user's request, October 2026). The AI said it
// was already so and changed nothing. Now: the AI is shown where things are,
// the facts it lists are checked on the layout, the zones they're about are
// placed anew with those relations, and what still doesn't hold is said.
TEST(BuildRobustTest, ARevisionsSpatialFactsAreCheckedOnTheLayout) {
    Fixture f("aige_build_where");
    PlaceBuild build(f.Host());
    const json off = {{"ok", false}, {"errors", json::array({"No AI provider configured"})}};
    ASSERT_EQ(build.Start("a village by a river"), "");
    json brief = {{"buildType", "settlement"}, {"name", "Riverside"}, {"features", json::array({{{"module", "river"}}})},
                  {"scale", {{"class", "village"}}}, {"walled", "none"},
                  {"components", {{"residential-ward", {{"enabled", true}}}, {"market-ward", {{"enabled", true}}},
                                  {"farmland-ring", {{"enabled", true}}}, {"house-plot", {{"enabled", true}}}, {"road", {{"enabled", true}}}}}};
    f.answered.insert(1);
    build.OnResponse(1, {{"ok", true}, {"drafts", json::array({{{"reading", "typical"}, {"brief", brief}}})}});
    f.AnswerAll(build, off);
    ASSERT_EQ(build.Step("assets"), "");
    ASSERT_EQ(build.Confirm(), "");
    f.AnswerAll(build, off); // the plan from the rules
    ASSERT_EQ(build.Stage(), "plan") << build.State()["status"];
    json spec = build.Spec();
    ASSERT_NE(SpecNode(spec, "farmland-ring"), nullptr) << spec.dump().substr(0, 800);

    ASSERT_EQ(build.Revise("make the residential ward lie along the river on the left side and put farmland to the left of it"), "");
    // The AI was shown the layout as it is.
    ASSERT_EQ(f.sent.back().first, "/places/revise-plan");
    const json where = f.sent.back().second["where"];
    EXPECT_FALSE(where["map"].empty());
    EXPECT_NE(where["zones"].dump().find("residential-ward"), std::string::npos);
    // It answers that it's so already, and lists the facts.
    const json answer = {{"summary", "Both placements were already in the plan."},
                         {"changes", json::array()},
                         {"expect", json::array({{{"node", "residential-ward"}, {"type", "along"}, {"to", "river"}},
                                                 {{"node", "residential-ward"}, {"type", "west-of"}, {"to", "river"}},
                                                 {{"node", "farmland-ring"}, {"type", "west-of"}, {"to", "residential-ward"}}})}};
    f.answered.insert(static_cast<int>(f.sent.size()));
    build.OnResponse(static_cast<int>(f.sent.size()), {{"ok", true}, {"answer", answer}});
    f.AnswerAll(build, off);
    ASSERT_EQ(build.Stage(), "plan");
    const json& revision = build.Revisions().back();
    ASSERT_TRUE(revision.contains("checks")) << revision.dump();
    ASSERT_EQ(revision["checks"].size(), 3u);
    // The report is the layout's truth, and says what wasn't achieved.
    const json truth = CheckExpectations(build.Solved(), answer["expect"]);
    bool allOk = true;
    for (size_t i = 0; i < truth.size(); ++i) {
        EXPECT_EQ(revision["checks"][i]["ok"], truth[i]["ok"]) << revision["checks"][i].dump();
        allOk = allOk && truth[i]["ok"].get<bool>();
    }
    EXPECT_EQ(revision["summary"].get<std::string>().find("Not achieved") == std::string::npos, allOk) << revision.dump();
    // Along the river on its west bank: the solver can do that much.
    EXPECT_TRUE(truth[0]["ok"].get<bool>()) << truth[0].dump();
    EXPECT_TRUE(truth[1]["ok"].get<bool>()) << truth[1].dump();
    std::cout << "  checks: " << truth.dump() << "\n";
}
