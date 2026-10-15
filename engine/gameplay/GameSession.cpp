#include "gameplay/GameSession.h"

#include "gameplay/Components.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cmath>
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

void UpdateFollowers(Registry& registry, const SolidQueryFn& isSolid, float dt) {
    auto view = registry.view<Follow, WorldTransform2D>();
    for (auto entity : view) {
        const auto& follow = view.get<Follow>(entity);
        if (follow.target == entity || !registry.valid(follow.target)) {
            continue;
        }
        const auto* target = registry.try_get<WorldTransform2D>(follow.target);
        if (target == nullptr) {
            continue;
        }
        auto& world = view.get<WorldTransform2D>(entity);
        float dx = target->x - world.x;
        float dy = target->y - world.y;
        float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= follow.stopDistance || distance < 1e-4f || (follow.range > 0.0f && distance > follow.range)) {
            continue;
        }
        float step = std::min(follow.speed * dt, distance - follow.stopDistance);
        Vec2 half{0.3f, 0.3f};
        if (const auto* collider = registry.try_get<Collider2D>(entity)) {
            half = {collider->halfWidth, collider->halfHeight};
        }
        Vec2 resolved = ResolveAabbMovement({world.x, world.y}, half, {dx / distance * step, dy / distance * step}, isSolid);
        world.x = resolved.x;
        world.y = resolved.y;
    }
}

void UpdateWorldToScreen(Registry& registry, const IsoParams& iso) {
    auto view = registry.view<WorldTransform2D, ScreenTransform2D>();
    for (auto entity : view) {
        auto& world = view.get<WorldTransform2D>(entity);
        auto& screen = view.get<ScreenTransform2D>(entity);
        ScreenPose pose = ProjectIso(world.x, world.y, iso);
        // Characters and props stand on the ground point rather than being
        // centered on it: the sprite's bottom edge sits a quarter tile below
        // the point (inside the diamond), whatever the sprite's height. With
        // no sprite, fall back to a half-tile lift. Drawn just in front of
        // the tile they stand on.
        const auto* sprite = registry.try_get<Sprite>(entity);
        float lift = sprite ? sprite->height * 0.5f - iso.tileHeight * 0.25f : iso.tileHeight * 0.5f;
        screen.x = pose.x;
        screen.y = pose.y - lift;
        screen.depth = pose.depth + 0.5f;
    }

    auto tiles = registry.view<Tile, ScreenTransform2D>();
    for (auto entity : tiles) {
        const auto& tile = tiles.get<Tile>(entity);
        auto& screen = tiles.get<ScreenTransform2D>(entity);
        ScreenPose pose = ProjectIso(static_cast<float>(tile.gx) + 0.5f, static_cast<float>(tile.gy) + 0.5f, iso);
        // A tile sprite taller than the diamond (a wall block, a tree on a
        // tile) extends upward: its bottom diamond stays on the cell.
        const auto* sprite = registry.try_get<Sprite>(entity);
        float extra = sprite ? std::max(0.0f, sprite->height - iso.tileHeight) : 0.0f;
        screen.x = pose.x;
        screen.y = pose.y - extra * 0.5f;
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

GameSession::GameSession(Registry& registry, LuaVM& lua, IsoParams iso, std::filesystem::path scriptRoot)
    : registry_(registry),
      lua_(lua),
      iso_(iso),
      scriptRoot_(std::move(scriptRoot)),
      binder_(MakeWorldTransformBinder(registry)) {}

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
        UpdateFollowers(registry_, isSolid, dt);
        UpdateScripts(registry_, lua_, dt, binder_, scriptRoot_);
        UpdateWorldToScreen(registry_, iso_);
        accumulator_ -= kFixedDt;
        ++steps;
    }
    return steps;
}

} // namespace aige
