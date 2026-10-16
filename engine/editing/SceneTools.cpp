#include "editing/SceneTools.h"

#include "assets/SceneIO.h"
#include "editing/Edits.h"
#include "editing/SceneEditing.h"
#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "reflection/Reflection.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cmath>

namespace aige {

std::optional<std::string> ScaleEntity(const ReflectionRegistry& reflection, Registry& registry, Entity entity,
                                       float factor) {
    if (!registry.valid(entity)) return "that entity no longer exists";
    if (factor <= 0.0f) return "the scale must be positive";
    const auto* sprite = registry.try_get<Sprite>(entity);
    if (sprite == nullptr) return "it has no sprite to resize";
    // Keep sprites within sane bounds: too small to see or too big to place.
    const float biggest = std::max(sprite->width, sprite->height);
    const float smallest = std::min(sprite->width, sprite->height);
    factor = std::clamp(factor, 4.0f / std::max(1.0f, smallest), 2048.0f / std::max(1.0f, biggest));

    auto scale = [&](const char* component, const char* field) -> std::optional<std::string> {
        const ComponentInfo* info = reflection.FindComponent(component);
        const FieldInfo* f = info ? info->Field(field) : nullptr;
        if (f == nullptr || !info->has(registry, entity)) return std::nullopt;
        float value = ToFloat(f->get(registry, entity)) * factor;
        if (f->max) value = std::min(value, static_cast<float>(*f->max));
        if (f->min) value = std::max(value, static_cast<float>(*f->min));
        return EditField(reflection, registry, entity, component, field, value);
    };
    for (auto [component, field] : {std::pair{"Sprite", "width"}, std::pair{"Sprite", "height"},
                                    std::pair{"Collider2D", "halfWidth"}, std::pair{"Collider2D", "halfHeight"},
                                    std::pair{"Hurtbox", "halfWidth"}, std::pair{"Hurtbox", "halfHeight"}}) {
        if (auto problem = scale(component, field)) return problem;
    }
    return std::nullopt;
}

bool NudgeEntity(Registry& registry, Entity entity, float dx, float dy, const IsoParams& iso) {
    if (!registry.valid(entity)) return false;
    if (registry.all_of<Tile>(entity)) {
        dx = std::round(dx);
        dy = std::round(dy);
        if (dx == 0.0f && dy == 0.0f) return false;
    }
    // DragEntity speaks scene pixels: project the world step.
    float x0 = 0.0f, y0 = 0.0f;
    if (const auto* tile = registry.try_get<Tile>(entity)) {
        x0 = tile->gx + 0.5f;
        y0 = tile->gy + 0.5f;
    } else if (const auto* world = registry.try_get<WorldTransform2D>(entity)) {
        x0 = world->x;
        y0 = world->y;
    } else {
        return false;
    }
    ScreenPose from = ProjectIso(x0, y0, iso);
    ScreenPose to = ProjectIso(x0 + dx, y0 + dy, iso);
    return DragEntity(registry, entity, from.x, from.y, to.x, to.y, iso);
}

std::string CopyEntities(Registry& registry, const std::vector<Entity>& entities) {
    std::vector<Entity> valid;
    for (Entity e : entities)
        if (registry.valid(e)) valid.push_back(e);
    return SerializeEntities(registry, valid);
}

std::vector<Entity> PasteEntities(Registry& registry, const std::string& text, int gx, int gy) {
    std::vector<Entity> pasted = AppendEntities(registry, text);
    if (pasted.empty()) return pasted;
    // Where the first one was: everything shifts by the same whole cells.
    auto cellOf = [&](Entity e) -> std::optional<std::pair<float, float>> {
        if (const auto* tile = registry.try_get<Tile>(e)) return std::pair{tile->gx + 0.5f, tile->gy + 0.5f};
        if (const auto* world = registry.try_get<WorldTransform2D>(e)) return std::pair{world->x, world->y};
        return std::nullopt;
    };
    auto anchor = cellOf(pasted.front());
    if (!anchor) return pasted;
    const int shiftX = gx - static_cast<int>(std::floor(anchor->first));
    const int shiftY = gy - static_cast<int>(std::floor(anchor->second));
    for (Entity e : pasted) {
        if (const auto* tile = registry.try_get<Tile>(e)) {
            const int tx = tile->gx + shiftX, ty = tile->gy + shiftY;
            // A cell holds one tile: the pasted one wins.
            Entity occupant = entt::null;
            for (auto [other, existing] : registry.view<Tile>().each()) {
                if (other != e && existing.gx == tx && existing.gy == ty &&
                    std::find(pasted.begin(), pasted.end(), other) == pasted.end()) {
                    occupant = other;
                    break;
                }
            }
            if (occupant != entt::null) registry.destroy(occupant); // moves Tile storage: look it up again
            auto& moved = registry.get<Tile>(e);
            moved.gx = tx;
            moved.gy = ty;
            if (auto* world = registry.try_get<WorldTransform2D>(e)) *world = {tx + 0.5f, ty + 0.5f};
        } else if (auto* world = registry.try_get<WorldTransform2D>(e)) {
            world->x += static_cast<float>(shiftX);
            world->y += static_cast<float>(shiftY);
        }
    }
    return pasted;
}

} // namespace aige
