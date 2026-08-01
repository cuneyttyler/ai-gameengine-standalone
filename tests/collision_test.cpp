#include <gtest/gtest.h>

#include "physics2d/Collision.h"

namespace {

// A one-cell-thick horizontal wall at row 5; open everywhere else.
bool WallAtY5(int, int gy) {
    return gy == 5;
}

} // namespace

TEST(CollisionTest, FreeMovementAppliesFully) {
    auto noWalls = [](int, int) { return false; };
    aige::Vec2 result = aige::ResolveAabbMovement({2.0f, 2.0f}, {0.3f, 0.3f}, {0.5f, -0.25f}, noWalls);
    EXPECT_FLOAT_EQ(result.x, 2.5f);
    EXPECT_FLOAT_EQ(result.y, 1.75f);
}

TEST(CollisionTest, BlockedAxisStopsWhileOtherAxisSlides) {
    // Solid for gx >= 5; open otherwise. Approaching diagonally into the wall
    // should stop X but still allow Y to slide.
    auto wallAtX5 = [](int gx, int) { return gx >= 5; };
    aige::Vec2 result = aige::ResolveAabbMovement({4.5f, 2.0f}, {0.3f, 0.3f}, {0.5f, 0.5f}, wallAtX5);
    EXPECT_FLOAT_EQ(result.x, 4.5f); // rejected: moving to 5.0 would put the AABB's edge into gx==5
    EXPECT_FLOAT_EQ(result.y, 2.5f); // Y move alone doesn't overlap the wall, so it applies
}

TEST(CollisionTest, BlockedOnBothAxesStaysInPlace) {
    auto solidEverywhere = [](int, int) { return true; };
    aige::Vec2 result = aige::ResolveAabbMovement({2.0f, 2.0f}, {0.3f, 0.3f}, {0.1f, 0.1f}, solidEverywhere);
    EXPECT_FLOAT_EQ(result.x, 2.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
}

TEST(CollisionTest, RepeatedSmallStepsDoNotTunnelThroughAThinWall) {
    // A fixed-timestep-sized step (well under one grid cell) repeatedly
    // pushed toward a one-cell-thick wall should approach it and then stop,
    // never ending up on the far side.
    aige::Vec2 pos{2.0f, 4.0f};
    for (int i = 0; i < 30; ++i) {
        pos = aige::ResolveAabbMovement(pos, {0.3f, 0.3f}, {0.0f, 0.05f}, WallAtY5);
    }
    // Allow a hair of float accumulation slop from 30 repeated additions —
    // the point is "didn't cross into the next cell", not exact-to-the-bit.
    EXPECT_LE(pos.y + 0.3f, 5.0f + 1e-3f);
    EXPECT_NEAR(pos.y, 4.7f, 1e-3f);
}
