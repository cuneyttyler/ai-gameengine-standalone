# Build fixes: making places look hand-crafted

Started 2026-10-24. The goal: when the project Test2 is opened, every place the AI Planner built
looks like a place from a shipped, hand-crafted game. The method is the one that turned the
fishing village from a flat board into something close to Age of Empires II
(`docs/testing/build-test.md`, `tests/quality/assets-before` → `assets-after4`): a clear goal per kind
of place, a short list of things to develop, and each one improved in turn, judged run against run.

## 1. What matters most (every kind of place)

1. **Assets are coherent with the scene and fit the place they stand in** (style, palette, scale,
   era, mood, the story).
2. **Each asset's placement makes sense**, on its own (a bed against a wall, a door in a wall) and
   against the others near it (chairs round a table, crates together by a storeroom).
3. **Layout and Plan work**: no zone nobody can reach, no rooms floating apart, a heart where a
   place needs one.

## 2. Every step checked by a vision model

The vision model is Mistral's best (`mistral-large-4`, the service's default vision model; it
falls back to `mistral-medium-latest` when Large is at capacity). Each check (`POST /places/inspect`,
`ai_service/app/inspect.py`) does the same four things:

1. **Identify** the elements in the image: the zones of a layout; each kind of thing in a plan or a
   build.
2. **Placement**: each element's position in the whole place and against the elements around it.
3. **Fit**: its visual fit with the whole scene and with what stands near it.
4. **Quality**: how it looks, and whether it is what the story describes.

It answers with what it identified, a verdict per element (placement, fit, quality, each with why),
what's missing, a list of problems (error or warning, each with a fix) and four scores out of 10
(layout, placement, fit, quality), judged "as a strict art director: 10 is a place from a shipped,
hand-crafted game". Every request carries the kind of place's **look** (its reference games and
its goals, `data/places/buildtypes.json`), the place's **story** (the Planner's purpose, mood, scenes,
who is there) and its **Brief**.

| Step | What the model sees | What the build does with it |
|---|---|---|
| ② Layout | the zones alone over the ground, drawn from the solved layout (`inspect-layout.png`) | errors go back as a revision of the zones; the layout is checked again (two revisions at most). `PlaceBuild::CheckLayout`; at ② Layout, or at ③ Plan for a build without that step (the Planner's) |
| ③ Plan | the plan with every kind of thing in its own colour and code, and a legend (`inspect-plan.png`) | the plan review (`/places/review-layout`) works from this map: it names each kind by its code, judges where it stands, alone and beside the others, against the look and story, and proposes changes the rules allow |
| ⑤ Build | the scene as the game draws it, in windows of at most 56 cells a side (`inspect-build-<n>.png`), each with what was placed in it | art judged not to fit, or poor, is refused for its need and found or made again (made art: its prompt rewritten with why); the place is built again, once. `PlaceBuild::InspectBuilt`, then the editor (or the harness) sources and builds again |

The map images are schematic: both prompts say the colours, codes, outlines and legend are the
map's notation, so the model judges the arrangement, not the drawing (in the first runs it took
the plot lines for "debug overlays").

## 3. Every found asset checked against the scene

Art found in the libraries (the AIGE library and the user's) or in the project is looked at by the
vision model before it's used (`AssetSourcing::checkFound`, a pending "check" per need): the same
critic as made art (`/assets/describe` with what was wanted, its room, place and story). Refused,
it's skipped for that need (`Tried::refused`) and the next candidate is taken, down to the Art tool
when no found art is left; approved, it isn't looked at again in that build (`Tried::approved`).
Both are kept in the build's session.

Measured whatever the model says: a thing that stands in the scene must be cut out (not on a box of
background: `AnalyzeShape(...).edgesOpaque`) and one thing (not a sheet: five or more separate
pieces, `SegmentObjects`).

## 4. The goal per kind of place

Each kind's look (`buildtypes.json` → `look.reference`, `look.goals`) is what its checks judge
against. These are the development items, kind by kind.

**Settlement** (Age of Empires II, Anno 1404, Stronghold): one continuous painted ground; a clear
heart (market square or well plaza on the main road), houses fronting the streets; one
architectural style and pixel density, buildings sized by purpose; dense varied woods at the
edges, soft river banks; props in purposeful groups; fenced plots, gardens, fields with furrows;
unique things once, at a meaningful spot.

**Dungeon** (Diablo II, Baldur's Gate, Darkest Dungeon): every room connected, a route from the
entrance to the boss with side rooms and loops; rooms varied by purpose; thick continuous walls,
doors in them, solid rock outside; immersive lighting (dark, warm pools of torchlight falling off
into shadow); props against walls and in corners, a focal piece per room, the middle walkable;
varied floors; items with a story (a key near its door, a lever by its gate).

**Interior** (Baldur's Gate, Divinity: Original Sin, Pillars of Eternity): rooms sharing walls with
doors between them; each room furnished for its purpose; furniture against walls, tables in the
middle with chairs round them, rugs under tables, walkways free; warm light from hearth and
candles; one continuous floor, whole walls with corners; one furniture style.

**Courtyard**: enclosed, entered by a gate; paved paths from the gate to each door; a centrepiece in
the middle; buildings facing in, planters and benches along the walls.

**Castle**: curtain walls with corner towers and a gatehouse; the keep at the heart; the bailey's
buildings against the walls; a moat or ditch; one stone style.

**Camp**: tents round a central fire; supplies stacked together; a boundary with an entrance;
trampled ground; racks, cooking spot and bedrolls where people would use them.

**Farmland**: large fields of one crop each with furrows; a farmstead on a track; fences and hedges
on field edges; orchards in rows.

**Wilderness / region**: dense natural forests with clearings; soft shores; winding paths;
landmarks where paths meet; nothing on a grid.

## 5. How the runs are made

`tests/live_build_test.cpp`, `LiveBuildTest.APlannerPlaceBuild`:

    AIGE_LIVE_PLANNER="<project>|<place>|<out dir>|<port>"

builds one place of a project's Planner as the Planner builds it (its Brief and story, the Brief's
art strategy, the AIGE and user libraries, the checks above), in a copy of the project without its
places, scenes and painted ground (`tests/quality/t2/base-project`: Test2 without `.cache`, which is
370 MB of cooked assets). Every step's image, the checks' answers and `scores.json` go into the out
dir. One AI service per run: the service holds one open project, so parallel runs on one service
look in each other's projects (the first baseline's checks failed that way).
`LiveBuildTest.RenderTheBuiltPlaces` (`AIGE_RENDER_SCENES=<project>|<out>`) draws a project's built
places from their saved scenes.

## 6. Runs

### Baseline (r0): Test2 as it was built, and the same places rebuilt with the checks only measuring

Test2's built places as they were (`tests/quality/t2/baseline/`), by eye:

- **Ashen Vale** (settlement): sprite sheets on white boxes standing as props, placeholder boxes,
  the town's sign twice and huge, roads in a grid over the whole map.
- **Dungeon of Whispers**: thin fence-like walls, tiny props in a sickly tint, keys scattered
  everywhere.
- **Enforcer's Office** (interior): three rooms apart from each other, wall pieces scattered like
  fence bits, doors standing in the middle of rooms.

Rebuilt (r0, `tests/quality/t2/<kind>-r0/`), scores out of 10:

| Place | Plan: layout / placement / fit / quality | Build: layout / placement / fit / quality | errors (plan, build) |
|---|---|---|---|
| Settlement (Dungeon of Whispers Depths) | 3 / 2 / 5 / 2 | 1.75 / 1.75 / 0.75 / 2.75 | 4, 18 |
| Dungeon (Dungeon of Whispers) | 4 / 2 / 6 / 3 | 4.25 / 4.5 / 5.75 / 4.75 | 4, 11 |

What the checks found (and what was seen by eye):

- Settlement: trees in a perfect grid in the forest zone; no town heart; buildings not facing the
  streets; props scattered at random; "shacks" that are crates and warehouses that are modern
  houses on white boxes (all from the user's library, used without a look); a bright green and pink
  palette in a "cursed, oppressive" town; the mud ground a flat placeholder (a harness bug: see 7).
- Dungeon: lights in the middle of rooms; debris and rocks in the middle of rooms; one
  sarcophagus floating in its crypt; the floor's tile grid showing; the rock between rooms drawn
  as a tiled floor, its grid the most visible thing on screen; rooms all rectangles.

### r1: checks acting, found art checked, rooms and wild scatter by role, dark rock

| Place | Layout check (rounds) | Build: layout / placement / fit / quality | errors (build) | found art refused |
|---|---|---|---|---|
| Settlement | 3 → 4 → 3 | 2.25 / 2.5 / 1.0 / 4.0 | 16 | 41 |
| Dungeon | 3 → 2 → 4 | 4.25 / 6.0 / 6.25 / 5.5 | 9 | — |
| Interior | 1 → 1 → 1 | 2.0 / 4.0 / 7.0 / 7.0 | 2 | 10 |

Improved:

- **Interior**: from scattered fence bits and a "GameJam 2019" banner image to real walls with
  arched doorways, dark damp art in one style and warm lantern light (fit 3 → 7, quality 2 → 7).
- **Dungeon**: the rock between rooms is darkness (no grid); rocks and debris stand in corners and
  along walls, the middles walkable (placement 4.5 → 6.0).
- **Found art**: the check refused what didn't fit, with reasons a person would give: the inn that
  was the "Ashen Vale" sign, a "bush" that was a tree, a clean, cheerful stall, stable and shop in a
  ruined, thorn-choked town, three-quarter views in an isometric scene.

Not improved, and why:

- **Layout revisions changed nothing.** At ③ Plan (a build without ② Layout, as the Planner's are)
  the zones are frozen for a revision, so every move the check asked for was dropped ("Where the
  zones lie is changed in ① Confirm → Layout, so the swap or move was left out"). Fixed: the layout
  check's own revision may move zones.
- **The interior's layout check was blind**: its map dropped the walls (lines) and labelled only top
  level zones, so an interior read as two bare floors; and the check counted missing furniture
  (placed in the next step) and the floors drawn side by side as errors. Fixed: walls kept, zones
  and rooms labelled, the module of each zone described, and the prompt says what a layout is.
- **The plan checks were cut short**: Large 4 in JSON mode ran to its 12,000-token limit after 15
  entries (no scores). Fixed: one entry per kind, short reasons, at most 12 problems, a 6,000-token
  cap, and runs of whitespace collapsed.
- **The settlement still used refused art** (the warehouses on white boxes, the bright library
  trees): a need's found candidates were checked one after another for six rounds, and what was
  still unchecked was used. Fixed: two found pieces refused for a need → the Art tool makes it.
- **Furniture still floated**: an interior's furniture is placed by the `place` op, not `scatter`,
  so the role hints didn't reach it; a hearth set's chairs have no relation at all. Fixed: in the
  `place` op too, a role's own place (`look.at`) and beside the other things of its set.
- **Made art came out cheerful** in a grim place: the art brief used the project's palette (from
  earlier, brighter builds) over the story's mood. Fixed: the mood decides palette and condition;
  the project's art only its pixel size, outline and shading.

## 7. Fixes, and why

| # | Fix | Where | For |
|---|---|---|---|
| 1 | Vision checks of layout, plan and build, acted on | `inspect.py`, `PlaceInspect`, `PlaceBuild::CheckLayout`, `InspectBuilt`, `Review` | every kind |
| 2 | Found art checked against the scene before use; refused → next candidate → Art tool | `Sourcing` (`Tried`), `PlanPlaceAssets`, `PlaceBuild` | every kind |
| 3 | Measured refusal: background boxes, sprite sheets | `PlaceBuild` critic | every kind |
| 4 | Where a thing belongs in a room, from its role (`look.at`: wall, corner, centre; `look.near`: a chair by a table) | `roles.json`, `Solver::Scatter` | dungeon, interior |
| 5 | Wild things in groves and clearings (a slow noise), not an even sprinkle; trees and bushes vary; each nudged up to a third of a cell | `Solver::Scatter`, `roles.json`, `PlaceScene` | settlement, wilderness, farmland, camp |
| 6 | Solid rock indoors is darkness: its texture flattened to a faint grain, barely lit | `PlaceScene` ground painter | dungeon |
| 7 | Harness: a new asset's catalog fields were lost (a loop over `.items()` of a temporary: it dangles), so made ground was never used; `InspectSummary` had the same bug | `live_build_test.cpp`, `PlaceInspect.cpp` | tests |
| 8 | An indoor place's wall lines stood up as its wall blocks, cut away as in Diablo: a wall with the floor on the viewer's side stands whole, one between two rooms lower, one in front of a room is left out; the interior gets the dungeon's parts (wall block, wall torch) | `PlaceScene` (lines, parts), `buildtypes.json` | interior |
| 9 | The layout check's revision may move zones even at ③ Plan (the Planner's builds have no ② Layout, and the zones were frozen) | `PlaceBuild::OnRevision` | every kind |
| 10 | The layout map keeps walls and labels zones and rooms; the prompt says the step is only zones and rooms (things come next), that floors are drawn side by side, and what each zone's module is | `PlaceInspect`, `LayoutImage` (`partNames`), `inspect.py` | interior, dungeon |
| 11 | Check answers kept short (one entry per kind, short reasons, at most 12 problems), whitespace runs collapsed; Large 4 thinks before it answers, so the token cap is 24,000 (at 6,000 it answered nothing, at 12,000 it was cut off) | `inspect.py`, `providers.py` | every kind |
| 12 | Two found pieces refused for a need → the Art tool makes it | `Sourcing::Decide` | every kind |
| 13 | The story's mood decides made art's palette and condition; the project's earlier art only its pixel size, outline and shading | `art_brief.py` | every kind |
| 14 | `place` op: a role's own place (`look.at`) when the statement gives none, and beside the other things of its set (in rooms) | `Solver::Place` | interior, dungeon |
| 15 | "search" searches the web only when the project allows it (it ignored the setting) | `PlaceScene`, `Sourcing` | every kind |
| 16 | A road on ground of much the same colour (a dirt track through mud) gets a worn, darker edge | `PlaceScene` ground painter | settlement, farmland, camp |
| 17 | What grows (grass, reeds, mushrooms) isn't coded on a check's map, drawn as tufts | `LayoutImage` | settlement, wilderness |
| 19 | Made art's file named by the place, the need and a stamp: by the need and the try alone ("tree-prop-1.png"), the next place's tree (or this place's next round) overwrote art a built scene already used | `PlaceBuild::SendArt` | every kind (the game build makes places one after another) |
| 20 | A redo that makes the place worse is undone: the art from before comes back, the redo's is refused, and the place is built once more (a redo in the interior gave oversized torches on boards and tables that looked like little houses) | `PlaceBuild::InspectBuilt` | every kind |
| 21 | A found piece refused under its copy's path came back under its library id, every round (the warehouse was refused 16 times and still used, and two refusals were never counted): verdicts are kept under both, and refusals counted apart | `Tried::refusals`, `PlaceScene`, `PlaceBuild` | every kind |
| 22 | A decal (flowers, debris) is judged lying flat: top-down or isometric both do | `PlaceBuild::WantedFor` | every kind |
| 23 | **Assets designed against the assets they go with**, as a human artist would: the ontology says what each role depends on (`look.dependsOn`: a door and a window on the wall block, a wall torch and a banner on the wall, a gate on the fence, a chair on the table); a dependent piece waits until its dependency's art is made, then is drawn with that art as the image model's first reference and its description and prompt in the brief ("goesWith"), and the brief says how it meets it | `roles.json`, `PlaceBuild` (`DependsOn`, `GoesWith`, `SendWaiting`), `/assets/generate` (`withImages`), `art_brief.py` | every kind (dungeon doors first) |
| 24 | **The ontology in every check**: each zone with its module's description and placement rules (its relations); each kind of thing with what the role is, how it's drawn, where it belongs (`at`, `near`), what it depends on, and the set it's part of with that set's rules; the vision model judges placement against these definitions | `PlaceInspect` (`KindFacts`, `RulesOf`), `inspect.py` | every kind |
| 25 | Scores per item: every placement, fit and quality verdict has a score out of 10; the build check's windows carry each kind's cells from the layout (the PLL), so what the model sees is matched to what was placed | `inspect.py`, `PlaceInspect` | every kind |
| 26 | **Each level its own scene**: a place with two or more zones of the ontology's class Level (a dungeon's levels, a building's floors) is saved as one scene per level, the first at the place's scene and the others beside it (`<scene>-<level>.aige`); stairs lead down to the next level and back up (a `Gateway` on each, arriving at a marker by the other level's stairs). The build check looks at each level in its own windows, and the build test draws each level apart | `PlaceLevelAreas`, `GameBuild::FinishInEditor`, `PlaceInspect`, `live_build_test.cpp` | dungeon, interior |
| 27 | The scores of every run, step and item, written by `tools/build_scores.py` into `docs/build-scores.md` | `tools/build_scores.py` | tests |
| 28 | **The place sketched first, as a concept artist would**: after the plan, its block-out (every thing a plain block at its footprint, the ground in its colours, in the game's view) is drawn window by window at 400 px (the most pixflux paints) and painted over by PixelLab (`init_image`, its composition kept) from a prompt the text model writes out of the plan (the rooms, what's in them with the ontology's words, the look and mood); each need's box in the block-out is that thing in the painting, cropped (from the window it's biggest in); its art is then made from the crop (the first reference, "keep its look, draw it alone and cut out"), so every piece of a window comes from one painting: one palette, one light, one way of drawing | `PlaceSketch` (`BuildBlockOut`, `SketchRequests`), `RenderScenePreview` (boxes), `/assets/sketch` (`sketch.py`), `PlaceBuild::Sketch`, `AssetSourcing::sketched`, the editor (the Planner's builds: sketching → sourcing), the build test (`AIGE_SKETCH`) | every kind |
| 29 | A plain door whose walls are both cut away stood alone in the dark: left out, the doorway open (locked and secret doors stay) | `PlaceScene` | dungeon |
| 30 | Any place whose look stands its walls up (a wall part) does so for its wall lines too, outdoors every edge whole: a castle's curtain walls are tall crenellated stone (a new role, `curtain-wall`), not a fence's thin pieces | `PlaceScene`, `roles.json`, `buildtypes.json` | castle |
| 31 | What hangs on a wall (`look.hang`: a banner, a shield) goes on the wall's face, the back walls first, lifted, at a banner's size (they stood huge in the middle of rooms) | `roles.json`, `PlaceScene` | interior, dungeon, castle |
| 32 | An AI answer of an odd shape no longer throws out of the build (a revision's "expect" with `"to": 6` crashed the courtyard run): facts that aren't words are skipped, and any answer that throws is noted, the build put back where it was (a piece of art's need marked done, so sourcing ends) | `PlaceBuild::OnResponse` | every kind |
| 33 | **Harness: the art provider wasn't set**: the services the build test starts draw with Mistral's images unless told otherwise, while the editor is set to PixelLab (Preferences ▸ AI ▸ Art). Every run before r5 (and castle/courtyard r3) made its art with Mistral; the build test now sets the art provider as the editor does (PixelLab unless `AIGE_ART` says). The sketch (28) needs PixelLab; r4 ran without it and was stopped | `live_build_test.cpp` | tests |
| 34 | A colour grade per place from its mood (the Brief's style, the story's mood): a cursed, ruined or oppressive place muted, darker and cool, the ground painted through it and every thing tinted by it, as a game grades a level as one picture (the settlement was bright green and pink in a "cursed, blackened" town) | `SetMoodGrade`, `PlaceScene` (painter, sprites), editor, build test | every kind |
| 18 | Harness: `request:<words>` builds a kind the Planner hasn't made; `inplace` builds in the project itself and takes the place into its game build (`GameBuild::InstallBuilt`) | `live_build_test.cpp`, `GameBuild` | tests, Test2 |

(The results of each run go into §6 as they come.)
