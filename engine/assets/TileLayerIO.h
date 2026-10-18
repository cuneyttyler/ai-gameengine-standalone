#pragma once

#include "ecs/Registry.h"

#include <filesystem>
#include <string>

namespace aige {

// A scene's tile layers on disk (development_plan.md 7.2), next to the scene
// file: scenes/main.aige keeps them in scenes/main.tiles/:
//
//   layers.json             each layer's name, order, folder and palette
//   <folder>/r_<x>_<y>.bin  one region (128x128 cells): its chunks, run-length
//                           encoded palette indices
//
// Saving writes only the regions changed since the last save or load (and
// removes regions that became empty and layers that were removed), so saving
// a big built place after a small edit touches one file. Saving to another
// path writes everything there.

std::filesystem::path TileLayersDir(const std::filesystem::path& scenePath);

// Writes the registry's layers for the scene at `scenePath`. A scene with no
// layers leaves no folder.
void SaveTileLayers(Registry& registry, const std::filesystem::path& scenePath);

// Replaces the registry's layers with those saved for `scenePath` (none if
// there's no folder). Returns a problem, or empty.
std::string LoadTileLayers(Registry& registry, const std::filesystem::path& scenePath);

// Copies one registry's layers into another (CloneScene).
void CopyTileLayers(const Registry& from, Registry& to);

} // namespace aige
