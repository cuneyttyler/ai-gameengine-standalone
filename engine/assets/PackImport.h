#pragma once

#include "assets/AssetDatabase.h"
#include "assets/Prefab.h"
#include "gameplay/Isometric.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace aige {

// Turning an art pack into prefabs at the right size (development_plan.md 6.3).
//
// A pack is drawn at one scale: its ground tiles are a cell, its barrel a
// quarter of its house. So every image of a folder is sized by one number,
// pixels per cell, instead of each being guessed: a 480 px house in a pack
// of 256 px tiles is 1.9 cells wide, whatever cell size the game uses.

struct PackScale {
    float pixelsPerCell = 0.0f;
    std::string source; // "set by hand", "ground tiles", "analysis", "default"
};

// <folder>/pack.json {"pixelsPerCell": N}: set by hand, kept with the pack.
std::optional<float> ReadPackScale(const Project& project, const std::string& folder);
bool WritePackScale(const Project& project, const std::string& folder, float pixelsPerCell);

// The pack's scale: as set, else its ground tiles' width (tiles are a cell),
// else what the image analysis says things measure, else 1 cell per tile width.
PackScale PackScaleFor(const Project& project, const AssetDatabase& assets, const std::string& folder,
                       const IsoParams& iso);

// One prefab to make from one image.
struct PrefabPlan {
    std::string asset;  // the image
    std::string path;   // prefabs/<pack>/<name>.prefab
    std::string name;
    std::string kind;   // from the analysis, or "ground" for tiles found by their edges
    bool tile = false;  // a ground tile (painted, flat) or a prop (placed)
    bool solid = false; // tiles: block movement; props: block their cell
    float width = 0.0f; // on screen, pixels
    float height = 0.0f;
};

// What importing `images` would make: ground tiles become flat tiles of one
// cell; everything else props sized by the pack scale, solid if the analysis
// says they block the way (or they're buildings). Names from the analysis
// (else the file), unique among themselves and the existing prefabs.
std::vector<PrefabPlan> PlanPackImport(const AssetDatabase& assets, const std::vector<std::string>& images,
                                       const PrefabLibrary& prefabs, const IsoParams& iso,
                                       const std::function<float(const std::string& folder)>& pixelsPerCell);

// Makes one planned prefab. Returns why not, or "".
std::string CreatePrefabFromPlan(PrefabLibrary& prefabs, const PrefabPlan& plan, const IsoParams& iso);

// "Top-Down Simple Summer_Prop - Well" -> "top-down-simple-summer-prop-well".
std::string PrefabSlug(const std::string& text);

} // namespace aige
