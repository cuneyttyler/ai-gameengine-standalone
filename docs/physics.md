# Game physics

Status: **draft for review (2026-10-21)**. Phase 20 in `development_plan.md`.

---

## 1. Goal, and what "physics" means per genre

Today: a custom 2D system (`physics2d`): axis-aligned boxes against the tile grid and
Obstacle footprints, with collide-and-slide for walking, plus A* pathfinding. It is right
for top-down ARPGs. Whether a game needs more depends on its genre more than on 2D vs 3D:

| Genre (`game-types.md`) | Needs |
|---|---|
| top-down / isometric ARPG, RPG, life-sim | kinematic movement against tiles and obstacles, triggers, projectiles, line of sight |
| platformer, metroidvania | a precise kinematic **platformer controller**: gravity, jumps, slopes, one-way and moving platforms |
| shoot 'em up, twin-stick | overlap tests for many bullets, screen bounds; no forces |
| falling-block and grid puzzles | **no physics**: grid logic |
| physics puzzle, destruction, vehicles | **rigid bodies**: forces, rotation, joints, stacking |
| 3D isometric, first/third person (later) | 3D physics: character controllers, rigid bodies, raycasts |

So the design is two layers in 2D, and a separate engine for 3D.

## 2. What exists elsewhere

| Library / engine | Model | Notes |
|---|---|---|
| **Box2D v3** (Catto, 2024; MIT, C) | rigid bodies (static, kinematic, dynamic), shapes, joints, sensors, continuous collision, multithreaded solver; v3.1 adds a character mover | the standard 2D rigid-body library |
| Chipmunk2D | rigid bodies | older, less active |
| Godot | `CharacterBody2D.move_and_slide`, `Area2D`, collision layers and masks, `RigidBody2D` | kinematic characters plus rigid bodies; layers/masks |
| Unity | `Rigidbody2D` (Box2D underneath), layer collision matrix | |
| Celeste / TowerFall (Thorson) | "actors and solids": integer-pixel movement, no rigid bodies | the reference for platformer feel |
| **Jolt Physics** (MIT) | 3D rigid bodies, character controllers | chosen for 3D in `technical.md` §12 |

Platformer feel: coyote time, jump buffering, variable jump height, apex hang, separate
rise and fall gravity (from Celeste and common practice).

## 3. Decision (proposed)

1. **Extend our kinematic layer** (what every current game uses): collision **layers and
   masks**, more shapes (circle, capsule), **sensors/triggers**, **queries** (raycast,
   shape cast, overlap), swept movement (no tunnelling), one-way platforms, slopes, moving
   platforms, and **character controllers** per genre (§5). Deterministic and grid-aware.
2. **Add Box2D v3 for dynamic bodies** when a game needs them (crates that topple, physics
   puzzles, debris, simple vehicles), behind the same components; the kinematic layer's
   queries see Box2D bodies and vice versa. A game that doesn't use rigid bodies doesn't
   step Box2D.
3. **3D physics with Jolt** comes with the 3D path (Later, `development_plan.md`), on the
   same component ideas.

## 4. Components and settings

| Component | Fields |
|---|---|
| `Collider2D` (extended) | shape (box, circle, capsule, tile), size, offset, `layer`, `mask`, `sensor`, material |
| `RigidBody2D` | type (static, kinematic, dynamic), mass or density, linear and angular damping, gravity scale, fixed rotation, continuous collision |
| `CharacterController2D` | mode (§5) and its parameters |
| `Trigger` | a sensor with filters (layer, tag, player only), once or repeating, events on enter, stay, exit |
| `Joint2D` (with Box2D) | revolute, distance, prismatic, weld, rope |
| `PhysicsMaterial` (record) | friction, bounciness, density |

Project Settings ▸ Physics: named **layers** (world, player, enemy, ally, projectile,
item, trigger, plus the game's), the **collision matrix**, gravity (for side-view games),
the fixed step (60 Hz now) and sub-steps.

## 5. Character controllers

| Mode | Behaviour | Parameters |
|---|---|---|
| `top-down` | 8-way or analog movement with acceleration and friction, collide-and-slide against tiles, obstacles and bodies; screen directions mapped to the isometric grid | speed, acceleration, friction, iso mapping |
| `grid-step` | moves tile by tile (roguelikes, Pokémon-like), turn-based or real-time | step time, diagonal allowed |
| `platformer` | gravity, variable jump height, coyote time, jump buffer, max fall speed, separate rise/fall gravity, apex hang, air control, wall slide and wall jump, dash, ladders, drop through one-way platforms, slopes, riding moving platforms; optional integer-pixel movement (Celeste-like) | jump height, time to apex, run speed, acceleration (ground, air), coyote and buffer times, and the rest |
| `free-flight` | shmups and twin-sticks: bounded to the screen or camera, no gravity | speed, focus speed (slow mode), bounds |
| `vehicle` (later) | top-down car: throttle, steering, drift | |

Presets of "feel" (tight, floaty, heavy) give the platformer's parameters; the editor's
**jump preview** draws the jump arc and reachable area from the current parameters.

## 6. Queries and events

- **Queries:** `raycast(from, to, mask)`, `shape_cast(shape, from, to, mask)`,
  `overlap(area, mask)`, `line_of_sight(a, b)` (tiles and occluders; used by perception,
  `ai-packages.md` §8); available in C++, Lua and to effects and abilities.
- **Events** on the event bus: `contact_begin/end`, `trigger_enter/exit`, `hit` (with
  impulse), `landed`, `wall_touched`.

## 7. Determinism, performance, editor

- Fixed step (60 Hz), seeded, so playtests replay; Box2D with a fixed sub-step count.
- Broad phase: a spatial hash for the kinematic layer (cells already exist), Box2D's own
  tree for bodies; sleeping bodies cost nothing.
- **Editor:** collider gizmos editable in the viewport; the layer matrix; a physics debug
  draw in Play (shapes, contacts, rays, triggers); the jump preview.
- **Checks:** a trigger on no layer; a dynamic body inside a solid; a mask that collides
  with nothing.

## 8. The Planner and physics

- The **genre profile** chooses the controller and the physics features (`planner-schema.md`
  §2.10); the Systems stage writes the controller's parameters from a feel preset and the
  layer matrix.
- **Platformer levels** need physics-aware building: a level generator must check every gap
  and ledge against the controller's jump arc (reachability by jump, not by walking). This is
  a new build type (`platformer-level`) in the place ontology, planned with v1.2, the
  version that brings side-view games (`roadmap.md`). Phase 20 (v1.0) builds the layers,
  sensors, queries, top-down and grid-step controllers and Box2D; the platformer and
  free-flight controllers of §5 come with v1.2.
- Out of scope: soft bodies, fluids, cloth.
