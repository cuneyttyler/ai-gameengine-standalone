#pragma once

#include <functional>

namespace aige {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// Answers "is the unit grid cell (gridX, gridY) solid?" — cell (gx, gy)
// occupies world-space [gx, gx+1) x [gy, gy+1). Deliberately a callback
// rather than a concrete grid type, so this math doesn't depend on how (or
// whether) a project stores its tilemap.
using SolidQueryFn = std::function<bool(int gridX, int gridY)>;

// Moves an AABB (centered at `pos`, half-extents `halfSize`) by `delta`,
// resolved one axis at a time against solid unit grid cells: an axis move
// that would end up overlapping a solid cell is rejected outright rather
// than swept to the exact wall boundary. At normal per-fixed-step deltas
// (a small fraction of one grid cell) the resulting "stops slightly early"
// error is sub-pixel in practice — exact boundary snapping is deferred until
// a system actually needs it. Resolving axes separately (rather than
// rejecting the full diagonal move) is what makes the entity slide along a
// wall instead of stopping dead when approaching it at an angle.
Vec2 ResolveAabbMovement(Vec2 pos, Vec2 halfSize, Vec2 delta, const SolidQueryFn& isSolid);

// Parts of cells that block though their cell doesn't (a door's cell: wall but for the door's gap): "does anything block
// in the world box [x0, x1) x [y0, y1)?". The game sets it while it plays (GameSession, from its TileGrid); every
// ResolveAabbMovement checks it as well as its solid cells. Empty: nothing more blocks.
using BoxQueryFn = std::function<bool(float x0, float y0, float x1, float y1)>;
void SetBlockedParts(BoxQueryFn query);
// Where a path crossing a partly blocked cell goes through it (a door's gap's middle), when it has such a point: walkers
// steer to it instead of the cell's middle. The game sets it with SetBlockedParts.
using PassPointFn = std::function<bool(int gridX, int gridY, float& x, float& y)>;
void SetPassPoints(PassPointFn query);
// A path's waypoint in cell (gx, gy): its pass point, else its middle.
Vec2 WaypointIn(int gridX, int gridY);

} // namespace aige
