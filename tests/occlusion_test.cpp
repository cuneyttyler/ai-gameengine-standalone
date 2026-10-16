#include <gtest/gtest.h>

#include "editor/Occlusion.h"

// development_plan.md 4.1: menus and dialogs over the viewport are cut out of
// its window; this is the geometry.
TEST(OcclusionTest, OverlaysAreClippedToTheViewportInLocalCoordinates) {
    aige::PixelRect viewport{300, 60, 800, 500}; // page coordinates
    auto local = aige::LocalOcclusion(viewport, {
        {320, 80, 200, 150},   // inside: shifted to local
        {250, 40, 100, 100},   // over the corner: clipped
        {0, 0, 200, 900},      // left of it entirely: dropped
        {0, 0, 1920, 1080},    // a modal backdrop: the whole viewport
        {1100, 100, 10, 10},   // touching the right edge only: dropped
    });
    ASSERT_EQ(local.size(), 3u);
    EXPECT_EQ(local[0].x, 20);
    EXPECT_EQ(local[0].y, 20);
    EXPECT_EQ(local[0].w, 200);
    EXPECT_EQ(local[0].h, 150);
    EXPECT_EQ(local[1].x, 0);
    EXPECT_EQ(local[1].y, 0);
    EXPECT_EQ(local[1].w, 50);
    EXPECT_EQ(local[1].h, 80);
    EXPECT_EQ(local[2].w, 800);
    EXPECT_EQ(local[2].h, 500);
    EXPECT_TRUE(aige::LocalOcclusion(viewport, {}).empty());
}
