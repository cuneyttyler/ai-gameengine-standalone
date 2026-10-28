#include "editing/SceneEditing.h"

#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <cmath>
#include <limits>

namespace aige {

void CellAt(float sceneX, float sceneY, const IsoParams& iso, int& gx, int& gy) {
    float worldX = 0.0f;
    float worldY = 0.0f;
    UnprojectIso(sceneX, sceneY, iso, worldX, worldY);
    gx = static_cast<int>(std::floor(worldX));
    gy = static_cast<int>(std::floor(worldY));
}

std::optional<Entity> PickEntity(Registry& registry, float sceneX, float sceneY, const IsoParams& iso) {
    std::optional<Entity> best;
    float bestDepth = -std::numeric_limits<float>::infinity();

    auto view = registry.view<ScreenTransform2D, Sprite>(entt::exclude<Tile>);
    for (auto entity : view) {
        const auto& t = view.get<ScreenTransform2D>(entity);
        const auto& s = view.get<Sprite>(entity);
        bool inside = std::abs(sceneX - t.x) <= s.width * 0.5f && std::abs(sceneY - t.y) <= s.height * 0.5f;
        if (inside && t.depth >= bestDepth) {
            best = entity;
            bestDepth = t.depth;
        }
    }
    if (best) {
        // A picture shown from Equipment (a sword in a hand) isn't in the scene: its wearer is picked.
        if (registry.all_of<Transient>(*best)) {
            if (const auto* link = registry.try_get<AttachmentLink>(*best); link && registry.valid(link->parent)) return link->parent;
        }
        return best;
    }

    int gx = 0;
    int gy = 0;
    CellAt(sceneX, sceneY, iso, gx, gy);
    Entity tile = TileGrid::Build(registry).At(gx, gy);
    if (tile == entt::null) {
        return std::nullopt;
    }
    return tile;
}

Entity PaintTile(Registry& registry, int gx, int gy, TileKind kind) {
    return PaintTile(registry, gx, gy, StyleFor(kind));
}

Entity PaintTile(Registry& registry, int gx, int gy, const TileStyle& style) {
    Entity existing = TileGrid::Build(registry).At(gx, gy);
    if (existing != entt::null) {
        registry.destroy(existing);
    }
    return CreateTile(registry, gx, gy, style);
}

Entity PlaceCharacter(Registry& registry, CharacterKind kind, int gx, int gy) {
    float worldX = static_cast<float>(gx) + 0.5f;
    float worldY = static_cast<float>(gy) + 0.5f;
    if (kind == CharacterKind::Player) {
        if (auto player = FindPlayer(registry)) {
            auto& world = registry.get<WorldTransform2D>(*player);
            world.x = worldX;
            world.y = worldY;
            return *player;
        }
        return CreatePlayer(registry, worldX, worldY);
    }
    if (kind == CharacterKind::Enemy) {
        auto player = FindPlayer(registry);
        return CreateEnemy(registry, worldX, worldY, player ? *player : Entity{entt::null});
    }
    if (kind == CharacterKind::Pickup) {
        return CreateHealthPickup(registry, worldX, worldY);
    }
    return CreateNpc(registry, worldX, worldY);
}

void DeleteEntity(Registry& registry, Entity entity) {
    if (registry.valid(entity)) {
        registry.destroy(entity);
    }
}

std::optional<Entity> DuplicateEntity(Registry& registry, Entity entity) {
    if (!registry.valid(entity) || registry.all_of<Tile>(entity)) {
        return std::nullopt;
    }
    if (registry.all_of<Velocity2D, Collider2D>(entity)) {
        return std::nullopt; // the player is unique, see PlaceCharacter
    }

    // Generic copy over every component storage, so components added later
    // are duplicated without touching this code.
    Entity copy = registry.create();
    for (auto [id, storage] : registry.storage()) {
        if (id == entt::type_hash<Entity>::value()) {
            continue; // the entity storage itself, not a component
        }
        if (storage.contains(entity)) {
            storage.push(copy, storage.value(entity));
        }
    }

    // A script's loaded Lua environment must not be shared between entities:
    // keep the path, let the copy load its own.
    if (auto* script = registry.try_get<LuaScript>(copy)) {
        script->Unload();
    }

    if (auto* world = registry.try_get<WorldTransform2D>(copy)) {
        world->x += 1.0f;
    } else if (auto* screen = registry.try_get<ScreenTransform2D>(copy)) {
        screen->x += 32.0f;
    }
    return copy;
}

bool DragEntity(Registry& registry, Entity entity, float fromX, float fromY, float toX, float toY,
                const IsoParams& iso) {
    if (!registry.valid(entity)) {
        return false;
    }

    if (auto* tile = registry.try_get<Tile>(entity)) {
        int fromGx = 0;
        int fromGy = 0;
        int toGx = 0;
        int toGy = 0;
        CellAt(fromX, fromY, iso, fromGx, fromGy);
        CellAt(toX, toY, iso, toGx, toGy);
        int newGx = tile->gx + (toGx - fromGx);
        int newGy = tile->gy + (toGy - fromGy);
        if (newGx == tile->gx && newGy == tile->gy) {
            return false;
        }
        Entity occupant = TileGrid::Build(registry).At(newGx, newGy);
        if (occupant != entt::null && occupant != entity) {
            return false;
        }
        tile->gx = newGx;
        tile->gy = newGy;
        return true;
    }

    if (auto* world = registry.try_get<WorldTransform2D>(entity)) {
        float fromWx = 0.0f;
        float fromWy = 0.0f;
        float toWx = 0.0f;
        float toWy = 0.0f;
        UnprojectIso(fromX, fromY, iso, fromWx, fromWy);
        UnprojectIso(toX, toY, iso, toWx, toWy);
        if (fromWx == toWx && fromWy == toWy) {
            return false;
        }
        world->x += toWx - fromWx;
        world->y += toWy - fromWy;
        return true;
    }

    if (auto* screen = registry.try_get<ScreenTransform2D>(entity)) {
        if (fromX == toX && fromY == toY) {
            return false;
        }
        screen->x += toX - fromX;
        screen->y += toY - fromY;
        return true;
    }
    return false;
}

std::optional<size_t> IndexOfEntity(Registry& registry, Entity entity) {
    auto entities = EntitiesInOrder(registry);
    for (size_t i = 0; i < entities.size(); ++i) {
        if (entities[i] == entity) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<Entity> EntityAtIndex(Registry& registry, size_t index) {
    auto entities = EntitiesInOrder(registry);
    if (index >= entities.size()) {
        return std::nullopt;
    }
    return entities[index];
}

} // namespace aige
