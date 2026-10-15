#pragma once

#include "ecs/Registry.h"
#include "renderer2d/Camera2D.h"
#include "renderer2d/Components.h"

#include <cmath>

namespace aige {

// Eases the camera toward centering `target` in a viewport of the given size
// (pixels). `stiffness` is how quickly it catches up (higher = snappier);
// frame-rate independent. Does nothing if the target has no screen position.
inline void FollowWithCamera(Camera2D& camera, Registry& registry, Entity target, float viewportWidth,
                             float viewportHeight, float dt, float stiffness = 6.0f) {
    if (!registry.valid(target)) {
        return;
    }
    const auto* screen = registry.try_get<ScreenTransform2D>(target);
    if (screen == nullptr) {
        return;
    }
    float desiredX = screen->x - viewportWidth * 0.5f / camera.zoom;
    float desiredY = screen->y - viewportHeight * 0.5f / camera.zoom;
    float t = 1.0f - std::exp(-stiffness * dt);
    camera.offsetX += (desiredX - camera.offsetX) * t;
    camera.offsetY += (desiredY - camera.offsetY) * t;
}

// Centers the camera on `target` immediately (e.g. when play starts).
inline void SnapCameraTo(Camera2D& camera, Registry& registry, Entity target, float viewportWidth,
                         float viewportHeight) {
    FollowWithCamera(camera, registry, target, viewportWidth, viewportHeight, 1.0f, 1000.0f);
}

} // namespace aige
