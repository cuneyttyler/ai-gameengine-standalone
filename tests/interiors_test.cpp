#include "assets/Catalog.h"
#include "assets/UserLibrary.h"
#include "editing/PaintedPlace.h"
#include "gameplay/Components.h"
#include "places/Ontology.h"
#include "gameplay/Interiors.h"
#include "gameplay/TileGrid.h"
#include "gameplay/TileLayer.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace aige;
using json = nlohmann::json;
namespace fs = std::filesystem;

// docs/places/interiors.md: a building's interior shown while the player is inside it, walked into through its door;
// painted buildings the same over the painting; a pair kept in the library stays a pair.

namespace {

void WriteFile(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

} // namespace

TEST(InteriorsTest, WallsLeaveTheDoorwayOpen) {
    // A 3 x 3 footprint whose cell (its front corner) is 5,5: its edge blocks but the middle of its south side.
    std::set<std::pair<int, int>> walls;
    ForEachInteriorWall(5, 5, 2, 0, 2, "s", [&](int x, int y) { walls.insert({x, y}); });
    EXPECT_EQ(walls.size(), 7u);
    EXPECT_FALSE(walls.contains({4, 5})); // the doorway
    EXPECT_FALSE(walls.contains({4, 4})); // inside
    EXPECT_TRUE(walls.contains({3, 3}));
    walls.clear();
    ForEachInteriorWall(5, 5, 2, 0, 2, "e", [&](int x, int y) { walls.insert({x, y}); });
    EXPECT_FALSE(walls.contains({5, 4}));
    EXPECT_TRUE(walls.contains({4, 5}));
}

TEST(InteriorsTest, ShownWhileThePlayerIsInside) {
    Registry registry;
    const Entity house = registry.create();
    registry.emplace<WorldTransform2D>(house, 5.0f, 5.0f);
    registry.emplace<Obstacle>(house, Obstacle{2, 0, 2});
    Sprite sprite;
    sprite.texture = "assets/house.png";
    registry.emplace<Sprite>(house, sprite);
    registry.emplace<Interior>(house).texture = "assets/house-interior.png";
    const Entity player = registry.create();
    auto& at = registry.emplace<WorldTransform2D>(player, 9.0f, 9.0f);

    UpdateInteriors(registry, player);
    EXPECT_FALSE(registry.get<Interior>(house).shown);
    EXPECT_EQ(registry.get<Sprite>(house).texture, "assets/house.png");

    at.x = 4.5f, at.y = 4.5f; // in through the door
    UpdateInteriors(registry, player);
    EXPECT_TRUE(registry.get<Interior>(house).shown);
    EXPECT_EQ(registry.get<Sprite>(house).texture, "assets/house-interior.png");

    at.x = 4.5f, at.y = 6.5f; // out again
    UpdateInteriors(registry, player);
    EXPECT_EQ(registry.get<Sprite>(house).texture, "assets/house.png");

    // The editor's preview: shown by hand.
    registry.get<Interior>(house).shown = true;
    ApplyInteriors(registry);
    EXPECT_EQ(registry.get<Sprite>(house).texture, "assets/house-interior.png");
}

TEST(InteriorsTest, ItsStairsTakeOneUpAndDownTheFloors) {
    Registry registry;
    const Entity house = registry.create();
    registry.emplace<WorldTransform2D>(house, 5.0f, 5.0f);
    registry.emplace<Obstacle>(house, Obstacle{2, 0, 2}); // cells 3..5 x 3..5; its back corner 3,3 the stairs
    registry.emplace<Sprite>(house).texture = "house.png";
    auto& interior = registry.emplace<Interior>(house);
    interior.texture = "house-interior.png";
    interior.floors = "house-interior-2.png;house-interior-3.png";
    EXPECT_EQ(InteriorFloors(interior), 3);
    EXPECT_EQ(InteriorPicture(interior, 3), "house-interior-3.png");
    const Entity player = registry.create();
    auto& at = registry.emplace<WorldTransform2D>(player, 4.5f, 4.5f);
    UpdateInteriors(registry, player);
    EXPECT_EQ(registry.get<Interior>(house).floor, 1);
    EXPECT_EQ(registry.get<Sprite>(house).texture, "house-interior.png");
    auto step = [&](float x, float y) {
        at.x = x, at.y = y;
        UpdateInteriors(registry, player);
    };
    step(3.5f, 3.5f); // on the stairs: up
    EXPECT_EQ(registry.get<Interior>(house).floor, 2);
    EXPECT_EQ(registry.get<Sprite>(house).texture, "house-interior-2.png");
    step(3.5f, 3.5f); // still on them: no more
    EXPECT_EQ(registry.get<Interior>(house).floor, 2);
    step(4.5f, 4.5f), step(5.5f, 3.5f); // floor 2's stairs up stand in the right corner: up to the top
    EXPECT_EQ(registry.get<Interior>(house).floor, 3);
    step(4.5f, 4.5f), step(5.5f, 3.5f); // from the top, the same stairs: down
    EXPECT_EQ(registry.get<Interior>(house).floor, 2);
    step(4.5f, 4.5f), step(3.5f, 3.5f); // the first stairs again: down to the ground floor
    EXPECT_EQ(registry.get<Interior>(house).floor, 1);
    step(4.5f, 4.5f), step(3.5f, 3.5f); // and up
    EXPECT_EQ(registry.get<Interior>(house).floor, 2);
    step(4.5f, 9.5f); // out: the exterior, and the ground floor next time
    EXPECT_EQ(registry.get<Sprite>(house).texture, "house.png");
    EXPECT_EQ(registry.get<Interior>(house).floor, 1);
}

TEST(InteriorsTest, PaintedBuildingHidesItsOccluders) {
    Registry registry;
    const Entity room = registry.create();
    Sprite cutaway;
    cutaway.texture = "places/v/marks/o1-interior.png";
    cutaway.a = 0.0f;
    registry.emplace<Sprite>(room, cutaway);
    Interior interior;
    interior.texture = cutaway.texture;
    interior.cells = "10,10,12,11";
    interior.hides = "painting#o1/";
    registry.emplace<Interior>(room, interior);
    const Entity strip = registry.create();
    registry.emplace<Sprite>(strip, Sprite{});
    registry.emplace<PlaceNode>(strip, "v", "", "painting#o1/0");
    const Entity other = registry.create();
    registry.emplace<Sprite>(other, Sprite{});
    registry.emplace<PlaceNode>(other, "v", "", "painting#o12/0");
    const Entity player = registry.create();
    auto& at = registry.emplace<WorldTransform2D>(player, 11.5f, 11.5f);

    UpdateInteriors(registry, player);
    EXPECT_FLOAT_EQ(registry.get<Sprite>(room).a, 1.0f);
    EXPECT_FLOAT_EQ(registry.get<Sprite>(strip).a, 0.0f);
    EXPECT_FLOAT_EQ(registry.get<Sprite>(other).a, 1.0f); // another building's

    at.x = 15.0f;
    UpdateInteriors(registry, player);
    EXPECT_FLOAT_EQ(registry.get<Sprite>(room).a, 0.0f);
    EXPECT_FLOAT_EQ(registry.get<Sprite>(strip).a, 1.0f);
}

TEST(InteriorsTest, PaintedWalkCellsBecomeWallsAndADoor) {
    // The marks block the whole house (cells 1..3 x 1..3); with an interior, only its walls do.
    const json marks = {{"walk", {{"rows", {".....", ".###.", ".###.", ".###.", "....."}}}},
                        {"occluders", {{{"id", "o1"}, {"interior", "marks/o1-interior.png"}, {"cells", {1, 1, 3, 3}}, {"door", "s"}}}}};
    const auto blocked = MarkedBlocked(marks, 5, 5);
    EXPECT_FALSE(blocked[2][2]); // inside
    EXPECT_FALSE(blocked[2][3]); // the doorway
    EXPECT_TRUE(blocked[1][3]);
    EXPECT_TRUE(blocked[2][1]);
    EXPECT_FALSE(blocked[0][0]);
}

TEST(InteriorsTest, PaintedInteriorsArePairedWithTheirBuilding) {
    const fs::path folder = fs::temp_directory_path() / "aige_interiors_paint";
    fs::remove_all(folder);
    fs::create_directories(folder / "marks");
    WriteFile(folder / "marks.json", json{{"occluders", {{{"id", "o1"}, {"object", "h1"}}, {{"id", "o2"}, {"object", "t1"}}}}}.dump());
    WriteFile(folder / "marks/o1-interior.png", "png");
    const json solved = {{"objects", {{{"id", "h1"}, {"layer", "building"}, {"x", 2}, {"y", 3}, {"w", 3}, {"h", 2}, {"facing", "e"}},
                                      {{"id", "t1"}, {"layer", "prop"}, {"x", 7}, {"y", 7}}}}};
    EXPECT_EQ(PairInteriorMarks(solved, folder), 1);
    // A building type chosen as a scene interior: its doorway only, no cutaway.
    EXPECT_EQ(SetInteriorChoice(folder, "house", "scene"), "");
    EXPECT_EQ(LoadInteriorChoices(folder).value("house", ""), "scene");
    const json marks = json::parse(ReadFile(folder / "marks.json"));
    EXPECT_EQ(marks["occluders"][0]["interior"], "marks/o1-interior.png");
    EXPECT_EQ(marks["occluders"][0]["cells"], json({2, 3, 4, 4}));
    EXPECT_EQ(marks["occluders"][0]["door"], "e");
    EXPECT_FALSE(marks["occluders"][1].contains("interior"));
}

TEST(InteriorsTest, ASceneInteriorIsSolidButItsDoorway) {
    Registry registry;
    TileLayers& layers = LayersOf(registry);
    TileLayer& ground = layers.Get("ground");
    const uint16_t grass = ground.Intern(TileDef{});
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) ground.Set(x, y, grass);
    }
    const Entity inn = registry.create();
    registry.emplace<WorldTransform2D>(inn, 5.0f, 5.0f);
    registry.emplace<Obstacle>(inn, Obstacle{2, 0, 2});
    // Linked: its door leads to the inn's scene, and one comes out a step in front of it.
    ASSERT_TRUE(LinkInteriorOutside(registry, inn, "scenes/interiors/village-inn.aige", "entrance", "from-village-inn"));
    const auto& interior = registry.get<Interior>(inn);
    EXPECT_EQ(interior.kind, "scene");
    int dx = 0, dy = 0;
    ASSERT_TRUE(InteriorDoor(registry, inn, interior, dx, dy));
    EXPECT_EQ(dx, 4);
    EXPECT_EQ(dy, 5);
    const TileGrid grid = TileGrid::Build(registry);
    EXPECT_FALSE(grid.IsSolid(4, 5)); // the doorway
    EXPECT_TRUE(grid.IsSolid(4, 4));  // inside: not walked (it's another scene)
    EXPECT_TRUE(grid.IsSolid(3, 5));
    bool back = false;
    for (auto [e, marker, world] : registry.view<Marker, WorldTransform2D>().each()) {
        back = back || (marker.name == "from-village-inn" && world.x == 4.5f && world.y == 6.5f);
    }
    EXPECT_TRUE(back);
    // Not shown in place: a scene interior has no cutaway.
    registry.emplace<Sprite>(inn, Sprite{});
    const Entity player = registry.create();
    registry.emplace<WorldTransform2D>(player, 4.5f, 4.5f);
    UpdateInteriors(registry, player);
    EXPECT_FALSE(registry.get<Interior>(inn).shown);
}

TEST(InteriorsTest, AnInteriorSceneLeadsBackOut) {
    Registry registry;
    TileLayers& layers = LayersOf(registry);
    TileLayer& floor = layers.Get("floor");
    TileDef wall;
    wall.solid = true;
    const uint16_t open = floor.Intern(TileDef{}), solid = floor.Intern(wall);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) floor.Set(x, y, x == 0 || x == 5 || y == 0 || (y == 5 && x != 2) ? solid : open);
    }
    ASSERT_TRUE(LinkInteriorInside(registry, 0, 0, 5, 5, "scenes/village.aige", "from-village-inn", "entrance"));
    bool gateway = false, entrance = false;
    for (auto [e, g, world] : registry.view<Gateway, WorldTransform2D>().each()) {
        gateway = g.scene == "scenes/village.aige" && g.marker == "from-village-inn" && world.x == 2.5f && world.y == 5.5f;
    }
    for (auto [e, m, world] : registry.view<Marker, WorldTransform2D>().each()) {
        entrance = entrance || (m.name == "entrance" && world.x == 2.5f && world.y < 5.0f);
    }
    EXPECT_TRUE(gateway);
    EXPECT_TRUE(entrance);
    // Linked again: still one way out.
    ASSERT_TRUE(LinkInteriorInside(registry, 0, 0, 5, 5, "scenes/village.aige", "from-village-inn", "entrance"));
    int ways = 0;
    for (auto e : registry.view<Gateway>()) ways += registry.valid(e);
    EXPECT_EQ(ways, 1);
}

TEST(InteriorsTest, EachBuildingTypeChoosesItsInterior) {
    Ontology ontology;
    ontology.Load({EnginePlacesDir()});
    const json none = json::object();
    EXPECT_EQ(InteriorKindOf(&ontology, none, "house"), "cutaway");
    EXPECT_EQ(InteriorKindOf(&ontology, none, "inn"), "scene");
    EXPECT_EQ(InteriorKindOf(&ontology, none, "tree"), ""); // nothing to choose
    EXPECT_EQ(InteriorKindOf(&ontology, json{{"inn", "cutaway"}}, "inn"), "cutaway");
    EXPECT_EQ(InteriorKindOf(&ontology, json{{"house", "none"}}, "house"), "none");
    const json solved = {{"objects", {{{"role", "house"}, {"layer", "building"}}, {{"role", "house"}, {"layer", "building"}},
                                      {{"role", "inn"}, {"layer", "building"}}, {{"role", "tree"}, {"layer", "prop"}}}}};
    const fs::path folder = fs::temp_directory_path() / "aige_interior_choices";
    fs::remove_all(folder);
    fs::create_directories(folder);
    const json table = InteriorChoices(solved, &ontology, folder);
    ASSERT_EQ(table.size(), 2u); // houses and the inn; trees have no inside
    for (const auto& row : table) {
        if (row["role"] == "house") EXPECT_EQ(row["count"], 2);
        if (row["role"] == "inn") EXPECT_EQ(row["kind"], "scene");
    }
}

TEST(InteriorsTest, AnInteriorIsNeverABuildsArt) {
    // Named "house interior", it read as a house: a build drew houses with it.
    AssetInfo inside;
    inside.path = "assets/matched/v/house-interior.png";
    inside.width = inside.height = 256;
    inside.analysis = {{"name", "house interior"}, {"kind", "building"}, {"layer", "building"}, {"exteriorOf", "assets/matched/v/house.png"}};
    EXPECT_TRUE(CatalogEntriesFor(inside, nullptr, CatalogOptions{}).empty());
    AssetInfo outside = inside;
    outside.path = "assets/matched/v/house.png";
    outside.analysis = {{"name", "house"}, {"kind", "building"}, {"layer", "building"}, {"roles", "house"}, {"interior", inside.path}};
    EXPECT_FALSE(CatalogEntriesFor(outside, nullptr, CatalogOptions{}).empty());
}

TEST(InteriorsTest, ALibraryPairStaysAPair) {
    const fs::path base = fs::temp_directory_path() / "aige_interiors_library";
    fs::remove_all(base);
    const fs::path project = base / "Village";
    WriteFile(project / "places/v/art/house.png", "png-house");
    WriteFile(project / "places/v/art/house.png.meta", "name=house\ninterior=places/v/art/house-interior.png\n");
    WriteFile(project / "places/v/art/house-interior.png", "png-house-inside");
    WriteFile(project / "places/v/art/house-interior.png.meta", "name=house interior\nexteriorOf=places/v/art/house.png\n");

    UserLibrary library(base / "library");
    const std::string house = library.Add(project / "places/v/art/house.png", json::object());
    ASSERT_FALSE(house.empty());
    const json state = library.State();
    ASSERT_EQ(state["items"].size(), 2u); // its interior came with it
    std::string inside;
    for (const auto& i : state["items"]) {
        if (i["id"] == house) inside = i.value("interior", "");
    }
    ASSERT_FALSE(inside.empty());
    for (const auto& i : state["items"]) {
        if (i["id"] == inside) EXPECT_EQ(i.value("exteriorOf", ""), house);
    }

    const fs::path other = base / "Other";
    const std::string path = library.CopyToProject(house, other);
    ASSERT_FALSE(path.empty());
    const std::string meta = ReadFile(other / (path + ".meta"));
    const size_t at = meta.find("interior=");
    ASSERT_NE(at, std::string::npos);
    const std::string insidePath = meta.substr(at + 9, meta.find('\n', at) - at - 9);
    EXPECT_TRUE(fs::exists(other / insidePath));
    EXPECT_NE(ReadFile(other / (insidePath + ".meta")).find("exteriorOf=" + path), std::string::npos);
}
