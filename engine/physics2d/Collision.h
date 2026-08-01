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

} // namespace aige
