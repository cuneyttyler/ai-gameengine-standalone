#include "gameplay/BootstrapLevel.h"

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

} // namespace

IsoParams BootstrapIso() {
    return {kTileWidth, kTileHeight, 640.0f, 96.0f};
}

bool IsBootstrapSolidCell(int gx, int gy) {
    if (gx < 0 || gy < 0 || gx >= kBootstrapGridSize || gy >= kBootstrapGridSize) {
        return true; // outside the map entirely
    }
    bool isBorder = (gx == 0 || gy == 0 || gx == kBootstrapGridSize - 1 || gy == kBootstrapGridSize - 1);
    if (isBorder) {
        return true;
    }
    if (gx == 2 && (gy == 2 || gy == 3)) {
        return true; // small interior test wall
    }
    return false;
}

// Collision cells treat integer (gx, gy) as a cell's *corner* — cell (gx, gy)
// spans world [gx, gx+1) x [gy, gy+1). Everything placed in world space is
// positioned at a cell's *center* (index + 0.5), so a spawn point never
// straddles a cell boundary and clips into a neighboring solid cell.
Entity BuildBootstrapScene(Registry& registry, const std::string& npcScriptPath) {
    const IsoParams iso = BootstrapIso();

    for (int gridX = 0; gridX < kBootstrapGridSize; ++gridX) {
        for (int gridY = 0; gridY < kBootstrapGridSize; ++gridY) {
            auto entity = registry.create();
            ScreenPose pose = ProjectIso(static_cast<float>(gridX) + 0.5f, static_cast<float>(gridY) + 0.5f, iso);
            registry.emplace<ScreenTransform2D>(entity, pose.x, pose.y, pose.depth);

            bool solid = IsBootstrapSolidCell(gridX, gridY);
            bool isPath = !solid && (gridX == kBootstrapGridSize / 2 || gridY == kBootstrapGridSize / 2);
            const float* uv = isPath ? kDirtUV : kGrassUV;
            float tint = solid ? 0.55f : 1.0f; // same predicate as collision, so visuals can't drift from it

            registry.emplace<Sprite>(entity, kTileWidth, kTileHeight, uv[0], uv[1], uv[2], uv[3], tint, tint, tint,
                                      1.0f);
        }
    }

    auto SpawnNpc = [&](float worldX, float worldY, const float* uv) {
        auto entity = registry.create();
        ScreenPose pose = ProjectIso(worldX, worldY, iso);
        registry.emplace<ScreenTransform2D>(entity, pose.x, pose.y - kTileHeight * 0.5f, pose.depth + 0.5f);
        registry.emplace<Sprite>(entity, 32.0f, 48.0f, uv[0], uv[1], uv[2], uv[3], 1.0f, 1.0f, 1.0f, 1.0f);
        return entity;
    };
    SpawnNpc(5.5f, 2.5f, kNpcUV);

    // Second NPC is Lua-driven: a WorldTransform2D so UpdateWorldToScreen
    // projects it every step, plus a hot-reloadable LuaScript.
    auto scriptedNpc = SpawnNpc(5.5f, 5.5f, kNpcUV);
    registry.emplace<WorldTransform2D>(scriptedNpc, 5.5f, 5.5f);
    registry.emplace<LuaScript>(scriptedNpc).path = npcScriptPath;

    constexpr float kPlayerSpawnX = 3.5f;
    constexpr float kPlayerSpawnY = 3.5f;

    auto player = registry.create();
    registry.emplace<WorldTransform2D>(player, kPlayerSpawnX, kPlayerSpawnY);
    registry.emplace<Velocity2D>(player);
    registry.emplace<Collider2D>(player, 0.3f, 0.3f);
    ScreenPose playerPose = ProjectIso(kPlayerSpawnX, kPlayerSpawnY, iso);
    registry.emplace<ScreenTransform2D>(player, playerPose.x, playerPose.y - kTileHeight * 0.5f,
                                         playerPose.depth + 0.5f);
    registry.emplace<Sprite>(player, 32.0f, 48.0f, kPlayerUV[0], kPlayerUV[1], kPlayerUV[2], kPlayerUV[3], 1.0f, 1.0f,
                              1.0f, 1.0f);
    return player;
}

} // namespace aige
