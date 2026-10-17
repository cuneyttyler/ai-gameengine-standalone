#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aige {

// Settlement layout (development_plan.md 6.5): streets with a square, plots
// along them with houses facing the road, fenced gardens, a well, landmarks
// at the entrances, decorations, and nature around the edge. Pure and
// deterministic — the same parameters and seed always give the same village —
// so it can be previewed in review and tested. Putting it into a scene, with
// the pack's prefabs and autotiled roads, is editing/VillageBuild.

struct VillageParams {
    int x0 = 0, y0 = 0;    // top-left cell; the village grows right and down from it
    int houses = 8;        // 1-60
    uint32_t seed = 1;
    int roadWidth = 1;     // 1-3
    int houseCells = 2;    // a house's footprint side in cells (from the house art's size)
    float nature = 0.5f;   // 0-1: trees, bushes and rocks around the edge and in empty plots
    bool fences = true;    // fence the gardens behind the houses
    bool square = true;    // a square with a well where the main streets cross
};

using VillageCell = std::pair<int, int>;

enum class VillageRole {
    House,
    Well,
    Tree,
    Bush,
    Rock,
    Decoration, // barrels, carts, campfires, chests
    Entrance,   // banners and flags where the roads leave the village
    Landmark,   // a watchtower, a windmill: one, by an entrance
    FenceH,     // runs left-right (along x)
    FenceV,     // runs up-down (along y)
};

const char* VillageRoleName(VillageRole role);

struct VillageItem {
    VillageRole role;
    float x = 0.0f, y = 0.0f; // the middle of its footprint (world units = cells)
    int size = 1;             // footprint side in cells: houses and landmarks are bigger
    bool mirror = false;      // upright art flipped, for variety
    int pick = 0;             // which of the role's prefabs (modulo how many there are)
};

struct VillagePlot {
    int x0, y0, x1, y1;       // inclusive
    bool northOfRoad;         // the road runs along its south side (else its north side)
    bool hasHouse = false;
};

struct VillageLayout {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // the whole area, inclusive
    std::set<VillageCell> road;
    std::set<VillageCell> square;
    std::vector<VillagePlot> plots;
    std::vector<VillageItem> items;
    int houses = 0;

    // A character map for review: '=' road, '#' square, 'H' house, 'f' fence,
    // 'T' tree, 'b' bush, 'r' rock, 'w' well, 'd' decoration, '!' entrance,
    // 'L' landmark, '.' ground; one line per row, north at the top.
    std::string Ascii() const;
};

// The cells an item stands on (its size × size footprint around x, y).
std::vector<VillageCell> VillageFootprint(const VillageItem& item);

std::optional<std::string> CheckVillageParams(const VillageParams& params);
std::optional<VillageLayout> GenerateVillage(const VillageParams& params, std::string* error = nullptr);

} // namespace aige
