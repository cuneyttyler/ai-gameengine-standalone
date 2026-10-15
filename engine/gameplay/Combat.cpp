#include "gameplay/Combat.h"

#include "gameplay/GameSession.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cmath>

namespace aige {

namespace {

constexpr float kHitFlashSeconds = 0.12f;
constexpr float kHitboxLifetime = 0.15f;

bool Overlaps(float ax, float ay, float ahw, float ahh, float bx, float by, float bhw, float bhh) {
    return std::abs(ax - bx) < ahw + bhw && std::abs(ay - by) < ahh + bhh;
}

void StartHitFlash(Registry& registry, Entity entity) {
    auto* sprite = registry.try_get<Sprite>(entity);
    if (sprite == nullptr) {
        return;
    }
    if (auto* flash = registry.try_get<HitFlash>(entity)) {
        flash->remaining = kHitFlashSeconds; // already red; just extend
        return;
    }
    registry.emplace<HitFlash>(entity, kHitFlashSeconds, sprite->r, sprite->g, sprite->b);
    sprite->r = 1.0f;
    sprite->g = 0.3f;
    sprite->b = 0.3f;
}

void EndHitFlash(Registry& registry, Entity entity) {
    const auto& flash = registry.get<HitFlash>(entity);
    if (auto* sprite = registry.try_get<Sprite>(entity)) {
        sprite->r = flash.r;
        sprite->g = flash.g;
        sprite->b = flash.b;
    }
    registry.remove<HitFlash>(entity);
}

} // namespace

Team TeamOf(Registry& registry, Entity entity) {
    const auto* faction = registry.try_get<Faction>(entity);
    return faction ? faction->team : Team::Neutral;
}

bool IsHostile(Team attacker, Team target) {
    return attacker == Team::Neutral || target == Team::Neutral || attacker != target;
}

std::optional<Entity> TryMeleeAttack(Registry& registry, Entity attacker, float dirX, float dirY) {
    if (!registry.valid(attacker) || registry.all_of<Dead>(attacker)) {
        return std::nullopt;
    }
    auto* attack = registry.try_get<MeleeAttack>(attacker);
    const auto* world = registry.try_get<WorldTransform2D>(attacker);
    if (attack == nullptr || world == nullptr || attack->cooldownRemaining > 0.0f) {
        return std::nullopt;
    }
    float length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 1e-4f) {
        const auto* facing = registry.try_get<Facing>(attacker);
        dirX = facing ? facing->x : 1.0f;
        dirY = facing ? facing->y : 0.0f;
        length = std::sqrt(dirX * dirX + dirY * dirY);
    }
    dirX /= length;
    dirY /= length;
    registry.emplace_or_replace<Facing>(attacker, dirX, dirY);
    attack->cooldownRemaining = attack->cooldown;

    const auto* stats = registry.try_get<Stats>(attacker);
    Entity hitbox = registry.create();
    registry.emplace<WorldTransform2D>(hitbox, world->x + dirX * attack->reach, world->y + dirY * attack->reach);
    Hitbox& box = registry.emplace<Hitbox>(hitbox);
    box.owner = attacker;
    box.team = TeamOf(registry, attacker);
    box.damage = attack->damage + (stats ? stats->attack : 0.0f);
    box.halfWidth = attack->radius;
    box.halfHeight = attack->radius;
    box.knockback = attack->knockback;
    box.lifetime = kHitboxLifetime;
    box.effect = attack->effect;
    box.effectDuration = attack->effectDuration;
    box.effectMagnitude = attack->effectMagnitude;
    return hitbox;
}

Entity ApplyStatus(Registry& registry, Entity target, EffectKind kind, float duration, float magnitude,
                   float tickInterval) {
    auto effects = registry.view<StatusEffect>();
    for (auto entity : effects) {
        auto& effect = effects.get<StatusEffect>(entity);
        if (effect.target == target && effect.kind == kind) {
            effect.remaining = std::max(effect.remaining, duration);
            effect.magnitude = std::max(effect.magnitude, magnitude);
            return entity;
        }
    }
    Entity entity = registry.create();
    registry.emplace<StatusEffect>(entity, target, kind, duration, magnitude, tickInterval, tickInterval);
    return entity;
}

float SpeedMultiplier(Registry& registry, Entity entity) {
    float multiplier = 1.0f;
    auto effects = registry.view<StatusEffect>();
    for (auto e : effects) {
        const auto& effect = effects.get<StatusEffect>(e);
        if (effect.target == entity && effect.kind == EffectKind::Slow) {
            multiplier *= std::clamp(1.0f - effect.magnitude, 0.0f, 1.0f);
        }
    }
    return multiplier;
}

float DealDamage(Registry& registry, Entity target, float amount, Entity source, std::vector<GameEvent>* events,
                 bool applyDefense, bool cannotKill) {
    auto* health = registry.try_get<Health>(target);
    if (health == nullptr || registry.all_of<Dead>(target) || health->current <= 0.0f) {
        return 0.0f;
    }
    if (applyDefense) {
        const auto* stats = registry.try_get<Stats>(target);
        amount = std::max(1.0f, amount - (stats ? stats->defense : 0.0f));
    }
    if (cannotKill) {
        amount = std::min(amount, health->current - 1.0f);
    }
    if (amount <= 0.0f) {
        return 0.0f;
    }
    health->current -= amount;
    StartHitFlash(registry, target);
    if (events) {
        const auto* sounds = registry.try_get<Sounds>(target);
        events->push_back({GameEvent::Type::Hit, target, source, amount, sounds ? sounds->hurt : std::string()});
    }
    return amount;
}

void Heal(Registry& registry, Entity target, float amount) {
    if (auto* health = registry.try_get<Health>(target); health != nullptr && !registry.all_of<Dead>(target)) {
        health->current = std::min(health->max, health->current + amount);
    }
}

void UpdateCombat(Registry& registry, const SolidQueryFn& isSolid, float dt, std::vector<GameEvent>& events) {
    // Timers.
    for (auto [entity, attack] : registry.view<MeleeAttack>().each()) {
        attack.cooldownRemaining = std::max(0.0f, attack.cooldownRemaining - dt);
    }
    for (auto [entity, hurtbox] : registry.view<Hurtbox>().each()) {
        hurtbox.invulnerableRemaining = std::max(0.0f, hurtbox.invulnerableRemaining - dt);
    }

    // Hitboxes against hurtboxes.
    auto hitboxes = registry.view<Hitbox, WorldTransform2D>();
    auto targets = registry.view<Hurtbox, Health, WorldTransform2D>(entt::exclude<Dead>);
    for (auto hitEntity : hitboxes) {
        auto& box = hitboxes.get<Hitbox>(hitEntity);
        const auto& at = hitboxes.get<WorldTransform2D>(hitEntity);
        for (auto target : targets) {
            if (target == box.owner || !IsHostile(box.team, TeamOf(registry, target)) ||
                std::find(box.alreadyHit.begin(), box.alreadyHit.end(), target) != box.alreadyHit.end()) {
                continue;
            }
            auto& hurtbox = targets.get<Hurtbox>(target);
            auto& world = targets.get<WorldTransform2D>(target);
            if (hurtbox.invulnerableRemaining > 0.0f ||
                !Overlaps(at.x, at.y, box.halfWidth, box.halfHeight, world.x, world.y, hurtbox.halfWidth,
                          hurtbox.halfHeight)) {
                continue;
            }
            box.alreadyHit.push_back(target);
            DealDamage(registry, target, box.damage, box.owner, &events);
            hurtbox.invulnerableRemaining = hurtbox.invulnerableTime;
            if (box.effect != EffectKind::None) {
                ApplyStatus(registry, target, box.effect, box.effectDuration, box.effectMagnitude);
            }
            // Knockback away from the attacker (or the hitbox, if the
            // attacker is gone), resolved against walls.
            if (box.knockback > 0.0f) {
                const auto* from = registry.valid(box.owner) ? registry.try_get<WorldTransform2D>(box.owner) : nullptr;
                float fx = from ? from->x : at.x;
                float fy = from ? from->y : at.y;
                float dx = world.x - fx;
                float dy = world.y - fy;
                float length = std::sqrt(dx * dx + dy * dy);
                if (length > 1e-4f) {
                    Vec2 half{hurtbox.halfWidth, hurtbox.halfHeight};
                    if (const auto* collider = registry.try_get<Collider2D>(target)) {
                        half = {collider->halfWidth, collider->halfHeight};
                    }
                    Vec2 pushed = ResolveAabbMovement({world.x, world.y}, half,
                                                      {dx / length * box.knockback, dy / length * box.knockback},
                                                      isSolid);
                    world.x = pushed.x;
                    world.y = pushed.y;
                }
            }
        }
    }
    std::vector<Entity> expired;
    for (auto [entity, box] : registry.view<Hitbox>().each()) {
        box.lifetime -= dt;
        if (box.lifetime <= 0.0f) {
            expired.push_back(entity);
        }
    }

    // Status effects.
    for (auto [entity, effect] : registry.view<StatusEffect>().each()) {
        if (!registry.valid(effect.target) || registry.all_of<Dead>(effect.target)) {
            expired.push_back(entity);
            continue;
        }
        effect.tickTimer -= dt;
        while (effect.tickTimer <= 0.0f && effect.tickInterval > 0.0f) {
            effect.tickTimer += effect.tickInterval;
            switch (effect.kind) {
                case EffectKind::Burn:
                    DealDamage(registry, effect.target, effect.magnitude, entt::null, &events, false);
                    break;
                case EffectKind::Poison:
                    DealDamage(registry, effect.target, effect.magnitude, entt::null, &events, false, true);
                    break;
                case EffectKind::Regen:
                    Heal(registry, effect.target, effect.magnitude);
                    break;
                case EffectKind::Slow:
                case EffectKind::None:
                    break;
            }
        }
        effect.remaining -= dt;
        if (effect.remaining <= 0.0f) {
            expired.push_back(entity);
        }
    }

    // Hit flashes.
    std::vector<Entity> flashesDone;
    for (auto [entity, flash] : registry.view<HitFlash>().each()) {
        flash.remaining -= dt;
        if (flash.remaining <= 0.0f) {
            flashesDone.push_back(entity);
        }
    }
    for (auto entity : flashesDone) {
        EndHitFlash(registry, entity);
    }

    // Pickups: collected by the (living) player on overlap.
    if (auto player = FindPlayer(registry); player && !registry.all_of<Dead>(*player)) {
        const auto& playerPos = registry.get<WorldTransform2D>(*player);
        for (auto [entity, pickup, world] : registry.view<Pickup, WorldTransform2D>().each()) {
            float dx = world.x - playerPos.x;
            float dy = world.y - playerPos.y;
            if (dx * dx + dy * dy > pickup.radius * pickup.radius) {
                continue;
            }
            const auto* health = registry.try_get<Health>(*player);
            if (pickup.kind == PickupKind::Heal && health != nullptr && health->current >= health->max) {
                continue; // don't waste a potion at full health
            }
            Heal(registry, *player, pickup.amount);
            const auto* sounds = registry.try_get<Sounds>(entity);
            events.push_back(
                {GameEvent::Type::Pickup, *player, entity, pickup.amount, sounds ? sounds->collect : std::string()});
            expired.push_back(entity);
        }
    }

    // Deaths. Collected first: adding Dead while iterating a view that
    // excludes Dead would modify the storage being filtered on.
    std::vector<Entity> died;
    for (auto [entity, health] : registry.view<Health>(entt::exclude<Dead>).each()) {
        if (health.current <= 0.0f) {
            died.push_back(entity);
        }
    }
    bool enemyDied = false;
    for (auto entity : died) {
        registry.emplace<Dead>(entity);
        const auto* sounds = registry.try_get<Sounds>(entity);
        events.push_back({GameEvent::Type::Death, entity, entt::null, 0.0f, sounds ? sounds->death : std::string()});
        enemyDied |= TeamOf(registry, entity) == Team::Enemy;
    }
    if (enemyDied) {
        bool enemiesLeft = false;
        for (auto [entity, faction] : registry.view<Faction>(entt::exclude<Dead>).each()) {
            enemiesLeft |= faction.team == Team::Enemy;
        }
        if (!enemiesLeft) {
            events.push_back({GameEvent::Type::Victory});
        }
    }
    auto player = FindPlayer(registry);
    for (auto entity : registry.view<Dead>()) {
        if (!player || entity != *player) {
            expired.push_back(entity);
        }
    }

    std::sort(expired.begin(), expired.end());
    expired.erase(std::unique(expired.begin(), expired.end()), expired.end());
    for (auto entity : expired) {
        if (registry.valid(entity)) {
            registry.destroy(entity);
        }
    }
}

void AppendHealthBars(Registry& registry, std::vector<OverlayLine>& out) {
    for (auto [entity, health, screen, sprite] : registry.view<Health, ScreenTransform2D, Sprite>().each()) {
        // Full-health enemies get no bar (less clutter); the player's is always shown.
        bool isPlayer = TeamOf(registry, entity) == Team::Player;
        if (health.max <= 0.0f || (health.current >= health.max && !isPlayer)) {
            continue;
        }
        float fraction = std::clamp(health.current / health.max, 0.0f, 1.0f);
        float width = std::max(24.0f, sprite.width * 0.8f);
        float x0 = screen.x - width * 0.5f;
        float y = screen.y - sprite.height * 0.5f - 6.0f;
        out.push_back({x0, y, x0 + width, y, 4.0f, 0.15f, 0.05f, 0.05f, 0.85f});
        if (fraction > 0.0f) {
            out.push_back({x0, y, x0 + width * fraction, y, 4.0f, 0.2f + 0.7f * (1.0f - fraction),
                           0.75f * fraction + 0.1f, 0.15f, 1.0f});
        }
    }
}

} // namespace aige
