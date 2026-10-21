#include "gameplay/Combat.h"

#include "gameplay/Attributes.h"
#include "gameplay/Behaviour.h"
#include "gameplay/GameSession.h"
#include "records/Records.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace aige {

namespace {


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
        flash->remaining = ActiveSettings().combat.hitFlashSeconds; // already red; just extend
        return;
    }
    registry.emplace<HitFlash>(entity, ActiveSettings().combat.hitFlashSeconds, sprite->r, sprite->g, sprite->b);
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
    box.knockback = attack->knockback * ActiveSettings().combat.knockbackScale;
    box.lifetime = kHitboxLifetime;
    box.effect = attack->effect;
    box.effectDuration = attack->effectDuration;
    box.effectMagnitude = attack->effectMagnitude;
    box.damageType = attack->damageType;
    return hitbox;
}

bool AreHostile(Registry& registry, Entity attacker, Entity target) {
    if (attacker == target) return false;
    const auto* a = registry.valid(attacker) ? registry.try_get<FactionMember>(attacker) : nullptr;
    const auto* b = registry.valid(target) ? registry.try_get<FactionMember>(target) : nullptr;
    const Team at = TeamOf(registry, attacker), bt = TeamOf(registry, target);
    // The player and a faction member: by the member's disposition.
    if (at == Team::Player && b) return b->disposition < 0;
    if (bt == Team::Player && a) return a->disposition < 0;
    if (a && b && !a->faction.empty() && !b->faction.empty()) {
        if (a->faction == b->faction) return false;
        // The relation in either faction's record: below zero is war.
        auto standing = [](const std::string& from, const std::string& to) -> std::optional<int> {
            const Record* record = ActiveRecords().Find("faction", from);
            if (!record) return std::nullopt;
            for (const auto& r : record->resolved.value("relations", nlohmann::json::array())) {
                if (r.value("faction", "") == to) return r.value("standing", 0);
            }
            return std::nullopt;
        };
        auto s = standing(a->faction, b->faction);
        if (!s) s = standing(b->faction, a->faction);
        if (s) return *s < 0;
    }
    return IsHostile(at, bt);
}

std::optional<Entity> TryRangedAttack(Registry& registry, Entity attacker, float dirX, float dirY) {
    if (!registry.valid(attacker) || registry.all_of<Dead>(attacker)) return std::nullopt;
    auto* ranged = registry.try_get<RangedAttack>(attacker);
    const auto* world = registry.try_get<WorldTransform2D>(attacker);
    if (!ranged || !world || ranged->cooldownRemaining > 0.0f) return std::nullopt;
    float length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 1e-4f) {
        const auto* facing = registry.try_get<Facing>(attacker);
        dirX = facing ? facing->x : 1.0f;
        dirY = facing ? facing->y : 0.0f;
        length = std::max(1e-4f, std::sqrt(dirX * dirX + dirY * dirY));
    }
    dirX /= length;
    dirY /= length;
    registry.emplace_or_replace<Facing>(attacker, dirX, dirY);
    ranged->cooldownRemaining = ranged->cooldown;
    const auto* stats = registry.try_get<Stats>(attacker);
    Entity shot = registry.create();
    registry.emplace<WorldTransform2D>(shot, world->x + dirX * 0.5f, world->y + dirY * 0.5f);
    Projectile& p = registry.emplace<Projectile>(shot);
    p.owner = attacker;
    p.team = TeamOf(registry, attacker);
    p.damageType = ranged->damageType;
    p.damage = ranged->damage + (stats ? stats->attack * 0.5f : 0.0f);
    p.vx = dirX * ranged->speed;
    p.vy = dirY * ranged->speed;
    p.remaining = ranged->range / std::max(0.1f, ranged->speed);
    p.pierce = ranged->pierce;
    Sprite& sprite = registry.emplace<Sprite>(shot);
    sprite.width = 16.0f;
    sprite.height = 16.0f;
    sprite.texture = ranged->projectile;
    if (ranged->projectile.empty()) {
        sprite.width = 8.0f;
        sprite.height = 8.0f;
        sprite.r = 1.0f;
        sprite.g = 0.85f;
        sprite.b = 0.4f;
    }
    registry.emplace<ScreenTransform2D>(shot);
    return shot;
}

bool StartDodge(Registry& registry, Entity entity, float dirX, float dirY) {
    if (!registry.valid(entity) || registry.all_of<Dead>(entity) || registry.all_of<Dodging>(entity)) return false;
    const float cost = ActiveSettings().combat.dodgeStamina;
    if (auto* resources = registry.try_get<Resources>(entity); resources && resources->maxStamina > 0.0f) {
        if (resources->stamina < cost) return false;
        resources->stamina -= cost;
    }
    float length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 1e-4f) {
        const auto* facing = registry.try_get<Facing>(entity);
        dirX = facing ? facing->x : 1.0f;
        dirY = facing ? facing->y : 0.0f;
        length = std::max(1e-4f, std::sqrt(dirX * dirX + dirY * dirY));
    }
    const float seconds = 0.22f;
    const float distance = ActiveSettings().combat.dodgeDistance;
    registry.emplace_or_replace<Dodging>(entity, dirX / length * distance / seconds, dirY / length * distance / seconds, seconds);
    return true;
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
                 bool applyDefense, bool cannotKill, DamageType type) {
    auto* health = registry.try_get<Health>(target);
    if (health == nullptr || registry.all_of<Dead>(target) || health->current <= 0.0f) {
        return 0.0f;
    }
    // A dodge in progress can't be hit (13.6).
    if (registry.all_of<Dodging>(target)) return 0.0f;
    // Resistances (13.6): the share of that kind of damage taken away.
    if (type != DamageType::Physical && registry.all_of<Attributes>(target)) {
        const char* name = type == DamageType::Fire    ? "resistFire"
                           : type == DamageType::Frost ? "resistFrost"
                           : type == DamageType::Shock ? "resistShock"
                                                       : "resistPoison";
        amount *= 1.0f - std::clamp(EffectiveAttribute(registry, target, name), 0.0f, 0.9f);
    }
    if (applyDefense) {
        const auto* stats = registry.try_get<Stats>(target);
        amount = std::max(ActiveSettings().combat.minimumDamage, amount - (stats ? stats->defense : 0.0f));
    }
    // Blocking: most of the hit is stopped while there's stamina for it.
    if (registry.all_of<Blocking>(target)) {
        auto* resources = registry.try_get<Resources>(target);
        const float cost = amount * 2.0f;
        if (!resources || resources->stamina >= cost) {
            if (resources) resources->stamina -= cost;
            amount *= 1.0f - std::clamp(ActiveSettings().combat.blockReduction, 0.0f, 1.0f);
        }
    }
    if (registry.valid(source) && source != target) {
        registry.emplace_or_replace<LastHit>(target, source);
        // Attacking a faction member turns it (and its feelings) against the attacker.
        if (auto* member = registry.try_get<FactionMember>(target); member && TeamOf(registry, source) == Team::Player) {
            member->disposition = std::min(member->disposition, 0) - 25;
        }
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
    for (auto [entity, ranged] : registry.view<RangedAttack>().each()) {
        ranged.cooldownRemaining = std::max(0.0f, ranged.cooldownRemaining - dt);
    }

    // Dodges (13.6): the dash, against walls.
    std::vector<Entity> dodgesDone;
    for (auto [entity, dodge, world] : registry.view<Dodging, WorldTransform2D>().each()) {
        Vec2 half{0.3f, 0.3f};
        if (const auto* collider = registry.try_get<Collider2D>(entity)) half = {collider->halfWidth, collider->halfHeight};
        const Vec2 moved = ResolveAabbMovement({world.x, world.y}, half, {dodge.vx * dt, dodge.vy * dt}, isSolid);
        world.x = moved.x;
        world.y = moved.y;
        dodge.remaining -= dt;
        if (dodge.remaining <= 0.0f) dodgesDone.push_back(entity);
    }
    for (auto entity : dodgesDone) registry.remove<Dodging>(entity);

    // Hitboxes against hurtboxes.
    auto hitboxes = registry.view<Hitbox, WorldTransform2D>();
    auto targets = registry.view<Hurtbox, Health, WorldTransform2D>(entt::exclude<Dead>);
    for (auto hitEntity : hitboxes) {
        auto& box = hitboxes.get<Hitbox>(hitEntity);
        const auto& at = hitboxes.get<WorldTransform2D>(hitEntity);
        for (auto target : targets) {
            const bool hostile = registry.valid(box.owner) ? AreHostile(registry, box.owner, target)
                                                           : IsHostile(box.team, TeamOf(registry, target));
            if (target == box.owner || !hostile ||
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
            DealDamage(registry, target, box.damage, box.owner, &events, true, false, box.damageType);
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

    // Projectiles (13.6): fly, stop at walls, hit the first hostile hurtbox.
    for (auto [shot, projectile, at] : registry.view<Projectile, WorldTransform2D>().each()) {
        at.x += projectile.vx * dt;
        at.y += projectile.vy * dt;
        projectile.remaining -= dt;
        if (projectile.remaining <= 0.0f || isSolid(static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)))) {
            expired.push_back(shot);
            continue;
        }
        for (auto target : targets) {
            if (target == projectile.owner ||
                std::find(projectile.alreadyHit.begin(), projectile.alreadyHit.end(), target) != projectile.alreadyHit.end()) {
                continue;
            }
            const bool hostile = registry.valid(projectile.owner) ? AreHostile(registry, projectile.owner, target)
                                                                  : IsHostile(projectile.team, TeamOf(registry, target));
            if (!hostile) continue;
            auto& hurtbox = targets.get<Hurtbox>(target);
            const auto& world = targets.get<WorldTransform2D>(target);
            if (hurtbox.invulnerableRemaining > 0.0f ||
                !Overlaps(at.x, at.y, 0.15f, 0.15f, world.x, world.y, hurtbox.halfWidth, hurtbox.halfHeight)) {
                continue;
            }
            projectile.alreadyHit.push_back(target);
            DealDamage(registry, target, projectile.damage, projectile.owner, &events, true, false, projectile.damageType);
            hurtbox.invulnerableRemaining = hurtbox.invulnerableTime;
            if (!projectile.pierce) {
                expired.push_back(shot);
                break;
            }
        }
    }

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
            const auto* instance = registry.try_get<PrefabInstance>(entity);
            events.push_back({GameEvent::Type::Pickup, *player, entity, pickup.amount,
                              sounds ? sounds->collect : std::string(), instance ? instance->path : std::string()});
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
        const auto* instance = registry.try_get<PrefabInstance>(entity);
        GameEvent death{GameEvent::Type::Death, entity, entt::null, 0.0f, sounds ? sounds->death : std::string(),
                        instance ? instance->path : std::string(), TeamOf(registry, entity)};
        // Who killed it, where it fell and what it gives (13.2, 13.4).
        if (const auto* last = registry.try_get<LastHit>(entity); last && registry.valid(last->source)) death.other = last->source;
        if (const auto* world = registry.try_get<WorldTransform2D>(entity)) {
            death.x = world->x;
            death.y = world->y;
        }
        if (const auto* rewards = registry.try_get<Rewards>(entity)) {
            death.experience = rewards->experience;
            static std::mt19937 rng{0xC0FFEE};
            death.gold = rewards->goldMax > rewards->goldMin
                             ? std::uniform_int_distribution<int>(rewards->goldMin, rewards->goldMax)(rng)
                             : rewards->goldMin;
            death.loot = rewards->loot;
        }
        if (const auto* actor = registry.try_get<Actor>(entity)) death.actor = actor->key;
        events.push_back(death);
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
