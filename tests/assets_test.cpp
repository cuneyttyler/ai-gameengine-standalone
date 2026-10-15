#include <gtest/gtest.h>

#include "assets/AssetDatabase.h"
#include "assets/SceneIO.h"
#include "assets/TextureCook.h"
#include "ecs/Registry.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

namespace {

// A fresh, empty project directory per test.
class AssetsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / ("aige_assets_test_" + std::string(
                                                 ::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        fs::create_directories(root_ / "assets");
        sources_ = root_ / "incoming"; // outside assets/, like a user's Downloads folder
        fs::create_directories(sources_);
    }
    void TearDown() override { fs::remove_all(root_); }

    fs::path root_;
    fs::path sources_;
};

aige::ImageData MakeCheckerImage(uint32_t width, uint32_t height) {
    aige::ImageData image;
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            size_t i = (static_cast<size_t>(y) * width + x) * 4;
            bool dark = ((x / 4) + (y / 4)) % 2 == 0;
            image.rgba[i + 0] = dark ? 20 : static_cast<uint8_t>(x * 7);
            image.rgba[i + 1] = dark ? 40 : static_cast<uint8_t>(y * 5);
            image.rgba[i + 2] = 200;
            image.rgba[i + 3] = (x + y) % 3 == 0 ? 128 : 255; // exercise alpha too
        }
    }
    return image;
}

// A minimal 16-bit mono PCM WAV of `seconds` of silence.
void WriteSilentWav(const fs::path& path, uint32_t sampleRate, double seconds) {
    uint32_t samples = static_cast<uint32_t>(sampleRate * seconds);
    uint32_t dataBytes = samples * 2;
    std::ofstream out(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4);
    u32(36 + dataBytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1); // PCM
    u16(1); // mono
    u32(sampleRate);
    u32(sampleRate * 2);
    u16(2);
    u16(16);
    out.write("data", 4);
    u32(dataBytes);
    std::string silence(dataBytes, '\0');
    out.write(silence.data(), dataBytes);
}

} // namespace

TEST_F(AssetsTest, ImportedPngCooksToIdenticalPixels) {
    aige::ImageData original = MakeCheckerImage(24, 16);
    ASSERT_TRUE(aige::WritePng(sources_ / "hero.png", original));

    aige::AssetDatabase db(aige::Project{root_});
    std::string error;
    auto path = db.Import(sources_ / "hero.png", &error);
    ASSERT_TRUE(path.has_value()) << error;
    EXPECT_EQ(*path, "assets/textures/hero.png");
    EXPECT_TRUE(fs::exists(root_ / "assets/textures/hero.png"));
    EXPECT_TRUE(fs::exists(root_ / "assets/textures/hero.png.meta"));

    const aige::AssetInfo* info = db.Find(*path);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->type, aige::AssetType::Texture);
    EXPECT_EQ(info->width, 24u);
    EXPECT_EQ(info->height, 16u);

    auto cooked = db.LoadTexture(*path);
    ASSERT_TRUE(cooked.has_value());
    EXPECT_EQ(cooked->width, original.width);
    EXPECT_EQ(cooked->height, original.height);
    EXPECT_EQ(cooked->rgba, original.rgba);
}

TEST_F(AssetsTest, ImportNeverOverwritesExistingAsset) {
    ASSERT_TRUE(aige::WritePng(sources_ / "tile.png", MakeCheckerImage(8, 8)));
    aige::AssetDatabase db(aige::Project{root_});
    auto first = db.Import(sources_ / "tile.png");
    auto second = db.Import(sources_ / "tile.png");
    ASSERT_TRUE(first && second);
    EXPECT_EQ(*first, "assets/textures/tile.png");
    EXPECT_EQ(*second, "assets/textures/tile_1.png");
    EXPECT_EQ(db.Assets().size(), 2u);
}

TEST_F(AssetsTest, ImportRejectsUnsupportedAndCorruptFiles) {
    std::ofstream(sources_ / "notes.txt") << "hello";
    std::ofstream(sources_ / "broken.png") << "not really a png";

    aige::AssetDatabase db(aige::Project{root_});
    std::string error;
    EXPECT_FALSE(db.Import(sources_ / "notes.txt", &error).has_value());
    EXPECT_NE(error.find("unsupported"), std::string::npos);

    EXPECT_FALSE(db.Import(sources_ / "broken.png", &error).has_value());
    EXPECT_FALSE(fs::exists(root_ / "assets/textures/broken.png")); // no half-imported leftovers
    EXPECT_TRUE(db.Assets().empty());
}

TEST_F(AssetsTest, TilesetSettingsPersistInMeta) {
    ASSERT_TRUE(aige::WritePng(sources_ / "ground.png", MakeCheckerImage(128, 64)));
    {
        aige::AssetDatabase db(aige::Project{root_});
        auto path = db.Import(sources_ / "ground.png");
        ASSERT_TRUE(path);
        ASSERT_TRUE(db.SetImageSettings(*path, aige::AssetType::Tileset, 64, 32));
        const aige::AssetInfo* info = db.Find(*path);
        EXPECT_EQ(info->type, aige::AssetType::Tileset);
        EXPECT_EQ(info->TileColumns(), 2u);
        EXPECT_EQ(info->TileRows(), 2u);
        EXPECT_EQ(info->TileCount(), 4u);
    }
    // A fresh database (next editor launch) reads the same settings back.
    aige::AssetDatabase reopened(aige::Project{root_});
    reopened.Refresh();
    const aige::AssetInfo* info = reopened.Find("assets/textures/ground.png");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->type, aige::AssetType::Tileset);
    EXPECT_EQ(info->tileWidth, 64u);
    EXPECT_EQ(info->tileHeight, 32u);
}

TEST_F(AssetsTest, RefreshPicksUpDroppedInFilesAndRecooksEdits) {
    fs::create_directories(root_ / "assets/props");
    ASSERT_TRUE(aige::WritePng(root_ / "assets/props/crate.png", MakeCheckerImage(16, 16)));

    aige::AssetDatabase db(aige::Project{root_});
    auto changed = db.Refresh();
    ASSERT_EQ(db.Assets().size(), 1u);
    EXPECT_EQ(db.Assets()[0].path, "assets/props/crate.png");
    EXPECT_TRUE(fs::exists(root_ / "assets/props/crate.png.meta")); // default meta written
    EXPECT_EQ(changed.size(), 1u);

    // Nothing changed: nothing re-cooked.
    EXPECT_TRUE(db.Refresh().empty());

    // Edit the source (as an image editor would); the next scan re-cooks it.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT_TRUE(aige::WritePng(root_ / "assets/props/crate.png", MakeCheckerImage(32, 16)));
    fs::last_write_time(root_ / "assets/props/crate.png", fs::file_time_type::clock::now() + std::chrono::seconds(2));
    changed = db.Refresh();
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(db.Find("assets/props/crate.png")->width, 32u);
}

TEST_F(AssetsTest, RefreshDropsDeletedFiles) {
    ASSERT_TRUE(aige::WritePng(sources_ / "gone.png", MakeCheckerImage(8, 8)));
    aige::AssetDatabase db(aige::Project{root_});
    ASSERT_TRUE(db.Import(sources_ / "gone.png"));
    fs::remove(root_ / "assets/textures/gone.png");
    db.Refresh();
    EXPECT_TRUE(db.Assets().empty());
}

TEST_F(AssetsTest, AudioImportReportsDuration) {
    WriteSilentWav(sources_ / "hit.wav", 22050, 0.5);
    aige::AssetDatabase db(aige::Project{root_});
    std::string error;
    auto path = db.Import(sources_ / "hit.wav", &error);
    ASSERT_TRUE(path.has_value()) << error;
    EXPECT_EQ(*path, "assets/audio/hit.wav");
    const aige::AssetInfo* info = db.Find(*path);
    EXPECT_EQ(info->type, aige::AssetType::Audio);
    EXPECT_NEAR(info->durationSeconds, 0.5, 0.01);
    EXPECT_FALSE(db.LoadTexture(*path).has_value()); // audio isn't a texture
}

TEST(SceneTextureTest, SpriteTextureAndQuotedPathsRoundTrip) {
    aige::Registry registry;
    auto entity = registry.create();
    registry.emplace<aige::ScreenTransform2D>(entity);
    auto& sprite = registry.emplace<aige::Sprite>(entity);
    sprite.texture = "assets/textures/my hero \"v2\".png";
    registry.emplace<aige::LuaScript>(entity).path = "scripts/with space.lua";

    aige::Registry loaded;
    aige::DeserializeScene(loaded, aige::SerializeScene(registry));
    auto loadedEntity = *loaded.view<aige::Sprite>().begin();
    EXPECT_EQ(loaded.get<aige::Sprite>(loadedEntity).texture, "assets/textures/my hero \"v2\".png");
    EXPECT_EQ(loaded.get<aige::LuaScript>(loadedEntity).path, "scripts/with space.lua");
}

TEST(SceneTextureTest, LegacyUnquotedScriptPathStillLoads) {
    aige::Registry loaded;
    aige::DeserializeScene(loaded, "entity\n  LuaScript path=C:/old style/path.lua\n");
    auto entity = *loaded.view<aige::LuaScript>().begin();
    EXPECT_EQ(loaded.get<aige::LuaScript>(entity).path, "C:/old style/path.lua");
}
