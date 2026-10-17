#include <gtest/gtest.h>

#include "assets/AssetDatabase.h"
#include "assets/TileEdges.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace fs = std::filesystem;

// development_plan.md 6.2: what a ground tile's border shows, from its pixels.
namespace {

const std::array<uint8_t, 3> kGrass{90, 160, 60};
const std::array<uint8_t, 3> kDirt{215, 170, 110};

// A 64×64 tile, each pixel grass or dirt by `dirtAt(x, y)` (fractions 0-1).
aige::ImageData Tile(const std::function<bool(float, float)>& dirtAt, uint8_t alpha = 255) {
    aige::ImageData image;
    image.width = image.height = 64;
    image.rgba.resize(64 * 64 * 4);
    for (uint32_t y = 0; y < 64; ++y) {
        for (uint32_t x = 0; x < 64; ++x) {
            const auto& c = dirtAt((x + 0.5f) / 64.0f, (y + 0.5f) / 64.0f) ? kDirt : kGrass;
            uint8_t* p = &image.rgba[(y * 64 + x) * 4];
            p[0] = c[0];
            p[1] = c[1];
            p[2] = c[2];
            p[3] = alpha;
        }
    }
    return image;
}

std::string Signature(const aige::EdgeClassification& result, size_t i) {
    return aige::FormatSignature(result.signatures[i]);
}

} // namespace

TEST(TileEdgesTest, BordersAreClassifiedIntoNamedTerrains) {
    std::vector<aige::BorderSamples> tiles = {
        aige::SampleBorder(Tile([](float, float) { return false; })),                    // all grass
        aige::SampleBorder(Tile([](float, float) { return true; })),                     // all dirt
        aige::SampleBorder(Tile([](float x, float) { return x < 0.35f; })),              // dirt down the left side
        aige::SampleBorder(Tile([](float x, float y) { return !(x > 0.7f && y > 0.7f); })), // dirt, a grass corner
        aige::SampleBorder(Tile([](float, float y) { return y < 0.65f && y > 0.35f; })), // a dirt road left-right
    };
    const auto result = aige::ClassifyEdges(tiles);
    ASSERT_EQ(result.terrains.size(), 2u);
    EXPECT_NE(std::find(result.terrains.begin(), result.terrains.end(), "grass"), result.terrains.end());
    EXPECT_NE(std::find(result.terrains.begin(), result.terrains.end(), "dirt"), result.terrains.end());
    // NW, N, NE, E, SE, S, SW, W
    EXPECT_EQ(Signature(result, 0), "grass,grass,grass,grass,grass,grass,grass,grass");
    EXPECT_EQ(Signature(result, 1), "dirt,dirt,dirt,dirt,dirt,dirt,dirt,dirt");
    EXPECT_EQ(Signature(result, 2), "dirt,grass,grass,grass,grass,grass,dirt,dirt");
    EXPECT_EQ(Signature(result, 3), "dirt,dirt,dirt,dirt,grass,dirt,dirt,dirt");
    EXPECT_EQ(Signature(result, 4), "grass,grass,grass,dirt,grass,grass,grass,dirt");
    // The middle: a road through grass is road in the middle.
    EXPECT_EQ(result.centers[0], "grass");
    EXPECT_EQ(result.centers[2], "grass"); // the dirt strip is down the left side only
    EXPECT_EQ(result.centers[4], "dirt");
}

TEST(TileEdgesTest, TransparentBordersAreNotGround) {
    EXPECT_TRUE(aige::SampleBorder(Tile([](float, float) { return false; })).opaque);
    EXPECT_FALSE(aige::SampleBorder(Tile([](float, float) { return false; }, 0)).opaque); // a prop's background
}

TEST(TileEdgesTest, SignaturesAndTerrainNamesRoundTrip) {
    aige::EdgeSignature signature{"grass", "dirt", "dirt", "grass", "water", "grass", "sand", "stone"};
    aige::EdgeSignature parsed;
    ASSERT_TRUE(aige::ParseSignature(aige::FormatSignature(signature), parsed));
    EXPECT_EQ(parsed, signature);
    EXPECT_FALSE(aige::ParseSignature("grass,dirt", parsed));
    EXPECT_EQ(aige::TerrainNameFor({0.35f, 0.63f, 0.24f}), "grass");
    EXPECT_EQ(aige::TerrainNameFor({0.2f, 0.4f, 0.85f}), "water");
    EXPECT_EQ(aige::TerrainNameFor({0.5f, 0.5f, 0.5f}), "stone");
    EXPECT_EQ(aige::TerrainNameFor({0.55f, 0.38f, 0.2f}), "dirt");
}

TEST(TileEdgesTest, AnalysisIsKeptInTheMetaThroughSettingsChanges) {
    const fs::path root = fs::temp_directory_path() / "aige_asset_analysis";
    fs::remove_all(root);
    fs::create_directories(root / "assets" / "pack");
    std::ofstream(root / "assets" / "pack" / "well.png") << "not really a png"; // listed even if it can't decode
    {
        aige::AssetDatabase assets(aige::Project{root});
        assets.Refresh();
        ASSERT_TRUE(assets.SetAnalysis("assets/pack/well.png", {{"kind", "building"},
                                                                {"description", "A stone well.\nFor water."},
                                                                {"cells", "1.20"}}));
        EXPECT_EQ(assets.Find("assets/pack/well.png")->analysis.at("kind"), "building");
        assets.SetImageSettings("assets/pack/well.png", aige::AssetType::Tileset, 32, 32); // keeps the analysis
        ASSERT_TRUE(assets.SetAnalysis("assets/pack/well.png", {{"cells", ""}}));          // empty removes a key
    }
    aige::AssetDatabase reopened(aige::Project{root});
    reopened.Refresh();
    const auto* well = reopened.Find("assets/pack/well.png");
    ASSERT_NE(well, nullptr);
    EXPECT_EQ(well->analysis.at("kind"), "building");
    EXPECT_EQ(well->analysis.at("description"), "A stone well. For water."); // one line in the meta
    EXPECT_FALSE(well->analysis.contains("cells"));
    std::stringstream text;
    {
        std::ifstream in(root / "assets" / "pack" / "well.png.meta");
        text << in.rdbuf();
    } // closed before the folder is removed
    EXPECT_NE(text.str().find("type=tileset"), std::string::npos);
    fs::remove_all(root);
}
