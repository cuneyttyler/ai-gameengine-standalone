#pragma once

#include "ecs/Registry.h"
#include "gameplay/LevelGen.h"
#include "reflection/Reflection.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace aige {

class PrefabLibrary;

// What to build a generated layout (gameplay/LevelGen) out of.
struct LevelPrefabs {
    std::string floor;  // a tile prefab
    std::string wall;   // a tile prefab (solid)
    std::string enemy;  // empty: none
    std::string pickup; // empty: none
    bool movePlayer = true;
};

struct LevelBuildResult {
    int floorTiles = 0;
    int wallTiles = 0;
    int enemies = 0;
    int pickups = 0;
    int cleared = 0; // entities removed from the area first
};

// Puts a layout into the scene: everything in the area (but the player) is
// removed, floor and walls are painted with the tile prefabs, enemies and
// pickups are placed, and the player is moved to the start. Returns why not
// (unknown or non-tile prefabs) before changing anything.
std::optional<std::string> BuildLevel(Registry& registry, PrefabLibrary& prefabs, const LevelParams& params,
                                      const LevelLayout& layout, const LevelPrefabs& use,
                                      LevelBuildResult* result = nullptr);

// level.generate's arguments (in its parameter order) as LevelParams, from the
// command's typed values or from a proposal's JSON args.
LevelParams LevelParamsFromArgs(const std::vector<FieldValue>& args);
LevelParams LevelParamsFromJson(const nlohmann::json& args);

// One paragraph and a map, for review panels: what level.generate will build.
std::string DescribeLevel(const LevelParams& params, const LevelLayout& layout);

} // namespace aige
