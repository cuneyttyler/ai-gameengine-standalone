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
    // Lit by itself (windows at night, glowing runes): 2D lighting leaves it alone (18.6).
    bool emissive = false;
    // Turned (degrees, clockwise) around its position: a rigid body's angle (20.3).
    float angle = 0.0f;
    // Quarter turns clockwise of a flat image (0-3), so one road piece can
    // run either way along the grid. Upright sprites ignore it.
    int rotation = 0;
    // Raised off the ground, in screen pixels (high ground, place-refine.md
    // §10): the whole sprite by `lift`, and a flat one's corners (top, right,
    // bottom, left) each by its own, so a tile lies on a slope.
    float lift = 0.0f;
    float cornerLift[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    // A tile kind whose picture covers uvCells x uvCells cells, from the cell
    // (uvOriginX, uvOriginY): each cell shows its own square of it (a built
    // place's painted ground). 0: the uv above, the same on every cell.
    int uvCells = 0;
    int uvOriginX = 0, uvOriginY = 0;
};

} // namespace aige
