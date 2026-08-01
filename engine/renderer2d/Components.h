#pragma once

namespace aige {

// Screen-space position (pixels, origin top-left, Y down) — no camera/zoom
// yet, that's introduced once there's a reason to move the viewport
// (development_plan.md Phase 1). `depth` is the painter's-algorithm sort key:
// lower draws first, so nearer/foreground content should use a larger value.
// Content code (not the renderer) decides what depth means — e.g. an
// isometric tilemap uses gridX + gridY.
struct Transform2D {
    float x = 0.0f;
    float y = 0.0f;
    float depth = 0.0f;
};

// A sprite quad sampled from Renderer2D's shared atlas texture. (u0,v0)-(u1,v1)
// is the sub-rect in the atlas; (r,g,b,a) tints the sampled texel.
struct Sprite {
    float width = 32.0f;
    float height = 32.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

} // namespace aige
