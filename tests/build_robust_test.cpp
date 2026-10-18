#include "editing/PlaceBuild.h"
#include "places/Brief.h"
#include "places/PlaceValidate.h"
#include "places/Planner.h"

#include <gtest/gtest.h>

#include <filesystem>
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
    EXPECT_EQ(build.Stage(), "planned");
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
