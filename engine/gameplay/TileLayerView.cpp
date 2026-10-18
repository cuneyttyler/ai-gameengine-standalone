#include "gameplay/TileLayerView.h"

#include "gameplay/GameSession.h"

#include <algorithm>
#include <cmath>

namespace aige {

ScreenTransform2D LayerCellTransform(int gx, int gy, const Sprite& sprite, const IsoParams& iso) {
    const ScreenPose pose = ProjectIso(static_cast<float>(gx) + 0.5f, static_cast<float>(gy) + 0.5f, iso);
    if (sprite.flat) return {pose.x, pose.y, kGroundLayerDepth + pose.depth};
    const float extra = std::max(0.0f, sprite.height - iso.tileHeight);
    return {pose.x, pose.y - extra * 0.5f, pose.depth};
}

void CollectLayerSprites(const Registry& registry, const IsoParams& iso, const Camera2D& camera, float viewportWidth,
                         float viewportHeight, std::vector<SpriteItem>& out, size_t* culled) {
    const TileLayers* layers = FindLayers(registry);
    if (layers == nullptr) return;
    // The grid cells under the viewport: unproject its corners, take their
    // bounding box, and widen it for sprites that stand taller than a cell.
    float sx[4], sy[4];
    camera.ViewportToScene(0, 0, sx[0], sy[0]);
    camera.ViewportToScene(viewportWidth, 0, sx[1], sy[1]);
    camera.ViewportToScene(viewportWidth, viewportHeight, sx[2], sy[2]);
    camera.ViewportToScene(0, viewportHeight, sx[3], sy[3]);
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (int i = 0; i < 4; ++i) {
        float wx = 0, wy = 0;
        UnprojectIso(sx[i], sy[i], iso, wx, wy);
        minX = std::min(minX, wx);
        minY = std::min(minY, wy);
        maxX = std::max(maxX, wx);
        maxY = std::max(maxY, wy);
    }
    size_t skipped = 0;
    for (const TileLayer* layer : layers->Ordered()) {
        float tallest = 0.0f;
        for (const auto& def : layer->Palette()) tallest = std::max(tallest, def.sprite.height);
        const int margin = 1 + static_cast<int>(std::ceil(tallest / std::max(1.0f, iso.tileHeight)));
        const int x0 = static_cast<int>(std::floor(minX)) - margin, y0 = static_cast<int>(std::floor(minY)) - margin;
        const int x1 = static_cast<int>(std::ceil(maxX)) + margin + 1, y1 = static_cast<int>(std::ceil(maxY)) + margin + 1;
        // Zoomed far out, the box can be huge: clamp it to the layer.
        const auto b = layer->ChunkBounds();
        layer->ForEachIn(std::max(x0, b[0]), std::max(y0, b[1]), std::min(x1, b[2]), std::min(y1, b[3]),
                         [&](int gx, int gy, uint16_t id) {
                             const TileDef* def = layer->Def(id);
                             if (def == nullptr) return;
                             const ScreenTransform2D t = LayerCellTransform(gx, gy, def->sprite, iso);
                             if (SpriteOnScreen(t, def->sprite, camera, viewportWidth, viewportHeight)) {
                                 out.push_back({t, &def->sprite});
                             } else {
                                 ++skipped;
                             }
                         });
    }
    if (culled) *culled += skipped;
}

LayerCell TopLayerCell(const Registry& registry, int gx, int gy) {
    const TileLayers* layers = FindLayers(registry);
    if (layers == nullptr) return {};
    const auto ordered = layers->Ordered();
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
        if (const uint16_t id = (*it)->Get(gx, gy); id != TileLayer::kEmpty) return {*it, id, (*it)->Def(id)};
    }
    return {};
}

} // namespace aige
