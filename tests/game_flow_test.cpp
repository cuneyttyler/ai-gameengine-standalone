#include <gtest/gtest.h>

#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "game/GameFlow.h"
#include "game/GameRunner.h"
#include "game/SaveGames.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "scripting/LuaVM.h"

#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

// development_plan.md 12.4 (game flow) and 12.5 (saved games v2).
namespace {

aige::InputState Press(const std::set<aige::Key>& keys, const std::string& context, aige::InputState state = {}) {
    auto map = aige::InputMap::Defaults();
    state.SetContext(context);
    state.Update(map, [](aige::Key) { return false; }); // settle into the context, nothing held
    state.Update(map, [&](aige::Key k) { return keys.contains(k); });
    return state;
}

class SavesTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("aige_saves_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        fs::create_directories(root_ / "scenes");
        savedSettings_ = aige::ActiveSettings();
    }
    void TearDown() override {
        aige::ActiveSettings() = savedSettings_;
        fs::remove_all(root_);
    }
    fs::path root_;
    aige::ProjectSettings savedSettings_;
};

} // namespace

TEST(GameFlowTest, TitleMenusAndCommands) {
    aige::GameFlow flow;
    aige::GameManifest manifest;
    manifest.title = "The Moon Amulet";
    flow.SetManifest(manifest);
    EXPECT_EQ(flow.Begin().kind, aige::GameFlow::Command::Kind::None);
    EXPECT_EQ(flow.Current(), aige::GameFlow::State::Title);
    auto view = flow.View();
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->title, "The Moon Amulet");
    EXPECT_FALSE(view->items[0].enabled); // Continue: no saves yet
    EXPECT_EQ(view->highlighted, 1);      // New game

    auto command = flow.HandleInput(Press({aige::Key::Enter}, "menu"));
    EXPECT_EQ(command.kind, aige::GameFlow::Command::Kind::NewGame);
    EXPECT_TRUE(flow.InPlay());
    EXPECT_FALSE(flow.View().has_value());

    flow.Pause();
    EXPECT_EQ(flow.View()->title, "Paused");
    command = flow.HandleInput(Press({aige::Key::Escape}, "menu"));
    EXPECT_EQ(command.kind, aige::GameFlow::Command::Kind::Resume);

    // With saves: Continue picks the newest; Save lists the player's slots only.
    flow.SetSlots({{"quick", true, "2026-10-21 10:00", "Riverford", 60},
                   {"auto", true, "2026-10-21 11:00", "Crypt", 90},
                   {"slot1", false}});
    flow.Pause();
    flow.Choose(1); // Save game
    EXPECT_EQ(flow.Current(), aige::GameFlow::State::Save);
    auto items = flow.View()->items;
    EXPECT_EQ(items.size(), 2u); // slot1 and Back
    command = flow.Choose(0);
    EXPECT_EQ(command.kind, aige::GameFlow::Command::Kind::Save);
    EXPECT_EQ(command.slot, "slot1");
    EXPECT_EQ(flow.Current(), aige::GameFlow::State::Paused);

    flow.OnPlayerDied();
    command = flow.Choose(0); // Load last save
    EXPECT_EQ(command.kind, aige::GameFlow::Command::Kind::Load);
    EXPECT_EQ(command.slot, "auto");
}

TEST(GameFlowTest, NoTitleScreenStartsPlaying) {
    aige::GameFlow flow;
    aige::GameManifest manifest;
    manifest.titleScreen = false;
    flow.SetManifest(manifest);
    EXPECT_EQ(flow.Begin().kind, aige::GameFlow::Command::Kind::NewGame);
    EXPECT_TRUE(flow.InPlay());
}

TEST_F(SavesTest, ManifestRoundTripsAndDefaultsToTheFolderName) {
    auto manifest = aige::LoadGameManifest(root_);
    EXPECT_EQ(manifest.title, root_.filename().string());
    EXPECT_TRUE(manifest.titleScreen);
    manifest.title = "Moon";
    manifest.titleScreen = false;
    manifest.systems = {{"combat", true}};
    ASSERT_TRUE(aige::SaveGameManifest(root_, manifest));
    auto back = aige::LoadGameManifest(root_);
    EXPECT_EQ(back.title, "Moon");
    EXPECT_FALSE(back.titleScreen);
    EXPECT_TRUE(back.systems["combat"].get<bool>());
}

TEST_F(SavesTest, SlotsWriteListReadAndReadOldQuicksaves) {
    aige::SaveGames saves(root_ / "saves");
    EXPECT_FALSE(saves.List()[0].used);
    ASSERT_TRUE(saves.Write("slot2", {{"session", {{"scene", "scenes/a.aige"}}}, {"meta", {{"place", "Riverford"}, {"playSeconds", 3720}}}}));
    auto list = saves.List();
    auto slot2 = std::find_if(list.begin(), list.end(), [](const auto& s) { return s.slot == "slot2"; });
    ASSERT_NE(slot2, list.end());
    EXPECT_TRUE(slot2->used);
    EXPECT_EQ(slot2->place, "Riverford");
    EXPECT_EQ(slot2->time.size(), 16u);
    EXPECT_EQ((*saves.Read("slot2"))["schema"], "aige.save/2");

    std::ofstream(root_ / "saves" / "quicksave.json") << R"({"version": 1, "scene": "scenes/old.aige", "clock": 600})";
    auto old = saves.Read("quick");
    ASSERT_TRUE(old.has_value());
    EXPECT_EQ((*old)["session"]["scene"], "scenes/old.aige");
    EXPECT_TRUE(saves.List()[0].used);
}

TEST_F(SavesTest, WorldStateRemembersRemovedChangedAndSpawned) {
    aige::Registry original;
    aige::CreatePlayer(original, 1.5f, 1.5f);
    aige::CreateEnemy(original, 4.5f, 4.5f, entt::null, "");
    aige::CreateEnemy(original, 6.5f, 4.5f, entt::null, "");
    const std::string text = aige::SerializeScene(original);

    aige::Registry live;
    aige::DeserializeScene(live, text);
    std::vector<aige::Entity> enemies;
    for (auto [e, faction] : live.view<aige::Faction>().each()) {
        if (faction.team == aige::Team::Enemy) enemies.push_back(e);
    }
    ASSERT_EQ(enemies.size(), 2u);
    const int keptIndex = live.get<aige::SceneOrigin>(enemies[1]).index;
    live.destroy(enemies[0]);                          // killed
    live.get<aige::Health>(enemies[1]).current = 1.0f; // hurt
    live.get<aige::WorldTransform2D>(enemies[1]).x = 9.5f;
    aige::CreateEnemy(live, 2.5f, 7.5f, entt::null, ""); // spawned in play (no SceneOrigin)

    aige::Registry pristine;
    aige::DeserializeScene(pristine, text);
    auto state = aige::CaptureSceneState(live, pristine, aige::EngineReflection());
    EXPECT_EQ(state["removed"].size(), 1u);
    EXPECT_TRUE(state.contains("spawned"));
    ASSERT_TRUE(state["changed"].contains(std::to_string(keptIndex)));

    aige::Registry restored;
    aige::DeserializeScene(restored, text);
    aige::ApplySceneState(restored, nlohmann::json::parse(state.dump()), aige::EngineReflection());
    int enemyCount = 0;
    float hurtHealth = -1.0f, hurtX = 0.0f;
    for (auto [e, faction, health, at] : restored.view<aige::Faction, aige::Health, aige::WorldTransform2D>().each()) {
        if (faction.team != aige::Team::Enemy) continue;
        ++enemyCount;
        if (restored.all_of<aige::SceneOrigin>(e) && restored.get<aige::SceneOrigin>(e).index == keptIndex) {
            hurtHealth = health.current;
            hurtX = at.x;
        }
    }
    EXPECT_EQ(enemyCount, 2); // one killed, one kept, one spawned
    EXPECT_FLOAT_EQ(hurtHealth, 1.0f);
    EXPECT_FLOAT_EQ(hurtX, 9.5f);
}

TEST_F(SavesTest, TheRunnerSavesAndLoadsAWorldThatRemembers) {
    // A scene with a player and two enemies; the start scene.
    {
        aige::Registry scene;
        aige::CreatePlayer(scene, 1.5f, 1.5f);
        aige::CreateEnemy(scene, 5.5f, 5.5f, entt::null, "");
        aige::CreateEnemy(scene, 7.5f, 5.5f, entt::null, "");
        aige::SaveScene(scene, (root_ / "scenes/main.aige").string());
    }
    aige::ActiveSettings().game.startScene = "scenes/main.aige";
    std::ofstream(root_ / "game.json") << R"({"title": "Test", "titleScreen": false, "autosave": false})";
    aige::Project project{root_};
    aige::PrefabLibrary prefabs(project);
    aige::LuaVM lua;
    aige::Registry registry;
    aige::GameRunnerOptions options;
    options.saveDir = root_ / "saves";
    aige::GameRunner game(project, registry, lua, prefabs, options);
    game.Start();
    EXPECT_TRUE(game.Flow().InPlay());

    auto countEnemies = [&] {
        int n = 0;
        for (auto [e, faction] : registry.view<aige::Faction>().each()) n += faction.team == aige::Team::Enemy;
        return n;
    };
    ASSERT_EQ(countEnemies(), 2);
    for (auto [e, faction] : registry.view<aige::Faction>().each()) {
        if (faction.team == aige::Team::Enemy) {
            registry.destroy(e);
            break;
        }
    }
    auto player = *aige::FindPlayer(registry);
    registry.get<aige::Health>(player).current = 3.0f;
    ASSERT_TRUE(game.SaveTo("slot1"));

    game.NewGame(); // a fresh start has both enemies again
    EXPECT_EQ(countEnemies(), 2);
    ASSERT_TRUE(game.LoadFrom("slot1"));
    EXPECT_EQ(countEnemies(), 1); // the killed one stays dead
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(*aige::FindPlayer(registry)).current, 3.0f);

    // Escape pauses; quitting from the pause menu asks to quit.
    std::set<aige::Key> held = {aige::Key::Escape};
    auto keys = [&](aige::Key k) { return held.contains(k); };
    game.Frame(1.0 / 60.0, keys);
    EXPECT_EQ(game.Flow().Current(), aige::GameFlow::State::Paused);
    EXPECT_EQ(game.Flow().Choose(5).kind, aige::GameFlow::Command::Kind::Quit); // Quit game
}
