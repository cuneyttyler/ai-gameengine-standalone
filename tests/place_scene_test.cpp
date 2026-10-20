#include "assets/AssetDatabase.h"
#include "assets/Catalog.h"
#include "assets/Prefab.h"
#include "editing/Autotile.h"
#include "editing/EditHistory.h"
#include "editing/PlaceBuild.h"
#include "editing/PlaceScene.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "gameplay/TileGrid.h"
#include "gameplay/TileLayer.h"
#include "physics2d/Components.h"
#include "places/Brief.h"
#include "places/Ontology.h"
#include "places/Planner.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <set>

// development_plan.md 7.7: the solved layout into the scene, verified and repaired.
// In progress: the scene build tests are disabled until 7.7 is finished.
using namespace aige;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

const Ontology& Library() {
    static Ontology o = [] {
        Ontology ontology;
        ontology.Load({EnginePlacesDir()});
        return ontology;
    }();
    return o;
}

// A project with grass and dirt ground tiles (the autotiler's set), a house
// image with its prefab, and a village planned and laid out without the AI.
struct Village {
    fs::path root;
    std::unique_ptr<PrefabLibrary> prefabs;
    std::unique_ptr<AssetDatabase> assets;
    TerrainSet terrains;
    json spec, solved;
    PlaceSceneOptions options;

    Village() {
        root = fs::temp_directory_path() / ("aige_place_scene_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root);
        fs::create_directories(root / "prefabs");
        fs::create_directories(root / "assets" / "pack");
        prefabs = std::make_unique<PrefabLibrary>(Project{root});
        assets = std::make_unique<AssetDatabase>(Project{root});
        for (const char* name : {"grass", "dirt"}) {
            Registry scratch;
            TileStyle style;
            style.texture = std::string("assets/pack/") + name + ".png";
            style.flat = true;
            prefabs->SaveAsPrefab(scratch, CreateTile(scratch, 0, 0, style), std::string("prefabs/") + name + ".prefab");
        }
        {
            Registry scratch;
            prefabs->SaveAsPrefab(scratch, CreateProp(scratch, 0, 0, "assets/pack/house.png", 128, 128), "prefabs/house.prefab");
        }
        prefabs->Refresh();
        terrains = BuildTerrainSet(*prefabs, [](const std::string& asset) {
            const std::string t = fs::path(asset).stem().string();
            std::map<std::string, std::string> fields;
            if (t == "grass" || t == "dirt") {
                fields["edges"] = t + "," + t + "," + t + "," + t + "," + t + "," + t + "," + t + "," + t;
                fields["center"] = t;
            }
            return fields;
        });
        // Planned and solved without the AI.
        PlaceBuild::Host host;
        host.ontology = []() -> const Ontology& { return Library(); };
        host.catalog = [] { return Catalog{}; };
        host.isoGrid = [] { return true; };
        host.request = [](const std::string&, const json&, int) { return 0; };
        host.projectRoot = root;
        PlaceBuild build(host);
        EXPECT_EQ(build.Start("a village with gardens"), "");
        EXPECT_EQ(build.Step("assets"), "");
        EXPECT_EQ(build.Confirm(), "");
        EXPECT_EQ(build.Approve(), "");
        spec = build.Spec();
        solved = build.Solved();
        options.place = build.Id();
        options.originX = 40;
        options.originY = 12;
        options.isoGrid = true;
    }
    ~Village() { fs::remove_all(root); }

    const json* Check(const json& checks, const std::string& id) {
        for (const auto& c : checks) {
            if (c["id"] == id) return &c;
        }
        return nullptr;
    }
    size_t Built(Registry& registry) {
        size_t n = 0;
        for (auto [e, node] : registry.view<PlaceNode>().each()) n += node.place == options.place;
        return n;
    }
};

std::string Describe(const json& checks) {
    std::string out;
    for (const auto& c : checks) {
        if (!c.value("ok", true)) out += "  " + c.value("id", "") + ": " + c.value("detail", "") + " " + c.value("items", json::array()).dump() + "\n";
    }
    return out;
}

} // namespace

TEST(PlaceSceneTest, DISABLED_AVillageIsBuiltIntoTheSceneBesideWhatsThere) {
    Village v;
    ASSERT_TRUE(v.solved.is_object());
    Registry registry;
    // The user's own things, at the origin: never touched.
    Entity mine = CreateProp(registry, 2.5f, 3.5f, "assets/pack/house.png", 64, 64);
    registry.emplace<Name>(mine, "my house");
    const auto origin = FreeOrigin(registry, v.options.place, 80);
    EXPECT_EQ(origin, (std::pair{2 + 1 + 8, 3}));

    const json plan = PlanPlaceAssets(Library(), v.spec, v.solved, Catalog{}, *v.prefabs, *v.assets, v.terrains, v.options);
    // No art for most things: placeholders, said so; the project's grass and dirt for the ground.
    ASSERT_FALSE(plan["needs"].empty());
    for (const auto& n : plan["needs"]) {
        EXPECT_EQ(n["source"], "placeholder") << n.dump();
        EXPECT_FALSE(n.value("prefab", "").empty()) << n.dump();
    }
    EXPECT_EQ(plan["terrains"]["grass"]["terrain"], "grass");
    EXPECT_EQ(plan["terrains"]["dirt"]["terrain"], "dirt");
    EXPECT_TRUE(fs::exists(v.root / "assets" / "generated" / "placeholders"));

    json report;
    auto command = BuildPlaceScene(registry, *v.prefabs, v.solved, plan, v.terrains, v.options, &report);
    ASSERT_NE(command, nullptr);
    EXPECT_TRUE(report["missing"].empty()) << report["missing"].dump();
    EXPECT_GT(report["objects"].value("building", 0), 10);
    const TileLayer* ground = LayersOf(registry).Find("place:" + v.options.place);
    ASSERT_NE(ground, nullptr);
    const int side = v.solved["grid"]["bounds"][2].get<int>();
    EXPECT_EQ(ground->CellCount(), static_cast<size_t>(side * v.solved["grid"]["bounds"][3].get<int>()));
    EXPECT_NE(ground->Get(v.options.originX, v.options.originY), TileLayer::kEmpty);
    EXPECT_EQ(ground->Get(v.options.originX - 1, v.options.originY), TileLayer::kEmpty);
    ASSERT_TRUE(registry.valid(mine));

    const json checks = VerifyPlaceScene(registry, v.solved, plan, v.options);
    for (const char* id : {"objects", "positions", "ground", "doors", "overlaps"}) {
        const json* c = v.Check(checks, id);
        ASSERT_NE(c, nullptr) << id;
        EXPECT_TRUE((*c)["ok"].get<bool>()) << id << ": " << c->dump();
    }
    EXPECT_FALSE((*v.Check(checks, "art"))["ok"].get<bool>()); // placeholders, a warning

    // A building blocks its footprint.
    const TileGrid grid = TileGrid::Build(registry);
    for (const auto& o : v.solved["objects"]) {
        if (o.value("layer", "") != "building") continue;
        for (int y = 0; y < o["h"].get<int>(); ++y) {
            for (int x = 0; x < o["w"].get<int>(); ++x) {
                EXPECT_TRUE(grid.IsSolid(v.options.originX + o["x"].get<int>() + x, v.options.originY + o["y"].get<int>() + y)) << o.dump();
            }
        }
        break;
    }

    // One undo step: out of the scene and back.
    EditHistory history;
    const size_t built = v.Built(registry);
    history.PushCommand(std::move(command));
    ASSERT_TRUE(history.Undo(registry));
    EXPECT_EQ(v.Built(registry), 0u);
    EXPECT_EQ(LayersOf(registry).Get("place:" + v.options.place).CellCount(), 0u);
    EXPECT_TRUE(registry.valid(mine));
    ASSERT_TRUE(history.Redo(registry));
    EXPECT_EQ(v.Built(registry), built);

    // A rebuild replaces the place (and only it); its undo brings the first build back.
    json again;
    auto rebuild = BuildPlaceScene(registry, *v.prefabs, v.solved, plan, v.terrains, v.options, &again);
    EXPECT_EQ(v.Built(registry), built);
    history.PushCommand(std::move(rebuild));
    ASSERT_TRUE(history.Undo(registry));
    EXPECT_EQ(v.Built(registry), built);
    EXPECT_EQ(LayersOf(registry).Get("place:" + v.options.place).CellCount(), ground->CellCount());
    size_t names = 0;
    for (auto [e, n] : registry.view<Name>().each()) names += n.value == "my house";
    EXPECT_EQ(names, 1u);
}

TEST(PlaceSceneTest, DISABLED_InjectedFailuresAreFoundAndRepaired) {
    Village v;
    Registry registry;
    const json plan = PlanPlaceAssets(Library(), v.spec, v.solved, Catalog{}, *v.prefabs, *v.assets, v.terrains, v.options);
    json report;
    auto command = BuildPlaceScene(registry, *v.prefabs, v.solved, plan, v.terrains, v.options, &report);
    // A building lost, and a crate dropped on another's door.
    const json* house = nullptr;
    const json* other = nullptr;
    for (const auto& o : v.solved["objects"]) {
        if (o.value("layer", "") != "building" || !o.value("primary", false)) continue;
        if (!house) house = &o;
        else if (!other) other = &o;
    }
    ASSERT_TRUE(house && other);
    for (auto [e, node] : registry.view<PlaceNode>().each()) {
        if (node.statement == (*house)["id"].get<std::string>()) {
            registry.destroy(e);
            break;
        }
    }
    const int dx = v.options.originX + (*other)["x"].get<int>() + (*other)["w"].get<int>() / 2;
    const int dy = v.options.originY + (*other)["y"].get<int>() + (*other)["h"].get<int>();
    const std::string facing = (*other).value("facing", "s");
    ASSERT_EQ(facing, "s") << "the test puts the crate south of the door";
    Entity crate = CreateProp(registry, dx + 0.5f, dy + 0.5f, "assets/pack/crate.png", 16, 16);
    registry.emplace<Obstacle>(crate);
    registry.emplace<PlaceNode>(crate, v.options.place, "x", "x/crate");

    const json before = VerifyPlaceScene(registry, v.solved, plan, v.options);
    EXPECT_FALSE((*v.Check(before, "objects"))["ok"].get<bool>());
    EXPECT_FALSE((*v.Check(before, "doors"))["ok"].get<bool>()) << Describe(before);

    const json repairs = RepairPlaceScene(registry, *v.prefabs, v.solved, plan, before, v.options);
    EXPECT_GE(repairs.size(), 2u) << repairs.dump();
    EXPECT_NE(repairs.dump().find((*house)["id"].get<std::string>()), std::string::npos);
    EXPECT_NE(repairs.dump().find("x/crate"), std::string::npos);
    const json after = VerifyPlaceScene(registry, v.solved, plan, v.options);
    for (const char* id : {"objects", "doors", "overlaps"}) EXPECT_TRUE((*v.Check(after, id))["ok"].get<bool>()) << id << Describe(after);
}

TEST(PlaceSceneTest, DISABLED_TheProjectsArtIsUsedWhereItFits) {
    Village v;
    // The house image, catalogued as a house (no cottage drawn: the nearest is fine).
    Catalog catalog;
    CatalogEntry house;
    house.id = house.path = "assets/pack/house.png";
    house.name = "brick house";
    house.roles = {"house"};
    house.affordance = "environmental-object";
    house.perspective = "isometric";
    house.layer = "building";
    house.footprint.w = 3;
    house.footprint.h = 3;
    house.rect = {0, 0, 128, 128};
    catalog.entries.push_back(house);
    const json plan = PlanPlaceAssets(Library(), v.spec, v.solved, catalog, *v.prefabs, *v.assets, v.terrains, v.options);
    bool used = false;
    for (const auto& n : plan["needs"]) {
        if (n.value("role", "") != "house") continue;
        if (n.value("source", "") == "project") {
            used = true;
            EXPECT_EQ(n["prefab"], "prefabs/house.prefab"); // its existing prefab
            EXPECT_TRUE(n.contains("nearest")) << n.dump();  // a cottage was asked for
        } else {
            ADD_FAILURE() << n.dump();
        }
    }
    EXPECT_TRUE(used) << plan["needs"].dump();
}

TEST(PlaceSceneTest, ObstaclesBlockTheirFootprint) {
    Registry registry;
    Entity house = registry.create();
    registry.emplace<WorldTransform2D>(house, 10.5f, 10.5f);
    registry.emplace<Obstacle>(house, 2, 0, 1); // 3 wide to the left, 2 deep
    TileLayer& ground = LayersOf(registry).Get("g");
    TileDef def;
    def.sprite.texture = "g.png";
    const uint16_t id = ground.Intern(def);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x) ground.Set(x, y, id);
    const TileGrid grid = TileGrid::Build(registry);
    EXPECT_TRUE(grid.IsSolid(10, 10));
    EXPECT_TRUE(grid.IsSolid(8, 10));
    EXPECT_TRUE(grid.IsSolid(8, 9));
    EXPECT_FALSE(grid.IsSolid(7, 10));
    EXPECT_FALSE(grid.IsSolid(11, 10));
    EXPECT_FALSE(grid.IsSolid(10, 8));
    EXPECT_FALSE(grid.IsSolid(10, 11));
}

