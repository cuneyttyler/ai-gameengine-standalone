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

### Example: moving interact off Space

1. Open **Project Settings ▸ Input** and find `interact` (E, Space, pad A; situation gameplay).
2. Remove the Space binding, so Space is free for a jump of your own.
3. Find `quicksave` and check it's still F6.
4. Press Play: **E** now talks, opens and picks up; Space does nothing until you bind it.

The change is saved to the project's `input.json`; scripts that ask for `action_pressed("interact")` keep working.

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

### Example: a "kick" action with its own clip and event

1. In Project Settings ▸ Input, type `kick` in "a new action: farm, parry, kick…" and press **Add action**.
2. **+ Key**: press **G**. **+ Button…**: pad Y. Description `Kick doors and barrels`.
3. **plays**: `attack_kick` (the player's kick clip, made in Generate ▸ Animate).
4. **event**: press **New event…** and name it `kick`. **effect**: `hit-dust`.
5. Tick **only near something it activates**, so the kick only plays beside a barrel or door that uses it.
6. Give the barrels a Reaction: trigger `input`, action `kick`, range `1.2`, becomes `prefabs/barrel-broken.prefab`.

In play, pressing G next to a barrel plays the kick clip with dust, sends `kick` (a script can count kicks with
`on("kick", fn)`), and the barrel turns into its broken prefab. Away from barrels, G does nothing.

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

### Example: a well where you fill a bucket, which takes three seconds

1. Select the well and choose **Reaction** from **+ Add component**.
2. **trigger** `input`, **action** `interact`, **range** `1.5`, **prompt** `Draw water`.
3. **requires** `bucket`; **takes** `bucket`; **gives** `bucket-water`.
4. **userAnimation** `use_pickup`; **duration** `3`; **text** `You fill the bucket.`; **cooldown** `2`.
5. **do**: `[{"op": "add_var", "var": "water_drawn", "value": 1}]`.

In play the prompt reads "E  Draw water". Without a bucket it says "Needs Bucket"; with one, the player works the well
for three seconds and gets a full bucket. Walking off halfway interrupts it and the empty bucket comes back.

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

### Example: a locked chest, a sign, and a guard who opens the gate

1. **Locked chest:** select the chest; add an **Interactable** (verb `Open`, name `Chest`, **locked** ticked, **key**
   `crypt-key`, **consumeKey** ticked) and a **Container** with **loot** `crypt-chest`. Prompt: "Open Chest"; without
   the key it stays shut; with it, the key is used up and the loot is rolled.
2. **Sign:** select the signpost; add an **Interactable** with verb `Read`, name `Sign`, **text** `North: Ravenford. East:
   the Old Mill.` Pressing E shows the text.
3. **Gate:** the town gate has a **Door** and an **Interactable** with **locked** and **key** `gate-key`. Corporal Hale
   carries `gate-key` in his Inventory and has a custom package step:

```json
{ "type": "act", "action": "interact", "around": "town-gate", "radius": 3 }
```

At dawn Hale walks to the gate and opens it with the key from his own pockets, just as the player would.

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

### Example: Tomas's wheat field, step by step

1. In Project Settings ▸ Input, **Add action** `farm`, **+ Key** F, **plays** `farm`.
2. Make the prefab `prefabs/field.prefab` with a Reaction: **trigger** `input`, **action** `farm`, **requires** `seeds`,
   **takes** `seeds`, **userAnimation** `farm`, **becomes** `prefabs/field-seeded.prefab`, **prompt** `Sow`.
3. `prefabs/field-seeded.prefab`: Reaction **trigger** `timer`, **seconds** `120`, **becomes**
   `prefabs/field-grown.prefab`.
4. `prefabs/field-grown.prefab`: Reaction **trigger** `input`, **action** `interact`, **userAnimation** `use_pickup`,
   **gives** `wheat*3`, **becomes** `prefabs/field.prefab`, **prompt** `Harvest`.
5. Paint a row of `field` prefabs by Tomas's farmhouse. Make two packages for Tomas: `tomas-sow` (template `tend`,
   location marker `fields`, data action `farm`, radius 12) and `tomas-harvest` (the same with action `interact`).
6. Give the player and Tomas `seeds*10` in their Inventories.

The player presses F on a field to sow it; two minutes later it's grown, and E harvests three wheat. Tomas does the same
with his own seeds, working whichever fields are free.

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

### Example: an inn door that opens as people come near, and a locked cellar hatch

1. Select the Golden Goose and choose **Build ▸ Animate** (or right-click its door ▸ **Animate a part…**). Mark the door
   and make its open clip.
2. In the new **Reaction: Moving Part**, set **activation** `auto`, **range** `1.5`, **door** ticked (the building's
   doorway).
3. Do the same for the cellar hatch, but set **activation** `use`, **prompt** `Open the hatch|Close the hatch`,
   **locked** ticked, **key** `cellar-key`, **fires** `hatch_opened`.

In play the inn door swings open whenever the player or Vara walks up, and shuts behind them. The hatch shows "Open the
hatch" but stays shut without the cellar key; with it, it opens and sends `hatch_opened`.

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

### Example: Vara reacts to the quest, a wave, and the alarm

1. Select Vara Bell and choose **Reaction: Character Rules** from **+ Add component**.
2. Enter this JSON list:

```json
[
  { "event": "quest:rats-in-the-cellar:20", "says": "Did I hear squeaking stop?", "once": true },
  { "event": "action:wave", "range": 3, "says": "Hello, love!", "clip": "wave" },
  { "event": "alarm", "if": [{ "flag": "alarm_raised" }],
    "do": [{ "op": "push_package", "package": "hide-in-cellar" }], "says": "Not again!" }
]
```

3. Give the bell tower's Reaction `do`: `[{"op": "set_flag", "flag": "alarm_raised"}, {"op": "send_event", "event":
   "alarm"}]`.

When the rat quest reaches stage 20, Vara says her line once. Pressing the `wave` action within three cells makes her
wave back. Ringing the alarm makes her cry out and hide in the cellar (her `hide-in-cellar` package is pushed).

See also: Crafting, Dialogue (conditions and effects), AI packages.

Questions: how do I make an NPC react to a quest stage? how do I make a character respond to the player's action? can a
Reaction open the inventory?
