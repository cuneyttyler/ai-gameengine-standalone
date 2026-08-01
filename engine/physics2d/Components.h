#pragma once

namespace aige {

// True world-space position, orthogonal grid units (1.0 == one tile cell).
// The renderer never reads this — a projection step derives ScreenTransform2D
// (engine/renderer2d/Components.h) from it each frame. Only entities that
// actually move/collide need this; static content can go straight to a
// ScreenTransform2D set once at scene-build time.
struct WorldTransform2D {
    float x = 0.0f;
    float y = 0.0f;
};

struct Velocity2D {
    float x = 0.0f;
    float y = 0.0f;
};

// Axis-aligned box centered on the entity's WorldTransform2D, in the same
// grid units.
struct Collider2D {
    float halfWidth = 0.3f;
    float halfHeight = 0.3f;
};

} // namespace aige
