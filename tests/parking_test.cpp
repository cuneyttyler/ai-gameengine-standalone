#include "places/Ontology.h"
#include "places/Solver.h"

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <vector>

// docs/places/ontology-in-use.md, Layout rules: a parking lot's cars in bays, every one able to drive out.
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

// One parking lot (w x h) full of cars, alone on its site.
json Lot(int w, int h, double density = 1.0) {
    const json ir = {{"schema", "aige.ir/1"}, {"seed", 5}, {"grid", {{"bounds", {0, 0, w + 4, h + 4}}}},
                     {"statements", json::array({
                         {{"op", "region"}, {"id", "site"}, {"node", "s"}, {"at", {0, 0}}, {"size", {{"w", w + 4}, {"h", h + 4}}}},
                         {{"op", "region"}, {"id", "lot"}, {"node", "lot"}, {"in", "site"}, {"at", {2, 2}}, {"size", {{"w", w}, {"h", h}}},
                          {"module", "parking-lot"}},
                         {{"op", "scatter"}, {"id", "lot/cars"}, {"node", "lot"}, {"in", "lot"}, {"slot", "car"}, {"density", density},
                          {"layer", "prop"}, {"spacing", {{"min", 1}}}, {"ordered", true}},
                     })}};
    return SolveLayout(Library(), ir).document;
}

struct Parked {
    std::map<std::pair<int, int>, char> cars; // cell -> facing
    std::set<std::pair<int, int>> lot;        // the lot's cells
};

Parked Read(const json& doc, int w, int h) {
    Parked p;
    for (int y = 2; y < 2 + h; ++y) {
        for (int x = 2; x < 2 + w; ++x) p.lot.insert({x, y});
    }
    for (const auto& o : doc["objects"]) {
        if (o.value("slot", "") == "car") p.cars[{o.value("x", 0), o.value("y", 0)}] = o.value("facing", "s")[0];
    }
    return p;
}

std::pair<int, int> Step(std::pair<int, int> c, char facing, int k = 1) {
    const int dx = facing == 'e' ? 1 : facing == 'w' ? -1 : 0, dy = facing == 's' ? 1 : facing == 'n' ? -1 : 0;
    return {c.first + dx * k, c.second + dy * k};
}

} // namespace

TEST(ParkingTest, CarsStandInBaysFacingAnOpenAisle) {
    for (const auto& [w, h] : {std::pair{16, 10}, std::pair{10, 7}, std::pair{18, 9}, std::pair{9, 9}}) {
        const Parked p = Read(Lot(w, h), w, h);
        ASSERT_GT(p.cars.size(), 3u) << w << "x" << h;
        for (const auto& [cell, facing] : p.cars) {
            // In front of it, two cells of open aisle inside the lot: room to drive out.
            for (int k = 1; k <= 2; ++k) {
                const auto ahead = Step(cell, facing, k);
                EXPECT_TRUE(p.lot.contains(ahead)) << w << "x" << h << " car at " << cell.first << "," << cell.second << " faces out of the lot";
                EXPECT_FALSE(p.cars.contains(ahead)) << w << "x" << h << " car at " << cell.first << "," << cell.second << " faces another";
            }
            // Behind it, its tail: no other car (a car is longer than a cell).
            EXPECT_FALSE(p.cars.contains(Step(cell, facing, -1))) << w << "x" << h << " car at " << cell.first << "," << cell.second << " has one on its tail";
        }
    }
}

TEST(ParkingTest, EveryCarCanReachTheLotsEdge) {
    const int w = 16, h = 10;
    const Parked p = Read(Lot(w, h), w, h);
    // From each car's aisle cell, over cells with no car, to the lot's border.
    for (const auto& [cell, facing] : p.cars) {
        std::vector<std::pair<int, int>> todo = {Step(cell, facing)};
        std::set<std::pair<int, int>> seen(todo.begin(), todo.end());
        bool out = false;
        while (!todo.empty() && !out) {
            const auto c = todo.back();
            todo.pop_back();
            out = c.first == 2 || c.second == 2 || c.first == 1 + w || c.second == 1 + h;
            for (const auto& n : {std::pair{c.first + 1, c.second}, std::pair{c.first - 1, c.second}, std::pair{c.first, c.second + 1},
                                  std::pair{c.first, c.second - 1}}) {
                if (p.lot.contains(n) && !p.cars.contains(n) && seen.insert(n).second) todo.push_back(n);
            }
        }
        EXPECT_TRUE(out) << "car at " << cell.first << "," << cell.second << " is boxed in";
    }
}

TEST(ParkingTest, CarsInARowStandSideBySide) {
    // Full: whole rows of cars, next to each other along the row, all facing the same way.
    const int w = 16, h = 10;
    const Parked p = Read(Lot(w, h), w, h);
    int neighbours = 0;
    for (const auto& [cell, facing] : p.cars) {
        const auto side = (facing == 'n' || facing == 's') ? std::pair{cell.first + 1, cell.second} : std::pair{cell.first, cell.second + 1};
        if (const auto it = p.cars.find(side); it != p.cars.end()) {
            ++neighbours;
            EXPECT_EQ(it->second, facing);
        }
    }
    EXPECT_GT(neighbours, static_cast<int>(p.cars.size()) / 2);
}
