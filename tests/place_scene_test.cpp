#include "assets/AssetDatabase.h"
#include "assets/Catalog.h"
#include "assets/Prefab.h"
#include "editing/Autotile.h"
#include "editing/EditHistory.h"
#include "editing/PlaceBuild.h"
#include "editing/PlaceScene.h"
#include "assets/TextureCook.h"
#include "editing/ScenePreview.h"
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
#include <iostream>
#include <fstream>
#include <functional>
#include <map>
#include <set>

// development_plan.md 7.7: the solved layout into the scene, verified and repaired.

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

TEST(PlaceSceneTest, AVillageIsBuiltIntoTheSceneBesideWhatsThere) {
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

TEST(PlaceSceneTest, InjectedFailuresAreFoundAndRepaired) {
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
    // The cell in front of its door, whichever way it faces.
    const int ox = v.options.originX + (*other)["x"].get<int>(), oy = v.options.originY + (*other)["y"].get<int>();
    const int ow = (*other)["w"].get<int>(), oh = (*other)["h"].get<int>();
    const std::string facing = (*other).value("facing", "s");
    int dx = ox + ow / 2, dy = oy + oh;
    if (facing == "n") dy = oy - 1;
    if (facing == "w") dx = ox - 1, dy = oy + oh / 2;
    if (facing == "e") dx = ox + ow, dy = oy + oh / 2;
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

TEST(PlaceSceneTest, TheProjectsArtIsUsedWhereItFits) {
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
            // A kind asked for (a cottage) that no art shows: the nearest, said so.
            if (!n["concepts"].empty()) EXPECT_TRUE(n.contains("nearest")) << n.dump();
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


// 7.8: an edit replaces only its parts in the scene; a part edited by hand
// (and so locked) keeps the edit when what holds it is built again.
TEST(PlaceSceneTest, AnEditReplacesOnlyItsPartsAndKeepsHandEdits) {
    Village v;
    Registry registry;
    const json plan = PlanPlaceAssets(Library(), v.spec, v.solved, Catalog{}, *v.prefabs, *v.assets, v.terrains, v.options);
    json report;
    EditHistory history;
    history.PushCommand(BuildPlaceScene(registry, *v.prefabs, v.solved, plan, v.terrains, v.options, &report));
    // A house of the residential ward, moved by hand: its part is locked.
    Entity moved = entt::null;
    std::string part;
    for (auto [e, node] : registry.view<PlaceNode>().each()) {
        if (node.statement.rfind("residential-ward/", 0) == 0 && node.statement.find('#') != std::string::npos &&
            registry.all_of<Obstacle>(e)) {
            moved = e;
            part = node.statement.substr(0, node.statement.find('/', node.statement.find('#')));
            break;
        }
    }
    ASSERT_TRUE(moved != entt::null);
    auto& world = registry.get<WorldTransform2D>(moved);
    world.x += 1.0f;
    const float movedX = world.x, movedY = world.y;
    const std::string movedStatement = registry.get<PlaceNode>(moved).statement;
    // Everything outside the ward, as it stands.
    std::map<std::string, std::pair<float, float>> outside;
    for (auto [e, node, w] : registry.view<PlaceNode, WorldTransform2D>().each()) {
        if (node.statement.rfind("residential-ward", 0) != 0) outside[node.statement] = {w.x, w.y};
    }
    // The ward built again, the moved house locked.
    PlaceSceneOptions edit = v.options;
    edit.only = {"residential-ward"};
    edit.locked = {part};
    history.PushCommand(BuildPlaceScene(registry, *v.prefabs, v.solved, plan, v.terrains, edit, &report));
    bool kept = false;
    for (auto [e, node, w] : registry.view<PlaceNode, WorldTransform2D>().each()) {
        if (node.statement == movedStatement) kept = w.x == movedX && w.y == movedY;
    }
    EXPECT_TRUE(kept) << "the hand-moved house went back";
    for (auto [e, node, w] : registry.view<PlaceNode, WorldTransform2D>().each()) {
        if (auto it = outside.find(node.statement); it != outside.end()) {
            EXPECT_EQ(std::pair(w.x, w.y), it->second) << node.statement;
        }
    }
    size_t count = 0;
    for (auto [e, node] : registry.view<PlaceNode>().each()) count += node.statement.rfind(movedStatement, 0) == 0 && node.statement == movedStatement;
    EXPECT_EQ(count, 1u); // not built twice
    // The edit is one undo step: back to the first build, with the hand move.
    ASSERT_TRUE(history.Undo(registry));
    EXPECT_GT(v.Built(registry), 0u);
}

// AIGE_SCENE_BUILD=<project root>|<build id>: a COPY of the project (never the
// project itself), the build put into an empty scene with the project's own
// art, verified, and drawn to AIGE_SCENE_PNG (a diagnostic; skipped otherwise).
TEST(PlaceSceneTest, BuildsAGivenProjectsPlace) {
    const char* arg = std::getenv("AIGE_SCENE_BUILD");
    if (arg == nullptr) GTEST_SKIP() << "set AIGE_SCENE_BUILD=<project root>|<build id>";
    const std::string text = arg;
    const fs::path from = text.substr(0, text.find('|'));
    const std::string id = text.substr(text.find('|') + 1);
    const fs::path root = fs::temp_directory_path() / "aige_scene_copy";
    fs::remove_all(root);
    fs::copy(from, root, fs::copy_options::recursive);
    PrefabLibrary prefabs(Project{root});
    AssetDatabase assets(Project{root});
    assets.Refresh();
    const TerrainSet terrains = ProjectTerrainSet(prefabs, Project{root});
    CatalogOptions catalogOptions;
    const Catalog catalog = BuildCatalog(assets, &Library(), catalogOptions);
    const json spec = json::parse(std::ifstream(root / "places" / id / "spec.json"));
    const json solved = json::parse(std::ifstream(root / "places" / id / "layout.solved.json"));
    PlaceSceneOptions options;
    options.place = id;
    options.isoGrid = true;
    const json plan = PlanPlaceAssets(Library(), spec, solved, catalog, prefabs, assets, terrains, options);
    Registry registry;
    json report;
    auto command = BuildPlaceScene(registry, prefabs, solved, plan, terrains, options, &report);
    const json checks = VerifyPlaceScene(registry, solved, plan, options);
    std::cout << "  report " << report.dump() << "\n";
    for (const auto& c : checks) std::cout << "  " << (c["ok"].get<bool>() ? "ok   " : "FAIL ") << c["id"] << " " << c["detail"] << "\n";
    int placeholders = 0;
    for (const auto& n : plan["needs"]) placeholders += n["source"] == "placeholder";
    std::cout << "  needs " << plan["needs"].size() << ", placeholders " << placeholders << "\n";
    const int side = solved["grid"]["bounds"][2].get<int>();
    if (const char* png = std::getenv("AIGE_SCENE_PNG")) {
        const auto image = RenderScenePreview(registry, Project{root}, options.iso, 0, 0, side, side, 1800);
        WritePng(png, image);
        std::cout << "  preview " << image.width << "x" << image.height << "\n";
    }
}
