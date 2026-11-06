# Scripting with Lua

Writing gameplay scripts in Lua: where scripts attach, the script editor and Project ▸ Scripts, the script lifecycle and
hot reload, units and ids, events, timers and coroutines, script properties, scripted AI packages, and a worked example.
The function list is in "Lua API".

## When to use a script

Most things need no script: Reactions, Character Rules, AI packages, quests, dialogue effects and recipes cover farming,
doors, chests, shops, daily lives and quest logic as data. Use a Lua script when behaviour has an order, choices or
state that data can't express: a boss with phases, a puzzle, a ghost that appears only to whoever carries an amulet, a
character who cooks a recipe when the pot is empty.

A script can be attached in two ways:
- a **LuaScript** component on an entity (Inspector): the script runs for that entity while the game plays;
- a **script package** (an AI package with template `script` and `data.script`): the character runs the file's `run()`
  while that package is active.

Scripts live under the project's `scripts/` folder.

Questions: do I need a script? where do scripts go? how do I attach a script to an entity?

## Creating and editing scripts

- **Inspector:** on a LuaScript component, **+ New script…** creates one from a template under `scripts/`, and **Open**
  opens it.
- **Project ▸ Scripts** lists every Lua script in the project with what uses it (things in the open scene, packages).
  Search box: "Search scripts and what uses them". **Refresh** looks again. **New script…** asks for a name and opens
  `scripts/<name>.lua` in the script editor. Double-click a row or press **✎ Edit** to open one.
- **The script editor** (a window over the editor): the script with line numbers, **Tab** indents by four spaces,
  **Ctrl+S** or **Save** saves, **Close** (asks before discarding unsaved changes; Esc too). A dot ● marks unsaved
  changes. The header says what uses the script. **Open outside** opens it in the program your computer uses for .lua
  files. A new script says "A new script: Save writes it."

Saving while the game is running reloads the script in Play.

Questions: how do I create a new script? where is the script editor? can I edit scripts in my own editor? how do I find
which entities use a script?

## Script lifecycle and hot reload

- A script's top-level code runs once when it loads, and again whenever you save the file while the game is running
  (hot reload). Script-level variables reset on reload.
- If the script defines `update(dt)`, it's called every fixed step (60 times a second) while playing; `dt` is the step
  length in seconds.
- Each entity gets its own environment: two entities running the same file don't share variables.
- Errors are printed to the console and leave the entity's previous behaviour in place; a broken script never crashes
  the game. An error in an event handler doesn't stop the others.
- Reloading a script stops its handlers, timers and coroutines and runs its top-level code again; removing an entity
  stops its scripts' handlers.

Questions: when does my script run? does saving a script update the running game? why do my variables reset? what happens
when a script errors?

## Units and ids in scripts

- Positions and distances are world grid units: one tile is 1.0, tile (gx, gy) spans [gx, gx+1) × [gy, gy+1), so cell
  centres are n + 0.5. Speeds are cells per second.
- Entities are referred to by integer ids, as returned by `self_id()`, `find_nearest`, `entity_named`, `spawn`.
- Paths are project-relative: `"assets/audio/hit.wav"`, `"prefabs/skeleton.prefab"`, `"scripts/boss.lua"`.
- Records are named by type and id: `record("item", "iron-sword")`.
- Input actions are named, not keys: `action_pressed("farm")` keeps working when the player rebinds keys.

Questions: what units do scripts use? how do I refer to another entity in a script? where is the centre of a tile?

## Events, timers and coroutines

Things that happen in the game are events with a name and a table `e` (`e.name`, `e.entity` who it happened to,
`e.other` who did it, `e.amount`, `e.text`).

- `on(event, fn)` calls `fn(e)` whenever it happens (`"*"` for every event); `emit(event[, data])` sends your own.
- `after(seconds, fn)` once later; `every(seconds, fn)` repeatedly; `cancel(handle)` stops a timer or handler.
- `start(fn)` runs a coroutine now; inside it `wait(seconds)` pauses (`wait()` until the next step).
- Handlers and timers run as coroutines too, at the end of each step after combat, so they can `wait`.

Built-in events include: `hit`, `death`, `pickup`, `attack`, `victory`, `trigger_enter` / `trigger_exit`, `talk`,
`quest_started`, `quest_completed`, `quest_failed`, `quest_stage`, `objective_completed`, `cutscene_started`,
`cutscene_ended`, `ui_action`, `anim_event`, `effect`, `difficulty_changed`, `item_added`, `level_up`, `item_used`,
`item_combined`, `crafted`, `reaction_began`, `reaction_ended`, `reaction_interrupted`, `ability_used`,
`ability_learned`, `effect_applied`, `effect_removed`, `package_started`, `say`, and any event an input action, Reaction,
part or Interactable sends.

```lua
on("death", function(e)
    if e.entity == self_id() then return end
    wait(1.5)
    play_sound("assets/audio/bell.wav")
end)
every(2, function() apply_status(self_id(), "regen", 2, 1) end)
```

Questions: how do I react to an enemy dying? how do I run code after a delay? how do I wait inside a script? what events
are there?

## Script properties

A script can declare settings each entity using it may change in the Inspector:

```lua
properties = {
    swing_range = 1.1,   -- numbers, booleans and strings become editable fields
    taunt = "",
}
```

The values are the defaults. Each entity's own values appear under its LuaScript section (with ↺ to go back to the
default) and are written into `properties` after the script loads, so read them inside functions such as `update`, not
in top-level code. Editing a value while the game runs applies immediately. Prefabs carry property values too, and a
copy can override them like any other field.

Questions: how do I make a script setting editable in the Inspector? why is my property value ignored? can prefab copies
have different script settings?

## Scripted AI packages

A package with template **script** runs the Lua file in `data.script` for the character while the package is active.
The file must define `run()`; it runs as a coroutine with the whole API, so behaviour reads top to bottom. When `run()`
returns, the package's run ends and the stack is looked at again.

```lua
-- scripts/ai/cook.lua
function run()
    while true do
        local stove = find_part("cook", 15)
        if stove and move_to(stove.x, stove.y) then
            use_part(stove.id, stove.index)
            say("Smells good.")
            craft("stew", "stove", self_id())   -- from its own inventory
            wait(8)
        end
        local px, py = position_of(self_id())
        local x, y = random_point_near(px, py, 4)
        if x then move_to(x, y) end
        wait(3)
    end
end
```

`move_to(x, y)` walks there around walls (opening doors on the way) and returns true on arrival, false when there's no
way. `follow_path(name)` walks a named path. Use field packages (live, sleep, work) for the rest of the day and a script
package only where logic needs it; both sit in the same stack.

Questions: how do I write an AI package in Lua? what does run() do? how do I make a character walk somewhere in a script?

## Example: a melee enemy script

Chasing is done by the entity's Follow component (with no target it follows the player); the script decides when to
swing:

```lua
properties = {
    swing_range = 1.1, -- cells; a bit more than MeleeAttack.reach
    taunt = "",        -- a sound played when it first spots the player
}
local spotted = false
function update(dt)
    local target, distance = find_nearest("Player")
    if not target then return end
    if not spotted and distance < get_field(self_id(), "Follow", "range") then
        spotted = true
        if properties.taunt ~= "" then play_sound(properties.taunt) end
    end
    if distance < properties.swing_range then attack_toward(target) end
end
```

Combat components set in the Inspector: Health, Faction (Neutral, Player, Enemy; no friendly fire within a team),
Hurtbox, MeleeAttack, Stats (attack, defense, moveSpeed), Pickup, Follow (target, speed, stopDistance, range).

See also: Lua API, AI packages, Actions and Reactions.

Questions: can you show an example enemy script? how do I make an enemy attack when close?
