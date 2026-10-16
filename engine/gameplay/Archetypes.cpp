#include "gameplay/Archetypes.h"

#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

namespace aige {

namespace {

constexpr float kTileWidth = 64.0f;
constexpr float kTileHeight = 32.0f;

// UV sub-rects into Renderer2D's 2x2 procedural atlas (Renderer2D.cpp).
constexpr float kGrassUV[4] = {0.0f, 0.0f, 0.5f, 0.5f};
constexpr float kDirtUV[4] = {0.5f, 0.0f, 1.0f, 0.5f};
constexpr float kPlayerUV[4] = {0.0f, 0.5f, 0.5f, 1.0f};
constexpr float kNpcUV[4] = {0.5f, 0.5f, 1.0f, 1.0f};

void AddCharacterVisuals(Registry& registry, Entity entity, const float* uv) {
    // ScreenTransform2D is derived from WorldTransform2D every frame; it only
    // needs to exist here.
    registry.emplace<ScreenTransform2D>(entity);
    registry.emplace<Sprite>(entity, 32.0f, 48.0f, uv[0], uv[1], uv[2], uv[3], 1.0f, 1.0f, 1.0f, 1.0f);
}

} // namespace

TileStyle StyleFor(TileKind kind) {
    // Walls reuse the grass texel, darkened, so the solid/walkable split is
    // visible at a glance.
    const float* uv = kind == TileKind::Dirt ? kDirtUV : kGrassUV;
    TileStyle style;
    style.u0 = uv[0];
    style.v0 = uv[1];
    style.u1 = uv[2];
    style.v1 = uv[3];
    style.width = kTileWidth;
    style.height = kTileHeight;
    style.tint = kind == TileKind::Wall ? 0.55f : 1.0f;
    style.solid = kind == TileKind::Wall;
    return style;
}

Entity CreateTile(Registry& registry, int gx, int gy, TileKind kind) {
    return CreateTile(registry, gx, gy, StyleFor(kind));
}

Entity CreateTile(Registry& registry, int gx, int gy, const TileStyle& style) {
    auto entity = registry.create();
    registry.emplace<Tile>(entity, gx, gy, style.solid);
    registry.emplace<ScreenTransform2D>(entity);
    auto& sprite = registry.emplace<Sprite>(entity, style.width, style.height, style.u0, style.v0, style.u1, style.v1,
                                            style.tint, style.tint, style.tint, 1.0f);
    sprite.texture = style.texture;
    sprite.flat = style.flat;
    sprite.rotation = style.rotation;
    return entity;
}

Entity CreateProp(Registry& registry, float worldX, float worldY, const std::string& texture, float width,
                  float height) {
    auto entity = registry.create();
    registry.emplace<WorldTransform2D>(entity, worldX, worldY);
    registry.emplace<ScreenTransform2D>(entity);
    auto& sprite = registry.emplace<Sprite>(entity, width, height, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    sprite.texture = texture;
    return entity;
}

Entity CreateNpc(Registry& registry, float worldX, float worldY, const std::string& scriptPath) {
    auto entity = registry.create();
    registry.emplace<Name>(entity, "NPC");
    registry.emplace<Faction>(entity, Team::Neutral);
    registry.emplace<WorldTransform2D>(entity, worldX, worldY);
    AddCharacterVisuals(registry, entity, kNpcUV);
    if (!scriptPath.empty()) {
        registry.emplace<LuaScript>(entity).path = scriptPath;
    }
    return entity;
}

Entity CreatePlayer(Registry& registry, float worldX, float worldY) {
    auto entity = registry.create();
    registry.emplace<Name>(entity, "Player");
    registry.emplace<WorldTransform2D>(entity, worldX, worldY);
    registry.emplace<Velocity2D>(entity);
    registry.emplace<Collider2D>(entity, 0.3f, 0.3f);
    registry.emplace<Faction>(entity, Team::Player);
    registry.emplace<Health>(entity, 20.0f, 20.0f);
    registry.emplace<Stats>(entity, 1.0f, 0.0f, 4.2f);
    registry.emplace<Hurtbox>(entity);
    registry.emplace<MeleeAttack>(entity, 3.0f, 0.7f, 0.5f, 0.35f, 0.5f);
    AddCharacterVisuals(registry, entity, kPlayerUV);
    return entity;
}

Entity CreateEnemy(Registry& registry, float worldX, float worldY, Entity target, const std::string& scriptPath) {
    auto entity = registry.create();
    registry.emplace<Name>(entity, "Enemy");
    registry.emplace<WorldTransform2D>(entity, worldX, worldY);
    registry.emplace<Collider2D>(entity, 0.3f, 0.3f);
    registry.emplace<Faction>(entity, Team::Enemy);
    registry.emplace<Health>(entity, 8.0f, 8.0f);
    registry.emplace<Stats>(entity, 0.0f, 0.0f, 2.0f);
    registry.emplace<Hurtbox>(entity);
    // Slower, weaker swings than the player so a 1-on-1 is winnable.
    registry.emplace<MeleeAttack>(entity, 2.0f, 0.7f, 0.45f, 1.2f, 0.3f);
    registry.emplace<Follow>(entity, target, 1.6f, 0.8f, 5.0f);
    AddCharacterVisuals(registry, entity, kNpcUV);
    // Tinted so enemies read differently from friendly NPCs at a glance.
    auto& sprite = registry.get<Sprite>(entity);
    sprite.r = 1.0f;
    sprite.g = 0.55f;
    sprite.b = 0.55f;
    if (!scriptPath.empty()) {
        registry.emplace<LuaScript>(entity).path = scriptPath;
    }
    return entity;
}

Entity CreateHealthPickup(Registry& registry, float worldX, float worldY, float amount) {
    auto entity = registry.create();
    registry.emplace<Name>(entity, "Health Potion");
    registry.emplace<WorldTransform2D>(entity, worldX, worldY);
    registry.emplace<Pickup>(entity, PickupKind::Heal, amount, 0.5f);
    registry.emplace<ScreenTransform2D>(entity);
    // Small, bright marker until real art exists: the player-marker texel,
    // tinted green.
    registry.emplace<Sprite>(entity, 16.0f, 16.0f, kPlayerUV[0], kPlayerUV[1], kPlayerUV[2], kPlayerUV[3], 0.3f,
                             1.0f, 0.4f, 1.0f);
    return entity;
}

Entity CreateGateway(Registry& registry, float worldX, float worldY, const std::string& scene,
                     const std::string& marker, const std::string& texture) {
    auto entity = registry.create();
    registry.emplace<Name>(entity, "Gateway");
    registry.emplace<WorldTransform2D>(entity, worldX, worldY);
    registry.emplace<Gateway>(entity, scene, marker);
    registry.emplace<ScreenTransform2D>(entity);
    auto& sprite = texture.empty()
                       ? registry.emplace<Sprite>(entity, 64.0f, 32.0f, kPlayerUV[0], kPlayerUV[1], kPlayerUV[2],
                                                  kPlayerUV[3], 0.3f, 0.6f, 1.0f, 0.8f)
                       : registry.emplace<Sprite>(entity, 64.0f, 32.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    sprite.texture = texture;
    sprite.flat = true;
    return entity;
}

} // namespace aige
