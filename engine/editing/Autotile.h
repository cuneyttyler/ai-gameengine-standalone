#pragma once

#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/TileEdges.h"
#include "ecs/Registry.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace aige {

// Autotiling (development_plan.md 6.4): say which cells are road (or water,
// or sand) and the engine picks each cell's piece — straight, corner, end,
// junction — from the pack's ground tiles, by their border terrains (6.2).
//
// The rule for what a cell needs, its 8 border points plus its middle:
//   middle      = the cell's terrain
//   edge (N…)   = the cell's terrain if the neighbour across it has it too,
//                 else the neighbour's
//   corner (NW…)= the cell's terrain only if all four cells meeting there
//                 have it, else the background's
// so one-cell roads get the path pieces (road at N and S, grass elsewhere) and
// wide areas get their rims and inner corners. Background cells (the grass)
// are always plain: the road's own tiles hold the transition, so a road stays
// as wide as it was painted. Flat tiles may be turned a
// quarter (5.2) when that matches better.

struct TerrainTile {
    std::string prefab;
    EdgeSignature edges;
    std::string center;
};

struct TerrainSet {
    std::vector<TerrainTile> tiles;
    std::vector<std::string> terrains; // every terrain the tiles use
    // The tile prefab that is plain `terrain` all over (what a cell of it gets
    // when nothing around differs), if there is one.
    std::optional<std::string> PlainTile(const std::string& terrain) const;
    // The terrain a tile prefab stands for (its middle), if it's in the set.
    std::optional<std::string> TerrainOf(const std::string& prefab) const;
};

// Every tile prefab whose texture has border analysis. `analysisOf` gives an
// asset's analysis fields (ReadAssetAnalysis); in prefab order.
TerrainSet BuildTerrainSet(const PrefabLibrary& prefabs,
                           const std::function<std::map<std::string, std::string>(const std::string& asset)>& analysisOf);

// Every asset's analysis as the project's .meta files have it, with border
// edges and middles read from the pixels for tile prefabs' images that were
// never analyzed — the edge pass (6.2) needs no AI, so a pack works without
// pressing Analyze first. One terrain clustering per folder, as Analyze does.
// Keyed by asset path; assets nobody uses are left out.
using AssetAnalysis = std::map<std::string, std::map<std::string, std::string>>;
AssetAnalysis ProjectAnalysis(const PrefabLibrary& prefabs, const Project& project);

// BuildTerrainSet over ProjectAnalysis: the terrain set the commands use.
TerrainSet ProjectTerrainSet(const PrefabLibrary& prefabs, const Project& project);

// What a cell needs, given every cell's terrain and the background's.
EdgeSignature TargetSignature(const std::function<std::string(int, int)>& terrainAt, int gx, int gy,
                              const std::string& background);

// The rotation `turns` quarter turns clockwise of a signature (5.2 rotation).
EdgeSignature Rotated(const EdgeSignature& signature, int turns);

struct TileChoice {
    std::string prefab;
    int rotation = 0;
    int mismatches = 0; // border points that don't match (0 = exact)
};

// The best tile for a target: fewest mismatching points, an exact middle
// required; unturned preferred over turned; then the set's order. Nothing
// when no tile has that middle. With a `variety` (a hash of the cell), the
// equally good tiles take turns, and a tile that's the same ground on every
// side turns freely: a field of one grass doesn't show its repeat
// (place-refine.md §1).
std::optional<TileChoice> ChooseTile(const TerrainSet& set, const EdgeSignature& target, const std::string& center,
                                     unsigned variety = 0);
// A cell's variety, for ChooseTile.
unsigned CellVariety(int gx, int gy);

// Paints `terrain` onto `cells` and re-fits them and their neighbours. Cells
// without a set tile count as `background` (the pack's grass, say), and stay
// as they are unless they're painted. Returns the number of cells changed.
int PaintTerrain(Registry& registry, PrefabLibrary& prefabs, const TerrainSet& set,
                 const std::vector<std::pair<int, int>>& cells, const std::string& terrain,
                 const std::string& background);

// What surrounds `terrain` when nothing says otherwise: the set's most common
// other middle terrain (the pack's grass when painting a dirt road).
std::string DefaultBackground(const TerrainSet& set, const std::string& terrain);

// "3,4;10,4;10,12" -> {{3,4},{10,4},{10,12}}; empty on a malformed list.
std::vector<std::pair<int, int>> ParseCells(const std::string& text);

// The cells of a road through `points` (a polyline of cells), `width` cells
// wide, in straight steps along the grid.
std::vector<std::pair<int, int>> PathCells(const std::vector<std::pair<int, int>>& points, int width);

} // namespace aige
