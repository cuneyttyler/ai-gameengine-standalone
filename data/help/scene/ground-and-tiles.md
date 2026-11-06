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

### Example: a pond in the meadow that the player walks round

1. Paint the meadow with the `grass` tile prefab (non-solid): the player can walk anywhere on it.
2. Make a `water` tile prefab with **Solid** ticked (from an image: **New tile prefab**), and paint the pond with it.
3. Press Play: the player walks round the pond's edge but can't step into the water, and can't leave the meadow where
   there are no tiles at all, as empty space is a wall.
4. Click a water cell with the Select tool: its **Tile** shows **gx**, **gy** and **solid** ticked; it's listed in the
   Scene Tree's **Tiles** group.

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

### Example: a dirt yard behind the farmhouse

1. In Assets ▸ Prefabs, click the **Tiles** chip and then the `dirt` card. The toolbar shows **Paint dirt**.
2. Turn **Shift+wheel** until it says "Brush: 3 × 3 cells" and drag across the yard: the grass becomes dirt.
3. Right-click the one cell by the gate ▸ **Paint dirt here** to fill the gap exactly.
4. Painted over the path by mistake? Press **E** and click those cells to erase them, or **Ctrl+Z**.
5. Right-click a cell of the old path ▸ **Draw with path** to paint with that tile again.

Questions: how do I paint ground? how do I erase ground? how do I paint with the tile already in the scene? how do I
change all the grass to sand?

## Auto-fit terrain

When the brush is a tile of a **terrain set** (pieces for edges, corners and ends of one terrain against another), the
toolbar shows a checkbox **Auto-fit <terrain>**. With it on, painting lays that terrain and picks every piece (edges,
corners, ends) to match its neighbours, and erasing turns the cell back to the background terrain. Turn it off to
place the exact piece you picked.

Analyzing tiles (Assets ▸ Analyze) works out each ground tile's **Borders**: the terrain at each corner and edge,
which is how pieces are matched.

### Example: a winding road with clean edges

Your project has a road terrain set: straight edges, corners and ends of road against grass.
1. Analyze the set's images (Assets ▸ Assets ▸ Analyze) so each tile's **Borders** are known.
2. Pick any piece of the road set as the brush. The toolbar shows **Auto-fit road**: tick it.
3. Drag a winding line across the grass: the road is laid with the right edges, corners and ends at every bend, and the
   tiles beside it change to match.
4. Erase a cell in the middle (**E**): it turns back to grass and the road ends round off on both sides.
5. Untick **Auto-fit road** to put one exact piece, such as a crossroads, by hand.

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

### Example: a red rug in the inn's front room

1. Make a prefab from `rug_red.png`, a top-down picture: **Drawn: Top-down (lies flat on the grid)**, **New prop
   prefab**.
2. Place it in the inn's front room: it lies on the floor's diamond, under the tables and the people.
3. Press **R** to give it a quarter turn so it runs along the room.
4. It shows under the floor's wooden boards piece you placed earlier: select the rug and press **Ctrl+]** to bring it
   forward (its **order** goes up), or **Ctrl+Shift+]** to put it on top of all ground pieces.

A field drawn in the isometric view instead takes **ground** rather than flat: tick **Sprite ▸ ground** and it fits its
cell, drawn under what stands.

Questions: why does my top-down tile look squashed? how do I make a rug lie on the floor? how do I put a road over
grass? what is the difference between flat and ground?

## The tile grid and framing

- **G** (View ▸ **Tile grid on empty ground**) draws cell borders where there's no ground yet, so you can see where to
  paint.
- **F** (View ▸ **Frame scene**, the toolbar's **Frame**) fits the whole scene in the view.
- **Right-drag** or dragging empty ground with the Select tool pans; the **wheel** zooms (speed and direction in
  Preferences ▸ Viewport).

### Example: planning a new island

1. Make an empty scene `isle` and press **G**: cell borders show on the empty ground.
2. Paint a sand outline of the island with the `sand` tile, following the grid.
3. Zoom out with the **wheel** and pan with **right-drag** to see the far end.
4. Press **F** (or the toolbar's **Frame**) to fit the whole island in the view.
5. Press **G** again to hide the grid once the shape is done.

Questions: how do I show the grid? how do I see the whole scene? how do I pan and zoom?

## Height: raising and lowering the ground

The toolbar's **Height** tool shapes a built place's ground: drag over the ground to **Raise**, **Lower** or **Level**
it (the mode select beside the tool), within a **Radius** of 0–12 cells round the cursor.

Characters walk between heights only where there's a **Ramp** (stairs, a slope, a ladder); elsewhere a step much higher
or lower than where one stands is a cliff. Add one with right-click the ground ▸ **Ramp here (walk between heights)**,
or the Ramp component. A Sprite's **lift** raises a thing off the ground by pixels (standing on high ground); things on
raised ground are lifted to its height.

### Example: a hill with a lookout and steps up

1. In the built place `highlands`, pick the toolbar's **Height** tool, choose **Raise** and set **Radius** to `4`.
2. Drag over the middle of the moor a few times: the ground rises into a hill.
3. Choose **Level** and drag over the top to flatten it for a lookout tower.
4. Press Play: the player can't climb the steep side, a cliff.
5. Right-click the gentlest slope ▸ **Ramp here (walk between heights)**. Play again: the player walks up there, and
   only there.

Questions: how do I make hills? how do I make a cliff? why can't my character climb up? how do I add stairs between
heights?
