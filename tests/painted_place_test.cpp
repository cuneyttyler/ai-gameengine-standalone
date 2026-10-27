#include "assets/TextureCook.h"
#include "editing/PaintedPlace.h"
#include "gameplay/Components.h"
#include "places/Ontology.h"
#include "renderer2d/Components.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace aige;
using json = nlohmann::json;
namespace fs = std::filesystem;

// docs/places/painted-places.md: the block-out painted, the marks pre-filled from the plan, the scene from them.

namespace {

const Ontology& Library() {
    static Ontology ontology = [] {
        Ontology o;
        o.Load({EnginePlacesDir()});
        return o;
    }();
    return ontology;
}

// A 10 x 10 site of grass with water along its east edge: a house (2 x 2), a tree, a chest, a fence along a row.
json Plan() {
    json rows = json::array();
    for (int y = 0; y < 10; ++y) rows.push_back(json::array({json::array({9, 0}), json::array({1, 1})}));
    return {{"grid", {{"bounds", {0, 0, 10, 10}}}},
            {"terrain", {{"palette", {"grass", "water"}}, {"rows", rows}}},
            {"objects",
             {{{"id", "house1"}, {"slot", "house"}, {"layer", "building"}, {"x", 2}, {"y", 2}, {"w", 2}, {"h", 2}},
              {{"id", "tree1"}, {"slot", "tree"}, {"layer", "plant"}, {"x", 6}, {"y", 6}, {"w", 1}, {"h", 1}},
              {{"id", "chest1"}, {"slot", "chest"}, {"layer", "prop"}, {"x", 6}, {"y", 2}, {"w", 1}, {"h", 1}}}},
            {"lines", {{{"id", "f1"}, {"slot", "fence"}, {"edges", {{1, 8, "n"}, {2, 8, "n"}, {3, 8, "n"}}}}}}};
}

fs::path Fresh(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / "aige_painted" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

} // namespace

TEST(PaintedPlaceTest, ViewMapsCellsToPixelsAndBack) {
    const PaintView v = PaintView::From({{"x0", 0}, {"y0", 0}, {"x1", 10}, {"y1", 10}, {"tw", 50.0}, {"th", 25.0}, {"top", 75.0}});
    for (const auto& [x, y] : {std::pair{0.0, 0.0}, std::pair{3.5, 7.25}, std::pair{10.0, 2.0}}) {
        const auto p = v.Pixel(x, y);
        const auto c = v.Cell(p.first, p.second);
        EXPECT_NEAR(c.first, x, 1e-9);
        EXPECT_NEAR(c.second, y, 1e-9);
    }
    // The site's top corner is at the middle of the picture's width, `top` down.
    EXPECT_NEAR(v.Pixel(0, 0).first, 250.0, 1e-9);
    EXPECT_NEAR(v.Pixel(0, 0).second, 75.0, 1e-9);
}

TEST(PaintedPlaceTest, OnlyEnvironmentalThingsArePainted) {
    const json plan = Plan();
    EXPECT_TRUE(PaintedObject(&Library(), plan["objects"][0]));  // a building
    EXPECT_TRUE(PaintedObject(&Library(), plan["objects"][1]));  // a tree
    EXPECT_FALSE(PaintedObject(&Library(), plan["objects"][2])); // a chest: the player opens it
    EXPECT_EQ(PaintedObjectIds(&Library(), plan), (std::set<std::string>{"house1", "tree1"}));
}

TEST(PaintedPlaceTest, MarksArePrefilledFromThePlan) {
    const fs::path dir = Fresh("prefill");
    const json plan = Plan();
    const json painting = PrepareBlockout(plan, &Library(), dir, 800);
    ASSERT_TRUE(fs::exists(dir / "blockout.png"));
    ASSERT_TRUE(fs::exists(dir / "blockout-ids.png"));
    EXPECT_EQ(painting["objects"], json({"house1", "tree1"})); // the chest isn't in the painting
    // The provider's painting stands in: the block-out itself.
    fs::copy_file(dir / "blockout.png", dir / "painting.png");
    PaintingMade(dir, {{"provider", "test"}, {"seconds", 1.0}});

    const json marks = PrefillMarks(plan, &Library(), dir);
    ASSERT_TRUE(marks.is_object());
    const json rows = marks["walk"]["rows"];
    ASSERT_EQ(rows.size(), 10u);
    EXPECT_EQ(rows[0].get<std::string>()[0], '.');  // grass
    EXPECT_EQ(rows[0].get<std::string>()[9], '#');  // water
    EXPECT_EQ(rows[2].get<std::string>()[2], '#');  // the house
    EXPECT_EQ(rows[3].get<std::string>()[3], '#');
    EXPECT_EQ(rows[6].get<std::string>()[6], '#');  // the tree's trunk
    EXPECT_EQ(rows[2].get<std::string>()[6], '.');  // the chest is a sprite: its own collision
    EXPECT_EQ(rows[8].get<std::string>()[2], '#');  // the fence

    std::map<std::string, json> byKind;
    for (const auto& o : marks["occluders"]) byKind[o.value("kind", "")] = o;
    ASSERT_TRUE(byKind.contains("house"));
    ASSERT_TRUE(byKind.contains("tree"));
    ASSERT_TRUE(byKind.contains("fence"));
    // The house's base: its footprint's two front edges, a "V" (left, bottom, right).
    const json base = byKind["house"]["base"];
    ASSERT_EQ(base.size(), 3u);
    EXPECT_LT(base[0][0].get<double>(), base[1][0].get<double>());
    EXPECT_GT(base[1][1].get<double>(), base[0][1].get<double>());
    EXPECT_TRUE(fs::exists(dir / byKind["house"]["mask"].get<std::string>()));

    const json summary = MarksSummary(dir);
    EXPECT_TRUE(summary["painted"].get<bool>());
    EXPECT_FALSE(summary["stale"].get<bool>());
    EXPECT_EQ(summary["occluders"].get<size_t>(), marks["occluders"].size());
}

TEST(PaintedPlaceTest, EditedOccludersSurviveAPrefill) {
    const fs::path dir = Fresh("keep");
    const json plan = Plan();
    PrepareBlockout(plan, &Library(), dir, 800);
    fs::copy_file(dir / "blockout.png", dir / "painting.png");
    PaintingMade(dir, {{"provider", "test"}});
    json marks = PrefillMarks(plan, &Library(), dir);
    for (auto& o : marks["occluders"]) {
        if (o.value("kind", "") == "house") o["edited"] = true, o["name"] = "my house";
    }
    marks["occluders"].push_back({{"id", "u1"}, {"name", "by hand"}, {"box", {0, 0, 4, 4}}, {"base", {{0, 4}, {4, 4}}}, {"prefilled", false}});
    ASSERT_EQ(SaveMarks(dir, marks), "");
    const json again = PrefillMarks(plan, &Library(), dir);
    std::set<std::string> names;
    for (const auto& o : again["occluders"]) names.insert(o.value("name", ""));
    EXPECT_TRUE(names.contains("my house"));
    EXPECT_TRUE(names.contains("by hand"));
    EXPECT_TRUE(names.contains("tree 1"));
}

TEST(PaintedPlaceTest, OccludersAreStripsAtTheirBaseLinesDepth) {
    const fs::path dir = Fresh("scene");
    const json plan = Plan();
    PrepareBlockout(plan, &Library(), dir, 800);
    fs::copy_file(dir / "blockout.png", dir / "painting.png");
    PaintingMade(dir, {{"provider", "test"}});
    PrefillMarks(plan, &Library(), dir);

    Registry registry;
    IsoParams iso;
    const json made = AddPaintingEntities(registry, dir, "places/test", "test", 10, 20, iso);
    ASSERT_FALSE(made.contains("error")) << made.dump();
    EXPECT_TRUE(made["backdrop"].get<bool>());
    EXPECT_GE(made["occluders"].get<int>(), 2);
    EXPECT_GT(made["strips"].get<int>(), made["occluders"].get<int>()); // a house is wider than one strip

    float minHouse = 1e9f, maxHouse = -1e9f;
    bool backdrop = false;
    for (auto [e, node, screen] : registry.view<PlaceNode, ScreenTransform2D>().each()) {
        if (node.statement == "painting#backdrop") {
            backdrop = true;
            EXPECT_LT(screen.depth, -1e6f);
            continue;
        }
        const auto& sprite = registry.get<Sprite>(e);
        EXPECT_LT(sprite.u0, sprite.u1);
        EXPECT_LE(sprite.v1, 1.0f);
        const json marks = LoadMarks(dir);
        for (const auto& o : marks["occluders"]) {
            if (o.value("kind", "") == "house" && node.statement.rfind("painting#" + o.value("id", "") + "/", 0) == 0) {
                minHouse = std::min(minHouse, screen.depth), maxHouse = std::max(maxHouse, screen.depth);
            }
        }
    }
    EXPECT_TRUE(backdrop);
    // The house covers cells 2..4 both ways, the site's corner at (10, 20): its front edges run from (2, 4) through
    // (4, 4) to (4, 2), x + y 6 to 8, 36 to 38 in the world. A character is drawn at its x + y + 0.5: behind the house
    // it's under a strip, on its front edge over it.
    EXPECT_GE(minHouse, 36.0f - 0.1f);
    EXPECT_LE(maxHouse, 38.0f + 0.6f);
}

TEST(PaintedPlaceTest, MarkedCellsAndBase64) {
    const json marks = {{"walk", {{"rows", {"..#", "#.."}}}}};
    const auto blocked = MarkedBlocked(marks, 3, 2);
    EXPECT_TRUE(blocked[2][0]);
    EXPECT_TRUE(blocked[0][1]);
    EXPECT_FALSE(blocked[1][1]);
    const auto bytes = DecodeBase64("aGVsbG8=");
    EXPECT_EQ(std::string(bytes.begin(), bytes.end()), "hello");
}
