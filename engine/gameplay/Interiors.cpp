#include "gameplay/Interiors.h"

#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

namespace aige {

bool InteriorFootprint(Registry& registry, Entity entity, const Interior& interior, int& x0, int& y0, int& x1, int& y1) {
    if (!interior.cells.empty()) {
        return std::sscanf(interior.cells.c_str(), "%d,%d,%d,%d", &x0, &y0, &x1, &y1) == 4;
    }
    const auto* obstacle = registry.try_get<Obstacle>(entity);
    const auto* world = registry.try_get<WorldTransform2D>(entity);
    if (obstacle == nullptr || world == nullptr) return false;
    const int sx = static_cast<int>(std::floor(world->x)), sy = static_cast<int>(std::floor(world->y));
    x0 = sx - obstacle->left, x1 = sx + obstacle->right, y0 = sy - obstacle->back, y1 = sy;
    return true;
}

void UpdateInteriors(Registry& registry, std::optional<Entity> player) {
    const WorldTransform2D* at = player && registry.valid(*player) ? registry.try_get<WorldTransform2D>(*player) : nullptr;
    for (auto [entity, interior] : registry.view<Interior>().each()) {
        if (interior.texture.empty()) continue;
        int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
        bool inside = false;
        if (at != nullptr && InteriorFootprint(registry, entity, interior, x0, y0, x1, y1)) {
            const int px = static_cast<int>(std::floor(at->x)), py = static_cast<int>(std::floor(at->y));
            inside = px >= x0 && px <= x1 && py >= y0 && py <= y1;
        }
        interior.shown = inside;
    }
    ApplyInteriors(registry);
}

void ApplyInteriors(Registry& registry) {
    std::vector<std::pair<std::string, bool>> hides; // a painted building's occluders, and whether its interior shows
    for (auto [entity, interior, sprite] : registry.view<Interior, Sprite>().each()) {
        if (interior.texture.empty()) continue;
        if (!interior.cells.empty()) {
            // Painted: the painting is its outside; the cutaway over it only while inside.
            sprite.texture = interior.texture;
            sprite.a = interior.shown ? 1.0f : 0.0f;
        } else if (interior.shown && sprite.texture != interior.texture) {
            interior.exteriorTexture = sprite.texture; // put back when it's left
            sprite.texture = interior.texture;
        } else if (!interior.shown && sprite.texture == interior.texture && !interior.exteriorTexture.empty()) {
            sprite.texture = interior.exteriorTexture;
        }
        if (!interior.hides.empty()) hides.emplace_back(interior.hides, interior.shown);
    }
    if (hides.empty()) return;
    for (auto [entity, node, sprite] : registry.view<PlaceNode, Sprite>().each()) {
        for (const auto& [prefix, shown] : hides) {
            if (node.statement.rfind(prefix, 0) == 0) sprite.a = shown ? 0.0f : 1.0f;
        }
    }
}

} // namespace aige
