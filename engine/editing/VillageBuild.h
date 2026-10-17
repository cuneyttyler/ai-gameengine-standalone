#pragma once

#include "assets/Project.h"
#include "ecs/Registry.h"
#include "editing/Autotile.h"
#include "gameplay/VillageGen.h"
#include "reflection/Reflection.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aige {

class PrefabLibrary;

// Building a settlement layout (gameplay/VillageGen) out of a pack
// (development_plan.md 6.5): every prop prefab gets a role from its art's
// analysis (6.2) — houses, the well, fences by direction, trees, bushes,
// rocks, decorations, banners, a tower — and the ground and road come from
// the analyzed ground tiles, laid with autotiling (6.4).

// The roles a prefab can fill, from its art's analysis fields (kind, tags,
// name, description) or, without analysis, its file name. A fence that runs
// neither way fills both fence roles; nothing fits: empty.
std::vector<VillageRole> VillageRolesOf(const std::map<std::string, std::string>& analysis, const std::string& prefabName);

struct VillageRoles {
    std::map<VillageRole, std::vector<std::string>> prefabs; // in prefab order
    std::string ground;     // background terrain (grass)
    std::string road;       // road and square terrain (dirt)
    std::string groundTile; // the plain ground tile prefab
    int houseCells = 2;     // the houses' footprint side, from their art's size
    // Roles the layout uses but nothing fills, for the review ("no well").
    std::vector<std::string> Missing() const;
};

// Sorts the project's prop prefabs into roles; picks the ground and road
// terrains (`ground`/`road` empty: grass-like and dirt-like ones). `tileWidth`
// converts a prefab's width to cells.
VillageRoles FindVillageRoles(const PrefabLibrary& prefabs, const TerrainSet& set,
                              const std::function<std::map<std::string, std::string>(const std::string& asset)>& analysisOf,
                              float tileWidth, const std::string& ground = {}, const std::string& road = {});

// level.village's arguments.
struct VillageRequest {
    VillageParams params;
    bool autoHouseCells = true; // houseCells 0: from the house art
    std::string ground, road;   // terrain names; empty: found
    bool movePlayer = true;
};
VillageRequest VillageRequestFromArgs(const std::vector<FieldValue>& args);
VillageRequest VillageRequestFromJson(const nlohmann::json& args);

struct VillagePlan {
    VillageParams params;
    VillageLayout layout;
    VillageRoles roles;
    TerrainSet set;
};

// Roles, then the layout sized for the houses: everything review needs to
// show what will be built, and everything BuildVillage needs. Nothing changes.
std::optional<VillagePlan> PlanVillage(const PrefabLibrary& prefabs, const Project& project, const VillageRequest& request,
                                       std::string* error);

struct VillageBuildResult {
    int cleared = 0;
    int groundTiles = 0;
    int roadTiles = 0;
    int props = 0;
};

// Puts it into the scene: the area cleared (but the player), ground filled,
// roads and the square laid with fitted tiles, every item placed with one of
// its role's prefabs (upright art mirrored where the layout says), and the
// player moved onto the main street.
std::optional<std::string> BuildVillage(Registry& registry, PrefabLibrary& prefabs, const VillagePlan& plan,
                                        bool movePlayer, VillageBuildResult* result = nullptr);

// For review panels: what will be built, with what, and a map.
std::string DescribeVillage(const VillagePlan& plan);

} // namespace aige
