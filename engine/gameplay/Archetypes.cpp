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
    AddCharacterVisuals(registry, entity, kPlayerUV);
    return entity;
}

} // namespace aige
