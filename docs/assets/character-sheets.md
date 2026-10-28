# Character sheets

A character is one asset: its **character sheet**, one picture and one `aige.anim/1` sheet beside it
(`assets/characters/<name>/<name>.character.png`, `.character.anim.json`). The sheet's clips are its actions (`idle`,
`walk`, `attack`, `cast`, `hurt`, `die`, or any other), each holding every direction it faces (8, or 4). Like Godot's
SpriteFrames or RPG Maker's character sheet: one resource a character, not a picture each direction.

```json
{"schema": "aige.anim/1", "kind": "character",
 "character": {"name": "herbalist", "description": "...", "directions": 8, "size": 64, "view": "low top-down",
               "isometric": true, "provider": "bfl", "model": "flux-2-klein-4b"},
 "texture": "assets/characters/herbalist/herbalist.character.png",
 "grid": {"width": 64, "height": 64}, "size": [512, 384], "fps": 8,
 "clips": {"idle": {"loop": "loop", "directions": {"s": [0], "se": [1], "e": [2], "...": []}},
           "walk": {"loop": "loop", "fps": 8, "directions": {"s": [8, 9, 10, 11], "...": []}}}}
```

- **Slots**: square, 8 to a row. A clip added later takes new rows at the bottom, so slots already used keep their
  numbers (`app/charsheet.py`).
- **Made by** Generate ▸ Character (`/characters/generate`): the project's art model when it's BFL's
  (`bfl_sprites.character`: facing the camera, each direction an edit of it, the west side mirrored), else PixelLab;
  written as a sheet with its `idle` clip, a pose each direction. No loose direction pictures.
- **Animated** by Generate ▸ Animate with a character picked (`/characters/animate`): each painted direction animated
  from its idle pose (south, south-east, east, north-east, north for 8; the west side mirrored: 5 calls), added as one
  clip named for the animators (walking → `walk`, attacking → `attack`, casting → `cast`; anything else its own name).
- **Characters made before sheets** (loose `…-south.png` pictures): Generate ▸ Character's **Make character sheet**
  (`/characters/from-pictures`) joins them as one sheet.

## In the editor

- **Assets ▸ Characters**: every character sheet, turning round; right-click **Make player** (the engine's player:
  moves, collides, fights, in the player's stead), **Make NPC** (in the middle of the view), **Animate…** (Animate with
  it picked), **Edit…** (the sheet editor), Show in Explorer. Assets ▸ Animations lists the other sheets.
- **Make player / Make NPC** (`MakeCharacterPrefab`): a prefab with the sheet's picture (its idle facing the camera
  until it plays), an Animator (`humanoid`) playing the sheet, and `Facing` south on screen.

## Which side shows

- **Play**: the player's `Facing` is the way it last moved, an NPC's the way it walks or whom it turns to, an enemy's
  its target. The animator turns it into a screen direction (isometric: sx = x − y, sy = (x + y) / 2), takes the
  nearest of the 8 eighths, and plays that direction's frames of the clip its state wants (walk while moving, idle
  standing, attack swinging); a direction the clip lacks uses the nearest it has, or another's mirrored.
- **Editor**: animators run outside Play too, so a character stands in its idle facing its `Facing`. **R** / Shift+R
  on a character turn it an eighth on screen (its facing, kept as an override on a prefab copy); the Inspector's
  `Facing ▸ turn to` picks a direction. `Facing` is saved with the scene: an NPC starts Play facing the way it was
  left, until it moves.
