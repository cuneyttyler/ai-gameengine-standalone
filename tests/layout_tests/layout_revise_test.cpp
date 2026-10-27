// ② Layout revised as a user does it in the editor (tests/layout_tests/cases.json): a place
// built from its words to the layout with the real AI service, then the layout's revision box
// with the case's words (the zones named as the map shows them). Checked: the zones changed
// places, moved or changed size as asked, and the zones the request didn't touch kept their land.
// Each case's pictures (before.png, after.png), run.log and report.json go to results/<case>/.
//
//   AIGE_REVISE_PORT=<service port> [AIGE_REVISE_CASES=<name>,...] aige_tests --gtest_filter=LayoutRevise.*
#include "../revise_harness.h"

using namespace aige;
using namespace aige::revise_tests;

namespace {

// The zones touching any of `named` (a cell next to theirs), before the revision.
std::set<std::string> Touching(const std::map<std::string, std::set<int>>& land, const std::set<std::string>& named, int w) {
    std::set<int> theirs;
    for (const auto& id : named) {
        if (land.contains(id)) theirs.insert(land.at(id).begin(), land.at(id).end());
    }
    std::set<std::string> out;
    for (const auto& [id, cells] : land) {
        if (named.contains(id)) continue;
        for (int c : cells) {
            const int x = c % w;
            if (theirs.contains(c - w) || theirs.contains(c + w) || (x > 0 && theirs.contains(c - 1)) || (x + 1 < w && theirs.contains(c + 1))) {
                out.insert(id);
                break;
            }
        }
    }
    return out;
}

json RunCase(const json& c, int port) {
    const std::string name = c.value("name", "");
    json report = {{"name", name}, {"about", c.value("about", "")}, {"request", c.value("request", "")}};
    json failures = json::array();
    auto fail = [&](const std::string& why) { failures.push_back(why); };
    LiveBuild run(ResultsOf("layout_tests", name), port);
    auto finish = [&] {
        report["failures"] = failures;
        report["passed"] = failures.empty();
        WriteText(run.Out() / "report.json", report.dump(2));
        return report;
    };
    if (!run.Error().empty()) return fail(run.Error()), finish();
    if (auto why = run.ToLayout(c.value("request", "")); !why.empty()) return fail("to the layout: " + why), finish();
    run.Picture("zones.png", "before.png");

    // The zones the case names, as this plan has them.
    std::map<std::string, std::string> zones;
    std::set<std::string> taken;
    const json named_ = c.value("zones", json::object());
    for (const auto& [key, words] : named_.items()) {
        const std::string id = run.FindZone(words, taken);
        if (id.empty()) return fail("the plan has no zone like " + words.dump() + " (it has " + json(run.Zones()).dump() + ")"), finish();
        zones[key] = id;
        taken.insert(id);
    }
    report["zones"] = zones;
    report["allZones"] = run.Zones();
    const std::string words = Fill(c.value("revise", ""), zones);
    report["revise"] = words;

    const json before = run.Build().Solved();
    if (auto why = run.Revise(words); !why.empty()) return fail("the revision: " + why), finish();
    run.Picture("zones.png", "after.png");
    const json after = run.Build().Solved();
    report["revision"] = run.Build().Revisions().empty() ? json() : run.Build().Revisions().back();

    const int w = Width(before);
    const auto landBefore = ZoneLand(before), landAfter = ZoneLand(after);
    json sizes = json::object();
    for (const auto& [id, cells] : landBefore) {
        const auto& now = landAfter.contains(id) ? landAfter.at(id) : std::set<int>{};
        const auto [bx, by] = Middle(cells, w);
        const auto [ax, ay] = Middle(now, w);
        sizes[id] = {{"before", cells.size()}, {"after", now.size()}, {"kept", Kept(cells, now)},
                     {"middleBefore", {bx, by}}, {"middleAfter", {ax, ay}}};
    }
    report["land"] = sizes;
    for (const auto& [id, cells] : landBefore) {
        if (!landAfter.contains(id) || landAfter.at(id).empty()) fail(id + " is gone after the revision");
    }
    if (!failures.empty()) return finish();

    const json expect = c.value("expect", json::object());
    std::set<std::string> named;
    for (const auto& [key, id] : zones) named.insert(id);
    if (expect.contains("swap")) {
        const std::string a = zones[expect["swap"][0]], b = zones[expect["swap"][1]];
        const double ab = Kept(landAfter.at(a), landBefore.at(b)), ba = Kept(landAfter.at(b), landBefore.at(a));
        report["swap"] = {{a + " on " + b + "'s land", ab}, {b + " on " + a + "'s land", ba}};
        if (ab < 0.8) fail(a + " is only " + std::to_string(static_cast<int>(ab * 100)) + "% on where " + b + " was");
        if (ba < 0.8) fail(b + " is only " + std::to_string(static_cast<int>(ba * 100)) + "% on where " + a + " was");
    }
    if (expect.contains("direction")) {
        const std::string a = zones[expect["direction"][0]], b = zones[expect["direction"][2]];
        const std::string dir = expect["direction"][1];
        const auto [ax, ay] = Middle(landAfter.at(a), w);
        const auto [bx, by] = Middle(landAfter.at(b), w);
        const bool ok = dir == "north" ? ay < by : dir == "south" ? ay > by : dir == "east" ? ax > bx : ax < bx;
        if (!ok) fail(a + " isn't " + dir + " of " + b + " (middles " + std::to_string(ax) + "," + std::to_string(ay) + " and " +
                      std::to_string(bx) + "," + std::to_string(by) + ")");
    }
    if (expect.contains("size")) {
        const std::string a = zones[expect["size"][0]];
        const double ratio = static_cast<double>(landAfter.at(a).size()) / static_cast<double>(std::max<size_t>(1, landBefore.at(a).size()));
        report["sizeRatio"] = ratio;
        if (expect["size"][1].is_number() && ratio < expect["size"][1].get<double>()) {
            fail(a + " is " + std::to_string(ratio) + " times its size: asked at least " + expect["size"][1].dump());
        }
        if (expect["size"][2].is_number() && ratio > expect["size"][2].get<double>()) {
            fail(a + " is " + std::to_string(ratio) + " times its size: asked at most " + expect["size"][2].dump());
        }
    }
    // Only what was asked: the zones it doesn't name keep their land (those touching the named
    // ones may give some up when a zone grows or moves; the rest shouldn't move at all).
    const auto touching = Touching(landBefore, named, w);
    const double othersKeep = expect.value("othersKeep", 0.9), touchingKeep = expect.value("touchingKeep", 0.5);
    for (const auto& [id, cells] : landBefore) {
        if (named.contains(id)) continue;
        const double kept = Kept(cells, landAfter.at(id));
        const double need = touching.contains(id) ? touchingKeep : othersKeep;
        if (kept < need) {
            fail(id + " (not asked about" + std::string(touching.contains(id) ? ", touching" : "") + ") kept only " +
                 std::to_string(static_cast<int>(kept * 100)) + "% of its land (at least " + std::to_string(static_cast<int>(need * 100)) + "%)");
        }
    }
    return finish();
}

} // namespace

TEST(LayoutRevise, AsAUserAsksInTheEditor) {
    const int port = ServicePort();
    if (port == 0) GTEST_SKIP() << "set AIGE_REVISE_PORT=<AI service port>";
    json verdicts = json::array();
    for (const auto& c : Cases("layout_tests")) {
        if (!Chosen(c.value("name", ""))) continue;
        std::cout << "layout case " << c.value("name", "") << std::endl;
        const json v = RunCase(c, port);
        verdicts.push_back({{"name", v["name"]}, {"passed", v["passed"]}, {"failures", v["failures"]}, {"revise", v.value("revise", "")}});
        EXPECT_TRUE(v.value("passed", false)) << c.value("name", "") << ": " << v["failures"].dump(2);
    }
    WriteSummary("layout_tests", verdicts);
}
