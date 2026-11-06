# Ground: tiles, tile layers, flat and lying-down art, height

How the ground of a scene works: hand-painted tile entities, the tile layers that built places use, painting and
erasing ground, auto-fit terrain, art that lies on the ground (flat and ground sprites) and its order, the tile grid,
and raising or lowering the ground with the Height tool.

## How ground works

The editor draws an isometric grid: each cell a diamond (64 × 32 pixels at zoom 1 by default). The ground comes in
two forms:
- **Tile entities**: one entity per cell, with a **Tile** component (its column **gx**, row **gy**, and **solid**) and
  a Sprite. Painting a tile prefab makes these. They're listed in the Scene Tree's **Tiles** group and suit a few
  hundred cells.
- **Tile layers**: a built place has hundreds of thousands of ground cells, so the Build panel writes them as **tile
  layers** instead: each layer a palette of tile kinds and, per 32 × 32 chunk, one entry per cell. They're saved next
  to the scene (in region files of 128 × 128 cells, only changed regions rewritten) and aren't listed in the Scene Tree.
  Several layers draw in order; a higher one wins a cell.

Collision comes from the ground: a cell is walkable only if it holds a non-solid tile, so empty space is a wall; a
**solid** tile (a wall, water, a hedge) blocks. A painted (Paint type) scene uses an invisible tile layer called `walk`
for where one can walk.

Questions: why can't my player walk off the tiles? what is a tile layer? why don't I see the ground in the scene tree?
how big is a cell?

## Painting and erasing ground

1. Pick a **tile prefab** (Assets ▸ Prefabs ▸ Tiles chip; press 1–9; or right-click a tile in the scene ▸ **Draw with
   <tile>**). The toolbar shows **Paint <name>**.
2. Click or drag over the ground: every cell the stroke crosses gets that tile, replacing the tile that was there.
   **Shift+wheel** sets the brush to 1 × 1 up to 4 × 4 cells ("Brush: N × N cells").
3. Right-click a cell ▸ **Paint <tile> here** paints exactly one cell.

To erase: the **Erase** tool (**E**), then click a tile or a thing to delete it; on a built place's ground, clicking
erases that tile layer cell. With the **Select** tool, click a ground cell: the Inspector shows "Ground cell (x, y)",
its layer and tile, with **Draw with it** and **Erase this cell (Delete)**. **Shift+drag** box-selects things and
ground cells to delete together.

To make new tiles: an image ▸ **New tile prefab** (assets.md), or on a built place right-click a cell ▸ **Make an asset
from this cell** (its picture becomes a tile prefab to paint elsewhere). To swap a tile everywhere: **Edit ▸ Replace
prefab…** (it covers hand-painted tiles and built ground alike).

Questions: how do I paint ground? how do I erase ground? how do I paint with the tile already in the scene? how do I
change all the grass to sand?

## Auto-fit terrain

When the brush is a tile of a **terrain set** (pieces for edges, corners and ends of one terrain against another), the
toolbar shows a checkbox **Auto-fit <terrain>**. With it on, painting lays that terrain and picks every piece (edges,
corners, ends) to match its neighbours, and erasing turns the cell back to the background terrain. Turn it off to
place the exact piece you picked.

Analyzing tiles (Assets ▸ Analyze) works out each ground tile's **Borders**: the terrain at each corner and edge,
which is how pieces are matched.

Questions: how do I paint roads with proper edges? what does Auto-fit do? why does painting change the tiles next to
it?

## Art that lies on the ground: flat and ground sprites

Two Sprite settings make art lie on the ground instead of standing up:
- **flat**: for art drawn **top-down**. The image is mapped onto the cell's diamond (width × height) instead of standing
  upright. Flat tiles are drawn before everything else (nothing can be behind the ground). A flat prop (a rug, a road
  piece, a shadow) is centred on its point, above the tiles and under everything standing. **rotation** turns it in
  quarter turns (R / Shift+R).
- **ground**: for art that lies on the ground **as drawn** in the isometric view (a field, a rug, a path piece): its
  bottom edge sits on its cell's front corner and it's drawn under what stands. Ticking it fits it to its cell.

Among flat and ground pieces, **order** (−9 to 9) says which is on top (a road over grass, a rug over the road); then
where they are. Change it with **Ctrl+]** / **Ctrl+[** (forward/backward), **Ctrl+Shift+]** / **Ctrl+Shift+[** (to
front/back), or the Edit menu.

When making a prefab from an image, **Drawn: Top-down (lies flat on the grid)** makes it flat; **Isometric / upright**
stands it as drawn. A prefab card's right-click **Lay flat on the grid (top-down art)** / **Stand upright (isometric
art)** changes it later.

Questions: why does my top-down tile look squashed? how do I make a rug lie on the floor? how do I put a road over
grass? what is the difference between flat and ground?

## The tile grid and framing

- **G** (View ▸ **Tile grid on empty ground**) draws cell borders where there's no ground yet, so you can see where to
  paint.
- **F** (View ▸ **Frame scene**, the toolbar's **Frame**) fits the whole scene in the view.
- **Right-drag** or dragging empty ground with the Select tool pans; the **wheel** zooms (speed and direction in
  Preferences ▸ Viewport).

Questions: how do I show the grid? how do I see the whole scene? how do I pan and zoom?

## Height: raising and lowering the ground

The toolbar's **Height** tool shapes a built place's ground: drag over the ground to **Raise**, **Lower** or **Level**
it (the mode select beside the tool), within a **Radius** of 0–12 cells round the cursor.

Characters walk between heights only where there's a **Ramp** (stairs, a slope, a ladder); elsewhere a step much higher
or lower than where one stands is a cliff. Add one with right-click the ground ▸ **Ramp here (walk between heights)**,
or the Ramp component. A Sprite's **lift** raises a thing off the ground by pixels (standing on high ground); things on
raised ground are lifted to its height.

Questions: how do I make hills? how do I make a cliff? why can't my character climb up? how do I add stairs between
heights?
