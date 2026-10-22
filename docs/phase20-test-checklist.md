# Phase 20 — Manual Test Checklist

The user-test checkpoints from Phase 20 (`development_plan.md`, "Physics v2"), for one
end-of-phase pass. Automated tests cover the logic: layers, masks, shapes, raycasts, overlap and
line of sight, triggers entering and leaving, fast projectiles that don't pass through, masked
projectiles, grid-step movement, a toppling stack of crates, a character pushing a crate, a
distance joint, debug shapes (`physics_test.cpp`), and the AI's physics checks (`test_physics.py`).
This list covers how it plays. The design is `physics.md` (see "As built").

**Before you start**
- **Rebuild:** close the editor, then `cmake --build build --config Release --target editor sandbox`.
  The first configure fetches Box2D v3.0.0.
- A scene with the player, a few enemies, some walls.

## 20.1 Layers, triggers, queries, projectiles

- [ ] Your existing top-down game plays as before: walking, walls, melee, arrows, spells.
- [ ] Add an entity with **Trigger** `{"name": "gate", "mask": "player", "radius": 1.5}` and a
      script on it: `on("trigger_enter", function(e) notify("toast", "Entered " .. e.text) end)`.
      Walk in: one toast. Stand there: no more. Walk out and in again: another.
      Tick **once**: only the first time.
- [ ] An enemy walking through the trigger doesn't fire it (mask `player`).
- [ ] A script: `local hit = raycast(x, y, x + 8, y)` returns the first wall or collider;
      `line_of_sight` is false through a wall; `overlap(x, y, 2)` lists who is near.
- [ ] Very fast arrows or firebolts at close range still hit (no passing through).
- [ ] An enemy's **Collider2D** `layer` set to `ghost`: arrows still hit it unless their mask
      excludes it; **mask** `world` on a collider lists what it collides with.

## 20.2 Grid-step

- [ ] Add **CharacterController2D** `{"mode": "GridStep", "stepTime": 0.18}` to the player.
      Arrow keys move exactly one tile per step, eased; holding keeps stepping; diagonal input
      steps along one axis (tick **diagonal** to allow diagonals).
- [ ] Walking into a wall: it turns but doesn't move. Isometric and top-down both step along
      the grid's lines.

## 20.3 Rigid bodies and joints

- [ ] Put a crate (sprite + **Collider2D** + **RigidBody2D** Dynamic) in the way. Walking into it
      pushes it; it slides and stops (damping); it doesn't go through walls.
- [ ] Project Settings ▸ Physics: **gravityY** 10. A stack of four crates leaning slightly topples
      and the crates turn as they fall (sprites rotate). Set gravity back to 0.
- [ ] A **Joint2D** Distance between a static post and a crate (`length` 2): push the crate; it
      swings around the post at that distance. Revolute: a hinged gate.
- [ ] A scene with no rigid bodies: frame stats (F3) unchanged from before.

## 20.4 Debug view

- [ ] In play, **F5**: colliders yellow, triggers green, dynamic bodies orange, static grey; F5
      again hides them.
- [ ] Editor: View ▸ **Physics shapes** shows them while editing.

## AI panel

- [ ] "Add a trigger at the cave mouth that shows a message when the player enters": the AI
      adds a named Trigger with mask `player` and a script using `trigger_enter`.
- [ ] "Make these barrels pushable": RigidBody2D (Dynamic) with Collider2D on each.
- [ ] "Hang a lantern on a rope from that post": a Joint2D Distance to a body.
- [ ] "Make movement tile by tile": CharacterController2D GridStep on the player.
- [ ] A typo'd mask ("playr") is caught by validation before applying.
