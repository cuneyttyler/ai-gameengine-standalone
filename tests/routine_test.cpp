#include <gtest/gtest.h>

#include "ai_bridge/Proposal.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "editing/Commands.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/GameSession.h"
#include "gameplay/Pathfinding.h"
#include "gameplay/Schedule.h"
#include "gameplay/Settings.h"
#include "gameplay/Archetypes.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/LuaVM.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

// development_plan.md 3.5: clock, pathfinding, schedules, a simulated day.

TEST(ScheduleTest, ClockTimesAndWhichEntryIsActive) {
    EXPECT_EQ(aige::ParseClockTime("06:30"), 390);
    EXPECT_EQ(aige::ParseClockTime("0:05"), 5);
    EXPECT_EQ(aige::ParseClockTime("23:59"), 23 * 60 + 59);
    for (const char* bad : {"24:00", "12:60", "noon", "6", "06:3", "-1:00", "06:30pm"}) {
        EXPECT_FALSE(aige::ParseClockTime(bad)) << bad;
    }
    EXPECT_EQ(aige::FormatClockTime(390), "06:30");
    EXPECT_EQ(aige::FormatClockTime(aige::kMinutesPerDay + 5), "00:05");

    std::string error;
    auto schedule = aige::ScheduleFromJson(nlohmann::json::parse(R"({"name": "Day", "entries": [
        {"from": "21:00", "activity": "sleep", "place": "home"},
        {"from": "06:00", "activity": "wander", "place": "garden", "radius": 3},
        {"from": "12:00", "activity": "stay", "place": "well"}]})"), "day", &error);
    ASSERT_TRUE(schedule) << error;
    EXPECT_EQ(schedule->entries.front().place, "garden"); // sorted by time
    EXPECT_EQ(schedule->At(7 * 60)->place, "garden");
    EXPECT_EQ(schedule->At(12 * 60)->place, "well");
    EXPECT_EQ(schedule->At(23 * 60)->place, "home");
    EXPECT_EQ(schedule->At(3 * 60)->place, "home"); // past midnight: still last night's entry
    EXPECT_EQ(aige::ScheduleToJson(*aige::ScheduleFromJson(aige::ScheduleToJson(*schedule), "day", &error)),
              aige::ScheduleToJson(*schedule));

    EXPECT_FALSE(aige::ScheduleFromJson(nlohmann::json::parse(R"({"entries": [{"from": "25:00"}]})"), "x", &error));
    auto problems = aige::ValidateSchedule(*aige::ScheduleFromJson(nlohmann::json::parse(R"({"entries": [
        {"from": "06:00", "activity": "dance", "place": "garden"},
        {"from": "06:00", "activity": "stay", "place": "nowhere"},
        {"from": "09:00", "activity": "wander", "place": "garden", "radius": 0}]})"), "x", &error),
        [](const std::string&, const std::string& name) { return name == "garden"; });
    ASSERT_EQ(problems.size(), 4u);
    EXPECT_NE(problems[0].find("activity should be stay, wander or sleep"), std::string::npos);
    EXPECT_NE(problems[1].find("no marker called 'nowhere'"), std::string::npos);
}

TEST(PathfindingTest, FindsTheShortestWayAroundWallsWithoutCuttingCorners) {
    // A 10x10 room with a wall at x = 5 from y = 0 to 7 (gap at the top).
    auto solid = [](int x, int y) {
        if (x < 0 || x > 9 || y < 0 || y > 9) return true;
        return x == 5 && y <= 7;
    };
    auto path = aige::FindPath(solid, {2, 2}, {8, 2});
    ASSERT_TRUE(path);
    EXPECT_EQ(path->front(), (std::pair<int, int>{2, 2}));
    EXPECT_EQ(path->back(), (std::pair<int, int>{8, 2}));
    for (size_t i = 1; i < path->size(); ++i) {
        auto [x0, y0] = (*path)[i - 1];
        auto [x1, y1] = (*path)[i];
        EXPECT_FALSE(solid(x1, y1));
        EXPECT_LE(std::abs(x1 - x0), 1);
        EXPECT_LE(std::abs(y1 - y0), 1);
        if (x1 != x0 && y1 != y0) { // a diagonal step never squeezes past a corner
            EXPECT_FALSE(solid(x1, y0));
            EXPECT_FALSE(solid(x0, y1));
        }
    }
    EXPECT_TRUE(std::any_of(path->begin(), path->end(), [](auto c) { return c.second >= 8; })); // through the gap

    auto straight = aige::FindPath(solid, {0, 9}, {9, 9});
    ASSERT_TRUE(straight);
    EXPECT_EQ(straight->size(), 10u); // shortest

    auto walledIn = [](int x, int y) { return !(x == 0 && y == 0) && !(x == 3 && y == 3); };
    EXPECT_FALSE(aige::FindPath(walledIn, {0, 0}, {3, 3}));
    EXPECT_FALSE(aige::FindPath(solid, {2, 2}, {5, 2})); // the goal is a wall
}

namespace {

// A walled field: floor 0..19 x 0..9, a wall at x = 10 for y 0..6 (a gap at
// the top), home on the left, the garden on the right.
struct Village {
    Village() {
        root = fs::temp_directory_path() /
               ("aige_routine_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root);
        fs::create_directories(root / "schedules");
        std::ofstream(root / "schedules" / "tamsin.json") << R"({"name": "Tamsin", "entries": [
            {"from": "06:00", "activity": "wander", "place": "garden", "radius": 2},
            {"from": "20:00", "activity": "sleep", "place": "home"}]})";
        schedules = std::make_unique<aige::ScheduleLibrary>(root);
        schedules->Refresh();
        for (int x = -1; x <= 20; ++x) {
            for (int y = -1; y <= 10; ++y) {
                bool wall = x < 0 || x > 19 || y < 0 || y > 9 || (x == 10 && y <= 6);
                aige::Entity tile = registry.create();
                registry.emplace<aige::Tile>(tile, x, y, wall);
            }
        }
        marker("home", 2.5f, 2.5f);
        marker("garden", 16.5f, 2.5f);
        aige::CreatePlayer(registry, 1.5f, 8.5f);
        npc = registry.create();
        registry.emplace<aige::WorldTransform2D>(npc, 2.5f, 2.5f);
        registry.emplace<aige::Sprite>(npc);
        registry.emplace<aige::Routine>(npc, "schedules/tamsin.json", 3.0f);
        registry.emplace<aige::Talk>(npc, "dialogue/x.json", "character/villager", 1.5f);
    }
    ~Village() { fs::remove_all(root); }
    void marker(const std::string& name, float x, float y) {
        aige::Entity e = registry.create();
        registry.emplace<aige::WorldTransform2D>(e, x, y);
        registry.emplace<aige::Marker>(e, name);
    }
    float DistanceTo(float x, float y) {
        const auto& w = registry.get<aige::WorldTransform2D>(npc);
        return std::hypot(w.x - x, w.y - y);
    }
    fs::path root;
    std::unique_ptr<aige::ScheduleLibrary> schedules;
    aige::LuaVM lua;
    aige::Registry registry;
    aige::Entity npc{};
};

} // namespace

TEST(RoutineTest, ADayInTheVillage) {
    Village v;
    aige::ProjectSettings saved = aige::ActiveSettings();
    aige::ActiveSettings().time.minutesPerSecond = 10.0f; // 10 game minutes per second
    aige::GameSession session(v.registry, v.lua, aige::BootstrapIso(), v.root);
    session.SetSchedules(v.schedules.get());
    session.SetClockMinutes(5 * 60.0); // 05:00: still last night's "sleep at home"
    auto none = [](aige::Key) { return false; };
    auto run = [&](double seconds) {
        for (int i = 0; i < static_cast<int>(seconds * 60); ++i) session.Advance(1.0 / 60.0, none);
    };

    run(1.0);
    EXPECT_TRUE(v.registry.all_of<aige::Sleeping>(v.npc)); // asleep at home
    EXPECT_EQ(v.registry.get<aige::Sprite>(v.npc).a, 0.0f);

    run(8.0); // past 06:00: wakes and sets off for the garden, round the wall
    EXPECT_FALSE(v.registry.all_of<aige::Sleeping>(v.npc));
    EXPECT_EQ(v.registry.get<aige::Sprite>(v.npc).a, 1.0f);
    run(15.0);
    EXPECT_LT(v.DistanceTo(16.5f, 2.5f), 3.0f) << "should be in the garden by now";

    // Wandering stays near the garden.
    float farthest = 0.0f;
    for (int i = 0; i < 20 * 60; ++i) {
        session.Advance(1.0 / 60.0, none);
        farthest = std::max(farthest, v.DistanceTo(16.5f, 2.5f));
    }
    EXPECT_LT(farthest, 3.6f);
    EXPECT_GT(farthest, 0.5f); // and does wander

    // Evening: home to sleep, then nobody to talk to.
    session.SetClockMinutes(19 * 60 + 59.0);
    run(20.0);
    EXPECT_LT(v.DistanceTo(2.5f, 2.5f), 0.7f);
    EXPECT_TRUE(v.registry.all_of<aige::Sleeping>(v.npc));
    aige::StoryLibrary story(v.root);
    session.SetStory(&story);
    v.registry.get<aige::WorldTransform2D>(*aige::FindPlayer(v.registry)) = {3.2f, 2.5f};
    EXPECT_FALSE(session.TalkTarget().has_value());

    // The next morning she's up again.
    session.SetClockMinutes(aige::kMinutesPerDay + 6 * 60 + 1.0);
    run(0.5);
    EXPECT_FALSE(v.registry.all_of<aige::Sleeping>(v.npc));
    EXPECT_EQ(session.Day(), 2);
    aige::ActiveSettings() = saved;
}

// The AI proposes places and a routine together.
TEST(RoutineTest, MarkersAndAScheduleProposedTogether) {
    Village v;
    aige::Project project{v.root};
    aige::PrefabLibrary prefabs(project);
    aige::RegisterEditingCommands(aige::EngineReflection());
    nlohmann::json ops = nlohmann::json::parse(R"([
        {"op": "command", "name": "scene.addMarker", "args": {"name": "village_well", "x": 8.5, "y": 8.5}},
        {"op": "write_schedule", "id": "miller", "schedule": {"name": "The miller", "entries": [
            {"from": "07:00", "activity": "stay", "place": "village_well"},
            {"from": "19:00", "activity": "sleep", "place": "home"}]}}
    ])");
    auto resolved = aige::ResolveProposal({{"summary", "x"}, {"operations", ops}}, v.registry, aige::EngineReflection(),
                                          prefabs, project);
    ASSERT_EQ(resolved.items.size(), 2u);
    EXPECT_TRUE(resolved.items[1].error.empty()) << resolved.items[1].error;
    EXPECT_EQ(resolved.items[1].target, "Schedule · schedules/miller.json (The miller)");

    auto outcome = aige::ApplyOperations(ops.get<std::vector<nlohmann::json>>(), v.registry, aige::EngineReflection(),
                                         prefabs, project);
    ASSERT_TRUE(outcome.errors.empty()) << outcome.errors[0];
    aige::ScheduleLibrary library(v.root);
    library.Refresh();
    ASSERT_NE(library.Find("schedules/miller.json"), nullptr);
    auto markers = aige::MarkerPositions(v.registry);
    EXPECT_TRUE(std::any_of(markers.begin(), markers.end(), [](auto& m) { return m.first == "village_well"; }));

    nlohmann::json bad = nlohmann::json::parse(R"([{"op": "write_schedule", "id": "x", "schedule": {"entries": [
        {"from": "07:00", "activity": "stay", "place": "the_moon"}]}},
        {"op": "command", "name": "scene.addMarker", "args": {"name": "home", "x": 1, "y": 1}}])");
    resolved = aige::ResolveProposal({{"summary", "x"}, {"operations", bad}}, v.registry, aige::EngineReflection(),
                                     prefabs, project);
    EXPECT_NE(resolved.items[0].error.find("no marker called 'the_moon'"), std::string::npos) << resolved.items[0].error;
    outcome = aige::ApplyOperations({bad[1]}, v.registry, aige::EngineReflection(), prefabs, project);
    ASSERT_FALSE(outcome.errors.empty());
    EXPECT_NE(outcome.errors[0].find("already a marker called home"), std::string::npos);
}
