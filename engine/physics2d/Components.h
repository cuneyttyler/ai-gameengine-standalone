#pragma once

#include <string>

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
    // Physics v2 (development_plan.md 20.1): a circle instead of the box, and the
    // collision layer it's on and the layers it collides with ("" = by team / all).
    int shape = 0;        // 0 box, 1 circle
    float radius = 0.3f;  // circle
    std::string layer;
    std::string mask;
};

} // namespace aige
