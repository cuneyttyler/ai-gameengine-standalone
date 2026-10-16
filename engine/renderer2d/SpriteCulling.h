#pragma once

#include "ecs/Registry.h"
#include "renderer2d/Camera2D.h"

#include <cstddef>
#include <vector>

namespace aige {

struct ScreenTransform2D;
struct Sprite;

// What Renderer2D draws (development_plan.md 4.6), without a GPU so it's
// testable: every (ScreenTransform2D, Sprite) whose quad overlaps the
// viewport, in painter's order (depth, then registry order). Sprites wholly
// outside the camera are skipped and counted in `culled`.
std::vector<Entity> VisibleSprites(Registry& registry, const Camera2D& camera, float viewportWidth,
                                   float viewportHeight, size_t* culled = nullptr);

// Whether one sprite's quad overlaps the viewport.
bool SpriteOnScreen(const ScreenTransform2D& transform, const Sprite& sprite, const Camera2D& camera,
                    float viewportWidth, float viewportHeight);

// The last frame, for the stats overlay (F3).
struct RenderStats {
    size_t sprites = 0;   // drawn
    size_t culled = 0;    // outside the camera
    size_t drawCalls = 0;
    size_t vertices = 0;
};

} // namespace aige
