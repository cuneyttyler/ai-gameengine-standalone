# Character sheets and animation sheets

How the editor stores animation: an animation sheet (`.anim.json`) names frames of a picture as clips, and a character
sheet is a special sheet holding one character's every action in every direction. This page covers both formats,
directions and mirrors, and which side of a character shows.

## What a character sheet is

A character is one asset: its **character sheet**, one picture and one sheet file beside it:

- `assets/characters/<name>/<name>.character.png`: every frame of every action, in square slots, 8 to a row;
- `assets/characters/<name>/<name>.character.anim.json`: the `aige.anim/1` sheet saying which slots make which clip in
  which direction.

The sheet's clips are its actions (`idle`, `walk`, `attack`, `cast`, `hurt`, `die`, or any other name), each holding
every direction the character faces (8, or 4). It is like Godot's SpriteFrames or RPG Maker's character sheet: one
resource per character, not one picture per direction.

Generate ▸ Character makes one with an `idle` clip (a pose each direction); Generate ▸ Animate with the character
picked adds a clip. A clip added later takes new rows at the bottom of the picture, so slots already used keep their
numbers. Assets ▸ Characters lists every character sheet (each card shows the character facing the camera; click a clip
name on the card to play it in every direction in turn).

### Example: Watching Mira's walk in every direction

Mira Quill was drawn on Generate ▸ Character and then given a walk on Generate ▸ Animate.

1. Open the Assets panel ▸ **Characters**. Mira's card shows her facing the camera, with her clip names under it:
   `idle` and `walk`.
2. Click `walk` on the card. Her walk plays facing south, then south-east, east and so on round the compass.
3. Open `assets/characters/mira-quill/mira-quill.character.png` in an image viewer: the idle poses are on the top row
   and the walk's frames on the rows added below them.

Result: one picture and one sheet hold everything Mira does; adding an `attack` later adds rows at the bottom
without moving the walk's slots.

Questions: what is a character sheet? where are a character's frames stored? why is my character one big picture?

## The character sheet file, field by field

A character sheet looks like this:

```json
{"schema": "aige.anim/1", "kind": "character",
 "character": {"name": "herbalist", "description": "...", "directions": 8, "size": 64, "view": "low top-down",
               "isometric": true, "provider": "bfl", "model": "flux-2-klein-4b"},
 "texture": "assets/characters/herbalist/herbalist.character.png",
 "grid": {"width": 64, "height": 64}, "size": [512, 384], "fps": 8,
 "clips": {"idle": {"loop": "loop", "directions": {"s": [0], "se": [1], "e": [2]}},
           "walk": {"loop": "loop", "fps": 10, "directions": {"s": [8, 9, 10, 11]}}}}
```

- `kind: "character"` marks it as a character (Assets ▸ Characters, not Animations).
- `character`: how it was made: its name, description, directions, size, view, isometric, provider and model; with
  AutoSprite also `autosprite` (its AutoSprite character id, so it is uploaded only once) and `humanoid`.
- `texture`, `grid` (the slot size), `size` (the picture's size), `fps` (the default speed).
- `clips`: each with `loop`, its own `fps`, and `directions`, each a list of slot numbers (counted left to right, top
  to bottom). A clip may also carry `plays` (the animator states it plays on) and `points` (a hand, a head, per frame).

### Example: Reading which slots Mira's south-facing walk uses

Mira's sheet is 64 px slots, 8 to a row. Her `walk` clip, trimmed to two directions, reads:

```json
"walk": {"loop": "loop", "fps": 10,
         "directions": {"s":  [8, 9, 10, 11, 12, 13, 14, 15],
                        "se": [16, 17, 18, 19, 20, 21, 22, 23]}}
```

1. Slots count from 0, left to right, top to bottom, so slot 8 is the first picture of the second row.
2. Her south walk is the whole second row (slots 8 to 15) and her south-east walk the third row.
3. `"fps": 10` overrides the sheet's default `"fps": 8` for this clip only.

Result: you can tell which frames play for each facing without opening the editor; the `character` block above it
also holds her `autosprite` id when AutoSprite animated her, so she isn't uploaded again.

Questions: what is in a .character.anim.json? where is the AutoSprite id kept? how are slot numbers counted?

## Animation sheets (.anim.json) for anything that moves

Any animated thing, not only characters, plays an **animation sheet**: `assets/…/<name>.anim.json` (`aige.anim/1`):

- `texture`: the picture; `size`: its width and height; `grid`: an optional frame size for numbered slots;
- `fps`: the default frames per second;
- `clips`: each with `frames` (slot numbers, or `{region: [x, y, w, h], duration}` for frames of any size), `fps`,
  `loop` (`loop`, `once`, `pingpong`, `hold`), `events` (a frame and an event, see below), and optionally `directions`
  (`s`, `se`, `e`, `ne`, `n`, `nw`, `w`, `sw`, each its frames or `"mirror:<dir>"`), `plays` and `points`.

Sheets come from Generate ▸ Animate's **Make animation**, from Project ▸ Effects ▸ Sheets (**New sheet from a grid**, or
**From Aseprite**: tags become clips, a tag named `walk_e` is the east direction of `walk`), from the AI panel
(`write_anim`), or written by hand. Assets ▸ Animations lists all sheets that aren't characters.

**Frame events**: `effect:<id>` plays that effect at the entity, `sound:<path>` plays a sound, `footstep` plays the
`footstep` effect, and any other name is heard by scripts as an `anim_event`. Events are set in the sheet's file (the
sheet editor shows their count but has no event editor).

### Example: A footstep sound on the guard's walk

The town guard's walk should make a sound each time a foot lands, on frames 2 and 6 of the clip.

1. In the project folder, open `assets/characters/town-guard/town-guard.character.anim.json` in a text editor.
2. In the `walk` clip, add an `events` list beside `loop` and `fps`:

   ```json
   "walk": {"loop": "loop", "fps": 10,
            "events": [{"frame": 2, "event": "footstep"}, {"frame": 6, "event": "footstep"}],
            "directions": { ... }}
   ```

3. Save the file. In **Project ▸ Effects ▸ Sheets**, select the guard's sheet: the `walk` clip's heading now reads
   "loop, 2 events".

Result: in Play, the `footstep` effect plays at the guard on those two frames. Use `"sound:assets/sounds/clank.wav"`
to play a sound file directly, or a name of your own (`"swing_peak"`) to hear it in a script as an `anim_event`.

Questions: what is an .anim.json? how do I play a sound on a frame? how do I import an Aseprite animation? what loop modes are there?

## Directions and mirrors in a sheet

A clip with `directions` holds frames per screen direction: `s` (towards the camera), `se`, `e`, `ne`, `n` (away),
`nw`, `w`, `sw`. A direction can be `"mirror:e"`: drawn as east's frames flipped left to right, so the west side needs
no frames of its own (attachment points are mirrored too: x and angle).

When the game wants a direction a clip doesn't have, it uses, in order: that direction's mirror if set; the nearest
direction it does have, stepping round the compass one eighth at a time (each way); then a clip's plain `frames`; then
any direction it has. So a character with only 4 directions walking north-east shows its north or east walk, and a
clip made only facing south plays south whatever the facing.

Generated characters: BFL and PixelLab v3 paint the east side and copy it flipped for the west (the frames are stored
flipped in the picture); AutoSprite's pack draws every direction itself.

### Example: Making a hand-made bat's west side use its east frames

You imported a bat sheet with frames only for `e`, and it flies west showing its east frames facing the wrong way.

1. Open the sheet in the sheet editor (Project ▸ Effects ▸ Sheets, select it, **Edit**) and click the `fly` clip tab.
2. Under **add direction**, pick `w`. A `w` row appears.
3. In the `w` row's menu, change **own frames** to **mirror of e**.
4. Do the same for `nw` (mirror of `ne`) and `sw` (mirror of `se`) if the clip has those, then click **Save**.

Result: the file now holds `"w": "mirror:e"`, and the bat flying west shows its east frames flipped left to right.
Without the mirror, the game would have used the nearest direction it had.

Questions: how does mirroring work? what happens if my animation has no north-east frames? how do I make west use east's frames?

## Which side of a character shows

**In Play**: the player's **Facing** is the way it last moved; an NPC's the way it walks or whom it turns to; an
enemy's its target. The animator turns that into a screen direction (isometric: across = x − y, down = (x + y) / 2;
on a top-down grid as it is), takes the nearest of the 8 eighths, and plays that direction's frames of the clip its
state wants (walk while moving, idle standing, attack swinging). A direction the clip lacks uses the nearest it has or
its mirror (see the directions section).

**In the editor**: animators run outside Play too, so a character stands in its idle pose facing its Facing. **R** /
**Shift+R** on a selected character turn it an eighth on screen (on a prefab copy it is kept as an override); the
Inspector's **Facing ▸ turn to** picks a direction. Facing is saved with the scene: an NPC starts Play facing the way it
was left, until it moves.

### Example: Mira facing the well when the game starts

Mira stands west of the well but faces the camera (south) in the editor.

1. Click Mira in the Scene view to select her.
2. Press **R** until she faces the well (each press turns her an eighth on screen; **Shift+R** turns the other way).
   Or, in the Inspector, use **Facing ▸ turn to** and pick the direction.
3. Save the scene.

Result: in the editor she stands in her idle pose facing the well; in Play she starts facing it too, until she walks
off, when her Facing follows the way she moves.

Questions: how do I turn a character in the editor? why does my NPC face the wrong way? how does the game pick a direction?

## Removing or replacing a character's animation

- **Replace**: animate the same action again in Generate ▸ Animate; the new clip takes the old one's name. The old
  frames stay in the picture, unused.
- **Remove a clip**: Assets ▸ Characters, right-click the character ▸ **Edit…** (the sheet editor on Project ▸
  Effects), pick the clip's tab, **Remove**, then **Save** (**Revert** undoes changes until then). Its frames stay in the
  picture, unused.
- **Rename a clip**: the sheet editor's **Rename** (idle, walk, attack, cast, hurt and die are what the animators play).
- **Delete the whole character**: Assets ▸ Characters, right-click ▸ **Delete…**. The sheet goes, its picture too
  unless another sheet uses it, and any animated parts that played it are removed.

### Example: Renaming the guard's "swing" clip so the animator plays it

The guard's custom attack was added as a clip named `swing`, so it never plays when he attacks.

1. In Assets ▸ **Characters**, right-click `town-guard` ▸ **Edit…**. The sheet editor opens on Project ▸ Effects.
2. If the sheet already has an `attack` clip you no longer want, click its tab and **Remove** it first (a clip can't
   be renamed to a name the sheet already has).
3. Click the `swing` clip tab, then **Rename**; in the "Rename clip" dialog type `attack` and confirm.
4. Click **Save** (or **Revert** to undo everything since the last save).

Result: the `humanoid` animator plays the clip when the guard attacks.

See also: Animations tab and sheet editor, The Animator, Animate tab.

Questions: how do I delete a character? how do I remove the walk animation? how do I rename a clip?
