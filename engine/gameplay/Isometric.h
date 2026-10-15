#pragma once

namespace aige {

// Parameters of the 2:1 isometric projection from world grid space
// (WorldTransform2D, 1.0 == one tile cell) to screen pixels
// (ScreenTransform2D). `origin` is where world (0, 0) lands on screen.
struct IsoParams {
    float tileWidth = 64.0f;
    float tileHeight = 32.0f;
    float originX = 0.0f;
    float originY = 0.0f;
};

struct ScreenPose {
    float x;
    float y;
    float depth;
};

// World-diagonal movement maps to screen-cardinal movement; depth is
// worldX + worldY so things further "down" the screen draw on top.
inline ScreenPose ProjectIso(float worldX, float worldY, const IsoParams& iso) {
    return {
        iso.originX + (worldX - worldY) * (iso.tileWidth * 0.5f),
        iso.originY + (worldX + worldY) * (iso.tileHeight * 0.5f),
        worldX + worldY,
    };
}

// Inverse of ProjectIso on the ground plane: which world point lies under
// scene pixel (sceneX, sceneY).
inline void UnprojectIso(float sceneX, float sceneY, const IsoParams& iso, float& worldX, float& worldY) {
    float a = (sceneX - iso.originX) / (iso.tileWidth * 0.5f);  // worldX - worldY
    float b = (sceneY - iso.originY) / (iso.tileHeight * 0.5f); // worldX + worldY
    worldX = (a + b) * 0.5f;
    worldY = (b - a) * 0.5f;
}

} // namespace aige
