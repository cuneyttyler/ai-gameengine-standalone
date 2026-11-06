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

See also: The Animator and attachments, Character sheets, Effects.

Questions: how do I set where the hand is? how do I make a sword follow the swing? what are attachment points?
