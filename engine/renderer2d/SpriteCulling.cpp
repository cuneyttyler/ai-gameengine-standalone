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
