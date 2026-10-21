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

#include "gameplay/Isometric.h"
#include "gameplay/Settings.h"
#include "gameplay/TileLayer.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <limits>

namespace aige {

// The play camera (development_plan.md 12.7): it follows the target through a
// dead zone (the target moves freely in the middle of the screen), looks ahead
// in the way it faces, and stays inside the level when asked. Shake (Phase 18)
// is added on top without moving the camera's own position.
struct CameraRig {
    float levelMinX = 0.0f, levelMinY = 0.0f, levelMaxX = 0.0f, levelMaxY = 0.0f; // scene pixels
    bool hasLevel = false;
    float shakeX = 0.0f, shakeY = 0.0f; // this frame's shake offset, scene pixels

    // The level's extent: every sprite's position and every tile layer's cells.
    // Call when a scene starts (not every frame: it visits everything).
    void MeasureLevel(Registry& registry, const IsoParams& iso) {
        float x0 = std::numeric_limits<float>::max(), y0 = x0, x1 = -x0, y1 = -x0;
        auto grow = [&](float x, float y) {
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
        };
        for (auto [entity, screen] : registry.view<ScreenTransform2D>().each()) grow(screen.x, screen.y);
        if (const TileLayers* layers = FindLayers(registry)) {
            for (const auto& [name, layer] : layers->layers) {
                if (layer.Empty()) continue;
                const auto b = layer.Bounds();
                for (auto [gx, gy] : {std::pair{b[0], b[1]}, std::pair{b[2], b[1]}, std::pair{b[0], b[3]}, std::pair{b[2], b[3]}}) {
                    const ScreenPose pose = ProjectIso(static_cast<float>(gx), static_cast<float>(gy), iso);
                    grow(pose.x, pose.y);
                }
            }
        }
        hasLevel = x1 >= x0;
        if (hasLevel) {
            levelMinX = x0 - iso.tileWidth;
            levelMinY = y0 - iso.tileHeight * 2.0f;
            levelMaxX = x1 + iso.tileWidth;
            levelMaxY = y1 + iso.tileHeight;
        }
    }
};

// Moves the camera toward the target, frame-rate independent.
inline void UpdatePlayCamera(Camera2D& camera, CameraRig& rig, Registry& registry, Entity target, float viewportWidth,
                             float viewportHeight, float dt, const CameraSettings& settings) {
    if (!registry.valid(target)) return;
    const auto* screen = registry.try_get<ScreenTransform2D>(target);
    if (screen == nullptr) return;
    const float halfW = viewportWidth * 0.5f / camera.zoom;
    const float halfH = viewportHeight * 0.5f / camera.zoom;
    float focusX = screen->x;
    float focusY = screen->y;
    // Look ahead: the way the target moves, a few cells, eased.
    if (settings.lookAhead > 0.0f) {
        if (const auto* velocity = registry.try_get<Velocity2D>(target)) {
            const float speed = std::sqrt(velocity->x * velocity->x + velocity->y * velocity->y);
            if (speed > 0.05f) {
                // World direction to screen direction (isometric: diagonal).
                const float sx = velocity->x - velocity->y;
                const float sy = (velocity->x + velocity->y) * 0.5f;
                const float length = std::max(0.001f, std::sqrt(sx * sx + sy * sy));
                focusX += sx / length * settings.lookAhead * 32.0f;
                focusY += sy / length * settings.lookAhead * 16.0f;
            }
        }
    }
    // Dead zone: the camera moves only to keep the focus inside it.
    const float centerX = camera.offsetX + halfW;
    const float centerY = camera.offsetY + halfH;
    const float zoneW = halfW * std::clamp(settings.deadZone, 0.0f, 0.45f) * 2.0f;
    const float zoneH = halfH * std::clamp(settings.deadZone, 0.0f, 0.45f) * 2.0f;
    float desiredCenterX = centerX, desiredCenterY = centerY;
    if (focusX < centerX - zoneW) desiredCenterX = focusX + zoneW;
    if (focusX > centerX + zoneW) desiredCenterX = focusX - zoneW;
    if (focusY < centerY - zoneH) desiredCenterY = focusY + zoneH;
    if (focusY > centerY + zoneH) desiredCenterY = focusY - zoneH;
    float desiredX = desiredCenterX - halfW;
    float desiredY = desiredCenterY - halfH;
    if (settings.clampToLevel && rig.hasLevel) {
        // Centred when the level is smaller than the screen.
        auto clampAxis = [](float value, float lo, float hi, float span) {
            if (hi - lo <= span) return (lo + hi - span) * 0.5f;
            return std::clamp(value, lo, hi - span);
        };
        desiredX = clampAxis(desiredX, rig.levelMinX, rig.levelMaxX, halfW * 2.0f);
        desiredY = clampAxis(desiredY, rig.levelMinY, rig.levelMaxY, halfH * 2.0f);
    }
    const float t = dt >= 1.0f ? 1.0f : 1.0f - std::exp(-settings.followStiffness * dt);
    camera.offsetX += (desiredX - camera.offsetX) * t;
    camera.offsetY += (desiredY - camera.offsetY) * t;
}

} // namespace aige
