#include "gameplay/GameSession.h"

#include "gameplay/EngineReflection.h"
#include "scripting/LuaVM.h"
#include "gameplay/Components.h"
#include "gameplay/Settings.h"
#include "gameplay/TileGrid.h"
#include "gameplay/Schedule.h"
#include "scripting/Components.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"
#include "ui/GameUi.h"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <type_traits>

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

    // Physics.playerCollides off (a debugging aid) walks through everything.
    static const SolidQueryFn kNothingSolid = [](int, int) { return false; };
    Vec2 resolved = ResolveAabbMovement({world.x, world.y}, {collider.halfWidth, collider.halfHeight},
                                         {velocity.x * dt, velocity.y * dt},
                                         ActiveSettings().physics.playerCollides ? isSolid : kNothingSolid);
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
        return entity ? sol::make_object(lua_.State(), ToId(*entity)) : sol::lua_nil;
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
    // The game clock: hours since midnight (fractional), and the day number from 1.
    env["time_of_day"] = [this]() { return MinuteOfDay() / 60.0f; };
    env["day"] = [this]() { return Day(); };
}

// --- Story -------------------------------------------------------------------

void GameSession::SetStory(const StoryLibrary* library) {
    storyLibrary_ = library;
    story_ = {};
    conversation_.End();
    talkingTo_ = entt::null;
    notice_.clear();
}

bool GameSession::Pressed(Key key, const KeyQueryFn& isKeyDown) {
    bool down = isKeyDown(key);
    bool was = std::find(keysDown_.begin(), keysDown_.end(), key) != keysDown_.end();
    if (down && !was) {
        keysDown_.push_back(key);
    } else if (!down && was) {
        keysDown_.erase(std::remove(keysDown_.begin(), keysDown_.end(), key), keysDown_.end());
    }
    return down && !was;
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
    // Read every edge once per frame, so a press isn't seen twice.
    const bool e = Pressed(Key::E, isKeyDown);
    const bool enter = Pressed(Key::Enter, isKeyDown);
    const bool space = Pressed(Key::Space, isKeyDown);
    const bool up = Pressed(Key::Up, isKeyDown);
    const bool down = Pressed(Key::Down, isKeyDown);
    int number = 0;
    for (int i = 0; i < 4; ++i) {
        if (Pressed(static_cast<Key>(static_cast<int>(Key::Num1) + i), isKeyDown)) {
            number = i + 1;
        }
    }
    if (storyLibrary_ == nullptr) {
        return;
    }
    if (conversation_.Active()) {
        if (number > 0) {
            conversation_.Choose(static_cast<size_t>(number - 1), story_, *storyLibrary_);
        } else if (up || down) {
            conversation_.MoveHighlight(up ? -1 : 1);
        } else if (enter || space || e) {
            conversation_.Continue(story_, *storyLibrary_);
        }
        if (!conversation_.Active()) {
            talkingTo_ = entt::null;
            attackKeyWasDown_ = isKeyDown(Key::Space); // the closing Space isn't a swing
        }
        return;
    }
    if (e) {
        if (auto npc = TalkTarget()) {
            const Talk& talk = registry_.get<Talk>(*npc);
            if (const Dialogue* dialogue = storyLibrary_->FindDialogue(talk.dialogue)) {
                story_.OnTalkedTo(talk.character, *storyLibrary_);
                if (conversation_.Start(*dialogue, story_, *storyLibrary_)) {
                    talkingTo_ = *npc;
                }
            }
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

void GameSession::BuildUi(std::vector<UiQuad>& out, float viewportWidth, float viewportHeight) {
    const auto routines = registry_.view<Routine>();
    if (ActiveSettings().time.showClock || routines.begin() != routines.end()) {
        AddClock(out, "Day " + std::to_string(Day()) + "  " + FormatClockTime(static_cast<int>(MinuteOfDay())),
                 viewportWidth, viewportHeight);
    }
    if (storyLibrary_ == nullptr) {
        return;
    }
    if (auto shown = story_.CurrentObjective(*storyLibrary_)) {
        AddObjective(out, shown->quest, shown->text, viewportWidth, viewportHeight);
    }
    if (!notice_.empty()) {
        AddToast(out, notice_, viewportWidth, viewportHeight);
    }
    if (const DialogueNode* node = conversation_.Node()) {
        const Dialogue* dialogue = conversation_.Current();
        DialogueView view;
        const std::string speaker = node->speaker.empty() ? dialogue->speaker : node->speaker;
        if (speaker == "player") {
            auto player = FindPlayer(registry_);
            view.speaker = player ? NameOf("", *player) : std::string();
            if (view.speaker.empty()) {
                view.speaker = "You";
            }
        } else {
            view.speaker = NameOf(speaker, talkingTo_);
        }
        view.text = node->text;
        for (size_t index : conversation_.VisibleChoices()) {
            view.choices.push_back(node->choices[index].text);
        }
        view.highlighted = conversation_.Highlighted();
        AddDialogueBox(out, view, viewportWidth, viewportHeight);
    } else if (auto npc = TalkTarget()) {
        const Talk& talk = registry_.get<Talk>(*npc);
        if (storyLibrary_->FindDialogue(talk.dialogue) != nullptr) {
            AddPrompt(out, "E", "Talk to " + NameOf(talk.character, *npc), viewportWidth, viewportHeight);
        }
    }
}

void GameSession::ShowNotices() {
    for (const auto& notice : story_.TakeNotices()) {
        using Kind = StoryState::Notice::Kind;
        notice_ = notice.kind == Kind::QuestStarted     ? "New quest: " + notice.text
                  : notice.kind == Kind::QuestCompleted ? "Quest complete: " + notice.text
                                                        : "Done: " + notice.text;
        noticeSeconds_ = 3.0;
    }
}

int GameSession::Advance(double frameSeconds, const KeyQueryFn& isKeyDown) {
    events_.clear();
    accumulator_ += std::min(frameSeconds, 0.25);
    if (clockMinutes_ < 0.0) {
        clockMinutes_ = ActiveSettings().time.startHour * 60.0;
    }
    HandleStoryInput(isKeyDown);
    if (noticeSeconds_ > 0.0) {
        noticeSeconds_ -= frameSeconds;
        if (noticeSeconds_ <= 0.0) {
            notice_.clear();
        }
    }
    ShowNotices();
    if (conversation_.Active()) {
        // A conversation pauses the world.
        accumulator_ = std::fmod(accumulator_, kFixedDt);
        return 0;
    }
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
        clockMinutes_ += dt * ActiveSettings().time.minutesPerSecond;
        if (schedules_ != nullptr) {
            UpdateRoutines(registry_, *schedules_, MinuteOfDay(), isSolid, dt, routineRng_);
        }
        UpdateScripts(registry_, lua_, dt, binder_, scriptRoot_, [this](Entity entity, const std::string& path, const std::string& message) {
            if (scriptErrors_.size() < 64) {
                scriptErrors_.push_back({entity, path, message});
            }
        });
        const size_t firstEvent = events_.size();
        UpdateCombat(registry_, isSolid, dt, events_);
        OnStoryEvents(firstEvent);
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
    // While a quest is under way it's the goal: clearing every enemy isn't
    // the end of the game.
    if (story_.AnyQuestActive()) {
        events_.erase(std::remove_if(events_.begin(), events_.end(),
                                     [](const GameEvent& e) { return e.type == GameEvent::Type::Victory; }),
                      events_.end());
    }
    ShowNotices();
    return steps;
}

} // namespace aige
