# Animations

How things move: where animations come from (Generate ▸ Animate), how a clip is tied to what happens in the game
(walking, attacking, opening a chest, any action of your own), several attacks for one character, and things held or
worn (one swing animation, any number of swords). Everything here can be set by hand in the Inspector or the sheet
editor; what's generated only fills in defaults.

Related: [character-sheets.md](character-sheets.md) (the sheet format, which side shows),
[autosprite.md](autosprite.md) (the default animator), [bfl-sprites.md](bfl-sprites.md) (BFL experiments),
[../systems/effects-animation.md](../systems/effects-animation.md) (sheets, animators, effects).

## 1. Making animations

Generate ▸ Animate picks an image or a character and an action. Preferences ▸ AI ▸ Animate chooses who animates it:

| Provider | What it does | Cost | Status |
|---|---|---|---|
| **AutoSprite** (`autosprite`, default) | A picture in its own view; a character as an **isometric pack, all 8 directions in one render** | 5 credits a picture (turbo), 15 a character's action (≈ $0.10 / $0.29 on Pro) | Needs AutoSprite's Pro plan for API access |
| PixelLab Animate with text v3 (`pixellab:animate-v3`) | 8 frames from the picture, up to 256 px; south, east, north, west mirrored | PixelLab credits | Works; too slow to use (its queue takes minutes a call) |

Tried and dropped (still taken when asked for by name, no longer offered; [bfl-sprites.md](bfl-sprites.md)):

- **BFL image strip / guide grids**: stick figures or mannequins in a grid for the model to paint over; FLUX.3 Image
  klein ignored the poses, FLUX.2 klein 4b painted mannequins.
- **FLUX 3 video** (`bfl:flux-3-video`): the pose as both keyframes of a looping clip, one cycle cut into frames. The
  best motion, but $0.75 a direction (HD); one call for every direction moved badly. Its Draft mode ($0.06 a second)
  was never tried.
- **BFL image, editing copies** (`bfl:image`): the pose copied into 8 squares and FLUX.2 klein 4b asked to change each
  copy's legs and arms ($0.014 a direction). East and the diagonals were acceptable, walking away barely moved or
  turned to profile; judged not good enough.

### Humanoid or not

AutoSprite rigs what it animates as a person unless told otherwise (`isHumanoid`). It's decided from what the thing
is: the Animate form's "What it is", the picture's `.gen.json` description, its name, a character's description.
People words (herbalist, knight, goblin…) or thing and animal words (chest, torch, wolf…); unclear, the LLM is asked;
failing that a character is humanoid and a picture isn't. Kept as `humanoid` in the `.gen.json` / the character
sheet.

### Motions

With AutoSprite the Animate panel lists its motions (autosprite.io/docs/reference-animation-types): basic (idle, walk,
run, attack, jump), combat (punch, kick, block, hurt, hit_react, dodge, dodge_lean, death, shoot), magic and skills
(cast, pickup, push, pull, throw), emotes (wave, sit, kneel, point, sleep, victory, dance, talk), movement (dash, fall,
climb, swim, crouch, roll, fly, slide), or **custom**, said in words ("the lid swings open, gold glints inside").

### Clean-up

AutoSprite draws a dark outline round its figures and its frames come out darker than the picture they're made from:
the outline is peeled off (near-black pixels on the silhouette's edge, a ring at a time, at its 256 px) and the colours
matched to the pose (one shift for all frames, so they don't flicker), then the frames are cut from one box and brought
to the idle pose's height (`autosprite.cleaned`, `fitted`). The clip plays at AutoSprite's own speed (24 frames a
second × its `speedMultiplier`, about 10 for a 12-frame walk).

### Directions

AutoSprite animates one direction a request (the picture's own view), except its isometric pack: a character takes one
pack an action, so every direction has its own frames (no mirroring, no "north-east plays north"). PixelLab paints
south, east and north and mirrors the west side.

### Removing an animation

Assets ▸ Characters, right-click ▸ **Edit…** (the sheet editor), pick the clip, **Remove**, **Save** (Revert until
then). Its frames stay in the picture, unused.

## 2. Which clip plays when

An animated entity has an **Animator** (Inspector): its `sheet` and an `animator`, a state machine. Each state plays a
clip; transitions move between states on conditions. Built in:

| Animator | States |
|---|---|
| `humanoid` | idle, walk, attack, cast, use, hurt, die |
| `quadruped` | idle, walk, attack, hurt, die |
| `flyer` | fly, attack, hurt, die |
| `prop` | idle, open, close, break |

Your own: an animator record (`records/animator/<id>.json`, states and transitions with conditions like
`speed > 0.1`, `attacking`, `open && !closed`, `health < 0.3`, or any parameter a script sets).

What sets things off:

| | From |
|---|---|
| speed | moving (Velocity2D) |
| attacking | a combat attack |
| hurt, dead | taking damage, dying |
| casting | an ability, by its `animation` |
| open / closed | a Door's `open`; a Container once looted |
| any name | `play(id, name)` in Lua, a cutscene's `animate` step, an Interactable being used, an ability's `animation` |

### 2.1 Which clip a state plays

On entering a state the animator picks its clip, in this order:

1. **The Animator's own `clips`** (Inspector), per entity: `attack=attack_punch|attack_kick; open=lid_up`. A state
   listed plays one of those clips.
2. **The sheet's clips for it**: the clip named like the state (`attack`), its **variants** (`attack_slash`,
   `attack_punch`…), and any clip whose **Plays on** names the state.
3. Among several, by the weapon (2.3), then by the Animator's **`pick`**: `weapon` (the default: the weapon's choice,
   else at random), `random`, `order` (in turn: a combo), `first`.

A state with no clip plays `idle`, else the sheet's first clip.

**Plays on** is set per clip in the sheet editor (stored as the clip's `plays` in the sheet), so a clip named `punch`
can be the attack: `plays: attack`. A clip made by Generate ▸ Animate gets it from its motion: punch, kick, shoot →
attack; cast → cast; death → die; hurt, hit_react → hurt; pickup, push, pull, throw → use; a custom "open…" on a prop
→ open. Anything else plays on its own name only (`dance`: played by `play(id, "dance")`).

### 2.2 Custom actions

Any clip can be played by name, whatever the animator:

- **Lua** `play(id, "dig")` (also `anim_trigger(id, "dig")`): a state of that name if the animator has one, else the
  clip `dig` once, then back to the default state. `play(id, "attack_kick")` plays the attack state with that clip.
  `play(id, "stop")` goes back to the default state. `anim_set(id, "angry", 1)` sets a parameter for an animator
  record's conditions.
- **Cutscenes**: the `animate` step's trigger, the same way.
- **Interactables**: used, it plays its `animation` (Inspector; empty: its verb, "Open" → `open`, "Pray" → `pray`,
  when the sheet has that clip).
- **Abilities**: their `animation` (cast, or any clip).

### 2.3 Several attack types

One `attack` state, several clips: `attack_slash`, `attack_thrust`, `attack_punch`, `attack_kick` (or any clip with
`plays: attack`). Which one:

- **The weapon** (Equipment ▸ weapon, an item record): its `animation` names the clip or its variant (`slash`), else
  its `class` (`sword`) prefers clips with that word (`attack_sword`, `attack_sword_heavy`). Unarmed prefers clips
  with `unarmed` or `punch`.
- **An ability**: "Kick" with `animation: attack_kick`.
- **A script**: `play(id, "attack_kick")`.
- **Variety**: the rest by the Animator's `pick` (random, or in order for a combo).
- **By hand**: the Animator's `clips` (`attack=attack_kick`) overrides all of it for that entity; its `attackClass`
  stands in for the weapon's class.

### 2.4 Props: a chest that opens

A chest is an entity with a Sprite, an Animator (`prop`, its sheet with an `open` clip, maybe `close` and `break`) and
an Interactable (verb Open). Used, it plays `open` and stays on its last frame (the clip's loop: `once` / `hold`). A
Door plays `open` / `close` as it toggles. An Interactable's `becomes` (the prefab it turns into) waits until the clip
has played (`becomeAfterAnimation`, on by default), so the lid opens, then the opened chest takes its place.

An image animated in Generate ▸ Animate is written as a sheet whose clip is named by what it does (`open`, `break`,
a looping `idle` for a torch's flicker), so **Make prefab** gives a prop that plays it.

## 3. Things held and worn

One character, one swing animation, any number of swords (or a torch, a hat, a shield, a rider on a horse, a flag on a
tower). Not a character per weapon.

| | How | Cost |
|---|---|---|
| Baked | a character per weapon, every animation redrawn | characters × weapons × actions × directions |
| Layered frames | each weapon animated for every action | every weapon × every action |
| **Attachment points** (used) | the animation carries a point per frame; a held thing is one picture drawn there | one picture a thing |

### 3.1 Points

A sheet's clips carry **named points** per direction and frame: `hand`, `offhand`, `head`, `back`, or any name. Each
is a position in the frame (pixels), an angle (degrees, 0 = right, clockwise) and whether it's **in front of or
behind** the body. Stored in the sheet:

```json
"clips": {"attack_slash": {"directions": {"s": [40, 41, 42]},
                           "points": {"hand": {"s": [[38, 30, -60, 1], [44, 28, 0, 1], [40, 36, 60, 1]]}}}},
"points": {"head": [32, 10, 0, 1]}
```

A list of one point holds for every frame; the sheet's `points` hold where a clip has none. Mirrored directions mirror
the point (x and angle). Set them in the **sheet editor's point tool** (pick a point name, click where it is in each
frame, drag for the angle, toggle front/behind) or let Generate fill them in (3.4).

### 3.2 Attaching anything to anything

The **Attachment** component (Inspector) puts its entity on another's point:

| Field | |
|---|---|
| `to` | the other entity's Name |
| `point` | the point (`hand`); empty: the other's bottom middle |
| `offsetX`, `offsetY`, `angle` | added to the point (pixels of the other's frame, degrees) |
| `pivotX`, `pivotY` | where this picture is held (0–1 of it): a sword's grip |
| `pointing` | which way this picture's art points (degrees: 0 right, −90 up), turned to the point's angle |
| `size` | its longest side in the other's frame pixels (0: its own size at the other's scale) |
| `layer` | auto (the point's front/behind), front, behind |
| `turn` | turned with the point's angle (off: kept upright, a hat) |

Works with any entity: a sword prefab on a character's hand, a lantern on a cart, a rider on a horse's `saddle`.

### 3.3 Equipment shows itself

An entity with **Equipment** draws what it wears and wields, from the item records:

| Item record field | |
|---|---|
| `sprite` | its picture held or worn (the `icon` is the inventory's) |
| `point` | where (default by slot: weapon → hand, offhand → offhand, head → head) |
| `grip` | where it's held, 0–1 of the picture ([0.5, 0.9]: the hilt) |
| `pointing` | which way the art points (degrees) |
| `size` | longest side in the wearer's frame pixels |
| `layer` | auto, front, behind |
| `class` | weapon class: sword, axe, spear, dagger, greatsword, bow, staff… (2.3) |
| `animation` | its attack clip or variant |

Changing Equipment ▸ weapon (Inspector, a script, the inventory) swaps the picture; Equipment's `show` turns it off.
These pictures aren't saved with the scene (made from the Equipment when it loads).

### 3.4 Points from generated animations

Generate ▸ Animate, a character with AutoSprite: **holds a weapon** asks for the motion with the character holding a
**plain bright magenta rod** in its hand (`autosprite.ROD`; the motion becomes a custom one, said with it), and
**Weapon class** names the clip `attack_<class>`. In each frame the
rod is found by its colour: its grip end is the hand, its direction the angle, a rod partly hidden is behind the body.
Then it's removed (the same colour key as the backgrounds), leaving a clean animation with a `hand` track. Fix any
frame with the point tool.

### 3.5 Weapon classes

A dagger stab and a greatsword swing move differently, so attack animations are made **per weapon class**, not per
weapon: characters × classes × actions, any number of weapons in each class. A weapon's `class` picks its class's
clips (2.3).

## 4. Animated parts (a door, a window, a sign)

Only the part that moves is animated, not the whole picture (a scene painted whole stays one still picture):
right-click a building, any thing, or a painted scene's picture ▸ **Animate a part…**, drag a box round the part, say
how it moves ("the door swings open"). The box is cut out of the picture, animated like any picture (Generate ▸
Animate's provider; its clip named by what it does: `open`), and placed over it as an **overlay**: a Sprite with its
Animator (`prop`), an **Attachment** to the thing (where the box was, so it moves and turns with it; the thing gets a
name of its own if copies share one) and an **Action** (trigger touch: it plays `open` when someone comes to it; set
it to an input or an event instead in the Inspector). Assembled scenes attach it to the building; Paint scenes to the
painting. `EditorApp` `part:animate`, `PlacePart`.

### 4.1 Build ▸ Animate

After a place's art is made, the Build panel animates what has to move, so a built place works as built, in two steps
(the page says so at its top):

1. **① Identify doors**: each building's doors looked for in its picture (`/parts/find`, every ground-level door) and
   shown with their boxes ("door found: check it"). Check them: click a picture for the big view; where a box is off or a
   door is missing, **Draw the doors** (a box each, **Clear** to start again) and **Use these doors**. **Identify** on a
   row looks for that building's doors again.
2. **② Animate**: each door identified opened by **PixelLab** (Animate with text v3: the doorway's crop as the first
   frame, 8 frames, kept to the doorway; the in-engine swing if PixelLab fails), and each thing that's used animated
   (`/animations/generate`). They play in the list as they're done.

Who animates (Preferences ▸ AI): **Humanoid animation** (AutoSprite, or PixelLab) for people and creatures; **Non-humanoid
animation** (PixelLab) for things: a door, a chest, a lever, a flag. AutoSprite is a character animator: given a door it
added a person in every frame and redrew the door; PixelLab kept the door as drawn and opened it inward into a dark room.
A request says which is which by the picture's words (`is_humanoid`).

- **Assembled**: the Assets page's **Animate** tab (the panel moves to it when the art is made; **To Animate →**). Each
  building's door is found in its art (the vision model's box, `/parts/find`, Mistral Large 4: on a test house it was
  the only model near the door) and **opened from its own pixels** (`/parts/door`, `ai_service/app/doors.py`): the
  doorway fitted to the picture (its sides the frame's edges, its top where the planks start, under a beam, its top and
  bottom along the wall's slope, either face), the panel swinging inward on its hinge, darker as it turns, over a dark
  doorway, all cut to the doorway and to the door's own pixels (a post or railing in front stays drawn). 24 frames at
  20 fps, free, the art kept exactly. AutoSprite was tried for doors and redrew the wall and added people. It becomes
  an **AnimatedPart** on the building's prefab, so every copy has its moving door. Each thing that's used — a chest, a door, a gate, a portcullis, a drawbridge, a lever, a sarcophagus, a
  cupboard, a cage — is animated whole and its prefab gets an Animator (`prop`) and an Interactable whose verb plays it.
- **Paint**: the **⑥ Animate** step after ⑤ Mark finds each painted building's door in its part of the painting (and
  opens it the same way); the
  doors are kept with the build (`places/<id>/parts.json`) and put over the painting as overlays when the place is
  built (`AddPaintingEntities`). Things that are used are sprites of their own, animated on the Assets page as above.
- **What triggers them** is decided by what they are: a building's door opens as someone comes near and closes as they
  go (AnimatedPart, or an Action with trigger `near` and an `awayAnimation`); a chest opens when used (its Interactable,
  the interact key); a lever is pulled when used. All can be changed in the Inspector.
- **Close** is the `open` frames played backwards (no second animation; each held at its last frame).
- Known gap: a step or tread in front of a door's foot, the same wood as the door, darkens a little with it mid-swing.
- The page shows each target's picture with its door's box, where it is (copies in the scene, or its cell), what
  triggers it, its status (to animate, finding its door, animating, animated, failed, skipped), a progress bar, and
  **Animate all** (doors free; things about 5 AutoSprite credits each), each one's **Animate** / **Skip**. `apps/editor/BuildAnimate.cpp`,
  `components/AnimatePartsView.vue`.

### 4.2 How doors open

A building's animated door (its **AnimatedPart**) opens one of two ways:

- **use**: the player's door shortcuts — Project Settings ▸ Game ▸ **doorActions**, input actions comma-separated
  (`interact` by default: E, Space, the pad's A; add others, e.g. `interact, open_door`) — or a **double click** on
  the door (**doorDoubleClick**), the player within its range. Pressed again, it shuts. Other characters open it as
  they come (they don't press keys). While it's shut its doorway blocks the way in; a path still goes through it for
  characters, who open it as they reach it.
- **auto**: by itself, as anyone comes near.

The project's way is Project Settings ▸ Game ▸ **doorsOpen** (`use` by default). Each door can say its own: Inspector
▸ AnimatedPart ▸ **opens** (`project`, `auto`, `use`), and **locked** / **key** / **consumeKey**: a locked door stays
shut, its doorway blocked, and "Locked: needs Iron Key" shows when the player tries it; carrying the key, it unlocks
for good and opens. Either way it closes once everyone has gone. Near is measured from the door itself (its foot), not
the building's middle, so a big building's door opens too.

**Animate a part…** (right-click a building, or anything with a picture) does the same for a building generated or
imported by hand: drag a box round the door or press **Find the door** (the vision model), choose **a door** (opened
from its own pixels) or **something else** (AutoSprite, from the motion you give), and for a door how it opens and
whether it's locked, with which key. On a prefab's copy, every copy gets it.

In code: `AnimatedPart` / `PartOpen` (`engine/gameplay/Components.h`), `DoorOpens` / `DoorwayShut`
(`engine/gameplay/Interiors.cpp`), the doorway in `TileGrid::Build`, characters' paths in `Behaviour.cpp`, the rules in
`GameSession::StepActions`, `RequestUseAt` (the editor's Play and `apps/sandbox/main.cpp` for a double click).

### 4.3 Several doors, double doors, archways

A building can have several doors (a front and a side door, a big double door and a small one, doors on both faces).
The finder asks for every ground-level door (`/parts/find`: each with its box, `leaves`, `shape`, `open`), the main one
first; Build ▸ Animate gives each its own row (`<key>`, `<key>#1`, …), each opened and redone on its own. An open
archway (no door in it) isn't animated ("skipped: an open archway"). A double door (`leaves` 2, or a door wider than
three quarters of its height) opens as two halves, each on its own side's hinge.

In the big view (click a picture) **Draw the doors** takes as many boxes as the building has doors (numbered, **Clear**
to start again); **Redo with N doors** makes the building's doors exactly those, one from each box as drawn. **Animate
a part…** adds a door (or redoes the one whose box it's drawn over).

On the building: AnimatedPart's own fields are its first door; `more` (a JSON list) holds the others, each with the same
fields (`DoorsOf` / `SetDoors`). Each door has its own overlay ("part", "part1", …), its own open / use / lock, and its
own doorway in the walls (the footprint's edge cell nearest it), passable while it's open or opens by itself unlocked.

## 5. In the Inspector

| Component | Fields |
|---|---|
| Animator | `sheet`, `animator`, `speed`, `corpseSeconds`, `clips` (state = clips), `pick`, `attackClass` |
| Interactable | … `animation` (played when used), `becomeAfterAnimation` |
| AnimatedPart | `sheet`, the box (`x`, `y`, `width`, `height`), `clip`, `closeClip`, `range`, `opens`, `locked`, `key`, `consumeKey` |
| Attachment | `to`, `point`, `offsetX`, `offsetY`, `angle`, `pivotX`, `pivotY`, `pointing`, `size`, `layer`, `turn` |
| Equipment | its slots, `show` |

Sheet editor: clips (loop, speed, **Plays on**), directions, frames, **points**. Records editor: item `sprite`,
`point`, `grip`, `pointing`, `size`, `layer`, `class`, `animation`; animator records.

## 6. Where it lives

| | |
|---|---|
| Clip choice, triggers, points, attachments | `engine/gameplay/Animation.h/.cpp` (`ChooseClip`, `TriggerAnimation`, `UpdateAttachments`, `PlaceAttachments`) |
| Interactables' animation, becomes after it, Lua `play` / `anim_set` | `engine/gameplay/GameSession.cpp` |
| Shown equipment never saved | `Transient` (`engine/ecs/Registry.h`), left out of `EntitiesInOrder`; picking one picks its wearer |
| Item record fields | `engine/records/Records.cpp` |
| Plays on, point tool | `apps/editor/ui/src/components/SheetEditor.vue` |
| Generated clip names, the rod, clean-up | `ai_service/app/charsheet.py` (`clip_for`), `ai_service/app/autosprite.py` (`take_rod`, `fit_points`, `cleaned`) |
| Tests | `tests/fx_test.cpp` (SheetsCarryPlaysOnAndPoints, AttacksChooseTheirClip, ClipsPlayByName, EquipmentShowsOnTheHand) |
