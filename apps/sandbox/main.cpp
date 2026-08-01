#include "ecs/Registry.h"
#include "physics2d/Collision.h"
#include "physics2d/Components.h"
#include "platform/Input.h"
#include "platform/Window.h"
#include "renderer2d/Components.h"
#include "renderer2d/Renderer2D.h"
#include "rhi/VulkanRHI.h"

#include <algorithm>
#include <chrono>
#include <iostream>

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
    };
    SpawnNpc(5.5f, 2.5f, kNpcUV);
    SpawnNpc(5.5f, 5.5f, kNpcUV);

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

    ScreenPose pose = ProjectIso(world.x, world.y, iso);
    auto& screen = registry.get<aige::ScreenTransform2D>(player);
    screen.x = pose.x;
    screen.y = pose.y - kTileHeight * 0.5f;
    screen.depth = pose.depth + 0.5f;
}

} // namespace

int main() {
    try {
        aige::Window::Desc desc;
        aige::Window window(desc);
        aige::VulkanRHI rhi(window);
        aige::Renderer2D renderer(rhi);

        IsoParams iso{kTileWidth, kTileHeight, static_cast<float>(desc.width) * 0.5f, 96.0f};

        aige::Registry registry;
        aige::Entity player = BuildScene(registry, iso);

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
