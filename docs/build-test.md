# Build quality check: one request, every step, before and after

Date: 2026-10-21. The ideas behind the changes are in `docs/place-refine.md`; this file is the
test: how it was run, what each step produced, and what was measured.

## 1. How it was run

The request: **"a fishing village by a river with a market, cottages, gardens and farm fields"**.

`tests/live_build_test.cpp` runs the Build panel's steps against the real AI service (Mistral
for text and images), with no editor:

| Mode | Environment | What it does |
|---|---|---|
| Whole build | `AIGE_LIVE_BUILD="<request>\|<out>\|<port>"` | ① Confirm, ② Layout, ③ Plan, ④ Assets, ⑤ Result, into `<out>` |
| From a saved plan | `+ AIGE_LIVE_FROM="<project>\|<place>"` | ④ and ⑤ only, on that plan: two versions of the asset steps judged on **one layout** |
| Re-solved | `+ AIGE_LIVE_RESOLVE=1` | the saved layout plus what the compiler adds now (the woods) |
| Replay | `AIGE_REPLAY_ASSETS="<project>\|<place>"` | ⑤ again from a project's art, no AI (about 30 s): for tuning the look |

Requests go four at a time, as the editor sends them (the first version sent one at a time, which
is why it took 20 minutes). A whole build takes about 7 minutes to the plan and 3 to 4 more for
the art.

The baseline is the same code with the asset and ground changes taken out (two bug fixes kept,
so it could get past ③), run against an AI service with the original image prompts.

## 2. The steps

**① Confirm and ② Layout** (`tests/quality/after/1-confirm-brief.json`, `2-layout.png`): sound.
The market ward, docks and residential ward sit on the river and the bridge carries the main
road. Two bugs were found:

- the Brief listed its features as strings (`["river"]`); the validator only took
  `{"module": ...}` and ③ stopped with "unknown feature ''". Fixed: both forms are read.
- the plan's "north road" and "south road" were routed west to east, on top of the main road,
  so they never appeared. Fixed: a branch road runs from its side of the site to the middle.

Still open: the layout reports relations it didn't meet ("residential-ward isn't adjacent
commons") and doesn't try again.

**③ Plan** (`after/3-plan.png`): parcels placed, 28 doors reachable, no overlaps.

**④ Assets** and **⑤ Result**: the subject of the rest of this file.

## 3. Runs on the same plan

| Run | What changed | Folder |
|---|---|---|
| before | baseline | `tests/quality/assets-before/` |
| after | role size, height and words; one art direction; view check | `assets-after/` |
| after2 | water generated; tile flattening; no-base rule; crop and mound words | `assets-after2/` |
| after3 | woods; fence pieces made and measured; 256 px tiles | `assets-after3/` |
| after4 | painted ground, shadows, smooth shapes, hills, palette lock, fence critic | `assets-after4/` |

Each folder has `4-assets.json` (every need and what it got), `4-art/` (every image),
`5-scene.png` (the place), `5-scene-detail.png` (a close window) and `5-verify.json`; replays add
`replay-scene.png` and `replay-detail.png`. Side by side: `tests/quality/compare-scene.png` and
`compare-scene-detail.png`.

## 4. Measured

### Size: each prop's width against its role's size (more than 40% off is wrong)

| | before | after (all runs) |
|---|---|---|
| props and decals off by > 40% | **12 of 19** | **0 of 24** |
| worst | wheat crop at 10 cells (16.7×) | none |

The baseline's crop decal (a field in a box) was read as 10 cells across, so a few giant copies
covered the farmland (the green blocks in `compare-scene.png`). Note the after figure holds by
construction (the role's size is written into the catalog); the visual effect is the evidence.

### What was drawn against what was asked (checked by eye on `4-art/`)

| Need | before | after4 |
|---|---|---|
| lamp | a desk lamp | a lantern on a post |
| sign | a door | a wooden signpost |
| farmhouse | a whole farm diorama | a farmhouse |
| cottage | modern bungalows | thatched timber cottages |
| tilled ground | a stone floor | furrowed soil |
| trees, rocks, bushes | each on its own cube of ground | standing alone |
| roofed well | white background left in | cut out |
| water ground | not made (a flat blue placeholder) | made |
| fence section | not made (grey bars, all one way) | one straight diagonal run |

### Ground tiles: brightness difference between a tile's edges and its middle (shows as a grid)

| Tile | before | after (prompt change only) | after4 (flattened) |
|---|---|---|---|
| grass | 2.3 | **18.9** | 0.9 |
| dirt | 0.4 | 6.6 | 0.1 |
| tilled | 2.9 | 14.2 | 0.2 |
| cobble | 4.2 | 10.5 | 1.4 |
| water | — | — | 0.2 |

The first prompt change ("low contrast, soft even lighting") made tiles **worse**: the image
model framed them. It was reverted, and deterministic post-processing (`flatten_tile`: trim the
frame, take out the slow change of light) brought every seam under 1.5.

### Fence sections, measured by their shape (`PieceKind`)

| Run | take 1 | take 2 | take 3 | used |
|---|---|---|---|---|
| after2 | straight-y (elongation 5.6) | | | yes |
| after3 | cross (1.9): a little box | | | yes (no check yet) |
| after4 | cross (1.5) rejected | t-e (1.4) rejected | straight-y (4.6) | yes |

### Counts

| Run | art step (s) | pieces | placeholders | objects placed |
|---|---|---|---|---|
| before | 138 | 33 | 1 | 733 |
| after | 217 | 33 | 2 | 1027 |
| after2 | 207 | 34 | 1 | 1027 |
| after3 | 175 | 35 | 1 | 1425 (woods) |
| after4 | 196 | 35 | 1 | 1131 |

The one placeholder left in every run is the commons pond: the layout asks for it as an object
of role `water-terrain`, where it should be water painted into the ground (open).

## 5. Judged against a 90s strategy game

**Before**: a flat board. One tile repeated in one turn on every cell, so the grid shows; hard
stair-stepped edges between grounds; props each on their own block of ground, in different
palettes; half the farmland covered by giant pixelated green blocks; the river a flat blue
ribbon; fences missing.

**After4**: reads as one place. The ground is painted as one picture: grounds blend in ragged
edges, the river bends between sand banks, roads soften into the grass, fields and the square
keep straight edges. Woods ring the edges of the site, dense and varied (each tree mirrored and
sized a little differently), on a forest floor. Things cast soft shadows to the lower right. The
wild land rolls in low hills, shaded, with trees standing on them. Every ground has the same
muted palette whatever the image model drew.

Still short of Age of Empires II:

- the generated grass is busier than AoE2's (its texture comes from the image model; the
  palette lock fixes the colour, not the detail);
- the water doesn't move, and has no foam at the shore;
- fences are thin and broken by their gates; no posts at the corners;
- buildings come in mixed styles (stone and slate beside thatch) and sometimes at mixed pixel
  densities;
- the pond is a placeholder; hills don't yet show cliffs at the map's edge.

## 6. Tests

`aige_tests`: 353 pass (the tests of the deleted demo project were removed). `ai_service`: 102
pass. New: `test_a_framed_ground_tile_gets_even_light` (a framed tile's seam falls below 6).
The editor builds; the height brush and the new Brief fields haven't been tried in the editor
yet.
