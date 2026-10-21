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
| `get_field(id, component, field)` | value or nil | Any field the Inspector shows, e.g. `get_field(self_id(), "Health", "max")`. Enums come back as their option name (`"Enemy"`), entity references as ids. |
| `set_field(id, component, field, value)` | boolean | The reverse; enums accept the option name or index. False if the entity lacks the component or the value has the wrong type. |
| `spawn(prefab, x, y)` | id or nil | Places a copy of a prefab, e.g. `spawn("prefabs/skeleton.prefab", 4.5, 2.5)`. Tile prefabs paint the cell under the point. |
| `start_quest(id)` | — | Starts `quests/<id>.json` (if it hasn't been started). See [story.md](story.md). |
| `complete_objective(quest, objective)` | — | Completes that objective if it's open in the quest's current stage. |
| `quest_state(id)` | string | `"inactive"`, `"active"`, `"completed"` or `"failed"`. |
| `quest_stage(id)` / `set_stage(id, stage)` / `fail_quest(id)` | number / — / — | A quest's current stage (0 when not active); move it to a stage; fail it. See [story.md](story.md#questsidjson). |
| `get_var(name)` / `set_var(name, value)` | value / — | Story variables, shared with dialogue and quest conditions (numbers, text, booleans; `nil` removes). |
| `used(name)` | — | Counts as using something, for `use` and `examine` objectives. |
| `difficulty()` / `difficulty_step(category)` / `set_difficulty(id)` | string / number / boolean | The difficulty level's id; a category's step, 1-5 (3: as designed); change the level. See [difficulty.md](difficulty.md). |
| `play_cutscene(id)` / `skip_cutscene()` / `in_cutscene()` | boolean / — / boolean | Plays `cutscenes/<id>.json` (false if it doesn't load). See [story.md](story.md#cutscenes-cutscenesidjson). |
| `set_flag(name[, value])` | — | Story flags shared with dialogue conditions; `value` defaults to true. |
| `get_flag(name)` | boolean | |
| `time_of_day()` | number | Hours since midnight on the game clock, e.g. `13.5`. See [story.md](story.md#routines-schedulesidjson). |
| `day()` | number | The day number, from 1. |
| `record(type, id)` | table or nil | A record's fields (`records/<type>/<id>.json`), with its base and defaults applied, e.g. `record("item", "iron-sword").value`. See [records.md](records.md). |
| `records(type)` | table | The ids of every record of a type. |
| `on(event, fn)` | handle | Calls `fn(e)` whenever `event` happens; `"*"` for every event. See [Events](#events-timers-and-coroutines). |
| `emit(event[, data])` | — | Sends your own event; `data` may have `entity`, `other`, `amount`, `text`. |
| `after(seconds, fn)` / `every(seconds, fn)` | handle | Calls `fn` once after a delay, or repeatedly. |
| `cancel(handle)` | — | Stops a timer or a handler. |
| `start(fn)` | — | Runs `fn` as a coroutine now; inside it `wait(seconds)` pauses it. |
| `wait(seconds)` | — | Pauses the running coroutine (handlers, timers and `start` run as coroutines). |
| `action_down(name)` / `action_pressed(name)` | boolean | An input action held, or pressed this frame (`input.json`: `attack`, `interact`, `menu`…). Rebinding keys changes nothing in scripts. |
| `axis(name)` | number | An axis action in [-1, 1], e.g. `axis("move_x")` (keys or a gamepad stick). |
| `play_sound_at(path, x, y)` | — | A sound at a point in the world: quieter with distance, panned left or right. |
| `play_music(path)` | — | Music instead of the scene's (a boss fight); `play_music("")` goes back to the scene's. |
| attributes, items, experience, ranged attacks, factions | | `attribute`, `add_modifier`, `give_item`, `take_item`, `count_item`, `equip`, `use_item`, `award_xp`, `level`, `take_perk`, `drop_loot`, `shoot`, `dodge`, `is_hostile`, `set_disposition`: see [gameplay-systems.md](gameplay-systems.md#lua). |
| `push_package(id, package)` / `pop_package(id, package)` | — | A character AI package on top of its stack, until popped ([ai-packages.md](ai-packages.md)). |
| `bark(text)` | — | A one-line remark from this entity, shown at the bottom (`a|b` picks one). |
| `awareness_of([id])` | number | How aware a character is of the player, 0-1. |
| `set_visible(id, visible)` / `face(id)` | — | Show or hide an entity; turn this one toward another. |
| `start_dialogue(path)` | boolean | Starts a conversation with this entity as the speaker. |
| `find_object(type[, radius])` | id or nil | The nearest smart object of a type. |
| `move_to(x, y)` | — | In a coroutine (a scripted package's `run()`): walks there, around walls, and returns on arrival. |

## Events, timers and coroutines

Things that happen in the game are **events**, each with a name and a table `e`:
`e.name`, `e.entity` (who it happened to), `e.other` (who did it), `e.amount`, `e.text`.

| Event | `entity` | `other` | `amount` / `text` |
|---|---|---|---|
| `hit` | who was hit | the attacker | damage / — |
| `death` | who died | the killer | — / their prefab |
| `pickup` | who picked it up | the pickup | healing / the pickup's prefab |
| `attack` | who swung | — | — |
| `victory` | — | — | — (the last enemy died) |
| `talk` | who was talked to | the player | — / their World Bible key |
| `quest_started`, `quest_completed`, `quest_failed`, `quest_stage`, `objective_completed` | — | — | — / the quest's id |
| `cutscene_started`, `cutscene_ended` | — | — | — / the cutscene's id |

Handlers run at the end of each step, after combat. They, timers and `start` run as
coroutines, so they can `wait`:

```lua
on("death", function(e)
    if e.entity == self_id() then return end
    wait(1.5)
    play_sound("assets/audio/bell.wav")
end)

every(2, function() apply_status(self_id(), "regen", 2, 1) end)

start(function()
    while true do
        move_toward(4.5, 4.5, 2)  -- one step
        wait()                     -- until the next step
    end
end)
```

Reloading a script stops its handlers, timers and coroutines and its top-level code runs
again; when an entity is removed, its scripts' handlers stop too. An error in a handler is
reported like any script error and doesn't stop the others.

## Script properties

A script can declare settings that each entity using it may change in the Inspector:

```lua
properties = {
    swing_range = 1.1,   -- numbers, booleans and strings become editable fields
    taunt = "",
}
```

The values in that table are the defaults. Each entity's own values (shown under its
`LuaScript` section, with a ↺ to go back to the default) are written into `properties`
after the script loads, so **read them inside functions** like `update`, not in top-level
code. Editing a value while the game runs applies immediately. Prefabs carry property
values too, and a copy can override them like any other field.

## Example: a melee enemy

Chasing is handled by the entity's `Follow` component (with no target it follows the
player); the script decides when to swing (`projects/demo/scripts/enemy_melee.lua`):

```lua
properties = {
    swing_range = 1.1, -- cells; a bit more than MeleeAttack.reach
    taunt = "",        -- a sound to play when it first spots the player
}

local spotted = false

function update(dt)
    local target, distance = find_nearest("Player")
    if not target then
        return
    end
    if not spotted and distance < get_field(self_id(), "Follow", "range") then
        spotted = true
        if properties.taunt ~= "" then
            play_sound(properties.taunt)
        end
    end
    if distance < properties.swing_range then
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
- `Follow` — walks toward a target entity (the player, if none is set) within `range`,
  stopping at `stopDistance`.
