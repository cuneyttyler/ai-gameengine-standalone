#include <gtest/gtest.h>

#include "assets/Prefab.h"
#include "assets/TextureCook.h"
#include "gameplay/BootstrapLevel.h"
#include "editing/VillageBuild.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/VillageGen.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <array>
#include <deque>
#include <tuple>
#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;

// development_plan.md 6.5: a whole village from one command.
namespace {

aige::VillageLayout Make(aige::VillageParams params) {
    std::string error;
    auto layout = aige::GenerateVillage(params, &error);
    EXPECT_TRUE(layout.has_value()) << error;
    return layout.value_or(aige::VillageLayout{});
}

int Count(const aige::VillageLayout& layout, aige::VillageRole role) {
    return static_cast<int>(std::count_if(layout.items.begin(), layout.items.end(),
                                          [&](const aige::VillageItem& i) { return i.role == role; }));
}

} // namespace

TEST(VillageGenTest, SameSeedSameVillage) {
    aige::VillageParams params;
    params.houses = 14;
    params.seed = 7;
    EXPECT_EQ(Make(params).Ascii(), Make(params).Ascii());
    auto other = params;
    other.seed = 8;
    EXPECT_NE(Make(params).Ascii(), Make(other).Ascii());
}

TEST(VillageGenTest, AsManyHousesAsAskedAndBiggerForMore) {
    int lastArea = 0;
    for (int houses : {1, 6, 15, 30, 60}) {
        aige::VillageParams params;
        params.houses = houses;
        const auto layout = Make(params);
        EXPECT_EQ(layout.houses, houses);
        EXPECT_EQ(Count(layout, aige::VillageRole::House), houses);
        const int area = (layout.x1 - layout.x0 + 1) * (layout.y1 - layout.y0 + 1);
        EXPECT_GT(area, lastArea) << houses;
        lastArea = area;
        // Roughly square, not one long street.
        const double aspect = double(layout.x1 - layout.x0 + 1) / double(layout.y1 - layout.y0 + 1);
        if (houses >= 15) {
            EXPECT_GT(aspect, 0.5) << houses;
            EXPECT_LT(aspect, 2.0) << houses;
        }
    }
}

TEST(VillageGenTest, NothingOverlapsAndNothingStandsOnTheRoad) {
    for (int houseCells : {1, 2, 3}) {
        aige::VillageParams params;
        params.houses = 20;
        params.houseCells = houseCells;
        params.roadWidth = 2;
        params.nature = 1.0f;
        const auto layout = Make(params);
        std::set<aige::VillageCell> used;
        for (const auto& item : layout.items) {
            for (const auto& cell : aige::VillageFootprint(item)) {
                EXPECT_TRUE(used.insert(cell).second) << aige::VillageRoleName(item.role) << " at " << cell.first << ","
                                                      << cell.second << " overlaps";
                EXPECT_FALSE(layout.road.contains(cell)) << aige::VillageRoleName(item.role) << " on the road";
                EXPECT_GE(cell.first, layout.x0);
                EXPECT_LE(cell.first, layout.x1);
                EXPECT_GE(cell.second, layout.y0);
                EXPECT_LE(cell.second, layout.y1);
                if (item.role == aige::VillageRole::House) {
                    EXPECT_FALSE(layout.square.contains(cell));
                }
            }
            if (item.role == aige::VillageRole::House) {
                EXPECT_EQ(item.size, houseCells);
            }
        }
    }
}

TEST(VillageGenTest, StreetsConnectAndEveryHouseFacesOne) {
    aige::VillageParams params;
    params.houses = 24;
    params.seed = 3;
    const auto layout = Make(params);

    std::set<aige::VillageCell> blocked;
    for (const auto& item : layout.items)
        for (const auto& cell : aige::VillageFootprint(item)) blocked.insert(cell);
    std::set<aige::VillageCell> open;
    for (const auto& c : layout.road) open.insert(c);
    for (const auto& c : layout.square)
        if (!blocked.contains(c)) open.insert(c);
    ASSERT_FALSE(open.empty());
    std::set<aige::VillageCell> seen{*open.begin()};
    std::deque<aige::VillageCell> queue{*open.begin()};
    while (!queue.empty()) {
        auto [x, y] = queue.front();
        queue.pop_front();
        for (auto next : {aige::VillageCell{x + 1, y}, aige::VillageCell{x - 1, y}, aige::VillageCell{x, y + 1}, aige::VillageCell{x, y - 1}}) {
            if (open.contains(next) && seen.insert(next).second) queue.push_back(next);
        }
    }
    EXPECT_EQ(seen.size(), open.size()) << layout.Ascii();

    // A house's front is one yard row from a street.
    for (const auto& item : layout.items) {
        if (item.role != aige::VillageRole::House) continue;
        bool facing = false;
        for (const auto& [x, y] : aige::VillageFootprint(item))
            for (int d : {-2, 2})
                if (layout.road.contains({x, y + d}) || layout.square.contains({x, y + d})) facing = true;
        EXPECT_TRUE(facing) << "house at " << item.x << "," << item.y << "\n" << layout.Ascii();
    }
}

TEST(VillageGenTest, UsesTheWholeKit) {
    aige::VillageParams params;
    params.houses = 12;
    params.nature = 0.8f;
    auto layout = Make(params);
    EXPECT_EQ(Count(layout, aige::VillageRole::Well), 1);
    EXPECT_GT(Count(layout, aige::VillageRole::FenceH), 0);
    EXPECT_GT(Count(layout, aige::VillageRole::FenceV), 0);
    EXPECT_GT(Count(layout, aige::VillageRole::Tree), 0);
    EXPECT_GT(Count(layout, aige::VillageRole::Bush), 0);
    EXPECT_GT(Count(layout, aige::VillageRole::Decoration), 0);
    EXPECT_EQ(Count(layout, aige::VillageRole::Entrance), 8); // both sides of four roads out
    EXPECT_EQ(Count(layout, aige::VillageRole::Landmark), 1);
    EXPECT_FALSE(layout.square.empty());

    params.fences = false;
    params.square = false;
    params.nature = 0.0f;
    layout = Make(params);
    EXPECT_EQ(Count(layout, aige::VillageRole::Well), 0);
    EXPECT_EQ(Count(layout, aige::VillageRole::FenceH) + Count(layout, aige::VillageRole::FenceV), 0);
    EXPECT_EQ(Count(layout, aige::VillageRole::Tree) + Count(layout, aige::VillageRole::Rock), 0);
    EXPECT_TRUE(layout.square.empty());
}

TEST(VillageGenTest, BadParameters) {
    aige::VillageParams params;
    params.houses = 0;
    std::string error;
    EXPECT_FALSE(aige::GenerateVillage(params, &error).has_value());
    EXPECT_NE(error.find("houses"), std::string::npos);
    params.houses = 5;
    params.roadWidth = 4;
    EXPECT_FALSE(aige::GenerateVillage(params, &error).has_value());
}

TEST(VillageGenTest, RolesFromTheArtsAnalysis) {
    using R = aige::VillageRole;
    using A = std::map<std::string, std::string>;
    auto roles = [](const A& analysis, const std::string& name = "Asset 12") { return aige::VillageRolesOf(analysis, name); };
    EXPECT_EQ(roles({{"kind", "building"}, {"name", "stone-well"}, {"tags", "well, water"}}), std::vector<R>{R::Well});
    EXPECT_EQ(roles({{"kind", "building"}, {"name", "brick-house"}, {"tags", "house, village"}}), std::vector<R>{R::House});
    EXPECT_EQ(roles({{"kind", "building"}, {"name", "watchtower"}, {"tags", "tower"}}), std::vector<R>{R::Landmark});
    EXPECT_EQ(roles({{"kind", "decoration"}, {"name", "fence-horizontal"}, {"direction", "horizontal"}}),
              std::vector<R>{R::FenceH});
    EXPECT_EQ(roles({{"kind", "building"}, {"name", "stone-wall"}, {"direction", "vertical"}}), std::vector<R>{R::FenceV});
    EXPECT_EQ(roles({{"kind", "decoration"}, {"name", "fence-post"}}), (std::vector<R>{R::FenceH, R::FenceV}));
    EXPECT_EQ(roles({{"kind", "nature"}, {"name", "oak-tree"}}), std::vector<R>{R::Tree});
    EXPECT_EQ(roles({{"kind", "nature"}, {"name", "mossy-boulder"}}), std::vector<R>{R::Rock});
    EXPECT_EQ(roles({{"kind", "nature"}, {"name", "green-thing"}, {"description", "A leafy tree."}}), std::vector<R>{R::Tree});
    EXPECT_EQ(roles({{"kind", "decoration"}, {"name", "red-banner"}}), std::vector<R>{R::Entrance});
    EXPECT_EQ(roles({{"kind", "decoration"}, {"name", "wooden-cart"}}), std::vector<R>{R::Decoration});
    EXPECT_TRUE(roles({{"kind", "ground"}, {"name", "dirt-road"}}).empty());
    EXPECT_TRUE(roles({{"kind", "character"}, {"name", "villager"}}).empty());
    // No analysis: the prefab's name.
    EXPECT_EQ(roles({}, "barrel_2"), std::vector<R>{R::Decoration});
    EXPECT_TRUE(roles({}, "Asset 12").empty());
}

// Built from a little pack: two ground tiles and a set of props, analyzed.
TEST(VillageGenTest, BuildsFromThePack) {
    const fs::path root = fs::temp_directory_path() / "aige_village_build";
    fs::remove_all(root);
    fs::create_directories(root / "prefabs");
    aige::PrefabLibrary prefabs{aige::Project{root}};
    const std::map<std::string, std::pair<std::string, std::string>> tiles = {
        {"grass", {"grass,grass,grass,grass,grass,grass,grass,grass", "grass"}},
        {"dirt", {"dirt,dirt,dirt,dirt,dirt,dirt,dirt,dirt", "dirt"}},
    };
    const std::map<std::string, std::map<std::string, std::string>> props = {
        {"house", {{"kind", "building"}, {"name", "cottage"}, {"tags", "house"}}},
        {"well", {{"kind", "building"}, {"name", "well"}}},
        {"fence", {{"kind", "decoration"}, {"name", "fence"}, {"direction", "horizontal"}}},
        {"tree", {{"kind", "nature"}, {"name", "tree"}}},
        {"cart", {{"kind", "decoration"}, {"name", "cart"}}},
    };
    for (const auto& [name, _] : tiles) {
        aige::Registry scratch;
        aige::TileStyle style;
        style.texture = "assets/pack/" + name + ".png";
        style.flat = true;
        prefabs.SaveAsPrefab(scratch, aige::CreateTile(scratch, 0, 0, style), "prefabs/" + name + ".prefab");
    }
    for (const auto& [name, _] : props) {
        aige::Registry scratch;
        const float width = name == "house" ? 192.0f : 64.0f; // three cells wide
        prefabs.SaveAsPrefab(scratch, aige::CreateProp(scratch, 0, 0, "assets/pack/" + name + ".png", width, width),
                             "prefabs/" + name + ".prefab");
    }
    prefabs.Refresh();
    auto analysisOf = [&](const std::string& asset) {
        const std::string stem = fs::path(asset).stem().string();
        std::map<std::string, std::string> fields;
        if (auto t = tiles.find(stem); t != tiles.end()) {
            fields = {{"kind", "ground"}, {"edges", t->second.first}, {"center", t->second.second}};
        } else if (auto p = props.find(stem); p != props.end()) {
            fields = p->second;
        }
        return fields;
    };

    aige::VillagePlan plan;
    plan.set = aige::BuildTerrainSet(prefabs, analysisOf);
    plan.roles = aige::FindVillageRoles(prefabs, plan.set, analysisOf, 64.0f);
    EXPECT_EQ(plan.roles.ground, "grass");
    EXPECT_EQ(plan.roles.road, "dirt");
    EXPECT_EQ(plan.roles.groundTile, "prefabs/grass.prefab");
    EXPECT_EQ(plan.roles.houseCells, 3);
    EXPECT_EQ(plan.roles.prefabs[aige::VillageRole::FenceV], std::vector<std::string>{"prefabs/fence.prefab"});
    const auto missing = plan.roles.Missing();
    EXPECT_NE(std::find(missing.begin(), missing.end(), std::string("rock")), missing.end());

    plan.params.houses = 6;
    plan.params.houseCells = plan.roles.houseCells;
    plan.params.x0 = 10;
    plan.layout = aige::GenerateVillage(plan.params).value();

    aige::Registry registry;
    const aige::Entity player = aige::CreatePlayer(registry, 12.5f, 12.5f);
    aige::VillageBuildResult built;
    ASSERT_FALSE(aige::BuildVillage(registry, prefabs, plan, true, &built).has_value());
    const auto& layout = plan.layout;
    EXPECT_EQ(built.groundTiles, (layout.x1 - layout.x0 + 1) * (layout.y1 - layout.y0 + 1));
    int houses = 0, dirtTiles = 0;
    std::set<std::pair<int, int>> cells;
    for (auto [e, link] : registry.view<aige::PrefabInstance>().each()) {
        if (link.path == "prefabs/house.prefab") ++houses;
        if (const auto* tile = registry.try_get<aige::Tile>(e)) {
            EXPECT_TRUE(cells.insert({tile->gx, tile->gy}).second); // one tile per cell
            if (link.path == "prefabs/dirt.prefab") ++dirtTiles;
        }
    }
    EXPECT_EQ(houses, 6);
    EXPECT_EQ(dirtTiles, static_cast<int>(layout.road.size() + layout.square.size()));
    // The player stands on the street.
    const auto& at = registry.get<aige::WorldTransform2D>(player);
    EXPECT_TRUE(layout.road.contains({static_cast<int>(at.x), static_cast<int>(at.y)}));
    EXPECT_NE(aige::DescribeVillage(plan).find("6 houses"), std::string::npos);
    fs::remove_all(root);
}

// The case from testing (2026-10-17): a pack never analyzed, its house
// imported at its pixel size (480 px, 7.5 cells). The ground tiles' edges come
// from their pixels, and every piece is sized to the village.
TEST(VillageGenTest, UnanalyzedPackWithHugeHousesStillBuilds) {
    const fs::path root = fs::temp_directory_path() / "aige_village_unanalyzed";
    fs::remove_all(root);
    fs::create_directories(root / "prefabs");
    fs::create_directories(root / "assets" / "pack");
    const std::array<uint8_t, 3> grass{60, 160, 60}, dirt{220, 170, 100};
    // A 64 px tile: grass, dirt, or a dirt strip running top to bottom.
    auto writeTile = [&](const std::string& name, int kind) {
        aige::ImageData image;
        image.width = image.height = 64;
        image.rgba.resize(64 * 64 * 4);
        for (uint32_t y = 0; y < 64; ++y)
            for (uint32_t x = 0; x < 64; ++x) {
                const bool isDirt = kind == 1 || (kind == 2 && x >= 22 && x < 42);
                const auto& c = isDirt ? dirt : grass;
                const size_t i = (y * 64 + x) * 4;
                image.rgba[i] = c[0];
                image.rgba[i + 1] = c[1];
                image.rgba[i + 2] = c[2];
                image.rgba[i + 3] = 255;
            }
        ASSERT_TRUE(aige::WritePng(root / "assets" / "pack" / (name + ".png"), image));
    };
    aige::PrefabLibrary prefabs{aige::Project{root}};
    for (auto [name, kind] : {std::pair{"Ground_01", 0}, std::pair{"Ground_02", 1}, std::pair{"Ground_03", 2}}) {
        writeTile(name, kind);
        aige::Registry scratch;
        aige::TileStyle style;
        style.texture = std::string("assets/pack/") + name + ".png";
        style.flat = true;
        prefabs.SaveAsPrefab(scratch, aige::CreateTile(scratch, 0, 0, style), std::string("prefabs/") + name + ".prefab");
    }
    for (auto [name, width, height] : {std::tuple{"Prop_-_House", 480.0f, 640.0f}, std::tuple{"Prop_-_Well", 300.0f, 300.0f},
                                       std::tuple{"Prop_-_Wooden_Fence_Horizontal", 400.0f, 100.0f}}) {
        aige::Registry scratch;
        prefabs.SaveAsPrefab(scratch, aige::CreateProp(scratch, 0, 0, std::string("assets/pack/") + name + ".png", width, height),
                             std::string("prefabs/") + name + ".prefab");
    }
    prefabs.Refresh();

    aige::VillageRequest request;
    request.params.houses = 4;
    std::string error;
    auto plan = aige::PlanVillage(prefabs, aige::Project{root}, request, &error);
    ASSERT_TRUE(plan.has_value()) << error;
    EXPECT_EQ(plan->roles.ground, "grass");
    EXPECT_EQ(plan->roles.road, "dirt");
    EXPECT_EQ(plan->roles.houseCells, 3); // not 7.5
    EXPECT_EQ(plan->params.houseCells, 3);
    EXPECT_NEAR(plan->roles.cells.at("prefabs/Prop_-_House.prefab"), 2.7f, 0.01f);
    EXPECT_LE(plan->roles.cells.at("prefabs/Prop_-_Well.prefab"), 1.5f);
    EXPECT_FLOAT_EQ(plan->roles.cells.at("prefabs/Prop_-_Wooden_Fence_Horizontal.prefab"), 1.0f);
    EXPECT_EQ(plan->roles.prefabs[aige::VillageRole::FenceH],
              std::vector<std::string>{"prefabs/Prop_-_Wooden_Fence_Horizontal.prefab"}); // by its name

    aige::Registry registry;
    ASSERT_FALSE(aige::BuildVillage(registry, prefabs, *plan, false).has_value());
    const float tileWidth = aige::BootstrapIso().tileWidth;
    int houses = 0, paths = 0;
    for (auto [e, link, sprite] : registry.view<aige::PrefabInstance, aige::Sprite>().each()) {
        if (link.path == "prefabs/Prop_-_House.prefab") {
            ++houses;
            EXPECT_NEAR(sprite.width, 2.7f * tileWidth, 1.0f);
            EXPECT_NEAR(sprite.height / sprite.width, 640.0f / 480.0f, 0.01f); // in proportion
        }
        if (link.path == "prefabs/Ground_03.prefab") ++paths; // the narrow path piece, somewhere
    }
    EXPECT_EQ(houses, 4);
    fs::remove_all(root);
}
