#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aige {

// Dungeon layout generation for the action-RPG template (development_plan.md
// 3.4): rooms joined by corridors inside a rectangle of grid cells, walled in,
// with a player start, enemies and pickups. Pure and deterministic — the same
// parameters and seed always give the same layout — so it can be previewed
// in review, tested, and re-run. Putting it into a scene is
// editing/LevelBuild (command level.generate).

struct LevelParams {
    int x0 = 0, y0 = 0, x1 = 39, y1 = 29; // the area, inclusive; walls stay inside it
    uint32_t seed = 1;
    int rooms = 6;          // how many to try to place (fewer if they don't fit)
    int roomMin = 4;        // room side length, floor cells
    int roomMax = 8;
    int corridorWidth = 1;  // 1-3
    float loops = 0.15f;    // 0..1: extra corridors beyond a tree, so it isn't all dead ends
    int enemies = 6;
    int pickups = 2;
};

using Cell = std::pair<int, int>; // gx, gy

struct LevelRoom {
    int x0, y0, x1, y1; // floor, inclusive
    Cell Center() const { return {(x0 + x1) / 2, (y0 + y1) / 2}; }
    bool Contains(Cell c) const { return c.first >= x0 && c.first <= x1 && c.second >= y0 && c.second <= y1; }
};

struct LevelLayout {
    std::vector<LevelRoom> rooms;
    std::set<Cell> floor;
    std::set<Cell> walls;
    Cell playerStart{};
    size_t startRoom = 0;
    size_t farthestRoom = 0;  // the "boss room": the longest walk from the start
    int pathLength = 0;       // steps from the start to the farthest room's center
    std::vector<int> roomDistance; // steps from the start to each room's center
    std::vector<Cell> enemies;
    std::vector<Cell> pickups;
    std::vector<std::pair<size_t, size_t>> corridors; // rooms they join

    // A character map for review: '#' wall, '.' floor, '@' start, 'e' enemy,
    // '+' pickup, ' ' untouched; one line per row, north at the top.
    std::string Ascii(const LevelParams& params) const;
};

// Why the parameters can't work, or nullopt.
std::optional<std::string> CheckLevelParams(const LevelParams& params);

// The layout, or nullopt (and `error`) when not even two rooms fit.
std::optional<LevelLayout> GenerateLevel(const LevelParams& params, std::string* error = nullptr);

// Steps from `from` to every reachable floor cell (4-neighbour walking).
std::vector<std::pair<Cell, int>> FloorDistances(const std::set<Cell>& floor, Cell from);

} // namespace aige
