#pragma once

#include "ecs/Registry.h"
#include "gameplay/Isometric.h"
#include "gameplay/TileLayer.h"
#include "renderer2d/Camera2D.h"
#include "renderer2d/SpriteCulling.h"

#include <vector>

namespace aige {

// Where a layer cell's sprite is drawn: the same rule as a Tile entity
// (GameSession's world-to-screen step). Flat ground lies on the cell's
// diamond at the ground depth; a taller sprite stands on the cell.
ScreenTransform2D LayerCellTransform(int gx, int gy, const Sprite& sprite, const IsoParams& iso);

// The layer cells the camera sees, as sprites, appended to `out` in layer
// order (low first), so cells of one depth keep that order when sorted.
// Only chunks near the view are visited: the cost follows what's on screen,
// not the size of the scene. `culled` (optional) counts visited cells that
// fell outside.
void CollectLayerSprites(const Registry& registry, const IsoParams& iso, const Camera2D& camera, float viewportWidth,
                         float viewportHeight, std::vector<SpriteItem>& out, size_t* culled = nullptr);

// The topmost layer cell at (gx, gy), or null: which layer and its tile.
struct LayerCell {
    const TileLayer* layer = nullptr;
    uint16_t id = 0;
    const TileDef* def = nullptr;
};
LayerCell TopLayerCell(const Registry& registry, int gx, int gy);

} // namespace aige
