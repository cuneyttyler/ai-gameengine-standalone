#include "editing/ScenePreview.h"

#include "assets/TextureCook.h"
#include "gameplay/GameSession.h"
#include "gameplay/TileLayer.h"
#include "renderer2d/Components.h"
#include "renderer2d/SpriteCulling.h"

#include <algorithm>
#include <array>
#include <tuple>
#include <cmath>
#include <map>
#include <optional>

namespace aige {

namespace {

struct Canvas {
    ImageData image;
    float minX = 0, minY = 0, scale = 1;
    void Clear(uint8_t r, uint8_t g, uint8_t b) {
        for (size_t i = 0; i < image.rgba.size(); i += 4) image.rgba[i] = r, image.rgba[i + 1] = g, image.rgba[i + 2] = b, image.rgba[i + 3] = 255;
    }
};

// A texture's colour at (u, v), bilinear (weighted by alpha, so a sprite's clear pixels don't darken its edge).
std::array<float, 4> Sample(const ImageData& t, float u, float v) {
    const float fx = std::clamp(u * t.width - 0.5f, 0.0f, t.width - 1.0f), fy = std::clamp(v * t.height - 0.5f, 0.0f, t.height - 1.0f);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const int jx = std::min(ix + 1, static_cast<int>(t.width) - 1), jy = std::min(iy + 1, static_cast<int>(t.height) - 1);
    const float ax = fx - ix, ay = fy - iy;
    float r = 0, g = 0, b = 0, a = 0;
    for (const auto& [x, y, w] : {std::tuple{ix, iy, (1 - ax) * (1 - ay)}, std::tuple{jx, iy, ax * (1 - ay)},
                                  std::tuple{ix, jy, (1 - ax) * ay}, std::tuple{jx, jy, ax * ay}}) {
        const uint8_t* p = &t.rgba[(static_cast<size_t>(y) * t.width + x) * 4];
        const float wa = w * p[3];
        r += p[0] * wa, g += p[1] * wa, b += p[2] * wa, a += wa;
    }
    if (a <= 0) return {0, 0, 0, 0};
    return {r / a / 255.0f, g / a / 255.0f, b / a / 255.0f, a / 255.0f};
}

// A textured quad (two triangles), sampled bilinear, alpha over.
void DrawQuad(Canvas& canvas, const SpriteQuad& q, const ImageData* texture, const Sprite& s) {
    float px[4], py[4];
    for (int i = 0; i < 4; ++i) {
        px[i] = (q.x[i] - canvas.minX) * canvas.scale;
        py[i] = (q.y[i] - canvas.minY) * canvas.scale;
    }
    const int W = static_cast<int>(canvas.image.width), H = static_cast<int>(canvas.image.height);
    for (const auto& tri : {std::array<int, 3>{0, 1, 2}, std::array<int, 3>{0, 2, 3}}) {
        const float ax = px[tri[0]], ay = py[tri[0]], bx = px[tri[1]], by = py[tri[1]], cx = px[tri[2]], cy = py[tri[2]];
        const float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
        if (std::abs(area) < 1e-6f) continue;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx}))));
        const int x1 = std::min(W - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy}))));
        const int y1 = std::min(H - 1, static_cast<int>(std::ceil(std::max({ay, by, cy}))));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float fx = x + 0.5f, fy = y + 0.5f;
                const float w0 = ((bx - fx) * (cy - fy) - (cx - fx) * (by - fy)) / area;
                const float w1 = ((cx - fx) * (ay - fy) - (ax - fx) * (cy - fy)) / area;
                const float w2 = 1 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const float u = w0 * q.u[tri[0]] + w1 * q.u[tri[1]] + w2 * q.u[tri[2]];
                const float v = w0 * q.v[tri[0]] + w1 * q.v[tri[1]] + w2 * q.v[tri[2]];
                float r = s.r, g = s.g, b = s.b, a = s.a;
                if (texture && texture->width > 0) {
                    const auto t = Sample(*texture, u, v);
                    r *= t[0], g *= t[1], b *= t[2], a *= t[3];
                } else {
                    r *= 0.8f, g *= 0.4f, b *= 0.8f; // no texture: magenta-ish, so it shows
                }
                if (a <= 0.01f) continue;
                uint8_t* p = &canvas.image.rgba[(static_cast<size_t>(y) * W + x) * 4];
                p[0] = static_cast<uint8_t>(p[0] * (1 - a) + r * 255 * a);
                p[1] = static_cast<uint8_t>(p[1] * (1 - a) + g * 255 * a);
                p[2] = static_cast<uint8_t>(p[2] * (1 - a) + b * 255 * a);
            }
        }
    }
}

} // namespace

ImageData RenderScenePreview(Registry& registry, const Project& project, const IsoParams& iso, int x0, int y0, int x1, int y1,
                             int maxWidth, std::map<Entity, std::array<float, 4>>* boxes) {
    UpdateWorldToScreen(registry, iso);
    // The region's corners on screen, with room above for what stands up.
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (const auto& [cx, cy] : {std::pair{x0, y0}, std::pair{x1, y0}, std::pair{x0, y1}, std::pair{x1, y1}}) {
        const ScreenPose p = ProjectIso(static_cast<float>(cx), static_cast<float>(cy), iso);
        minX = std::min(minX, p.x), minY = std::min(minY, p.y), maxX = std::max(maxX, p.x), maxY = std::max(maxY, p.y);
    }
    minY -= iso.tileHeight * 6; // tall buildings at the back
    Canvas canvas;
    canvas.minX = minX, canvas.minY = minY;
    // At most the art's own pixels: a painted ground is 192 px a cell, a cut sprite up to 256 (four times the cell).
    canvas.scale = std::min(4.0f, maxWidth / std::max(1.0f, maxX - minX));
    canvas.image.width = static_cast<uint32_t>(std::ceil((maxX - minX) * canvas.scale));
    canvas.image.height = static_cast<uint32_t>(std::ceil((maxY - minY) * canvas.scale));
    canvas.image.rgba.assign(static_cast<size_t>(canvas.image.width) * canvas.image.height * 4, 0);
    canvas.Clear(28, 28, 38);

    std::map<std::string, std::optional<ImageData>> textures;
    auto texture = [&](const std::string& path) -> const ImageData* {
        if (path.empty()) return nullptr;
        auto it = textures.find(path);
        if (it == textures.end()) it = textures.emplace(path, DecodeImage(project.Resolve(path))).first;
        return it->second ? &*it->second : nullptr;
    };
    // The tile layers, low order first.
    if (const TileLayers* layers = FindLayers(registry)) {
        for (const TileLayer* layer : layers->Ordered()) {
            layer->ForEachIn(x0, y0, x1, y1, [&](int gx, int gy, uint16_t id) {
                const TileDef* def = layer->Def(id);
                if (def == nullptr) return;
                const ScreenPose p = ProjectIso(gx + 0.5f, gy + 0.5f, iso);
                ScreenTransform2D t{p.x, p.y, p.depth};
                Sprite s = CellSprite(*layer, gx, gy, *def);
                s.width = iso.tileWidth, s.height = iso.tileHeight;
                DrawQuad(canvas, SpriteQuadOf(t, s, !iso.topDown), texture(s.texture), s);
            });
        }
    }
    // Sprites on the region, back to front.
    struct Item {
        float depth;
        Entity entity;
    };
    std::vector<Item> items;
    for (auto [entity, t, s] : registry.view<ScreenTransform2D, Sprite>().each()) {
        if (t.x + s.width < minX || t.x - s.width > maxX || t.y + s.height < minY || t.y - s.height > maxY) continue;
        items.push_back({t.depth, entity});
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.depth < b.depth; });
    for (const auto& item : items) {
        const auto& t = registry.get<ScreenTransform2D>(item.entity);
        const auto& s = registry.get<Sprite>(item.entity);
        const SpriteQuad quad = SpriteQuadOf(t, s, !iso.topDown);
        DrawQuad(canvas, quad, texture(s.texture), s);
        if (boxes) {
            float qx0 = 1e30f, qy0 = 1e30f, qx1 = -1e30f, qy1 = -1e30f;
            for (int k = 0; k < 4; ++k) qx0 = std::min(qx0, quad.x[k]), qy0 = std::min(qy0, quad.y[k]), qx1 = std::max(qx1, quad.x[k]), qy1 = std::max(qy1, quad.y[k]);
            (*boxes)[item.entity] = {(qx0 - canvas.minX) * canvas.scale, (qy0 - canvas.minY) * canvas.scale, (qx1 - qx0) * canvas.scale, (qy1 - qy0) * canvas.scale};
        }
    }
    return canvas.image;
}

} // namespace aige
