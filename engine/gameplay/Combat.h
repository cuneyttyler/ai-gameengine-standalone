#pragma once

#include "ecs/Registry.h"
#include "gameplay/Components.h"
#include "physics2d/Collision.h"
#include "renderer2d/Renderer2D.h"

#include <optional>
#include <vector>

namespace aige {

// Something that happened during a combat step, for the host to react to
// (sounds, UI, game over) without polling every component.
struct GameEvent {
    enum class Type { Hit, Death, Pickup };
    Type type;
    Entity entity = entt::null; // who was hit / died / picked up
    Entity other = entt::null;  // the attacker / the pickup
    float amount = 0.0f;        // damage dealt / amount healed
};

Team TeamOf(Registry& registry, Entity entity);

// Whether an attack from `attacker` may hurt `target`: never yourself, and
// no friendly fire between members of the same (non-neutral) team.
bool IsHostile(Team attacker, Team target);

// Swings `attacker`'s MeleeAttack in direction (dirX, dirY): spawns a
// short-lived Hitbox entity `reach` cells away. Returns nullopt while on
// cooldown, when dead, or without a MeleeAttack.
std::optional<Entity> TryMeleeAttack(Registry& registry, Entity attacker, float dirX, float dirY);

// Applies a status effect, or refreshes it if the target already has one of
// that kind (duration reset, stronger magnitude kept) — effects of the same
// kind don't stack.
Entity ApplyStatus(Registry& registry, Entity target, EffectKind kind, float duration, float magnitude,
                   float tickInterval = 0.5f);

// Product of all active Slow effects on `entity` (1 = unaffected).
float SpeedMultiplier(Registry& registry, Entity entity);

// Applies damage to an entity's Health (after its Stats.defense when
// `applyDefense`), with hit flash; health reaching 0 marks it Dead on the
// next UpdateCombat. Returns the damage actually dealt.
float DealDamage(Registry& registry, Entity target, float amount, Entity source, std::vector<GameEvent>* events,
                 bool applyDefense = true, bool cannotKill = false);

void Heal(Registry& registry, Entity target, float amount);

// One fixed step of combat: cooldown and invulnerability timers, hitbox
// overlaps (damage, knockback, on-hit effects), hitbox expiry, status effect
// ticks and expiry, hit flashes, pickups, and deaths (non-player entities are
// destroyed; the player is marked Dead). `isSolid` keeps knockback out of walls.
void UpdateCombat(Registry& registry, const SolidQueryFn& isSolid, float dt, std::vector<GameEvent>& events);

// Small health bars above every damaged entity with Health and a sprite.
void AppendHealthBars(Registry& registry, std::vector<OverlayLine>& out);

} // namespace aige
