#include <gtest/gtest.h>

#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "ecs/Registry.h"
#include "editing/Commands.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/GameSession.h"
#include "gameplay/Settings.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

class CommandsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("aige_commands_test_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        fs::create_directories(root_);
        project_ = aige::Project{root_};
        prefabs_ = std::make_unique<aige::PrefabLibrary>(project_);
        aige::RegisterEditingCommands(aige::EngineReflection());

        aige::Registry scratch;
        prefabs_->SaveAsPrefab(scratch, aige::CreateTile(scratch, 0, 0, aige::TileKind::Grass), "prefabs/grass.prefab");
        prefabs_->SaveAsPrefab(scratch, aige::CreateEnemy(scratch, 0.0f, 0.0f, entt::null, ""), "prefabs/enemy.prefab");
        prefabs_->SaveAsPrefab(scratch, aige::CreatePlayer(scratch, 0.0f, 0.0f), "prefabs/hero.prefab");
    }
    void TearDown() override { fs::remove_all(root_); }

    aige::CommandResult Run(const std::string& command, const std::vector<aige::FieldValue>& args) {
        aige::CommandContext context{registry_, prefabs_.get(), &project_};
        return aige::EngineReflection().Run(context, command, args);
    }

    fs::path root_;
    aige::Project project_;
    std::unique_ptr<aige::PrefabLibrary> prefabs_;
    aige::Registry registry_;
};

} // namespace

TEST_F(CommandsTest, RegistrationIsIdempotent) {
    size_t count = aige::EngineReflection().Commands().size();
    aige::RegisterEditingCommands(aige::EngineReflection());
    EXPECT_EQ(aige::EngineReflection().Commands().size(), count);
    for (const auto& command : aige::EngineReflection().Commands()) {
        EXPECT_FALSE(command.description.empty()) << command.name;
        for (const auto& param : command.params) {
            EXPECT_FALSE(param.description.empty()) << command.name << "." << param.name;
        }
    }
}

TEST_F(CommandsTest, SpawnPlacesPrefabCopies) {
    auto result = Run("scene.spawn", {std::string("prefabs/enemy.prefab"), 3.5f, 2.5f});
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_TRUE(result.created.has_value());
    EXPECT_FLOAT_EQ(registry_.get<aige::WorldTransform2D>(*result.created).x, 3.5f);
    EXPECT_EQ(registry_.get<aige::PrefabInstance>(*result.created).path, "prefabs/enemy.prefab");

    EXPECT_FALSE(Run("scene.spawn", {std::string("prefabs/dragon.prefab"), 0.0f, 0.0f}).ok);
    EXPECT_FALSE(Run("scene.spawn", {std::string(""), 0.0f, 0.0f}).ok); // required parameter
}

TEST_F(CommandsTest, SpawningThePlayerAgainMovesIt) {
    auto first = Run("scene.spawn", {std::string("prefabs/hero.prefab"), 1.5f, 1.5f});
    auto second = Run("scene.spawn", {std::string("prefabs/hero.prefab"), 6.5f, 2.5f});
    ASSERT_TRUE(first.ok && second.ok);
    EXPECT_EQ(*first.created, *second.created);
    EXPECT_FLOAT_EQ(registry_.get<aige::WorldTransform2D>(*first.created).x, 6.5f);
}

TEST_F(CommandsTest, PaintRectFillsCellsWithTiles) {
    ASSERT_TRUE(Run("scene.paintRect", {std::string("prefabs/grass.prefab"), 3, 2, 0, 0}).ok); // any corner order
    EXPECT_EQ(registry_.view<aige::Tile>().size(), 12u);
    aige::TileGrid grid = aige::TileGrid::Build(registry_);
    EXPECT_FALSE(grid.IsSolid(3, 2));
    EXPECT_TRUE(grid.IsSolid(4, 2));

    EXPECT_FALSE(Run("scene.paintRect", {std::string("prefabs/enemy.prefab"), 0, 0, 1, 1}).ok); // not a tile
    EXPECT_FALSE(Run("scene.paintRect", {std::string("prefabs/grass.prefab"), 0, 0, 999, 999}).ok); // too big
}

TEST_F(CommandsTest, DeleteRemovesEntities) {
    auto spawned = Run("scene.spawn", {std::string("prefabs/enemy.prefab"), 1.5f, 1.5f});
    ASSERT_TRUE(Run("entity.delete", {*spawned.created}).ok);
    EXPECT_FALSE(registry_.valid(*spawned.created));
    EXPECT_FALSE(Run("entity.delete", {*spawned.created}).ok);
}

TEST_F(CommandsTest, CreatePrefabFromEntity) {
    auto npc = aige::CreateNpc(registry_, 2.5f, 2.5f);
    ASSERT_TRUE(Run("prefab.createFromEntity", {npc, std::string("guard")}).ok);
    EXPECT_TRUE(fs::exists(root_ / "prefabs/guard.prefab"));
    EXPECT_EQ(registry_.get<aige::PrefabInstance>(npc).path, "prefabs/guard.prefab");
    EXPECT_FALSE(Run("prefab.createFromEntity", {npc, std::string("guard")}).ok);     // exists
    EXPECT_FALSE(Run("prefab.createFromEntity", {npc, std::string("../evil")}).ok);   // unsafe name
}

TEST_F(CommandsTest, ScriptWriteStaysInsideScripts) {
    ASSERT_TRUE(Run("script.write", {std::string("scripts/ai/boss.lua"), std::string("function update(dt) end\n")}).ok);
    std::ifstream in(root_ / "scripts/ai/boss.lua");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(text, "function update(dt) end\n");

    for (const char* bad : {"../escape.lua", "scripts/../../escape.lua", "scripts/x.txt", "C:/Windows/x.lua",
                            "assets/x.lua", "scripts\\x.lua"}) {
        EXPECT_FALSE(Run("script.write", {std::string(bad), std::string("--")}).ok) << bad;
    }
}

TEST(SettingsIOTest, SettingsRoundTripThroughTheProjectFile) {
    fs::path file = fs::temp_directory_path() / "aige_settings_test.aige";
    aige::ProjectSettings saved = aige::ActiveSettings();

    aige::ActiveSettings().combat.knockbackScale = 2.5f;
    aige::ActiveSettings().physics.playerCollides = false;
    aige::ActiveSettings().renderer.clearG = 0.5f;
    aige::SaveSettings(aige::EngineReflection(), file.string());

    aige::ActiveSettings() = aige::ProjectSettings{};
    ASSERT_TRUE(aige::LoadSettings(aige::EngineReflection(), file.string()));
    EXPECT_FLOAT_EQ(aige::ActiveSettings().combat.knockbackScale, 2.5f);
    EXPECT_FALSE(aige::ActiveSettings().physics.playerCollides);
    EXPECT_FLOAT_EQ(aige::ActiveSettings().renderer.clearG, 0.5f);

    // Unknown sections/fields and out-of-range values are ignored.
    std::ofstream(file, std::ios::trunc) << "Combat knockbackScale=999 minimumDamage=2 bogus=1\nWeather rain=1\n";
    aige::ActiveSettings() = aige::ProjectSettings{};
    ASSERT_TRUE(aige::LoadSettings(aige::EngineReflection(), file.string()));
    EXPECT_FLOAT_EQ(aige::ActiveSettings().combat.knockbackScale, 1.0f); // 999 > max: default kept
    EXPECT_FLOAT_EQ(aige::ActiveSettings().combat.minimumDamage, 2.0f);

    EXPECT_FALSE(aige::LoadSettings(aige::EngineReflection(), (file.string() + ".missing")));
    aige::ActiveSettings() = saved;
    fs::remove(file);
}

TEST(SettingsIOTest, CombatSettingsChangeTheRules) {
    aige::ProjectSettings saved = aige::ActiveSettings();
    aige::Registry registry;
    auto target = registry.create();
    registry.emplace<aige::Health>(target, 20.0f, 20.0f);
    registry.emplace<aige::Stats>(target, 0.0f, 50.0f, 3.0f); // huge defense

    aige::ActiveSettings().combat.minimumDamage = 3.0f;
    aige::DealDamage(registry, target, 1.0f, entt::null, nullptr);
    EXPECT_FLOAT_EQ(registry.get<aige::Health>(target).current, 17.0f);
    aige::ActiveSettings() = saved;
}
