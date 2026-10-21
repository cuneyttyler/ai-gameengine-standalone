#include <gtest/gtest.h>

#include "ai_bridge/Proposal.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "editing/Commands.h"
#include "editing/LevelBuild.h"
#include "gameplay/Archetypes.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/GameSession.h"
#include "gameplay/LevelGen.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"
#include "scripting/LuaVM.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

#ifndef AIGE_SOURCE_DIR
#define AIGE_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;

namespace {

aige::LevelParams Params(uint32_t seed) {
    aige::LevelParams p;
    p.x0 = 40;
    p.y0 = 0;
    p.x1 = 89;
    p.y1 = 39;
    p.seed = seed;
    p.rooms = 8;
    p.roomMin = 4;
    p.roomMax = 9;
    p.enemies = 10;
    p.pickups = 3;
    return p;
}

std::map<aige::Cell, int> Distances(const aige::LevelLayout& layout) {
    std::map<aige::Cell, int> d;
    for (const auto& [cell, steps] : aige::FloorDistances(layout.floor, layout.playerStart)) d[cell] = steps;
    return d;
}

} // namespace

// development_plan.md 3.4. Many seeds, the properties every level must have.
TEST(LevelGenTest, EveryLevelIsConnectedWalledInAndInsideItsArea) {
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        const aige::LevelParams p = Params(seed);
        std::string error;
        auto layout = aige::GenerateLevel(p, &error);
        ASSERT_TRUE(layout) << "seed " << seed << ": " << error;
        EXPECT_GE(layout->rooms.size(), 4u) << "seed " << seed;

        // Navigable: every floor cell — so every room, enemy and pickup — can
        // be walked to from the start.
        auto reach = Distances(*layout);
        EXPECT_EQ(reach.size(), layout->floor.size()) << "seed " << seed;
        for (const auto& c : layout->enemies) EXPECT_TRUE(reach.contains(c)) << "seed " << seed;
        for (const auto& c : layout->pickups) EXPECT_TRUE(reach.contains(c)) << "seed " << seed;

        // Walled in: no floor cell touches the outside; walls stay in the area.
        for (const auto& [gx, gy] : layout->floor) {
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy) {
                    aige::Cell n{gx + dx, gy + dy};
                    EXPECT_TRUE(layout->floor.contains(n) || layout->walls.contains(n)) << "seed " << seed;
                }
        }
        for (const auto& [gx, gy] : layout->walls) {
            EXPECT_TRUE(gx >= p.x0 && gx <= p.x1 && gy >= p.y0 && gy <= p.y1) << "seed " << seed;
        }
        for (const auto& room : layout->rooms) {
            EXPECT_GE(room.x1 - room.x0 + 1, p.roomMin);
            EXPECT_LE(room.y1 - room.y0 + 1, p.roomMax);
        }

        // Counts, and nothing on top of anything else.
        EXPECT_EQ(layout->enemies.size(), static_cast<size_t>(p.enemies)) << "seed " << seed;
        EXPECT_EQ(layout->pickups.size(), static_cast<size_t>(p.pickups)) << "seed " << seed;
        std::vector<aige::Cell> all = layout->enemies;
        all.insert(all.end(), layout->pickups.begin(), layout->pickups.end());
        all.push_back(layout->playerStart);
        std::sort(all.begin(), all.end());
        EXPECT_EQ(std::adjacent_find(all.begin(), all.end()), all.end()) << "seed " << seed;
    }
}

TEST(LevelGenTest, EnemiesStayAwayFromTheStartAndThickenFurtherIn) {
    int farHeavier = 0;
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        auto layout = *aige::GenerateLevel(Params(seed));
        const auto& start = layout.rooms[layout.startRoom];
        for (const auto& c : layout.enemies) {
            EXPECT_FALSE(start.Contains(c)) << "seed " << seed;
            EXPECT_GT(std::abs(c.first - layout.playerStart.first) + std::abs(c.second - layout.playerStart.second), 3);
        }
        // Enemies per room: the farthest room gets at least as many as the nearest non-start room.
        std::map<size_t, int> perRoom;
        for (const auto& c : layout.enemies)
            for (size_t i = 0; i < layout.rooms.size(); ++i)
                if (layout.rooms[i].Contains(c)) perRoom[i]++;
        size_t nearest = layout.farthestRoom;
        for (size_t i = 0; i < layout.rooms.size(); ++i)
            if (i != layout.startRoom && layout.roomDistance[i] < layout.roomDistance[nearest]) nearest = i;
        farHeavier += perRoom[layout.farthestRoom] >= perRoom[nearest] ? 1 : 0;
        EXPECT_EQ(layout.pathLength, layout.roomDistance[layout.farthestRoom]);
    }
    EXPECT_GE(farHeavier, 36); // a curve, not a coin flip (room size can still win now and then)
}

TEST(LevelGenTest, TheSameSeedGivesTheSameLevel) {
    auto a = *aige::GenerateLevel(Params(7));
    auto b = *aige::GenerateLevel(Params(7));
    auto c = *aige::GenerateLevel(Params(8));
    EXPECT_EQ(a.Ascii(Params(7)), b.Ascii(Params(7)));
    EXPECT_NE(a.Ascii(Params(7)), c.Ascii(Params(8)));
    // Fixed down to the map, so a seed in a proposal means one level everywhere.
    EXPECT_EQ(a.floor, b.floor);
    EXPECT_EQ(a.enemies, b.enemies);
}

TEST(LevelGenTest, ImpossibleParametersAreExplained) {
    aige::LevelParams p = Params(1);
    std::string error;
    p.x1 = p.x0;
    EXPECT_FALSE(aige::GenerateLevel(p, &error));
    EXPECT_NE(error.find("x1 > x0"), std::string::npos);
    p = Params(1);
    p.x1 = p.x0 + 5; // smaller than one room plus walls
    p.roomMin = 8;
    p.roomMax = 9;
    EXPECT_FALSE(aige::GenerateLevel(p, &error));
    p = Params(1);
    p.x1 = p.x0 + 10;
    p.y1 = p.y0 + 10;
    p.roomMin = p.roomMax = 6; // only one fits
    EXPECT_FALSE(aige::GenerateLevel(p, &error));
    EXPECT_NE(error.find("fewer than two rooms"), std::string::npos) << error;
}

TEST(LevelGenTest, TheMapShowsWhatWillBeBuilt) {
    auto params = Params(3);
    auto layout = *aige::GenerateLevel(params);
    std::string map = layout.Ascii(params);
    EXPECT_EQ(std::count(map.begin(), map.end(), '@'), 1);
    EXPECT_EQ(std::count(map.begin(), map.end(), 'e'), params.enemies);
    EXPECT_EQ(std::count(map.begin(), map.end(), '+'), params.pickups);
    EXPECT_EQ(std::count(map.begin(), map.end(), '\n'), params.y1 - params.y0 + 1);
    std::string text = aige::DescribeLevel(params, layout);
    EXPECT_NE(text.find(std::to_string(layout.rooms.size()) + " rooms"), std::string::npos);
}

// The golden recording's level.generate (ai_service/tests/golden/dungeon.json):
// what the model actually proposed must really build.
TEST(LevelBuildTest, TheRecordedAiProposalGenerates) {
    std::ifstream in(fs::path(AIGE_SOURCE_DIR) / "ai_service" / "tests" / "golden" / "dungeon.json");
    nlohmann::json record = nlohmann::json::parse(in);
    nlohmann::json answer = nlohmann::json::parse(record["responses"].back().get<std::string>());
    bool found = false;
    for (const auto& op : answer["operations"]) {
        if (op.value("name", "") != "level.generate") continue;
        found = true;
        std::string error;
        auto layout = aige::GenerateLevel(aige::LevelParamsFromJson(op["args"]), &error);
        ASSERT_TRUE(layout) << error;
        EXPECT_GE(layout->rooms.size(), 2u);
        EXPECT_EQ(aige::FloorDistances(layout->floor, layout->playerStart).size(), layout->floor.size());
    }
    EXPECT_TRUE(found);
}
