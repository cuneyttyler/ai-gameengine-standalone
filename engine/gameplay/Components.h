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
// Kinds of damage (development_plan.md 13.6); resistances to each are attributes.
enum class DamageType {
    Physical,
    Fire,
    Frost,
    Shock,
    Poison,
};

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
    DamageType damageType = DamageType::Physical;
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
    DamageType damageType = DamageType::Physical;
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
// Blocks movement through the cell it stands in and, for something bigger
// (a house), the cells beyond it: `left` and `right` across, `back` behind
// (toward -y). Upright art stands on its front row, so a house's footprint
// lies behind and beside where it stands.
struct Obstacle {
    int left = 0;
    int right = 0;
    int back = 0;
};

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

// Made by a place build (development_plan.md 7.2): which build, which Spec
// node and which PLL statement. Lets a built part be found, explained
// ("Why?") and regenerated or removed as a unit.
// --- Combat v2 (development_plan.md 13.6) --------------------------------------

// Shoots projectiles: arrows, bolts, thrown knives.
struct RangedAttack {
    DamageType damageType = DamageType::Physical;
    float damage = 2.0f;
    float range = 7.0f;   // cells the projectile flies
    float speed = 9.0f;   // cells per second
    float cooldown = 0.9f;
    std::string projectile; // its texture ("" = a plain bolt)
    bool pierce = false;    // flies on through what it hits
    float cooldownRemaining = 0.0f; // runtime
};

// A projectile in flight (runtime, made by RangedAttack).
struct Projectile {
    Entity owner = entt::null;
    Team team = Team::Neutral;
    DamageType damageType = DamageType::Physical;
    float damage = 1.0f;
    float vx = 0.0f, vy = 0.0f;
    float remaining = 1.0f; // seconds left
    bool pierce = false;
    std::vector<Entity> alreadyHit;
    std::string mask; // the layers it hits (20.1); "" = hostile characters
};

// Holding block: hits from the front are reduced, at a stamina cost (runtime).
struct Blocking {};

// A dodge in progress: a quick dash, untouchable (runtime).
struct Dodging {
    float vx = 0.0f, vy = 0.0f;
    float remaining = 0.0f;
};

// Who hit this last (runtime): the killer, for experience.
struct LastHit {
    Entity source = entt::null;
};

// --- Factions and spawners (13.7) ----------------------------------------------

// Belongs to a faction (a record); `disposition` is how it feels about the
// player (-100..100: below 0 it's hostile). Attacking it lowers it.
struct FactionMember {
    std::string faction;
    int disposition = 0;
};

// Makes enemies from an encounter record around itself, up to `maxAlive`,
// again after `respawnSeconds` (0: once).
struct Spawner {
    std::string encounter;
    float radius = 3.0f;
    int maxAlive = 3;
    float respawnSeconds = 0.0f;
    int level = 1;
    bool started = false; // runtime
    float timer = 0.0f;   // runtime
};

struct SpawnedBy {
    Entity spawner = entt::null;
};

// --- Interactables (13.5) -------------------------------------------------------

// Something the player uses with the interact action: a lever, a sign, a bed, a
// locked chest or door. What happens comes from its other components (Door,
// Container, Merchant, ItemPickup) and its own fields.
struct Interactable {
    std::string verb = "Use";       // the prompt's verb: Open, Read, Pull, Sleep
    std::string name;               // what it's called on the prompt ("" = its Name)
    float range = 1.3f;
    bool locked = false;
    std::string key;                // an item id that unlocks it
    bool consumeKey = false;
    std::string requiresFlag;       // a story flag that must be set
    std::string setFlag;            // a story flag set when used
    std::string text;               // shown when used (a sign, a book)
    std::string event;              // emitted when used (scripts listen)
    bool once = false;
    bool used = false;
};

// A building or other closed place one can go into (development_plan.md Phase 27): its interior picture (the same
// building cut away: rooms, floor and furniture inside the walls' outline), shown while the player is inside its
// footprint (Interiors.h); its walls then the only solid cells, with a doorway on its `door` side.
struct Interior {
    std::string texture;         // the interior art; empty: none
    std::string door = "s";      // the doorway's side: s, e, n, w
    bool shown = false;          // the interior showing now (the player inside; in the editor, a preview)
    std::string exteriorTexture; // the outside art while the interior shows (put back after)
    // A painted building (docs/places/interiors.md §7): no exterior of its own (the painting shows it), so its sprite is
    // the interior, seen only while shown.
    std::string cells; // its footprint "x0,y0,x1,y1" in world cells (it has no Obstacle); empty: its Obstacle's
    std::string hides; // the sprites hidden while it shows: those whose PlaceNode statement starts with this
};

// A door: open, it doesn't block its cell.
struct Door {
    bool open = false;
    std::string openTexture;   // "" = keep the sprite
    std::string closedTexture;
};

// The scene's music (development_plan.md 12.6).
struct Music {
    std::string track;
    std::string ambience;
    std::string combat;
};

// Which entity of its scene file this is (its order there), set when a scene is
// read. Saved games list what changed in each scene by it (development_plan.md
// 12.5). Not reflected, so never written to scene files.
struct SceneOrigin {
    int index = -1;
};

struct PlaceNode {
    std::string place;     // the build's id (places/<id>/)
    std::string node;      // Spec node id ("w-old/plots#14")
    std::string statement; // PLL statement id ("plots[14].house")
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

// A named place in the scene (development_plan.md 3.5): where routines send
// people ("herb_garden", "tamsin_home"). Invisible in play.
struct Marker {
    std::string name;
};

// A way into another scene (development_plan.md 5.3): when the player steps
// into the gateway's cell during play, the game moves to `scene` (a
// project-relative scenes/*.aige), with the player standing at that scene's
// Marker `marker`.
struct Gateway {
    std::string scene;
    std::string marker;
};

// Follows a daily schedule (schedules/<id>.json, gameplay/Schedule).
struct Routine {
    std::string schedule; // schedules/<id>.json
    float speed = 1.5f;   // walking speed, cells per second
};

// Runtime only (not saved): where a Routine is in its day.
struct RoutineState {
    int entry = -1;                        // the schedule entry being carried out
    std::vector<std::pair<int, int>> path; // cells to walk
    size_t next = 0;
    bool arrived = false;
    float idle = 0.0f;  // wander: seconds until the next stroll
    float stuck = 0.0f; // seconds without progress (re-plan after a while)
};

// Runtime only: asleep (hidden; can't be talked to). `alpha` is the sprite's
// opacity to restore on waking.
struct Sleeping {
    float alpha = 1.0f;
};

// Someone the player can talk to (E within range): which conversation, and
// who they are in the World Bible (their name on screen, talk objectives).
struct Talk {
    std::string dialogue;  // dialogue/<id>.json
    std::string character; // World Bible key, e.g. character/villager
    float range = 1.5f;    // cells
};

} // namespace aige
