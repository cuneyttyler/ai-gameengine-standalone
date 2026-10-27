#include "places/Arrange.h"
#include "places/Ontology.h"

#include <gtest/gtest.h>

// docs/places/ontology-in-use.md: things turned by their relations.
using namespace aige;
using json = nlohmann::json;

namespace {

const Ontology& Library() {
    static Ontology o = [] {
        Ontology ontology;
        ontology.Load({EnginePlacesDir()});
        return ontology;
    }();
    return o;
}

// A 12 x 12 lawn with roads two cells wide: along x on rows 5-6, along y on columns 5-6, or both (a crossing).
enum class Roads { AlongX, AlongY, Crossing };

json Solved(Roads way, const json& objects) {
    json rows = json::array(), roads = json::array();
    for (int y = 0; y < 12; ++y) {
        json row = json::array();
        for (int x = 0; x < 12; ++x) {
            const bool onX = (y == 5 || y == 6) && way != Roads::AlongY, onY = (x == 5 || x == 6) && way != Roads::AlongX;
            row.push_back({1, onX || onY ? 1 : 0});
            if (onX || onY) roads.push_back({x, y, 2});
        }
        rows.push_back(row);
    }
    return {{"grid", {{"bounds", {0, 0, 12, 12}}}},
            {"terrain", {{"palette", {"lawn", "asphalt"}}, {"rows", rows}}},
            {"roads", roads},
            {"objects", objects}};
}

json Car(int x, int y) {
    return {{"statement", "traffic"}, {"slot", "car"}, {"layer", "prop"}, {"x", x}, {"y", y}, {"w", 1}, {"h", 1}, {"facing", "s"}};
}

json Statements() {
    const json relations = Library().RoleLook("car", "relations");
    return {{"statements", json::array({{{"id", "traffic"}, {"relations", relations}}})}};
}

} // namespace

TEST(ArrangeTest, TheOntologyAlignsVehiclesWithRoads) {
    const json relations = Library().RoleLook("car", "relations");
    ASSERT_TRUE(relations.is_array());
    bool aligned = false;
    for (const auto& r : relations) aligned |= r.value("type", "") == "aligned" && r.value("to", "") == "road-terrain";
    EXPECT_TRUE(aligned);
    EXPECT_EQ(Library().RoleLook("road-terrain", "found"), "roads");
}

TEST(ArrangeTest, CarsDriveAlongTheirRoadOnTheRight) {
    // Along x: the south lane drives east, the north lane west; a car parked beside the road too.
    json solved = Solved(Roads::AlongX, json::array({Car(3, 6), Car(8, 5), Car(2, 8)}));
    EXPECT_EQ(FaceObjects(Library(), solved, Statements()), 3);
    EXPECT_EQ(solved["objects"][0]["facing"], "e");
    EXPECT_EQ(solved["objects"][1]["facing"], "w");
    EXPECT_EQ(solved["objects"][2]["facing"], "e");
    EXPECT_EQ(solved["objects"][0]["faced"], "aligned");
    // Along y: the east lane drives north, the west lane south.
    solved = Solved(Roads::AlongY, json::array({Car(6, 3), Car(5, 8)}));
    FaceObjects(Library(), solved, Statements());
    EXPECT_EQ(solved["objects"][0]["facing"], "n");
    EXPECT_EQ(solved["objects"][1]["facing"], "s");
}

TEST(ArrangeTest, ACarFarFromAnyRoadKeepsItsFacing) {
    json solved = Solved(Roads::AlongX, json::array({Car(2, 0)}));
    FaceObjects(Library(), solved, Statements());
    EXPECT_EQ(solved["objects"][0]["facing"], "s");
    EXPECT_FALSE(solved["objects"][0].contains("faced"));
}

TEST(ArrangeTest, ACarAtTheKerbNearACrossingFollowsTheRoadItIsBeside) {
    // North of the road along x, two cells from the crossing road: beside the first, so along x, in its west-bound lane.
    json solved = Solved(Roads::Crossing, json::array({Car(3, 4), Car(8, 7)}));
    FaceObjects(Library(), solved, Statements());
    EXPECT_EQ(solved["objects"][0]["facing"], "w");
    EXPECT_EQ(solved["objects"][1]["facing"], "e");
}
