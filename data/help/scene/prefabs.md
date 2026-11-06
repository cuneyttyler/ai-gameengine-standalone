# Prefabs: making, placing and changing reusable things

What a prefab is in AIGE, the Prefabs tab of the Assets panel, how to make prefabs from assets or from things in the
scene, how to place and paint them, how copies and overrides work, how to change a prefab for every copy, replace one
prefab with another, and make character prefabs.

## What a prefab is

A **prefab** is a reusable template of one entity, stored as `prefabs/<name>.prefab` in the project (in the scene file
format). Everything you place in a scene is a prefab: a grass tile, a wall block, a tree, a skeleton, a potion, a
house, the player. The editor's palette is the list of the project's prefabs.

A thing placed from a prefab is a **copy** (instance). It remembers its prefab and which of its fields were changed on
that copy (its **overrides**). When the prefab changes, every copy takes the new values except where it was changed
deliberately. Where a copy stands, and fields that point at other entities, are never copied from the prefab. Scenes
store full copies, and when a scene opens, its copies are brought up to date with any prefab edits made since it was
saved.

A new project with no `prefabs/` folder gets a starter set: **grass**, **dirt**, **wall** (tiles), **player**, **npc**,
**enemy**, **potion** and **gateway**. They're ordinary files: edit, restyle or delete them.

### Example: every pine in the forest gets a new picture

The forest scene has forty copies of the `pine` prefab. Two of them, by the road, were made smaller on purpose.
1. Select any pine and pick a new picture in **Sprite ▸ texture** (`pine_winter.png`).
2. Click **Apply to prefab** in the Inspector's Prefab header.
3. All forty pines in the open scene show the winter picture at once; the two by the road stay smaller, as their size
   was their own override.
4. Open the `mountain_pass` scene, which also has pines: they're brought up to date as it opens.

Questions: what is a prefab? where are prefabs stored? what happens to placed copies when I change a prefab? what are
the default prefabs?

## The Prefabs tab

**Assets panel ▸ Prefabs** (show the Assets panel with **J**) is the palette:
- **folders** on the left ("All prefabs", then subfolders of `prefabs/`); click one to show what's in it and its
  subfolders;
- **kind chips**: All, Tiles, Characters (has Health), Items (a Pickup), Other;
- the **Search** box filters by path;
- **cards** with a thumbnail (cropped to the sprite's region), the name, and a badge: its kind or "player", and its
  number **1–9** for the first nine prefabs (press that number key to pick it as the brush). A broken prefab is marked
  and its tooltip says why.

Click a card to pick it as the brush. **Ctrl+click** adds or removes a card from a multi-selection, **Shift+click**
picks a range, **Ctrl+A** everything shown, **Escape** clears, **Delete** deletes what's picked (after a question). Drag
cards onto a folder to move them. When you select a copy in the scene, its prefab is picked and scrolled to here.

**Refresh** rescans the project folder (for files changed outside the editor).

### Example: finding the rope bridge among two hundred prefabs

1. Press **J** to show the Assets panel and click **Prefabs**.
2. Click the folder `props/outdoor` on the left, then the **Other** chip.
3. Type `bridge` in **Search**: two cards remain, `rope_bridge` and `stone_bridge`.
4. Click `rope_bridge` to pick it as the brush. (The first nine cards also show numbers: press **3**, say, to pick the
   third one without the mouse.)
5. Later, click a bridge copy in the scene: its card is picked and scrolled to here.

Questions: where is the prefab palette? how do I filter prefabs? what are the numbers on prefab cards? how do I find
the prefab of an object I selected?

## Placing and painting prefabs

Pick a prefab (click its card, press 1–9, or right-click it ▸ **Place it** / **Paint with it**). The toolbar's brush
button shows **Place <name>** or **Paint <name>**.
- **Tile prefabs paint**: click or drag over the ground and every cell the stroke crosses gets the tile, replacing
  what was there. **Shift+wheel** changes the brush to 1×1 up to 4×4 cells.
- **Other prefabs are placed** one per click, on the cell's middle, and the new copy is selected.
- **The player**: there's only one; placing the player prefab again moves the existing player.
- **Size**: the toolbar's **Size %** (10–1000), or **]** / **[** and **Alt+wheel** while the brush is armed, sets how
  big the next copies are.
- **Turn**: **R**, **Shift+R** or **Shift+wheel** turns what the brush places: a flat (top-down) prefab a quarter
  turn, an upright picture mirrored.
- **Auto-fit**: for a terrain tile set, the toolbar's **Auto-fit <terrain>** checkbox picks every piece (edges,
  corners, ends) to match its neighbours as you paint; erasing turns it back.
- Right-click a cell ▸ **Place / Paint <brush> here** puts it on exactly that cell.

**Escape** or **V** goes back to the Select tool. **E** is the Erase tool: click a thing to delete it, or a built
place's ground cell to erase it.

### Example: a cobbled square with a fountain in the middle

1. In Assets ▸ Prefabs, click the `cobble` tile card. The toolbar's brush button shows **Paint cobble**.
2. Press **Shift+wheel** until the brush is 3×3, then drag across the square: every cell the stroke crosses becomes
   cobbles.
3. Click the `fountain` card (**Place fountain**), set **Size %** to `150` on the toolbar, and click the square's
   centre cell once. The fountain copy is placed and selected.
4. Pick the `bench` prefab, press **R** to mirror it, and click either side of the fountain.
5. Press **Escape** to go back to the Select tool; press **E** and click a bench to delete it with the Erase tool.

Questions: how do I place an object? how do I paint ground tiles? how do I paint a bigger area at once? how do I place
things smaller? how do I stop placing?

## Making a prefab from an asset

In **Assets ▸ Assets**, click an image; its details show **Make a prefab from it**:
1. **Name** (starts as the file name; a tileset tile adds `_<tile number>`). Letters, digits, `-` and `_`.
2. **Drawn**: **Top-down (lies flat on the grid)** or **Isometric / upright**. A square image is guessed top-down, a
   2:1 one isometric. Top-down art is laid onto one cell's diamond.
3. For a **tileset**, click the tile to use in the tile picker.
4. **Solid**: tiles block movement; props block their cell (an Obstacle).
5. **New tile prefab** (a ground tile painted onto cells) or **New prop prefab** (an object that stands in the world).

The new prefab is armed as the brush at once: "Created prefabs/<name>.prefab — click in the viewport to place it". If
the picture's doors were animated before, the prefab gets them ("with its picture's doors"). A name already used is
refused.

With several images picked, the details offer **Make a prefab from each** (named after their files) and **Import N as
prefabs…** (see assets.md, Import as prefabs).

### Example: a haystack prop from a generated picture

1. In **Assets ▸ Assets**, click `haystack.png`. Its details show **Make a prefab from it**.
2. **Name**: keep `haystack`.
3. **Drawn**: the picture is 2:1, so it's guessed **Isometric / upright**; leave it.
4. Tick **Solid** so the haystack blocks its cell.
5. Click **New prop prefab**. The editor says "Created prefabs/haystack.prefab — click in the viewport to place it":
   click in the farmyard to place a few.

For a ground tile from a tileset such as `meadow_tiles.png`, click the tile in the tile picker (the name becomes, say,
`meadow_tiles_7`), leave **Solid** off and click **New tile prefab**.

Questions: how do I turn an image into a prefab? what is the difference between a tile prefab and a prop prefab? how do
I use one tile of a tileset? how do I make a solid prop?

## Making a prefab from something in the scene

Select a thing that isn't a copy of a prefab yet and use **Save as prefab…** at the bottom of the Inspector, or
right-click it ▸ **Save as prefab…**. Give a file name (letters, digits, `-` and `_`). The entity is saved as
`prefabs/<name>.prefab` (without its position or references to other entities) and becomes its first copy. Undo
returns the entity to an ordinary one (the file stays).

This is how you make a reusable character or prop you've set up by hand: add its components, set its fields, then
save it as a prefab and place more copies.

### Example: a reusable town guard

1. Set up a guard by hand: an entity `Town guard` with Sprite, Health (max `40`), Team Neutral, MeleeAttack and a
   Behaviour whose **work** is `gate_post`.
2. Right-click it ▸ **Save as prefab…** and type `town-guard`.
3. `prefabs/town-guard.prefab` is written without the guard's position; the guard becomes its first copy.
4. Pick `town-guard` in Assets ▸ Prefabs and place three more at the other gates; set each one's **work** marker on its
   copy.

Questions: how do I make a prefab from an entity? can I reuse an NPC I set up? why is Save as prefab greyed out?

## Changing a prefab and its copies

There is no separate prefab editor: you change a prefab through one of its copies.
1. Place or select a copy and change it in the Inspector (fields, components, size).
2. Changed fields are this copy's **overrides**. To give them to the prefab: **Apply to prefab** in the Inspector's
   Prefab header (all of them), or **⤒** beside one field.
3. Every other copy in the open scene updates at once, keeping its own overrides; other scenes update when they open.
   A copy sized on its own keeps its share of the new size.

To throw away a copy's changes: **Revert all**, **↺** beside a field, or right-click ▸ **Revert to prefab**.

Some changes go to the prefab directly: **Add interior** and animating a part (Animate a part…, Animate the interior…,
Build ▸ Animate) put the result on the prefab of the copy, so every copy has it. **Lay flat on the grid (top-down
art)** / **Stand upright (isometric art)** in a prefab card's right-click menu changes how the prefab is drawn.

### Example: making every lantern flicker

1. Select one lantern copy in the street and add **Light2D** (+ Add component) with **flicker** `0.3`, **radius** `3`.
2. The Prefab header counts the new component as an override of this copy.
3. Click **Apply to prefab**: every lantern in the scene now flickers. One lantern you'd already made brighter keeps its
   own brighter value.
4. On the lantern in the alley, which should stay dark, click **↺** beside **flicker** (or **Revert all**).

Questions: how do I edit a prefab? how do I change all trees at once? why didn't my change reach the other copies? how
do I update every copy?

## The prefab card's menu: rename, move, delete

Right-click a prefab card:
- **Place it** / **Paint with it**.
- **Go to interior** / **Go to exterior**: when its picture is paired with an interior.
- **Add interior: cutaway** / **Add interior: its own scene** (not for tiles).
- **Rename…**: everything that uses it (scenes, prefabs, quests, scripts) is updated.
- **Move to folder…**: a folder inside `prefabs/` (for example `characters` or `props/outdoor`; empty for the top). New
  folders are created; references are updated.
- **Show in Explorer**.
- **Replace every copy in the scene…**: the Replace prefab dialog.
- **Lay flat on the grid (top-down art)** / **Stand upright (isometric art)**.
- **Delete prefab…**: after a confirmation the file is deleted; its copies in the scene stay as ordinary entities ("N
  copies kept as ordinary entities"). Quests that count this prefab will show a problem.

Right-click a folder for **Rename folder…**, **Show in Explorer** and **Remove folder…** (its contents move up a level
unless you tick "Also remove the N prefabs in it"). With several cards picked: **Move N to folder…** and **Delete N…**.

### Example: tidying the character prefabs into a folder

1. In Assets ▸ Prefabs, **Ctrl+click** the cards `guard`, `miller` and `innkeeper`.
2. Right-click one ▸ **Move 3 to folder…** and type `characters/town`. The folder is created and every scene and quest
   that uses them is updated.
3. Right-click `innkeeper` ▸ **Rename…** and type `vara-bell`: the scenes' copies follow.
4. Right-click the unused `old_cart` card ▸ **Delete prefab…** and confirm; its two copies in the scene stay as ordinary
   entities ("2 copies kept as ordinary entities").

Questions: how do I rename a prefab? how do I organise prefabs into folders? what happens when I delete a prefab?

## Replacing a prefab in the scene

**Edit ▸ Replace prefab…**, a card's **Replace every copy in the scene…**, the Inspector's **Replace prefab…**, or a
thing's right-click **Replace prefab…** open the **Replace a prefab in the scene** dialog: "Every copy of the first
becomes the second, where it was. One undo step."
- **Replace**: the prefab to replace; **With**: the new one. Tiles are offered for tiles and things for things.
- **Which** (when opened from one thing): **this one** or **every copy in the scene**.
- For a ground tile it covers hand-painted tiles and a built place's ground alike.

Press **Replace it** / **Replace every copy**. Each copy keeps its place, and how it was turned and mirrored.

### Example: autumn comes to the village

1. Choose **Edit ▸ Replace prefab…**.
2. **Replace**: `oak`. **With**: `oak_autumn`.
3. Press **Replace every copy**: every oak in the scene becomes an autumn oak in the same place, turned and mirrored as
   before. One **Ctrl+Z** puts them all back.
4. For the ground, open the dialog again with **Replace** `grass` and **With** `grass_autumn`: hand-painted tiles and
   the built place's ground change alike.

Questions: how do I swap all copies of one prefab for another? how do I replace one tree with another kind? how do I
change the ground tile everywhere?

## Character prefabs: the player and NPCs

- **Assets ▸ Characters** lists the project's character sheets (one sheet each, every direction and action, turning
  round). Right-click one: **Make player** or **Make NPC** makes a prefab that plays that sheet; **Animate…** opens the
  Animate tab for it; **Edit…** opens it in Project ▸ Effects; **Show in Explorer**; **Delete…**. No characters yet:
  make one with Generate ▸ Character.
- **Assets ▸ Animations** lists every animation sheet. Right-click: **Make prefab** (a prefab that plays it), **Add to
  selected** (gives the selected thing an Animator with it), **Edit…**, **Show in Effects**, **Show in Explorer**,
  **Delete…**. Click a clip name on a card to play it; double-click to open it in Effects.
- **Assets ▸ Assets**: a generated character whose pictures are named `…-south.png`, `…-east.png` and so on in one
  folder has **Make player** in its right-click menu: the player, facing the way it walks.

### Example: Mira the ranger becomes the player

You generated Mira with Generate ▸ Character and she has a sheet in Assets ▸ Characters.
1. In **Assets ▸ Characters**, right-click `mira` ▸ **Make player**. A prefab that plays her sheet is made.
2. Delete the old player from the scene, then pick Mira's prefab in Assets ▸ Prefabs and click where she should
   start.
3. Right-click `old-fisher` ▸ **Make NPC** for an NPC that plays his sheet, and place him on the pier.
4. To give the windmill its turning sails, select it, then in **Assets ▸ Animations** right-click `windmill_sails` ▸
   **Add to selected**.

Questions: how do I make a player from a generated character? how do I make an NPC prefab? how do I give an object an
animation?
