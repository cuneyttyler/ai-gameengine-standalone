# The Animator: which clip plays when, and things held and worn

How an animated entity plays its sheet in the game: the Animator component, the built-in animators (humanoid,
quadruped, flyer, prop) and your own animator records, how idle and walk follow speed, how a state picks its clip
(several attacks, weapons), playing clips by name, and attachments and equipment drawn on a character's points.

## The Animator component

An entity is animated by an **Animator** component (Inspector): it plays an animation sheet on the entity's Sprite. Its
fields:

| Field | What it does |
|---|---|
| `sheet` | the animation sheet, `assets/<…>.anim.json` (a character's `.character.anim.json`) |
| `animator` | the state machine: `humanoid`, `quadruped`, `flyer`, `prop`, or an animator record's id |
| `speed` | the playback rate (1 = as the clips say; 0 to 10) |
| `corpseSeconds` | how long a dead one stays, so its death clip is seen (0 to 30) |
| `clips` | the clips a state plays, by hand: `attack=attack_punch|attack_kick; open=lid_up` (empty: the sheet's clips) |
| `pick` | among several clips for a state: `weapon` (default), `random`, `order` (in turn: a combo), `first` |
| `attackClass` | stands in for the weapon's class when choosing attack clips (sword, axe, unarmed…) |

The entity also needs a **Sprite** (the Animator sets which part of the picture it shows each frame). Make player, Make
NPC and Make prefab set all of this up. An unknown animator name falls back to `humanoid`. Animators run in the editor
too, outside Play: a character stands in its idle pose facing its Facing.

Questions: how do I animate an entity? what is the Animator component? how do I slow down an animation? what does corpseSeconds do?

## Built-in animators: humanoid, quadruped, flyer, prop

| Animator | States | Moves between them |
|---|---|---|
| `humanoid` | idle, walk, attack, cast, use, hurt, die | idle → walk when speed > 0.1, back when speed ≤ 0.1; attacking, casting, using, hurt, dead from any state |
| `quadruped` | idle, walk, attack, hurt, die | the same, without cast and use |
| `flyer` | fly, attack, hurt, die | attack, hurt, die from any state; otherwise fly |
| `prop` | idle, open, close, break | open when `open && !closed`; open → close when `closed`; break when dead |

Attack, cast, use and hurt last one clip and then go back to the first state; a lower-priority change doesn't cut them
short (no walking mid-swing), but hurt (priority 2) and death (priority 9) do. Each state plays the clip of the same
name unless told otherwise.

What sets things off: **speed** comes from the entity's velocity, or, when it is moved without one (an AI package's
walk, a cutscene's move), from how far it moved, smoothed so one still frame doesn't drop it to idle (a teleport or a
drag in the editor isn't walking). **attacking** comes from a combat attack; **hurt** and **dead** from taking damage and
dying; **casting** from an ability; **open / closed** from a Door's open state (closed only once it has been open) or a
Container once looted; **health** is the share of Health left.

Questions: when does the walk animation play? why does my character slide without walking? what states does the humanoid animator have? how does a chest know to open?

## Animator records: your own state machine

For anything the built-ins don't cover, write an **animator** record (Project ▸ Records ▸ animator, `records/animator/
<id>.json`) and put its id in the Animator's `animator` field:

```json
{"states": [{"id": "idle"}, {"id": "angry", "clip": "stomp", "speed": 1.5}, {"id": "hit", "exit": "end"}],
 "transitions": [{"from": "idle", "to": "angry", "if": "angry > 0"},
                 {"from": "angry", "to": "idle", "if": "angry == 0"},
                 {"from": "*", "to": "hit", "if": "hurt", "priority": 2}],
 "default": "idle"}
```

- **states**: `id`, the `clip` it plays (default: its id), `exit: "end"` to go back to the default state when the clip
  ends, and a `speed`.
- **transitions**: `from` (a state or `*` for any), `to`, `if` and `priority` (the highest that holds wins).
- **Conditions**: terms joined by `&&`; each `name`, `!name`, or `name` compared with a number (`>`, `<`, `>=`, `<=`,
  `==`, `!=`). Names: `speed`, `dead`, `health`, `attacking`, `hurt`, `casting`, `using`, `open`, `closed`, or any
  parameter a script sets with `anim_set(id, "angry", 1)`.
- **default**: the starting state (else the first).

Questions: how do I make my own animator? what conditions can transitions use? how do I set an animator parameter from Lua?

## How a state picks its clip: several attacks, weapons

On entering a state the animator picks its clip, in this order:

1. **The Animator's own `clips`** (Inspector), per entity: `attack=attack_punch|attack_kick; open=lid_up`. A state
   listed plays one of those clips.
2. **The sheet's clips for it**: the clip named like the state (`attack`), its **variants** (`attack_slash`,
   `attack_punch`…), and any clip whose **plays on** names the state.
3. Among several: with `pick: weapon` (the default), the weapon's choice: the equipped weapon's `animation` (a clip or
   its variant, `slash`), else its `class` (`sword` prefers clips with "sword" in the name: `attack_sword`); unarmed
   prefers clips with "unarmed" or "punch". Then by `pick`: `random`, `order` (in turn, a combo), `first`, or at random
   for `weapon` when nothing matched.

A state with no clip plays `idle`, else the sheet's first clip. So one character can have `attack_sword`,
`attack_axe` and `attack_punch` (made with Generate ▸ Animate's weapon class), and the right one plays for what it holds.

Questions: how do I give a character several attacks? how does the game choose which attack to play? how do I make a combo?

## Playing any clip by name: Lua, cutscenes, interactables, abilities

Any clip can be played by name, whatever the animator:

- **Lua**: `play(id, "dig")` (also `anim_trigger(id, "dig")`) plays the state of that name if the animator has one,
  else the clip `dig` once, then goes back to the default state. `play(id, "attack_kick")` plays the attack state with
  that clip. `play(id, "stop")` goes back to the default state. A clip played by name holds against everything but hurt
  and death. `anim_set(id, "name", value)` sets a parameter for an animator record.
- **Cutscenes**: the `animate` step's trigger, the same way.
- **Interactables**: used, it plays its `animation` (Inspector; empty: its verb, "Open" plays `open`, when the sheet has
  that clip). Its `becomes` (the prefab it turns into) waits for the clip when `becomeAfterAnimation` is on (the default):
  the lid opens, then the opened chest takes its place.
- **Abilities**: their `animation` (cast, or any clip).

Questions: how do I play an animation from a script? how do I make a chest play its open animation? how do I play a dance animation?

## Props: a chest that opens, a torch that flickers

A chest is an entity with a Sprite, an Animator (`prop`, its sheet with an `open` clip, maybe `close` and `break`) and an
Interactable (verb Open). Used, it plays `open` and stays on its last frame (the clip's loop `once` or `hold`). A Door
plays `open` and `close` as it toggles.

The quick way: animate the chest's picture in Generate ▸ Animate with a motion like "the lid swings open" (it is
recognised as a thing, so the non-humanoid model animates it), then **Make prefab** in its history. The clip is named
`open`, and a `close` clip (the same frames backwards) is added; both play once and hold. A looping motion of a thing (a
torch's flicker, a flag waving) becomes a looping `idle` clip, which the `prop` animator plays all the time.

Questions: how do I make an animated chest? how do I make a torch flicker? how do I make a door open and close?

## Attachments and equipment: things held and worn

One character, one swing animation, any number of swords. A sheet's clips carry named **points** per direction and
frame (`hand`, `offhand`, `head`, `back`…: a position, an angle, in front of or behind the body), set in the sheet
editor's point tool or by Generate ▸ Animate's **holds a weapon**.

The **Attachment** component (Inspector) puts its entity on another's point: `to` (the other's Name), `point` (empty:
its bottom middle), `offsetX`, `offsetY`, `angle`, `pivotX` / `pivotY` (where this picture is held, 0–1: a sword's
grip), `pointing` (which way the art points: 0 right, −90 up), `size` (its longest side in the other's frame pixels; 0:
its own size), `layer` (auto follows the point's front/behind, front, behind, own) and `turn` (off: kept upright, like a
hat). It works with any entity: a lantern on a cart, a rider on a horse's `saddle`.

An entity with **Equipment** draws what it wears and wields from the item records: the item's `sprite` (its picture held
or worn; the `icon` is the inventory's), `point` (default by slot: weapon → hand, offhand → offhand, head → head), `grip`,
`pointing`, `size`, `layer`, `class` and `animation`. Changing the weapon swaps the picture; Equipment's `show` turns it
off. These pictures aren't saved with the scene; they are made from the Equipment when it loads.

See also: Animations tab and sheet editor, Character sheets, Animate tab (weapons).

Questions: how do I put a sword in a character's hand? how do I attach a hat? how does equipment show on the character? what is the Attachment component?
