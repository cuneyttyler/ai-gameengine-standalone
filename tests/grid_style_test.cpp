#include <gtest/gtest.h>

#include "assets/ProjectTemplate.h"
#include "assets/SceneIO.h"
#include "gameplay/Archetypes.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/GameSession.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "renderer2d/SpriteCulling.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// development_plan.md 6.1: isometric or top-down grids.
namespace {

// Switches the active project's grid for one test.
struct GridStyleScope {
    explicit GridStyleScope(aige::GridStyle style) : saved(aige::ActiveSettings().grid.style) {
        aige::ActiveSettings().grid.style = style;
    }
    ~GridStyleScope() { aige::ActiveSettings().grid.style = saved; }
    aige::GridStyle saved;
};

} // namespace

TEST(GridStyleTest, BothProjectionsRoundTrip) {
    for (bool topDown : {false, true}) {
        aige::IsoParams iso{64.0f, topDown ? 64.0f : 32.0f, 10.0f, 20.0f, topDown};
        for (auto [x, y] : {std::pair{0.0f, 0.0f}, std::pair{3.25f, 7.5f}, std::pair{-2.0f, 5.75f}}) {
            const aige::ScreenPose pose = aige::ProjectIso(x, y, iso);
            float wx = 0.0f, wy = 0.0f;
            aige::UnprojectIso(pose.x, pose.y, iso, wx, wy);
            EXPECT_NEAR(wx, x, 1e-4f) << topDown;
            EXPECT_NEAR(wy, y, 1e-4f) << topDown;
        }
    }
}

TEST(GridStyleTest, TopDownCellsAreSquaresAlongTheScreenAxes) {
    const aige::IsoParams iso{64.0f, 64.0f, 0.0f, 0.0f, true};
    const auto a = aige::ProjectIso(2.0f, 3.0f, iso);
    const auto right = aige::ProjectIso(3.0f, 3.0f, iso);
    const auto down = aige::ProjectIso(2.0f, 4.0f, iso);
    EXPECT_FLOAT_EQ(right.x - a.x, 64.0f);
    EXPECT_FLOAT_EQ(right.y, a.y); // one cell right is straight right on screen
    EXPECT_FLOAT_EQ(down.y - a.y, 64.0f);
    EXPECT_FLOAT_EQ(down.x, a.x);
    // Lower rows draw in front, whatever the column.
    EXPECT_GT(aige::ProjectIso(0.0f, 5.0f, iso).depth, aige::ProjectIso(40.0f, 4.0f, iso).depth);
}

TEST(GridStyleTest, TheSettingPicksTheProjectionAndTileShape) {
    {
        GridStyleScope scope(aige::GridStyle::Isometric);
        EXPECT_FALSE(aige::BootstrapIso().topDown);
        EXPECT_FLOAT_EQ(aige::StyleFor(aige::TileKind::Grass).height, 32.0f);
    }
    GridStyleScope scope(aige::GridStyle::TopDown);
    const aige::IsoParams iso = aige::BootstrapIso();
    EXPECT_TRUE(iso.topDown);
    EXPECT_FLOAT_EQ(iso.tileWidth, iso.tileHeight);
    EXPECT_FLOAT_EQ(aige::StyleFor(aige::TileKind::Grass).height, 64.0f); // built-in tiles fill their square
}

TEST(GridStyleTest, FlatImagesLieAsSquaresOnATopDownGrid) {
    aige::ScreenTransform2D t{100.0f, 100.0f, 0.0f};
    aige::Sprite s;
    s.width = 64.0f;
    s.height = 64.0f;
    s.flat = true;
    auto square = aige::SpriteQuadOf(t, s, false);
    EXPECT_FLOAT_EQ(square.x[0], 68.0f); // top-left corner, not the top point of a diamond
    EXPECT_FLOAT_EQ(square.y[0], 68.0f);
    EXPECT_FLOAT_EQ(square.x[2], 132.0f);
    // Turned a quarter: the image's top-left is now at the top-right corner.
    s.rotation = 1;
    square = aige::SpriteQuadOf(t, s, false);
    EXPECT_FLOAT_EQ(square.u[1], 0.0f);
    EXPECT_FLOAT_EQ(square.v[1], 0.0f);
}

TEST(GridStyleTest, ThePlayerWalksAlongTheScreenAxesOnATopDownGrid) {
    GridStyleScope scope(aige::GridStyle::TopDown);
    aige::Registry registry;
    for (int x = 0; x < 5; ++x)
        for (int y = 0; y < 5; ++y) aige::CreateTile(registry, x, y, aige::TileKind::Grass);
    aige::Entity player = aige::CreatePlayer(registry, 2.5f, 2.5f);
    auto noWalls = [](int, int) { return false; };
    auto pressD = [](aige::Key k) { return k == aige::Key::D; };
    aige::UpdatePlayer(registry, player, pressD, noWalls, 1.0f / 60.0f);
    const auto& v = registry.get<aige::Velocity2D>(player);
    EXPECT_GT(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f); // straight right, not diagonal as on an isometric grid

    GridStyleScope iso(aige::GridStyle::Isometric);
    aige::UpdatePlayer(registry, player, pressD, noWalls, 1.0f / 60.0f);
    EXPECT_LT(registry.get<aige::Velocity2D>(player).y, 0.0f); // isometric: screen-right is world (+x, -y)
}

TEST(GridStyleTest, TheSettingIsSavedAndNewProjectsCanStartTopDown) {
    const fs::path dir = fs::temp_directory_path() / "aige_grid_style";
    fs::remove_all(dir);
    ASSERT_FALSE(aige::CreateProject(dir / "topdown", std::nullopt, true).has_value());
    {
        GridStyleScope scope(aige::GridStyle::Isometric);
        ASSERT_TRUE(aige::LoadSettings(aige::EngineReflection(), (dir / "topdown" / "settings.aige").string()));
        EXPECT_EQ(aige::ActiveSettings().grid.style, aige::GridStyle::TopDown);
        aige::SaveSettings(aige::EngineReflection(), (dir / "saved.aige").string());
        std::ifstream in(dir / "saved.aige");
        std::stringstream text;
        text << in.rdbuf();
        EXPECT_NE(text.str().find("Grid style=TopDown"), std::string::npos);
    }
    fs::remove_all(dir);
}
