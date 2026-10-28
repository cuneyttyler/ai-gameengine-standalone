# Background: a place's ground painted from its plan

Status: **method v1 (2026-10-26), in the editor (2026-10-27)**. Tested on a 110×110-cell modern city
(`full_city_test_3/background_layered`) and a 43×43-cell dungeon (`dungeon_test/background_layered`). Steps 1–7 are
`paint_background` in `ai_service/app/matched.py` (`POST /places/background`); the scene projects it (step 8) when it
lays the ground. See §7 for how the editor uses it.

**The goal.** The background is the ground every asset stands on: roads, sidewalks, grass, plazas, stone floors. It
must:
- match the plan exactly: every area and edge in its cell, and nothing where the plan has rock or void;
- be sharp at the scene's full size (about 40–100 px a cell top-down);
- show only ground: no buildings, people, objects or walls painted at the edges;
- work for any kind of place (city, village, dungeon, camp) and any size, from a 20-cell room to a big scene;
- cost few model calls.

**The model to keep in mind: a designer painting a map.** We hand over the plan's ground as a flat colour map ("grey
is the stone floor, black is rock") with a short brief, and the designer paints each colour as its ground. A big map
is too large to paint in one go at full detail. So the designer first paints the whole map small, which decides
every look once, then paints each part again large, copying the small painting. The parts agree because they all
copy the same picture.

## 1. Terms

| Term | Meaning |
|---|---|
| **grid** | the plan's terrain, one name per cell (`stone-floor`, `asphalt`, `lawn`, `rock-wall`, `void`); from `layout.solved.json` (`terrain.rows`, `terrain.palette`) |
| **solid** | a cell nothing stands on: rock, wall, void (`_solid`). Drawn black and kept black. |
| **colour map** | the grid drawn top-down, one flat colour per terrain, at `SRC` px a cell (`background_palette`) |
| **whole picture** | the colour map painted in one call at low resolution: the looks, decided once |
| **tile** | a rectangle of cells painted again at high resolution from its part of the whole picture |
| **outer border** | a tile's full rectangle: everything the model sees |
| **inner border** | the part of a tile that is kept: the outer border less half the overlap on every side shared with another tile |
| **band** | the strip, centred on an inner border, where two tiles fade into each other |
| **iso view** | the scene's projection (2:1); its parameters (`tw, th, top, x0, y0, y1`) come from the block-out's `plan` view in `matched/sheets.json` |

## 2. The algorithm

Input: the grid (W×H cells), the place's kind and style (from the request and the Brief), the iso view.
Output: `background.png` (iso, the scene's size × scale) and `background-topdown.png`.

### Step 1. Colour map

Draw the grid top-down with `background_palette(terrain, (W, H), cell_px=32)`:
- Each terrain gets its own tone (from `vocabularies.json`, terrain-kind `look.tone`), so the colour already suggests
  the ground: asphalt dark grey, lawn green, sand yellow.
- Two terrains with close tones are pushed apart (at least 42 apart in RGB). The commonest keeps its tone; the rarer
  one moves lighter, then darker.
- Solid cells are black.

The map is **top-down, not iso**. A top-down map gives the model a plain, square picture with nothing hidden. The
angle is added exactly by code in step 7, so the model never has to draw it.

Returns: the picture, its key (`[{terrain, words, rgb}]`) and the grid.

### Step 2. Prompt

Short plain sentences. No asset images, no reference images, no list of the scene's objects (the model would try to
paint them in). The pattern:

```
A {kind} background for a game, seen from directly above (top-down).
Style: {style words}. It looks like {reference games}.
Image 1 is a {colour map | map} of the {ground | dungeon}. Paint each colour as its ground.
Keep every area and every edge exactly where it is.
{one line per colour: "- #424248 = asphalt road with white lane markings"}
Only the ground: no buildings, no cars, no people, no trees, no objects, no text.
```

- Each colour line says what the ground looks like in plain words, including the details that make it read: "asphalt
  road with white lane markings", "paved sidewalk", "park grass", "old worn stone slabs, cracks, moss in the joints".
- Solid ground is named and told to stay as it is: "Black is solid rock: keep it black."
- The "no …" line names only things that place could contain. A dungeon's says "no walls"; a city's says "no cars".

Examples used in the tests:

<details><summary>City</summary>

```
A city background for a game, seen from directly above (top-down).
Style: modern city. It looks like Cities: Skylines, SimCity 4.
Image 1 is a colour map of the ground. Paint each colour as its ground. Keep every area and every edge exactly where it is.
Colours:
- #424248 = asphalt road with white lane markings
- #949490 = concrete ground where buildings stand
- #CAC0B0 = paved town square
- #E0DED6 = paved sidewalk
- #608C40 = park grass
- #BEAA78 = sand
Only the ground: no buildings, no cars, no people, no trees, no street lights, no objects, no text.
```
</details>

<details><summary>Dungeon</summary>

```
A dungeon background for a game, seen from directly above (top-down).
Style: lived-in. It looks like Diablo II, Baldur's Gate, Darkest Dungeon.
Image 1 is a map of the dungeon. Grey is the floor: old worn stone slabs, cracks, moss in the joints, some dirt and gravel. Black is solid rock: keep it black. Keep every room and corridor exactly where it is.
Only the floor: no walls, no objects, no people, no text.
```
</details>

### Step 3. Whole picture (one call)

Scale the colour map so its longer side is 2048 px (nearest-neighbour, so edges stay sharp; sides rounded down to a
multiple of 16). Send it to **FLUX.3 Image** (`flux-3-image`, image edit) with the step 2 prompt.

This one call decides every look in the scene: which blocks are paved and which are plain concrete, the road
markings, the grass, the slab size. Nothing after this step makes a new choice.

- The model is FLUX.3 Image, **never klein**: klein adds buildings, cars and walls even when told not to.
- Resolution: 2048 / max(W, H) px a cell (city 110 cells: about 19; dungeon 43 cells: about 48).
- **If that's already sharp enough for the scene, stop here** and go to step 6 with the whole picture as the only
  tile.

### Step 4. Plan the tiles

`plan_tiles(grid, max_cells, overlap)` chooses the tiles' outer borders from the plan:

1. **How many.** Along each axis, the fewest parts that keep every tile within `max_cells`, overlaps included:
   `parts = ceil((n − overlap) / (max_cells − overlap))`.
2. **Where each split goes.** Near each even share (n·i/parts, within a quarter of a tile), at the line that is
   cheapest in `seam_costs`: through the middle of wide areas or through rock, and along a road or an edge only where
   there's no choice. Costs per cell:
   - an edge between two grounds: 20;
   - inside a strip 6 cells wide or less (a road, path or corridor): 12;
   - any other ground: 1;
   - solid: 0.
3. **The tiles.** The split lines are the inner borders. Each tile's outer border reaches `overlap / 2` cells past
   every split it touches. Tiles are returned in reading order.

Choose `max_cells` so a tile painted at 2048 px gets the detail wanted: `max_cells ≈ 2048 / target px a cell`.

| Test | Grid | max_cells | overlap | Tiles | Tile resolution |
|---|---|---|---|---|---|
| City | 110×110 | 64 | 16 | 2×2, splits at 61 / 56 | about 30 px a cell |
| Dungeon | 43×43 | 30 | 10 | 2×2, splits at 22 / 23 | about 76 px a cell |

### Step 5. Paint each tile (one call each, in parallel)

For each tile:
1. Crop its outer border from the **whole picture** (not from the colour map) and scale it up (bicubic) so its
   longer side is 2048 px.
2. Send it to FLUX.3 Image with the detail prompt:

```
{first line of the step 2 prompt}
Image 1 is one part of it. Make it sharp and detailed at high resolution.
Keep everything exactly as it is: every area, edge, line and colour stays where it is.
Only the ground: no buildings, no cars, no people, no trees, no street lights, no objects, no text.
```

Every tile starts from the same picture, so a block that is paved in the whole picture is paved in both tiles that
hold it, and a road's markings line up across tiles. The outer border shows the model what lies beyond the kept part,
so roads, corridors and edges carry on correctly across the join.

### Step 6. Check each tile, repaint if needed

Scale every tile to the output size (`CELL` px a cell). For each pair of tiles that overlap, measure the mean absolute
difference (0–255) of their pictures inside the overlap.
- **Good:** 7–21 in the tests (only the fine detail differs).
- **Bad:** 52–58 in the tests (the model redrew the tile: shifted, rescaled, new layout).
- **Rule:** a tile above about 35 against **every** neighbour it overlaps is repainted from the same input. That's
  one more call; at most 2 retries a tile.

In the city test one tile of four was redrawn on the first try (shifted and scaled about 0.98). The check caught it,
and one repaint fixed it (it then scored 15–20).

### Step 7. Join

`blend_tiles(tiles, (W, H), CELL, overlap, band)` lays the tiles in reading order:
- Each tile is kept inside its inner border.
- Across a `band`-cell strip centred on each inner border it shares with an earlier tile, it fades in linearly from 0
  to full: one ramp across, one down, multiplied at corners.
- Edges on the scene's border have no band.

No seam search or colour matching is needed: the tiles already agree, so a soft blend hides the join. The band must
fit inside the overlap: `band ≤ overlap`. The tests used 6 for the city (overlap 16) and 4 for the dungeon
(overlap 10).

Then **cut to the plan**: every solid cell is set back to black, so nothing painted spills into rock or void.

Result: `background-topdown.png`, W·CELL × H·CELL (city 4400², dungeon 4128²).

### Step 8. Project into the iso view

One affine transform (PIL `Image.transform(..., AFFINE)`) maps the top-down picture onto the scene's iso view, at the
block-out's size × `scale`. With `ox = (y1 − y0) · tw / 2` and `s = scale`:

```
a = 1/(s·tw)    b = 1/(s·th)    c = −ox/tw − top/th + x0
d = −1/(s·tw)   e = 1/(s·th)    f =  ox/tw − top/th + y0
coefficients = (a·CELL, b·CELL, c·CELL, d·CELL, e·CELL, f·CELL)   # bilinear
```

Code sets the angle, so it is exactly the grid's 2:1. Cells land exactly where the block-out and the assets put
them.

Result: `background.png` (city 12000×6192, dungeon 8256×4464 at scale 2).

## 3. Calls

| Scene | Calls |
|---|---|
| Small (the whole picture is sharp enough) | 1 |
| 2×2 tiles | 1 + 4 (+ repaints) |
| N tiles | 1 + N (+ repaints) |

Tests: the city took 6 calls (1 + 4 + 1 repaint) and the dungeon 5.

## 4. Big scenes: more levels (designed, not tested yet)

The whole picture must be fine enough to decide the looks. In the tests, about 18 px a cell or more was enough.
When a scene is larger than about 110 cells across (2048 / 18), repeat step 3 one level down:
1. The whole scene at 2048 px decides the looks of large areas.
2. Regions (planned with `plan_tiles` from the whole picture's grid) are painted from their part of it, like
   step 5. Each region is then the "whole picture" for its own tiles.
3. Tiles are painted from their region's picture, checked, and joined, as in steps 5–7.

Calls grow to about 1.3 × the number of tiles. Every look is still decided once, at the top, and handed down.

## 5. What we tried and why it isn't used

| Tried | Result |
|---|---|
| One call on the iso block-out | low resolution; the model draws its own angle |
| klein on windows of the block-out | invents walls and objects, loses corridors |
| FLUX.3 for the layout, then klein for the look | klein adds walls back |
| One ground texture, cut to the plan | good for a dungeon's single floor; no roads or markings for cities |
| Swatches per terrain | areas don't join; rejected |
| Tiles painted straight from the colour map, joined along the cheapest seams with colours matched | each tile chooses its own looks (paved in one tile, concrete in the next); seams show wherever an area crosses a join, wherever the joins are moved |

The last row is the reason for the whole picture: **decide every look once, then only add detail.**

## 6. Parameters

| Name | Value | Meaning |
|---|---|---|
| `SRC` | 32 | colour-map px a cell |
| model | `flux-3-image` | every call |
| call size | 2048 px on the longer side, sides a multiple of 16 | |
| `max_cells` | 2048 / target px a cell | largest tile, overlaps included |
| `overlap` | 10–16 cells | outer border = inner border + overlap/2 |
| `band` | 4–6 cells, ≤ overlap | blend strip at the inner border |
| `CELL` | 40 (city), 96 (dungeon) | top-down output px a cell |
| repaint threshold | about 35 | mean overlap difference |
| `scale` | 2 | iso output × block-out size |

## 7. In the editor

- **Painting it.** `paint_background(painter, terrain, cells, description, folder)` runs steps 1–7: the colour map,
  the prompt (§2) from the description, the whole picture, tiles where it's coarser than 64 px a cell (at most 9,
  planned by `plan_tiles`), the check and up to two repaints a tile, `blend_tiles`, solid ground cut back to black. It
  writes `places/<id>/background/background-topdown.png` and its prompts. `POST /places/background` calls it with the
  background provider (`PUT /background-provider`; FLUX.3 Image only, for now).
- **The description.** What it's painted from: a first line saying what the place is and how it looks, then one line
  per kind of ground, `<ground>: <how it looks>` (`background_words` reads them; a ground with no line uses its
  vocabulary words). The AI Planner writes it in the place's Brief (`background.description`); without one, it's made
  from the Brief (kind, style, the build type's reference games). It's an editable field in **Build ▸ Assets**, under
  the background row, at every stage.
- **Build ▸ Assets ▸ Background** (the row at the top of the art plan): its picture, who painted it, and
  **Generate / Regenerate** (`build:background` → `PlaceBuild::MakeBackground`). The Build step lays it as the ground,
  cell by cell (`PlaceSceneOptions.groundPicture`).
- **Edit ▸ Background Image…** (`BackgroundDialog.vue`): the background painted again, or replaced by a picture file.
  - **Build:** None, or one of the project's builds (the open one at first). **Model:** the background models
    (`backgroundModels.ts`, the same list as the Build panel's). **Words:** the chosen build's background description
    (`BackgroundDescription`, editable); with none, the open scene's last words (`assets/backgrounds/<scene>.json`).
  - **Regenerate with a build** (`background:regenerate`): that build opened in the Build panel if it isn't, its
    background model and description set, `MakeBackground` from its plan. When it comes, a built place whose scene is
    open is laid with it again (`BuildIntoScene`); otherwise the editor says to open its scene and Build.
  - **Regenerate with no build:** the open scene's ground painted from the words alone (`POST /images/background`, the
    model's `create`, in the project's view: isometric or top-down), written as `assets/backgrounds/<scene>.png` and
    drawn as one picture behind everything (an entity "Background", ScreenTransform2D at depth -1e7) over the scene's
    extent (its ground's cells and tiles, else 32 x 32 cells). Regenerating replaces its picture.
  - **Use a picture file…** (`background:fromFile`): with a build, a top-down picture as its ground (as before); with
    none, the scene's backdrop.
- **Who paints it.** Preferences ▸ AI ▸ Background (the app's default); Project Settings ▸ AI ▸ background (this
  project's, "Use app default" unless set).
- **The things in its light.** The scene tones the place's sprites to the background's colours (matched-art.md §2d).
- **Still to do:** a vignette in the whole picture can darken its corners (even it out before the tiles); dungeon slabs
  can follow the cell grid ("slabs of different sizes" may loosen it).
