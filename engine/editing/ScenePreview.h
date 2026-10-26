#pragma once

#include "assets/Project.h"
#include "ecs/Registry.h"
#include "gameplay/Isometric.h"
#include "renderer2d/ImageData.h"

#include <array>
#include <map>

namespace aige {

// A picture of part of the scene, drawn on the CPU the way the renderer draws
// it (development_plan.md 7.7): the tile layers' cells as flat tiles, then
// every sprite in depth order, textures sampled from the project, alpha
// blended. Without a window: the build checks what it put in the scene, and
// the AI looks at the rendered place, not only at the plan's map.
//
// The cells [x0, x1) x [y0, y1) are what's shown (with what stands on them),
// scaled to at most `maxWidth` pixels across.
// `boxes` (optional): each sprite drawn, its box in the picture's pixels {x, y, w, h}.
ImageData RenderScenePreview(Registry& registry, const Project& project, const IsoParams& iso, int x0, int y0, int x1,
                             int y1, int maxWidth = 1600, std::map<Entity, std::array<float, 4>>* boxes = nullptr);

} // namespace aige
