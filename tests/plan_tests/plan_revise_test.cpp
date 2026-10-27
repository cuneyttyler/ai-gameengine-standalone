// ③ Plan revised as a user does it in the editor (tests/plan_tests/cases.json): a place built
// from its words with the real AI service through the layout and the plan to Approve, then the
// plan's revision box with the case's words (the zone named as the map shows it). Checked: the
// zone has exactly the buildings asked for more, and every other zone kept its land and its
// things exactly where they were (only the required change is made).
// Each case's pictures (before.png, after.png), run.log and report.json go to results/<case>/.
//
//   AIGE_REVISE_PORT=<service port> [AIGE_REVISE_CASES=<name>,...] aige_tests --gtest_filter=PlanRevise.*
#include "../revise_harness.h"

using namespace aige;
using namespace aige::revise_tests;

namespace {

json RunCase(const json& c, int port) {
    const std::string name = c.value("name", "");
    json report = {{"name", name}, {"about", c.value("about", "")}, {"request", c.value("request", "")}};
    json failures = json::array();
    auto fail = [&](const std::string& why) { failures.push_back(why); };
    LiveBuild run(ResultsOf("plan_tests", name), port);
    auto finish = [&] {
        report["failures"] = failures;
        report["passed"] = failures.empty();
        WriteText(run.Out() / "report.json", report.dump(2));
        return report;
    };
    if (!run.Error().empty()) return fail(run.Error()), finish();
    if (auto why = run.ToLayout(c.value("request", "")); !why.empty()) return fail("to the layout: " + why), finish();
    if (auto why = run.ToPlanned(); !why.empty()) return fail("to the plan: " + why), finish();
    run.Picture("layout.png", "before.png");

    std::map<std::string, std::string> zones;
    const json named_ = c.value("zones", json::object());
    for (const auto& [key, words] : named_.items()) {
        const std::string id = run.FindZone(words);
        if (id.empty()) return fail("the plan has no zone like " + words.dump() + " (it has " + json(run.Zones()).dump() + ")"), finish();
        zones[key] = id;
    }
    report["zones"] = zones;
    report["allZones"] = run.Zones();
    const std::string words = Fill(c.value("revise", ""), zones);
    report["revise"] = words;

    const json before = run.Build().Solved();
    if (auto why = run.Revise(words); !why.empty()) return fail("the revision: " + why), finish();
    run.Picture("layout.png", "after.png");
    const json after = run.Build().Solved();
    report["revision"] = run.Build().Revisions().empty() ? json() : run.Build().Revisions().back();

    const json expect = c.value("expect", json::object());
    const auto landBefore = ZoneLand(before), landAfter = ZoneLand(after);
    if (expect.contains("add")) {
        const std::string a = zones[expect["add"][0]];
        const int more = expect["add"][1].get<int>();
        const int was = Buildings(before, a), now = Buildings(after, a);
        report["buildings"] = {{"zone", a}, {"before", was}, {"after", now}, {"asked", more}};
        if (now - was != more) fail(a + " has " + std::to_string(now - was) + " more buildings (" + std::to_string(was) + " -> " + std::to_string(now) + "): asked " + std::to_string(more));
        if (landBefore.contains(a) && landAfter.contains(a)) {
            report["zoneLand"] = {{"before", landBefore.at(a).size()}, {"after", landAfter.at(a).size()}};
        }
    }
    // Every other zone exactly as it was: its land, and its things where they stood.
    std::set<std::string> named;
    for (const auto& [key, id] : zones) named.insert(id);
    json others = json::object();
    for (const auto& [id, module] : run.Zones()) {
        if (named.contains(id)) continue;
        const auto thingsBefore = ThingsIn(before, id), thingsAfter = ThingsIn(after, id);
        int moved = 0;
        for (const auto& t : thingsBefore) moved += !thingsAfter.contains(t);
        int added = 0;
        for (const auto& t : thingsAfter) added += !thingsBefore.contains(t);
        const bool sameLand = landBefore.contains(id) && landAfter.contains(id) && landBefore.at(id) == landAfter.at(id);
        others[id] = {{"things", thingsBefore.size()}, {"movedOrGone", moved}, {"new", added}, {"sameLand", sameLand},
                      {"buildings", {Buildings(before, id), Buildings(after, id)}}};
        if (!sameLand) {
            const double kept = landBefore.contains(id) && landAfter.contains(id) ? Kept(landBefore.at(id), landAfter.at(id)) : 0.0;
            fail(id + " (not asked about) changed shape: " + std::to_string(static_cast<int>(kept * 100)) + "% of its land kept");
        }
        if (moved > 0 || added > 0) {
            fail(id + " (not asked about): " + std::to_string(moved) + " of its " + std::to_string(thingsBefore.size()) + " things moved or gone, " +
                 std::to_string(added) + " new");
        }
    }
    report["others"] = others;
    return finish();
}

} // namespace

TEST(PlanRevise, AsAUserAsksInTheEditor) {
    const int port = ServicePort();
    if (port == 0) GTEST_SKIP() << "set AIGE_REVISE_PORT=<AI service port>";
    json verdicts = json::array();
    for (const auto& c : Cases("plan_tests")) {
        if (!Chosen(c.value("name", ""))) continue;
        std::cout << "plan case " << c.value("name", "") << std::endl;
        const json v = RunCase(c, port);
        verdicts.push_back({{"name", v["name"]}, {"passed", v["passed"]}, {"failures", v["failures"]}, {"revise", v.value("revise", "")}});
        EXPECT_TRUE(v.value("passed", false)) << c.value("name", "") << ": " << v["failures"].dump(2);
    }
    WriteSummary("plan_tests", verdicts);
}

// A saved build revised again, without the AI (AIGE_REVISE_REPRO=<project>|<words>): for a revision that fails
// before it asks the AI, so it can be looked at alone.
TEST(PlanRevise, ReproduceOnASavedBuild) {
    const char* arg = std::getenv("AIGE_REVISE_REPRO");
    if (arg == nullptr) GTEST_SKIP() << "set AIGE_REVISE_REPRO=<project>|<words>";
    const std::string text = arg;
    const fs::path project = text.substr(0, text.find('|'));
    const std::string words = text.substr(text.find('|') + 1);
    Ontology ontology;
    ontology.Load({EnginePlacesDir()});
    PlaceBuild::Host host;
    host.ontology = [&]() -> const Ontology& { return ontology; };
    host.catalog = [] { return Catalog{}; };
    host.isoGrid = [] { return true; };
    int sent = 0;
    host.request = [&](const std::string& path, const json& body, int) {
        std::cout << "would ask " << path << std::endl;
        try {
            std::cout << "  body: " << body.dump().size() << " bytes" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  the body can't be sent: " << e.what() << std::endl;
            std::string bad = body.dump(-1, ' ', false, json::error_handler_t::replace);
            const size_t at = bad.find("ï¿½");
            std::cout << "  near: " << bad.substr(at == std::string::npos ? 0 : (at > 200 ? at - 200 : 0), 400) << std::endl;
        }
        return ++sent;
    };
    host.projectRoot = project;
    host.layoutStep = true;
    PlaceBuild build(host);
    std::string id;
    for (const auto& e : fs::directory_iterator(project / "places")) id = e.path().filename().string();
    ASSERT_EQ(build.Open(id), "");
    std::cout << "opened " << id << " at " << build.Stage() << std::endl;
    try {
        std::cout << "revise: " << build.Revise(words) << std::endl;
    } catch (const std::exception& e) {
        FAIL() << "threw: " << e.what();
    }
}
