#include <gtest/gtest.h>

#include "assets/Prefab.h"
#include "editing/Autotile.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "renderer2d/Components.h"

#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;

// development_plan.md 6.4: say where the road goes; the pieces fit.
namespace {

// NW, N, NE, E, SE, S, SW, W.
const std::map<std::string, std::pair<std::string, std::string>> kTiles = {
    {"grass", {"grass,grass,grass,grass,grass,grass,grass,grass", "grass"}},
    {"dirt", {"dirt,dirt,dirt,dirt,dirt,dirt,dirt,dirt", "dirt"}},
    {"path", {"grass,dirt,grass,grass,grass,dirt,grass,grass", "dirt"}}, // runs N-S
    {"end", {"grass,grass,grass,grass,grass,dirt,grass,grass", "dirt"}},  // opens to the S
    {"bend", {"grass,dirt,grass,dirt,grass,grass,grass,grass", "dirt"}}, // joins N and E
};

struct Set {
    Set() {
        root = fs::temp_directory_path() / ("aige_autotile_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root);
        fs::create_directories(root / "prefabs");
        prefabs = std::make_unique<aige::PrefabLibrary>(aige::Project{root});
        for (const auto& [name, _] : kTiles) {
            aige::Registry scratch;
            aige::TileStyle style;
            style.texture = "assets/pack/" + name + ".png";
            style.flat = true;
            prefabs->SaveAsPrefab(scratch, aige::CreateTile(scratch, 0, 0, style), "prefabs/" + name + ".prefab");
        }
        prefabs->Refresh();
        set = aige::BuildTerrainSet(*prefabs, [](const std::string& asset) {
            const std::string name = fs::path(asset).stem().string();
            const auto it = kTiles.find(name);
            std::map<std::string, std::string> fields;
            if (it != kTiles.end()) {
                fields["edges"] = it->second.first;
                fields["center"] = it->second.second;
            }
            return fields;
        });
    }
    ~Set() { fs::remove_all(root); }
    fs::path root;
    std::unique_ptr<aige::PrefabLibrary> prefabs;
    aige::TerrainSet set;
};

// A terrain map from a set of road cells; grass elsewhere.
std::function<std::string(int, int)> RoadAt(std::set<std::pair<int, int>> road) {
    return [road](int x, int y) { return road.contains({x, y}) ? std::string("dirt") : std::string("grass"); };
}

aige::TileChoice Pick(const Set& s, const std::set<std::pair<int, int>>& road, int x, int y) {
    const auto at = RoadAt(road);
    auto choice = aige::ChooseTile(s.set, aige::TargetSignature(at, x, y, "grass"), at(x, y));
    EXPECT_TRUE(choice.has_value());
    return choice.value_or(aige::TileChoice{});
}

} // namespace

TEST(AutotileTest, TheSetComesFromAnalyzedTilePrefabs) {
    Set s;
    EXPECT_EQ(s.set.tiles.size(), 5u);
    EXPECT_EQ(s.set.terrains, (std::vector<std::string>{"dirt", "grass"}));
    EXPECT_EQ(s.set.PlainTile("grass"), "prefabs/grass.prefab");
    EXPECT_EQ(s.set.TerrainOf("prefabs/path.prefab"), "dirt");
    EXPECT_EQ(aige::DefaultBackground(s.set, "dirt"), "grass");
}

TEST(AutotileTest, StraightRoadsPickThePathPieceTurnedToFit) {
    Set s;
    const std::set<std::pair<int, int>> northSouth{{5, 4}, {5, 5}, {5, 6}};
    auto middle = Pick(s, northSouth, 5, 5);
    EXPECT_EQ(middle.prefab, "prefabs/path.prefab");
    EXPECT_EQ(middle.rotation, 0);
    EXPECT_EQ(middle.mismatches, 0);

    const std::set<std::pair<int, int>> eastWest{{4, 5}, {5, 5}, {6, 5}};
    middle = Pick(s, eastWest, 5, 5);
    EXPECT_EQ(middle.prefab, "prefabs/path.prefab");
    EXPECT_EQ(middle.rotation % 2, 1); // a quarter turn either way: the pack has no left-right piece
    EXPECT_EQ(middle.mismatches, 0);
}

TEST(AutotileTest, EndsBendsAndAreas) {
    Set s;
    const std::set<std::pair<int, int>> road{{5, 4}, {5, 5}, {5, 6}};
    auto top = Pick(s, road, 5, 4);
    EXPECT_EQ(top.prefab, "prefabs/end.prefab");
    EXPECT_EQ(top.rotation, 0); // opens to the south, as drawn
    auto bottom = Pick(s, road, 5, 6);
    EXPECT_EQ(bottom.prefab, "prefabs/end.prefab");
    EXPECT_EQ(bottom.rotation, 2);

    const std::set<std::pair<int, int>> corner{{5, 4}, {5, 5}, {6, 5}};
    auto bend = Pick(s, corner, 5, 5);
    EXPECT_EQ(bend.prefab, "prefabs/bend.prefab");
    EXPECT_EQ(bend.rotation, 0);
    EXPECT_EQ(bend.mismatches, 0);

    std::set<std::pair<int, int>> square;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) square.insert({x, y});
    EXPECT_EQ(Pick(s, square, 1, 1).prefab, "prefabs/dirt.prefab"); // the middle of an area
    EXPECT_EQ(Pick(s, square, 7, 7).prefab, "prefabs/grass.prefab"); // well away from it
}

TEST(AutotileTest, PaintingFitsTheRoadAndLeavesTheFieldAlone) {
    Set s;
    aige::Registry registry;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) s.prefabs->Instantiate(registry, "prefabs/grass.prefab", x + 0.5f, y + 0.5f);
    const auto cells = aige::PathCells({{2, 2}, {2, 6}, {7, 6}}, 1);
    const int changed = aige::PaintTerrain(registry, *s.prefabs, s.set, cells, "dirt", "grass");
    EXPECT_EQ(changed, static_cast<int>(cells.size())); // only the road changed; the grass around it already fits

    std::map<std::pair<int, int>, std::pair<std::string, int>> at;
    for (auto [e, tile, link] : registry.view<aige::Tile, aige::PrefabInstance>().each()) {
        at[{tile.gx, tile.gy}] = {link.path, registry.get<aige::Sprite>(e).rotation};
    }
    EXPECT_EQ(at.size(), 100u); // still one tile per cell
    EXPECT_EQ(at[{2, 2}].first, "prefabs/end.prefab");
    EXPECT_EQ(at[{2, 4}].first, "prefabs/path.prefab");
    EXPECT_EQ(at[{4, 6}].first, "prefabs/path.prefab");
    EXPECT_EQ(at[{4, 6}].second % 2, 1);
    EXPECT_EQ(at[{2, 6}].first, "prefabs/bend.prefab"); // the corner where it turns east
    EXPECT_EQ(at[{0, 0}].first, "prefabs/grass.prefab");

    // Painting the same again changes nothing.
    EXPECT_EQ(aige::PaintTerrain(registry, *s.prefabs, s.set, cells, "dirt", "grass"), 0);
}

TEST(AutotileTest, PathsAndCellLists) {
    EXPECT_EQ(aige::ParseCells("3,4;10,4"), (std::vector<std::pair<int, int>>{{3, 4}, {10, 4}}));
    EXPECT_TRUE(aige::ParseCells("3;4").empty());
    const auto cells = aige::PathCells({{0, 0}, {3, 0}, {3, 2}}, 1);
    EXPECT_EQ(cells.size(), 6u); // (0..3, 0) and (3, 1..2)
    EXPECT_EQ(aige::PathCells({{0, 0}, {4, 0}}, 3).size(), 15u); // 5 long, 3 wide
}
