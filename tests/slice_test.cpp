#include <gtest/gtest.h>

#include "assets/AssetDatabase.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "ecs/Registry.h"
#include "gameplay/Archetypes.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/CameraFollow.h"
#include "gameplay/Combat.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"
#include "scripting/LuaVM.h"

#include <filesystem>
#include <string>
#include <vector>

#ifndef AIGE_SOURCE_DIR
#define AIGE_SOURCE_DIR "."
#endif

namespace {

const auto kNoKeys = [](aige::Key) { return false; };

void MakeFloor(aige::Registry& registry, int size) {
    for (int gx = 0; gx < size; ++gx) {
        for (int gy = 0; gy < size; ++gy) {
            aige::CreateTile(registry, gx, gy, aige::TileKind::Grass);
        }
    }
}

std::vector<aige::Entity> LivingEnemies(aige::Registry& registry) {
    std::vector<aige::Entity> enemies;
    for (auto [entity, faction] : registry.view<aige::Faction>(entt::exclude<aige::Dead>).each()) {
        if (faction.team == aige::Team::Enemy) {
            enemies.push_back(entity);
        }
    }
    return enemies;
}

} // namespace

TEST(SliceTest, ObstaclesBlockTheirCell) {
    aige::Registry registry;
    MakeFloor(registry, 4);
    auto rock = aige::CreateProp(registry, 2.5f, 1.5f, "assets/textures/rock.png", 32.0f, 24.0f);
    EXPECT_FALSE(aige::TileGrid::Build(registry).IsSolid(2, 1));
    registry.emplace<aige::Obstacle>(rock);
    aige::TileGrid grid = aige::TileGrid::Build(registry);
    EXPECT_TRUE(grid.IsSolid(2, 1));
    EXPECT_FALSE(grid.IsSolid(1, 1));
    EXPECT_TRUE(grid.At(2, 1) != entt::null); // the tile is still what's in the cell
}

TEST(SliceTest, VictoryOnlyWhenTheLastEnemyDies) {
    aige::Registry registry;
    aige::CreatePlayer(registry, 1.5f, 1.5f);
    auto a = aige::CreateEnemy(registry, 3.5f, 3.5f, entt::null, "");
    auto b = aige::CreateEnemy(registry, 5.5f, 5.5f, entt::null, "");
    auto victories = [&](float dt) {
        std::vector<aige::GameEvent> events;
        aige::UpdateCombat(registry, [](int, int) { return false; }, dt, events);
        return std::count_if(events.begin(), events.end(),
                             [](const auto& e) { return e.type == aige::GameEvent::Type::Victory; });
    };

    registry.get<aige::Health>(a).current = 0.0f;
    EXPECT_EQ(victories(0.016f), 0);
    registry.get<aige::Health>(b).current = 0.0f;
    EXPECT_EQ(victories(0.016f), 1);
    EXPECT_EQ(victories(0.016f), 0); // reported once
}

TEST(SliceTest, CombatEventsCarryEntitySounds) {
    aige::LuaVM lua;
    aige::Registry registry;
    MakeFloor(registry, 8);
    auto player = aige::CreatePlayer(registry, 2.5f, 2.5f);
    registry.emplace<aige::MeleeAttack>(player, 3.0f, 0.7f, 0.5f, 0.35f, 0.5f); // characters come without an attack
    registry.emplace<aige::Sounds>(player, "swing.wav", "ouch.wav", "", "");
    auto enemy = aige::CreateEnemy(registry, 3.2f, 2.5f, entt::null, "");
    registry.get<aige::Health>(enemy).current = 1.0f;
    registry.emplace<aige::Sounds>(enemy, "", "hit.wav", "death.wav", "");

    std::vector<std::string> played;
    aige::GameSession session(registry, lua, aige::BootstrapIso());
    session.SetSoundPlayer([&](const std::string& path) { played.push_back(path); });
    session.RequestPlayerAttack(3.2f, 2.5f);
    for (int i = 0; i < 10; ++i) {
        session.Advance(aige::GameSession::kFixedDt, kNoKeys);
    }

    EXPECT_NE(std::find(played.begin(), played.end(), "swing.wav"), played.end());
    EXPECT_NE(std::find(played.begin(), played.end(), "hit.wav"), played.end());
    EXPECT_NE(std::find(played.begin(), played.end(), "death.wav"), played.end());
    EXPECT_EQ(std::find(played.begin(), played.end(), "ouch.wav"), played.end()); // the player wasn't hit
}

TEST(SliceTest, SpritesFlipToFaceTheirDirection) {
    aige::Registry registry;
    auto npc = aige::CreateNpc(registry, 2.5f, 2.5f);
    auto& sprite = registry.get<aige::Sprite>(npc);
    sprite.u0 = 0.0f;
    sprite.u1 = 1.0f;

    registry.emplace_or_replace<aige::Facing>(npc, -1.0f, 1.0f); // screen-left
    aige::UpdateSpriteFacing(registry);
    EXPECT_GT(registry.get<aige::Sprite>(npc).u0, registry.get<aige::Sprite>(npc).u1);

    registry.emplace_or_replace<aige::Facing>(npc, 1.0f, 1.0f); // straight down-screen: keep current side
    aige::UpdateSpriteFacing(registry);
    EXPECT_GT(registry.get<aige::Sprite>(npc).u0, registry.get<aige::Sprite>(npc).u1);

    registry.emplace_or_replace<aige::Facing>(npc, 1.0f, -1.0f); // screen-right
    aige::UpdateSpriteFacing(registry);
    EXPECT_FLOAT_EQ(registry.get<aige::Sprite>(npc).u0, 0.0f);
    EXPECT_FLOAT_EQ(registry.get<aige::Sprite>(npc).u1, 1.0f);
}

TEST(SliceTest, CameraSnapsAndEasesTowardTarget) {
    aige::Registry registry;
    auto player = aige::CreatePlayer(registry, 2.5f, 2.5f);
    registry.get<aige::ScreenTransform2D>(player) = {500.0f, 300.0f, 0.0f};

    aige::Camera2D camera;
    camera.zoom = 2.0f;
    aige::SnapCameraTo(camera, registry, player, 800.0f, 600.0f);
    float vx = 0.0f;
    float vy = 0.0f;
    camera.SceneToViewport(500.0f, 300.0f, vx, vy);
    EXPECT_NEAR(vx, 400.0f, 0.01f); // dead center
    EXPECT_NEAR(vy, 300.0f, 0.01f);

    registry.get<aige::ScreenTransform2D>(player).x = 600.0f;
    float before = camera.offsetX;
    aige::FollowWithCamera(camera, registry, player, 800.0f, 600.0f, 1.0f / 60.0f);
    EXPECT_GT(camera.offsetX, before);                // moving toward the target
    EXPECT_LT(camera.offsetX, before + 100.0f / 2.0f); // but eased, not snapped
}

