#pragma once

namespace aige {

// A ground tile occupying one isometric grid cell. Its ScreenTransform2D is
// derived from (gx, gy) every frame (UpdateWorldToScreen), so moving a tile
// means changing its cell. Collision comes from tiles too (TileGrid): a cell
// is walkable only if it holds a non-solid tile, so empty space is a wall.
struct Tile {
    int gx = 0;
    int gy = 0;
    bool solid = false;
};

} // namespace aige
