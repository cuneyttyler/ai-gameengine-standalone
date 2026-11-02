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

## 2. Things that react: the Reaction component

An entity with a **Reaction** (Inspector) does something when its trigger happens, to whoever (or whatever) uses it.
(The Inspector calls it **Reaction**, so it isn't mistaken for an input action; in files and code it is still `Action`.)
The **Interactable** is a Reaction with set settings: trigger input, action `interact`, plus what doors, chests, signs
and merchants add. Both go through the same use (`Act(user, target)`), so anything can use either.

| Field | |
|---|---|
| `trigger` | `input` (its `action` pressed within `range` of it), `event` (its `event` emitted), `timer` (`seconds` after it appears), `touch` (walked onto) |
| `action` | the input action (farm, interact, any of the project's) |
| `event` | the event, for `trigger: event` |
| `seconds` | for `trigger: timer` |
| `range` | how close the user must be, in cells |
| `requires` | items the user must carry, kept unless taken too (`hoe, seeds*2`); empty: none |
| `requiresFlag` | a story flag that must be set |
| `takes` | items taken from the user (`seeds`, `ore*2`); all must be carried, or it doesn't happen |
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

Missing anything it `requires` or `takes`: it says what ("Needs 2 seeds, Hoe"), does nothing and takes nothing. An item
named in both counts once, at the larger number.

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

### The Interactable, a Reaction with set settings

| Interactable | As a Reaction |
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
   first); the user carries all it `requires` and `takes`; the story flag is set.
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

## 5. Conditions and effects

Action, Interactable and AnimatedPart (and recipes) have **`if`** and **`do`**: the same JSON lists as dialogue
([story.md](story.md): flags, quests, variables, items, gold, level, day, attributes, time, factions, difficulty; set
flags, start quests, give items...). Text that isn't a JSON list never holds and does nothing.

`do` can also open what the player sees: `{"op": "open_screen", "screen": "inventory"}` (`crafting`, `spellbook`,
`journal`, or a screen's id in `ui/screens`), and `close_screen`. So can an input action's **screen** (Project Settings ▸
Input), and a screen's button with `action('open:inventory')` or `action('close:shop')`: no script needed.

`do` can change what a character does, with no script: `{"op": "push_package", "package": "chase-player", "who": "Mara"}`
(no `who`: the character or thing it runs on), `pop_package`, `{"op": "say", "text": "..."}`, `{"op": "send_event", "event":
"alarm"}`.

**Taking time**: a Reaction with `duration` (seconds) or `untilAnimation`. `do` runs at the start; what it gives, its
`becomes` and `end` when it's over; `interrupted` if it's cut short (its user hit or walking off, a cutscene), and what
it took comes back unless `refund` is off. It sends `reaction_began`, `reaction_ended`, `reaction_interrupted`. A moving
part sends the first two and has `end`. A recipe with `seconds` has `start` and `interrupted`, and its inputs come back if
it's cut short; one under way is kept in a save. A Reaction under way isn't: a save loaded ends it, and what it took stays
taken.

## 6. Items in hand and recipes

**In hand**: pick an item in the inventory: one with no use of its own by a click; any by a right click (`ui_context`
on keys and gamepads) ▸ **Use on...**. Then:

- another item in the inventory: the recipe of those two is made, or "That doesn't work";
- close the window and click a thing, or press interact near it: its Action runs if it `accepts` the item (seeds on a
  field, a key on a lock), or a station's recipe of that item alone is made, or the station's list opens;
- right click or Esc (`put_back`, the gamepad's B): put away.

An Action with `accepts` works for the player only with the item in hand; anyone else just carries one. It takes the
item only if `takes` says so. Scripts: `hold_item(id)`, `held_item()`, `put_back_item()`, `craft(id, station)`. Events:
`item_used` (text: the item), `item_combined` and `crafted` (text: the recipe).

**Recipes** (`recipe` records, Records panel):

| Field | |
|---|---|
| `inputs` | items used up, each with a count |
| `keeps` | items needed but kept (a knife) |
| `gives` | what it makes |
| `at` | a station, a Reaction whose `station` names it (`stove`); empty: anywhere |
| `seconds` | how long it takes (what it makes comes after this; not kept in a save made meanwhile) |
| `clip`, `effect`, `text` | on the player, as it's made |
| `if`, `do` | as §5 |

**Two items** combine by using one on the other. **More than two**: right click an item ▸ **Crafting** (the recipes using
it), the `crafting` action (C, all that can be made here), or a station (a Reaction with `station`: its recipes too).
Lines that can't be made are greyed, and say what's missing.

Example, a stove: a Reaction with `station: stove`, and a recipe `{"name": "Stew", "inputs": [{"item": "meat"},
{"item": "water"}, {"item": "turnip", "count": 2}], "keeps": ["pot"], "gives": [{"item": "stew"}], "at": "stove"}`.

## 7. Characters that respond

A thing has one Reaction. A character can do many things, so it has **Character Rules** (`Responses`): a list of
`{"event", "if", "do", "clip", "says", "range", "once"}`. `event` is any event's name, `quest:<id>:<stage>` when a quest
reaches that stage, or `action:<name>` when the player presses one of the project's own actions; `range` needs the player
that near. The planner writes these from each character's behaviour plan (⑦ Systems).

## 8. Where it lives

| | |
|---|---|
| Actions and their keys | `engine/gameplay/InputMap.h/.cpp` (`InputAction`: `plays`, `event`), `input.json` |
| The Reaction component | `engine/gameplay/Components.h` (`Action`, runtime `ActionState`) |
| `Act(user, target)`, triggers (presses, timers, touches, events) | `engine/gameplay/GameSession.cpp` (`Act`, `StepActions`) |
| The NPC step `act` | `engine/gameplay/Behaviour.cpp` (`AiContext::act`) |
| Lua `act` | `engine/gameplay/GameSession.cpp` |
| Input settings (add, remove, plays, event) | `apps/editor/ui/src/components/InputSettings.vue` |
| Recipes, the item in hand | `engine/gameplay/Crafting.h/.cpp`, `GameSession` (`Craft`, `UseHeldOn`, `StepHeldItem`) |
| The Crafting list, an item's right-click options | `engine/game/PlayWindows.cpp` |
