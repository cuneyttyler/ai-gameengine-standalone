#include "ecs/Registry.h"
#include "physics2d/Collision.h"
#include "physics2d/Components.h"
#include "platform/Input.h"
#include "platform/Window.h"
#include "renderer2d/Components.h"
#include "renderer2d/Renderer2D.h"
#include "rhi/VulkanRHI.h"
#include "scripting/Components.h"
#include "scripting/LuaVM.h"
#include "scripting/ScriptSystem.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#ifndef AIGE_SANDBOX_SOURCE_DIR
#define AIGE_SANDBOX_SOURCE_DIR "."
#endif

namespace {

constexpr int kGridSize = 8;
constexpr float kTileWidth = 64.0f;
constexpr float kTileHeight = 32.0f;

// UV sub-rects into Renderer2D's 2x2 procedural atlas (Renderer2D.cpp).
constexpr float kGrassUV[4] = {0.0f, 0.0f, 0.5f, 0.5f};
constexpr float kDirtUV[4] = {0.5f, 0.0f, 1.0f, 0.5f};
constexpr float kPlayerUV[4] = {0.0f, 0.5f, 0.5f, 1.0f};
constexpr float kNpcUV[4] = {0.5f, 0.5f, 1.0f, 1.0f};

struct IsoParams {
    float tileWidth;
    float tileHeight;
    float originX;
    float originY;
};

struct ScreenPose {
    float x;
    float y;
    float depth;
};

ScreenPose ProjectIso(float worldX, float worldY, const IsoParams& iso) {
    return {
        iso.originX + (worldX - worldY) * (iso.tileWidth * 0.5f),
        iso.originY + (worldX + worldY) * (iso.tileHeight * 0.5f),
        worldX + worldY,
    };
}

// Border ring + one small interior blob are solid; everything else in the
// grid is walkable. Same predicate drives both the physics query and the
// grey "this is a wall" tint below, so the two can't drift out of sync.
bool IsSolidCell(int gx, int gy) {
    if (gx < 0 || gy < 0 || gx >= kGridSize || gy >= kGridSize) {
        return true; // outside the map entirely
    }
    bool isBorder = (gx == 0 || gy == 0 || gx == kGridSize - 1 || gy == kGridSize - 1);
    if (isBorder) {
        return true;
    }
    if (gx == 2 && (gy == 2 || gy == 3)) {
        return true; // small interior test wall
    }
    return false;
}

// Collision cells (IsSolidCell) treat integer (gx, gy) as a cell's *corner*
// — cell (gx, gy) spans world [gx, gx+1) x [gy, gy+1). Everything placed in
// world space — tiles, NPCs, the player — is positioned at a cell's *center*
// (index + 0.5), so a spawn point never straddles a cell boundary/corner and
// clips into a neighboring solid cell before it's ever moved.
aige::Entity BuildScene(aige::Registry& registry, const IsoParams& iso) {
    for (int gridX = 0; gridX < kGridSize; ++gridX) {
        for (int gridY = 0; gridY < kGridSize; ++gridY) {
            auto entity = registry.create();
            ScreenPose pose = ProjectIso(static_cast<float>(gridX) + 0.5f, static_cast<float>(gridY) + 0.5f, iso);
            registry.emplace<aige::ScreenTransform2D>(entity, pose.x, pose.y, pose.depth);

            bool solid = IsSolidCell(gridX, gridY);
            bool isPath = !solid && (gridX == kGridSize / 2 || gridY == kGridSize / 2);
            const float* uv = isPath ? kDirtUV : kGrassUV;
            float tint = solid ? 0.55f : 1.0f;

            registry.emplace<aige::Sprite>(entity, kTileWidth, kTileHeight, uv[0], uv[1], uv[2], uv[3], tint, tint,
                                            tint, 1.0f);
        }
    }

    auto SpawnNpc = [&](float worldX, float worldY, const float* uv) {
        auto entity = registry.create();
        ScreenPose pose = ProjectIso(worldX, worldY, iso);
        registry.emplace<aige::ScreenTransform2D>(entity, pose.x, pose.y - kTileHeight * 0.5f, pose.depth + 0.5f);
        registry.emplace<aige::Sprite>(entity, 32.0f, 48.0f, uv[0], uv[1], uv[2], uv[3], 1.0f, 1.0f, 1.0f, 1.0f);
        return entity;
    };
    SpawnNpc(5.5f, 2.5f, kNpcUV);

    // Second NPC is Lua-driven instead of static — needs a WorldTransform2D
    // (so UpdateWorldToScreen projects it every frame like the player) plus
    // a LuaScript pointing at the source-tree file, so editing and saving it
    // while the app runs hot-reloads the behavior (milestone 0.5).
    auto scriptedNpc = SpawnNpc(5.5f, 5.5f, kNpcUV);
    registry.emplace<aige::WorldTransform2D>(scriptedNpc, 5.5f, 5.5f);
    registry.emplace<aige::LuaScript>(scriptedNpc).path = std::string(AIGE_SANDBOX_SOURCE_DIR) + "/scripts/npc_wander.lua";

    constexpr float kPlayerSpawnX = 3.5f;
    constexpr float kPlayerSpawnY = 3.5f;

    auto player = registry.create();
    registry.emplace<aige::WorldTransform2D>(player, kPlayerSpawnX, kPlayerSpawnY);
    registry.emplace<aige::Velocity2D>(player);
    registry.emplace<aige::Collider2D>(player, 0.3f, 0.3f);
    ScreenPose playerPose = ProjectIso(kPlayerSpawnX, kPlayerSpawnY, iso);
    registry.emplace<aige::ScreenTransform2D>(player, playerPose.x, playerPose.y - kTileHeight * 0.5f,
                                               playerPose.depth + 0.5f);
    registry.emplace<aige::Sprite>(player, 32.0f, 48.0f, kPlayerUV[0], kPlayerUV[1], kPlayerUV[2], kPlayerUV[3], 1.0f,
                                    1.0f, 1.0f, 1.0f);
    return player;
}

void UpdatePlayer(aige::Registry& registry, aige::Entity player, aige::Window& window, const IsoParams& iso,
                   float dt) {
    constexpr float kMoveSpeed = 3.0f; // grid cells per second

    // Input is screen-relative (W visibly moves up-screen), not world-axis-relative
    // — the standard control scheme for isometric ARPGs (our target genre). Since
    // the iso projection maps world-diagonal movement to screen-cardinal movement
    // (ProjectIso above), screen-relative input is world-diagonal: rotate the
    // (right, down) input pair 45° into world space rather than driving
    // WorldTransform2D's axes directly.
    float inputRight = 0.0f;
    float inputDown = 0.0f;
    if (window.IsKeyDown(aige::Key::D) || window.IsKeyDown(aige::Key::Right)) {
        inputRight += 1.0f;
    }
    if (window.IsKeyDown(aige::Key::A) || window.IsKeyDown(aige::Key::Left)) {
        inputRight -= 1.0f;
    }
    if (window.IsKeyDown(aige::Key::S) || window.IsKeyDown(aige::Key::Down)) {
        inputDown += 1.0f;
    }
    if (window.IsKeyDown(aige::Key::W) || window.IsKeyDown(aige::Key::Up)) {
        inputDown -= 1.0f;
    }
    if (inputRight != 0.0f && inputDown != 0.0f) {
        constexpr float kInvSqrt2 = 0.70710678f; // don't let a diagonal screen direction move faster
        inputRight *= kInvSqrt2;
        inputDown *= kInvSqrt2;
    }

    auto& velocity = registry.get<aige::Velocity2D>(player);
    velocity.x = (inputRight + inputDown) * kMoveSpeed;
    velocity.y = (inputDown - inputRight) * kMoveSpeed;

    auto& world = registry.get<aige::WorldTransform2D>(player);
    const auto& collider = registry.get<aige::Collider2D>(player);

    aige::Vec2 resolved =
        aige::ResolveAabbMovement({world.x, world.y}, {collider.halfWidth, collider.halfHeight},
                                   {velocity.x * dt, velocity.y * dt}, [](int gx, int gy) { return IsSolidCell(gx, gy); });
    world.x = resolved.x;
    world.y = resolved.y;
}

// Re-derives ScreenTransform2D from WorldTransform2D for every entity that has
// both, each fixed step — the one place "world space" meets "screen space"
// (technical.md §8's projection step), shared by anything that moves a
// WorldTransform2D: the player (input+collision) and Lua-scripted entities
// alike. Static content skips this entirely and keeps the ScreenTransform2D
// it was given at scene-build time.
void UpdateWorldToScreen(aige::Registry& registry, const IsoParams& iso) {
    auto view = registry.view<aige::WorldTransform2D, aige::ScreenTransform2D>();
    for (auto entity : view) {
        auto& world = view.get<aige::WorldTransform2D>(entity);
        auto& screen = view.get<aige::ScreenTransform2D>(entity);
        ScreenPose pose = ProjectIso(world.x, world.y, iso);
        screen.x = pose.x;
        screen.y = pose.y - kTileHeight * 0.5f;
        screen.depth = pose.depth + 0.5f;
    }
}

} // namespace

int main() {
    try {
        aige::Window::Desc desc;
        aige::Window window(desc);
        aige::VulkanRHI rhi(window);
        aige::Renderer2D renderer(rhi);

        IsoParams iso{kTileWidth, kTileHeight, static_cast<float>(desc.width) * 0.5f, 96.0f};

        // lua must outlive registry: LuaScript components store a
        // sol::environment referencing lua's Lua state, so registry (and
        // anything in it) has to be destroyed before lua closes that state.
        aige::LuaVM lua;
        aige::Registry registry;
        aige::Entity player = BuildScene(registry, iso);

        aige::ScriptBinder scriptBinder = [&registry](sol::environment& env, aige::Entity entity) {
            env["get_position"] = [&registry, entity]() {
                auto& world = registry.get<aige::WorldTransform2D>(entity);
                return std::make_tuple(world.x, world.y);
            };
            env["set_position"] = [&registry, entity](float x, float y) {
                auto& world = registry.get<aige::WorldTransform2D>(entity);
                world.x = x;
                world.y = y;
            };
        };

        using Clock = std::chrono::steady_clock;
        constexpr double kFixedDt = 1.0 / 60.0;
        auto previousTime = Clock::now();
        double accumulator = 0.0;

        while (!window.ShouldClose()) {
            window.PollEvents();
            if (window.IsKeyDown(aige::Key::Escape)) {
                break;
            }

            auto currentTime = Clock::now();
            double frameSeconds = std::chrono::duration<double>(currentTime - previousTime).count();
            previousTime = currentTime;
            frameSeconds = std::min(frameSeconds, 0.25); // avoid a "spiral of death" after a long stall

            accumulator += frameSeconds;
            while (accumulator >= kFixedDt) {
                UpdatePlayer(registry, player, window, iso, static_cast<float>(kFixedDt));
                aige::UpdateScripts(registry, lua, static_cast<float>(kFixedDt), scriptBinder);
                UpdateWorldToScreen(registry, iso);
                accumulator -= kFixedDt;
            }

            VkCommandBuffer cmd = rhi.BeginFrame();
            if (cmd != VK_NULL_HANDLE) {
                renderer.Render(cmd, registry);
                rhi.EndFrame();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
