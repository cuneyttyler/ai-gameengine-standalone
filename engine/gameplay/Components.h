#pragma once

#include "ecs/Registry.h"

#include <string>
#include <vector>

namespace aige {

// A ground tile occupying one isometric grid cell. Its ScreenTransform2D is
// derived from (gx, gy) every frame (UpdateWorldToScreen), so moving a tile
// means changing its cell. Collision comes from tiles too (TileGrid): a cell
// is walkable only if it holds a non-solid tile, so empty space is a wall.
struct Tile {
    int gx = 0;
    int gy = 0;
    bool solid = false;
};

// A designer-facing name, shown in the Scene Tree and usable from scripts.
struct Name {
    std::string value;
};

// Which side an entity is on; combat (milestone 1.5) only lets attacks hit
// entities of an opposing faction.
enum class Team {
    Neutral,
    Player,
    Enemy,
};

struct Faction {
    Team team = Team::Neutral;
};

struct Health {
    float current = 10.0f;
    float max = 10.0f;
};

// Combat numbers. Damage dealt = max(1, attack's damage + attacker.attack -
// target.defense). moveSpeed (cells/s) drives the player's walk speed.
struct Stats {
    float attack = 0.0f;
    float defense = 0.0f;
    float moveSpeed = 3.0f;
};

// The area attacks can hit, centered on the entity's WorldTransform2D. After
// a hit the entity is invulnerable for `invulnerableTime` seconds, so one
// swing can't land twice and enemies don't shred the player in one frame.
struct Hurtbox {
    float halfWidth = 0.35f;
    float halfHeight = 0.35f;
    float invulnerableTime = 0.4f;
    float invulnerableRemaining = 0.0f; // runtime
};

// What a status effect does to its target each tick / while active.
enum class EffectKind {
    None,
    Burn,   // `magnitude` damage per tick
    Poison, // like Burn, but can't kill (stops at 1 HP)
    Slow,   // movement multiplied by (1 - magnitude), magnitude in 0..1
    Regen,  // `magnitude` healing per tick
};

// A close-range swing: spawns a short-lived hitbox `reach` cells in front
// of the attacker. Optionally applies a status effect on hit.
struct MeleeAttack {
    float damage = 2.0f;
    float reach = 0.7f;
    float radius = 0.45f;
    float cooldown = 0.5f;
    float knockback = 0.4f; // cells
    EffectKind effect = EffectKind::None;
    float effectDuration = 3.0f;
    float effectMagnitude = 1.0f;
    float cooldownRemaining = 0.0f; // runtime
};

enum class PickupKind {
    Heal,
};

// Collected when the player walks over it.
struct Pickup {
    PickupKind kind = PickupKind::Heal;
    float amount = 5.0f;
    float radius = 0.5f;
};

// Runtime-only (never saved): last direction moved/attacked, in world space.
struct Facing {
    float x = 1.0f;
    float y = 0.0f;
};

// Runtime-only: an attack's damage area, alive for `lifetime` seconds.
// Each target is hit at most once per hitbox.
struct Hitbox {
    Entity owner = entt::null;
    Team team = Team::Neutral;
    float damage = 1.0f;
    float halfWidth = 0.45f;
    float halfHeight = 0.45f;
    float knockback = 0.0f;
    float lifetime = 0.15f;
    EffectKind effect = EffectKind::None;
    float effectDuration = 0.0f;
    float effectMagnitude = 0.0f;
    std::vector<Entity> alreadyHit;
};

// Runtime-only: status effects are their own entities pointing at a target
// (the ECS-for-data-heavy-systems pattern from technical.md): any number per
// target, iterated in one tight loop, and removing one is destroying it.
struct StatusEffect {
    Entity target = entt::null;
    EffectKind kind = EffectKind::None;
    float remaining = 0.0f;
    float magnitude = 0.0f;
    float tickInterval = 1.0f;
    float tickTimer = 0.0f;
};

// Runtime-only: brief red tint after taking damage; restores the sprite's
// own tint when it ends.
struct HitFlash {
    float remaining = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

// Runtime-only: health reached zero. Non-player entities are removed; the
// player stays (and stops responding) so the host can show a game over.
struct Dead {};

// Blocks movement through the cell it stands in (trees, rocks, crates) —
// TileGrid treats that cell as solid.
struct Obstacle {};

// Sound effects (project-relative audio asset paths) played when this
// entity swings, takes damage, dies, or — for a pickup — is collected.
struct Sounds {
    std::string attack;
    std::string hurt;
    std::string death;
    std::string collect;
};

// Placed from a prefab (see engine/assets/Prefab.h). `overrides` lists the
// fields changed on this copy, which prefab updates leave alone.
struct PrefabInstance {
    std::string path;
    std::string overrides;
};

// Walks toward another entity (with collision) until within stopDistance.
// The simplest useful "AI": an enemy that chases the player needs nothing
// but this and a target. Distances in world grid units, speed in cells/s.
struct Follow {
    Entity target = entt::null; // none = the player, so enemy prefabs need no reference
    float speed = 2.0f;
    float stopDistance = 0.8f;
    float range = 6.0f; // only follows while the target is this close; 0 = always
};

// Someone the player can talk to (E within range): which conversation, and
// who they are in the World Bible (their name on screen, talk objectives).
struct Talk {
    std::string dialogue;  // dialogue/<id>.json
    std::string character; // World Bible key, e.g. character/villager
    float range = 1.5f;    // cells
};

} // namespace aige
