#include "renderer2d/SpriteCulling.h"

#include "renderer2d/Components.h"

#include <algorithm>
#include <cmath>

namespace aige {

bool SpriteOnScreen(const ScreenTransform2D& t, const Sprite& s, const Camera2D& camera, float viewportWidth,
                    float viewportHeight) {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    camera.SceneToViewport(t.x - s.width * 0.5f, t.y - s.height * 0.5f, x0, y0);
    camera.SceneToViewport(t.x + s.width * 0.5f, t.y + s.height * 0.5f, x1, y1);
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    return x1 >= 0.0f && y1 >= 0.0f && x0 <= viewportWidth && y0 <= viewportHeight;
}

SpriteQuad SpriteQuadOf(const ScreenTransform2D& t, const Sprite& s) {
    SpriteQuad q{};
    const float hw = s.width * 0.5f;
    const float hh = s.height * 0.5f;
    if (s.flat) {
        // Top, right, bottom, left: the cell's corners at world (gx, gy),
        // (gx+1, gy), (gx+1, gy+1), (gx, gy+1).
        const float xs[4] = {t.x, t.x + hw, t.x, t.x - hw};
        const float ys[4] = {t.y - hh, t.y, t.y + hh, t.y};
        std::copy(xs, xs + 4, q.x);
        std::copy(ys, ys + 4, q.y);
    } else {
        const float xs[4] = {t.x - hw, t.x + hw, t.x + hw, t.x - hw};
        const float ys[4] = {t.y - hh, t.y - hh, t.y + hh, t.y + hh};
        std::copy(xs, xs + 4, q.x);
        std::copy(ys, ys + 4, q.y);
    }
    // The image's corners, clockwise from its top-left.
    const float us[4] = {s.u0, s.u1, s.u1, s.u0};
    const float vs[4] = {s.v0, s.v0, s.v1, s.v1};
    const int turns = s.flat ? ((s.rotation % 4) + 4) % 4 : 0;
    for (int i = 0; i < 4; ++i) {
        // A clockwise turn moves the image's top-left to the next corner.
        const int corner = (i - turns + 4) % 4;
        q.u[i] = us[corner];
        q.v[i] = vs[corner];
    }
    return q;
}

std::vector<Entity> VisibleSprites(Registry& registry, const Camera2D& camera, float viewportWidth,
                                   float viewportHeight, size_t* culled) {
    struct Item {
        Entity entity;
        float depth;
    };
    std::vector<Item> items;
    auto view = registry.view<ScreenTransform2D, Sprite>();
    items.reserve(view.size_hint());
    size_t skipped = 0;
    for (auto [entity, transform, sprite] : view.each()) {
        if (SpriteOnScreen(transform, sprite, camera, viewportWidth, viewportHeight)) {
            items.push_back({entity, transform.depth});
        } else {
            ++skipped;
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.depth < b.depth; });
    if (culled) {
        *culled = skipped;
    }
    std::vector<Entity> out;
    out.reserve(items.size());
    for (const auto& item : items) {
        out.push_back(item.entity);
    }
    return out;
}

} // namespace aige
