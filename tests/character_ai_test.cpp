#include <gtest/gtest.h>

#include "game/Actors.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Behaviour.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/GameSession.h"
#include "gameplay/Paths.h"
#include "gameplay/Story.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"
#include "records/Records.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"
#include "scripting/LuaVM.h"

#include <cmath>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// development_plan.md Phase 14: packages, procedures, smart objects, perception,
// combat behaviour, scripted packages and persistent actors (ai-packages.md).
namespace {

void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

class AiTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("aige_ai_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        Write(root_ / "records/package/work-day.json",
              R"({"template": "work", "location": {"marker": "shop"}, "schedule": {"from": "08:00", "to": "20:00"},
                  "data": {"object": "counter"}})");
        Write(root_ / "records/package/sleep-night.json", R"({"template": "sleep", "location": {"alias": "bed"}})");
        Write(root_ / "records/package/rounds.json",
              R"({"template": "patrol", "data": {"path": ["a", "b"], "duration": 0.5}})");
        Write(root_ / "scripts/ghost.lua", R"(
            function run()
                move_to(8.5, 3.5)
                set_field(self_id(), "Name", "value", "arrived")
            end)");
        Write(root_ / "records/package/haunt.json", R"({"template": "script", "data": {"script": "scripts/ghost.lua"}})");
        Write(root_ / "records/package/elsewhere.json", R"({"template": "stay", "location": {"marker": "x", "scene": "scenes/b.aige"}})");
        Write(root_ / "records/combat-style/coward.json", R"({"range": "melee", "fleeAt": 0.5})");
        // A place by a thing's name, and a package with its own procedure (docs/ai/ai-packages.md §4.1, §5.1).
        Write(root_ / "records/package/to-well.json", R"({"template": "travel", "location": {"building": "Well"}})");
        // A named path walked once (docs/ai/ai-paths.md).
        Write(root_ / "records/package/walk-lane.json", R"({"template": "follow-path", "data": {"route": "lane", "mode": "once", "from": "start"}})");
        Write(root_ / "records/package/own-walk.json",
              R"({"template": "custom", "procedure": "{\"type\": \"sequence\", \"children\": [{\"type\": \"move-to\", \"to\": \"b\"}, {\"type\": \"play\", \"clip\": \"wave\", \"seconds\": 0.2}]}"})");
        aige::ActiveRecords().Open(aige::EngineReflection(), root_);
        ASSERT_TRUE(aige::ActiveRecords().Problems().empty()) << aige::ActiveRecords().Problems()[0].message;
        for (int x = 0; x < 20; ++x) {
            for (int y = 0; y < 20; ++y) aige::CreateTile(registry_, x, y, aige::TileKind::Grass);
        }
    }
    void TearDown() override {
        aige::ActiveRecords().Close();
        fs::remove_all(root_);
    }

    aige::Entity MarkerAt(const std::string& name, float x, float y) {
        auto e = registry_.create();
        registry_.emplace<aige::Marker>(e, name);
        registry_.emplace<aige::WorldTransform2D>(e, x, y);
        return e;
    }
    aige::Entity Object(const std::string& type, const std::string& affordance, float x, float y) {
        auto e = registry_.create();
        registry_.emplace<aige::WorldTransform2D>(e, x, y);
        auto& so = registry_.emplace<aige::SmartObject>(e);
        so.type = type;
        so.affordances = affordance;
        so.minDuration = so.maxDuration = 1e6f;
        return e;
    }
    aige::Entity Villager(float x, float y, const std::string& packages) {
        auto e = registry_.create();
        registry_.emplace<aige::WorldTransform2D>(e, x, y);
        registry_.emplace<aige::Collider2D>(e);
        registry_.emplace<aige::Sprite>(e);
        registry_.emplace<aige::Name>(e, "Mira");
        registry_.emplace<aige::Behaviour>(e).packages = packages;
        return e;
    }
    // Runs the AI for `seconds` at `minute` of the day.
    void Run(float seconds, float minute = 600.0f) {
        auto grid = aige::TileGrid::Build(registry_);
        aige::SolidQueryFn isSolid = [&](int x, int y) { return grid.IsSolid(x, y); };
        for (float t = 0.0f; t < seconds; t += 1.0f / 60.0f) {
            aige::AiContext ctx{registry_, isSolid};
            ctx.minuteOfDay = minute;
            ctx.story = &story_;
            ctx.rng = &rng_;
            ctx.bark = [&](aige::Entity, const std::string& text) { barks_.push_back(text); };
            aige::UpdateBehaviours(ctx, 1.0f / 60.0f);
            aige::UpdateWorldToScreen(registry_, aige::IsoParams{});
        }
    }
    float DistanceTo(aige::Entity e, float x, float y) {
        const auto& w = registry_.get<aige::WorldTransform2D>(e);
        return std::hypot(w.x - x, w.y - y);
    }

    fs::path root_;
    aige::Registry registry_;
    aige::StoryState story_;
    std::mt19937 rng_{11};
    std::vector<std::string> barks_;
};

} // namespace

TEST_F(AiTest, TemplatesParseAndRolesGiveAffordances) {
    for (const char* name : {"stay", "travel", "wander", "sandbox", "sleep", "work", "vendor", "patrol", "guard",
                             "follow", "escort", "flee", "ambush", "approach-player", "script"}) {
        ASSERT_NE(aige::FindProcedure(name), nullptr) << name;
    }
    EXPECT_EQ(aige::FindProcedure("sandbox")->nodes[0].type, "loop");
    EXPECT_FALSE(aige::ParseProcedure(nlohmann::json::object()).has_value());
    EXPECT_EQ(aige::AffordancesForRole("bed"), "sleep");
    EXPECT_EQ(aige::AffordancesForRole("house"), "");
}

TEST_F(AiTest, NewTemplatesAndAPackagesOwnProcedure) {
    for (const char* name : {"live", "use-part", "tend"}) ASSERT_NE(aige::FindProcedure(name), nullptr) << name;
    const auto* record = aige::ActiveRecords().Find("package", "own-walk");
    ASSERT_NE(record, nullptr);
    const aige::Procedure* own = aige::PackageProcedure("own-walk", record->resolved);
    ASSERT_NE(own, nullptr);
    EXPECT_EQ(own->nodes[0].type, "sequence"); // its own tree, not the template's
    // It runs: walks to the marker.
    MarkerAt("b", 12.5f, 3.5f);
    auto walker = Villager(3.5f, 3.5f, "own-walk");
    Run(10.0f);
    EXPECT_LT(DistanceTo(walker, 12.5f, 3.5f), 0.8f);
}

TEST_F(AiTest, APlaceByAThingsName) {
    auto well = registry_.create();
    registry_.emplace<aige::WorldTransform2D>(well, 12.5f, 12.5f);
    registry_.emplace<aige::Name>(well, "Well");
    auto walker = Villager(3.5f, 3.5f, "to-well");
    Run(12.0f);
    EXPECT_LT(DistanceTo(walker, 12.5f, 12.5f), 0.8f);
}

TEST_F(AiTest, TheScheduleChoosesWorkByDayAndBedByNight) {
    MarkerAt("shop", 10.5f, 10.5f);
    MarkerAt("home", 3.5f, 3.5f);
    auto counter = Object("counter", "work", 11.5f, 10.5f);
    auto bed = Object("bed", "sleep", 3.5f, 4.5f);
    auto mira = Villager(5.5f, 5.5f, "work-day, sleep-night");
    registry_.get<aige::Behaviour>(mira).home = "home";

    Run(8.0f, 600.0f); // 10:00
    EXPECT_EQ(registry_.get<aige::BehaviourState>(mira).package, "work-day");
    EXPECT_LT(DistanceTo(mira, 11.5f, 10.5f), 1.3f);
    EXPECT_EQ(registry_.get<aige::BehaviourState>(mira).using_, counter);

    Run(8.0f, 23 * 60.0f); // 23:00
    EXPECT_EQ(registry_.get<aige::BehaviourState>(mira).package, "sleep-night");
    EXPECT_EQ(registry_.get<aige::BehaviourState>(mira).using_, bed);
    EXPECT_TRUE(registry_.all_of<aige::Sleeping>(mira)); // in bed, out of sight
    EXPECT_FLOAT_EQ(registry_.get<aige::Sprite>(mira).a, 0.0f);

    Run(1.0f, 600.0f); // morning: up again
    EXPECT_FALSE(registry_.all_of<aige::Sleeping>(mira));
}

TEST_F(AiTest, PatrolsWalkBetweenMarkers) {
    MarkerAt("a", 3.5f, 3.5f);
    MarkerAt("b", 12.5f, 3.5f);
    auto guard = Villager(3.5f, 3.5f, "rounds");
    bool reachedB = false, backAtA = false;
    for (int i = 0; i < 40; ++i) {
        Run(0.5f);
        if (DistanceTo(guard, 12.5f, 3.5f) < 0.6f) reachedB = true;
        if (reachedB && DistanceTo(guard, 3.5f, 3.5f) < 0.6f) backAtA = true;
    }
    EXPECT_TRUE(reachedB);
    EXPECT_TRUE(backAtA);
}

TEST_F(AiTest, AnAggressiveEnemyNoticesChasesAndAttacks) {
    auto hero = aige::CreatePlayer(registry_, 10.5f, 10.5f);
    registry_.get<aige::Health>(hero).current = registry_.get<aige::Health>(hero).max = 100.0f;
    auto bandit = aige::CreateEnemy(registry_, 5.5f, 10.5f, entt::null, "");
    registry_.remove<aige::Follow>(bandit); // the AI moves it, not Follow
    auto& behaviour = registry_.emplace<aige::Behaviour>(bandit);
    behaviour.aggression = aige::Aggression::Aggressive;
    registry_.emplace<aige::Perception>(bandit);
    registry_.emplace_or_replace<aige::Facing>(bandit, 1.0f, 0.0f);
    registry_.emplace<aige::Barks>(bandit).alert = "There you are!";
    Run(2.0f);
    EXPECT_EQ(registry_.get<aige::BehaviourState>(bandit).mode, aige::BehaviourState::Mode::Combat);
    EXPECT_EQ(barks_.front(), "There you are!");
    Run(3.0f);
    EXPECT_LT(DistanceTo(bandit, 10.5f, 10.5f), 1.6f); // closed in
}

TEST_F(AiTest, AWallHidesThePlayer) {
    for (auto [e, tile] : registry_.view<aige::Tile>().each()) {
        if (tile.gx == 8) tile.solid = true; // a wall down the middle
    }
    aige::CreatePlayer(registry_, 12.5f, 10.5f);
    auto bandit = aige::CreateEnemy(registry_, 5.5f, 10.5f, entt::null, "");
    registry_.remove<aige::Follow>(bandit);
    registry_.emplace<aige::Behaviour>(bandit).aggression = aige::Aggression::Aggressive;
    registry_.emplace_or_replace<aige::Facing>(bandit, 1.0f, 0.0f);
    Run(3.0f);
    EXPECT_EQ(registry_.get<aige::BehaviourState>(bandit).mode, aige::BehaviourState::Mode::Package);
}

TEST_F(AiTest, HurtCowardsFleeAndDefendersFightBack) {
    auto hero = aige::CreatePlayer(registry_, 10.5f, 10.5f);
    auto peasant = Villager(9.5f, 10.5f, "");
    registry_.emplace<aige::Faction>(peasant, aige::Team::Enemy);
    registry_.emplace<aige::Health>(peasant, 10.0f, 10.0f);
    auto& b = registry_.get<aige::Behaviour>(peasant);
    b.aggression = aige::Aggression::Defensive;
    b.combatStyle = "coward";
    aige::AlertActor(registry_, peasant, hero);
    EXPECT_EQ(registry_.get<aige::BehaviourState>(peasant).mode, aige::BehaviourState::Mode::Combat);
    registry_.get<aige::Health>(peasant).current = 4.0f; // below half
    Run(2.0f);
    EXPECT_EQ(registry_.get<aige::BehaviourState>(peasant).mode, aige::BehaviourState::Mode::Flee);
    EXPECT_GT(DistanceTo(peasant, 10.5f, 10.5f), 3.0f);
}

TEST_F(AiTest, AScriptedPackageRunsItsLua) {
    aige::LuaVM lua;
    auto ghost = Villager(3.5f, 3.5f, "haunt");
    aige::GameSession session(registry_, lua, aige::IsoParams{}, root_);
    for (int i = 0; i < 360 && registry_.get<aige::Name>(ghost).value != "arrived"; ++i) {
        session.Advance(aige::GameSession::kFixedDt, [](aige::Key) { return false; });
    }
    EXPECT_TRUE(session.ScriptErrors().empty()) << session.ScriptErrors()[0].message;
    EXPECT_EQ(registry_.get<aige::Name>(ghost).value, "arrived");
    EXPECT_LT(DistanceTo(ghost, 8.5f, 3.5f), 0.6f);
}

TEST_F(AiTest, ActorsAreOnePersonAcrossScenes) {
    aige::ActorRoster roster;
    auto spawnCount = 0;
    auto spawn = [&](const std::string&, float x, float y) -> std::optional<aige::Entity> {
        ++spawnCount;
        auto e = registry_.create();
        registry_.emplace<aige::WorldTransform2D>(e, x, y);
        return e;
    };
    // Scene a: Mira is there.
    auto mira = Villager(4.5f, 4.5f, "");
    registry_.emplace<aige::Actor>(mira, "character/mira", "prefabs/mira.prefab");
    roster.Enter(registry_, "scenes/a.aige", spawn);
    ASSERT_TRUE(roster.Entries().contains("character/mira"));
    // Her day takes her to scene b: leaving a records that.
    registry_.emplace_or_replace<aige::BehaviourState>(mira).package = "elsewhere";
    roster.Leave(registry_, "scenes/a.aige");
    EXPECT_EQ(roster.Entries().at("character/mira").scene, "scenes/b.aige");
    // Back in a (a copy placed there too) she's gone; in b she arrives.
    roster.Enter(registry_, "scenes/a.aige", spawn);
    EXPECT_FALSE(registry_.valid(mira));
    registry_.clear();
    MarkerAt("x", 6.5f, 6.5f);
    roster.Enter(registry_, "scenes/b.aige", spawn);
    EXPECT_EQ(spawnCount, 1);
    // Dead is dead, wherever she was placed.
    roster.OnDeath("character/mira");
    auto copy = Villager(1.5f, 1.5f, "");
    registry_.emplace<aige::Actor>(copy, "character/mira", "prefabs/mira.prefab");
    roster.Enter(registry_, "scenes/b.aige", spawn);
    EXPECT_FALSE(registry_.valid(copy));
    auto saved = roster.ToJson();
    aige::ActorRoster back;
    back.FromJson(saved);
    EXPECT_FALSE(back.Entries().at("character/mira").alive);
}

// docs/ai/ai-paths.md: a path read and written, forks chosen by conditions, priority, weight and default.
TEST_F(AiTest, PathsForkByConditionsPriorityAndWeight) {
    aige::Path path;
    path.name = "lane";
    path.points = R"([{"x": 0, "y": 0}, {"x": 5, "y": 0, "wait": 1}, {"x": 5, "y": 5}])";
    aige::PathGraph graph = aige::ReadPath(path, 3.5f, 3.5f);
    ASSERT_EQ(graph.points.size(), 3u);
    EXPECT_FLOAT_EQ(graph.points[1].x, 8.5f); // from its own place
    ASSERT_EQ(graph.links.size(), 2u);        // in order
    path.end = "loop";
    EXPECT_EQ(aige::ReadPath(path).links.size(), 3u); // and round
    aige::Path written;
    aige::WritePath(aige::ReadPath(path, 3.5f, 3.5f), written, 3.5f, 3.5f);
    EXPECT_TRUE(written.links.empty()); // links in order aren't written
    EXPECT_TRUE(aige::CheckPath(graph).empty());

    // A fork: a conditional link, a weighted pair, a default.
    aige::PathGraph fork;
    fork.points.resize(4);
    fork.links = {{0, 1, 3.0f, "", false, false, "", "market"}, {0, 2, 1.0f, "", false, false, "", "well"},
                  {0, 3, 1.0f, R"([{"flag": "never"}])", false, true, "", "home"}};
    std::mt19937 rng{3};
    auto holds = [](const aige::PathLink& l) { return l.conditions.empty(); };
    int market = 0;
    for (int i = 0; i < 400; ++i) market += aige::ChooseLink(fork, {0, 1, 2}, holds, rng) == 0;
    EXPECT_GT(market, 250); // 3:1
    EXPECT_LT(market, 350);
    // A package's own preference by label.
    int well = 0;
    for (int i = 0; i < 200; ++i) well += aige::ChooseLink(fork, {0, 1, 2}, holds, rng, {{"well", 100.0}, {"market", 0.0}}) == 1;
    EXPECT_GT(well, 190);
    // None holds: the default (whatever its conditions); a priority one whenever it holds.
    EXPECT_EQ(aige::ChooseLink(fork, {0, 1, 2}, [](const aige::PathLink&) { return false; }, rng), 2);
    fork.links[1].priority = true;
    EXPECT_EQ(aige::ChooseLink(fork, {0, 1, 2}, holds, rng), 1);
    // The path's own conditions, and dialogue's handed on.
    bool asked = false;
    EXPECT_FALSE(aige::LinkConditionsHold(R"([{"playerNear": 4}, {"flag": "x"}])", [](const nlohmann::json&) { return false; },
                                          [&](const std::string&) { return asked = true; }));
    EXPECT_FALSE(asked);
    EXPECT_TRUE(aige::LinkConditionsHold(R"([{"flag": "x"}])", nullptr, [&](const std::string& text) { return text.find("flag") != std::string::npos; }));
    // Problems.
    fork.links.push_back({0, 9});
    EXPECT_FALSE(aige::CheckPath(fork).empty());
}

// docs/ai/ai-paths.md §4: characters walk a named path, each its own way at a fork: a link that needs the player near
// is taken (it goes first) only when the player is.
TEST_F(AiTest, CharactersFollowANamedPathAndChooseAtForks) {
    auto lane = registry_.create();
    registry_.emplace<aige::WorldTransform2D>(lane, 3.5f, 3.5f);
    auto& path = registry_.emplace<aige::Path>(lane);
    path.name = "lane";
    path.points = R"([{"x": 0, "y": 0}, {"x": 5, "y": 0}, {"x": 5, "y": 6}, {"x": 10, "y": 0}])";
    path.links = R"([{"from": 0, "to": 1}, {"from": 1, "to": 2, "priority": true, "if": [{"playerNear": 40}]}, {"from": 1, "to": 3}])";
    auto mira = Villager(3.5f, 3.5f, "walk-lane");
    Run(14.0f);
    EXPECT_LT(DistanceTo(mira, 13.5f, 3.5f), 0.8f); // no player: the other way
    EXPECT_EQ(registry_.get<aige::BehaviourState>(mira).blackboard["path"], "lane");

    // With the player about: the first way. Two walkers share the path.
    aige::CreatePlayer(registry_, 1.5f, 15.5f);
    registry_.get<aige::WorldTransform2D>(mira) = {3.5f, 3.5f};
    registry_.get<aige::BehaviourState>(mira).nodeState.clear();
    auto second = Villager(3.5f, 4.5f, "walk-lane");
    Run(16.0f);
    EXPECT_LT(DistanceTo(mira, 8.5f, 9.5f), 0.8f);
    EXPECT_LT(DistanceTo(second, 8.5f, 9.5f), 1.6f);
}

// A package's fields by its template (the Records panel shows only those; the AI's schema says which): a route only for
// follow-path, patrol and custom; markers only for patrol; its own procedure only for custom.
TEST_F(AiTest, PackageFieldsBelongToTheirTemplates) {
    const auto* type = aige::EngineReflection().FindRecordType("package");
    ASSERT_NE(type, nullptr);
    const nlohmann::json json = [&] {
        nlohmann::json all = nlohmann::json::array();
        for (const auto& f : type->fields) all.push_back(aige::RecordFieldToJson(f));
        return all;
    }();
    auto field = [&](const std::string& name) {
        for (const auto& f : json) {
            if (f.value("name", "") == name) return f;
        }
        return nlohmann::json();
    };
    EXPECT_EQ(field("procedure")["when"]["values"], nlohmann::json::array({"custom"}));
    EXPECT_FALSE(field("schedule").contains("when")); // every template
    const nlohmann::json data = field("data");
    for (const auto& sub : data["fields"]) {
        const auto values = sub["when"]["values"];
        if (sub["name"] == "route") EXPECT_EQ(values, nlohmann::json::array({"follow-path", "patrol", "custom"}));
        if (sub["name"] == "path") EXPECT_EQ(values, nlohmann::json::array({"patrol"}));
        if (sub["name"] == "mode") EXPECT_EQ(values, nlohmann::json::array({"follow-path"}));
    }
}
