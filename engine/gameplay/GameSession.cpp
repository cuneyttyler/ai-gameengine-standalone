#include "gameplay/GameSession.h"

#include "gameplay/Components.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <tuple>

namespace aige {

std::optional<Entity> FindPlayer(Registry& registry) {
    auto view = registry.view<Velocity2D, Collider2D>();
    if (view.begin() == view.end()) {
        return std::nullopt;
    }
    return *view.begin();
}

void UpdatePlayer(Registry& registry, Entity player, const KeyQueryFn& isKeyDown, const SolidQueryFn& isSolid,
                  float dt) {
    constexpr float kMoveSpeed = 3.0f; // grid cells per second

    // Screen-relative input is world-diagonal (ProjectIso maps world
    // diagonals to screen cardinals), so rotate the (right, down) input pair
    // 45° into world space rather than driving WorldTransform2D's axes directly.
    float inputRight = 0.0f;
    float inputDown = 0.0f;
    if (isKeyDown(Key::D) || isKeyDown(Key::Right)) {
        inputRight += 1.0f;
    }
    if (isKeyDown(Key::A) || isKeyDown(Key::Left)) {
        inputRight -= 1.0f;
    }
    if (isKeyDown(Key::S) || isKeyDown(Key::Down)) {
        inputDown += 1.0f;
    }
    if (isKeyDown(Key::W) || isKeyDown(Key::Up)) {
        inputDown -= 1.0f;
    }
    if (inputRight != 0.0f && inputDown != 0.0f) {
        constexpr float kInvSqrt2 = 0.70710678f; // don't let a diagonal screen direction move faster
        inputRight *= kInvSqrt2;
        inputDown *= kInvSqrt2;
    }

    auto& velocity = registry.get<Velocity2D>(player);
    velocity.x = (inputRight + inputDown) * kMoveSpeed;
    velocity.y = (inputDown - inputRight) * kMoveSpeed;

    auto& world = registry.get<WorldTransform2D>(player);
    const auto& collider = registry.get<Collider2D>(player);

    Vec2 resolved = ResolveAabbMovement({world.x, world.y}, {collider.halfWidth, collider.halfHeight},
                                         {velocity.x * dt, velocity.y * dt}, isSolid);
    world.x = resolved.x;
    world.y = resolved.y;
}

void UpdateWorldToScreen(Registry& registry, const IsoParams& iso) {
    auto view = registry.view<WorldTransform2D, ScreenTransform2D>();
    for (auto entity : view) {
        auto& world = view.get<WorldTransform2D>(entity);
        auto& screen = view.get<ScreenTransform2D>(entity);
        ScreenPose pose = ProjectIso(world.x, world.y, iso);
        // Characters stand on a tile rather than being centered in it:
        // lift by half a tile and draw just in front of the tile they're on.
        screen.x = pose.x;
        screen.y = pose.y - iso.tileHeight * 0.5f;
        screen.depth = pose.depth + 0.5f;
    }

    auto tiles = registry.view<Tile, ScreenTransform2D>();
    for (auto entity : tiles) {
        const auto& tile = tiles.get<Tile>(entity);
        auto& screen = tiles.get<ScreenTransform2D>(entity);
        ScreenPose pose = ProjectIso(static_cast<float>(tile.gx) + 0.5f, static_cast<float>(tile.gy) + 0.5f, iso);
        screen.x = pose.x;
        screen.y = pose.y;
        screen.depth = pose.depth;
    }
}

ScriptBinder MakeWorldTransformBinder(Registry& registry) {
    return [&registry](sol::environment& env, Entity entity) {
        env["get_position"] = [&registry, entity]() {
            auto& world = registry.get<WorldTransform2D>(entity);
            return std::make_tuple(world.x, world.y);
        };
        env["set_position"] = [&registry, entity](float x, float y) {
            auto& world = registry.get<WorldTransform2D>(entity);
            world.x = x;
            world.y = y;
        };
    };
}

GameSession::GameSession(Registry& registry, LuaVM& lua, IsoParams iso)
    : registry_(registry), lua_(lua), iso_(iso), binder_(MakeWorldTransformBinder(registry)) {}

int GameSession::Advance(double frameSeconds, const KeyQueryFn& isKeyDown) {
    accumulator_ += std::min(frameSeconds, 0.25);
    if (accumulator_ < kFixedDt) {
        return 0;
    }

    TileGrid grid = TileGrid::Build(registry_);
    auto isSolid = [&grid](int gx, int gy) { return grid.IsSolid(gx, gy); };

    int steps = 0;
    while (accumulator_ >= kFixedDt) {
        const float dt = static_cast<float>(kFixedDt);
        if (auto player = FindPlayer(registry_)) {
            UpdatePlayer(registry_, *player, isKeyDown, isSolid, dt);
        }
        UpdateScripts(registry_, lua_, dt, binder_);
        UpdateWorldToScreen(registry_, iso_);
        accumulator_ -= kFixedDt;
        ++steps;
    }
    return steps;
}

} // namespace aige
