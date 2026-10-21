#include <gtest/gtest.h>

#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "ecs/Registry.h"
#include "gameplay/Archetypes.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"
#include "scripting/LuaVM.h"
#include "scripting/ScriptSystem.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#ifndef AIGE_SOURCE_DIR
#define AIGE_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;

namespace {

class PrefabTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("aige_prefab_test_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        fs::create_directories(root_ / "scripts");
    }
    void TearDown() override { fs::remove_all(root_); }

    aige::Project project() const { return aige::Project{root_}; }

    // Saves a skeleton-like enemy as prefabs/enemy.prefab from a scratch registry.
    void MakeEnemyPrefab(aige::PrefabLibrary& prefabs) {
        aige::Registry scratch;
        auto enemy = aige::CreateEnemy(scratch, 9.0f, 9.0f, entt::null, "");
        scratch.get<aige::Health>(enemy) = {8.0f, 8.0f};
        ASSERT_TRUE(prefabs.SaveAsPrefab(scratch, enemy, "prefabs/enemy.prefab"));
    }

    // Rewrites the prefab file with a change, as an external edit would.
    void EditPrefabFile(const std::string& from, const std::string& to) {
        fs::path file = root_ / "prefabs/enemy.prefab";
        std::ifstream in(file);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        auto at = text.find(from);
        ASSERT_NE(at, std::string::npos) << from;
        text.replace(at, from.size(), to);
        std::ofstream(file, std::ios::trunc) << text;
        fs::last_write_time(file, fs::file_time_type::clock::now() + std::chrono::seconds(5));
    }

    fs::path root_;
};

void WriteFile(const fs::path& path, const std::string& text) {
    std::ofstream(path, std::ios::trunc) << text;
}

} // namespace

TEST_F(PrefabTest, SaveStripsPositionAndMakesTheEntityAnInstance) {
    aige::PrefabLibrary prefabs(project());
    aige::Registry registry;
    auto npc = aige::CreateNpc(registry, 4.5f, 2.5f);
    ASSERT_TRUE(prefabs.SaveAsPrefab(registry, npc, "prefabs/npc.prefab"));

    std::ifstream in(root_ / "prefabs/npc.prefab");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("WorldTransform2D x=0 y=0"), std::string::npos);
    EXPECT_EQ(text.find("PrefabInstance"), std::string::npos);

    ASSERT_TRUE(registry.all_of<aige::PrefabInstance>(npc));
    EXPECT_EQ(registry.get<aige::PrefabInstance>(npc).path, "prefabs/npc.prefab");
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(npc).x, 4.5f); // the original stays put

    ASSERT_EQ(prefabs.Prefabs().size(), 1u);
    EXPECT_EQ(prefabs.Prefabs()[0].name, "npc");
    EXPECT_FALSE(prefabs.Prefabs()[0].isTile);
}

TEST_F(PrefabTest, InstantiatePlacesCharactersAndPaintsTiles) {
    aige::PrefabLibrary prefabs(project());
    {
        aige::Registry scratch;
        ASSERT_TRUE(prefabs.SaveAsPrefab(scratch, aige::CreateTile(scratch, 0, 0, aige::TileKind::Wall),
                                         "prefabs/wall.prefab"));
    }
    MakeEnemyPrefab(prefabs);
    EXPECT_TRUE(prefabs.Find("prefabs/wall.prefab")->isTile);

    aige::Registry registry;
    aige::CreateTile(registry, 3, 4, aige::TileKind::Grass);
    auto wall = prefabs.Instantiate(registry, "prefabs/wall.prefab", 3.7f, 4.2f);
    ASSERT_TRUE(wall.has_value());
    EXPECT_EQ(registry.view<aige::Tile>().size(), 1u); // replaced, not stacked
    EXPECT_EQ(registry.get<aige::Tile>(*wall).gx, 3);
    EXPECT_EQ(registry.get<aige::Tile>(*wall).gy, 4);
    EXPECT_TRUE(registry.get<aige::Tile>(*wall).solid);

    auto enemy = prefabs.Instantiate(registry, "prefabs/enemy.prefab", 6.5f, 1.5f);
    ASSERT_TRUE(enemy.has_value());
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(*enemy).x, 6.5f);
    EXPECT_EQ(registry.get<aige::Faction>(*enemy).team, aige::Team::Enemy);
    EXPECT_EQ(registry.get<aige::PrefabInstance>(*enemy).path, "prefabs/enemy.prefab");

    EXPECT_FALSE(prefabs.Instantiate(registry, "prefabs/nope.prefab", 0.0f, 0.0f).has_value());
}

TEST_F(PrefabTest, PrefabEditsReachEveryCopyExceptOverriddenFields) {
    aige::PrefabLibrary prefabs(project());
    MakeEnemyPrefab(prefabs);
    aige::Registry registry;
    auto plain = *prefabs.Instantiate(registry, "prefabs/enemy.prefab", 1.5f, 1.5f);
    auto tough = *prefabs.Instantiate(registry, "prefabs/enemy.prefab", 4.5f, 4.5f);
    registry.get<aige::Health>(tough).max = 30.0f;
    aige::AddOverride(registry, tough, "Health.max");

    EditPrefabFile("Health current=8 max=8", "Health current=12 max=12");
    EditPrefabFile("damage=2", "damage=5");
    auto changed = prefabs.Refresh();
    ASSERT_EQ(changed.size(), 1u);
    prefabs.SyncAll(registry, changed[0]);

    EXPECT_FLOAT_EQ(registry.get<aige::Health>(plain).max, 12.0f);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(tough).max, 30.0f);              // overridden: kept
    EXPECT_FLOAT_EQ(registry.get<aige::MeleeAttack>(tough).damage, 5.0f);       // not overridden: updated
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(tough).x, 4.5f);       // position is per copy
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(plain).x, 1.5f);
}

TEST_F(PrefabTest, RemovedComponentsStayRemovedUntilReverted) {
    aige::PrefabLibrary prefabs(project());
    MakeEnemyPrefab(prefabs);
    aige::Registry registry;
    auto enemy = *prefabs.Instantiate(registry, "prefabs/enemy.prefab", 1.5f, 1.5f);

    registry.remove<aige::Follow>(enemy);
    aige::AddOverride(registry, enemy, "-Follow");
    prefabs.Sync(registry, enemy);
    EXPECT_FALSE(registry.all_of<aige::Follow>(enemy));

    prefabs.Revert(registry, enemy, "-Follow");
    EXPECT_TRUE(registry.all_of<aige::Follow>(enemy));
}

TEST_F(PrefabTest, RevertAllRestoresThePrefab) {
    aige::PrefabLibrary prefabs(project());
    MakeEnemyPrefab(prefabs);
    aige::Registry registry;
    auto enemy = *prefabs.Instantiate(registry, "prefabs/enemy.prefab", 1.5f, 1.5f);
    registry.get<aige::Health>(enemy).max = 99.0f;
    registry.get<aige::Stats>(enemy).defense = 7.0f;
    aige::AddOverride(registry, enemy, "Health.max");
    aige::AddOverride(registry, enemy, "Stats.defense");

    prefabs.RevertAll(registry, enemy);
    EXPECT_TRUE(registry.get<aige::PrefabInstance>(enemy).overrides.empty());
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(enemy).max, 8.0f);
    EXPECT_FLOAT_EQ(registry.get<aige::Stats>(enemy).defense, 0.0f);
}

TEST_F(PrefabTest, ApplyingACopyUpdatesThePrefabAndOtherCopies) {
    aige::PrefabLibrary prefabs(project());
    MakeEnemyPrefab(prefabs);
    aige::Registry registry;
    auto a = *prefabs.Instantiate(registry, "prefabs/enemy.prefab", 1.5f, 1.5f);
    auto b = *prefabs.Instantiate(registry, "prefabs/enemy.prefab", 4.5f, 4.5f);
    registry.get<aige::Stats>(a).moveSpeed = 3.5f;
    aige::AddOverride(registry, a, "Stats.moveSpeed");

    ASSERT_TRUE(prefabs.SaveAsPrefab(registry, a, "prefabs/enemy.prefab"));
    prefabs.SyncAll(registry, "prefabs/enemy.prefab");
    EXPECT_TRUE(registry.get<aige::PrefabInstance>(a).overrides.empty());
    EXPECT_FLOAT_EQ(registry.get<aige::Stats>(b).moveSpeed, 3.5f);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(b).x, 4.5f);
}

TEST_F(PrefabTest, InstancesSurviveSceneRoundTrip) {
    aige::PrefabLibrary prefabs(project());
    MakeEnemyPrefab(prefabs);
    aige::Registry registry;
    auto enemy = *prefabs.Instantiate(registry, "prefabs/enemy.prefab", 2.5f, 2.5f);
    aige::AddOverride(registry, enemy, "Health.max");
    aige::AddOverride(registry, enemy, "-Follow");

    aige::Registry loaded;
    aige::DeserializeScene(loaded, aige::SerializeScene(registry));
    auto copy = *loaded.view<aige::PrefabInstance>().begin();
    EXPECT_EQ(loaded.get<aige::PrefabInstance>(copy).path, "prefabs/enemy.prefab");
    EXPECT_TRUE(aige::HasOverride(loaded, copy, "Health.max"));
    EXPECT_TRUE(aige::HasOverride(loaded, copy, "-Follow"));
}

TEST_F(PrefabTest, ScriptPropertiesAreDiscoveredWithoutRunningTheGame) {
    WriteFile(root_ / "scripts/brute.lua",
              "local x, y = get_position()   -- gameplay API isn't available here: a no-op\n"
              "find_nearest('Player')\n"
              "properties = { range = 2.5, angry = true, taunt = 'hey you', nested = {} }\n"
              "function update(dt) end\n");
    aige::LuaVM lua;
    std::string error;
    auto properties = aige::ReadScriptProperties(lua, root_ / "scripts/brute.lua", &error);
    EXPECT_TRUE(error.empty()) << error;
    ASSERT_EQ(properties.size(), 3u); // the table isn't an editable property
    EXPECT_EQ(properties[0].name, "angry");
    EXPECT_EQ(std::get<bool>(properties[0].value), true);
    EXPECT_EQ(properties[1].name, "range");
    EXPECT_DOUBLE_EQ(std::get<double>(properties[1].value), 2.5);
    EXPECT_EQ(std::get<std::string>(properties[2].value), "hey you");

    WriteFile(root_ / "scripts/broken.lua", "properties = {");
    EXPECT_TRUE(aige::ReadScriptProperties(lua, root_ / "scripts/broken.lua", &error).empty());
    EXPECT_FALSE(error.empty());
}

TEST_F(PrefabTest, EntityPropertyValuesReachTheRunningScript) {
    WriteFile(root_ / "scripts/speedy.lua",
              "properties = { speed = 1, label = 'default' }\n"
              "function update(dt)\n"
              "  local x, y = get_position()\n"
              "  set_position(x + properties.speed, y)\n"
              "end\n");
    aige::LuaVM lua;
    aige::Registry registry;
    auto withDefault = aige::CreateNpc(registry, 0.0f, 0.0f, "scripts/speedy.lua");
    auto fast = aige::CreateNpc(registry, 0.0f, 0.0f, "scripts/speedy.lua");
    registry.get<aige::LuaScript>(fast).properties["speed"] = 5.0;

    aige::GameSession session(registry, lua, aige::BootstrapIso(), root_);
    session.Advance(aige::GameSession::kFixedDt, [](aige::Key) { return false; });
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(withDefault).x, 1.0f);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(fast).x, 5.0f);
}

TEST_F(PrefabTest, ScriptPropertiesRoundTripWithTheirTypes) {
    aige::Registry registry;
    auto npc = aige::CreateNpc(registry, 0.0f, 0.0f, "scripts/x.lua");
    auto& properties = registry.get<aige::LuaScript>(npc).properties;
    properties["count"] = 3.0;
    properties["enabled"] = false;
    properties["greeting"] = std::string("hello \"world\"");
    properties["numeric_text"] = std::string("42"); // stays a string

    aige::Registry loaded;
    aige::DeserializeScene(loaded, aige::SerializeScene(registry));
    const auto& back = loaded.get<aige::LuaScript>(*loaded.view<aige::LuaScript>().begin()).properties;
    EXPECT_DOUBLE_EQ(std::get<double>(back.at("count")), 3.0);
    EXPECT_EQ(std::get<bool>(back.at("enabled")), false);
    EXPECT_EQ(std::get<std::string>(back.at("greeting")), "hello \"world\"");
    EXPECT_EQ(std::get<std::string>(back.at("numeric_text")), "42");
}

TEST_F(PrefabTest, PrefabScriptPropertiesSyncUnlessOverridden) {
    aige::PrefabLibrary prefabs(project());
    {
        aige::Registry scratch;
        auto e = aige::CreateNpc(scratch, 0.0f, 0.0f, "scripts/x.lua");
        scratch.get<aige::LuaScript>(e).properties["speed"] = 2.0;
        ASSERT_TRUE(prefabs.SaveAsPrefab(scratch, e, "prefabs/walker.prefab"));
    }
    aige::Registry registry;
    auto a = *prefabs.Instantiate(registry, "prefabs/walker.prefab", 1.0f, 1.0f);
    auto b = *prefabs.Instantiate(registry, "prefabs/walker.prefab", 2.0f, 2.0f);
    EXPECT_DOUBLE_EQ(std::get<double>(registry.get<aige::LuaScript>(a).properties.at("speed")), 2.0);
    registry.get<aige::LuaScript>(b).properties["speed"] = 9.0;
    aige::AddOverride(registry, b, "LuaScript.prop.speed");

    {
        aige::Registry scratch;
        auto e = aige::CreateNpc(scratch, 0.0f, 0.0f, "scripts/x.lua");
        scratch.get<aige::LuaScript>(e).properties["speed"] = 4.0;
        ASSERT_TRUE(prefabs.SaveAsPrefab(scratch, e, "prefabs/walker.prefab"));
    }
    prefabs.SyncAll(registry);
    EXPECT_DOUBLE_EQ(std::get<double>(registry.get<aige::LuaScript>(a).properties.at("speed")), 4.0);
    EXPECT_DOUBLE_EQ(std::get<double>(registry.get<aige::LuaScript>(b).properties.at("speed")), 9.0);
}

TEST_F(PrefabTest, ScriptsReadWriteFieldsAndSpawnPrefabs) {
    aige::PrefabLibrary prefabs(project());
    MakeEnemyPrefab(prefabs);
    WriteFile(root_ / "scripts/boss.lua",
              "spawned = 0\n"
              "function update(dt)\n"
              "  local me = self_id()\n"
              "  set_field(me, 'Stats', 'defense', get_field(me, 'Stats', 'defense') + 1)\n"
              "  set_field(me, 'Faction', 'team', 'Neutral')\n"
              "  if get_field(me, 'Faction', 'team') == 'Neutral' and spawned == 0 then\n"
              "    spawned = 1\n"
              "    spawn('prefabs/enemy.prefab', 3.5, 3.5)\n"
              "  end\n"
              "end\n");
    aige::LuaVM lua;
    aige::Registry registry;
    auto boss = aige::CreateEnemy(registry, 1.5f, 1.5f, entt::null, "scripts/boss.lua");
    aige::GameSession session(registry, lua, aige::BootstrapIso(), root_);
    session.SetSpawner([&](const std::string& prefab, float x, float y) {
        return prefabs.Instantiate(registry, prefab, x, y);
    });
    session.Advance(aige::GameSession::kFixedDt * 2.5, [](aige::Key) { return false; });

    EXPECT_FLOAT_EQ(registry.get<aige::Stats>(boss).defense, 2.0f);
    EXPECT_EQ(registry.get<aige::Faction>(boss).team, aige::Team::Neutral);
    EXPECT_EQ(registry.view<aige::PrefabInstance>().size(), 1u); // spawned once
}

