#include "assets/SceneIO.h"
#include "assets/TileLayerIO.h"
#include "editing/EditHistory.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/TileGrid.h"
#include "gameplay/TileLayer.h"
#include "gameplay/TileLayerView.h"
#include "physics2d/Components.h"
#include "renderer2d/SpriteCulling.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

// development_plan.md 7.2: big scenes.
using namespace aige;
namespace fs = std::filesystem;

namespace {

TileDef Ground(const std::string& texture, bool solid = false) {
    TileDef d;
    d.sprite.texture = texture;
    d.sprite.width = 64;
    d.sprite.height = 32;
    d.sprite.flat = true;
    d.solid = solid;
    return d;
}

fs::path TempScene(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("aige_big_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir / "main.aige";
}

using Clock = std::chrono::steady_clock;
double Ms(Clock::time_point since) { return std::chrono::duration<double, std::milli>(Clock::now() - since).count(); }

#ifdef NDEBUG
constexpr double kSlack = 1.0;
#else
constexpr double kSlack = 12.0; // Debug builds are much slower; the budgets still catch blowups
#endif

} // namespace

TEST(TileLayerTest, CellsChunksAndPalette) {
    TileLayer layer;
    const uint16_t grass = layer.Intern(Ground("grass.png"));
    const uint16_t road = layer.Intern(Ground("road.png"));
    EXPECT_EQ(layer.Intern(Ground("grass.png")), grass); // the same kind, the same index
    EXPECT_NE(grass, road);
    TileDef turned = Ground("road.png");
    turned.sprite.rotation = 1;
    EXPECT_NE(layer.Intern(turned), road);

    EXPECT_EQ(layer.Set(-1, -1, grass), TileLayer::kEmpty);
    layer.Set(0, 0, road);
    layer.Set(31, 31, grass);
    layer.Set(32, 0, grass);
    EXPECT_EQ(layer.Get(-1, -1), grass);
    EXPECT_EQ(layer.Get(0, 0), road);
    EXPECT_EQ(layer.Get(5, 5), TileLayer::kEmpty);
    EXPECT_EQ(layer.Chunks().size(), 3u); // (-1,-1), (0,0), (1,0)
    EXPECT_EQ(layer.CellCount(), 4u);
    EXPECT_EQ(layer.Bounds(), (std::array<int, 4>{-1, -1, 33, 32}));
    EXPECT_EQ(layer.ChunkBounds(), (std::array<int, 4>{-32, -32, 64, 32}));

    int inView = 0;
    layer.ForEachIn(0, 0, 32, 32, [&](int, int, uint16_t) { ++inView; });
    EXPECT_EQ(inView, 2);

    layer.Erase(-1, -1);
    EXPECT_EQ(layer.Chunks().size(), 2u); // an empty chunk goes
    EXPECT_EQ(layer.Regions().size(), 1u);
    EXPECT_EQ(layer.DirtyRegions().size(), 2u); // (-1,-1) changed too
}

TEST(TileLayerTest, CommandsUndoAndRedoAcrossSnapshots) {
    Registry registry;
    EditHistory history;
    CreateProp(registry, 1, 1, "tree.png", 32, 48);

    TileLayerEdit edit("ground");
    TileLayer& ground = LayersOf(registry).Get("ground");
    const uint16_t grass = ground.Intern(Ground("grass.png"));
    const uint16_t dirt = ground.Intern(Ground("dirt.png"));
    for (int y = 0; y < 50; ++y) {
        for (int x = 0; x < 50; ++x) edit.Set(ground, x, y, grass);
    }
    edit.Set(ground, 3, 3, dirt); // set twice: one change, from empty to dirt
    ASSERT_EQ(edit.changes.size(), 2500u);
    history.PushCommand(std::make_unique<TileLayerCommand>(std::vector<TileLayerEdit>{edit}, "Fill ground"));

    // A snapshot step on top: moving the tree.
    history.Record(registry);
    for (auto [e, w] : registry.view<WorldTransform2D>().each()) w.x = 9;

    ASSERT_TRUE(history.Undo(registry)); // the move: a whole-scene snapshot, layers untouched
    EXPECT_EQ(LayersOf(registry).Get("ground").CellCount(), 2500u);
    ASSERT_TRUE(history.Undo(registry)); // the fill
    EXPECT_EQ(LayersOf(registry).Get("ground").CellCount(), 0u);
    ASSERT_TRUE(history.Redo(registry));
    EXPECT_EQ(LayersOf(registry).Get("ground").Get(3, 3), dirt);
    EXPECT_EQ(LayersOf(registry).Get("ground").CellCount(), 2500u);
    ASSERT_TRUE(history.Redo(registry));
    for (auto [e, w] : registry.view<WorldTransform2D>().each()) EXPECT_EQ(w.x, 9);
}

TEST(TileLayerTest, BuiltEntitiesUndoByTheirPlace) {
    Registry registry;
    EditHistory history;
    CreateProp(registry, 0, 0, "mine.png", 32, 32); // not built: stays
    for (int i = 0; i < 20; ++i) {
        Entity e = CreateProp(registry, static_cast<float>(i), 2, "house.png", 64, 64);
        registry.emplace<PlaceNode>(e, "riverford", "w-old/plots#" + std::to_string(i), "plots[" + std::to_string(i) + "].house");
    }
    history.PushCommand(std::make_unique<PlaceEntitiesCommand>("riverford"));
    // A snapshot step in between recreates every entity under new ids.
    history.Record(registry);
    registry.emplace<Name>(registry.create(), "marker");

    ASSERT_TRUE(history.Undo(registry));
    ASSERT_TRUE(history.Undo(registry));
    EXPECT_EQ(registry.view<PlaceNode>().size(), 0u);
    EXPECT_EQ(registry.view<WorldTransform2D>().size(), 1u);
    ASSERT_TRUE(history.Redo(registry));
    ASSERT_EQ(registry.view<PlaceNode>().size(), 20u);
    bool found = false;
    for (auto [e, node] : registry.view<PlaceNode>().each()) found = found || node.statement == "plots[7].house";
    EXPECT_TRUE(found);
}

TEST(TileLayerTest, SavesOnlyChangedRegionsAndLoadsBack) {
    const fs::path scene = TempScene("regions");
    Registry registry;
    TileLayers& layers = LayersOf(registry);
    TileLayer& ground = layers.Get("ground");
    TileLayer& roads = layers.Get("roads");
    roads.order = 1;
    const uint16_t grass = ground.Intern(Ground("grass.png"));
    const uint16_t road = roads.Intern(Ground("road.png"));
    for (int y = 0; y < 300; ++y) {
        for (int x = 0; x < 300; ++x) ground.Set(x, y, grass);
    }
    for (int x = 0; x < 300; ++x) roads.Set(x, 150, road);
    Entity house = CreateProp(registry, 5, 5, "house.png", 64, 64);
    registry.emplace<PlaceNode>(house, "riverford", "w-old/plots#1", "plots[1].house");
    SaveScene(registry, scene.string());

    const fs::path dir = TileLayersDir(scene);
    ASSERT_TRUE(fs::exists(dir / "layers.json"));
    EXPECT_EQ(std::distance(fs::directory_iterator(dir / "ground"), fs::directory_iterator()), 9); // 3x3 regions of 128

    // One cell changes: one region file is rewritten.
    const auto before = fs::last_write_time(dir / "ground" / "r_2_2.bin");
    const auto changedBefore = fs::last_write_time(dir / "ground" / "r_0_0.bin");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ground.Erase(1, 1);
    SaveScene(registry, scene.string());
    EXPECT_EQ(fs::last_write_time(dir / "ground" / "r_2_2.bin"), before);
    EXPECT_NE(fs::last_write_time(dir / "ground" / "r_0_0.bin"), changedBefore);

    Registry loaded;
    ASSERT_TRUE(LoadScene(loaded, scene.string()));
    const TileLayers* back = FindLayers(loaded);
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->Find("ground")->CellCount(), 300u * 300u - 1);
    EXPECT_EQ(back->Find("ground")->Get(1, 1), TileLayer::kEmpty);
    EXPECT_EQ(back->Find("roads")->Get(299, 150), road);
    EXPECT_EQ(back->Find("roads")->order, 1);
    EXPECT_EQ(back->Find("roads")->Def(road)->sprite.texture, "road.png");
    ASSERT_EQ(loaded.view<PlaceNode>().size(), 1u);
    EXPECT_EQ(loaded.get<PlaceNode>(loaded.view<PlaceNode>().front()).statement, "plots[1].house");

    // A removed layer's folder goes; saving elsewhere writes everything there.
    LayersOf(loaded).Remove("roads");
    SaveScene(loaded, scene.string());
    EXPECT_FALSE(fs::exists(dir / "roads"));
    const fs::path copy = scene.parent_path() / "copy.aige";
    SaveScene(loaded, copy.string());
    EXPECT_EQ(std::distance(fs::directory_iterator(TileLayersDir(copy) / "ground"), fs::directory_iterator()), 9);

    // A damaged region is reported, the rest still loads.
    std::ofstream(dir / "ground" / "r_1_1.bin", std::ios::binary) << "junk";
    Registry damaged;
    LoadScene(damaged, scene.string());
    EXPECT_NE(LoadTileLayers(damaged, scene).find("r_1_1.bin"), std::string::npos);
    EXPECT_GT(FindLayers(damaged)->Find("ground")->CellCount(), 0u);

    // A plain scene file (no layers) clears what a previous scene had.
    Registry reused;
    LayersOf(reused).Get("old").Set(0, 0, LayersOf(reused).Get("old").Intern(Ground("x.png")));
    std::ofstream(scene.parent_path() / "plain.aige") << "# AIGE Scene v1\n";
    LoadScene(reused, (scene.parent_path() / "plain.aige").string());
    EXPECT_TRUE(FindLayers(reused) == nullptr || FindLayers(reused)->layers.empty());
    fs::remove_all(scene.parent_path());
}

TEST(TileLayerTest, CollisionReadsLayers) {
    Registry registry;
    TileLayer& ground = LayersOf(registry).Get("ground");
    TileLayer& water = LayersOf(registry).Get("water");
    water.order = 1;
    const uint16_t grass = ground.Intern(Ground("grass.png"));
    const uint16_t pond = water.Intern(Ground("water.png", true));
    for (int x = 0; x < 10; ++x) ground.Set(x, 0, grass);
    water.Set(4, 0, pond);
    CreateTile(registry, 6, 0, TileKind::Wall); // a painted tile wins
    Entity rock = registry.create();
    registry.emplace<Obstacle>(rock);
    registry.emplace<WorldTransform2D>(rock, 8.5f, 0.5f);

    const TileGrid grid = TileGrid::Build(registry);
    EXPECT_FALSE(grid.IsSolid(0, 0));
    EXPECT_TRUE(grid.IsSolid(4, 0));  // the top layer
    EXPECT_TRUE(grid.IsSolid(6, 0));  // the painted wall
    EXPECT_TRUE(grid.IsSolid(8, 0));  // an obstacle on a layer cell
    EXPECT_TRUE(grid.IsSolid(20, 0)); // nothing: solid
}

TEST(TileLayerTest, OnlyCellsInViewAreDrawn) {
    Registry registry;
    TileLayer& ground = LayersOf(registry).Get("ground");
    TileLayer& roads = LayersOf(registry).Get("roads");
    roads.order = 1;
    const uint16_t grass = ground.Intern(Ground("grass.png"));
    const uint16_t road = roads.Intern(Ground("road.png"));
    for (int y = 0; y < 400; ++y) {
        for (int x = 0; x < 400; ++x) ground.Set(x, y, grass);
    }
    roads.Set(200, 200, road);
    IsoParams iso;
    Camera2D camera;
    // Centre the camera on cell (200, 200).
    const ScreenPose centre = ProjectIso(200.5f, 200.5f, iso);
    camera.offsetX = centre.x - 640;
    camera.offsetY = centre.y - 360;
    std::vector<SpriteItem> items;
    size_t culled = 0;
    CollectLayerSprites(registry, iso, camera, 1280, 720, items, &culled);
    // A 1280x720 view of 64x32 diamonds shows about 20 x 22 x 2 cells.
    EXPECT_GT(items.size(), 500u);
    EXPECT_LT(items.size(), 3000u);
    EXPECT_LT(items.size() + culled, 20000u); // never the whole 160k
    // At the road's cell: the ground first, then the road, at one depth.
    int groundAt = -1, roadAt = -1;
    const ScreenTransform2D t = LayerCellTransform(200, 200, ground.Def(grass)->sprite, iso);
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].transform.x == t.x && items[i].transform.y == t.y) {
            (items[i].sprite->texture == "road.png" ? roadAt : groundAt) = static_cast<int>(i);
        }
    }
    ASSERT_GE(groundAt, 0);
    ASSERT_GE(roadAt, 0);
    EXPECT_LT(groundAt, roadAt);
}

// The milestone's test: a 400x400 place builds, saves, loads and undoes
// within budgets, and 100k sprites cull fast.
TEST(BigSceneTest, FourHundredSquarePlaceWithinBudgets) {
    const fs::path scene = TempScene("400");
    Registry registry;
    EditHistory history;

    // Build: ground everywhere, a road grid, and 6000 built objects.
    auto start = Clock::now();
    TileLayer& ground = LayersOf(registry).Get("ground");
    TileLayer& roads = LayersOf(registry).Get("roads");
    roads.order = 1;
    const uint16_t grass = ground.Intern(Ground("grass.png"));
    const uint16_t dirt = ground.Intern(Ground("dirt.png"));
    const uint16_t road = roads.Intern(Ground("road.png"));
    TileLayerEdit groundEdit("ground"), roadEdit("roads");
    for (int y = 0; y < 400; ++y) {
        for (int x = 0; x < 400; ++x) groundEdit.Set(ground, x, y, (x * 7 + y * 13) % 11 == 0 ? dirt : grass);
    }
    for (int i = 0; i < 400; ++i) {
        for (int k = 20; k < 400; k += 40) {
            roadEdit.Set(roads, i, k, road);
            roadEdit.Set(roads, k, i, road);
        }
    }
    for (int i = 0; i < 6000; ++i) {
        Entity e = CreateProp(registry, static_cast<float>(i % 400) + 0.5f, static_cast<float>(i / 15 % 400) + 0.5f,
                              i % 3 ? "tree.png" : "house.png", 48, 64);
        registry.emplace<PlaceNode>(e, "big", "n" + std::to_string(i % 50), "s" + std::to_string(i));
    }
    auto build = std::make_unique<CompoundCommand>("Build big");
    build->Add(std::make_unique<TileLayerCommand>(std::vector<TileLayerEdit>{groundEdit, roadEdit}));
    build->Add(std::make_unique<PlaceEntitiesCommand>("big"));
    history.PushCommand(std::move(build));
    const double buildMs = Ms(start);
    EXPECT_EQ(ground.CellCount(), 160000u);

    start = Clock::now();
    SaveScene(registry, scene.string());
    const double saveMs = Ms(start);

    start = Clock::now();
    Registry loaded;
    ASSERT_TRUE(LoadScene(loaded, scene.string()));
    const double loadMs = Ms(start);
    EXPECT_EQ(FindLayers(loaded)->Find("ground")->CellCount(), 160000u);
    EXPECT_EQ(loaded.view<PlaceNode>().size(), 6000u);

    start = Clock::now();
    ASSERT_TRUE(history.Undo(registry));
    const double undoMs = Ms(start);
    EXPECT_EQ(LayersOf(registry).Get("ground").CellCount(), 0u);
    EXPECT_EQ(registry.view<PlaceNode>().size(), 0u);
    start = Clock::now();
    ASSERT_TRUE(history.Redo(registry));
    const double redoMs = Ms(start);
    EXPECT_EQ(LayersOf(registry).Get("ground").CellCount(), 160000u);
    EXPECT_EQ(registry.view<PlaceNode>().size(), 6000u);

    // A frame's worth of work: positions, the layer cells in view, the sprites in view.
    IsoParams iso;
    UpdateWorldToScreen(registry, iso);
    Camera2D camera;
    const ScreenPose centre = ProjectIso(200, 200, iso);
    camera.offsetX = centre.x - 960;
    camera.offsetY = centre.y - 540;
    start = Clock::now();
    std::vector<SpriteItem> items;
    CollectLayerSprites(registry, iso, camera, 1920, 1080, items);
    const auto visible = VisibleSprites(registry, camera, 1920, 1080);
    const double frameMs = Ms(start);

    std::cout << "[ big scene ] build " << buildMs << " ms, save " << saveMs << " ms, load " << loadMs << " ms, undo "
              << undoMs << " ms, redo " << redoMs << " ms, frame cull " << frameMs << " ms (" << items.size()
              << " cells, " << visible.size() << " sprites)" << std::endl;
    EXPECT_LT(buildMs, 400 * kSlack);
    EXPECT_LT(saveMs, 500 * kSlack);
    EXPECT_LT(loadMs, 500 * kSlack);
    EXPECT_LT(undoMs, 300 * kSlack);
    EXPECT_LT(redoMs, 300 * kSlack);
    EXPECT_LT(frameMs, 8 * kSlack);
    fs::remove_all(scene.parent_path());
}

TEST(BigSceneTest, HundredThousandSpritesCullFast) {
    Registry registry;
    for (int i = 0; i < 100000; ++i) {
        Entity e = registry.create();
        registry.emplace<ScreenTransform2D>(e, static_cast<float>(i % 1000) * 40.0f, static_cast<float>(i / 1000) * 40.0f,
                                            static_cast<float>(i));
        auto& s = registry.emplace<Sprite>(e);
        s.width = 32;
        s.height = 48;
    }
    Camera2D camera;
    camera.offsetX = 10000;
    camera.offsetY = 1000;
    size_t culled = 0;
    auto start = Clock::now();
    const auto visible = VisibleSprites(registry, camera, 1920, 1080, &culled);
    const double ms = Ms(start);
    std::cout << "[ 100k sprites ] cull " << ms << " ms, " << visible.size() << " visible" << std::endl;
    EXPECT_EQ(visible.size() + culled, 100000u);
    EXPECT_GT(visible.size(), 1000u);
    EXPECT_LT(ms, 10 * kSlack);
}
