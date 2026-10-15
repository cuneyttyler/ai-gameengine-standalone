# Lua Scripting Reference

Gameplay behaviour is written in Lua (`technical.md` §2–3). Attach a script to an entity
with a `LuaScript` component; the Inspector's **+ New script…** creates one from a template
under the project's `scripts/` folder, and **Open** opens it in your editor.

## Lifecycle

- A script's top-level code runs once when it loads — and again whenever you **save the
  file while the game is running** (hot reload). Script-level variables reset on reload.
- If the script defines `update(dt)`, it's called every fixed step (60 times per second)
  while playing. `dt` is the step length in seconds.
- Each entity gets its own environment: two entities running the same file don't share
  variables.
- Errors are printed to the console and leave the entity's previous behaviour in place;
  a broken script never crashes the game.

## Units

Positions and distances are **world grid units**: one tile is 1.0, and tile `(gx, gy)`
spans `[gx, gx+1) × [gy, gy+1)`, so cell centers are `n + 0.5`. Speeds are cells per second.

Entities are referred to by integer **ids** (as returned by `find_nearest` or `self_id`).

## Functions

| Function | Returns | Notes |
|---|---|---|
| `self_id()` | id | This entity. |
| `get_position()` | x, y | This entity's world position. |
| `set_position(x, y)` | — | Teleports; no collision. |
| `move_toward(x, y, speed)` | — | Walks toward a point for one step, with wall collision and Slow effects applied. |
| `position_of(id)` | x, y or nil | |
| `distance_to(id)` | number or nil | |
| `find_nearest(team)` | id, distance or nil | `team` is `"Player"`, `"Enemy"` or `"Neutral"`; only living entities with a `Faction`. |
| `is_alive(id)` | boolean | |
| `get_health([id])` | current, max or nil | This entity, or another by id. |
| `attack_toward(id)` / `attack_toward(x, y)` | boolean | Swings this entity's `MeleeAttack`; false while on cooldown. |
| `apply_status(id, kind, seconds, magnitude)` | — | `kind`: `"burn"`, `"poison"` (damage per tick; poison can't kill), `"slow"` (speed × (1 − magnitude)), `"regen"` (healing per tick). Re-applying refreshes rather than stacks. |
| `play_sound(path)` | — | Project-relative, e.g. `"assets/audio/hit.wav"`. |

## Example: a melee enemy

Chasing is handled by the entity's `Follow` component; the script decides when to swing
(`projects/demo/scripts/enemy_melee.lua`):

```lua
local swing_range = 1.1

function update(dt)
    local target, distance = find_nearest("Player")
    if target and distance < swing_range then
        attack_toward(target)
    end
end
```

## Combat components (set in the Inspector)

- `Health` — current/max. Reaching 0 removes the entity (the player stays, marked dead).
- `Faction` — `Neutral`, `Player` or `Enemy`. No friendly fire within a non-neutral team.
- `Hurtbox` — the area attacks can hit, plus invulnerability time after a hit.
- `MeleeAttack` — damage, reach, radius, cooldown, knockback, optional on-hit status effect.
- `Stats` — attack (added to damage dealt), defense (subtracted from damage taken, minimum
  1), moveSpeed (the player's walk speed in cells/s).
- `Pickup` — heals the player on contact (left on the ground at full health).
- `Follow` — walks toward a target entity within `range`, stopping at `stopDistance`.
