#pragma once

namespace aige {

// Maps scene pixels (ScreenTransform2D space) to viewport pixels:
// viewport = (scene - offset) * zoom. The default is the identity, which is
// what the sandbox uses; the editor pans and zooms it.
struct Camera2D {
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float zoom = 1.0f;

    void SceneToViewport(float sceneX, float sceneY, float& viewX, float& viewY) const {
        viewX = (sceneX - offsetX) * zoom;
        viewY = (sceneY - offsetY) * zoom;
    }

    void ViewportToScene(float viewX, float viewY, float& sceneX, float& sceneY) const {
        sceneX = viewX / zoom + offsetX;
        sceneY = viewY / zoom + offsetY;
    }

    // Changes zoom while keeping the scene point under (viewX, viewY) fixed
    // on screen — the expected feel for mouse-wheel zoom.
    void ZoomAt(float viewX, float viewY, float newZoom) {
        float sceneX = 0.0f;
        float sceneY = 0.0f;
        ViewportToScene(viewX, viewY, sceneX, sceneY);
        zoom = newZoom;
        offsetX = sceneX - viewX / zoom;
        offsetY = sceneY - viewY / zoom;
    }
};

} // namespace aige
