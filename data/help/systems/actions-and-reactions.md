# Actions and Reactions

What the player can do (input actions such as attack, interact and the project's own: farm, parry, kick), how things in
the world react without a script (the Reaction component and the Interactable), moving parts, Character Rules, and the
shared conditions and effects.

## Input actions the game uses

The game's actions live in Project Settings ▸ **Input** (saved to the project's `input.json`). Each has its keys, mouse
buttons and gamepad buttons (and sticks for axes) and the situation it works in: gameplay, dialogue, menu, cutscene or
any.

Built-in actions (rebindable, never removed) include: `move_x`, `move_y` (WASD, arrows, left stick), `attack` (left
mouse, Q, pad X), `interact` (E, Space, pad A), `fire` (F, pad RB), `block` (Left Ctrl, pad LB), `dodge` (Left Shift,
pad B), `inventory` (I, Tab, pad Y), `crafting` (C), `put_back` (right mouse, Z, pad B), `ability_1`-`ability_4` (1-4),
`spellbook` (K), `journal` (J), `skip` (cutscenes), `confirm`, `nav_up`, `nav_down`, `choice_1`-`choice_9` (dialogue),
`ui_up`, `ui_down`, `ui_select`, `ui_back`, `ui_context` (menus), `menu` (Esc, Start), `quicksave` (F6), `quickload`
(F9), `debug_stats` (F3), `debug_ai` (F4), `debug_physics` (F5).

**Interact is the default action**: talk, open, pick up, use. `attack` does something only for a character with
MeleeAttack (or RangedAttack for `fire`).

Questions: what keys does the game use? how do I rebind a key? what does the interact key do?

## Adding your own action

In Project Settings ▸ Input, type a name in "a new action: farm, parry, kick…" and press **Add action**; **Remove**
deletes one of the project's own; **Reset to defaults** restores the built-in map. Each action has a description, a
group, its bindings (**+ Key**, **+ Button…**; axes have + Key (+) and + Key (−)), and optionally:

- **plays**: a clip the player plays when it's pressed (`parry`, `attack_kick`);
- **event**: an event sent when it's pressed (**New event…**), for scripts, triggers and quests: `on("parry", fn)`;
- **effect**: an effect played on the player (a parry's flash);
- **screen**: a panel or one of the project's screens opened when it's pressed;
- **only near something it activates**: its clip, event and effect only when a part or Reaction using it is in range
  (cook only at a stove);
- **group**: actions sharing a key in one group are pressed together, and only the one whose target is nearest acts.

Example, right click to parry: add `parry`, bind MouseRight, plays `parry`. Scripts ask for actions by name
(`action_pressed("farm")`), so rebinding changes nothing in them.

Questions: how do I add a new key action? how do I make a key play an animation? how do I make a key open a screen?

## The Reaction component

An entity with a **Reaction** (the `Action` component in files) does something when its trigger happens, to whoever
uses it:

- `trigger`: `input` (its `action` pressed within `range`), `event` (its `event` sent), `timer` (`seconds` after it
  appears), `touch` (walked onto), `near` (someone comes within range; `awayAnimation` when all have gone).
- Requirements: `requires` (items the user must carry: `hoe, seeds*2`), `requiresFlag`, `accepts` (one of these items
  must be in the player's hand: a key, a lockpick), `if` (conditions).
- What happens: `takes` and `gives` items (`wheat*3, gold*5`), `userAnimation` and `animation` clips, `effect` and
  `userEffect`, `becomes` (a prefab it turns into after the animations), `emits` (an event), `setFlag`, `text`, `do`
  (effects), `prompt` (what the player is shown, with the key; empty: Use), `station` (opens the Crafting list with that
  station's recipes).
- Limits: `once`, `cooldown` seconds.
- Taking time: `duration` or `untilAnimation`; `do` runs at the start, gives/becomes/`end` when over, `interrupted` if
  cut short (user hit or walking off, a cutscene), and what it took comes back unless `refund` is off. Events
  `reaction_began`, `reaction_ended`, `reaction_interrupted`.

Missing an item it requires or takes, it says what ("Needs 2 seeds, Hoe") and does nothing.

Questions: how do I make an object do something when the player presses a key near it? how do I make a field the player
can farm? how do I make something take time?

## Who uses a Reaction, and the Interactable

Anything can use a Reaction, not only the player; the user's inventory is the one `requires`, `takes` and `gives` use:

- **the player**: its action pressed, the nearest such thing in range;
- **characters**: the package step `act` and the template `tend` (walk to the nearest free one and use it); a guard
  opens a gate with `act` and a key from its pockets;
- **scripts**: `act(user, target)` or `act(user, "farm")`;
- **events and timers**: no user.

The **Interactable** is a Reaction with set settings (trigger input, action interact) plus a verb and name for the
prompt, `locked` with a `key` (and `consumeKey`), `requiresFlag`, `setFlag`, `text` (a sign, a book), `event`, `once`,
`becomes`. With a **Door** it opens and closes, a **Container** gives its contents, an **ItemPickup** is picked up, a
**Merchant** opens the shop. The prompt says what it'll do ("Open Chest", "Read Sign").

Every use runs the same steps: can it (not used up or cooling, in range, items, flag) → animations → takes, gives, flag,
event, text → becomes once the animations end → once and cooldown.

Questions: can NPCs use Reactions? what is an Interactable? how do I make a locked chest? how do I make a sign?

## Farming without a script

Three prefabs, one Reaction each:

| prefab | Reaction |
|---|---|
| `field` | trigger input, action `farm`, requires and takes `seeds`, userAnimation `farm`, becomes `field-seeded` |
| `field-seeded` | trigger timer, seconds 120, becomes `field-grown` |
| `field-grown` | trigger input, action `interact`, userAnimation `use_pickup`, gives `wheat*3`, becomes `field` |

Add the `farm` action in Project Settings ▸ Input (bound to F) and give the player a `farm` clip (Generate ▸ Animate).
A farmer NPC tends the fields with a package: template `tend`, data action `farm`, radius 12, location the fields; a
second package with action `interact` harvests. The same Reactions serve the player and the farmer.

Questions: how do I make farming? how do crops grow over time? how do I make an NPC farmer?

## Moving parts and doors

A **Reaction: Moving Part** (AnimatedPart) is a part of a picture that moves: a door, a window, a stove, a lever. Its
**activation** (`opens`): project (Project Settings ▸ Game ▸ doorsOpen), auto (as someone comes near), use (the door
shortcuts or a double click), action (the input action in `trigger`), event, always, or open (an open doorway). It has a
`clip` and `closeClip`, `range`, `prompt` ("Open the door|Close the door"), `fires` (an event each time), `station`,
`duration` (then it shuts by itself), `playerClip` (and `playerLoop`, `holdsPlayer`), `if`, `do`, `end`, `door` (a
doorway of its building), `locked`, `key`, `consumeKey`. Build ▸ Animate or right-click ▸ Animate a part… makes them.

Doors open for anyone who moves: the player, and characters walking by their packages (a locked one only with the key
in their inventory). Characters use parts with the `use-part` and `act` steps and Lua `use_part`.

Questions: how do I make a door open when someone comes near? how do I make a lever? how do I lock a door with a key?

## Character Rules and shared conditions and effects

A thing has one Reaction; a character can respond to many things with **Reaction: Character Rules** (`Responses`): a
JSON list of `{"event", "if", "do", "clip", "says", "range", "once"}`. `event` is any event's name, `quest:<id>:<stage>`
when a quest reaches that stage, or `action:<name>` when the player presses one of the project's actions; `range` needs
the player that near. Example: `{"event": "action:wave", "range": 3, "says": "Hello!", "clip": "wave"}`.

Reactions, Interactables, Moving Parts and recipes share `if` and `do`: the same JSON lists as dialogue's conditions
and effects (text that isn't a JSON list never holds and does nothing). `do` can open screens
(`{"op": "open_screen", "screen": "inventory"}`: also crafting, spellbook, journal, or a screen id), and change what a
character does: `{"op": "push_package", "package": "chase-player", "who": "Mara"}`, `pop_package`,
`{"op": "say", "text": "..."}`, `{"op": "send_event", "event": "alarm"}`.

See also: Crafting, Dialogue (conditions and effects), AI packages.

Questions: how do I make an NPC react to a quest stage? how do I make a character respond to the player's action? can a
Reaction open the inventory?
