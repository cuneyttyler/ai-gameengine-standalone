#include <gtest/gtest.h>

#include "gameplay/Archetypes.h"
#include "gameplay/Benchmark.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "renderer2d/Components.h"
#include "renderer2d/SpriteCulling.h"
#include "scripting/LuaVM.h"

#include <chrono>
#include <filesystem>
#include <iostream>

// development_plan.md 4.6.
namespace {

aige::Entity SpriteAt(aige::Registry& registry, float x, float y, float depth, float size = 10.0f) {
    aige::Entity e = registry.create();
    registry.emplace<aige::ScreenTransform2D>(e, x, y, depth);
    auto& sprite = registry.emplace<aige::Sprite>(e);
    sprite.width = size;
    sprite.height = size;
    return e;
}

} // namespace

TEST(CullingTest, OnlySpritesOverlappingTheViewportAreDrawnInDepthOrder) {
    aige::Registry registry;
    aige::Camera2D camera;
    camera.offsetX = 100.0f;
    camera.offsetY = 50.0f;
    camera.zoom = 2.0f; // the viewport (200 x 100 px) shows scene x 100..200, y 50..100

    aige::Entity back = SpriteAt(registry, 150.0f, 75.0f, 2.0f);
    aige::Entity front = SpriteAt(registry, 120.0f, 60.0f, 1.0f);
    aige::Entity edge = SpriteAt(registry, 97.0f, 75.0f, 3.0f);   // overlaps the left edge by 2 px
    SpriteAt(registry, 90.0f, 75.0f, 0.0f);                       // just outside left
    SpriteAt(registry, 150.0f, 200.0f, 0.0f);                     // below
    SpriteAt(registry, 1000.0f, -1000.0f, 0.0f);                  // far away
    aige::Entity huge = SpriteAt(registry, 0.0f, 0.0f, 4.0f, 1000.0f); // around the whole view

    size_t culled = 0;
    auto visible = aige::VisibleSprites(registry, camera, 200.0f, 100.0f, &culled);
    ASSERT_EQ(visible.size(), 4u);
    EXPECT_EQ(culled, 3u);
    EXPECT_EQ(visible[0], front);
    EXPECT_EQ(visible[1], back);
    EXPECT_EQ(visible[2], edge);
    EXPECT_EQ(visible[3], huge);

    // Zooming out brings everything near into view.
    camera = {};
    camera.zoom = 0.05f; // 4000 x 2000 scene px from (-1500, -1500)
    camera.offsetX = -1500.0f;
    camera.offsetY = -1500.0f;
    aige::VisibleSprites(registry, camera, 200.0f, 100.0f, &culled);
    EXPECT_EQ(culled, 0u);
}

TEST(PerfTest, TenThousandTilesAndFiveHundredCharacters) {
    aige::LuaVM lua; // outlives the registry: scripts keep environments in its state
    aige::Registry registry;
    aige::BuildBenchmarkScene(registry);
    EXPECT_EQ(registry.view<aige::Tile>().size(), 10000u);
    EXPECT_EQ(registry.view<aige::Health>().size(), 501u); // + the player

    const auto root = std::filesystem::path(AIGE_SOURCE_DIR) / "projects" / "demo";
    aige::GameSession session(registry, lua, aige::BootstrapIso(), root);
    auto noKeys = [](aige::Key) { return false; };
    session.Advance(aige::GameSession::kFixedDt, noKeys); // scripts load on the first step
    EXPECT_TRUE(session.ScriptErrors().empty());

    using Clock = std::chrono::steady_clock;
#ifdef NDEBUG
    constexpr int kSteps = 120; // two seconds of game time
#else
    constexpr int kSteps = 5; // Debug is ~50x slower: just keep it working
#endif
    auto start = Clock::now();
    for (int i = 0; i < kSteps; ++i) {
        session.Advance(aige::GameSession::kFixedDt, noKeys);
    }
    const double updateMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count() / kSteps;

    // A 1280 x 720 view around the player, as the game draws it.
    aige::Camera2D camera;
    auto player = aige::FindPlayer(registry);
    ASSERT_TRUE(player);
    const auto& center = registry.get<aige::ScreenTransform2D>(*player);
    camera.offsetX = center.x - 640.0f;
    camera.offsetY = center.y - 360.0f;
#ifdef NDEBUG
    constexpr int kFrames = 60;
#else
    constexpr int kFrames = 5;
#endif
    size_t culled = 0;
    size_t drawn = 0;
    start = Clock::now();
    for (int i = 0; i < kFrames; ++i) {
        drawn = aige::VisibleSprites(registry, camera, 1280.0f, 720.0f, &culled).size();
    }
    const double cullMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count() / kFrames;

    std::cout << "[ perf ] update " << updateMs << " ms/step, sprite list " << cullMs << " ms/frame, " << drawn
              << " drawn, " << culled << " culled\n";
    EXPECT_GT(culled, drawn); // most of a 100 x 100 field is off screen
    EXPECT_GT(drawn, 100u);
#ifdef NDEBUG
    // The budget: a step and a frame's sprite list together well inside 60 fps.
    EXPECT_LT(updateMs + cullMs, 8.0);
#endif
}
