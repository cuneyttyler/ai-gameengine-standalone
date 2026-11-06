# Assets ▸ Animations, Assets ▸ Characters and the sheet editor

Where the project's animation sheets are listed, played and edited by hand: the Assets panel's Animations and Characters
tabs, Project ▸ Effects ▸ Sheets, and the sheet editor (clips, frames, directions, mirrors, speed, "plays on" and
attachment points).

## Assets ▸ Animations: every animation sheet, playing

The Assets panel's **Animations** tab shows every animation sheet in the project (`assets/**/*.anim.json`) that isn't a
character: made with Generate ▸ Animate (Make animation), imported in Project ▸ Effects, written by the AI panel, or
made by Build ▸ Animate. Each card plays its idle clip (or its first) facing its first direction, with its name and a
button per clip: click a clip to play it, click again to go back; double-click one to open the sheet in Project ▸
Effects. The search box at the top of the Assets panel filters by path and clip names.

Right-click a card:

- **Make prefab**: a prefab that plays the sheet, with the `humanoid` animator when the sheet has walk, attack or cast
  clips ("as a character"), else the `prop` animator (its idle looping).
- **Add to selected**: the selected thing in the scene plays this sheet (an Animator is added or changed, `humanoid` or
  `prop` as above; on a prefab copy it's an override). Select something first.
- **Edit…**: the sheet editor. **Show in Effects**: the sheet on Project ▸ Effects ▸ Sheets.
- **Show in Explorer**.
- **Delete…**: after confirming, the sheet goes, its picture too unless another sheet uses it, and the animated parts
  that played it are removed (their prefabs', the scene's): the picture under them shows as it was painted.

These are off during Play.

### Example: Making the tavern's existing fireplace flicker

You placed a still `fireplace` prop earlier and have since made a `fireplace-flames` animation in Generate ▸ Animate
(**Make animation**).

1. In the Scene view, click the fireplace to select it.
2. Open the Assets panel ▸ **Animations** and find the `fireplace-flames-sheet` card (type `fireplace` in the search
   box if there are many). It is already playing its idle clip.
3. Right-click the card and choose **Add to selected**.

Result: the fireplace gets an Animator with the `prop` animator and the sheet, and its flames loop in the editor and
in Play. On a prefab copy, the change is kept as an override of that copy only.

Questions: where are my animations? how do I give an object an animation? how do I make a prefab from an animation? how do I delete an animation?

## Assets ▸ Characters: every character sheet

The Assets panel's **Characters** tab shows the project's character sheets, one card each: the character in its idle
pose facing the camera, its name, and a button per clip (idle, walk, attack…). Clicking a clip plays it in every
direction in turn (twice round each direction), so you can check all eight at a glance. Empty, it says "No characters
yet. Make one with Generate ▸ Character".

Right-click a character:

- **Make player**: the player prefab from this sheet, replacing the scene's player.
- **Make NPC**: an NPC prefab, placed in the middle of the view.
- **Animate…**: Generate ▸ Animate with this character picked (every direction at once).
- **Edit…**: the sheet editor. **Show in Explorer**.
- **Delete…**: the sheet, its picture unless shared, and the parts that played it.

### Example: Checking Mira's walk before using her as the player

1. Open the Assets panel ▸ **Characters**. Mira Quill's card shows her idle, facing the camera, with the buttons
   `idle` and `walk`.
2. Click `walk`. She walks twice facing south, then twice south-east, east, and so on through all eight directions.
3. Her north-east walk looks wrong, so right-click the card ▸ **Animate…**, pick **walk** again and click **Animate**
   to replace the clip.
4. When it's done, check again, then right-click ▸ **Make player**.

Result: the scene's player is replaced by Mira, walking correctly in every direction.

Questions: where are my characters? how do I preview all directions of a walk? how do I animate a character from the Assets panel?

## Project ▸ Effects ▸ Sheets

The Project panel's **Effects** tab has three pages: **Sheets**, **Particles & effects** and **Cutscenes**. Sheets lists
every animation sheet on the left (with its clip count). Pick one to see its picture and size and each clip playing, per
direction, with its frames as a strip and its mirrored directions ("w = e mirrored"). Problems found in sheets (a missing
picture, a direction mirroring one it hasn't) are listed at the top. **Refresh** reads them again. **Edit** opens the
selected sheet in the sheet editor.

Two ways to make a sheet from a picture you already have:

- **New sheet from a grid**: the picture's path, the frame size (width × height), the number of **columns**, the
  **fps**, and **rows**: each row's clip name, comma-separated (`idle, walk_s, walk_e, walk_n`: a row named `walk_e` is
  the east direction of `walk`). **Make sheet** writes `<picture>.anim.json` beside it.
- **From Aseprite**: the path of Aseprite's JSON export (File ▸ Export Sprite Sheet ▸ JSON). **Import**: tags become
  clips (frame durations kept), a tag named `walk_e` is the east direction of `walk`; the sheet is written beside it.

To use a sheet, give an entity an **Animator** with this sheet and an animator (humanoid, quadruped, flyer, prop, or an
animator record).

### Example: A sheet for a hero strip drawn in another program

`assets/sprites/hero.png` holds 48 × 48 frames, 6 columns: row 1 is the idle, rows 2 to 4 the walk facing south, east
and north.

1. In the Project panel, click **Effects**, then **Sheets**.
2. Under **New sheet from a grid**, type `assets/sprites/hero.png` in the path box, set **frame** to `48` × `48`,
   **columns** to `6` and **fps** to `8`.
3. In **rows** type `idle, walk_s, walk_e, walk_n`.
4. Click **Make sheet**.

Result: `assets/sprites/hero.anim.json` is written with an `idle` clip and a `walk` clip that has `s`, `e` and `n`
directions. It shows in the Sheets list and in Assets ▸ Animations; give an entity an **Animator** with it and the
`humanoid` animator. Click **Edit** to add `"w"` as a mirror of `e`.

Questions: how do I import a sprite sheet? how do I use an Aseprite file? where is the Effects tab? how do I make a sheet from a grid of frames?

## The sheet editor: slots, clips and frames

The sheet editor opens from **Edit…** (Assets ▸ Animations or Characters), **Edit** on Project ▸ Effects ▸ Sheets, or
**Edit…** on a character's entry in Generate ▸ Animate's history. Its top bar shows the file, the picture, the slot size
and count, the sheet's default **fps**, and **Add pictures…**, **Revert**, **Save** and **Done**. Nothing is written
until **Save**; **Revert** throws away unsaved changes.

- **Left: the slots**: the picture cut into its grid's slots, numbered. Clicking a slot adds it to the end of the chosen
  direction's frames of the chosen clip. (A sheet whose frames are regions instead of a grid says "edit them in its
  file".)
- **Right: the clips**: a tab per clip, plus a **new clip** box and **Add** (walk and idle start looping, others play
  once). For the chosen clip: **loop** (loop, once, pingpong, hold), **fps** (empty: the sheet's), **plays on**,
  **Rename** and **Remove**.
- **Preview**: the chosen direction playing at the clip's speed (flipped when it is a mirror).
- **Direction rows**: each direction's frames in order, each with **‹** (earlier), **×** (remove) and **›** (later);
  click a row to make it the one slots are added to. A row's menu sets **own frames** or **mirror of <dir>**; **×**
  removes the direction. **add direction** adds one (a clip without directions turns its frames into that direction's).

**Add pictures…** takes images of the project (pick them in order) and adds them as new rows of slots; a strip of
frames is split into slots. Unsaved changes are saved first.

### Example: Slowing down the windmill's sails and fixing a frame out of order

The `windmill-sails` animation spins too fast, and its third frame jumps.

1. In Assets ▸ **Animations**, right-click `windmill-sails-sheet` ▸ **Edit…**.
2. Click the `idle` clip tab. In the clip's **fps** box type `6` (it was empty, so it played at the sheet's 8).
3. In the direction row, click **›** under the third frame to move it one place later. The **Preview** shows the
   sails turning smoothly.
4. Click **Save**.

Result: the sheet's file now has `"fps": 6` on the clip and the frames in the new order; every windmill playing it
turns slower. Click **Revert** before saving if you change your mind.

Questions: how do I edit an animation by hand? how do I change the speed of an animation? how do I reorder frames? how do I add frames from another picture?

## Plays on: tying a clip to an animator state

Each clip in the sheet editor has a **plays on** box: the animator states it plays for besides its own name's, as words
(attack, cast, use, hurt, die, open, close, break, or an action of your own). It is saved as the clip's `plays`. So a
clip named `punch` with plays on `attack` is one of the character's attacks; a clip `lid_up` with plays on `open` is what
a chest plays when opened.

Clips made by Generate ▸ Animate are named for their state already: punch, kick, shoot become `attack_punch`,
`attack_kick`, `attack_shoot` (variants of attack); cast → `cast`; death → `die`; hurt and hit react → `hurt`; pickup,
push, pull, throw → `use_pickup`, `use_push`…; a thing's open, close, break motions → `open`, `close`, `break`. Anything
else keeps its own name (a `dance` clip plays when a script calls `play(id, "dance")`).

### Example: A headbutt that counts as the goblin's attack

The goblin's sheet has a clip `headbutt` you made by hand; it never plays in fights.

1. In Assets ▸ **Characters**, right-click `goblin` ▸ **Edit…**.
2. Click the `headbutt` clip tab. In **plays on** type `attack`, then click elsewhere so it is taken.
3. Click **Save**.

Result: the clip is saved with `"plays": ["attack"]`, so the goblin's animator picks it when he attacks, alongside
any other attack clips he has.

Questions: what does "plays on" mean? how do I make a punch clip play as the attack? why doesn't my clip play?

## Points: where the hand is on each frame

The sheet editor's **points** area places named points on a clip's frames: `hand`, `offhand`, `head`, `back`, or a new
name typed in **new point** (Enter). Things held and worn (a sword, a hat, a shield) are drawn at these points.

1. Pick the point name and the direction row.
2. **in front**: ticked, the point is in front of the body (green); unticked, behind it (red).
3. Click where the point is on each frame; drag from it to set the angle (0 = right, clockwise). A frame not set yet
   keeps the last point.
4. **Clear** removes this point's positions in this direction. Save.

A mirrored direction takes its points from the direction it mirrors, flipped. A point set without a direction, or once
for all frames, holds for every frame; the sheet's own `points` hold where a clip has none. Generate ▸ Animate with
**holds a weapon** fills the `hand` point for you.

### Example: Fixing where the guard's hand is on one frame of his swing

In the guard's `attack_sword`, the sword floats away from his hand on the fourth frame facing east.

1. In Assets ▸ **Characters**, right-click `town-guard` ▸ **Edit…**, and click the `attack_sword` tab.
2. Click the `e` direction row to choose it. In **points**, pick `hand`.
3. Leave **in front** ticked (the sword is in front of his body here).
4. On the fourth frame, click on his fist and drag towards the blade's tip to set the angle.
5. Click **Save**.

Result: in Play, any sword in the guard's Equipment is drawn in his fist on that frame; the west side, mirrored from
east, is fixed too.

See also: The Animator and attachments, Character sheets, Effects.

Questions: how do I set where the hand is? how do I make a sword follow the swing? what are attachment points?
