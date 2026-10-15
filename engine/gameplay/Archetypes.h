#pragma once

#include "ecs/Registry.h"

#include <string>

namespace aige {

// Hand-written entity "prefabs" until a real prefab system exists: one place
// that knows which components and visuals make up a tile, an NPC or the
// player. Used by the bootstrap level and the editor's placement tools, so
// both produce identical entities.

enum class TileKind {
    Grass,
    Dirt,
    Wall,
};

// Creates a tile at grid cell (gx, gy). Does not check whether the cell is
// already occupied; callers that care use TileGrid first.
Entity CreateTile(Registry& registry, int gx, int gy, TileKind kind);

// Characters are placed in world grid units; cell centers are (n + 0.5).
Entity CreateNpc(Registry& registry, float worldX, float worldY, const std::string& scriptPath = {});
Entity CreatePlayer(Registry& registry, float worldX, float worldY);

} // namespace aige
