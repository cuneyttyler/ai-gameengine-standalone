#include <gtest/gtest.h>

#include "assets/AssetDatabase.h"
#include "assets/PackImport.h"
#include "assets/Prefab.h"
#include "assets/TextureCook.h"
#include "editing/Commands.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "renderer2d/Components.h"

#include <filesystem>

namespace fs = std::filesystem;

// development_plan.md 6.3: a pack becomes prefabs at its own scale.
namespace {

void Png(const fs::path& path, uint32_t w, uint32_t h, uint8_t alpha) {
    aige::ImageData image;
    image.width = w;
    image.height = h;
    image.rgba.assign(static_cast<size_t>(w) * h * 4, alpha);
    fs::create_directories(path.parent_path());
    ASSERT_TRUE(aige::WritePng(path, image));
}

struct Pack {
    Pack() {
        root = fs::temp_directory_path() / ("aige_pack_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root);
        const fs::path pack = root / "assets" / "summer";
        Png(pack / "Ground 01.png", 256, 256, 255);
        Png(pack / "Ground 02.png", 256, 256, 255);
        Png(pack / "Prop - House.png", 480, 640, 0);
        Png(pack / "Prop - Barrel.png", 120, 140, 0);
        assets = std::make_unique<aige::AssetDatabase>(aige::Project{root});
        assets->Refresh();
        assets->SetAnalysis("assets/summer/Ground 01.png", {{"kind", "ground"}, {"edges", "grass,grass,grass,grass,grass,grass,grass,grass"}});
        assets->SetAnalysis("assets/summer/Ground 02.png", {{"edges", "dirt,dirt,dirt,dirt,dirt,dirt,dirt,dirt"}});
        assets->SetAnalysis("assets/summer/Prop - House.png", {{"kind", "building"}, {"name", "cottage"}, {"cells", "2.5"}, {"blocks", "1"}});
        assets->SetAnalysis("assets/summer/Prop - Barrel.png", {{"kind", "decoration"}, {"name", "wooden-barrel"}, {"cells", "0.5"}, {"blocks", "0"}});
        prefabs = std::make_unique<aige::PrefabLibrary>(aige::Project{root});
        prefabs->Refresh();
    }
    ~Pack() { fs::remove_all(root); }
    std::vector<std::string> All() const {
        return {"assets/summer/Ground 01.png", "assets/summer/Ground 02.png", "assets/summer/Prop - House.png",
                "assets/summer/Prop - Barrel.png"};
    }
    fs::path root;
    std::unique_ptr<aige::AssetDatabase> assets;
    std::unique_ptr<aige::PrefabLibrary> prefabs;
};

const aige::IsoParams kIso{64.0f, 32.0f, 0.0f, 0.0f, false};

} // namespace

TEST(PackImportTest, TheScaleComesFromTheTilesOrIsSetByHand) {
    Pack p;
    const aige::Project project{p.root};
    auto scale = aige::PackScaleFor(project, *p.assets, "assets/summer", kIso);
    EXPECT_FLOAT_EQ(scale.pixelsPerCell, 256.0f);
    EXPECT_EQ(scale.source, "ground tiles");

    ASSERT_TRUE(aige::WritePackScale(project, "assets/summer", 200.0f));
    scale = aige::PackScaleFor(project, *p.assets, "assets/summer", kIso);
    EXPECT_FLOAT_EQ(scale.pixelsPerCell, 200.0f);
    EXPECT_EQ(scale.source, "set by hand");
}

TEST(PackImportTest, WithoutTilesTheAnalysisSaysHowBigThingsAre) {
    Pack p;
    fs::remove(p.root / "assets/summer/Ground 01.png");
    fs::remove(p.root / "assets/summer/Ground 02.png");
    p.assets->Refresh();
    // House: 480 px / 2.5 cells = 192; barrel: 120 / 0.5 = 240; the median of two is the larger.
    const auto scale = aige::PackScaleFor(aige::Project{p.root}, *p.assets, "assets/summer", kIso);
    EXPECT_EQ(scale.source, "analysis");
    EXPECT_GT(scale.pixelsPerCell, 190.0f);
}

TEST(PackImportTest, ThePlanKeepsThePacksProportions) {
    Pack p;
    const auto plans = aige::PlanPackImport(*p.assets, p.All(), *p.prefabs, kIso, [](const std::string&) { return 256.0f; });
    ASSERT_EQ(plans.size(), 4u);
    const auto& grass = plans[0];
    EXPECT_TRUE(grass.tile);
    EXPECT_FLOAT_EQ(grass.width, 64.0f);
    EXPECT_FLOAT_EQ(grass.height, 32.0f);
    EXPECT_TRUE(plans[1].tile); // ground found by its edges alone

    const auto& house = plans[2];
    EXPECT_FALSE(house.tile);
    EXPECT_TRUE(house.solid);
    EXPECT_FLOAT_EQ(house.width, 480.0f / 256.0f * 64.0f); // 1.875 cells
    EXPECT_FLOAT_EQ(house.height, 640.0f / 256.0f * 64.0f);
    EXPECT_EQ(house.path, "prefabs/summer/cottage.prefab");

    const auto& barrel = plans[3];
    EXPECT_FALSE(barrel.solid);
    EXPECT_NEAR(barrel.width / house.width, 120.0f / 480.0f, 1e-4f); // a quarter of the house, as drawn
    EXPECT_EQ(barrel.name, "wooden-barrel");
    EXPECT_EQ(plans[0].name, "ground-01"); // no analysed name: the file's
}

TEST(PackImportTest, PrefabsAreMadeAndNamesStayUnique) {
    Pack p;
    auto plans = aige::PlanPackImport(*p.assets, p.All(), *p.prefabs, kIso, [](const std::string&) { return 256.0f; });
    for (const auto& plan : plans) {
        ASSERT_EQ(aige::CreatePrefabFromPlan(*p.prefabs, plan, kIso), "") << plan.path;
    }
    const aige::PrefabInfo* house = p.prefabs->Find("prefabs/summer/cottage.prefab");
    ASSERT_NE(house, nullptr);
    EXPECT_FLOAT_EQ(house->width, 120.0f);
    const aige::PrefabInfo* grass = p.prefabs->Find("prefabs/summer/ground-01.prefab");
    ASSERT_NE(grass, nullptr);
    EXPECT_TRUE(grass->isTile);
    EXPECT_TRUE(grass->flat);
    p.prefabs->VisitTemplate("prefabs/summer/cottage.prefab", [](aige::Registry& r, aige::Entity e) {
        EXPECT_TRUE(r.all_of<aige::Obstacle>(e)); // solid
    });
    // Importing again doesn't overwrite: new names.
    plans = aige::PlanPackImport(*p.assets, p.All(), *p.prefabs, kIso, [](const std::string&) { return 256.0f; });
    EXPECT_EQ(plans[2].path, "prefabs/summer/cottage-2.prefab");
    EXPECT_NE(aige::CreatePrefabFromPlan(*p.prefabs, {"assets/summer/Prop - House.png", "prefabs/summer/cottage.prefab", "cottage"}, kIso), "");
}

TEST(PackImportTest, TheAiCanMakeAPrefab) {
    Pack p;
    aige::RegisterEditingCommands(aige::EngineReflection());
    aige::Registry registry;
    const aige::Project project{p.root};
    aige::CommandContext context{registry, p.prefabs.get(), &project};
    auto result = aige::EngineReflection().Run(context, "prefab.create",
                                               {std::string("assets/summer/Prop - House.png"), std::string("village/house"),
                                                false, true, 2.0f});
    ASSERT_TRUE(result.ok) << result.error;
    const aige::PrefabInfo* house = p.prefabs->Find("prefabs/village/house.prefab");
    ASSERT_NE(house, nullptr);
    const float cell = aige::BootstrapIso().tileWidth;
    EXPECT_FLOAT_EQ(house->width, 2.0f * cell);
    EXPECT_FLOAT_EQ(house->height, 2.0f * cell * 640.0f / 480.0f);
    EXPECT_FALSE(aige::EngineReflection().Run(context, "prefab.create",
                                              {std::string("../escape.png"), std::string("x"), false, false, 1.0f}).ok);
}
