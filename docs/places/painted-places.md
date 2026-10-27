# Painted places, marked by hand

Status: **designed (2026-10-27), Phase 26**. The plan's milestones are in `docs/project/development_plan.md` Phase 26.

**The idea.** A place can be painted whole: one picture in the game's isometric view, like Baldur's Gate or Pillars of
Eternity draw their areas. The painting is never cut into assets. To make it a level, two things are added on top:

- **what the player can walk on:** cells, walkable or blocked;
- **occluders:** the parts of the painting drawn again over a character who walks behind them, such as a house front,
  a tree canopy or a wall top.

Code can't find occluders reliably in a painting (trees, wooden walls, a tower), and a person can, in minutes. So after
the painting, the build has a **Mark** step where the user marks them over the painting. The plan pre-fills the step,
and click-to-select helps.

## 1. Build methods and steps

A build has a **build method** (Project Settings ▸ Places ▸ method, also asked in New Project):

| Method | Steps | What it makes |
|---|---|---|
| **Assembled** | ① Confirm → ② Layout → ③ Plan → ④ Assets → ⑤ Build | each building, tree and prop its own art, put together on the grid over a painted background (`matched-art.md`, `background.md`) |
| **Paint** | ① Confirm → ② Layout → ③ Plan → ④ Paint → ⑤ Mark → ⑥ Assets → ⑦ Build | the place painted whole, marked by hand; art only for what the player uses |

- A new build starts with the project's method; the Build panel's start page has a **Build method** select above Choices
  (`build:start {method}`), and ④ Paint can switch a build to Assembled (`build:setMethod`).
- The AI Planner's home page has a **Build method** too (`planner:setBuildMethod`, kept with the plan as `buildMethod`);
  every place the plan builds starts with it. In the automatic game build, a Paint place is painted, its marks
  pre-filled from the plan, and then built (the user refines the marks in ⑤ Mark later).
- `PlaceBuild::Method()` ("Assembled" or "Paint") is saved with the build; a build from before methods is Paint if it
  was painted, else Assembled.
- Once a place has a painting, its Assets step makes only what the painting doesn't hold: things the player uses
  (doors, chests, items: role affordance `use`, `container`, `item`), characters and anything that moves. Buildings,
  trees, fences, walls, ground and décor are in the painting.

## 2. Paint

1. The plan's block-out (`RenderPlanBlockout`) is drawn at 2048 px across, with its legend and id map:
   - `blockout.png`: the plan in the game's view, colour-coded by kind;
   - `blockout-ids.png`: each object's pixels, as index + 1 in r + 256 g;
   - `blockout.legend.json`: what each colour is, and the view.
2. The **paint model** (Project Settings ▸ AI ▸ paint, New Project ▸ Paint model; `bfl.paint_place`) paints it, at up
   to 4 MP:

   | Model | How |
   |---|---|
   | FLUX-3-Image (default) | the block-out with the colour-key prompt and the counts (Phase 25: layout 9, fit 10) |
   | FLUX-2-Klein-4b | the block-out scaled to 4096 px as a "rough draft" with a short key (klein copies a flat input it's told about) |
   | FLUX-2-Pro, FLUX-2-Max | the block-out with the colour-key prompt and the counts, prompt upsampling off (rich looks; Phase 25: may drift from the layout, swap kinds) |
   | FLUX-3-Image-to-Klein | FLUX-3-Image, then klein repaints its look |
   | FLUX-3-Image-to-Pro | FLUX-3-Image, then FLUX.2 pro refines it with the block-out beside it saying what each building is |

   Big places will be painted in tiles, as the background is (`background.md` §4–§7); not yet.
3. The result is `painting.png`, scaled to the block-out's size so that pixels line up with the id map, and
   `painting.json`:

```json
{
  "size": [2048, 1180],
  "view": {"x0": 0, "y0": 0, "x1": 48, "y1": 40, "tw": 42.67, "th": 21.33, "top": 160},
  "blockout": "blockout.png", "ids": "blockout-ids.png", "legend": "blockout.legend.json",
  "provider": "bfl:flux-3-image", "prompt": "…", "seconds": 41.2
}
```

The view is the block-out's own mapping: cell (x, y) at height `up` lands at pixel

```
px = (x - x0 - (y - y0)) * tw/2 + (y1 - y0) * tw/2
py = (x - x0 + y - y0) * th/2 + top - up
```

Everything in Mark and Build converts through it: cells to pixels, and pixels on the ground back to cells.

## 3. Marks

Stored as `places/<id>/marks.json`, with masks under `places/<id>/marks/`:

```json
{
  "version": 1,
  "painting": "painting.png",
  "paintedAt": "2026-10-27T14:02:11",
  "walk": {"x0": 0, "y0": 0, "w": 48, "h": 40, "rows": ["....##..", "…"]},
  "occluders": [
    {"id": "o1", "name": "house 3", "object": 12, "mask": "marks/o1.png", "box": [410, 220, 128, 140],
     "base": [[420, 352], [530, 352]], "prefilled": true, "edited": true}
  ]
}
```

- **walk:** one character per site cell, `.` walkable and `#` blocked.
- **occluder:**
  - `mask`: an alpha PNG the size of `box` (`[x, y, w, h]` in painting pixels), where white means "part of it";
  - `base`: a polyline in painting pixels where it meets the ground (two or more points, left to right);
  - `object`: the index of the plan object it came from, or absent when the user drew it.
- **stale:** `paintedAt` differs from the painting's time. The step says so. Walkable cells and base lines stay, since
  they follow the plan; the regions are shown for checking.

### Pre-fill (`PrefillMarks`)

- **walk:** blocked where the plan's cell is solid (`_solid`: rock, wall, void, water without a bridge) or covered by
  a thing that blocks (the role's `blocks`, buildings, trees' trunks); walkable elsewhere.
- **occluders:** one for each object whose role stands tall (`tall` × `size` ≥ 1 cell, or a building, tree, wall or
  tower):
  - the region is the object's pixels in `blockout-ids.png`, dilated by 2 px, since the painting spills a little past
    the block-out;
  - the base line is along the front edges of its footprint, the two edges that face the camera (x1 and y1). In
    pixels it's the polyline (x0, y1) → (x1, y1) → (x1, y0) projected at height 0, which is a "V" for a box;
  - a wall run (fences, walls) gets one occluder per run, with its base along the run.
- Everything pre-filled is marked `prefilled`. Trimming it sets `edited`.

### The editor (Build ⑤ Mark)

- The painting fills the canvas, with zoom on the wheel and pan with Space+drag or the middle button.
- Layers: **Walkable** and **Occluders**, each with its opacity, and one shown at a time.
- Walkable tools: the brush (cells, sizes 1, 2 and 4), the rectangle and fill. Left-click makes cells blocked;
  right-click or Alt makes them walkable.
- Occluder tools:
  - **Brush** and **Eraser** (size from the wheel with Ctrl, or [ and ]) on the selected occluder;
  - **Base**: drag its handles, or click to add a point;
  - **Select** (26.4): click to add the object under the cursor, Alt+click to take it away;
  - **New**: an empty occluder.
- The occluder list shows name, kind, edited and hidden, with delete.
- Undo and redo (Ctrl+Z, Ctrl+Y), and Save (Ctrl+S, or autosave when the step is left).

## 4. Click-to-select (26.4)

`POST /images/segment {path, points: [[x, y, 1|0]], box?}` returns `{png (base64, white inside), box}`
(`app/segment.py`). It uses SAM 2.1 tiny (`facebook/sam2.1-hiera-tiny` through transformers), on the CPU, in the AI
service's optional `segment` extra (`uv sync --extra segment`: torch, torchvision, transformers). The model is
downloaded on first use; the painting's image embedding is computed once and kept by path and modification time
(measured: the first click about 2 s after the download, then 0.2 s). Mark's **Click-select** tool (S) adds the
result to the selected occluder, or takes it away with Alt (`build:segment`, `build:segmented`).

## 5. The scene (Build ⑦, `BuildPaintedScene`)

1. **The painting** is laid as sprites at depth −1e7, split into chunks of at most 2048 px. It's placed so that the
   view's cell (x0, y0) lands on the site's origin cell, at the scale that makes `tw` equal the project's tile width.
2. **Walk:** a tile layer `walk` with invisible cells, solid where the marks say blocked. Pathing and collision read
   it as they read any layer.
3. **Occluders:** each is cut from the painting by its mask into `places/<id>/occluders/<id>.png`. It's drawn as
   **strips** of about half a tile wide (UV windows of that one picture). Each strip is placed at its screen position,
   with depth from the base line under the strip's centre: the base point is turned into a world point and its depth is
   worldX + worldY, as for any sprite (`ProjectIso`). A character whose feet are behind the base (a smaller x + y) is
   drawn before the strip and so goes under it. In front, it's drawn after.

   A strip covers only the rows above the base line. Mask pixels below it are dropped, since they're ground in front
   of the thing.
4. **The things from Assets** are placed as in a kit build: doors, chests, items, characters.
5. **The check** (`/places/inspect`) runs as before, on a render of the scene.

## 6. AI panel

- The build card shows Paint (painted, provider, time) and Mark (walkable cells, occluders, edited count, stale).
- Context `place.marks`.
- Ops:
  - `place.paint {notes}`: paint again;
  - `place.prefillMarks`: pre-fill again; a region that has been edited is kept unless asked;
  - `place.marks`: read.
- The prompt says marking is the user's step: the AI pre-fills and checks (blocked cells under buildings, an occluder
  for each tall thing) and never says the marks are done.
