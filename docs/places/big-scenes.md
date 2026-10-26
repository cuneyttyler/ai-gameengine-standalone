# Big scenes — painted places of any size, by tiles

Status: **design, v1 (2026-10-24)**. Covers the Painted build method (`docs/build-fixes.md`) for places
too big for one image, and for any place that must hold up under a close camera. Builds on the
plan's isometric block-out (`engine/editing/PlaceBlockout`), the BFL client (`ai_service/app/bfl.py`) and
the vision checks (`ai_service/app/inspect.py`: `match`, `audit`, `fixes`).

---

## 1. The problem

An image model paints one picture of a bounded size:

| Model (BFL) | Largest output | Reference images |
|---|---|---|
| FLUX.1 Kontext pro / max | about 1 MP | 4 |
| FLUX.2 klein / pro / flex / max | 4 MP | 4 (klein), 8 (pro, flex, max) |
| FLUX.3 Image | 4 MP with references | 10 |
| FLUX.3 Image Klein | 16.8 MP ("4k" tier) | 10 |

A place, though, can be any size, and the game's camera can come close. A settlement of 116 × 116 cells
in one 4 MP image gives each house about 120 px: fine from far away, mush up close. A village four
times that size in one image would give each house 60 px. Picking a bigger model doesn't solve this,
because there is always a bigger place.

So a painted place is made of **tiles**: each about one model image, painted at the density the camera
needs, so that together they read as **one painting**. The size of the place then only changes how
many tiles there are, never how much detail each has.

## 2. Goals

1. **Any size.** A room, a village, a region of a few hundred cells a side: the same pipeline.
2. **A set density.** Art pixels per cell are chosen (by the camera the game uses), not left to
   whatever fits one image.
3. **One painting.** No visible tile edges; one palette, light, pixel size and style everywhere.
4. **The plan exactly.** Every building, road, fence and tree where the plan puts it (the block-out is
   the plan; the model only paints it).
5. **Playable.** What can be walked on, what a character walks behind, the doors and the story's
   anchors all come from the plan, so they line up with the painting by construction.
6. **Editable.** A change to the plan repaints only the tiles it touches.
7. **Any provider.** The model is a project preference (§13); tiling doesn't depend on one.

Out of scope here: animated water and foliage, day/night variants (§15 lists them as later work).

---

## 3. Density: how big the painting is

The engine draws a cell as a 64 × 32 px diamond at zoom 1 (`IsoParams`). The **density** `d` is the
number of art pixels per screen pixel at zoom 1:

| `d` | Pixels per cell (width) | A 3-cell house | For |
|---|---|---|---|
| 1 | 64 | ~190 px | a strategy-game view, zoom ≤ 1 |
| 2 | 128 | ~380 px | the default play camera (`playZoom` 2), RPGs |
| 3 | 192 | ~570 px | very close cameras, cutscenes |

A place of `W × H` cells is a diamond `(W + H) · 32 · d` px wide and half as tall; about half of its
bounding rectangle is the place itself (the corners are background).

| Place | Cells | `d` = 1 | `d` = 2 | `d` = 3 |
|---|---|---|---|---|
| Room | 24 × 24 | 1536 × 768 (1.2 MP) | 2.4 × 1.2k (4.7 MP) | 10.6 MP |
| Settlement (the Test2 one) | 116 × 116 | 7.4 × 3.7k (27 MP) | 14.8 × 7.4k (110 MP) | 248 MP |
| Village × 4 | 232 × 232 | 110 MP | 440 MP | 990 MP |
| Region × 16 | 464 × 464 | 440 MP | 1.8 GP | 4 GP |

The project sets `d` (Project Settings ▸ Places ▸ `density`, default 2); a place may override it (a
world map at 1, a throne room at 3).

## 4. Zones are the tiles

A scene is made of the zones fixed at Confirm and Layout (wards, the docks, the square, a dungeon's
rooms). **A big scene is a scene with many zones**, not one with bigger zones. So the painting is cut
along the zones, not along a blind grid:

- **A zone is painted as a unit.** Its buildings, yards and streets come from one painting (or a few
  tiles painted together, if one zone is bigger than one image), so a ward never has a seam through its
  middle.
- **The land between zones is where tiles meet.** Forest, meadow, scrub, rocky ground, marsh, a road or
  a river: a seam in a wood or a field is all but invisible, while one through a street is not. These
  areas are painted last, as their own tiles, overlapping the zone tiles on both sides and absorbing
  the difference (§8).
- **So the layout must have them.** Today the land between zones is whatever is left over (the site's
  `wild` landscape scatter). For big scenes, the in-between land becomes **zones of its own**, planned
  at Layout like any other:

  | In-between zone | Ontology class | What it is | Where the layout puts it |
  |---|---|---|---|
  | `buffer-forest` | `NatureArea` | woods: a mass of trees with undergrowth | between wards; round the edge |
  | `buffer-meadow` | `NatureArea` | open grass with flowers and bushes | between farms, round a town |
  | `buffer-scrub` / `buffer-rocks` | `NatureArea` | dry scrub, rocky ground, boulders | hills, frontier, between camps |
  | `buffer-marsh` | `NatureArea` / `WaterBody` | reeds, pools, mud | by rivers and lakes |
  | `buffer-empty` | `Buffer` (new) | bare ground, nothing standing | where the story wants space |

  A new ontology class `Buffer` (broader: `NatureArea`) marks a zone as in-between land. The layout
  stage gets one rule: **every two zones that aren't joined on purpose (a street, a gate, a shared
  wall) have a buffer between them, at least one tile overlap wide** (about 6–12 cells at `d` = 2).
  The Planner's place outline names the buffers it wants ("the town sits in a forest clearing"); the
  layout fills the rest with the build type's default buffer (forest round a village, scrub round a
  camp, bare rock between a dungeon's rooms, which is already the case indoors).

Indoors, the rock between a dungeon's rooms and the void round a building are buffers already.

### The tile grid inside a zone

A zone (or a buffer) bigger than one image is split into **screen-space rectangles** over the
painting, not cell squares: the model paints rectangles, and a rectangle tile has no diamond-shaped
empty corners inside the place.

- **Size.** The model's largest output: 2048 × 2048 for a 4 MP model; 4096 × 4096 for FLUX.3 Image
  Klein at 4k. Each tile is painted at exactly its size, so there's no scaling.
- **Overlap.** 20 % on each side that has a neighbour (409 px at 2048), so each tile's core is about
  1640 × 1640 (2.7 MP of new painting per 4 MP image).
- **Skipped.** A tile whose rectangle holds no cell of the place (only background) isn't painted;
  the background colour fills it.
- **Levels.** Each level of a place (a dungeon's floors, a building's storeys: `PlaceLevelAreas`) is
  its own grid and its own painting (each is its own scene, `docs/build-fixes.md`).

The number of tiles is about `content MP / 2.7` for a 4 MP model:

| Place at `d` = 2 | Content | Tiles (4 MP model) | Tiles (FLUX.3 Klein 4k) |
|---|---|---|---|
| Room 24 × 24 | 2.4 MP | 1 | 1 |
| Settlement 116 × 116 | 55 MP | ~22 | ~6 |
| Village × 4 | 220 MP | ~85 | ~22 |
| Region × 16 | 880 MP | ~330 | ~85 |

A tile is named by its column and row (`t-<c>-<r>`), with its rectangle in painting pixels and the
cells it covers (for edits, §11).

## 5. The block-out per tile

`RenderPlanBlockout` already draws any cell window at any width. For tiles it gains a **screen-window**
form: the whole place's projection at density `d`, and only the tile's rectangle rasterised. This
keeps edges exact: a building that straddles two tiles is drawn in both, at the same pixels.

Each tile also gets **its own legend**: only the kinds of things in it (the colour key stays the same
across the place: a house is "bright red" in every tile), so a tile's prompt lists what it holds and
no more.

The colours are fixed for the whole place before any tile is drawn: the most common kinds get the most
distinct colours (as now), and the mapping is stored in the manifest (§10).

## 6. Painting order and consistency

The hard part isn't painting a tile: it's making 85 tiles look like one painting. Five rules do it.

1. **A style anchor first.** One zone is painted first, alone: the one with the most kinds of things in
   it (usually the heart of the place: its square, its main street). Its painting is the **style
   reference** for every other tile. The user sees it first and can repaint it before the rest are
   painted (§12): it sets the whole place's look for the cost of one image.
2. **Zones, then buffers.** Then the other zones, each as a unit (in order of distance from the anchor,
   so the nearest are painted first and can be referenced). Zones don't touch each other (a buffer lies
   between them), so they can be painted **in parallel**. Last, the buffers: each buffer tile has the
   zones on both its sides already painted, and continues them (rule 3).
3. **Painted neighbours go into the input.** Before a tile is sent, the painted overlap strips of its
   already painted neighbours are pasted over its block-out, and the prompt says so: "the edges are
   already painted: continue them exactly". The model continues roofs, roads and woods across the edge
   instead of starting afresh. This is outpainting done with an editing model.
4. **References.** Each tile request carries, after the input image: the style anchor, then up to two
   painted neighbours whole (the closest), so the palette and the drawing of each kind of thing carry
   over. (Kontext takes 4 images, FLUX.2 pro 8, FLUX.3 10: within every model's limit.)
5. **One prompt, one seed.** Every tile gets the place's prompt (§7), the same seed, and the same model
   and settings. The model choice can't change in the middle of a place.

Zones are painted in parallel (up to the provider's limit on concurrent requests: BFL allows a few at a
time); the buffers wait for the zones round them. Within a zone bigger than one image, its tiles go
from the anchor tile outward, each with its painted neighbours in its input.

## 7. The prompt per tile

Built from the ontology and the story, as the single-image prompt is (`bfl.blockout_prompt`), with
three additions:

- **Where this tile is:** "This is the north-east part of Blackthorn Hollow: the river enters at the
  top; the docks are at the bottom right." (compass words from the tile's cells, and the zones it
  covers, by name and the ontology's description).
- **Its colour key:** only the kinds in this tile (§5), each with the ontology's words (`Role::draw`,
  the module's name and description).
- **Continuity:** "The strips along the edges are already painted: continue them seamlessly. Match the
  second image's style, palette and light exactly." (the anchor is the second image).

The prompt never names tiles, seams or pixels the model could draw (no "border", no "frame").

## 8. Seams

Even with continuity, two paintings of one overlap differ a little. The overlap is blended, checked,
and mended.

1. **Blend.** In each overlap, the newer tile's pixels are mixed over the older's with a feathered
   weight across the overlap's width (smoothstep, 0 at the outer edge to 1 at the core). Seam
   lines are cut along low-contrast paths through the overlap (a minimum-cost path through the
   difference image, as in image quilting), so a seam never cuts a roof or a tree in half.
2. **Check.** Along each blended seam, the colour and texture difference across it is measured (mean
   difference of 8 px strips on both sides). Above a threshold, the seam is flagged.
3. **Mend.** A flagged seam is repainted with an inpainting model (BFL `flux-pro-1.0-fill`, or the
   provider's inpaint): a band 96 px wide centred on the seam, masked, with the place's prompt. If that
   fails twice, the later tile is painted again.

## 9. Checks

Every tile is checked against its block-out, and the place as a whole against the plan.

- **Per tile (cheap):**
  - **Code first:** the painted tile and its block-out are compared where buildings stand. Each
    building's screen polygon is known (§10), and its pixels must no longer be the key colour. A leaked
    key colour (a "bright red" roof) means repaint.
  - **Then vision:** the `match` check on the tile (block-out and painting side by side): "is each
    building, road and fence where the block-out has it, and drawn as what its colour stands for?"
- **Per place:** a downscaled mosaic (2k wide) gets `match` against the layout and the plan, and
  `audit` (everything drawn wrong). `audit`'s problems name places in compass words, which map back to
  tiles; those tiles are repainted with the fixes in their prompt, then `fixes` judges each one.
- **Scores** are written to the manifest and to `docs/build-scores.md`, as for Assembled builds.

## 10. From painting to game

Because the painting follows the block-out exactly, the game's map comes from the **plan**, not from
reading the painting:

- **Walking:** the plan's cells (solid terrain, buildings' footprints, props that block), as an
  invisible walk layer (`PlacePaint`'s walk layer, filled from the plan instead of the vision map).
- **Occluders:** each building, wall, tree and big prop is cut out of the painting along the polygon
  the block-out drew it with (known exactly, so no vision is needed), with a few pixels' margin.
  Each cut-out is a sprite standing on its footprint, drawn by depth, so a character walks behind it.
  Woods cut as one mass per clump, not per tree.
- **Doors, anchors, the way in:** the plan's doors (objects' `facing`) and the story's anchors (by their
  module) are markers on their cells.
- **Gateways** between levels: from the stairs, as for Assembled builds.

**The manifest** (`places/<id>/backdrop/manifest.json`):

```json
{
  "schema": "aige.backdrop/1",
  "place": "<build id>", "level": "", "density": 2, "model": "flux-3-image-klein", "seed": 7,
  "size": [14848, 7424], "tile": [4096, 4096], "overlap": 819,
  "colours": {"house": "#DC2020", "warehouse": "#2448E0"},
  "prompt": "…",
  "tiles": [{"id": "t-3-1", "rect": [9830, 3277, 4096, 4096], "cells": [[60, 10], [90, 40]], "anchor": false,
             "painted": "tiles/t-3-1.png", "hash": "…", "scores": {}, "attempts": 1}],
  "occluders": [{"node": "docks-ward/warehouse#0", "sprite": "parts/o-12.png", "rect": […], "foot": [x, y]}],
  "chunks": "chunks/"
}
```

## 11. Storage, streaming and edits

- **Chunks.** The blended painting is cut into 1024 × 1024 chunks (`chunks/c-<x>-<y>.png`), with a mip
  pyramid (each level half the size) for zoomed-out views. The scene holds one backdrop entity per
  level (a `Backdrop` component naming the manifest); the renderer draws the chunks in view at the mip
  level the zoom needs, loading and dropping them as the camera moves (`StreamingSettings`).
- **The library.** A finished backdrop (its manifest, chunks and occluders) goes into the AIGE library
  and the user library under **Backdrops** (`UserLibrary`), reusable as a whole place.
- **Edits.** A change to the plan (a building moved, a fence added) is diffed against the manifest's
  plan hash per tile: only tiles whose cells changed are repainted (with their painted neighbours as
  input, §6), then blended in. A tile's cache key is the hash of its block-out, prompt, seed, model and
  references, so nothing is painted twice.
- **Locks.** A tile the user likes can be locked: edits next to it repaint up to it, never over it.

## 12. The flow in the editor and the Planner

1. **Plan:** as now (layout, plan, their checks).
2. **Block-out:** drawn at once; the user sees the whole place colour-coded with its key.
3. **Style anchor:** one tile painted; shown beside the block-out. The user can accept it, repaint it
   (a new seed), or edit the prompt's look words. This is the only stop.
4. **Tiles:** painted ring by ring, each appearing as it lands, with a progress bar (tiles done /
   total, cost so far).
5. **Seams and checks:** blended, mended, checked; problem tiles repainted (at most twice).
6. **Scene:** the walk layer, occluders and markers made from the plan; saved; installed in the game
   build as any place.

The Planner runs the same flow without the stop at step 3 (the anchor is checked by `match` instead).
`AIGE_PAINT` in the build test runs it headless (`tests/live_build_test.cpp`).

## 13. Providers

Preferences ▸ AI ▸ Places (painted): the model (FLUX.3 Image Klein, FLUX.2 pro, Kontext pro, …), its
largest tile, and how many references it takes. The tiler reads these, so tile size and the number of
references follow the model. A provider needs three things: edit an image from a prompt, take at least
one reference image, and (for seams) inpaint with a mask. One that can't inpaint falls back to
repainting the later tile.

## 14. Cost and time

Per tile cost `c` (a 4 MP edit): about $0.03–0.08 on FLUX.2 [pro]–[max]; FLUX.3 Image Klein's 4k
price is to be measured. About 30–60 s per tile; with 4 at a time, rings run in parallel.

| Place at `d` = 2 | Tiles (4 MP) | At $0.05 a tile | Time (4 at a time) |
|---|---|---|---|
| Settlement 116 × 116 | ~22 + ~6 mends | ~$1.40 | ~8 min |
| Village × 4 | ~85 + ~20 | ~$5.30 | ~25 min |
| Region × 16 | ~330 + ~70 | ~$20 | ~1.5 h |

Before painting, the editor shows the estimate (tiles × the model's price) and asks; a project can set a
budget per place.

## 15. Plan of work

0. **Buffers in the layout:** the ontology class `Buffer` and the buffer modules (forest, meadow,
   scrub, rocks, marsh, empty); the layout rule (a buffer between zones not joined on purpose); the
   Planner's outline naming buffers; the build types' default buffer (`buildtypes.json`). The layout
   check (`inspect.py` "layout") learns that buffers are wanted, not empty space.
1. **Tiled block-out:** `RenderPlanBlockout` takes a screen window and a density; one colour map per
   place; per-tile legends. Test: tiles stitched unpainted are pixel-identical to the whole block-out.
2. **Tiler:** `engine/editing/PlaceTiles.{h,cpp}`: tiles from zones (a zone's screen rectangle, split
   when bigger than one image) and buffers, the order (anchor zone, other zones, buffers), the input
   composites (block-out with painted neighbour strips), the manifest.
3. **Service:** `ai_service/app/backdrop_tiles.py` and `POST /places/backdrop-tile` (one tile: input,
   references, prompt, model) and `POST /places/backdrop-seam` (inpaint a band).
4. **Blending:** feather plus minimum-cost seams, the seam check; in C++ (`PlaceTiles`), so no image
   round-trips.
5. **Game side:** the walk layer, occluders and markers from the plan (`PlacePaint` without the vision
   map); `Backdrop` component, chunk streaming and mips in the renderer.
6. **PlaceBuild stage** "painting" with per-tile progress, persistence and resume (a stopped build goes
   on from the last painted tile); the editor's stop at the anchor; Planner flow.
7. **Edits and locks:** per-tile plan hashes, the repaint of changed tiles.
8. **Library:** Backdrops section.
9. **Tests:** a 2 × 2-tile room (fast, cheap), the Test2 settlement, a 4× village; scores per run in
   `docs/build-scores.md`.

Later: animated water and foliage as separate layers painted over the backdrop; night versions painted
from the day tiles (the day tile as input, "the same place at night").

## 16. Open questions

- **Wavefront vs. all at once.** Neighbour strips in the input are the strongest continuity tool, but
  they make the order serial by ring. If the anchor reference alone gives one style, independent tiles
  plus seam mending would be faster. To be measured on the settlement.
- **Tile-local drift.** Over 300 tiles, small drifts can add up across rings. Mitigation: every tile
  references the anchor directly, not only its neighbours; measure colour statistics per ring and
  correct (histogram matching to the anchor) before blending.
- **Key colours leaking.** FLUX.2 klein copied key colours into the art (pink roofs). Per-tile code
  checks catch it (§9); if it's common for a model, use natural but distinct key colours (thatch brown,
  slate grey, …) for that model.
- **Interiors.** Cutaway walls and the dark round rooms: the block-out draws them low; whether models
  keep rooms readable at tile edges needs a dungeon test.
