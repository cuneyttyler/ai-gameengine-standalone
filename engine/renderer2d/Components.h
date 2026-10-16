#pragma once

#include <string>

namespace aige {

// Screen-space position (pixels, origin top-left, Y down) — what the renderer
// actually draws at. `depth` is the painter's-algorithm sort key: lower draws
// first, so nearer/foreground content should use a larger value. Content code
// (not the renderer) decides what depth means. Static content can set this
// directly; moving entities instead carry a WorldTransform2D
// (engine/physics2d/Components.h) and have a projection step (currently the
// isometric one in apps/sandbox/main.cpp) write this each frame — the
// renderer never knows about "world space" or "isometric" at all.
struct ScreenTransform2D {
    float x = 0.0f;
    float y = 0.0f;
    float depth = 0.0f;
};

// A sprite quad. `texture` is a project-relative asset path ("assets/
// textures/hero.png"), or empty for Renderer2D's built-in placeholder atlas.
// (u0,v0)-(u1,v1) is the sub-rect sampled from it (a tileset tile, or the
// whole image); (r,g,b,a) tints the sampled texel. width/height are the
// on-screen size in scene pixels.
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
    std::string texture;
    // Lies flat on the ground (development_plan.md 5.2): the image, drawn
    // top-down, is mapped corner to corner onto the width x height diamond
    // around the position (an isometric cell when they're the tile size)
    // instead of standing upright.
    bool flat = false;
    // Quarter turns clockwise of a flat image (0-3), so one road piece can
    // run either way along the grid. Upright sprites ignore it.
    int rotation = 0;
};

} // namespace aige
