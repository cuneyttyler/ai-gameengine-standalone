# Actions

What the player can do (attack, interact, and the project's own: farm, parry, kick), what keys do it, and how things
in the world react, without a script. Related: [scripting.md](scripting.md) (`action_pressed`, `on`),
[../assets/animations.md](../assets/animations.md) (clips played by name).

## 1. The game's actions (Project Settings ▸ Input)

The project's `input.json` lists its actions, each with its keys, mouse buttons and gamepad buttons (and sticks for
axes) and the situation it works in (gameplay, dialogue, menu, any).

- **Built in**: move_x, move_y, attack, interact, fire, block, dodge, inventory, ability_1-4, menu… Rebound, never
  removed (a project made before one existed gets it added).
- **The project's own**: **Add action** (a name: farm, parry, kick), **Remove**. Each has a description, its keys, and
  optionally:
  - **plays**: a clip the player plays when it's pressed (`parry`, `attack_kick`; any clip of its sheet, by name).
  - **event**: an event emitted when it's pressed, for scripts (`on("parry", function(e) ... end)`).
  - **effect**: an effect played on the player when it's pressed (a parry's flash).

**Effects** are records (`effect`: particles, a sound, a short light, a flash, a screen shake, hit-stop, slow motion,
in any mix): the engine's presets (Project ▸ Effects ▸ Try, **Customise** to copy one into the project) or the
project's own (**+ New**, then its parts in Records ▸ effect). Actions, input actions, hits, deaths and spells play
them by name.

**Interact is the default action** (E, Space, the gamepad's A): talk, open, pick up, use. The engine isn't for RPGs
only, so a character comes with no attack: `attack` (left click, the gamepad's X) does something only for a character
with **MeleeAttack** (or RangedAttack: `fire`), added in the Inspector when a game has fighting. The action templates
(RPG, Action RPG, Action-adventure) and a planned game whose brief has combat give their player one.

Right click to parry: add `parry`, bind MouseRight, plays `parry`.
Scripts ask for actions by name (`action_pressed("farm")`), so rebinding changes nothing in them.

## 2. Things that react: the Action component

An entity with an **Action** (Inspector) does something when its trigger happens, to whoever (or whatever) uses it.
The **Interactable** is an Action with set settings: trigger input, action `interact`, plus what doors, chests, signs
and merchants add. Both go through the same use (`Act(user, target)`), so anything can use either.

| Field | |
|---|---|
| `trigger` | `input` (its `action` pressed within `range` of it), `event` (its `event` emitted), `timer` (`seconds` after it appears), `touch` (walked onto) |
| `action` | the input action (farm, interact, any of the project's) |
| `event` | the event, for `trigger: event` |
| `seconds` | for `trigger: timer` |
| `range` | how close the user must be, in cells |
| `requires` | an item the user must carry (`seeds`); empty: none |
| `requiresFlag` | a story flag that must be set |
| `takes` | items taken from the user (`seeds`, `ore*2`) |
| `gives` | items given to the user (`wheat*3`) |
| `userAnimation` | the clip the user plays (farm, use_pickup, pray) |
| `animation` | the clip this plays |
| `effect` | an effect played on this when it happens: dust, sparks, a glow, a sound (an effect record) |
| `userEffect` | an effect played on the user |
| `becomes` | the prefab it turns into once the animations have played |
| `emits` | an event emitted, for scripts |
| `setFlag` | a story flag set |
| `text` | shown when it happens |
| `once` | works only once |
| `cooldown` | seconds before it works again |

Missing what it `requires`: it says so ("Needs seeds") and does nothing.

### Who uses it

Anything, not only the player: the user is whoever acts. Its inventory is the one `requires`, `takes` and `gives` use,
and it plays `userAnimation`.

- **The player**: its `action` pressed, the nearest such thing within `range`.
- **Characters** (Behaviour packages): the procedure step **act**, `{"type": "act", "action": "farm", "radius": 12}`:
  the nearest thing with that action (free: not used up, not cooling down), walked to, used. A farmer's work package
  is `act farm` in a loop; a guard opens a gate with `act` on it (an Interactable: its key from the guard's pockets).
- **Scripts**: `act(user, target)`, or `act(user, "farm")` for the nearest thing with that action in range; true when
  it happened.
- **Events and timers**: `trigger: event` / `timer` act with no user (or the event's entity as the user).

### The Interactable, an Action with set settings

| Interactable | As an Action |
|---|---|
| (the interact key) | trigger `input`, action `interact` |
| `range` | `range` |
| `key` (+ `locked`, `consumeKey`) | `requires` the key (+ `takes` it when it's used up) |
| `requiresFlag`, `setFlag` | the same |
| `text` | `text` |
| `event` | `emits` |
| `once` | `once` |
| `animation` | `animation` (its verb's clip when empty: Open plays `open`) |
| `becomes` (+ `becomeAfterAnimation`) | `becomes`, after the animations |
| Door, Container, Merchant, ItemPickup beside it | what they add: open/close, loot, shop, pick up |

It stays as its own component (doors, chests, signs, merchants are made with it), and both run through `Act`.

### One way to use things: `Act(user, target)`

Every use, whoever does it, runs the same steps:

1. **Can it?** The target isn't used up (`once`) or cooling down; the user is within `range` (characters walk there
   first); the user carries what it `requires`; the story flag is set.
2. **Animations**: the user plays `userAnimation`, the target plays `animation`.
3. **Effects**: `takes` from the user's inventory, `gives` to it; `setFlag`; `emits` (the event's entity the target,
   its other the user); `text` shown (to the player).
4. **Becomes**: once the longer of the two animations has played, the target turns into `becomes` in its place (what's
   left in a container kept).
5. **After**: `once` marks it used; `cooldown` starts.

An Interactable also opens or closes its Door, gives its Container's contents, opens its Merchant's shop.

## 3. Farming, script-free

Three prefabs, one Action each:

| Prefab | Action |
|---|---|
| `field` | trigger input, action `farm`, requires `seeds`, takes `seeds`, userAnimation `farm`, becomes `field-seeded` |
| `field-seeded` | trigger timer, seconds 120, becomes `field-grown` |
| `field-grown` | trigger input, action `interact`, userAnimation `use_pickup`, gives `wheat*3`, becomes `field` |

The player's `farm` clip: Generate ▸ Animate, the player's character, AutoSprite, custom "hoeing the soil", named
`farm` (or Plays on `use`). The `farm` action: Project Settings ▸ Input ▸ Add action, bound to F.

A farmer NPC tends the fields: a Behaviour package with the built-in template **tend** (`data: {"action": "farm",
"radius": 12}`, its location the fields): act on the nearest free field, wait a moment, again; nothing free, it goes to
its location and waits. A second package for `interact` harvests. The same Actions serve the player and the farmer.

## 4. Doors and NPCs

A door is an Interactable with a Door (verb Open). An NPC on its way through a closed door, or a package step `act`
on it, opens it as the player would: its key from the NPC's own inventory, its `open` clip played, its Door toggled.

## 5. Where it lives

| | |
|---|---|
| Actions and their keys | `engine/gameplay/InputMap.h/.cpp` (`InputAction`: `plays`, `event`), `input.json` |
| The Action component | `engine/gameplay/Components.h` (`Action`, runtime `ActionState`) |
| `Act(user, target)`, triggers (presses, timers, touches, events) | `engine/gameplay/GameSession.cpp` (`Act`, `StepActions`) |
| The NPC step `act` | `engine/gameplay/Behaviour.cpp` (`AiContext::act`) |
| Lua `act` | `engine/gameplay/GameSession.cpp` |
| Input settings (add, remove, plays, event) | `apps/editor/ui/src/components/InputSettings.vue` |
