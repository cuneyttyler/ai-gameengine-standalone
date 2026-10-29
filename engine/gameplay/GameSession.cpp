#include "gameplay/GameSession.h"
#include "gameplay/Profiler.h"
#include "gameplay/Streaming.h"

#include "gameplay/EngineReflection.h"
#include "scripting/LuaVM.h"
#include "gameplay/Components.h"
#include "gameplay/Attributes.h"
#include "gameplay/Encounters.h"
#include "gameplay/Interact.h"
#include "gameplay/Interiors.h"
#include "gameplay/Items.h"
#include "gameplay/TileLayer.h"
#include "gameplay/Localization.h"
#include "gameplay/Settings.h"
#include "gameplay/TileGrid.h"
#include "gameplay/Schedule.h"
#include "scripting/Components.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "modules/Modules.h"
#include "records/Records.h"
#include "ui/GameUi.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <type_traits>

namespace aige {

// A JSON value as Lua: objects become tables by key, arrays tables from 1.
sol::object JsonToLua(lua_State* L, const nlohmann::json& value) {
    switch (value.type()) {
        case nlohmann::json::value_t::boolean: return sol::make_object(L, value.get<bool>());
        case nlohmann::json::value_t::number_integer:
        case nlohmann::json::value_t::number_unsigned:
            return sol::make_object(L, static_cast<lua_Integer>(value.get<int64_t>()));
        case nlohmann::json::value_t::number_float: return sol::make_object(L, value.get<double>());
        case nlohmann::json::value_t::string: return sol::make_object(L, value.get<std::string>());
        case nlohmann::json::value_t::array: {
            sol::table table = sol::state_view(L).create_table();
            int i = 1;
            for (const auto& item : value) table[i++] = JsonToLua(L, item);
            return table;
        }
        case nlohmann::json::value_t::object: {
            sol::table table = sol::state_view(L).create_table();
            for (const auto& [key, item] : value.items()) table[key] = JsonToLua(L, item);
            return table;
        }
        default: return sol::lua_nil;
    }
}

std::optional<Entity> FindPlayer(Registry& registry) {
    auto view = registry.view<Velocity2D, Collider2D>();
    if (view.begin() == view.end()) {
        return std::nullopt;
    }
    return *view.begin();
}

void UpdatePlayer(Registry& registry, Entity player, const KeyQueryFn& isKeyDown, const SolidQueryFn& isSolid,
                  float dt) {
    float inputRight = 0.0f;
    float inputDown = 0.0f;
    if (isKeyDown(Key::D) || isKeyDown(Key::Right)) inputRight += 1.0f;
    if (isKeyDown(Key::A) || isKeyDown(Key::Left)) inputRight -= 1.0f;
    if (isKeyDown(Key::S) || isKeyDown(Key::Down)) inputDown += 1.0f;
    if (isKeyDown(Key::W) || isKeyDown(Key::Up)) inputDown -= 1.0f;
    UpdatePlayerMove(registry, player, inputRight, inputDown, isSolid, dt);
}

void UpdatePlayerMove(Registry& registry, Entity player, float inputRight, float inputDown, const SolidQueryFn& isSolid,
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
    // A gamepad stick pushed part way walks slower (its length, up to 1).
    const float strength = std::min(1.0f, std::sqrt(inputRight * inputRight + inputDown * inputDown));
    // Rotated into world space, then normalized so every direction moves at
    // exactly moveSpeed cells/s (unnormalized, screen-cardinal input would
    // move √2 faster in world space than intended).
    const bool topDown = ActiveSettings().grid.style == GridStyle::TopDown;
    // Top-down (6.1): the screen's axes are the world's.
    float dirX = topDown ? inputRight : inputRight + inputDown;
    float dirY = topDown ? inputDown : inputDown - inputRight;
    float length = std::sqrt(dirX * dirX + dirY * dirY);
    auto& velocity = registry.get<Velocity2D>(player);
    if (length > 1e-4f) {
        dirX /= length;
        dirY /= length;
        velocity.x = dirX * moveSpeed * strength;
        velocity.y = dirY * moveSpeed * strength;
        registry.emplace_or_replace<Facing>(player, dirX, dirY);
    } else {
        velocity = {};
    }

    auto& world = registry.get<WorldTransform2D>(player);
    const auto& collider = registry.get<Collider2D>(player);

    // Physics.playerCollides off (a debugging aid) walks through everything.
    static const SolidQueryFn kNothingSolid = [](int, int) { return false; };
    // High ground (place-refine.md §10): a cell much higher or lower than the one stood on is a cliff, unless either
    // has a Ramp (stairs, a ramp, a ladder).
    const int cx = static_cast<int>(std::floor(world.x)), cy = static_cast<int>(std::floor(world.y));
    const std::optional<float> here = GroundHeightAt(registry, cx + 0.5f, cy + 0.5f);
    SolidQueryFn withCliffs = isSolid;
    if (here) {
        const float step = ActiveSettings().grid.style == GridStyle::TopDown ? 12.0f : 14.0f; // pixels: a kerb walks, a wall doesn't
        auto rampAt = [&registry](int x, int y) {
            for (auto [e, ramp, at] : registry.view<Ramp, WorldTransform2D>().each()) {
                if (static_cast<int>(std::floor(at.x)) == x && static_cast<int>(std::floor(at.y)) == y) return true;
            }
            return false;
        };
        withCliffs = [&, h = *here](int x, int y) {
            if (isSolid(x, y)) return true;
            if (x == cx && y == cy) return false;
            const std::optional<float> there = GroundHeightAt(registry, x + 0.5f, y + 0.5f);
            return there && std::abs(*there - h) > step && !rampAt(x, y) && !rampAt(cx, cy);
        };
    }
    Vec2 resolved = ResolveAabbMovement({world.x, world.y}, {collider.halfWidth, collider.halfHeight},
                                         {velocity.x * dt, velocity.y * dt},
                                         ActiveSettings().physics.playerCollides ? withCliffs : kNothingSolid);
    world.x = resolved.x;
    world.y = resolved.y;
}

void UpdateFollowers(Registry& registry, const SolidQueryFn& isSolid, float dt) {
    const auto player = FindPlayer(registry);
    auto view = registry.view<Follow, WorldTransform2D>();
    for (auto entity : view) {
        const auto& follow = view.get<Follow>(entity);
        Entity targetEntity = follow.target == entt::null && player ? *player : follow.target;
        if (targetEntity == entity || !registry.valid(targetEntity)) {
            continue;
        }
        const auto* target = registry.try_get<WorldTransform2D>(targetEntity);
        if (target == nullptr || registry.all_of<Dead>(targetEntity) || registry.all_of<Dead>(entity)) {
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
        float screenDx = ActiveSettings().grid.style == GridStyle::TopDown ? facing.x : facing.x - facing.y;
        if (std::abs(screenDx) < 0.2f) {
            continue; // moving straight up/down screen: keep the current side
        }
        bool wantFlipped = screenDx < 0.0f; // art faces right
        if (wantFlipped != (sprite.u0 > sprite.u1)) {
            std::swap(sprite.u0, sprite.u1);
        }
    }
}

void UpdateGroundHeights(Registry& registry) {
    // Whatever moves stands at the land's height under its foot (place-refine.md §10), eased so a slope doesn't jitter.
    for (auto [entity, velocity, world, sprite] : registry.view<Velocity2D, WorldTransform2D, Sprite>(entt::exclude<Tile>).each()) {
        if (sprite.flat) continue;
        if (const std::optional<float> h = GroundHeightAt(registry, world.x, world.y)) sprite.lift += (*h - sprite.lift) * 0.5f;
    }
}

void UpdateWorldToScreen(Registry& registry, const IsoParams& iso) {
    UpdateGroundHeights(registry);
    // Tiles are placed by their cell below, so they're skipped here.
    auto view = registry.view<WorldTransform2D, ScreenTransform2D>(entt::exclude<Tile>);
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
        if (sprite && sprite->flat) {
            // Lies on the ground (a rug, a road piece, a shadow; 5.2): centered on the point, above the tiles, under
            // everything standing; among other flat things by its order first (a road over a grass patch), then by
            // where it is.
            screen.x = pose.x;
            screen.y = pose.y;
            screen.depth = kFlatLayerDepth + static_cast<float>(std::clamp(sprite->order, -9, 9)) * 5000.0f + pose.depth;
            continue;
        }
        float lift = sprite ? sprite->height * 0.5f - iso.tileHeight * 0.25f : iso.tileHeight * 0.5f;
        screen.x = pose.x;
        screen.y = pose.y - lift;
        screen.depth = pose.depth + 0.5f;
    }
    // A thing on several cells (a building) is drawn in front of or behind the player by the footprint point nearest
    // them, not by the one cell it stands on: in front of anyone behind any part of it, behind anyone in front of any
    // part of it, along its long sides too.
    if (const auto player = FindPlayer(registry); player && registry.all_of<WorldTransform2D>(*player)) {
        const auto& at = registry.get<WorldTransform2D>(*player);
        const float own = ProjectIso(at.x, at.y, iso).depth;
        for (auto [entity, obstacle, world, screen] : registry.view<Obstacle, WorldTransform2D, ScreenTransform2D>(entt::exclude<Tile>).each()) {
            if (obstacle.left <= 0 && obstacle.right <= 0 && obstacle.back <= 0) continue;
            if (const auto* sprite = registry.try_get<Sprite>(entity); !sprite || sprite->flat) continue;
            const float cx = std::floor(world.x), cy = std::floor(world.y);
            const float nx = std::clamp(at.x, cx - obstacle.left, cx + obstacle.right + 1.0f);
            const float ny = std::clamp(at.y, cy - obstacle.back, cy + 1.0f);
            const float nearest = ProjectIso(nx, ny, iso).depth;
            screen.depth = nearest + 0.5f + (own < nearest ? 0.01f : -0.01f);
        }
    }

    auto tiles = registry.view<Tile, ScreenTransform2D>();
    for (auto entity : tiles) {
        const auto& tile = tiles.get<Tile>(entity);
        auto& screen = tiles.get<ScreenTransform2D>(entity);
        ScreenPose pose = ProjectIso(static_cast<float>(tile.gx) + 0.5f, static_cast<float>(tile.gy) + 0.5f, iso);
        // A tile sprite taller than the diamond (a wall block, a tree on a
        // tile) extends upward: its bottom diamond stays on the cell.
        const auto* sprite = registry.try_get<Sprite>(entity);
        if (sprite && sprite->flat) {
            // Top-down ground (5.2): exactly on the cell's diamond, and drawn
            // before everything else, since nothing can be behind the ground.
            screen.x = pose.x;
            screen.y = pose.y;
            screen.depth = kGroundLayerDepth + pose.depth;
            continue;
        }
        float extra = sprite ? std::max(0.0f, sprite->height - iso.tileHeight) : 0.0f;
        screen.x = pose.x;
        screen.y = pose.y - extra * 0.5f;
        screen.depth = pose.depth;
    }
    PlaceAttachments(registry); // held and worn things, on their wearers' points
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
    WireStory();
    ReloadDifficulty();
    animations_ = AnimLibrary(scriptRoot_);
    animations_.Refresh();
    // Magic (19): its hooks reach the effects, spawns, events and lights here.
    magic_ = std::make_unique<MagicSystem>(registry_);
    achievements_.Reload();
    physics_ = std::make_unique<PhysicsWorld>();
    magic_->playCue = [this](const std::string& effect, float x, float y, Entity target, Entity source) {
        PlayEffect(effect, x, y, target, source);
    };
    magic_->spawn = [this](const std::string& prefab, float x, float y) -> std::optional<Entity> {
        if (!spawner_) return std::nullopt;
        difficultyDirty_ = true;
        return spawner_(prefab, x, y);
    };
    magic_->emit = [this](const std::string& name, Entity entity, Entity other, double amount, const std::string& text) {
        Emit({name, entity, other, amount, text});
    };
    magic_->light = [this](float x, float y, float radius, float r, float g, float b, float seconds) {
        tempLights_.push_back({x, y, radius, r, g, b, seconds, 0.0f});
    };
}

// --- Difficulty (development_plan.md 16) ---------------------------------------------

void GameSession::ReloadDifficulty() {
    difficultyModel_ = DifficultyModel::FromRecords(ActiveRecords());
    if (!difficultyLevel_.empty() && !difficultyModel_.Find(difficultyLevel_)) difficultyLevel_.clear();
    difficultyDirty_ = true;
}

void GameSession::SetDifficulty(const std::string& level) {
    if (!difficultyModel_.Find(level)) return;
    const bool changed = level != difficultyLevel_;
    difficultyLevel_ = level;
    ApplyDifficultyNow();
    if (changed) Emit({"difficulty_changed", entt::null, entt::null, 0.0, level});
}

void GameSession::SetAdaptive(bool on) {
    adaptiveOn_ = on;
    difficultyDirty_ = true;
}

void GameSession::ApplyDifficultyNow() {
    if (difficultyLevel_.empty()) difficultyLevel_ = difficultyModel_.DefaultLevel();
    const bool adaptive = adaptiveOn_ && ActiveSettings().difficulty.adaptive;
    ApplyDifficulty(registry_, EngineReflection(), difficultyModel_, difficultyLevel_, adaptive ? adaptive_.shift : 0);
    difficultyDirty_ = false;
}

float GameSession::XpScale() const {
    const DifficultyLevel* level = difficultyModel_.Find(difficultyLevel_);
    return level ? level->xp : 1.0f;
}

void GameSession::ResetClock() {
    accumulator_ = 0.0;
    input_.Reset();
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

void GameSession::RequestUseAt(float sceneX, float sceneY) {
    if (ActiveSettings().game.doorDoubleClick) requestedUse_ = std::make_pair(sceneX, sceneY);
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
            const float dx = world.x - from->x, dy = world.y - from->y;
            const float distance = dx * dx + dy * dy; // squared until the end
            if (!best || distance < bestDistance) {
                best = entity;
                bestDistance = distance;
            }
        }
        if (!best) {
            return {sol::lua_nil, sol::lua_nil};
        }
        bestDistance = std::sqrt(bestDistance);
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

    // get_field(id, "Component", "field") -> value or nil. Any field the
    // Inspector shows; enums come back as their option name, entity
    // references as ids.
    env["get_field"] = [&registry, L](lua_Integer id, const std::string& componentName,
                                      const std::string& fieldName) -> sol::object {
        Entity entity = ToEntity(id);
        const ComponentInfo* component = FindComponent(componentName);
        const FieldInfo* field = component ? component->Field(fieldName) : nullptr;
        if (field == nullptr || !registry.valid(entity) || !component->has(registry, entity)) {
            return sol::lua_nil;
        }
        FieldValue value = field->get(registry, entity);
        if (field->type == FieldType::Enum) {
            int index = std::get<int>(value);
            return index >= 0 && index < static_cast<int>(field->options.size())
                       ? sol::make_object(L, field->options[index])
                       : sol::make_object(L, index);
        }
        if (const auto* target = std::get_if<Entity>(&value)) {
            return registry.valid(*target) ? sol::make_object(L, ToId(*target)) : sol::lua_nil;
        }
        return std::visit([L](const auto& v) -> sol::object {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, Entity>) {
                return sol::lua_nil;
            } else {
                return sol::make_object(L, v);
            }
        }, value);
    };

    // set_field(id, "Component", "field", value) -> true if set.
    env["set_field"] = [&registry](lua_Integer id, const std::string& componentName, const std::string& fieldName,
                                   sol::object value) {
        Entity entity = ToEntity(id);
        const ComponentInfo* component = FindComponent(componentName);
        const FieldInfo* field = component ? component->Field(fieldName) : nullptr;
        if (field == nullptr || !registry.valid(entity) || !component->has(registry, entity)) {
            return false;
        }
        switch (field->type) {
            case FieldType::Float:
                if (!value.is<double>()) return false;
                field->set(registry, entity, static_cast<float>(value.as<double>()));
                return true;
            case FieldType::Int:
                if (!value.is<double>()) return false;
                field->set(registry, entity, static_cast<int>(value.as<double>()));
                return true;
            case FieldType::Bool:
                if (!value.is<bool>()) return false;
                field->set(registry, entity, value.as<bool>());
                return true;
            case FieldType::Enum: {
                if (value.is<std::string>()) {
                    auto it = std::find(field->options.begin(), field->options.end(), value.as<std::string>());
                    if (it == field->options.end()) return false;
                    field->set(registry, entity, static_cast<int>(it - field->options.begin()));
                    return true;
                }
                if (!value.is<double>()) return false;
                field->set(registry, entity, static_cast<int>(value.as<double>()));
                return true;
            }
            case FieldType::EntityRef: {
                Entity target = value.is<double>() ? ToEntity(value.as<lua_Integer>()) : Entity{entt::null};
                if (target != entt::null && !registry.valid(target)) return false;
                field->set(registry, entity, target);
                return true;
            }
            case FieldType::String:
            case FieldType::Texture:
            case FieldType::Script:
            case FieldType::Audio:
                if (!value.is<std::string>()) return false;
                field->set(registry, entity, value.as<std::string>());
                return true;
        }
        return false;
    };

    // spawn("prefabs/skeleton.prefab", x, y) -> id or nil.
    env["spawn"] = [this](const std::string& prefab, float x, float y) -> sol::object {
        if (!spawner_) {
            return sol::lua_nil;
        }
        auto entity = spawner_(prefab, x, y);
        difficultyDirty_ = true; // scaled before the next step
        return entity ? sol::make_object(lua_.State(), ToId(*entity)) : sol::lua_nil;
    };

    // become(id, "prefabs/chest-open.prefab") -> the new id, or nil: replaced in its place by the prefab, turned as it
    // was, what's left in it kept (an Interactable's becomes does the same when it's used).
    env["become"] = [this](lua_Integer id, const std::string& prefab) -> sol::object {
        const Entity entity = ToEntity(id);
        if (!registry_.valid(entity)) return sol::lua_nil;
        const auto made = BecomePrefab(entity, prefab);
        return made ? sol::make_object(lua_.State(), ToId(*made)) : sol::lua_nil;
    };
    // act(user, target) or act(user, "farm") -> boolean: `user` uses `target` (its Action or Interactable), or the nearest
    // thing with that action within 1.5 cells (docs/systems/actions.md).
    env["act"] = [this](lua_Integer userId, sol::object target) {
        const Entity user = ToEntity(userId);
        if (target.is<std::string>()) {
            const auto found = ActionTarget(user, target.as<std::string>(), 1.5f);
            return found ? Act(user, *found) : false;
        }
        return target.is<lua_Integer>() ? Act(user, ToEntity(target.as<lua_Integer>())) : false;
    };
    // use(id) -> boolean: used as the player would use it (unlocked with a key they carry, opened, taken, its flag,
    // its event, its becomes).
    env["use"] = [this](lua_Integer id) {
        const Entity entity = ToEntity(id);
        if (!registry_.valid(entity) || !FindPlayer(registry_)) return false;
        UseInteractable(entity);
        return true;
    };
    // set_texture(id, "assets/....png") -> boolean: its picture changed (a look, not another prefab).
    env["set_texture"] = [this](lua_Integer id, const std::string& texture) {
        const Entity entity = ToEntity(id);
        Sprite* sprite = registry_.valid(entity) ? registry_.try_get<Sprite>(entity) : nullptr;
        if (sprite == nullptr) return false;
        sprite->texture = texture;
        return true;
    };

    // Story (gameplay/Story): quests and flags, shared with dialogue.
    env["start_quest"] = [this](const std::string& id) {
        if (storyLibrary_) story_.StartQuest(id, *storyLibrary_);
    };
    env["complete_objective"] = [this](const std::string& quest, const std::string& objective) {
        if (storyLibrary_) story_.CompleteObjective(quest, objective, *storyLibrary_);
    };
    env["quest_state"] = [this](const std::string& id) {
        return std::string(StoryState::StateName(story_.Quest(id).state));
    };
    env["set_flag"] = [this](const std::string& name, sol::optional<bool> value) {
        story_.SetFlag(name, value.value_or(true));
    };
    env["get_flag"] = [this](const std::string& name) { return story_.Flag(name); };
    // Quests v2 and story variables (15.1-15.2), cutscenes (15.5).
    env["fail_quest"] = [this](const std::string& id) {
        if (storyLibrary_) story_.FailQuest(id, *storyLibrary_);
    };
    env["set_stage"] = [this](const std::string& id, int stage) {
        if (storyLibrary_) story_.SetStage(id, stage, *storyLibrary_);
    };
    env["quest_stage"] = [this](const std::string& id) {
        return storyLibrary_ ? story_.StageOf(id, *storyLibrary_) : 0;
    };
    env["get_var"] = [this, L](const std::string& name) -> sol::object { return JsonToLua(L, story_.Var(name)); };
    env["set_var"] = [this](const std::string& name, sol::object value) {
        if (value.is<bool>()) story_.SetVar(name, value.as<bool>());
        else if (value.get_type() == sol::type::number) {
            const double number = value.as<double>();
            if (number == static_cast<double>(static_cast<long long>(number))) story_.SetVar(name, static_cast<long long>(number));
            else story_.SetVar(name, number);
        } else if (value.is<std::string>()) story_.SetVar(name, value.as<std::string>());
        else story_.SetVar(name, nullptr);
    };
    env["used"] = [this](const std::string& thing) {
        if (storyLibrary_) story_.OnUsed(thing, *storyLibrary_);
    };
    env["play_cutscene"] = [this](const std::string& id) { return PlayCutscene(id); };
    // Game UI (17): notifications and the project's own screens.
    env["notify"] = [this](const std::string& channel, const std::string& text, sol::optional<double> seconds) {
        ShowNotice(text, seconds.value_or(3.0), channel);
    };
    env["open_screen"] = [this](const std::string& id) { return screenHook_ && screenHook_(id, 1); };
    env["close_screen"] = [this](const std::string& id) {
        if (screenHook_) screenHook_(id, 0);
    };
    env["is_open"] = [this](const std::string& id) { return screenHook_ && screenHook_(id, -1); };
    // Abilities and magic (19).
    env["cast"] = [this, self](const std::string& ability, sol::optional<sol::object> a, sol::optional<float> y) {
        float ax = 0, ay = 0;
        Entity target = entt::null;
        if (a && a->get_type() == sol::type::number && y) {
            ax = a->as<float>();
            ay = *y;
        } else if (a && a->get_type() == sol::type::number) {
            target = ToEntity(a->as<lua_Integer>());
            if (const auto* w = registry_.valid(target) ? registry_.try_get<WorldTransform2D>(target) : nullptr) {
                ax = w->x;
                ay = w->y;
            }
        } else if (const auto* w = registry_.try_get<WorldTransform2D>(self)) {
            const auto* f = registry_.try_get<Facing>(self);
            ax = w->x + (f ? f->x : 1.0f) * 3.0f;
            ay = w->y + (f ? f->y : 0.0f) * 3.0f;
        }
        return magic_->Cast(self, ability, ax, ay, target, nullptr, true);
    };
    env["apply_effect"] = [this, self](lua_Integer target, const std::string& effect, sol::optional<float> magnitude,
                                       sol::optional<float> duration) {
        return magic_->ApplyEffect(ToEntity(target), effect, self,
                                   magnitude ? std::optional<float>(*magnitude) : std::nullopt,
                                   duration ? std::optional<float>(*duration) : std::nullopt);
    };
    env["remove_effects"] = [this](lua_Integer target, const std::string& tag) { magic_->RemoveEffects(ToEntity(target), tag); };
    env["has_tag"] = [this](lua_Integer id, const std::string& tag) { return magic_->HasTag(ToEntity(id), tag); };
    env["add_tag"] = [this](lua_Integer id, const std::string& tag) { magic_->AddTag(ToEntity(id), tag); };
    env["cooldown_left"] = [this, self](const std::string& ability) { return magic_->CooldownLeft(self, ability); };
    env["learn"] = [this](lua_Integer id, const std::string& ability) { magic_->Learn(ToEntity(id), ability); };
    // Achievements (24).
    env["unlock_achievement"] = [this](const std::string& id) { return achievements_.Unlock(id); };
    env["achievement_unlocked"] = [this](const std::string& id) { return achievements_.Unlocked(id); };
    env["achievement_progress"] = [this](const std::string& id) { return achievements_.Progress(id); };
    // Physics (20): queries, impulses.
    env["raycast"] = [this, L, self](float x0, float y0, float x1, float y1, sol::optional<std::string> mask) -> sol::object {
        const auto hit = Raycast(registry_, [this](int gx, int gy) { return grid_.IsSolid(gx, gy); }, x0, y0, x1, y1,
                                 mask.value_or(""), self);
        if (!hit) return sol::lua_nil;
        sol::table t = sol::state_view(L).create_table();
        t["entity"] = hit->entity == entt::null ? sol::make_object(L, sol::lua_nil) : sol::make_object(L, ToId(hit->entity));
        t["x"] = hit->x;
        t["y"] = hit->y;
        t["distance"] = hit->distance;
        return t;
    };
    env["overlap"] = [this, L, self](float x, float y, float radius, sol::optional<std::string> mask) {
        sol::table ids = sol::state_view(L).create_table();
        int i = 1;
        for (Entity e : Overlap(registry_, x, y, radius, mask.value_or(""), self)) ids[i++] = ToId(e);
        return ids;
    };
    env["line_of_sight"] = [this](float x0, float y0, float x1, float y1) {
        return LineOfSight([this](int gx, int gy) { return grid_.IsSolid(gx, gy); }, x0, y0, x1, y1);
    };
    env["apply_impulse"] = [this](lua_Integer id, float ix, float iy) { physics_->ApplyImpulse(ToEntity(id), ix, iy); };
    env["set_velocity"] = [this](lua_Integer id, float vx, float vy) { physics_->SetVelocity(ToEntity(id), vx, vy); };
    // Animation and effects (18).
    env["play_effect"] = [this](const std::string& id, sol::object a, sol::optional<float> y) {
        if (a.is<float>() && y) return PlayEffect(id, a.as<float>(), *y);
        if (a.get_type() == sol::type::number) {
            const Entity target = ToEntity(a.as<lua_Integer>());
            if (const auto* w = registry_.valid(target) ? registry_.try_get<WorldTransform2D>(target) : nullptr) {
                return PlayEffect(id, w->x, w->y, target);
            }
        }
        return false;
    };
    env["emit_particles"] = [this](const std::string& id, float x, float y, sol::optional<float> seconds) {
        auto def = FindParticles(id);
        if (def) particles_.Emit(*def, x, y, seconds.value_or(0.0f));
        return def.has_value();
    };
    env["shake"] = [this](float trauma) { Shake(trauma); };
    env["hitstop"] = [this](float seconds) { HitStop(seconds); };
    env["slow_motion"] = [this](float scale, float seconds) { SlowMotion(scale, seconds); };
    env["tween"] = [this](lua_Integer id, const std::string& field, float to, float seconds, sol::optional<std::string> ease) {
        return StartTween(ToEntity(id), field, to, seconds, ease.value_or("ease_out"));
    };
    env["tween_done"] = [this](int id) { return TweenDone(id); };
    env["anim_trigger"] = [this](lua_Integer id, const std::string& trigger) { TriggerAnimation(registry_, ToEntity(id), trigger); };
    // play(id, "dig"): a state, a variant ("attack_kick") or a clip by name, once; "stop": back to its default state.
    env["play"] = [this](lua_Integer id, const std::string& name) { TriggerAnimation(registry_, ToEntity(id), name); };
    // anim_set(id, "angry", 1): a parameter for an animator record's conditions.
    env["anim_set"] = [this](lua_Integer id, const std::string& name, float value) { SetAnimationParameter(registry_, ToEntity(id), name, value); };
    // Difficulty (16): the level's id, a category's step (1-5), and changing it.
    env["difficulty"] = [this]() { return difficultyLevel_.empty() ? difficultyModel_.DefaultLevel() : difficultyLevel_; };
    env["difficulty_step"] = [this](const std::string& category) {
        const DifficultyLevel* level = difficultyModel_.Find(difficultyLevel_.empty() ? difficultyModel_.DefaultLevel() : difficultyLevel_);
        if (level == nullptr) return 3;
        auto it = level->steps.find(category);
        return it != level->steps.end() ? it->second : 3;
    };
    env["set_difficulty"] = [this](const std::string& id) {
        SetDifficulty(id);
        return difficultyLevel_ == id;
    };
    env["skip_cutscene"] = [this]() { SkipCutscene(); };
    env["in_cutscene"] = [this]() { return cutscene_.Active(); };
    // The game clock: hours since midnight (fractional), and the day number from 1.
    env["time_of_day"] = [this]() { return MinuteOfDay() / 60.0f; };
    env["day"] = [this]() { return Day(); };

    // Events, timers and coroutines (12.2). A reloaded script starts afresh.
    ForgetScript(self);
    // on("death", function(e) ... end) -> handle; e = {name, entity, other, amount, text}.
    // "*" receives every event. Handlers run as coroutines, so they may wait().
    env["on"] = [this, self](const std::string& name, sol::protected_function fn) {
        const int id = nextScriptId_++;
        handlers_.push_back({id, self, name, std::move(fn)});
        return id;
    };
    // emit("door_opened", {entity = id, other = id, amount = n, text = "..."})
    env["emit"] = [this](const std::string& name, sol::optional<sol::table> data) {
        ScriptEvent event{name};
        if (data) {
            if (auto entity = (*data).get<sol::optional<lua_Integer>>("entity")) event.entity = ToEntity(*entity);
            if (auto other = (*data).get<sol::optional<lua_Integer>>("other")) event.other = ToEntity(*other);
            event.amount = (*data).get_or("amount", 0.0);
            event.text = (*data).get_or<std::string>("text", "");
        }
        Emit(std::move(event));
    };
    // after(seconds, fn) / every(seconds, fn) -> handle; cancel(handle) stops a timer or handler.
    env["after"] = [this, self](double seconds, sol::protected_function fn) {
        const int id = nextScriptId_++;
        timers_.push_back({id, self, std::max(0.0, seconds), 0.0, std::move(fn)});
        return id;
    };
    env["every"] = [this, self](double seconds, sol::protected_function fn) {
        const int id = nextScriptId_++;
        const double interval = std::max(1.0 / 60.0, seconds);
        timers_.push_back({id, self, interval, interval, std::move(fn)});
        return id;
    };
    env["cancel"] = [this](int id) {
        std::erase_if(timers_, [id](const ScriptTimer& t) { return t.id == id; });
        std::erase_if(handlers_, [id](const ScriptHandler& h) { return h.id == id; });
    };
    // start(fn) runs fn as a coroutine now; inside, wait(seconds) pauses it.
    env["start"] = [this, self](sol::protected_function fn) { StartTask(self, fn, sol::lua_nil); };
    env["wait"] = sol::yielding([](sol::optional<double> seconds) { return seconds.value_or(0.0); });

    // Audio (12.6): a sound placed in the world; music instead of the scene's.
    env["play_sound_at"] = [this](const std::string& path, float x, float y) {
        if (playSoundAt_) playSoundAt_(path, x, y);
        else if (playSound_) playSound_(path);
    };
    env["play_music"] = [this](const std::string& path) { musicOverride_ = path; };

    // Input actions (12.3): action_down("attack"), action_pressed("interact")
    // (this frame only), axis("move_x") in [-1, 1]. See input.json.
    env["action_down"] = [this](const std::string& action) { return input_.Down(action); };
    env["action_pressed"] = [this](const std::string& action) { return input_.Pressed(action); };
    env["axis"] = [this](const std::string& action) { return input_.Value(action); };

    // Gameplay systems (13): attributes, items, experience, ranged, factions.
    env["attribute"] = [this](lua_Integer id, const std::string& name) {
        return EffectiveAttribute(registry_, ToEntity(id), name);
    };
    // add_modifier(id, "attack", 2, seconds, "source"[, "multiply"]): seconds < 0 lasts until removed.
    env["add_modifier"] = [this](lua_Integer id, const std::string& attribute, float value, sol::optional<float> seconds,
                                 sol::optional<std::string> source, sol::optional<std::string> op) {
        if (!registry_.valid(ToEntity(id))) return;
        AddModifier(registry_, ToEntity(id), {attribute, op && *op == "multiply" ? ModifierOp::Multiply : ModifierOp::Add,
                                              value, source.value_or("script"), seconds.value_or(-1.0f)});
    };
    env["remove_modifiers"] = [this](lua_Integer id, const std::string& source) {
        if (registry_.valid(ToEntity(id))) RemoveModifiers(registry_, ToEntity(id), source);
    };
    env["give_item"] = [this](lua_Integer id, const std::string& item, sol::optional<int> count) {
        const Entity entity = ToEntity(id);
        if (!registry_.valid(entity)) return 0;
        auto& inventory = registry_.get_or_emplace<Inventory>(entity);
        if (item == "gold") {
            inventory.gold += count.value_or(1);
            return count.value_or(1);
        }
        const int n = AddItem(inventory, item, count.value_or(1));
        if (n > 0) Emit({"item_added", entity, entt::null, static_cast<double>(n), item});
        return n;
    };
    env["take_item"] = [this](lua_Integer id, const std::string& item, sol::optional<int> count) {
        auto* inventory = registry_.valid(ToEntity(id)) ? registry_.try_get<Inventory>(ToEntity(id)) : nullptr;
        if (!inventory) return 0;
        if (item == "gold") {
            const int n = std::min(inventory->gold, count.value_or(1));
            inventory->gold -= n;
            return n;
        }
        return RemoveItem(*inventory, item, count.value_or(1));
    };
    env["count_item"] = [this](lua_Integer id, const std::string& item) {
        auto* inventory = registry_.valid(ToEntity(id)) ? registry_.try_get<Inventory>(ToEntity(id)) : nullptr;
        if (!inventory) return 0;
        return item == "gold" ? inventory->gold : CountItem(*inventory, item);
    };
    env["equip"] = [this](lua_Integer id, const std::string& item) {
        const std::string why = registry_.valid(ToEntity(id)) ? Equip(registry_, ToEntity(id), item) : "no such entity";
        return std::make_tuple(why.empty(), why);
    };
    env["use_item"] = [this](lua_Integer id, const std::string& item) {
        const std::string why = registry_.valid(ToEntity(id)) ? UseItem(registry_, ToEntity(id), item) : "no such entity";
        return std::make_tuple(why.empty(), why);
    };
    env["award_xp"] = [this](lua_Integer id, float amount) {
        const int levels = registry_.valid(ToEntity(id)) ? AwardExperience(registry_, ToEntity(id), amount * XpScale()) : 0;
        if (levels > 0) Emit({"level_up", ToEntity(id), entt::null, static_cast<double>(LevelOf(ToEntity(id))), ""});
        return levels;
    };
    env["level"] = [this](lua_Integer id) { return LevelOf(ToEntity(id)); };
    env["take_perk"] = [this](lua_Integer id, const std::string& perk) {
        const std::string why = registry_.valid(ToEntity(id)) ? TakePerk(registry_, ToEntity(id), perk) : "no such entity";
        return std::make_tuple(why.empty(), why);
    };
    // drop_loot(x, y, "loot-table"[, level]): rolls a loot table onto the ground.
    env["drop_loot"] = [this](float x, float y, const std::string& table, sol::optional<int> level) {
        const LootResult loot = RollLoot(table, level.value_or(1), gameplayRng_);
        return static_cast<int>(DropLoot(registry_, x, y, loot.items, loot.gold, gameplayRng_).size());
    };
    env["shoot"] = [this, self](float x, float y) {
        const auto* at = registry_.try_get<WorldTransform2D>(self);
        return at && TryRangedAttack(registry_, self, x - at->x, y - at->y).has_value();
    };
    env["dodge"] = [this, self](float dx, float dy) { return StartDodge(registry_, self, dx, dy); };
    env["is_hostile"] = [this](lua_Integer a, lua_Integer b) { return AreHostile(registry_, ToEntity(a), ToEntity(b)); };
    env["set_disposition"] = [this](lua_Integer id, int value) {
        if (auto* member = registry_.valid(ToEntity(id)) ? registry_.try_get<FactionMember>(ToEntity(id)) : nullptr) {
            member->disposition = std::clamp(value, -100, 100);
        }
    };

    // Character AI (14): packages, remarks, awareness, and a scripted package's moves.
    env["push_package"] = [this](lua_Integer id, const std::string& package) {
        if (registry_.valid(ToEntity(id))) PushPackage(registry_, ToEntity(id), package);
    };
    env["pop_package"] = [this](lua_Integer id, const std::string& package) {
        if (registry_.valid(ToEntity(id))) PopPackage(registry_, ToEntity(id), package);
    };
    env["bark"] = [this, self](const std::string& text) { ShowBark(self, text); };
    env["awareness_of"] = [this, self](sol::optional<lua_Integer> id) {
        const Entity who = id ? ToEntity(*id) : self;
        const auto* state = registry_.valid(who) ? registry_.try_get<BehaviourState>(who) : nullptr;
        return state ? state->awareness : 0.0f;
    };
    env["set_visible"] = [this](lua_Integer id, bool visible) {
        if (auto* sprite = registry_.valid(ToEntity(id)) ? registry_.try_get<Sprite>(ToEntity(id)) : nullptr) {
            sprite->a = visible ? 1.0f : 0.0f;
        }
    };
    env["face"] = [this, self](lua_Integer id) {
        const auto* a = registry_.try_get<WorldTransform2D>(self);
        const auto* b = registry_.valid(ToEntity(id)) ? registry_.try_get<WorldTransform2D>(ToEntity(id)) : nullptr;
        if (!a || !b) return;
        const float dx = b->x - a->x, dy = b->y - a->y, l = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
        registry_.emplace_or_replace<Facing>(self, dx / l, dy / l);
    };
    env["start_dialogue"] = [this, self](const std::string& path) {
        if (!storyLibrary_ || conversation_.Active()) return false;
        const Dialogue* dialogue = storyLibrary_->FindDialogue(path);
        if (!dialogue || !conversation_.Start(*dialogue, story_, *storyLibrary_)) return false;
        talkingTo_ = self;
        return true;
    };
    env["find_object"] = [this, self, L](const std::string& type, sol::optional<float> radius) -> sol::object {
        const auto* at = registry_.try_get<WorldTransform2D>(self);
        if (!at) return sol::lua_nil;
        std::optional<Entity> best;
        float bestD = radius.value_or(10.0f);
        for (auto [e, so, w] : registry_.view<SmartObject, WorldTransform2D>().each()) {
            if (!type.empty() && so.type != type) continue;
            const float d = std::hypot(w.x - at->x, w.y - at->y);
            if (d <= bestD) bestD = d, best = e;
        }
        return best ? sol::make_object(L, ToId(*best)) : sol::lua_nil;
    };
    // ai_step_to(x, y) -> arrived: a scripted package's walking (move_to below loops on it).
    env["ai_step_to"] = [this, self](float x, float y) {
        auto& state = registry_.get_or_emplace<BehaviourState>(self);
        const nlohmann::json goal = {x, y};
        if (state.blackboard["scriptGoal"] != goal) {
            state.blackboard["scriptGoal"] = goal;
            state.blackboard["scriptArrived"] = false;
            return false;
        }
        auto arrived = state.blackboard.find("scriptArrived");
        return arrived != state.blackboard.end() && arrived->second.is_boolean() && arrived->second.get<bool>();
    };
    lua_.State().safe_script("function move_to(x, y) while not ai_step_to(x, y) do wait() end end", env,
                             sol::script_pass_on_error);

    // Records (12.1): record("item", "iron-sword") -> a table of its fields
    // (with its base and defaults applied), or nil; records("item") -> ids.
    env["record"] = [L](const std::string& type, const std::string& id) -> sol::object {
        const Record* record = ActiveRecords().Find(type, id);
        return record ? JsonToLua(L, record->resolved) : sol::lua_nil;
    };
    env["records"] = [L](const std::string& type) {
        sol::table ids = sol::state_view(L).create_table();
        int i = 1;
        for (const Record* record : ActiveRecords().OfType(type)) ids[i++] = record->id;
        return ids;
    };
}

// --- Story -------------------------------------------------------------------

void GameSession::SetStory(const StoryLibrary* library) {
    storyLibrary_ = library;
    musicOverride_.clear(); // a new run plays the scenes' own music
    story_ = {};
    WireStory();
    conversation_.End();
    talkingTo_ = entt::null;
    notice_.clear();
    cutscene_ = {};
    cutsceneMoves_.clear();
    subtitle_.clear();
    titleCard_.clear();
    fade_ = fadeTarget_ = letterbox_ = 0.0f;
    physics_->Clear();
}

bool GameSession::CheckGateways() {
    auto player = FindPlayer(registry_);
    if (!player || registry_.all_of<Dead>(*player)) {
        return false;
    }
    const auto& at = registry_.get<WorldTransform2D>(*player);
    const int gx = static_cast<int>(std::floor(at.x));
    const int gy = static_cast<int>(std::floor(at.y));
    for (auto [entity, gateway, world] : registry_.view<Gateway, WorldTransform2D>().each()) {
        if (static_cast<int>(std::floor(world.x)) != gx || static_cast<int>(std::floor(world.y)) != gy) {
            continue;
        }
        if (!gatewayArmed_ || gateway.scene.empty()) {
            return false; // standing on one since arriving: step off first
        }
        travel_ = TravelRequest{gateway.scene, gateway.marker};
        gatewayArmed_ = false;
        return true;
    }
    // A building whose interior is its own scene: its doorway is a gateway there (docs/places/interiors.md).
    for (auto [entity, interior] : registry_.view<Interior>().each()) {
        int dx = 0, dy = 0;
        if (interior.kind != "scene" || !InteriorDoor(registry_, entity, interior, dx, dy) || dx != gx || dy != gy) continue;
        if (!gatewayArmed_ || interior.scene.empty()) {
            if (interior.scene.empty() && gatewayArmed_) ShowNotice("Its interior isn't built yet", 2.0);
            gatewayArmed_ = false;
            return false;
        }
        travel_ = TravelRequest{interior.scene, interior.marker};
        gatewayArmed_ = false;
        return true;
    }
    gatewayArmed_ = true;
    return false;
}

std::optional<GameSession::TravelRequest> GameSession::TakeTravel() {
    auto request = std::move(travel_);
    travel_.reset();
    return request;
}

void GameSession::SetScene(const std::string& scene) {
    scene_ = scene;
    physics_->Clear(); // a new scene's entities
    if (storyLibrary_ != nullptr) story_.SetScene(scene, *storyLibrary_);
}

nlohmann::json GameSession::MakeSave() const {
    nlohmann::json save = {{"version", 1}, {"scene", scene_}, {"clock", clockMinutes_}, {"story", story_.ToJson()},
                           {"difficulty", {{"level", difficultyLevel_}, {"adaptive", adaptive_.ToJson()}}}};
    if (auto player = FindPlayer(const_cast<Registry&>(registry_))) {
        // What the player knows and what's on them (19).
        nlohmann::json magic = nlohmann::json::object();
        if (const auto* a = registry_.try_get<Abilities>(*player)) magic["known"] = a->known, magic["slots"] = a->slots;
        if (const auto* m = registry_.try_get<MagicState>(*player)) {
            nlohmann::json effects = nlohmann::json::array();
            for (const auto& e : m->effects) {
                if (e.remaining < 1e8f) effects.push_back({{"effect", e.effect}, {"magnitude", e.magnitude}, {"remaining", e.remaining}});
            }
            magic["effects"] = effects;
            magic["cooldowns"] = m->cooldowns;
        }
        save["magic"] = magic;
    }
    if (auto player = FindPlayer(const_cast<Registry&>(registry_))) {
        const auto& at = registry_.get<WorldTransform2D>(*player);
        save["player"] = {{"x", at.x}, {"y", at.y}};
        if (const auto* health = registry_.try_get<Health>(*player)) save["player"]["health"] = health->current;
    }
    return save;
}

void GameSession::ApplySave(const nlohmann::json& save) {
    if (!save.is_object()) return;
    story_ = StoryState::FromJson(save.value("story", nlohmann::json::object()));
    WireStory();
    if (const auto difficulty = save.value("difficulty", nlohmann::json::object()); difficulty.is_object()) {
        if (difficultyModel_.Find(difficulty.value("level", ""))) difficultyLevel_ = difficulty.value("level", "");
        adaptive_ = AdaptiveDifficulty::FromJson(difficulty.value("adaptive", nlohmann::json::object()));
    }
    difficultyDirty_ = true;
    cutscene_ = {};
    cutsceneMoves_.clear();
    fade_ = fadeTarget_ = letterbox_ = 0.0f;
    if (save.contains("clock") && save["clock"].is_number()) clockMinutes_ = save["clock"].get<double>();
    scene_ = save.value("scene", scene_);
    if (auto player = FindPlayer(registry_); player && save.contains("magic") && save["magic"].is_object()) {
        const auto& magic = save["magic"];
        if (magic.contains("known")) registry_.emplace_or_replace<Abilities>(*player, magic.value("known", ""), magic.value("slots", ""));
        auto& state = registry_.get_or_emplace<MagicState>(*player);
        for (const auto& e : magic.value("effects", nlohmann::json::array())) {
            magic_->ApplyEffect(*player, e.value("effect", ""), *player, e.value("magnitude", 0.0f), e.value("remaining", 1.0f));
        }
        const auto cooldowns = magic.value("cooldowns", nlohmann::json::object());
        for (const auto& [id, left] : cooldowns.items()) state.cooldowns[id] = left.get<float>();
    }
    if (auto player = FindPlayer(registry_); player && save.contains("player") && save["player"].is_object()) {
        auto& at = registry_.get<WorldTransform2D>(*player);
        at.x = save["player"].value("x", at.x);
        at.y = save["player"].value("y", at.y);
        if (auto* health = registry_.try_get<Health>(*player); health && save["player"].contains("health")) {
            health->current = save["player"].value("health", health->current);
        }
    }
    conversation_ = DialogueRunner{};
    talkingTo_ = entt::null;
    ShowNotice("Game loaded", 2.0);
}

void GameSession::OnSceneChanged() {
    // Nothing from the old scene is valid any more; the story, the clock and
    // the player's own components carry on.
    conversation_ = DialogueRunner{};
    talkingTo_ = entt::null;
    requestedAttack_.reset();
    input_.HoldCurrent(); // a held attack doesn't swing on arrival
    events_.clear();
    gatewayArmed_ = false;
    travel_.reset();
    difficultyDirty_ = true; // the new scene's entities, at the current level
    // A cutscene doesn't carry over (its actors were in the old scene).
    cutscene_.Stop();
    cutsceneMoves_.clear();
    cameraFocus_ = cameraFollow_ = entt::null;
    cameraGoal_.reset();
    cameraZoom_.reset();
    subtitle_.clear();
    titleCard_.clear();
    fade_ = fadeTarget_ = letterbox_ = 0.0f;
}

std::optional<Entity> GameSession::TalkTarget() {
    if (storyLibrary_ == nullptr || conversation_.Active()) {
        return std::nullopt;
    }
    auto player = FindPlayer(registry_);
    if (!player || registry_.all_of<Dead>(*player)) {
        return std::nullopt;
    }
    const auto& p = registry_.get<WorldTransform2D>(*player);
    std::optional<Entity> best;
    float bestDistance = 0.0f;
    for (auto [entity, talk, world] : registry_.view<Talk, WorldTransform2D>(entt::exclude<Dead, Sleeping>).each()) {
        float distance = std::hypot(world.x - p.x, world.y - p.y);
        if (distance <= talk.range && (!best || distance < bestDistance)) {
            best = entity;
            bestDistance = distance;
        }
    }
    return best;
}

std::string GameSession::NameOf(const std::string& key, Entity fallback) const {
    if (nameOf_ && !key.empty()) {
        std::string name = nameOf_(key);
        if (!name.empty()) {
            return name;
        }
    }
    if (registry_.valid(fallback)) {
        if (const auto* name = registry_.try_get<Name>(fallback); name && !name->value.empty()) {
            return name->value;
        }
    }
    return key;
}

void GameSession::HandleStoryInput(const KeyQueryFn& isKeyDown) {
    (void)isKeyDown; // read through the input actions (12.3), updated once per frame
    const bool interact = input_.Pressed("interact");
    const bool confirm = input_.Pressed("confirm");
    const bool up = input_.Pressed("nav_up");
    const bool down = input_.Pressed("nav_down");
    int number = 0;
    for (int i = 1; i <= 9; ++i) {
        if (input_.Pressed("choice_" + std::to_string(i))) number = i;
    }
    if (storyLibrary_ == nullptr) {
        // No dialogue in this game: the interact action still uses things (13.5).
        if (interact && !conversation_.Active()) {
            if (const auto player = FindPlayer(registry_)) {
                if (auto thing = FindInteractable(registry_, *player)) UseInteractable(*thing);
            }
        }
        return;
    }
    if (conversation_.Active()) {
        if (number > 0) {
            conversation_.Choose(static_cast<size_t>(number - 1), story_, *storyLibrary_);
        } else if (up || down) {
            conversation_.MoveHighlight(up ? -1 : 1);
        } else if (confirm) {
            conversation_.Continue(story_, *storyLibrary_);
        }
        if ((number > 0 || confirm) && conversation_.LastCheck()) {
            Toast(*conversation_.LastCheck() ? "Success" : "Failure"); // a skill check (15.3)
        }
        if (!conversation_.Active()) {
            talkingTo_ = entt::null;
        }
        return;
    }
    if (interact) {
        if (auto npc = TalkTarget()) {
            const Talk& talk = registry_.get<Talk>(*npc);
            // Deliver objectives (15.1): what they're owed changes hands on talking.
            story_.OnDelivered(talk.character, [this](const std::string& item, int count) {
                const auto player = FindPlayer(registry_);
                auto* inventory = player ? registry_.try_get<Inventory>(*player) : nullptr;
                if (!inventory || CountItem(*inventory, item) < count) return false;
                RemoveItem(*inventory, item, count);
                return true;
            }, *storyLibrary_);
            if (const Dialogue* dialogue = storyLibrary_->FindDialogue(talk.dialogue)) {
                story_.OnTalkedTo(talk.character, *storyLibrary_);
                if (conversation_.Start(*dialogue, story_, *storyLibrary_)) {
                    talkingTo_ = *npc;
                    Emit({"talk", *npc, FindPlayer(registry_).value_or(Entity{entt::null}), 0.0, talk.character});
                }
            }
        } else if (auto target = CurrentInteraction(); target && !target->talk) {
            UseInteractable(target->entity); // a door, a chest, a lever, a merchant (13.5)
        }
    }
}

void GameSession::OnStoryEvents(size_t firstEvent) {
    if (storyLibrary_ == nullptr) {
        return;
    }
    for (size_t i = firstEvent; i < events_.size(); ++i) {
        const GameEvent& event = events_[i];
        if (event.type == GameEvent::Type::Death) {
            story_.OnDefeated(event.prefab, event.team == Team::Enemy ? "Enemy" : "", *storyLibrary_);
            // An escort or defend objective's charge died: the quest fails (15.1).
            std::vector<std::string> names = NamesOf(event.entity);
            if (!event.actor.empty()) names.push_back(event.actor);
            for (const auto& name : names) story_.OnLost(name, *storyLibrary_);
        } else if (event.type == GameEvent::Type::Pickup) {
            story_.OnCollected(event.prefab, *storyLibrary_);
        }
    }
}

float GameSession::MinuteOfDay() const {
    double minutes = std::max(0.0, clockMinutes_);
    return static_cast<float>(std::fmod(minutes, static_cast<double>(kMinutesPerDay)));
}

int GameSession::Day() const {
    return static_cast<int>(std::max(0.0, clockMinutes_) / kMinutesPerDay) + 1;
}

void GameSession::ShowNotice(const std::string& text, double seconds, const std::string& channel, const std::string& key) {
    if (text.empty()) return;
    if (noticeSink_) {
        noticeSink_(channel, text, key, seconds); // the UI documents' notifications (17.5)
        return;
    }
    notice_ = text;
    noticeSeconds_ = seconds;
}

std::optional<DialogueView> GameSession::CurrentDialogueView() const {
    const DialogueNode* node = conversation_.Node();
    if (node == nullptr) return std::nullopt;
    const Dialogue* dialogue = conversation_.Current();
    DialogueView view;
    const std::string speaker = node->speaker.empty() ? dialogue->speaker : node->speaker;
    if (speaker == "player") {
        auto player = FindPlayer(const_cast<Registry&>(registry_));
        view.speaker = player ? NameOf("", *player) : std::string();
        if (view.speaker.empty()) view.speaker = "You";
    } else {
        view.speaker = NameOf(speaker, talkingTo_);
    }
    // Translated when the game's language has the line (12.7).
    const std::string key = "dialogue/" + std::filesystem::path(dialogue->path).stem().string() + "/" + node->id;
    view.text = Tr(key, node->text);
    for (size_t index : conversation_.VisibleChoices()) {
        const DialogueChoice& choice = node->choices[index];
        std::string label = Tr(key + "/" + std::to_string(index + 1), choice.text);
        // A skill check shows what it tests: "[Speech 5] Surely you can spare it."
        if (choice.check.is_object() && choice.check.contains("attribute")) {
            std::string attribute = choice.check.value("attribute", "");
            if (!attribute.empty()) attribute[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(attribute[0])));
            const nlohmann::json level = choice.check.contains("difficulty") ? choice.check["difficulty"]
                                                                             : choice.check.value("atLeast", nlohmann::json(0));
            label = "[" + attribute + " " + (level.is_number() ? std::to_string(level.get<int>()) : std::string("?")) + "] " +
                    label;
        }
        view.choices.push_back(label);
    }
    view.highlighted = conversation_.Highlighted();
    view.portrait = node->portrait;
    return view;
}

void GameSession::ChooseDialogue(int index) {
    if (!conversation_.Active() || storyLibrary_ == nullptr || index < 0) return;
    conversation_.Choose(static_cast<size_t>(index), story_, *storyLibrary_);
    if (conversation_.LastCheck()) Toast(*conversation_.LastCheck() ? "Success" : "Failure");
    if (!conversation_.Active()) talkingTo_ = entt::null;
}

HudData GameSession::MakeHud(float viewportWidth, float viewportHeight) {
    HudData hud;
    hud.visible = !cutscene_.Active();
    if (const auto player = FindPlayer(registry_)) {
        if (const auto* health = registry_.try_get<Health>(*player)) {
            hud.health = std::max(0.0f, health->current);
            hud.healthMax = health->max;
        }
        if (const auto* resources = registry_.try_get<Resources>(*player)) {
            hud.mana = resources->mana;
            hud.manaMax = resources->maxMana;
            hud.stamina = resources->stamina;
            hud.staminaMax = resources->maxStamina;
        }
        if (const auto* progression = registry_.try_get<Progression>(*player)) {
            hud.level = progression->level;
            hud.xp = progression->experience;
            hud.xpNext = ExperienceForLevel(progression->level);
        }
        if (const auto* inventory = registry_.try_get<Inventory>(*player)) hud.gold = inventory->gold;
        // The quick slots with their cooldowns (19).
        if (const auto* abilities = registry_.try_get<Abilities>(*player)) {
            int n = 1;
            for (const auto& id : SplitList(abilities->slots)) {
                auto ability = FindAbility(id);
                const float left = magic_->CooldownLeft(*player, id);
                hud.abilities += (n > 1 ? "    " : "") + std::to_string(n) + " " + (ability ? ability->name : id) +
                                 (left > 0.05f ? " (" + std::to_string(static_cast<int>(std::ceil(left))) + "s)" : "");
                if (++n > 4) break;
            }
        }
    }
    const auto routines = registry_.view<Routine>();
    if (ActiveSettings().time.showClock || routines.begin() != routines.end()) {
        hud.clock = "Day " + std::to_string(Day()) + "  " + FormatClockTime(static_cast<int>(MinuteOfDay()));
    }
    if (!barkText_.empty() && !conversation_.Active()) hud.subtitle = barkText_;
    if (cutscene_.Active()) {
        hud.subtitle = subtitle_;
        return hud;
    }
    if (storyLibrary_ != nullptr) {
        if (auto shown = story_.CurrentObjective(*storyLibrary_)) {
            hud.quest = shown->quest;
            hud.objective = shown->text;
            // The marker (15.4): which way the objective lies, when it's more than a few steps off.
            const auto player = FindPlayer(registry_);
            const auto target = ObjectiveLocation();
            if (player && target && !conversation_.Active() && ActiveSettings().story.objectiveMarkers) {
                const auto& at = registry_.get<WorldTransform2D>(*player);
                const float dx = target->first - at.x, dy = target->second - at.y;
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance > 3.0f) {
                    const bool topDown = ActiveSettings().grid.style == GridStyle::TopDown;
                    const float sx = topDown ? dx : dx - dy, sy = topDown ? dy : (dx + dy) * 0.5f;
                    const float length = std::max(0.0001f, std::sqrt(sx * sx + sy * sy));
                    const float radius = std::min(viewportWidth, viewportHeight) * 0.36f;
                    hud.marker = std::to_string(static_cast<int>(distance + 0.5f)) + " m";
                    hud.markerX = viewportWidth * 0.5f + sx / length * radius;
                    hud.markerY = viewportHeight * 0.5f + sy / length * radius;
                }
            }
        }
    }
    if (!conversation_.Active()) {
        if (auto npc = storyLibrary_ ? TalkTarget() : std::nullopt) {
            const Talk& talk = registry_.get<Talk>(*npc);
            if (storyLibrary_->FindDialogue(talk.dialogue) != nullptr) {
                hud.promptText = "Talk to " + NameOf(talk.character, *npc);
            }
        } else if (auto target = CurrentInteraction(); target && !target->talk) {
            hud.promptText = InteractPrompt(registry_, target->entity);
        }
        if (hud.promptText.empty()) hud.promptText = doorPrompt_; // a door opened by use (docs/assets/animations.md §4.2)
        // The key or button the interact action is bound to, for the device in use (17.6).
        if (!hud.promptText.empty()) {
            hud.promptKey = ActionGlyph(ActiveInputMap(), "interact", input_.UsingPad());
            if (hud.promptKey.empty()) hud.promptKey = ActionGlyph(ActiveInputMap(), "interact", !input_.UsingPad());
        }
    }
    return hud;
}

void GameSession::BuildUi(std::vector<UiQuad>& out, float viewportWidth, float viewportHeight) {
    // Post (18.6): a vignette under the UI.
    if (ActiveSettings().lighting.vignette) {
        UiQuad v;
        v.x1 = viewportWidth;
        v.y1 = viewportHeight;
        v.u1 = v.v1 = 1.0f;
        v.texture = "fx:vignette";
        out.push_back(v);
    }
    // With the UI documents (17), the HUD and dialogue are documents: only the
    // cutscene layers and the debug overlay are drawn here.
    const HudData hud = documentHud_ ? HudData{} : MakeHud(viewportWidth, viewportHeight);
    if (!documentHud_) {
        if (!hud.clock.empty()) AddClock(out, hud.clock, viewportWidth, viewportHeight);
        if (!notice_.empty()) AddToast(out, notice_, viewportWidth, viewportHeight);
    }
    if (cutscene_.Active() || letterbox_ > 0.0f || fade_ > 0.0f) {
        // A cutscene (15.5): bars, its lines and title, its fades; nothing of play.
        AddLetterbox(out, letterbox_, viewportWidth, viewportHeight);
        if (!subtitle_.empty()) AddSubtitle(out, subtitle_, viewportWidth, viewportHeight);
        if (!titleCard_.empty()) {
            AddTitleCard(out, titleCard_, static_cast<float>(std::min(1.0, titleSeconds_)), viewportWidth, viewportHeight);
        }
        AddScreenFade(out, fade_, viewportWidth, viewportHeight);
        if (cutscene_.Active()) {
            if (cutscene_.WaitingForInput()) AddPrompt(out, "Space", "Continue", viewportWidth, viewportHeight);
            return;
        }
    }
    if (showAiDebug_) {
        // What every character is doing (F4).
        std::vector<std::string> lines;
        for (auto [entity, behaviour] : registry_.view<Behaviour>().each()) {
            const auto* name = registry_.try_get<Name>(entity);
            const auto* state = registry_.try_get<BehaviourState>(entity);
            char line[160];
            std::snprintf(line, sizeof(line), "%s: %s  aware %.0f%%", name && !name->value.empty() ? name->value.c_str() : "?",
                          DescribeBehaviour(registry_, entity).c_str(), state ? state->awareness * 100.0f : 0.0f);
            lines.push_back(line);
            if (lines.size() >= 24) break;
        }
        if (lines.empty()) lines.push_back("No characters with Behaviour");
        AddDebugLines(out, lines, viewportWidth, viewportHeight);
    }
    if (documentHud_) return;
    if (!hud.subtitle.empty()) AddSubtitle(out, hud.subtitle, viewportWidth, viewportHeight);
    if (!hud.objective.empty()) AddObjective(out, hud.quest, hud.objective, viewportWidth, viewportHeight);
    if (!hud.marker.empty()) {
        AddObjectiveMarker(out, hud.markerX - viewportWidth * 0.5f, hud.markerY - viewportHeight * 0.5f, hud.marker,
                           viewportWidth, viewportHeight);
    }
    if (auto view = CurrentDialogueView()) {
        AddDialogueBox(out, *view, viewportWidth, viewportHeight);
    } else if (!hud.promptText.empty()) {
        AddPrompt(out, hud.promptKey, hud.promptText, viewportWidth, viewportHeight);
    }
}

void GameSession::ShowNotices() {
    ApplyWorldEffects(); // the story's items, gold, experience, reputation, cutscenes
    for (const auto& notice : story_.TakeNotices()) {
        using Kind = StoryState::Notice::Kind;
        ShowNotice(notice.kind == Kind::QuestStarted     ? "New quest: " + notice.text
                   : notice.kind == Kind::QuestCompleted ? "Quest complete: " + notice.text
                   : notice.kind == Kind::QuestFailed    ? "Quest failed: " + notice.text
                   : notice.kind == Kind::StageChanged   ? "Journal: " + notice.text
                                                         : "Done: " + notice.text,
                   3.0);
        Emit({notice.kind == Kind::QuestStarted     ? "quest_started"
              : notice.kind == Kind::QuestCompleted ? "quest_completed"
              : notice.kind == Kind::QuestFailed    ? "quest_failed"
              : notice.kind == Kind::StageChanged   ? "quest_stage"
                                                    : "objective_completed",
              entt::null, entt::null, 0.0, notice.quest});
    }
    ApplyWorldEffects(); // and any the notices caused
}

// --- Narrative v2 (development_plan.md 15) ----------------------------------------

void GameSession::WireStory() {
    // The story asks about the world (15.2): items, gold, level, attributes, the
    // time, the day, reputation with a faction.
    story_.SetWorldConditions([this](const nlohmann::json& c) {
        const auto player = FindPlayer(registry_);
        const auto* inventory = player ? registry_.try_get<Inventory>(*player) : nullptr;
        if (c.contains("item")) {
            const int have = inventory ? CountItem(*inventory, c.value("item", "")) : 0;
            return have >= c.value("count", 1);
        }
        if (c.contains("gold")) return (inventory ? inventory->gold : 0) >= c.value("gold", 0);
        if (c.contains("level")) return player && LevelOf(*player) >= c.value("level", 1);
        if (c.contains("attribute")) {
            if (!player) return false;
            const float value = EffectiveAttribute(registry_, *player, c.value("attribute", ""));
            if (c.contains("below")) return value < c.value("below", 0.0f);
            return value >= c.value("atLeast", 1.0f);
        }
        if (c.contains("time") && c["time"].is_object()) {
            const float hour = MinuteOfDay() / 60.0f;
            const float from = c["time"].value("from", 0.0f), to = c["time"].value("to", 24.0f);
            return from <= to ? hour >= from && hour < to : hour >= from || hour < to;
        }
        if (c.contains("day")) return Day() >= c.value("day", 1);
        if (c.contains("difficulty")) {
            // {"difficulty": "story"}: at that level; {"difficulty": {"enemies": {"atLeast": 4}}}: a step.
            const std::string level = difficultyLevel_.empty() ? difficultyModel_.DefaultLevel() : difficultyLevel_;
            if (c["difficulty"].is_string()) return c["difficulty"].get<std::string>() == level;
            const DifficultyLevel* l = difficultyModel_.Find(level);
            if (!l || !c["difficulty"].is_object()) return false;
            for (const auto& [category, test] : c["difficulty"].items()) {
                auto it = l->steps.find(category);
                const int step = it == l->steps.end() ? 3 : it->second;
                if (test.is_object() && (step < test.value("atLeast", 1) || step > test.value("atMost", 5))) return false;
            }
            return true;
        }
        if (c.contains("faction")) {
            const nlohmann::json value = story_.Var("reputation." + c.value("faction", ""));
            const double reputation = value.is_number() ? value.get<double>() : 0.0;
            if (c.contains("below")) return reputation < c.value("below", 0.0);
            return reputation >= c.value("atLeast", 0.0);
        }
        return false;
    });
}

void GameSession::ApplyWorldEffects() {
    const auto player = FindPlayer(registry_);
    for (const StoryEffect& e : story_.TakeWorldEffects()) {
        const int amount = e.value.is_number() ? static_cast<int>(e.value.get<double>()) : 1;
        if (e.op == "play_cutscene") {
            PlayCutscene(e.name);
            continue;
        }
        if (e.op == "reputation") {
            const nlohmann::json now = story_.Var("reputation." + e.name);
            story_.SetVar("reputation." + e.name, (now.is_number() ? now.get<int>() : 0) + amount);
            for (auto [entity, member] : registry_.view<FactionMember>().each()) {
                if (member.faction == e.name) member.disposition = std::clamp(member.disposition + amount, -100, 100);
            }
            continue;
        }
        if (!player) continue;
        auto& inventory = registry_.get_or_emplace<Inventory>(*player);
        if (e.op == "give_item") {
            AddItem(inventory, e.name, std::max(1, amount));
            Toast("Received " + e.name + (amount > 1 ? " x" + std::to_string(amount) : ""));
            Emit({"item_added", *player, entt::null, static_cast<double>(amount), e.name});
        } else if (e.op == "take_item") {
            RemoveItem(inventory, e.name, std::max(1, amount));
        } else if (e.op == "give_gold") {
            inventory.gold = std::max(0, inventory.gold + amount);
            if (amount > 0) Toast("Received " + std::to_string(amount) + " gold");
        } else if (e.op == "give_xp") {
            const float xp = e.value.is_number() ? e.value.get<float>() : 0.0f;
            if (AwardExperience(registry_, *player, xp * XpScale()) > 0) {
                Emit({"level_up", *player, entt::null, static_cast<double>(LevelOf(*player)), ""});
                Toast("Level " + std::to_string(LevelOf(*player)) + "!");
            }
        }
    }
}

std::vector<std::string> GameSession::NamesOf(Entity entity) const {
    std::vector<std::string> names;
    if (!registry_.valid(entity)) return names;
    if (const auto* actor = registry_.try_get<Actor>(entity); actor && !actor->key.empty()) names.push_back(actor->key);
    if (const auto* talk = registry_.try_get<Talk>(entity); talk && !talk->character.empty()) names.push_back(talk->character);
    if (const auto* name = registry_.try_get<Name>(entity); name && !name->value.empty()) names.push_back(name->value);
    if (const auto* thing = registry_.try_get<Interactable>(entity); thing && !thing->name.empty()) names.push_back(thing->name);
    return names;
}

std::optional<Entity> GameSession::FindNamed(const std::string& who) const {
    if (who.empty()) return std::nullopt;
    if (who == "player") return FindPlayer(const_cast<Registry&>(registry_));
    for (auto [entity, world] : registry_.view<WorldTransform2D>(entt::exclude<Dead>).each()) {
        for (const auto& name : NamesOf(entity)) {
            if (name == who) return entity;
        }
    }
    return std::nullopt;
}

std::optional<std::pair<float, float>> GameSession::MarkerPosition(const std::string& name) const {
    for (auto [entity, marker, world] : registry_.view<Marker, WorldTransform2D>().each()) {
        if (marker.name == name) return std::pair{world.x, world.y};
    }
    return std::nullopt;
}

void GameSession::StepStory(float dt) {
    if (storyLibrary_ == nullptr || !story_.AnyQuestActive()) return;
    story_.OnTime(dt, *storyLibrary_);
    const auto player = FindPlayer(registry_);
    const auto* inventory = player ? registry_.try_get<Inventory>(*player) : nullptr;
    story_.OnHave([&](const std::string& item) { return inventory ? CountItem(*inventory, item) : 0; }, *storyLibrary_);
    story_.OnEscorted(
        [&](const std::string& who, const std::string& marker) {
            const auto entity = FindNamed(who);
            const auto at = MarkerPosition(marker);
            if (!entity || !at) return false;
            const auto& world = registry_.get<WorldTransform2D>(*entity);
            return std::hypot(world.x - at->first, world.y - at->second) <= 1.5f;
        },
        *storyLibrary_);
}

void GameSession::StepTriggers() {
    const auto player = FindPlayer(registry_);
    if (!player || cutscene_.Active()) return;
    const auto& at = registry_.get<WorldTransform2D>(*player);
    for (auto [entity, trigger, world] : registry_.view<CutsceneTrigger, WorldTransform2D>().each()) {
        if (trigger.cutscene.empty() || std::hypot(world.x - at.x, world.y - at.y) > trigger.radius) continue;
        const std::string flag = "cutscene:" + trigger.cutscene;
        if (story_.Flag(flag)) continue;
        story_.SetFlag(flag, true); // a repeating one: until the player leaves (cleared below)
        PlayCutscene(trigger.cutscene);
        return;
    }
    // A repeating trigger plays again once the player has left its area.
    for (auto [entity, trigger, world] : registry_.view<CutsceneTrigger, WorldTransform2D>().each()) {
        if (!trigger.once && std::hypot(world.x - at.x, world.y - at.y) > trigger.radius + 0.5f) {
            story_.SetFlag("cutscene:" + trigger.cutscene, false);
        }
    }
}

bool GameSession::PlayCutscene(const std::string& id, const std::string& quest) {
    std::string error;
    auto cutscene = LoadCutscene(scriptRoot_, id, &error);
    if (!cutscene) {
        if (scriptErrors_.size() < 64) scriptErrors_.push_back({entt::null, "cutscenes/" + id + ".json", error});
        return false;
    }
    if (cutscene_.Active()) SkipCutscene(); // one at a time: the old one jumps to its end
    conversation_.End();
    talkingTo_ = entt::null;
    cutsceneQuest_ = quest;
    cutsceneMoves_.clear();
    cameraFollow_ = entt::null;
    cameraGoal_.reset();
    cameraZoom_.reset();
    cutscene_.Start(std::move(*cutscene));
    Emit({"cutscene_started", entt::null, entt::null, 0.0, id});
    return true;
}

void GameSession::SkipCutscene() {
    if (!cutscene_.Active()) return;
    cutscene_.Skip([this](const CutscenePlayer::Due& due) { FireCutsceneKey(due); });
    StepCutscene(0.0f); // finishes moves and cleans up
}

void GameSession::FireCutsceneKey(const CutscenePlayer::Due& due) {
    const CutsceneTrack& track = *due.track;
    const CutsceneKey& key = *due.key;
    const nlohmann::json& d = key.data;
    // "@thief": the quest that started it, else any quest under way that has the alias.
    auto resolve = [this](const std::string& name) {
        if (name.size() < 2 || name[0] != '@' || storyLibrary_ == nullptr) return name;
        std::vector<std::string> quests = story_.StartedOrder();
        if (!cutsceneQuest_.empty()) quests.insert(quests.begin(), cutsceneQuest_);
        for (const auto& id : quests) {
            if (const Quest* quest = storyLibrary_->FindQuest(id); quest && quest->Resolve(name) != name) return quest->Resolve(name);
        }
        return name;
    };
    auto placeOf = [&](const nlohmann::json& data) -> std::optional<std::pair<float, float>> {
        if (data.contains("marker")) return MarkerPosition(resolve(data.value("marker", "")));
        if (data.contains("x") && data.contains("y")) return std::pair{data.value("x", 0.0f), data.value("y", 0.0f)};
        if (data.contains("actor") || data.contains("target")) {
            const std::string who = resolve(data.value("actor", data.value("target", "")));
            if (auto entity = FindNamed(who)) {
                const auto& w = registry_.get<WorldTransform2D>(*entity);
                return std::pair{w.x, w.y};
            }
        }
        return std::nullopt;
    };
    if (track.type == "actor") {
        const auto entity = FindNamed(resolve(track.actor));
        if (!entity) return;
        auto& world = registry_.get<WorldTransform2D>(*entity);
        if (key.op == "move" || key.op == "place") {
            const auto to = placeOf(d);
            if (!to) return;
            std::erase_if(cutsceneMoves_, [&](const CutsceneMove& m) { return m.entity == *entity; });
            if (key.op == "place" || due.skipping) {
                world.x = to->first;
                world.y = to->second;
            } else {
                cutsceneMoves_.push_back({*entity, to->first, to->second, d.value("speed", 2.0f)});
            }
        } else if (key.op == "face") {
            if (const auto to = placeOf(d)) {
                const float dx = to->first - world.x, dy = to->second - world.y, l = std::sqrt(dx * dx + dy * dy);
                if (l > 0.001f) registry_.emplace_or_replace<Facing>(*entity, dx / l, dy / l);
            }
        } else if (key.op == "show" || key.op == "hide") {
            if (auto* sprite = registry_.try_get<Sprite>(*entity)) sprite->a = key.op == "show" ? 1.0f : 0.0f;
        } else if (key.op == "tween") {
            // A field eased over the key's seconds (18.3): {"field": "Sprite.a", "to": 0}.
            const int id = StartTween(*entity, d.value("field", ""), d.value("to", 0.0f), due.skipping ? 0.0f : key.seconds,
                                      d.value("ease", "ease_in_out"));
            (void)id;
        } else if (key.op == "animate") {
            TriggerAnimation(registry_, *entity, d.value("trigger", "using"));
        }
    } else if (track.type == "dialogue") {
        if (due.skipping) return;
        const std::string speaker = resolve(d.value("speaker", ""));
        const std::string text = Tr("cutscene/" + cutscene_.Current().id + "/" + std::to_string(static_cast<int>(key.at * 10)),
                                    d.value("text", ""));
        const std::string who = speaker.empty() ? std::string() : NameOf(speaker, FindNamed(speaker).value_or(Entity{entt::null}));
        subtitle_ = who.empty() ? text : who + ": " + text;
        subtitleSeconds_ = key.seconds > 0.0f ? key.seconds : 2.0 + text.size() / 20.0;
        if (playSound_ && !d.value("voice", "").empty()) playSound_(d.value("voice", ""));
    } else if (track.type == "camera") {
        if (key.op == "follow") {
            cameraFollow_ = FindNamed(resolve(d.value("actor", ""))).value_or(Entity{entt::null});
            cameraGoal_.reset();
        } else if (key.op == "move") {
            cameraGoal_ = placeOf(d);
            cameraFollow_ = entt::null;
        } else if (key.op == "zoom") {
            cameraZoom_ = d.value("value", 1.0f);
        } else if (key.op == "shake" && !due.skipping) {
            Shake(d.value("trauma", 0.4f));
        }
    } else if (track.type == "audio") {
        if (key.op == "music") musicOverride_ = d.value("path", "");
        else if (key.op == "sound" && !due.skipping && playSound_) playSound_(d.value("path", ""));
    } else if (track.type == "screen") {
        if (key.op == "fade_out" || key.op == "fade_in") {
            fadeTarget_ = key.op == "fade_out" ? 1.0f : 0.0f;
            fadeRate_ = key.seconds > 0.0f ? 1.0f / key.seconds : 1000.0f;
            if (due.skipping) fade_ = fadeTarget_;
        } else if (key.op == "title" && !due.skipping) {
            titleCard_ = Tr("cutscene/" + cutscene_.Current().id + "/title", d.value("text", ""));
            titleSeconds_ = key.seconds > 0.0f ? key.seconds : 3.0;
        }
    } else if (track.type == "effect") {
        // An effect at a marker, an actor or a point (18.5); skipped when skipping.
        if (key.op == "play" && !due.skipping) {
            if (const auto at = placeOf(d)) {
                const auto who = d.contains("actor") ? FindNamed(resolve(d.value("actor", ""))) : std::nullopt;
                PlayEffect(d.value("effect", ""), at->first, at->second, who.value_or(Entity{entt::null}));
            }
        }
    } else if (track.type == "logic") {
        if (key.op == "do" && storyLibrary_ != nullptr) {
            story_.Apply(StoryEffectsFromJson(d.value("do", nlohmann::json::array())), *storyLibrary_);
        } else if (key.op == "emit") {
            Emit({d.value("event", ""), entt::null, entt::null, d.value("amount", 0.0), d.value("text", "")});
        }
    }
}

void GameSession::StepCutscene(float dt) {
    if (cutscene_.Active()) {
        if (input_.Pressed("skip")) {
            if (cutscene_.WaitingForInput()) cutscene_.Continue();
            else if (cutscene_.Current().skippable) {
                SkipCutscene();
                return;
            }
        }
        cutscene_.Update(dt, [this](const CutscenePlayer::Due& due) { FireCutsceneKey(due); });
    }
    const bool ended = !cutscene_.Active();
    // Moves under way (a skip or the end puts everyone at their mark).
    for (auto it = cutsceneMoves_.begin(); it != cutsceneMoves_.end();) {
        auto* world = registry_.valid(it->entity) ? registry_.try_get<WorldTransform2D>(it->entity) : nullptr;
        if (world == nullptr) {
            it = cutsceneMoves_.erase(it);
            continue;
        }
        const float dx = it->x - world->x, dy = it->y - world->y, distance = std::sqrt(dx * dx + dy * dy);
        const float step = it->speed * dt;
        if (ended || distance <= step || distance < 0.001f) {
            world->x = it->x;
            world->y = it->y;
            it = cutsceneMoves_.erase(it);
            continue;
        }
        world->x += dx / distance * step;
        world->y += dy / distance * step;
        registry_.emplace_or_replace<Facing>(it->entity, dx / distance, dy / distance);
        ++it;
    }
    // The camera's point of focus eases toward where it was sent.
    if (cameraGoal_) {
        if (!registry_.valid(cameraFocus_)) {
            cameraFocus_ = registry_.create();
            const auto player = FindPlayer(registry_);
            const auto start = player ? registry_.get<WorldTransform2D>(*player) : WorldTransform2D{};
            registry_.emplace<WorldTransform2D>(cameraFocus_, start);
            registry_.emplace<ScreenTransform2D>(cameraFocus_);
        }
        auto& focus = registry_.get<WorldTransform2D>(cameraFocus_);
        const float t = 1.0f - std::exp(-3.0f * dt);
        focus.x += (cameraGoal_->first - focus.x) * t;
        focus.y += (cameraGoal_->second - focus.y) * t;
    }
    if (fade_ != fadeTarget_) {
        const float step = fadeRate_ * dt;
        fade_ = fade_ < fadeTarget_ ? std::min(fadeTarget_, fade_ + step) : std::max(fadeTarget_, fade_ - step);
    }
    const float bars = !ended && cutscene_.Current().letterbox ? 1.0f : 0.0f;
    letterbox_ = letterbox_ < bars ? std::min(bars, letterbox_ + dt * 3.0f) : std::max(bars, letterbox_ - dt * 3.0f);
    if (subtitleSeconds_ > 0.0 && (subtitleSeconds_ -= dt) <= 0.0) subtitle_.clear();
    if (titleSeconds_ > 0.0 && (titleSeconds_ -= dt) <= 0.0) titleCard_.clear();
    UpdateSpriteFacing(registry_);
    UpdateWorldToScreen(registry_, iso_);
    if (ended) {
        // Back to play: the camera on the player, nothing left on screen.
        if (registry_.valid(cameraFocus_)) registry_.destroy(cameraFocus_);
        cameraFocus_ = entt::null;
        cameraFollow_ = entt::null;
        cameraGoal_.reset();
        cameraZoom_.reset();
        subtitle_.clear();
        titleCard_.clear();
        fade_ = fadeTarget_ = 0.0f;
        letterbox_ = 0.0f;
        Emit({"cutscene_ended", entt::null, entt::null, 0.0, cutscene_.Current().id});
        ResetClock();
    }
}

std::optional<Entity> GameSession::CameraTarget() {
    if (cutscene_.Active()) {
        if (registry_.valid(cameraFollow_)) return cameraFollow_;
        if (cameraGoal_ && registry_.valid(cameraFocus_)) return cameraFocus_;
    }
    return FindPlayer(registry_);
}

std::optional<std::pair<float, float>> GameSession::ObjectiveLocation() {
    if (storyLibrary_ == nullptr) return std::nullopt;
    for (const auto& id : story_.StartedOrder()) {
        const Quest* quest = storyLibrary_->FindQuest(id);
        const auto open = story_.OpenObjectives(id, *storyLibrary_);
        if (quest == nullptr || open.empty()) continue;
        const QuestObjective* objective = open.front();
        for (const QuestObjective* o : open) {
            if (!o->optional) {
                objective = o;
                break;
            }
        }
        if (!objective->scene.empty() && objective->scene != scene_) return std::nullopt; // elsewhere
        const std::string target = quest->Resolve(objective->target);
        if (objective->type == "reach") return MarkerPosition(target);
        if (objective->type == "escort") {
            // Bring them to the marker once they're with you.
            return MarkerPosition(quest->Resolve(objective->to));
        }
        if (auto entity = FindNamed(target)) {
            const auto& w = registry_.get<WorldTransform2D>(*entity);
            return std::pair{w.x, w.y};
        }
        if (objective->type == "collect") {
            for (auto [entity, pickup, w] : registry_.view<ItemPickup, WorldTransform2D>().each()) {
                if (pickup.item == target) return std::pair{w.x, w.y};
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// --- Abilities and magic (development_plan.md 19) --------------------------------------

bool GameSession::CastSlot(int slot, std::string* why) {
    const auto player = FindPlayer(registry_);
    if (!player) return false;
    const auto* abilities = registry_.try_get<Abilities>(*player);
    const auto slots = abilities ? SplitList(abilities->slots) : std::vector<std::string>{};
    if (slot < 0 || slot >= static_cast<int>(slots.size())) {
        if (why) *why = "nothing in that slot";
        return false;
    }
    // Aim: the nearest hostile in front within the ability's range, else straight ahead.
    const auto& at = registry_.get<WorldTransform2D>(*player);
    const auto* facing = registry_.try_get<Facing>(*player);
    const float fx = facing ? facing->x : 1.0f, fy = facing ? facing->y : 0.0f;
    const float range = FindAbility(slots[slot]) ? FindAbility(slots[slot])->range : 6.0f;
    float ax = at.x + fx * range, ay = at.y + fy * range;
    Entity target = entt::null;
    float best = range + 0.5f;
    for (auto [e, health, w] : registry_.view<Health, WorldTransform2D>(entt::exclude<Dead>).each()) {
        if (e == *player || !AreHostile(registry_, *player, e)) continue;
        const float dx = w.x - at.x, dy = w.y - at.y, d = std::sqrt(dx * dx + dy * dy);
        if (d < best && (dx * fx + dy * fy) >= 0.0f) {
            best = d;
            ax = w.x;
            ay = w.y;
            target = e;
        }
    }
    return magic_->Cast(*player, slots[slot], ax, ay, target, why);
}

void GameSession::StepPhysics(float dt, const SolidQueryFn& isSolid) {
    physics_->Step(registry_, isSolid, dt);
    for (const auto& t : UpdateTriggers(registry_)) {
        // trigger_enter / trigger_exit: the trigger, who, and its name as the text.
        Emit({t.enter ? "trigger_enter" : "trigger_exit", t.trigger, t.other, 0.0, t.name});
    }
}

void GameSession::AppendPhysicsDebugSprites(std::vector<SpriteItem>& out) {
    // Colliders yellow, triggers green, dynamic bodies orange, static bodies grey.
    static const float colours[4][3] = {{1.0f, 0.9f, 0.2f}, {0.3f, 1.0f, 0.4f}, {1.0f, 0.5f, 0.1f}, {0.6f, 0.6f, 0.6f}};
    for (const DebugShape& s : PhysicsDebugShapes(registry_)) {
        const ScreenPose pose = ProjectIso(s.x, s.y, iso_);
        SpriteItem item;
        item.owned = true;
        const float w = (s.circle ? s.radius : s.halfWidth) * 2.0f * iso_.tileWidth * (iso_.topDown ? 1.0f : 0.7f);
        const float h = (s.circle ? s.radius : s.halfHeight) * 2.0f * (iso_.topDown ? iso_.tileWidth : iso_.tileHeight * 1.4f);
        item.value.width = w;
        item.value.height = h;
        const auto& c = colours[std::clamp(s.kind, 0, 3)];
        item.value.r = c[0];
        item.value.g = c[1];
        item.value.b = c[2];
        item.value.a = 0.35f;
        item.value.texture = s.circle ? "fx:dot" : "";
        item.value.angle = s.angle;
        item.value.emissive = true;
        item.transform.x = pose.x;
        item.transform.y = pose.y + h * 0.5f;
        item.transform.depth = 1e9f;
        out.push_back(std::move(item));
    }
}

void GameSession::StepMagic(float dt, size_t firstEvent) {
    // Hits: an ability projectile's effects, a weapon's enchantment.
    for (size_t i = firstEvent; i < events_.size(); ++i) {
        const GameEvent event = events_[i];
        if (event.type == GameEvent::Type::Hit && registry_.valid(event.entity)) magic_->OnHit(event.other, event.entity, event.ability);
    }
    magic_->Update(dt);
    // Characters cast by their abilities' hints, a few times a second.
    if ((magicThink_ -= dt) <= 0.0f) {
        magicThink_ = 0.4f;
        if (const auto player = FindPlayer(registry_); player && !registry_.all_of<Dead>(*player)) {
            const auto& at = registry_.get<WorldTransform2D>(*player);
            std::vector<Entity> casters;
            for (auto [e, abilities, w] : registry_.view<Abilities, WorldTransform2D>(entt::exclude<Dead>).each()) {
                if (e != *player && std::hypot(w.x - at.x, w.y - at.y) < 12.0f) casters.push_back(e);
            }
            for (Entity e : casters) {
                if (AreHostile(registry_, e, *player)) magic_->ThinkFor(e, *player);
                else magic_->ThinkFor(e, entt::null); // allies heal and buff themselves
            }
        }
    }
}

// --- Animation and effects (development_plan.md 18) ---------------------------------

void GameSession::SetReduceMotion(bool on) {
    reduceMotion_ = on;
    ActiveFxAccessibility().shakeScale = on ? 0.0f : 1.0f;
    if (on) {
        trauma_ = 0.0f;
        slowSeconds_ = 0.0;
    }
}

void GameSession::SetReduceFlashes(bool on) { ActiveFxAccessibility().flashes = !on; }

void GameSession::Shake(float trauma) {
    trauma_ = std::clamp(trauma_ + trauma * ActiveFxAccessibility().shakeScale, 0.0f, 1.0f);
}

std::pair<float, float> GameSession::CameraShake() const {
    // Trauma squared, from smooth noise (Eiserloh, GDC 2016): a few pixels at most.
    const float k = trauma_ * trauma_ * 14.0f;
    const double t = shakeTime_ * 24.0;
    const float nx = static_cast<float>(std::sin(t * 1.3) * 0.6 + std::sin(t * 2.9 + 1.7) * 0.4);
    const float ny = static_cast<float>(std::sin(t * 1.7 + 0.5) * 0.6 + std::sin(t * 3.3 + 2.1) * 0.4);
    return {nx * k, ny * k};
}

bool GameSession::PlayEffect(const std::string& id, float x, float y, Entity target, Entity source) {
    auto effect = FindEffect(id);
    if (!effect) return false;
    for (const auto& part : effect->parts) {
        const auto& d = part.data;
        float px = x, py = y;
        Entity on = target;
        if (d.value("at", "target") == "source" && registry_.valid(source)) {
            on = source;
            if (const auto* w = registry_.try_get<WorldTransform2D>(source)) {
                px = w->x;
                py = w->y;
            }
        }
        if (part.kind == "particles") {
            if (auto def = FindParticles(d.value("particles", ""))) particles_.Emit(*def, px, py, d.value("seconds", 0.0f), on);
        } else if (part.kind == "sound") {
            if (playSoundAt_) playSoundAt_(d.value("sound", ""), px, py);
            else if (playSound_) playSound_(d.value("sound", ""));
        } else if (part.kind == "light") {
            const auto light = d.value("light", nlohmann::json::object());
            float c[4] = {1, 0.8f, 0.5f, 1};
            const std::string hex = light.value("color", "#ffcc88");
            if (hex.size() >= 7) {
                c[0] = std::stoi(hex.substr(1, 2), nullptr, 16) / 255.0f;
                c[1] = std::stoi(hex.substr(3, 2), nullptr, 16) / 255.0f;
                c[2] = std::stoi(hex.substr(5, 2), nullptr, 16) / 255.0f;
            }
            tempLights_.push_back({px, py, light.value("radius", 3.0f), c[0], c[1], c[2], light.value("seconds", 0.4f), 0.0f});
        } else if (part.kind == "flash") {
            if (ActiveFxAccessibility().flashes && registry_.valid(on)) {
                const auto flash = d.value("flash", nlohmann::json::object());
                const std::string hex = flash.value("color", "#ffffff");
                HitFlash f;
                f.remaining = flash.value("time", 0.08f);
                if (hex.size() >= 7) {
                    f.r = std::stoi(hex.substr(1, 2), nullptr, 16) / 255.0f;
                    f.g = std::stoi(hex.substr(3, 2), nullptr, 16) / 255.0f;
                    f.b = std::stoi(hex.substr(5, 2), nullptr, 16) / 255.0f;
                }
                registry_.emplace_or_replace<HitFlash>(on, f);
            }
        } else if (part.kind == "shake") {
            Shake(d.value("shake", 0.2f));
        } else if (part.kind == "hitstop") {
            HitStop(d.value("hitstop", 0.05f));
        } else if (part.kind == "slowmo") {
            const auto slow = d.value("slowmo", nlohmann::json::object());
            SlowMotion(slow.value("scale", 0.4f), slow.value("seconds", 0.6f));
        }
    }
    Emit({"effect", target, source, 0.0, id});
    return true;
}

void GameSession::CueEffects(size_t firstEvent) {
    for (size_t i = firstEvent; i < events_.size(); ++i) {
        const GameEvent event = events_[i];
        const bool alive = registry_.valid(event.entity);
        float x = event.x, y = event.y;
        if (alive) {
            if (const auto* w = registry_.try_get<WorldTransform2D>(event.entity)) {
                x = w->x;
                y = w->y;
            }
        }
        const auto* cues = alive ? registry_.try_get<EffectCues>(event.entity) : nullptr;
        if (event.type == GameEvent::Type::Attack && alive) {
            TriggerAnimation(registry_, event.entity, "attacking");
            if (cues && !cues->onAttack.empty()) PlayEffect(cues->onAttack, x, y, event.entity);
        } else if (event.type == GameEvent::Type::Hit && alive) {
            TriggerAnimation(registry_, event.entity, "hurt");
            if (cues && !cues->onHit.empty()) PlayEffect(cues->onHit, x, y, event.entity, event.other);
        } else if (event.type == GameEvent::Type::Death && cues && !cues->onDeath.empty()) {
            PlayEffect(cues->onDeath, x, y, event.entity, event.other);
        }
    }
}

void GameSession::StepFx(float dt) {
    // Becomes that waited for an animation.
    for (auto& pending : pendingBecomes_) pending.seconds -= dt;
    std::vector<PendingBecome> due;
    std::erase_if(pendingBecomes_, [&](const PendingBecome& p) {
        if (p.seconds > 0.0f) return false;
        due.push_back(p);
        return true;
    });
    for (const auto& p : due) {
        if (registry_.valid(p.target)) BecomePrefab(p.target, p.prefab);
    }
    // Animators (18.2) and the events on their frames.
    for (const auto& fired : UpdateAnimators(registry_, animations_, dt)) {
        float x = 0, y = 0;
        if (const auto* w = registry_.valid(fired.entity) ? registry_.try_get<WorldTransform2D>(fired.entity) : nullptr) {
            x = w->x;
            y = w->y;
        }
        if (fired.event.rfind("effect:", 0) == 0) PlayEffect(fired.event.substr(7), x, y, fired.entity);
        else if (fired.event.rfind("sound:", 0) == 0 && playSound_) playSound_(fired.event.substr(6));
        else if (fired.event == "footstep" && FindEffect("footstep")) PlayEffect("footstep", x, y, fired.entity);
        Emit({"anim_event", fired.entity, entt::null, 0.0, fired.event});
    }
    // Tweens (18.3).
    for (auto& tween : tweens_) {
        const ComponentInfo* component = EngineReflection().FindComponent(tween.component);
        const FieldInfo* field = component ? component->Field(tween.field) : nullptr;
        if (!field || !registry_.valid(tween.entity) || !component->has(registry_, tween.entity)) {
            tween.elapsed = tween.seconds;
            continue;
        }
        if (!tween.started) {
            const FieldValue now = field->get(registry_, tween.entity);
            tween.from = std::holds_alternative<float>(now) ? std::get<float>(now)
                         : std::holds_alternative<int>(now) ? static_cast<float>(std::get<int>(now)) : 0.0f;
            tween.started = true;
        }
        tween.elapsed += dt;
        const float value = tween.from + (tween.to - tween.from) * Ease(tween.ease, tween.elapsed / std::max(0.001f, tween.seconds));
        if (field->type == FieldType::Int) field->set(registry_, tween.entity, static_cast<int>(std::lround(value)));
        else field->set(registry_, tween.entity, value);
    }
    std::erase_if(tweens_, [](const Tween& t) { return t.started && t.elapsed >= t.seconds; });
    // Weather around the player (18.6).
    for (auto [entity, weather] : registry_.view<Weather>().each()) {
        static const char* presets[] = {"", "rain", "snow", "", "dust"};
        if (weather.kind <= 0 || weather.kind > 4 || std::string(presets[weather.kind]).empty()) break;
        if (const auto player = FindPlayer(registry_)) {
            const auto& at = registry_.get<WorldTransform2D>(*player);
            if (auto def = FindParticles(presets[weather.kind])) particles_.Stream(*def, at.x, at.y, dt, weather.intensity * 2.0f);
        }
        break;
    }
    particles_.Update(registry_, dt);
    for (auto& light : tempLights_) light.age += dt;
    std::erase_if(tempLights_, [](const TempLight& l) { return l.age >= l.seconds; });
}

void GameSession::StepPreviewFx(float dt) {
    trauma_ = std::max(0.0f, trauma_ - dt * 1.1f);
    shakeTime_ += dt;
    freeze_ = 0.0;
    particles_.Update(registry_, dt);
    for (auto& light : tempLights_) light.age += dt;
    std::erase_if(tempLights_, [](const TempLight& l) { return l.age >= l.seconds; });
}

int GameSession::StartTween(Entity entity, const std::string& componentField, float to, float seconds, const std::string& ease) {
    const auto dot = componentField.find('.');
    if (dot == std::string::npos || !registry_.valid(entity)) return 0;
    Tween tween;
    tween.id = nextTween_++;
    tween.entity = entity;
    tween.component = componentField.substr(0, dot);
    tween.field = componentField.substr(dot + 1);
    tween.to = to;
    tween.seconds = std::max(0.0f, seconds);
    tween.ease = ease;
    tweens_.push_back(tween);
    return tween.id;
}

bool GameSession::TweenDone(int id) const {
    return std::none_of(tweens_.begin(), tweens_.end(), [&](const Tween& t) { return t.id == id; });
}

LightingFrame GameSession::Lighting() const {
    LightingFrame frame;
    const auto& settings = ActiveSettings().lighting;
    frame.enabled = settings.enabled;
    if (!frame.enabled) return frame;
    AmbientAt(MinuteOfDay(), frame.ambient, &frame.darkness);
    // Weather dims the day: fog and rain grey it a little.
    for (auto [entity, weather] : registry_.view<Weather>().each()) {
        const float dim = weather.kind == 3 ? 0.25f : weather.kind == 1 || weather.kind == 4 ? 0.15f : 0.0f;
        for (float& c : frame.ambient) c *= 1.0f - dim * weather.intensity;
        break;
    }
    const float pxPerCell = iso_.tileWidth * 0.6f;
    for (auto [entity, light, world] : registry_.view<Light2D, WorldTransform2D>().each()) {
        if (light.nightOnly && frame.darkness < 0.3f) continue;
        float intensity = light.intensity * (light.nightOnly ? frame.darkness : 1.0f);
        if (light.flicker > 0.0f) {
            const double t = shakeTime_ * 9.0 + static_cast<double>(static_cast<uint32_t>(entity)) * 1.7;
            intensity *= 1.0f - light.flicker * 0.35f * static_cast<float>(0.5 + 0.5 * std::sin(t) * std::sin(t * 2.3));
        }
        const ScreenPose pose = ProjectIso(world.x, world.y, iso_);
        frame.lights.push_back({pose.x, pose.y, light.radius * pxPerCell, light.r, light.g, light.b, intensity});
    }
    for (const auto& l : tempLights_) {
        const ScreenPose pose = ProjectIso(l.x, l.y, iso_);
        frame.lights.push_back({pose.x, pose.y, l.radius * pxPerCell, l.r, l.g, l.b, 1.0f - l.age / std::max(0.01f, l.seconds)});
    }
    return frame;
}

// --- Gameplay systems (development_plan.md 13) ----------------------------------

int GameSession::LevelOf(Entity entity) const {
    if (const auto* progression = registry_.try_get<Progression>(entity)) return progression->level;
    return 1;
}

void GameSession::StepGameplay(float dt, const SolidQueryFn& isSolid, bool firstStep) {
    UpdateAttributes(registry_, dt);
    const auto player = FindPlayer(registry_);
    UpdateInteriors(registry_, player); // inside a building: its interior shows (Phase 27)
    if (player && !registry_.all_of<Dead>(*player)) {
        // Block while held; dodge, fire on press (once a frame).
        if (input_.Down("block") && !registry_.all_of<Dodging>(*player)) registry_.emplace_or_replace<Blocking>(*player);
        else registry_.remove<Blocking>(*player);
        for (int slot = 0; slot < 4 && firstStep; ++slot) {
            if (input_.Pressed("ability_" + std::to_string(slot + 1))) {
                std::string why;
                if (!CastSlot(slot, &why) && !why.empty()) ShowNotice(why, 1.5, "alert");
            }
        }
        if (firstStep && input_.Pressed("dodge")) {
            // The way the player walks (screen directions to the world's).
            const float right = input_.Value("move_x"), down = input_.Value("move_y");
            const bool topDown = ActiveSettings().grid.style == GridStyle::TopDown;
            StartDodge(registry_, *player, topDown ? right : right + down, topDown ? down : down - right);
        }
        const bool shoots = registry_.all_of<RangedAttack>(*player);
        if (firstStep && shoots &&
            (input_.Pressed("fire") || (input_.Pressed("attack") && !registry_.all_of<MeleeAttack>(*player)))) {
            if (TryRangedAttack(registry_, *player, 0.0f, 0.0f)) events_.push_back({GameEvent::Type::Attack, *player});
        }
        // Walking over items picks them up.
        for (const auto& taken : CollectItemPickups(registry_, *player)) {
            ShowNotice("Picked up " + taken, 2.0, "feed", taken);
            Emit({"item_added", *player, entt::null, 0.0, taken});
        }
    }
    // Archers: a hostile with a ranged attack shoots at the player in range, in sight.
    if (player && !registry_.all_of<Dead>(*player)) {
        const auto target = registry_.get<WorldTransform2D>(*player);
        for (auto [entity, ranged, world] : registry_.view<RangedAttack, WorldTransform2D>(entt::exclude<Dead>).each()) {
            if (entity == *player || ranged.cooldownRemaining > 0.0f || !AreHostile(registry_, entity, *player)) continue;
            const float dx = target.x - world.x, dy = target.y - world.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance > ranged.range * 0.9f || distance < 0.3f) continue;
            bool clear = true; // line of sight over the grid
            for (float t = 0.5f; t < distance; t += 0.5f) {
                if (isSolid(static_cast<int>(std::floor(world.x + dx / distance * t)),
                            static_cast<int>(std::floor(world.y + dy / distance * t)))) {
                    clear = false;
                    break;
                }
            }
            if (clear && TryRangedAttack(registry_, entity, dx, dy)) {
                const auto* sounds = registry_.try_get<Sounds>(entity);
                events_.push_back({GameEvent::Type::Attack, entity, entt::null, 0.0f, sounds ? sounds->attack : std::string()});
            }
        }
    }
    // Spawners (13.7).
    if (spawner_ && !registry_.view<Spawner>().empty()) {
        if (!UpdateSpawners(registry_, dt, spawner_, [&](float x, float y) {
                 return !isSolid(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
             }, gameplayRng_).empty()) {
            difficultyDirty_ = true;
        }
    }
}

void GameSession::OnDeaths(size_t firstEvent) {
    const size_t end = events_.size();
    for (size_t i = firstEvent; i < end; ++i) {
        const GameEvent event = events_[i]; // copied: pushing below may move the vector
        if (event.type != GameEvent::Type::Death) continue;
        // Adaptive difficulty and growing assists (16.4) follow the player's deaths and wins.
        if (const auto player = FindPlayer(registry_)) {
            const int before = adaptive_.shift;
            if (event.entity == *player) {
                adaptive_.OnPlayerDeath();
                ++ActiveAssists().deaths;
            } else if (event.other == *player && event.team == Team::Enemy) {
                adaptive_.OnFightWon(playerHurtSinceKill_);
                playerHurtSinceKill_ = false;
            }
            if (adaptive_.shift != before && adaptiveOn_ && ActiveSettings().difficulty.adaptive) difficultyDirty_ = true;
        }
        // Experience to the killer (13.2), times the difficulty's reward.
        if (event.experience > 0.0f && registry_.valid(event.other)) {
            const int levels = AwardExperience(registry_, event.other, event.experience * XpScale());
            if (levels > 0) {
                Emit({"level_up", event.other, entt::null, static_cast<double>(LevelOf(event.other)), ""});
                if (auto player = FindPlayer(registry_); player && *player == event.other) {
                    ShowNotice("Level " + std::to_string(LevelOf(event.other)) + "!", 3.0, "banner");
                }
            }
        }
        // Loot where it fell (13.4).
        if (!event.loot.empty() || event.gold > 0) {
            const int level = registry_.valid(event.other) ? LevelOf(event.other) : 1;
            LootResult loot = event.loot.empty() ? LootResult{} : RollLoot(event.loot, level, gameplayRng_);
            loot.gold += event.gold;
            if (!loot.items.empty() || loot.gold > 0) DropLoot(registry_, event.x, event.y, loot.items, loot.gold, gameplayRng_);
        }
    }
}

// --- Character AI (development_plan.md 14) ------------------------------------------

void GameSession::ShowBark(Entity speaker, const std::string& text) {
    if (text.empty()) return;
    // One of the '|' alternatives.
    std::vector<std::string> options;
    std::stringstream in(text);
    std::string part;
    while (std::getline(in, part, '|')) {
        if (!part.empty()) options.push_back(part);
    }
    if (options.empty()) return;
    const std::string line = options[std::uniform_int_distribution<size_t>(0, options.size() - 1)(gameplayRng_)];
    const auto* name = registry_.valid(speaker) ? registry_.try_get<Name>(speaker) : nullptr;
    const auto* talk = registry_.valid(speaker) ? registry_.try_get<Talk>(speaker) : nullptr;
    std::string who = talk ? NameOf(talk->character, speaker) : name ? name->value : std::string();
    barkText_ = who.empty() ? line : who + ": " + line;
    barkSeconds_ = 2.5 + line.size() / 25.0;
    Emit({"bark", speaker, entt::null, 0.0, line});
}

bool GameSession::RunScriptedPackage(Entity actor, const std::string& script, float dt) {
    (void)dt;
    auto known = scriptedPackages_.find(actor);
    if (known == scriptedPackages_.end() || known->second != script) {
        // Load it into the actor's own environment, with the whole API, and run run().
        // (Binding forgets the actor's earlier handlers and tasks, so it comes first.)
        sol::state& lua = lua_.State();
        sol::environment env(lua, sol::create, lua.globals());
        BindScriptApi(env, actor);
        scriptedPackages_[actor] = script;
        auto result = lua.safe_script_file((scriptRoot_ / script).string(), env, sol::script_pass_on_error);
        if (!result.valid()) {
            sol::error error = result;
            ScriptFailed(actor, error.what());
            return false;
        }
        sol::protected_function run = env["run"];
        if (!run.valid()) {
            ScriptFailed(actor, script + ": a scripted package needs a run() function");
            return false;
        }
        StartTask(actor, run, sol::lua_nil);
        scriptEnvs_[actor] = env;
    }
    for (const auto& task : tasks_) {
        if (task->owner == actor) return true;
    }
    return false;
}

void GameSession::StepCharacters(float dt, const SolidQueryFn& isSolid) {
    if (barkSeconds_ > 0.0) {
        barkSeconds_ -= dt;
        if (barkSeconds_ <= 0.0) barkText_.clear();
    }
    if (registry_.view<Behaviour>().empty()) {
        noises_.clear();
        return;
    }
    AiContext context{registry_, isSolid};
    context.minuteOfDay = MinuteOfDay();
    context.day = Day();
    context.story = &story_;
    context.scene = scene_;
    context.rng = &gameplayRng_;
    context.noises = std::move(noises_);
    noises_.clear();
    context.bark = [this](Entity speaker, const std::string& text) { ShowBark(speaker, text); };
    context.startDialogue = [this](Entity speaker, const std::string& path) {
        if (!storyLibrary_ || conversation_.Active()) return;
        if (const Dialogue* dialogue = storyLibrary_->FindDialogue(path)) {
            if (conversation_.Start(*dialogue, story_, *storyLibrary_)) talkingTo_ = speaker;
        }
    };
    context.emit = [this](const std::string& event, Entity entity) { Emit({event, entity}); };
    context.act = [this](Entity actor, Entity target) { return Act(actor, target); };
    context.runScript = [this](Entity actor, const std::string& script, float stepDt) {
        return RunScriptedPackage(actor, script, stepDt);
    };
    UpdateBehaviours(context, dt);
}

std::optional<GameSession::InteractTarget> GameSession::CurrentInteraction() {
    const auto player = FindPlayer(registry_);
    if (!player || conversation_.Active()) return std::nullopt;
    if (auto npc = TalkTarget()) return InteractTarget{*npc, true};
    if (auto thing = FindInteractable(registry_, *player)) return InteractTarget{*thing, false};
    return std::nullopt;
}

void GameSession::UseInteractable(Entity target, Entity user) {
    const auto player = FindPlayer(registry_);
    if (user == entt::null) {
        if (!player) return;
        user = *player;
    }
    if (!registry_.valid(user)) return;
    const bool byPlayer = player && user == *player;
    const InteractResult result = Interact(registry_, user, target, story_, LevelOf(user), gameplayRng_);
    if (byPlayer && !result.text.empty()) {
        ShowNotice(result.text, 4.0 + result.text.size() / 30.0); // time to read it
    } else if (byPlayer && !result.message.empty()) {
        ShowNotice(result.message, 2.5, "alert");
    }
    if (!result.event.empty()) Emit({result.event, target, user, 0.0, ""});
    if (result.used) Emit({"interact", registry_.valid(target) ? target : Entity{entt::null}, user, 0.0, ""});
    if (result.used && byPlayer && storyLibrary_ != nullptr) {
        for (const auto& name : NamesOf(target)) story_.OnUsed(name, *storyLibrary_); // use/examine objectives
    }
    if (result.openShop && byPlayer) shopRequest_ = target;
    // Its animation (its own, else its verb's: Open plays open), and its becomes once that has played.
    float playing = 0.0f;
    if (result.used && registry_.valid(target)) {
        if (const auto* thing = registry_.try_get<Interactable>(target)) {
            std::string clip = thing->animation;
            if (clip.empty()) {
                for (char c : thing->verb) clip += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            playing = AnimationLength(registry_, animations_, target, clip);
            if (playing > 0.0f) TriggerAnimation(registry_, target, clip);
            if (!thing->becomeAfterAnimation) playing = 0.0f;
        }
    }
    if (!result.becomes.empty() && registry_.valid(target)) {
        if (playing > 0.0f) pendingBecomes_.push_back({target, result.becomes, playing});
        else BecomePrefab(target, result.becomes);
    }
}

bool GameSession::Act(Entity user, Entity target) {
    if (!registry_.valid(target)) return false;
    const auto* action = registry_.try_get<Action>(target);
    if (!action) {
        if (!registry_.all_of<Interactable>(target)) return false;
        UseInteractable(target, user);
        return true;
    }
    auto& state = registry_.get_or_emplace<ActionState>(target);
    if ((action->once && state.done) || state.cooling > 0.0f) return false;
    const auto player = FindPlayer(registry_);
    const bool byPlayer = player && user == *player;
    const bool hasUser = registry_.valid(user);
    if (hasUser && action->trigger == "input") {
        const auto* a = registry_.try_get<WorldTransform2D>(user);
        const auto* b = registry_.try_get<WorldTransform2D>(target);
        if (a && b && std::hypot(a->x - b->x, a->y - b->y) > action->range + 0.25f) return false;
    }
    // What it needs from its user.
    auto* inventory = hasUser ? registry_.try_get<Inventory>(user) : nullptr;
    if (!action->requiresItem.empty() && (!inventory || CountItem(*inventory, action->requiresItem) < 1)) {
        if (byPlayer) ShowNotice("Needs " + action->requiresItem, 2.5, "alert");
        return false;
    }
    if (!action->requiresFlag.empty() && !story_.Flag(action->requiresFlag)) return false;
    // Animations, then what it takes and gives.
    float playing = 0.0f;
    if (hasUser && !action->userAnimation.empty()) {
        playing = std::max(playing, AnimationLength(registry_, animations_, user, action->userAnimation));
        TriggerAnimation(registry_, user, action->userAnimation);
    }
    if (!action->animation.empty()) {
        playing = std::max(playing, AnimationLength(registry_, animations_, target, action->animation));
        TriggerAnimation(registry_, target, action->animation);
    }
    if (hasUser) {
        if (const auto* at = registry_.try_get<WorldTransform2D>(target); at && registry_.all_of<WorldTransform2D>(user)) {
            const auto& from = registry_.get<WorldTransform2D>(user);
            const float dx = at->x - from.x, dy = at->y - from.y, l = std::hypot(dx, dy);
            if (l > 0.01f) registry_.emplace_or_replace<Facing>(user, dx / l, dy / l); // it turns to what it uses
        }
        for (const auto& stack : ParseItems(action->takes)) {
            if (!inventory) break;
            if (stack.item == "gold") inventory->gold = std::max(0, inventory->gold - stack.count);
            else RemoveItem(*inventory, stack.item, stack.count);
        }
        if (!action->gives.empty()) {
            auto& pockets = registry_.get_or_emplace<Inventory>(user);
            for (const auto& stack : ParseItems(action->gives)) {
                if (stack.item == "gold") {
                    pockets.gold += stack.count;
                } else if (const int n = AddItem(pockets, stack.item, stack.count); n > 0) {
                    Emit({"item_added", user, target, static_cast<double>(n), stack.item});
                }
            }
        }
    }
    // Effects: on the thing, on its user.
    if (!action->effect.empty()) {
        if (const auto* at = registry_.try_get<WorldTransform2D>(target)) PlayEffect(action->effect, at->x, at->y, target, hasUser ? user : Entity{entt::null});
    }
    if (hasUser && !action->userEffect.empty()) {
        if (const auto* at = registry_.try_get<WorldTransform2D>(user)) PlayEffect(action->userEffect, at->x, at->y, user, target);
    }
    if (!action->setFlag.empty()) story_.SetFlag(action->setFlag, true);
    if (!action->emits.empty()) Emit({action->emits, target, hasUser ? user : Entity{entt::null}, 0.0, action->action});
    if (byPlayer && !action->text.empty()) ShowNotice(action->text, 3.0 + action->text.size() / 30.0);
    state.done = true;
    state.cooling = action->cooldown;
    // Becomes, once the animations have played.
    if (!action->becomes.empty()) {
        if (playing > 0.0f) pendingBecomes_.push_back({target, action->becomes, playing});
        else BecomePrefab(target, action->becomes);
    }
    return true;
}

std::optional<Entity> GameSession::ActionTarget(Entity user, const std::string& action, float radius) const {
    auto& registry = const_cast<Registry&>(registry_);
    const auto* at = registry.valid(user) ? registry.try_get<WorldTransform2D>(user) : nullptr;
    if (!at) return std::nullopt;
    std::optional<Entity> best;
    float bestDistance = 1e9f;
    for (auto [entity, world] : registry.view<WorldTransform2D>().each()) {
        if (entity == user) continue;
        if (const auto* a = registry.try_get<Action>(entity)) {
            if (a->trigger != "input" || a->action != action) continue;
            if (const auto* s = registry.try_get<ActionState>(entity); s && ((a->once && s->done) || s->cooling > 0.0f)) continue;
        } else if (!(action == "interact" && registry.all_of<Interactable>(entity))) {
            continue;
        }
        const float d = std::hypot(world.x - at->x, world.y - at->y);
        if (d <= radius && d < bestDistance) best = entity, bestDistance = d;
    }
    return best;
}

void GameSession::StepActions(float dt) {
    const auto player = FindPlayer(registry_);
    // Input actions that play a clip on the player or emit an event (Project Settings ▸ Input).
    if (player) {
        for (const auto& a : ActiveInputMap().actions) {
            if (a.axis || (a.plays.empty() && a.event.empty() && a.effect.empty()) || !input_.Pressed(a.name)) continue;
            if (!a.plays.empty()) TriggerAnimation(registry_, *player, a.plays);
            if (!a.effect.empty()) {
                if (const auto* at = registry_.try_get<WorldTransform2D>(*player)) PlayEffect(a.effect, at->x, at->y, *player);
            }
            if (!a.event.empty()) Emit({a.event, *player, Entity{entt::null}, 0.0, a.name});
        }
    }
    // Pressed: the nearest thing with that action within its range (one a press).
    std::map<std::string, std::pair<Entity, float>> pressed;
    std::vector<std::pair<Entity, Entity>> due; // (user, target)
    const auto* at = player ? registry_.try_get<WorldTransform2D>(*player) : nullptr;
    std::vector<Entity> things;
    for (auto [entity, action] : registry_.view<Action>().each()) things.push_back(entity);
    for (Entity entity : things) {
        if (!registry_.valid(entity)) continue;
        const auto& action = registry_.get<Action>(entity);
        auto& state = registry_.get_or_emplace<ActionState>(entity);
        state.age += dt;
        state.cooling = std::max(0.0f, state.cooling - dt);
        const auto* world = registry_.try_get<WorldTransform2D>(entity);
        const float d = at && world ? std::hypot(world->x - at->x, world->y - at->y) : 1e9f;
        if (action.trigger == "input") {
            if (player && input_.Pressed(action.action) && d <= action.range) {
                auto it = pressed.find(action.action);
                if (it == pressed.end() || d < it->second.second) pressed[action.action] = {entity, d};
            }
        } else if (action.trigger == "timer") {
            if (state.age >= action.seconds && !(action.once && state.done) && state.cooling <= 0.0f) {
                due.push_back({Entity{entt::null}, entity});
                state.age = 0.0f; // again after as long (unless once)
            }
        } else if (action.trigger == "near") {
            // Someone (anyone moving) within range: it happens as the first comes; its away clip once they've all gone.
            bool someone = false;
            Entity who{entt::null};
            if (world) {
                for (auto [mover, v, mw] : registry_.view<Velocity2D, WorldTransform2D>().each()) {
                    if (mover != entity && std::hypot(mw.x - world->x, mw.y - world->y) <= action.range) {
                        someone = true;
                        who = mover;
                        break;
                    }
                }
            }
            if (someone && !state.touching) due.push_back({who, entity});
            if (!someone && state.touching && !action.awayAnimation.empty()) TriggerAnimation(registry_, entity, action.awayAnimation);
            state.touching = someone;
        } else if (action.trigger == "touch") {
            const bool on = d <= std::max(0.5f, action.range * 0.5f);
            if (on && !state.touching && player) due.push_back({*player, entity});
            state.touching = on;
        } else if (action.trigger == "event") {
            for (size_t i = 0; i < actionEvents_.size(); ++i) {
                if (actionEvents_[i] == action.event) {
                    due.push_back({actionEventUsers_[i].second, entity});
                    break;
                }
            }
        }
    }
    actionEvents_.clear();
    actionEventUsers_.clear();
    // Animated parts (a building's doors; docs/assets/animations.md §4.2, §4.3). Each door opens its project's way or
    // its own: auto, as someone comes near; use, for the player's door shortcuts (Project Settings ▸ Game ▸
    // doorActions) or a double click on it, near it, and for other characters as they come. A locked one opens only
    // for the player carrying its key (then it's unlocked for good). It closes once everyone has gone. Near is measured
    // from the door itself (its foot). Each door's doorway (the footprint's edge cell nearest it) is passable while
    // it's open, or opens by itself and isn't locked (PartOpen, read by TileGrid and characters' paths).
    {
        struct DoorAt {
            Entity owner, overlay;
            size_t index;
            float x, y;
            bool placed; // where it's drawn is known (else its building's middle, until it's drawn)
        };
        std::vector<DoorAt> doors;
        for (auto [overlay, link] : registry_.view<AttachmentLink, Transient>().each()) {
            if (link.slot.rfind("part", 0) != 0 || !registry_.valid(link.parent)) continue;
            const size_t index = link.slot.size() > 4 ? static_cast<size_t>(std::atoi(link.slot.c_str() + 4)) : 0;
            const auto* w = registry_.try_get<WorldTransform2D>(link.parent);
            if (!w || !registry_.all_of<AnimatedPart>(link.parent)) continue;
            float dx = w->x, dy = w->y;
            const auto* screen = registry_.try_get<ScreenTransform2D>(overlay);
            const auto* sprite = registry_.try_get<Sprite>(overlay);
            const bool placed = screen && sprite && sprite->height > 0.0f;
            if (placed) UnprojectIso(screen->x, screen->y + sprite->height * 0.5f, iso_, dx, dy);
            doors.push_back({link.parent, overlay, index, dx, dy, placed});
        }
        bool doorKey = false; // a door shortcut pressed this step
        {
            std::stringstream names(ActiveSettings().game.doorActions);
            for (std::string name; std::getline(names, name, ',');) {
                name.erase(0, name.find_first_not_of(' '));
                name.erase(name.find_last_not_of(' ') + 1);
                if (!name.empty() && input_.Pressed(name)) doorKey = true;
            }
        }
        const auto click = requestedUse_;
        requestedUse_.reset();
        // Each building's doors as they are now, read once; written back when one is unlocked.
        std::map<Entity, std::vector<AnimatedPart>> all;
        for (const auto& d : doors) {
            if (!all.contains(d.owner)) all[d.owner] = DoorsOf(registry_.get<AnimatedPart>(d.owner));
        }
        // The one door the player's shortcut (or double click) is for: the nearest within its range.
        const DoorAt* used = nullptr;
        float usedDistance = 1e9f;
        for (const auto& d : doors) {
            const auto& list = all[d.owner];
            if (!at || d.index >= list.size()) continue;
            const float dist = std::hypot(at->x - d.x, at->y - d.y);
            const auto* screen = registry_.try_get<ScreenTransform2D>(d.overlay);
            const auto* sprite = registry_.try_get<Sprite>(d.overlay);
            const bool clicked = click && screen && sprite && std::abs(click->first - screen->x) <= sprite->width * 0.5f &&
                                 std::abs(click->second - screen->y) <= sprite->height * 0.5f;
            if (dist <= list[d.index].range + (clicked ? 1.0f : 0.0f) && (doorKey || clicked) && dist < usedDistance) used = &d, usedDistance = dist;
        }
        // The prompt (the HUD's, as at a chest): the nearest door opened by use within its range.
        doorPrompt_.clear();
        float promptDistance = 1e9f;
        for (const auto& d : doors) {
            const auto& list = all[d.owner];
            if (!at || d.index >= list.size()) continue;
            const AnimatedPart& door = list[d.index];
            const float dist = std::hypot(at->x - d.x, at->y - d.y);
            if (dist > door.range || dist >= promptDistance) continue;
            const auto* state = registry_.try_get<PartOpen>(d.owner);
            const bool open = state && d.index < state->open.size() && state->open[d.index];
            if (door.locked) {
                auto* inventory = registry_.try_get<Inventory>(*player);
                const bool hasKey = !door.key.empty() && inventory && CountItem(*inventory, door.key) > 0;
                doorPrompt_ = hasKey ? "Unlock the door" : door.key.empty() ? "Locked" : "Locked: needs " + ItemName(door.key);
            } else if (DoorOpens(door) == "use") {
                doorPrompt_ = open ? "Close the door" : "Open the door";
            } else {
                continue;
            }
            promptDistance = dist;
        }
        std::set<Entity> unlocked;
        for (const auto& d : doors) {
            auto& list = all[d.owner];
            if (d.index >= list.size()) continue;
            AnimatedPart& door = list[d.index];
            auto& state = registry_.get_or_emplace<PartOpen>(d.owner);
            if (state.open.size() < list.size()) state.open.resize(list.size(), 0);
            if (state.passable.size() < list.size()) state.passable.resize(list.size(), 0);
            if (state.cells.size() < list.size()) state.cells.resize(list.size(), {INT_MIN, INT_MIN});
            // Its doorway: the footprint's edge cell nearest the door.
            if (const auto* interior = registry_.try_get<Interior>(d.owner)) {
                int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                if (int ix = 0, iy = 0; !d.placed && d.index == 0 && InteriorDoor(registry_, d.owner, *interior, ix, iy)) {
                    state.cells[d.index] = {ix, iy}; // not drawn yet: the first door at the interior's own doorway
                } else if (InteriorFootprint(registry_, d.owner, *interior, x0, y0, x1, y1)) {
                    const int cx = std::clamp(static_cast<int>(std::floor(d.x)), x0, x1), cy = std::clamp(static_cast<int>(std::floor(d.y)), y0, y1);
                    const int toEdge[4] = {cx - x0, x1 - cx, cy - y0, y1 - cy};
                    const int nearest = static_cast<int>(std::min_element(toEdge, toEdge + 4) - toEdge);
                    state.cells[d.index] = nearest == 0 ? std::make_pair(x0, cy) : nearest == 1 ? std::make_pair(x1, cy)
                                         : nearest == 2 ? std::make_pair(cx, y0) : std::make_pair(cx, y1);
                }
            }
            const bool byUse = DoorOpens(door) == "use";
            bool playerNear = false, othersNear = false;
            for (auto [mover, v, mw] : registry_.view<Velocity2D, WorldTransform2D>().each()) {
                if (std::hypot(mw.x - d.x, mw.y - d.y) > door.range) continue;
                (player && mover == *player ? playerNear : othersNear) = true;
            }
            char& open = state.open[d.index];
            bool opening = false;
            if (used == &d) {
                if (door.locked) {
                    auto* inventory = registry_.try_get<Inventory>(*player);
                    if (door.key.empty() || !inventory || CountItem(*inventory, door.key) <= 0) {
                        Toast(door.key.empty() ? "Locked." : "Locked: needs " + ItemName(door.key) + ".");
                    } else {
                        if (door.consumeKey) RemoveItem(*inventory, door.key, 1);
                        door.locked = false;
                        unlocked.insert(d.owner);
                        Toast("Unlocked with " + ItemName(door.key) + ".");
                        opening = true;
                    }
                } else if (open && byUse) {
                    open = 0; // by use: the shortcut opens it, or shuts it again
                    TriggerAnimation(registry_, d.overlay, door.closeClip);
                    state.passable[d.index] = 0;
                    continue;
                } else {
                    opening = true;
                }
            }
            if (!door.locked && (byUse ? othersNear : (playerNear || othersNear))) opening = true;
            if (opening && !open) {
                open = 1;
                TriggerAnimation(registry_, d.overlay, door.clip);
            } else if (open && !playerNear && !othersNear) {
                open = 0; // everyone gone: it closes
                TriggerAnimation(registry_, d.overlay, door.closeClip);
            }
            state.passable[d.index] = open || (!door.locked && !byUse);
        }
        for (Entity owner : unlocked) SetDoors(registry_.get<AnimatedPart>(owner), all[owner]);
    }
    // interact is the Interactables' key too: an Action on it acts only when no Interactable is nearer.
    for (const auto& [name, hit] : pressed) {
        if (name == "interact") {
            if (const auto thing = FindInteractable(registry_, *player)) {
                const auto* w = registry_.try_get<WorldTransform2D>(*thing);
                if (w && at && std::hypot(w->x - at->x, w->y - at->y) < hit.second) continue;
            }
        }
        due.push_back({*player, hit.first});
    }
    for (const auto& [user, target] : due) Act(user, target);
}

std::optional<Entity> GameSession::BecomePrefab(Entity target, const std::string& prefab) {
    // Replaced in its place by `prefab` (an opened chest): turned and mirrored as it was, what's left in it kept. A
    // saved game keeps it (the old one removed, the new one spawned).
    const auto* at = registry_.try_get<WorldTransform2D>(target);
    if (at == nullptr || !spawner_) return std::nullopt;
    const auto made = spawner_(prefab, at->x, at->y);
    if (!made || !registry_.valid(*made)) return std::nullopt;
    if (const auto* was = registry_.try_get<Sprite>(target)) {
        if (auto* now = registry_.try_get<Sprite>(*made)) {
            now->rotation = was->rotation, now->angle = was->angle;
            if ((was->u0 > was->u1) != (now->u0 > now->u1)) std::swap(now->u0, now->u1); // mirrored as it was
        }
    }
    if (const auto* was = registry_.try_get<Container>(target)) {
        auto& now = registry_.get_or_emplace<Container>(*made);
        now.items = was->items, now.gold = was->gold, now.looted = was->looted;
    }
    registry_.destroy(target);
    return made;
}

// --- Events, timers and coroutines (12.2) -------------------------------------

void GameSession::ScriptFailed(Entity owner, const std::string& message) {
    std::string path;
    if (registry_.valid(owner)) {
        if (const auto* script = registry_.try_get<LuaScript>(owner)) path = script->path;
    }
    if (scriptErrors_.size() < 64) scriptErrors_.push_back({owner, path, message});
}

void GameSession::ForgetScript(Entity owner) {
    scriptedPackages_.erase(owner);
    scriptEnvs_.erase(owner);
    std::erase_if(handlers_, [&](const ScriptHandler& h) { return h.owner == owner; });
    std::erase_if(timers_, [&](const ScriptTimer& t) { return t.owner == owner; });
    std::erase_if(tasks_, [&](const std::unique_ptr<ScriptTask>& t) { return t->owner == owner; });
}

void GameSession::StartTask(Entity owner, const sol::protected_function& fn, const sol::object& argument) {
    auto task = std::make_unique<ScriptTask>();
    task->owner = owner;
    task->thread = sol::thread::create(lua_.State().lua_state());
    task->co = sol::coroutine(task->thread.thread_state(), fn);
    ResumeTask(*task, argument);
    if (task->co.status() == sol::call_status::yielded) tasks_.push_back(std::move(task));
}

void GameSession::ResumeTask(ScriptTask& task, const sol::object& argument) {
    auto result = argument.valid() && argument.get_type() != sol::type::lua_nil ? task.co(argument) : task.co();
    if (!result.valid()) {
        sol::error error = result;
        ScriptFailed(task.owner, error.what());
        return;
    }
    if (task.co.status() == sol::call_status::yielded) {
        // wait(seconds) yields how long; anything else waits one step.
        task.wait = result.return_count() >= 1 && result.get<sol::object>(0).is<double>() ? result.get<double>(0) : 0.0;
    }
}

void GameSession::DispatchEvents() {
    // Handlers may emit more; a cap keeps a loop of events from hanging the game.
    for (int round = 0; round < 8 && !pendingEvents_.empty(); ++round) {
        std::vector<ScriptEvent> batch;
        batch.swap(pendingEvents_);
        for (const auto& event : batch) {
            delivered_.push_back(event);
            actionEvents_.push_back(event.name);
            actionEventUsers_.push_back({event.entity, event.other});
            // Achievements (24): what the event is about, by name and by text (a prefab, a trigger).
            {
                std::vector<std::string> about = {event.text};
                if (registry_.valid(event.entity)) {
                    if (const auto* n = registry_.try_get<Name>(event.entity)) about.push_back(n->value);
                }
                if (registry_.valid(event.other)) {
                    if (const auto* n = registry_.try_get<Name>(event.other)) about.push_back(n->value);
                }
                achievements_.OnEvent(event.name, about);
            }
            const std::vector<ScriptHandler> handlers = handlers_; // a handler may add or remove handlers
            for (const auto& handler : handlers) {
                if (handler.event != event.name && handler.event != "*") continue;
                if (handler.owner != entt::null && !registry_.valid(handler.owner)) continue;
                sol::table e = lua_.State().create_table();
                e["name"] = event.name;
                if (registry_.valid(event.entity)) e["entity"] = static_cast<lua_Integer>(static_cast<uint32_t>(event.entity));
                if (registry_.valid(event.other)) e["other"] = static_cast<lua_Integer>(static_cast<uint32_t>(event.other));
                e["amount"] = event.amount;
                e["text"] = event.text;
                StartTask(handler.owner, handler.fn, e);
            }
        }
    }
    pendingEvents_.clear();
}

void GameSession::UpdateTimersAndTasks(float dt) {
    // Scripts whose entity is gone stop.
    std::vector<Entity> gone;
    auto check = [&](Entity owner) {
        if (owner != entt::null && !registry_.valid(owner)) gone.push_back(owner);
    };
    for (const auto& h : handlers_) check(h.owner);
    for (const auto& t : timers_) check(t.owner);
    for (const auto& t : tasks_) check(t->owner);
    for (Entity owner : gone) ForgetScript(owner);

    std::vector<ScriptTimer> due;
    for (auto& timer : timers_) {
        timer.remaining -= dt;
        if (timer.remaining <= 0.0) {
            due.push_back(timer);
            timer.remaining += timer.interval > 0.0 ? timer.interval : 0.0;
        }
    }
    std::erase_if(timers_, [](const ScriptTimer& t) { return t.remaining <= 0.0 && t.interval <= 0.0; });
    for (const auto& timer : due) StartTask(timer.owner, timer.fn, sol::lua_nil);

    for (size_t i = 0; i < tasks_.size(); ++i) {
        ScriptTask& task = *tasks_[i];
        task.wait -= dt;
        if (task.wait <= 0.0) ResumeTask(task, sol::lua_nil);
    }
    std::erase_if(tasks_, [](const std::unique_ptr<ScriptTask>& t) {
        return t->co.status() != sol::call_status::yielded;
    });
}

int GameSession::Advance(double frameSeconds, const KeyQueryFn& isKeyDown) {
    events_.clear();
    delivered_.clear();
    frameSeconds *= std::clamp(ActiveAssists().gameSpeed, 0.25f, 1.0f); // the game-speed assist (16.4)
    // Juice (18.5): shake decays; slow motion stretches time for a while.
    trauma_ = std::max(0.0f, trauma_ - static_cast<float>(frameSeconds) * 1.1f);
    shakeTime_ += frameSeconds;
    if (slowSeconds_ > 0.0) {
        slowSeconds_ -= frameSeconds;
        frameSeconds *= slowScale_;
    }
    if (difficultyDirty_) ApplyDifficultyNow();
    accumulator_ += std::min(frameSeconds, 0.25);
    if (clockMinutes_ < 0.0) {
        clockMinutes_ = ActiveSettings().time.startHour * 60.0;
    }
    // Input actions (12.3): once per frame, in the context of what's on screen.
    input_.SetContext(cutscene_.Active() ? "cutscene" : conversation_.Active() ? "dialogue" : "gameplay");
    input_.Update(ActiveInputMap(), isKeyDown, axisQuery_);
    bool attackPressed = input_.Pressed("attack");
    if (input_.Pressed("debug_ai")) showAiDebug_ = !showAiDebug_;
    if (input_.Pressed("debug_physics")) showPhysicsDebug_ = !showPhysicsDebug_;
    HandleStoryInput(isKeyDown);
    StepActions(static_cast<float>(std::min(frameSeconds, 0.25)));
    if (noticeSeconds_ > 0.0) {
        noticeSeconds_ -= frameSeconds;
        if (noticeSeconds_ <= 0.0) {
            notice_.clear();
        }
    }
    ShowNotices();
    if (freeze_ > 0.0) {
        // Hit-stop (18.5): the world holds for a moment.
        freeze_ -= frameSeconds;
        accumulator_ = std::fmod(accumulator_, kFixedDt);
        return 0;
    }
    if (cutscene_.Active()) {
        // A cutscene runs instead of the world (15.5); its actors still animate.
        StepFx(static_cast<float>(std::min(frameSeconds, 0.25)));
        StepCutscene(static_cast<float>(std::min(frameSeconds, 0.25)));
        ShowNotices();
        DispatchEvents();
        accumulator_ = std::fmod(accumulator_, kFixedDt);
        return 0;
    }
    if (conversation_.Active()) {
        // A conversation pauses the world.
        if (const DialogueNode* node = conversation_.Node(); node != voicedNode_) {
            voicedNode_ = node;
            if (node && !node->voice.empty() && playSound_) playSound_(node->voice); // its voice line (15.3)
        }
        accumulator_ = std::fmod(accumulator_, kFixedDt);
        return 0;
    }
    voicedNode_ = nullptr;
    if (travel_) {
        return 0; // waiting for the host to change scene
    }
    if (accumulator_ < kFixedDt) {
        return 0;
    }

    grid_ = TileGrid::Build(registry_);
    auto isSolid = [this](int gx, int gy) { return grid_.IsSolid(gx, gy); };

    int steps = 0;
    bool firstStep = true; // this frame's presses act once
    size_t soundsPlayed = 0;
    while (accumulator_ >= kFixedDt && !cutscene_.Active()) { // a cutscene started mid-frame takes over
        const float dt = static_cast<float>(kFixedDt);
        if (auto player = FindPlayer(registry_)) {
            if (const auto* controller = registry_.try_get<CharacterController2D>(*player); controller && controller->mode == 1) {
                // Grid-step (20.2): cell by cell, on the screen's axes turned to the world's.
                const float mx = input_.Value("move_x"), my = input_.Value("move_y");
                float wx = mx, wy = my;
                if (ActiveSettings().grid.style != GridStyle::TopDown) { wx = mx + my; wy = my - mx; }
                registry_.get<Velocity2D>(*player) = {};
                if (!registry_.all_of<Dead>(*player)) UpdateGridStep(registry_, *player, wx, wy, isSolid, dt);
            } else {
                UpdatePlayerMove(registry_, *player, input_.Value("move_x"), input_.Value("move_y"), isSolid, dt);
            }

            // Attacks: the attack action swings toward the facing direction (on
            // press, once per frame); a click swings toward the clicked point.
            if (attackPressed) {
                Swing(*player, 0.0f, 0.0f);
                attackPressed = false;
            }
            if (requestedAttack_) {
                const auto& world = registry_.get<WorldTransform2D>(*player);
                Swing(*player, requestedAttack_->first - world.x,
                               requestedAttack_->second - world.y);
                requestedAttack_.reset();
            }
        }
        UpdateFollowers(registry_, isSolid, dt);
        clockMinutes_ += dt * ActiveSettings().time.minutesPerSecond;
        if (schedules_ != nullptr) {
            UpdateRoutines(registry_, *schedules_, MinuteOfDay(), isSolid, dt, routineRng_, scene_);
        }
        // Reach objectives: the player standing at a Marker.
        if (storyLibrary_ != nullptr && story_.AnyQuestActive()) {
            if (auto player = FindPlayer(registry_)) {
                const auto& at = registry_.get<WorldTransform2D>(*player);
                for (auto [entity, marker, world] : registry_.view<Marker, WorldTransform2D>().each()) {
                    if (std::abs(world.x - at.x) <= 0.9f && std::abs(world.y - at.y) <= 0.9f) {
                        story_.OnReached(marker.name, *storyLibrary_);
                    }
                }
            }
        }
        // Streaming (24): far chunks sleep; checked twice a second.
        streamingCheck_ -= dt;
        if (streamingCheck_ <= 0.0f) {
            streamingCheck_ = 0.5f;
            const auto& streaming = ActiveSettings().streaming;
            if (streaming.enabled) {
                if (auto player = FindPlayer(registry_)) {
                    const auto& at = registry_.get<WorldTransform2D>(*player);
                    UpdateStreaming(registry_, at.x, at.y, streaming.chunk, streaming.radius);
                }
            } else if (!registry_.view<Dormant>().empty()) {
                WakeAll(registry_);
            }
        }
        Profiler::Scope scriptsTime("scripts");
        UpdateScripts(registry_, lua_, dt, binder_, scriptRoot_, [this](Entity entity, const std::string& path, const std::string& message) {
            if (scriptErrors_.size() < 64) {
                scriptErrors_.push_back({entity, path, message});
            }
        });
        {
            Profiler::Scope t("gameplay");
            StepGameplay(dt, isSolid, firstStep); // Phase 13: attributes, actions, archers, spawners, pickups
        }
        {
            Profiler::Scope t("characters");
            StepCharacters(dt, isSolid); // Phase 14: packages, perception, combat behaviour
        }
        StepStory(dt);                         // Phase 15: timed, carried and escort objectives
        firstStep = false;
        const size_t firstEvent = events_.size();
        UpdateCombat(registry_, isSolid, dt, events_);
        OnDeaths(firstEvent);
        // Hurt characters turn on their attacker; noises for the next step's ears.
        for (size_t i = firstEvent; i < events_.size(); ++i) {
            const GameEvent& event = events_[i];
            if (event.type == GameEvent::Type::Hit && TeamOf(registry_, event.entity) == Team::Player) playerHurtSinceKill_ = true;
            if (event.type == GameEvent::Type::Hit && registry_.valid(event.entity) && registry_.all_of<Behaviour>(event.entity)) {
                AlertActor(registry_, event.entity, event.other);
                if (const auto* barks = registry_.try_get<Barks>(event.entity); barks && !barks->hit.empty()) {
                    ShowBark(event.entity, barks->hit);
                }
            }
            if ((event.type == GameEvent::Type::Hit || event.type == GameEvent::Type::Attack) && registry_.valid(event.entity)) {
                if (const auto* w = registry_.try_get<WorldTransform2D>(event.entity)) {
                    noises_.push_back({w->x, w->y, event.type == GameEvent::Type::Hit ? 1.4f : 1.0f, event.entity});
                }
            }
        }
        OnStoryEvents(firstEvent);
        CueEffects(firstEvent); // animation triggers and effect cues for the step's hits, deaths, attacks
        {
            Profiler::Scope t("magic");
            StepMagic(dt, firstEvent); // Phase 19: casts, effects, auras, enchantments, AI spells
        }
        {
            Profiler::Scope t("physics");
            StepPhysics(dt, isSolid); // Phase 20: rigid bodies, joints, triggers
        }
        // Achievements (24): quests and flags, now and then; what was unlocked, told.
        achievementCheck_ -= dt;
        if (achievementCheck_ <= 0.0f) {
            achievementCheck_ = 0.5f;
            achievements_.Check(
                [this](const std::string& quest) {
                    using S = StoryState::QuestState;
                    const auto state = story_.Quest(quest).state;
                    return state == S::Active ? std::string("active") : state == S::Completed ? std::string("completed")
                           : state == S::Failed   ? std::string("failed")
                                                  : std::string("inactive");
                },
                [this](const std::string& flag) { return story_.Flag(flag); });
        }
        for (const auto& a : achievements_.TakeUnlocked()) {
            ShowNotice("Achievement unlocked: " + a.name, 4.0, "banner");
            Emit({"achievement", entt::null, entt::null, static_cast<double>(a.points), a.id});
        }
        // Events (12.2): combat's, then the story's, to the scripts' handlers;
        // then timers, waiting coroutines and the modules' systems.
        static const char* kNames[] = {"hit", "death", "pickup", "attack", "victory"};
        for (size_t i = firstEvent; i < events_.size(); ++i) {
            const GameEvent& event = events_[i];
            Emit({kNames[static_cast<int>(event.type)], event.entity, event.other, event.amount, event.prefab});
        }
        ShowNotices();
        DispatchEvents();
        UpdateTimersAndTasks(dt);
        if (ModuleSystemCount() > 0) {
            RunModuleSystems(registry_, dt, [this](const std::string& message) {
                if (scriptErrors_.size() < 64) scriptErrors_.push_back({entt::null, "modules", message});
            });
        }
        UpdateSpriteFacing(registry_);
        StepFx(dt); // Phase 18: animators, particles, tweens, lights
        UpdateWorldToScreen(registry_, iso_);

        for (; soundsPlayed < events_.size(); ++soundsPlayed) {
            if (playSound_ && !events_[soundsPlayed].sound.empty()) {
                playSound_(events_[soundsPlayed].sound);
            }
        }
        accumulator_ -= kFixedDt;
        ++steps;
        StepTriggers(); // cutscene trigger areas
        if (CheckGateways()) {
            accumulator_ = std::fmod(accumulator_, kFixedDt);
            break; // the host changes scene before the next step
        }
    }
    // While a quest is under way it's the goal: clearing every enemy isn't
    // the end of the game.
    if (story_.AnyQuestActive()) {
        events_.erase(std::remove_if(events_.begin(), events_.end(),
                                     [](const GameEvent& e) { return e.type == GameEvent::Type::Victory; }),
                      events_.end());
    }
    ShowNotices();
    if (cutscene_.Active()) accumulator_ = std::fmod(accumulator_, kFixedDt);
    return steps;
}

} // namespace aige
