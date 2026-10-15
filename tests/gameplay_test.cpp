#include <gtest/gtest.h>

#include "assets/SceneIO.h"
#include "ecs/Registry.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/GameSession.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/LuaVM.h"

#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

aige::KeyQueryFn KeysHeld(std::initializer_list<aige::Key> keys) {
    std::vector<aige::Key> held(keys);
    return [held](aige::Key key) { return std::find(held.begin(), held.end(), key) != held.end(); };
}

size_t CountEntities(aige::Registry& registry) {
    size_t count = 0;
    for ([[maybe_unused]] auto entity : registry.view<aige::Entity>()) {
        ++count;
    }
    return count;
}

bool NoWalls(int, int) {
    return false;
}

aige::Entity MakePlayer(aige::Registry& registry, float x, float y) {
    auto player = registry.create();
    registry.emplace<aige::WorldTransform2D>(player, x, y);
    registry.emplace<aige::Velocity2D>(player);
    registry.emplace<aige::Collider2D>(player, 0.3f, 0.3f);
    registry.emplace<aige::ScreenTransform2D>(player);
    return player;
}

} // namespace

TEST(GameplayTest, FindPlayerPicksVelocityPlusCollider) {
    aige::Registry registry;
    auto npc = registry.create();
    registry.emplace<aige::WorldTransform2D>(npc, 1.0f, 1.0f);
    EXPECT_FALSE(aige::FindPlayer(registry).has_value());

    auto player = MakePlayer(registry, 3.5f, 3.5f);
    auto found = aige::FindPlayer(registry);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, player);
}

TEST(GameplayTest, UpIsScreenUpWhichIsWorldDiagonal) {
    aige::Registry registry;
    auto player = MakePlayer(registry, 3.5f, 3.5f);

    aige::UpdatePlayer(registry, player, KeysHeld({aige::Key::W}), &NoWalls, 0.1f);

    // W = screen-up = -x, -y in world space, at 3 cells/s.
    const auto& world = registry.get<aige::WorldTransform2D>(player);
    EXPECT_FLOAT_EQ(world.x, 3.2f);
    EXPECT_FLOAT_EQ(world.y, 3.2f);
}

TEST(GameplayTest, NoInputMeansNoMovement) {
    aige::Registry registry;
    auto player = MakePlayer(registry, 3.5f, 3.5f);

    aige::UpdatePlayer(registry, player, KeysHeld({}), &NoWalls, 0.1f);

    const auto& world = registry.get<aige::WorldTransform2D>(player);
    EXPECT_FLOAT_EQ(world.x, 3.5f);
    EXPECT_FLOAT_EQ(world.y, 3.5f);
}

TEST(GameplayTest, BootstrapWallsBlockMovement) {
    aige::Registry registry;
    auto player = MakePlayer(registry, 1.5f, 1.5f); // next to the border ring

    for (int i = 0; i < 60; ++i) {
        aige::UpdatePlayer(registry, player, KeysHeld({aige::Key::W}), &aige::IsBootstrapSolidCell, 1.0f / 60.0f);
    }

    const auto& world = registry.get<aige::WorldTransform2D>(player);
    EXPECT_GE(world.x - 0.3f, 1.0f);
    EXPECT_GE(world.y - 0.3f, 1.0f);
}

TEST(GameplayTest, WorldToScreenMatchesIsoProjection) {
    aige::Registry registry;
    auto player = MakePlayer(registry, 3.5f, 3.5f);
    auto staticTile = registry.create();
    registry.emplace<aige::ScreenTransform2D>(staticTile, 10.0f, 20.0f, 1.0f);

    const aige::IsoParams iso = aige::BootstrapIso();
    aige::UpdateWorldToScreen(registry, iso);

    aige::ScreenPose pose = aige::ProjectIso(3.5f, 3.5f, iso);
    const auto& screen = registry.get<aige::ScreenTransform2D>(player);
    EXPECT_FLOAT_EQ(screen.x, pose.x);
    EXPECT_FLOAT_EQ(screen.y, pose.y - iso.tileHeight * 0.5f);
    EXPECT_FLOAT_EQ(screen.depth, pose.depth + 0.5f);

    // Entities without a WorldTransform2D keep their authored screen position.
    EXPECT_FLOAT_EQ(registry.get<aige::ScreenTransform2D>(staticTile).x, 10.0f);
}

TEST(GameplayTest, SessionRunsFixedStepsAndCarriesRemainder) {
    aige::LuaVM lua;
    aige::Registry registry;
    MakePlayer(registry, 3.5f, 3.5f);
    aige::GameSession session(registry, lua, aige::BootstrapIso(), &NoWalls);

    EXPECT_EQ(session.Advance(aige::GameSession::kFixedDt * 2.5, KeysHeld({})), 2);
    EXPECT_EQ(session.Advance(aige::GameSession::kFixedDt * 0.6, KeysHeld({})), 1); // 0.5 + 0.6 carried over

    session.ResetClock();
    EXPECT_EQ(session.Advance(aige::GameSession::kFixedDt * 0.9, KeysHeld({})), 0);
}

TEST(GameplayTest, SessionMovesPlayerFromKeys) {
    aige::LuaVM lua;
    aige::Registry registry;
    auto player = MakePlayer(registry, 3.5f, 3.5f);
    aige::GameSession session(registry, lua, aige::BootstrapIso(), &NoWalls);

    session.Advance(0.2, KeysHeld({aige::Key::D}));

    // D = screen-right = +x, -y in world space.
    const auto& world = registry.get<aige::WorldTransform2D>(player);
    EXPECT_GT(world.x, 3.5f);
    EXPECT_LT(world.y, 3.5f);
}

// The editor's Play/Stop contract: snapshot, simulate, restore.
TEST(GameplayTest, PlaySnapshotRestoresSceneAfterSimulation) {
    aige::LuaVM lua;
    aige::Registry registry;
    aige::BuildBootstrapScene(registry, "nonexistent_script.lua"); // bad script logs and is skipped
    const auto entityCount = CountEntities(registry);

    std::string snapshot = aige::SerializeScene(registry);

    aige::GameSession session(registry, lua, aige::BootstrapIso(), &aige::IsBootstrapSolidCell);
    session.Advance(0.25, KeysHeld({aige::Key::S}));
    auto player = *aige::FindPlayer(registry);
    EXPECT_NE(registry.get<aige::WorldTransform2D>(player).y, 3.5f);

    aige::DeserializeScene(registry, snapshot);

    EXPECT_EQ(CountEntities(registry), entityCount);
    auto restored = aige::FindPlayer(registry);
    ASSERT_TRUE(restored.has_value());
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(*restored).x, 3.5f);
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(*restored).y, 3.5f);
}
