# The Assets panel: art, sound, your library and web search

How to use the Assets panel: its tabs, importing images and sound, folders and multi-select, the asset details pane
(analysis, import settings, interior pairing, making prefabs), Import as prefabs for whole packs, the Catalog, your own
Library across projects, and Web Search for free art.

## The Assets panel and its tabs

The **Assets panel** (show or hide with **J**) has these tabs:
- **Prefabs**: the project's placeable things (see prefabs.md).
- **Assets**: the project's images and sounds (files under `assets/`).
- **Characters**: the project's character sheets, each turning round; right-click to make the player or an NPC, or
  animate.
- **Animations**: every animation sheet (`assets/**/*.anim.json`), each playing; right-click to make a prefab or give
  the selection one.
- **Web Search**: free art from the web (OpenGameArt and Wikimedia Commons, licence-checked).
- **Library**: your own art, for every project.
- **Catalog**: what the builder understood about each image (role, footprint, anchor, perspective, piece sets) and
  what looks wrong; any field can be set by hand there, and what you set wins over the analysis.

The **Search** box filters the current tab (Prefabs, Assets, Characters, Animations, Catalog, Library). On the Assets
tab it matches the path, and also an analysed image's description and tags.

### Example: finding the barrel picture you imported last week

1. Press **J** to show the Assets panel and click the **Assets** tab.
2. Type `barrel` in **Search**: the cards whose path contains "barrel" remain.
3. Nothing? The file was named `IMG_0412.png`. If it was analysed, type `wooden cask` instead: the search also matches
   an analysed image's description and tags, and it shows up.
4. Click the **Catalog** tab with the same search to see what the builder understood about it (role, footprint,
   perspective).

Questions: where are my images? what are the tabs of the assets panel? how do I search my assets?

## Importing images and sounds

On **Assets ▸ Assets**, press **Import…** and pick files. Supported: images **PNG, JPG, BMP, TGA**; audio **WAV, MP3,
FLAC**. Images are copied into `assets/textures/`, sounds into `assets/audio/`. An existing file is never overwritten:
a copy gets `_1`, `_2`… added to its name. A file that can't be read is not kept ("Import failed — …"). The last file
imported is selected.

Other ways art arrives: the Art and Character tools (Generate panel) write to `assets/textures/generated/`; Web Search
adds to `assets/web/…`; the Library adds to `assets/library/`; builds write their own art.

**Refresh** rescans the project folder, for files you added or changed outside the editor.

### Example: bringing in a tavern sign and a door creak

1. On **Assets ▸ Assets**, press **Import…** and pick `tavern_sign.png` and `door_creak.wav` from your downloads.
2. The picture is copied to `assets/textures/tavern_sign.png` and the sound to `assets/audio/door_creak.wav`; the last
   one is selected, showing its **Duration** and **▶ Preview**.
3. Import `tavern_sign.png` again after touching it up: the old file isn't overwritten, the new one is
   `assets/textures/tavern_sign_1.png`.
4. Copied files into `assets/` with Explorer instead? Press **Refresh** to see them.

Questions: how do I import images? what file formats are supported? where do imported files go? why does my file have
_1 in its name? how do I add sound effects?

## Folders, multi-select, moving and deleting assets

The left column lists **folders** inside `assets/` ("All assets" first); click one to show it and its subfolders. The
**chips** filter by kind: All, Textures, Tilesets, Audio, Generated.

- **Click** a card to see its details; **Ctrl+click**, **Shift+click** and **Ctrl+A** pick several; **Escape** clears;
  **Delete** deletes the picked ones after a question.
- **Drag** cards (or every picked card) onto a folder to move them.
- Right-click an asset: **Details**, **Make player** (generated character pictures), **Go to interior / Go to
  exterior** or **Generate interior**, **Rename…**, **Move to folder…**, **Show in Explorer**, **Keep in my library**,
  **Delete…**.
- Rename and move rewrite every reference (scenes, prefabs, quests, scripts), so nothing breaks.
- Deleting an asset deletes the file (and its `.meta` and `.gen.json`); prefabs that use it show a missing texture.
- Right-click a folder: **Rename folder…** (everything in it moves, references follow), **Show in Explorer**, **Remove
  folder…** (its contents move up unless "Also remove the N assets in it" is ticked).

Several picked: the details pane shows "N assets picked" with **Move to folder…**, **Delete…**, **Analyze**, **Apply N
suggested name(s)**, **Import N as prefabs…**, and **Make a prefab from each**; their right-click has **Move N to
folder…**, **Keep N in my library**, **Delete N…**.

### Example: sorting the market pictures into their own folder

1. On Assets ▸ Assets, click `stall_red.png`, then **Shift+click** `stall_blue.png` to pick the range, and
   **Ctrl+click** `fruit_crate.png` to add it.
2. Right-click one of them ▸ **Move 4 to folder…** and type `textures/market`. The folder is made and the files move;
   the `fruit_stall` prefab and the village scene that use them are updated.
3. Right-click `fruit_crate.png` ▸ **Rename…** and type `crate_apples`: nothing breaks, the references follow.
4. Click the **Textures** chip to hide the sounds while you work.

Questions: how do I move assets into a folder? how do I rename an image without breaking things? how do I delete
several assets? can I make subfolders?

## The asset details pane

Click an image on the Assets tab to see, on the right:
- its path, **Size** in pixels, and if it was generated: its description, variant and model;
- after analysis: **What** (a description), **Kind** (and which way it runs, whether it blocks the way), **Tags**,
  **Game size** (cells wide), **Borders** (a ground tile's terrain at each corner and edge);
- **Analyze** / **Analyze again**, and **Rename to <suggested name>** when the analysis suggests one;
- **Interior** (a select of images to pair as this building's inside) or **Interior of** (with **Unpair**);
- **Import as**: **Texture** or **Tileset**, with **Tile size** for a tileset, and **Apply import settings**;
- for a tileset, the tile picker (click a tile);
- **Make a prefab from it** (name, Drawn, New tile prefab, New prop prefab, Solid);
- **Apply to selected entity**: uses this image (or tile) as the selected entity's sprite, sized to it.

An audio file shows its **Duration** and **▶ Preview**.

### Example: setting up a dungeon tileset and using one tile on a crate

1. Click `dungeon_tiles.png` on Assets ▸ Assets.
2. Under **Import as**, choose **Tileset**, set **Tile size** to `32`, and press **Apply import settings**. The tile
   picker appears.
3. Select the crate entity in the scene, click the cracked-floor tile in the picker, and press **Apply to selected
   entity**: the crate's sprite becomes that tile, sized to it.
4. To give the inn its inside, click `inn_exterior.png` and choose `inn_interior.png` under **Interior**; the interior
   picture then shows **Interior of** with **Unpair**.

Questions: how do I set up a tileset? how do I change the picture of an object? how do I preview a sound? what does the
analysis tell me?

## Analyzing assets

**Analyze N** on the Assets tab works out what every image shown is; **Analyze** in the details does one or the picked
ones. Ground tiles' border terrains are measured from their pixels; with the AI service on, a vision model adds a
description, kind, tags, direction, size and a better name. While it works the button shows "Analyzing N…".

Analysis helps elsewhere: better names, kinds and sizes in Import as prefabs, search by description and tags, and the
builder's catalog. **Rename to …** / **Apply N suggested name(s)** renames files to the suggested names after a
confirmation, updating prefabs, scenes and scripts that use them.

### Example: naming a pack of badly named pictures

A downloaded pack has files called `sprite_001.png` to `sprite_040.png`.
1. Click the pack's folder on Assets ▸ Assets and press **Analyze 40**. The button reads "Analyzing 40…" while it
   works (with the AI service running, a vision model describes each).
2. Click `sprite_007.png`: **What** says "a wooden water well with a bucket", **Kind** a prop that blocks the way, and
   a button **Rename to water_well**.
3. Press **Ctrl+A** to pick all of them and click **Apply 40 suggested name(s)**, then confirm.
4. The files are renamed and any prefab or scene using them follows.

Questions: what does Analyze do? how do I get better names for my images? do I need AI for analysis?

## Import as prefabs (asset packs)

To turn a whole pack of images into prefabs at once: show the pack's images on the Assets tab (its folder, a search)
and press **Import as prefabs…**, or pick several and press **Import N as prefabs…**. The **Import as prefabs** window
shows the plan before anything is made:
- "Ground tiles lie flat, one cell each. Everything else is sized by its pack's scale, so a pack keeps its
  proportions. Analyze the images first for better names, kinds and sizes."
- For each pack folder: **image pixels per cell** (with where that scale came from); change it and the plan is worked
  out again, keeping your edits.
- One row per image: include tick, thumbnail, **name**, kind, **Tile** or **Prop**, width in **cells** for a prop, and
  **solid**.

Press **Create N prefab(s)** (or **Cancel** / Escape).

### Example: a forest pack of trees, rocks and ground tiles

You imported a pack into `assets/textures/forest_pack/` with 12 ground tiles and 18 props.
1. Analyze the folder first (**Analyze 30**) for better names and kinds.
2. With the folder shown, press **Import as prefabs…**.
3. The pack's **image pixels per cell** says 64; the trees look tiny in the plan, so change it to `48`. The plan is
   worked out again and your edits are kept.
4. Untick the row of a duplicate rock. Make sure the ground rows say **Tile** and the trees say **Prop** with
   **solid** ticked; set the big oak's width to `3` **cells**.
5. Press **Create 29 prefab(s)**: they appear in Assets ▸ Prefabs, ready to paint and place.

Questions: how do I import an asset pack? how do I make many prefabs at once? why are my imported props the wrong size?
what is pixels per cell?

## Your library (the Library tab)

The **Library** is your own art outside any project, on this computer (`%LOCALAPPDATA%\aige\library\`), usable by
every project. It keeps art the Art tool generates (automatically, in its Generated folder, so it survives the project),
art from the web and the AIGE library, and project art you choose to keep.

- A breadcrumb shows where you are; **New folder**, **Import…** (an exported `.aige-library` folder), **Export…** (this
  folder or the whole library).
- Click an item for details (where it came from, its project, description, licence and author, sharing). Double-click
  or **Add to the project** copies it into the open project (`assets/library/`); a ✓ marks items already in the project.
- Right-click an item: **Add to the project**, **Move to**, **Rename…**, **Offer it to others** / **Keep it private**
  (generated art), **Remove from the library…** (projects keep their own copies).
- Right-click a folder: **Open**, **Rename…**, **Export…**, **Delete the folder** (contents move up; nothing deleted).
- From the Assets tab: right-click ▸ **Keep in my library**. Web art keeps its licence, author and URL, so credits
  follow it. A building keeps its interior and door animations with it.

The first time it holds generated art, the tab asks **Share the art you generate?**: **Offer it by default**, **I'll
decide for each**, **Keep it private**. The footer's select changes it any time. Nothing is uploaded until the AIGE
library's service is online.

### Example: reusing a lighthouse from your last game

1. In the old project, right-click `lighthouse.png` on Assets ▸ Assets ▸ **Keep in my library**. Its interior and door
   animations go with it.
2. Open the new project and click the **Library** tab. Find the lighthouse (or type `lighthouse` in Search).
3. Double-click it (or **Add to the project**): it's copied to `assets/library/` and gets a ✓.
4. Moving to a new computer? Click **Export…** for the whole library, and on the other computer use **Import…** with
   the exported `.aige-library` folder.

Questions: where is art I generated in another project? how do I reuse art across projects? how do I move my art to
another computer? does AIGE upload my art?

## Web Search for free art

**Assets ▸ Web Search** finds free art on **OpenGameArt** and **Wikimedia Commons**, licence-checked: CC0, CC-BY and
OGA-BY (CC-BY-SA when allowed in the build's asset settings). It needs the AI service running ("the AI service isn't
running (Preferences ▸ AI)" otherwise).

1. Type what you want (for example "isometric wooden fence") and press **Search** or Enter. A search reads each
   result's page: usually 10–40 seconds. If the first search finds little, better searches are tried and listed.
2. Results rank by the words they name and by how their colours match your project's style; words like "isometric"
   rank, they don't filter. Each card shows its licence, author, source and a style %.
3. Click a result for details (why it ranks there, size, its page, tags). **Add to assets** (or double-click) downloads
   it into `assets/web/search`; **Add to folder…** into `assets/web/<folder>`; a result with several files lists them
   to add one by one.

Added art is credited (in CREDITS.md, through its description) and kept in your library. **More sources: search them
in your browser** opens searches on OpenGameArt, Kenney, itch.io, CraftPix, Game-icons.net, Wikimedia Commons and
Lospec for what you typed; bring what you download into the project (Assets ▸ Import…) and keep its licence and
author for your credits.

### Example: a free wooden fence for the farm

1. Make sure the AI service is running (Preferences ▸ AI).
2. Click **Assets ▸ Web Search**, type `isometric wooden fence` and press **Enter**. After 10 to 40 seconds the
   results come, each with its licence (say CC0), author, source and a style %.
3. Click the one with the best style % to read why it ranks there and its size.
4. Click **Add to folder…** and type `farm`: it's downloaded to `assets/web/farm`, credited in CREDITS.md and kept in
   your library.
5. Make a prefab from it on the Assets tab (**Make a prefab from it**, Solid, **New prop prefab**).

Questions: how do I find free art? is web art safe to use in my game? where does downloaded art go? why does the web
search fail?
