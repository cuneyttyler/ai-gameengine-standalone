#include "editing/PlaceBuild.h"
#include "places/Brief.h"
#include "places/PlaceValidate.h"
#include "places/Planner.h"
#include "places/Solver.h"
#include "places/Where.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
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
    ASSERT_EQ(f.Count("/places/interpret"), 4); // the three readings, and the first asked again
    EXPECT_EQ(build.Stage(), "interpreting");
    f.AnswerAll(build, failed);
    EXPECT_EQ(f.Count("/places/interpret"), 9); // each reading three times in all, then the request's words
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
    EXPECT_EQ(f.Count("/places/interpret"), 3); // each reading once: none asked again
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
    // The other two readings (and any draft repair) get no answer: the one draft decides.
    f.AnswerAll(build, {{"ok", false}, {"errors", json::array({"No AI provider configured"})}});
    ASSERT_EQ(build.Stage(), "request");
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
    EXPECT_EQ(f.Count("/places/interpret"), 6); // the three readings, asked again
    EXPECT_TRUE(build.Owns(4));
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
    // As many as its field says (rebalanced with the rest to the Brief's buildings: the field follows).
    const int counted = (*ring)["quantity"]["farmsteads"].is_object() ? (*ring)["quantity"]["farmsteads"].value("count", 0)
                                                                      : (*ring)["quantity"].value("farmsteads", 0);
    EXPECT_GE(counted, 3);
    for (const auto& c : (*ring)["children"]) farmsteads = farmsteads || (c["module"] == "farmstead" && c.value("repeat", 1) == counted);
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

namespace {

// Everything of a layout but what belongs to `node` (and its parts).
json Without(const json& solved, const std::string& node) {
    json out = json::object();
    for (const char* list : {"regions", "objects", "lines"}) {
        out[list] = json::array();
        for (const auto& t : solved.value(list, json::array())) {
            bool of = false;
            for (const char* key : {"node", "id"}) {
                const std::string v = t.value(key, "");
                of = of || v == node || v.rfind(node + "/", 0) == 0 || v.rfind(node + "#", 0) == 0;
            }
            if (!of) out[list].push_back(t);
        }
    }
    return out;
}

} // namespace

// "Remove the inn north of the marketplace" (a user's request): the inn goes,
// and nothing else changes: not the market's other parcels, not the other
// zones, not one cell of the rest of the layout. Whether the AI says "remove"
// or re-plans the market without its inn.
TEST(BuildRobustTest, TakingAPartOutChangesNothingElse) {
    for (const bool asZone : {false, true}) {
        Fixture f(asZone ? "aige_build_local_zone" : "aige_build_local_remove");
        PlaceBuild build(f.Host());
        const json off = {{"ok", false}, {"errors", json::array({"No AI provider configured"})}};
        ASSERT_EQ(build.Start("a village by a river"), "");
        json brief = {{"buildType", "settlement"}, {"name", "Riverside"}, {"features", json::array({{{"module", "river"}}})},
                      {"scale", {{"class", "village"}}}, {"walled", "none"},
                      {"components", {{"residential-ward", {{"enabled", true}}}, {"market-ward", {{"enabled", true}}},
                                      {"house-plot", {{"enabled", true}}}, {"road", {{"enabled", true}}}}}};
        f.answered.insert(1);
        build.OnResponse(1, {{"ok", true}, {"drafts", json::array({{{"reading", "typical"}, {"brief", brief}}})}});
        f.AnswerAll(build, off);
        ASSERT_EQ(build.Step("assets"), "");
        ASSERT_EQ(build.Confirm(), "");
        // The market gets an inn; everything else comes from the rules.
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
            const bool market = path == "/places/plan-zone" && body["zone"].value("module", "") == "market-ward";
            const json answer = {{"children", json::array({{{"id", "square"}, {"module", "market-square"}},
                                                           {{"id", "shops"}, {"module", "shop-plot"}, {"repeat", 4}},
                                                           {{"id", "inn"}, {"module", "inn-plot"}}})}};
            build.OnResponse(next, market ? json{{"ok", true}, {"answer", answer}} : off);
        }
        ASSERT_EQ(build.Stage(), "plan") << build.State()["status"];
        const json before = build.Solved();
        const json specBefore = build.Spec();
        bool innThere = false;
        for (const auto& o : before["objects"]) innThere = innThere || o.value("node", "") == "market-ward/inn";
        ASSERT_TRUE(innThere) << "the inn wasn't laid out";

        ASSERT_EQ(build.Revise("remove the inn north of the marketplace"), "");
        json change = {{"op", "remove"}, {"node", "market-ward/inn"}};
        if (asZone) {
            json zone = *SpecNode(const_cast<json&>(specBefore), "market-ward");
            json children = json::array();
            for (const auto& c : zone["children"]) {
                if (c["id"] != "market-ward/inn") children.push_back(c);
            }
            change = {{"op", "zone"}, {"node", "market-ward"}, {"children", children}};
        }
        f.answered.insert(static_cast<int>(f.sent.size()));
        build.OnResponse(static_cast<int>(f.sent.size()),
                         {{"ok", true}, {"answer", {{"summary", "Removed the inn."}, {"changes", json::array({change})}}}});
        ASSERT_EQ(build.Stage(), "plan") << asZone;
        EXPECT_TRUE(build.Revisions().back().value("local", false)) << build.Revisions().back().dump();
        // The inn is gone, and the rest is the same, cell for cell.
        const json after = build.Solved();
        for (const auto& o : after["objects"]) EXPECT_NE(o.value("node", "").rfind("market-ward/inn", 0), 0u) << o.dump();
        EXPECT_EQ(Without(after, "market-ward/inn"), Without(before, "market-ward/inn")) << asZone;
        // The other zones' plans are as they were; the market keeps its other parcels.
        json spec = build.Spec();
        for (const auto& c : specBefore["root"]["children"]) {
            if (c["id"] == "market-ward") continue;
            const json* now = SpecNode(spec, c["id"]);
            ASSERT_NE(now, nullptr);
            EXPECT_EQ((*now)["children"], c["children"]) << c["id"];
        }
        EXPECT_EQ(SpecNode(spec, "market-ward/inn"), nullptr);
        EXPECT_NE(SpecNode(spec, "market-ward/shops"), nullptr);
        // Undo: the inn is back.
        ASSERT_EQ(build.Undo(), "");
        bool back = false;
        for (const auto& o : build.Solved()["objects"]) back = back || o.value("node", "") == "market-ward/inn";
        EXPECT_TRUE(back);
    }
}

// 7.7: a built place that passed its checks becomes an example for the next
// one like it; the AI looks at the built place as rendered.
TEST(BuildRobustTest, ABuiltPlaceIsKeptAndReviewedAsRendered) {
    Fixture f("aige_build_example");
    const json off = {{"ok", false}, {"errors", json::array({"No AI provider configured"})}};
    std::string first;
    {
        PlaceBuild build(f.Host());
        ASSERT_EQ(build.Start("a village by a river"), "");
        f.AnswerAll(build, off);
        ASSERT_EQ(build.Step("assets"), "");
        ASSERT_EQ(build.Confirm(), "");
        f.AnswerAll(build, off);
        ASSERT_EQ(build.Approve(), "");
        f.AnswerAll(build, off); // the map review
        ASSERT_EQ(build.StartBuilding(), "");
        // What the editor does: the scene picture, then the result.
        fs::create_directories(build.Folder());
        std::ofstream(build.Folder() / "scene.png") << "png";
        const size_t before = f.sent.size();
        build.Built({{"after", json::array({{{"id", "objects"}, {"ok", true}}})}, {"image", "places/x/scene.png"}});
        EXPECT_EQ(build.Stage(), "built");
        // Reviewed as rendered.
        ASSERT_EQ(f.sent.size(), before + 1);
        EXPECT_EQ(f.sent.back().first, "/places/review-layout");
        EXPECT_EQ(f.sent.back().second["kind"], "scene");
        EXPECT_EQ(f.sent.back().second["image"], "places/" + build.Id() + "/scene.png");
        f.answered.insert(static_cast<int>(f.sent.size()));
        build.OnResponse(static_cast<int>(f.sent.size()),
                         {{"ok", true}, {"answer", {{"summary", "One house stands on the road."}, {"issues", json::array({"a house on the road"})}}}});
        EXPECT_EQ(build.Result()["critique"]["issues"][0], "a house on the road");
        EXPECT_EQ(build.Stage(), "built");
        first = build.Id();
    }
    // Kept: the next place like it is planned with it as an example.
    const json examples = json::parse(std::ifstream(f.root / "places" / "examples.json"));
    ASSERT_EQ(examples.size(), 1u);
    EXPECT_EQ(examples[0]["id"], first);
    EXPECT_FALSE(examples[0]["zones"].empty());
    PlaceBuild next(f.Host());
    ASSERT_EQ(next.Start("another village by a river"), "");
    f.AnswerAll(next, off);
    ASSERT_EQ(next.Step("assets"), "");
    const size_t sentBefore = f.sent.size();
    ASSERT_EQ(next.Confirm(), "");
    ASSERT_GT(f.sent.size(), sentBefore);
    EXPECT_EQ(f.sent[sentBefore].first, "/places/plan-root");
    ASSERT_EQ(f.sent[sentBefore].second["examples"].size(), 1u) << f.sent[sentBefore].second["examples"].dump();
    EXPECT_EQ(f.sent[sentBefore].second["examples"][0]["request"], "a village by a river");
    // Why: each plan row says how it's placed.
    f.AnswerAll(next, off);
    const json rows = next.State()["plan"]["rows"];
    EXPECT_FALSE(rows.empty());
}

namespace {

// The layout's things outside `prefix`, or (inside = true) within it.
json Part(const json& doc, const std::string& prefix, bool inside) {
    json out = json::object();
    for (const char* list : {"regions", "objects", "lines", "paths"}) {
        out[list] = json::array();
        for (const auto& t : doc.value(list, json::array())) {
            bool in = false;
            for (const char* key : {"node", "id"}) {
                const std::string v = t.value(key, "");
                in = in || v == prefix || v.rfind(prefix + "/", 0) == 0 || v.rfind(prefix + "#", 0) == 0;
            }
            if (in == inside) out[list].push_back(t);
        }
        std::sort(out[list].begin(), out[list].end(), [](const json& a, const json& b) { return a.value("id", "") < b.value("id", ""); });
    }
    return out;
}

// An offline village, laid out: its residential ward's house template.
std::string PlannedVillage(Fixture& f, PlaceBuild& build) {
    const json off = {{"ok", false}, {"errors", json::array({"No AI provider configured"})}};
    EXPECT_EQ(build.Start("a village by a river"), "");
    f.AnswerAll(build, off);
    EXPECT_EQ(build.Step("assets"), "");
    EXPECT_EQ(build.Confirm(), "");
    f.AnswerAll(build, off);
    json spec = build.Spec();
    const json* ward = SpecNode(spec, "residential-ward");
    if (ward == nullptr) return "";
    for (const auto& c : (*ward)["children"]) {
        if (c.value("module", "") == "house-plot" && c.value("repeat", 1) >= 3) return c["id"];
    }
    return "";
}

} // namespace

// 7.8: editing a part lays out that part only; a locked part stays when what
// holds it is laid out again; the module brush paints a part where it's drawn.
TEST(BuildRobustTest, EditingAPartAfterTheBuildChangesOnlyIt) {
    Fixture f("aige_build_edit");
    PlaceBuild build(f.Host());
    const std::string houses = PlannedVillage(f, build);
    ASSERT_FALSE(houses.empty()) << build.Spec().dump().substr(0, 600);
    ASSERT_EQ(build.Stage(), "plan");

    // What the panel shows of a part.
    const json info = build.NodeInfo(houses + "#1");
    ASSERT_TRUE(info.is_object());
    EXPECT_EQ(info["module"], "house-plot");
    EXPECT_EQ(info["path"][0], "residential-ward");
    bool hasFenced = false;
    for (const auto& field : info["fields"]) hasFenced = hasFenced || field["name"] == "fenced";
    ASSERT_TRUE(hasFenced) << info.dump();

    // A field changed: that part laid out again, the rest as it was.
    const json before = build.Solved();
    ASSERT_EQ(build.EditNode(houses, "fenced", true), "");
    EXPECT_EQ(Part(build.Solved(), houses, false), Part(before, houses, false));
    EXPECT_EQ(build.TakeChanged(), (std::set<std::string>{houses}));
    EXPECT_NE(build.EditNode(houses, "fenced", "maybe"), ""); // checked against the module

    // A locked house stays when its whole ward is laid out again.
    build.Lock(houses + "#1", true);
    const json locked = Part(build.Solved(), houses + "#1", true);
    ASSERT_FALSE(locked["objects"].empty());
    const json beforeWard = build.Solved();
    ASSERT_EQ(build.Regenerate("residential-ward"), "");
    EXPECT_EQ(Part(build.Solved(), houses + "#1", true), locked);
    EXPECT_EQ(Part(build.Solved(), "residential-ward", false), Part(beforeWard, "residential-ward", false));
    EXPECT_TRUE(build.NodeInfo(houses + "#1")["locked"].get<bool>());

    // The module brush: a garden painted in the ward, where it was drawn.
    json ward;
    for (const auto& r : build.Solved()["regions"]) {
        if (r["id"] == "residential-ward") ward = r;
    }
    ASSERT_TRUE(ward.is_object());
    // A free spot in the ward: a cell of it that nothing else holds, and around it.
    std::set<int> taken;
    const int side = build.SiteSide();
    for (const auto& r : build.Solved()["regions"]) {
        if (r["id"] == "residential-ward" || r.value("parent", json()) == json()) continue;
        for (int c : DecodeCells(r["cells"], side)) taken.insert(c);
    }
    std::set<int> cells;
    for (int c : DecodeCells(ward["cells"], side)) cells.insert(c);
    int px = -1, py = -1;
    for (int c : cells) {
        const int x = c % side, y = c / side;
        bool ok = true;
        for (int dy = 0; dy < 3 && ok; ++dy)
            for (int dx = 0; dx < 3 && ok; ++dx) ok = cells.contains((y + dy) * side + x + dx) && !taken.contains((y + dy) * side + x + dx);
        if (ok) {
            px = x, py = y;
            break;
        }
    }
    ASSERT_GE(px, 0) << "no free 3x3 in the ward";
    const json beforePaint = build.Solved();
    ASSERT_EQ(build.Paint("garden", px, py, px + 3, py + 3), "");
    json spec = build.Spec();
    std::string painted;
    for (const auto& c : (*SpecNode(spec, "residential-ward"))["children"]) {
        if (c.contains("at")) painted = c["id"];
    }
    ASSERT_FALSE(painted.empty());
    bool there = false;
    for (const auto& r : build.Solved()["regions"]) {
        if (r["id"] != painted) continue;
        for (int c : DecodeCells(r["cells"], side)) there = there || (c % side >= px && c % side < px + 3 && c / side >= py && c / side < py + 3);
    }
    EXPECT_TRUE(there);
    EXPECT_EQ(Part(build.Solved(), painted, false)["objects"], Part(beforePaint, painted, false)["objects"]);
    EXPECT_NE(build.Paint("market-square", px, py, px + 3, py + 3), ""); // a ward can't hold a market square

    // Chat edits about the place go to it.
    EXPECT_TRUE(build.AboutThisPlace("make the residential ward's houses fenced"));
    EXPECT_FALSE(build.AboutThisPlace("make the player faster"));
    // Undo goes back through the edits.
    ASSERT_EQ(build.Undo(), "");
}
