#include "gameplay/GameSession.h"

#include "gameplay/Components.h"
#include "gameplay/TileGrid.h"
#include "scripting/Components.h"
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
    if (registry.all_of<Dead>(player)) {
        registry.get<Velocity2D>(player) = {};
        return;
    }
    const auto* stats = registry.try_get<Stats>(player);
    const float moveSpeed = (stats ? stats->moveSpeed : 4.2f) * SpeedMultiplier(registry, player); // cells/s

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
    // Rotated into world space, then normalized so every direction moves at
    // exactly moveSpeed cells/s (unnormalized, screen-cardinal input would
    // move √2 faster in world space than intended).
    float dirX = inputRight + inputDown;
    float dirY = inputDown - inputRight;
    float length = std::sqrt(dirX * dirX + dirY * dirY);
    auto& velocity = registry.get<Velocity2D>(player);
    if (length > 1e-4f) {
        dirX /= length;
        dirY /= length;
        velocity.x = dirX * moveSpeed;
        velocity.y = dirY * moveSpeed;
        registry.emplace_or_replace<Facing>(player, dirX, dirY);
    } else {
        velocity = {};
    }

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
        if (target == nullptr || registry.all_of<Dead>(follow.target) || registry.all_of<Dead>(entity)) {
            continue;
        }
        auto& world = view.get<WorldTransform2D>(entity);
        float dx = target->x - world.x;
        float dy = target->y - world.y;
        float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= follow.stopDistance || distance < 1e-4f || (follow.range > 0.0f && distance > follow.range)) {
            continue;
        }
        registry.emplace_or_replace<Facing>(entity, dx / distance, dy / distance);
        float speed = follow.speed * SpeedMultiplier(registry, entity);
        float step = std::min(speed * dt, distance - follow.stopDistance);
        Vec2 half{0.3f, 0.3f};
        if (const auto* collider = registry.try_get<Collider2D>(entity)) {
            half = {collider->halfWidth, collider->halfHeight};
        }
        Vec2 resolved = ResolveAabbMovement({world.x, world.y}, half, {dx / distance * step, dy / distance * step}, isSolid);
        world.x = resolved.x;
        world.y = resolved.y;
    }
}

void UpdateSpriteFacing(Registry& registry) {
    for (auto [entity, facing, sprite] : registry.view<Facing, Sprite>().each()) {
        // Screen-horizontal component of the world-space facing direction.
        float screenDx = facing.x - facing.y;
        if (std::abs(screenDx) < 0.2f) {
            continue; // moving straight up/down screen: keep the current side
        }
        bool wantFlipped = screenDx < 0.0f; // art faces right
        if (wantFlipped != (sprite.u0 > sprite.u1)) {
            std::swap(sprite.u0, sprite.u1);
        }
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

namespace {

std::optional<Team> ParseTeam(const std::string& name) {
    if (name == "Player" || name == "player") return Team::Player;
    if (name == "Enemy" || name == "enemy") return Team::Enemy;
    if (name == "Neutral" || name == "neutral") return Team::Neutral;
    return std::nullopt;
}

std::optional<EffectKind> ParseEffect(const std::string& name) {
    if (name == "burn") return EffectKind::Burn;
    if (name == "poison") return EffectKind::Poison;
    if (name == "slow") return EffectKind::Slow;
    if (name == "regen") return EffectKind::Regen;
    return std::nullopt;
}

// Scripts refer to entities by their integer id.
Entity ToEntity(lua_Integer id) {
    return static_cast<Entity>(static_cast<uint32_t>(id));
}

lua_Integer ToId(Entity entity) {
    return static_cast<lua_Integer>(static_cast<uint32_t>(entity));
}

} // namespace

GameSession::GameSession(Registry& registry, LuaVM& lua, IsoParams iso, std::filesystem::path scriptRoot)
    : registry_(registry), lua_(lua), iso_(iso), scriptRoot_(std::move(scriptRoot)) {
    binder_ = [this](sol::environment& env, Entity self) { BindScriptApi(env, self); };
}

void GameSession::ResetClock() {
    accumulator_ = 0.0;
    attackKeyWasDown_ = false;
    requestedAttack_.reset();
    events_.clear();
}

bool GameSession::Swing(Entity attacker, float dirX, float dirY) {
    if (!TryMeleeAttack(registry_, attacker, dirX, dirY)) {
        return false;
    }
    const auto* sounds = registry_.try_get<Sounds>(attacker);
    events_.push_back({GameEvent::Type::Attack, attacker, entt::null, 0.0f, sounds ? sounds->attack : std::string()});
    return true;
}

void GameSession::RequestPlayerAttack(float worldX, float worldY) {
    requestedAttack_ = std::make_pair(worldX, worldY);
}

void GameSession::BindScriptApi(sol::environment& env, Entity self) {
    Registry& registry = registry_;
    lua_State* L = env.lua_state();

    env["self_id"] = [self]() { return ToId(self); };

    env["get_position"] = [&registry, self]() {
        const auto* world = registry.try_get<WorldTransform2D>(self);
        return world ? std::make_tuple(world->x, world->y) : std::make_tuple(0.0f, 0.0f);
    };
    env["set_position"] = [&registry, self](float x, float y) {
        if (auto* world = registry.try_get<WorldTransform2D>(self)) {
            world->x = x;
            world->y = y;
        }
    };

    // Walks toward a point at `speed` cells/s for this frame, with collision.
    env["move_toward"] = [this, &registry, self](float x, float y, float speed) {
        auto* world = registry.try_get<WorldTransform2D>(self);
        if (world == nullptr || registry.all_of<Dead>(self)) {
            return;
        }
        float dx = x - world->x;
        float dy = y - world->y;
        float distance = std::sqrt(dx * dx + dy * dy);
        if (distance < 1e-4f) {
            return;
        }
        float step = std::min(speed * SpeedMultiplier(registry, self) * static_cast<float>(kFixedDt), distance);
        Vec2 half{0.3f, 0.3f};
        if (const auto* collider = registry.try_get<Collider2D>(self)) {
            half = {collider->halfWidth, collider->halfHeight};
        }
        Vec2 moved = ResolveAabbMovement({world->x, world->y}, half, {dx / distance * step, dy / distance * step},
                                         [this](int gx, int gy) { return grid_.IsSolid(gx, gy); });
        world->x = moved.x;
        world->y = moved.y;
        registry.emplace_or_replace<Facing>(self, dx / distance, dy / distance);
    };

    // id, distance of the closest living entity of a team ("Player",
    // "Enemy", "Neutral"), or nil.
    env["find_nearest"] = [&registry, self, L](const std::string& teamName) -> std::tuple<sol::object, sol::object> {
        auto team = ParseTeam(teamName);
        const auto* from = registry.try_get<WorldTransform2D>(self);
        if (!team || from == nullptr) {
            return {sol::lua_nil, sol::lua_nil};
        }
        std::optional<Entity> best;
        float bestDistance = 0.0f;
        for (auto [entity, faction, world] : registry.view<Faction, WorldTransform2D>(entt::exclude<Dead>).each()) {
            if (entity == self || faction.team != *team) {
                continue;
            }
            float distance = std::hypot(world.x - from->x, world.y - from->y);
            if (!best || distance < bestDistance) {
                best = entity;
                bestDistance = distance;
            }
        }
        if (!best) {
            return {sol::lua_nil, sol::lua_nil};
        }
        return {sol::make_object(L, ToId(*best)), sol::make_object(L, bestDistance)};
    };

    env["position_of"] = [&registry, L](lua_Integer id) -> std::tuple<sol::object, sol::object> {
        Entity entity = ToEntity(id);
        const auto* world = registry.valid(entity) ? registry.try_get<WorldTransform2D>(entity) : nullptr;
        if (world == nullptr) {
            return {sol::lua_nil, sol::lua_nil};
        }
        return {sol::make_object(L, world->x), sol::make_object(L, world->y)};
    };

    env["distance_to"] = [&registry, self, L](lua_Integer id) -> sol::object {
        Entity entity = ToEntity(id);
        const auto* from = registry.try_get<WorldTransform2D>(self);
        const auto* to = registry.valid(entity) ? registry.try_get<WorldTransform2D>(entity) : nullptr;
        if (from == nullptr || to == nullptr) {
            return sol::lua_nil;
        }
        return sol::make_object(L, std::hypot(to->x - from->x, to->y - from->y));
    };

    env["is_alive"] = [&registry](lua_Integer id) {
        Entity entity = ToEntity(id);
        return registry.valid(entity) && !registry.all_of<Dead>(entity);
    };

    // current, max of this entity (or another, by id).
    env["get_health"] = [&registry, self, L](sol::optional<lua_Integer> id) -> std::tuple<sol::object, sol::object> {
        Entity entity = id ? ToEntity(*id) : self;
        const auto* health = registry.valid(entity) ? registry.try_get<Health>(entity) : nullptr;
        if (health == nullptr) {
            return {sol::lua_nil, sol::lua_nil};
        }
        return {sol::make_object(L, health->current), sol::make_object(L, health->max)};
    };

    // Swings this entity's MeleeAttack toward another entity (by id) or a
    // point (x, y). Returns true if the swing happened (not on cooldown).
    env["attack_toward"] = [this, &registry, self](sol::object idOrX, sol::optional<float> y) {
        const auto* from = registry.try_get<WorldTransform2D>(self);
        if (from == nullptr || !idOrX.is<double>()) {
            return false;
        }
        float tx = 0.0f;
        float ty = 0.0f;
        if (y) {
            tx = static_cast<float>(idOrX.as<double>());
            ty = *y;
        } else {
            Entity target = ToEntity(idOrX.as<lua_Integer>());
            const auto* to = registry.valid(target) ? registry.try_get<WorldTransform2D>(target) : nullptr;
            if (to == nullptr) {
                return false;
            }
            tx = to->x;
            ty = to->y;
        }
        return Swing(self, tx - from->x, ty - from->y);
    };

    // apply_status(id, "burn" | "poison" | "slow" | "regen", seconds, magnitude)
    env["apply_status"] = [&registry](lua_Integer id, const std::string& kind, float duration, float magnitude) {
        Entity target = ToEntity(id);
        auto effect = ParseEffect(kind);
        if (registry.valid(target) && effect) {
            ApplyStatus(registry, target, *effect, duration, magnitude);
        }
    };

    env["play_sound"] = [this](const std::string& path) {
        if (playSound_) {
            playSound_(path);
        }
    };
}

int GameSession::Advance(double frameSeconds, const KeyQueryFn& isKeyDown) {
    events_.clear();
    accumulator_ += std::min(frameSeconds, 0.25);
    if (accumulator_ < kFixedDt) {
        return 0;
    }

    grid_ = TileGrid::Build(registry_);
    auto isSolid = [this](int gx, int gy) { return grid_.IsSolid(gx, gy); };

    int steps = 0;
    size_t soundsPlayed = 0;
    while (accumulator_ >= kFixedDt) {
        const float dt = static_cast<float>(kFixedDt);
        if (auto player = FindPlayer(registry_)) {
            UpdatePlayer(registry_, *player, isKeyDown, isSolid, dt);

            // Attacks: Space swings toward the facing direction (on press,
            // not while held); a click swings toward the clicked point.
            bool attackKeyDown = isKeyDown(Key::Space);
            if (attackKeyDown && !attackKeyWasDown_) {
                Swing(*player, 0.0f, 0.0f);
            }
            attackKeyWasDown_ = attackKeyDown;
            if (requestedAttack_) {
                const auto& world = registry_.get<WorldTransform2D>(*player);
                Swing(*player, requestedAttack_->first - world.x,
                               requestedAttack_->second - world.y);
                requestedAttack_.reset();
            }
        }
        UpdateFollowers(registry_, isSolid, dt);
        UpdateScripts(registry_, lua_, dt, binder_, scriptRoot_);
        UpdateCombat(registry_, isSolid, dt, events_);
        UpdateSpriteFacing(registry_);
        UpdateWorldToScreen(registry_, iso_);

        for (; soundsPlayed < events_.size(); ++soundsPlayed) {
            if (playSound_ && !events_[soundsPlayed].sound.empty()) {
                playSound_(events_[soundsPlayed].sound);
            }
        }
        accumulator_ -= kFixedDt;
        ++steps;
    }
    return steps;
}

} // namespace aige
