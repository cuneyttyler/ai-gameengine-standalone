#include <gtest/gtest.h>

#include "assets/SceneIO.h"
#include "gameplay/Archetypes.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/GameSession.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "renderer2d/SpriteCulling.h"

// development_plan.md 5.2: top-down art lying flat on the isometric grid.
namespace {

aige::TileStyle FlatStyle(const aige::IsoParams& iso, int rotation = 0) {
    aige::TileStyle style;
    style.texture = "assets/textures/road.png";
    style.width = iso.tileWidth;
    style.height = iso.tileHeight;
    style.flat = true;
    style.rotation = rotation;
    return style;
}

} // namespace

TEST(FlatSpritesTest, UprightSpritesKeepTheirRectangle) {
    aige::ScreenTransform2D t{100.0f, 50.0f, 0.0f};
    aige::Sprite s;
    s.width = 20.0f;
    s.height = 10.0f;
    s.rotation = 1; // ignored when upright
    auto q = aige::SpriteQuadOf(t, s);
    EXPECT_FLOAT_EQ(q.x[0], 90.0f);
    EXPECT_FLOAT_EQ(q.y[0], 45.0f);
    EXPECT_FLOAT_EQ(q.x[2], 110.0f);
    EXPECT_FLOAT_EQ(q.y[2], 55.0f);
    EXPECT_FLOAT_EQ(q.u[0], 0.0f);
    EXPECT_FLOAT_EQ(q.v[0], 0.0f);
    EXPECT_FLOAT_EQ(q.u[2], 1.0f);
    EXPECT_FLOAT_EQ(q.v[2], 1.0f);
}

TEST(FlatSpritesTest, FlatImagesMapCornerToCornerOntoTheDiamondAndTurn) {
    aige::ScreenTransform2D t{0.0f, 0.0f, 0.0f};
    aige::Sprite s;
    s.width = 64.0f;
    s.height = 32.0f;
    s.flat = true;
    s.u0 = 0.25f;
    s.u1 = 0.5f; // a tileset region turns with it
    auto q = aige::SpriteQuadOf(t, s);
    // Top, right, bottom, left.
    const float xs[4] = {0, 32, 0, -32};
    const float ys[4] = {-16, 0, 16, 0};
    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(q.x[i], xs[i]);
        EXPECT_FLOAT_EQ(q.y[i], ys[i]);
    }
    // The image's top-left is at the top point, its top-right at the right one.
    EXPECT_FLOAT_EQ(q.u[0], 0.25f);
    EXPECT_FLOAT_EQ(q.v[0], 0.0f);
    EXPECT_FLOAT_EQ(q.u[1], 0.5f);
    EXPECT_FLOAT_EQ(q.v[1], 0.0f);

    // A quarter turn clockwise: the top-left moves to the right point.
    s.rotation = 1;
    q = aige::SpriteQuadOf(t, s);
    EXPECT_FLOAT_EQ(q.u[1], 0.25f);
    EXPECT_FLOAT_EQ(q.v[1], 0.0f);
    EXPECT_FLOAT_EQ(q.u[0], 0.25f); // the bottom-left is now at the top
    EXPECT_FLOAT_EQ(q.v[0], 1.0f);
    // Four turns are none; out-of-range values wrap.
    s.rotation = 4;
    EXPECT_FLOAT_EQ(aige::SpriteQuadOf(t, s).v[0], 0.0f);
    s.rotation = -1;
    EXPECT_FLOAT_EQ(aige::SpriteQuadOf(t, s).u[3], 0.25f);
    EXPECT_FLOAT_EQ(aige::SpriteQuadOf(t, s).v[3], 0.0f);
}

TEST(FlatSpritesTest, NeighbouringFlatTilesShareTheirEdges) {
    const aige::IsoParams iso = aige::BootstrapIso();
    aige::Registry registry;
    aige::Entity a = aige::CreateTile(registry, 0, 0, FlatStyle(iso));
    aige::Entity right = aige::CreateTile(registry, 1, 0, FlatStyle(iso));
    aige::Entity below = aige::CreateTile(registry, 0, 1, FlatStyle(iso));
    aige::UpdateWorldToScreen(registry, iso);
    auto quad = [&](aige::Entity e) {
        return aige::SpriteQuadOf(registry.get<aige::ScreenTransform2D>(e), registry.get<aige::Sprite>(e));
    };
    auto same = [](const aige::SpriteQuad& p, int i, const aige::SpriteQuad& q, int j) {
        EXPECT_NEAR(p.x[i], q.x[j], 1e-3f);
        EXPECT_NEAR(p.y[i], q.y[j], 1e-3f);
    };
    // (0,0)'s right and bottom points are (1,0)'s top and left ones.
    same(quad(a), 1, quad(right), 0);
    same(quad(a), 2, quad(right), 3);
    // (0,0)'s left and bottom points are (0,1)'s top and right ones.
    same(quad(a), 3, quad(below), 0);
    same(quad(a), 2, quad(below), 1);
    // And the diamond is the cell: its top point is world (0, 0).
    const aige::ScreenPose corner = aige::ProjectIso(0.0f, 0.0f, iso);
    EXPECT_NEAR(quad(a).x[0], corner.x, 1e-3f);
    EXPECT_NEAR(quad(a).y[0], corner.y, 1e-3f);
}

TEST(FlatSpritesTest, FlatThingsDrawUnderWhatStands) {
    const aige::IsoParams iso = aige::BootstrapIso();
    aige::Registry registry;
    aige::Entity road = aige::CreateTile(registry, 5, 5, FlatStyle(iso));
    aige::Entity block = aige::CreateTile(registry, 0, 0, aige::TileKind::Wall); // isometric art
    aige::Entity rug = aige::CreateProp(registry, 5.5f, 5.5f, "assets/textures/rug.png", 64.0f, 32.0f);
    registry.get<aige::Sprite>(rug).flat = true;
    aige::Entity crate = aige::CreateProp(registry, 5.5f, 5.5f, "assets/textures/crate.png", 32.0f, 32.0f);
    aige::UpdateWorldToScreen(registry, iso);
    auto depth = [&](aige::Entity e) { return registry.get<aige::ScreenTransform2D>(e).depth; };
    EXPECT_LT(depth(road), depth(block)); // flat ground comes first, even far forward
    EXPECT_GT(depth(rug), registry.get<aige::ScreenTransform2D>(road).depth);
    EXPECT_LT(depth(rug), depth(crate)); // a rug lies under what stands on it
    // A flat prop is centered on its point; an upright one stands on it.
    const aige::ScreenPose at = aige::ProjectIso(5.5f, 5.5f, iso);
    EXPECT_FLOAT_EQ(registry.get<aige::ScreenTransform2D>(rug).y, at.y);
    EXPECT_LT(registry.get<aige::ScreenTransform2D>(crate).y, at.y);
}

TEST(FlatSpritesTest, FlatAndRotationSurviveTheSceneFileAndOldScenesStayUpright) {
    const aige::IsoParams iso = aige::BootstrapIso();
    aige::Registry registry;
    aige::CreateTile(registry, 2, 3, FlatStyle(iso, 3));
    const std::string text = aige::SerializeScene(registry);
    EXPECT_NE(text.find("flat=1 rotation=3"), std::string::npos) << text;

    aige::Registry loaded;
    aige::DeserializeScene(loaded, text);
    auto view = loaded.view<aige::Sprite>();
    ASSERT_EQ(view.size(), 1u);
    const auto& sprite = view.get<aige::Sprite>(*view.begin());
    EXPECT_TRUE(sprite.flat);
    EXPECT_EQ(sprite.rotation, 3);

    // A scene from before 5.2 has neither field: upright, unturned.
    aige::Registry old;
    aige::DeserializeScene(old, "# AIGE Scene v1\n\nentity\n  Tile gx=0 gy=0 solid=0\n  ScreenTransform2D x=0 y=0 depth=0\n"
                                "  Sprite texture=assets/textures/ground.png width=64 height=64 u0=0 v0=0 u1=1 v1=1 r=1 g=1 b=1 a=1\n");
    auto oldView = old.view<aige::Sprite>();
    ASSERT_EQ(oldView.size(), 1u);
    EXPECT_FALSE(oldView.get<aige::Sprite>(*oldView.begin()).flat);
    EXPECT_EQ(oldView.get<aige::Sprite>(*oldView.begin()).rotation, 0);
}
