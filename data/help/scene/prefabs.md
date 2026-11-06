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

Questions: how do I turn an image into a prefab? what is the difference between a tile prefab and a prop prefab? how do
I use one tile of a tileset? how do I make a solid prop?

## Making a prefab from something in the scene

Select a thing that isn't a copy of a prefab yet and use **Save as prefab…** at the bottom of the Inspector, or
right-click it ▸ **Save as prefab…**. Give a file name (letters, digits, `-` and `_`). The entity is saved as
`prefabs/<name>.prefab` (without its position or references to other entities) and becomes its first copy. Undo
returns the entity to an ordinary one (the file stays).

This is how you make a reusable character or prop you've set up by hand: add its components, set its fields, then
save it as a prefab and place more copies.

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

Questions: how do I rename a prefab? how do I organise prefabs into folders? what happens when I delete a prefab?

## Replacing a prefab in the scene

**Edit ▸ Replace prefab…**, a card's **Replace every copy in the scene…**, the Inspector's **Replace prefab…**, or a
thing's right-click **Replace prefab…** open the **Replace a prefab in the scene** dialog: "Every copy of the first
becomes the second, where it was. One undo step."
- **Replace**: the prefab to replace; **With**: the new one. Tiles are offered for tiles and things for things.
- **Which** (when opened from one thing): **this one** or **every copy in the scene**.
- For a ground tile it covers hand-painted tiles and a built place's ground alike.

Press **Replace it** / **Replace every copy**. Each copy keeps its place, and how it was turned and mirrored.

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

Questions: how do I make a player from a generated character? how do I make an NPC prefab? how do I give an object an
animation?
