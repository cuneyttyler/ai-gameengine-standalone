# Build scores

Written by `tools/build_scores.py` from the build test's runs (`tests/quality/t2/`); how they're made and
what each fix was for: `docs/build-fixes.md`. Every score is the vision model's (Mistral Large 4), out of 10,
judged as a strict art director (10: a shipped, hand-crafted game). Its scores vary by a point or two between
looks at the same image; read trends across runs, not single numbers.

## Run against run

### castle

| Run | ② Layout | ③ Plan | ⑤ Build | ⑤ Build after the redo | ⑤ Build, redo undone |
|---|---|---|---|---|---|
| castle-r1 | 4 / 4 / 3 / 4 (3 err) | 2 / 3 / 2 / 2 (5 err) | 1.5 / 2.5 / 2.25 / 4 (21 err) | 1.75 / 2.75 / 2.5 / 4.25 (17 err) | – |

### dungeon

| Run | ② Layout | ③ Plan | ⑤ Build | ⑤ Build after the redo | ⑤ Build, redo undone |
|---|---|---|---|---|---|
| dungeon-r0 | – | 4 / 2 / 6 / 3 (4 err) | 4.25 / 4.5 / 5.75 / 4.75 (11 err) | – | – |
| dungeon-r2 | 5 / 7 / 7 / 5 (0 err) | 4 / 5 / 6 / 4 (2 err) | 3 / 4 / 6 / 5 (3 err) | 4 / 7 / 8 / 7 (3 err) | – |
| dungeon-r3 | 6 / 7 / 6 / 5 (0 err) | 4 / 5 / 6 / 4 (2 err) | 4.5 / 6.25 / 7.25 / 6.75 (7 err) | 4.75 / 6 / 7 / 6.75 (5 err) | – |

### interior

| Run | ② Layout | ③ Plan | ⑤ Build | ⑤ Build after the redo | ⑤ Build, redo undone |
|---|---|---|---|---|---|
| interior-r0 | – | – / – / – / – (0 err) | 2 / 2 / 3 / 2 (4 err) | – | – |
| interior-r1 | 1 / 0 / 0 / 0 (6 err) | – / – / – / – (0 err) | 2 / 4 / 7 / 7 (2 err) | – | – |
| interior-r2 | 4 / 3 / 5 / 4 (2 err) | 5 / 4 / 5 / 4 (3 err) | 5 / 6 / 7 / 7 (2 err) | 2 / 3 / 5 / 6 (2 err) | – |
| interior-r3 | 5 / 6 / 7 / 4 (1 err) | 4 / 5 / 5 / 4 (3 err) | 4 / 4.5 / 6 / 5 (6 err) | 4.5 / 5 / 6.5 / 6.5 (4 err) | – |

### settlement

| Run | ② Layout | ③ Plan | ⑤ Build | ⑤ Build after the redo | ⑤ Build, redo undone |
|---|---|---|---|---|---|
| settlement-r0 | – | 3 / 2 / 5 / 2 (4 err) | 1.75 / 1.75 / 0.75 / 2.75 (18 err) | – | – |
| settlement-r2 | 4 / 3 / 6 / 4 (3 err) | 3 / 3 / 5 / 2 (4 err) | 2.25 / 2 / 1.75 / 3.5 (15 err) | 2 / 2.5 / 2 / 3.5 (16 err) | – |
| settlement-r3 | 4 / 5 / 5 / 4 (2 err) | 4 / 3 / 5 / 3 (3 err) | 3.5 / 3.25 / 1.5 / 3.75 (17 err) | 4 / 4.75 / 2.25 / 4.75 (16 err) | – |

## Each run

### castle-r1

Place: `request:a small stone castle with curtain walls, four corner towers, a gatehouse, a keep, and stables and a smithy in the bailey, dark fantasy` (castle). Art: 22 pieces, 1 placeholders, 12 found pieces refused by the check.

| Step | layout / placement / fit / quality | errors | warnings |
|---|---|---|---|
| ② Layout | 4 / 4 / 3 / 4 | 3 | 2 |
| ③ Plan | 2 / 3 / 2 / 2 | 5 | 7 |
| ⑤ Build | 1.5 / 2.5 / 2.25 / 4 | 21 | 17 |
| ⑤ Build after the redo | 1.75 / 2.75 / 2.5 / 4.25 | 17 | 18 |

Layout check, round by round: 1: 4 / 5 / 4 / 4 (4 errors); 2: 3 / 2 / 3 / 4 (3 errors); 3: 4 / 4 / 3 / 4 (3 errors)

Images: [2-layout-1](../tests/quality/t2/castle-r1/2-layout-1.png) · [2-layout-2](../tests/quality/t2/castle-r1/2-layout-2.png) · [2-layout-3](../tests/quality/t2/castle-r1/2-layout-3.png) · [2-layout](../tests/quality/t2/castle-r1/2-layout.png) · [3-plan](../tests/quality/t2/castle-r1/3-plan.png) · [5-build-1](../tests/quality/t2/castle-r1/5-build-1.png) · [5-build-2](../tests/quality/t2/castle-r1/5-build-2.png) · [5-build-3](../tests/quality/t2/castle-r1/5-build-3.png) · [5-build-4](../tests/quality/t2/castle-r1/5-build-4.png) · [5-scene-detail](../tests/quality/t2/castle-r1/5-scene-detail.png) · [5-scene](../tests/quality/t2/castle-r1/5-scene.png) · [6-build-1](../tests/quality/t2/castle-r1/6-build-1.png) · [6-build-2](../tests/quality/t2/castle-r1/6-build-2.png) · [6-build-3](../tests/quality/t2/castle-r1/6-build-3.png) · [6-build-4](../tests/quality/t2/castle-r1/6-build-4.png) · [6-scene-detail](../tests/quality/t2/castle-r1/6-scene-detail.png) · [6-scene](../tests/quality/t2/castle-r1/6-scene.png)

![castle-r1 build](../tests/quality/t2/castle-r1/5-scene.png)

#### ② Layout check, round 1

Scores (layout / placement / fit / quality): 4 / 5 / 4 / 4. This is a generic walled enclosure that lacks the concentric organization and specific defensive architecture of a proper medieval castle. The keep must be moved to the center, and the gatehouse, corner towers, and service buildings (stables, barracks, smithy) must be added to match the brief.

- *error* — keep: Keep is placed at the edge of the bailey instead of the heart
- *error* — curtain-wall: No gatehouse where the road enters the castle
- *error* — curtain-wall: Missing four corner towers
- *error* — bailey: Missing stables, barracks, and smithy buildings
- *warning* — layout: Layout is not concentric as specified in the brief
- *warning* — bailey: Internal plot boundaries are chaotic and don't define clear functional areas

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| bailey | – | 1 | 6/10 ok | 4/10 **no** | 4/10 **no** | Missing stables, barracks, smithy against walls as brief requires |
| curtain-wall | – | 1 | 5/10 **no** | 5/10 **no** | 5/10 **no** | Wall exists but lacks corner towers and gatehouse |
| keep | – | 1 | 3/10 **no** | 4/10 **no** | 5/10 ok | Placed at upper edge instead of the heart of the bailey |
| moat | – | 1 | 9/10 ok | 9/10 ok | 8/10 ok | Properly surrounds castle as defensive perimeter |
| plot boundaries | – | – | – | – | – |  |
| road | – | 1 | 6/10 ok | 6/10 ok | 6/10 ok | Connects through castle but lacks gatehouse structure |
| well | – | 1 | 8/10 ok | 8/10 ok | 7/10 ok | Central location in courtyard is appropriate |

#### ② Layout check, round 2

Scores (layout / placement / fit / quality): 3 / 2 / 3 / 4. This is a basic rectangular enclosure with a moat but lacks the defining features of a medieval castle: corner towers, a gatehouse, and a central keep. The most important fix is to add the four corner towers and gatehouse, and relocate the keep to the center to match the concentric layout brief.

- *error* — curtain wall: No corner towers or gatehouse as required by brief
- *error* — keep: Keep is in corner, not at heart of castle
- *error* — bailey: Missing stables, barracks, and smithy buildings
- *warning* — road: Road passes through castle instead of leading to gatehouse
- *warning* — layout: Not concentric as specified; keep should be central

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| bailey | – | 1 | 6/10 ok | 4/10 **no** | 4/10 **no** | Missing stables, barracks, smithy required by brief |
| curtain wall | – | 1 | 3/10 **no** | 2/10 **no** | 3/10 **no** | Present but missing corner towers and gatehouse |
| keep | – | 1 | 2/10 **no** | 3/10 **no** | 5/10 **no** | In upper left corner, not at heart as brief requires |
| moat | – | 1 | 9/10 ok | 9/10 ok | 8/10 ok | Surrounds castle properly with bridges at road crossings |
| road | – | 1 | 4/10 **no** | 4/10 **no** | 4/10 **no** | Passes through castle instead of leading to gatehouse and keep |
| well | – | 1 | 8/10 ok | 8/10 ok | 7/10 ok | Central location in bailey is appropriate |

#### ② Layout check, round 3

Scores (layout / placement / fit / quality): 4 / 4 / 3 / 4. This layout captures the basic moat-wall-bailey structure but fails the brief's core requirements: no corner towers, no gatehouse, and the keep is misplaced in a corner rather than at the heart. The most critical fix is adding the four corner towers and gatehouse to the curtain wall, then moving the keep to the center.

- *error* — curtain-wall: No corner towers at the four corners as required by the brief
- *error* — curtain-wall: No gatehouse where the road enters the castle
- *error* — keep: Keep is in the corner instead of at the heart of the castle
- *warning* — bailey: Internal walls form a confusing maze instead of clear building plots
- *warning* — road: Road enters from sides but has no gatehouse terminus

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| bailey | – | 1 | 6/10 ok | 5/10 **no** | 4/10 **no** | should contain stables, barracks and smithy but only has keep and well |
| curtain-wall | – | 1 | 5/10 **no** | 4/10 **no** | 5/10 **no** | encloses the bailey but lacks corner towers and gatehouse |
| keep | – | 1 | 3/10 **no** | 3/10 **no** | 5/10 ok | placed in corner instead of at the heart/center of the castle |
| moat | – | 1 | 9/10 ok | 9/10 ok | 8/10 ok | correctly surrounds the curtain wall on all sides |
| road | – | 1 | 5/10 **no** | 7/10 ok | 7/10 ok | runs through but doesn't terminate at a gatehouse structure |
| well | – | 1 | 8/10 ok | 8/10 ok | 6/10 ok | central location in bailey is appropriate for a well |

#### ③ Plan check

Scores (layout / placement / fit / quality): 2 / 3 / 2 / 2. This is a random field with scattered buildings and broken walls, not a functional castle. The most important fix is to replace the irregular walls with a proper curtain wall connecting the four corner towers, clear the bailey of trees to create a courtyard, and place a keep at the center.

- *error* — wall: Walls are irregular maze-like paths, not a defensive curtain wall connecting towers.
- *error* — layout: No keep at the heart; buildings scattered randomly without hierarchy.
- *error* — building: Seven buildings scattered with no relation to walls or each other.
- *error* — tree: 792 trees inside bailey obstruct courtyard and movement.
- *error* — gatehouse: Gatehouse not integrated into continuous wall; sits in broken gap.
- *warning* — bush: 320 bushes clutter the bailey courtyard.
- *warning* — bench: Single bench with no table is nonsensical.
- *warning* — rock: 38 rocks scattered randomly like noise.
- *warning* — cobble path: Maze-like paths don't connect gate to building doors.
- *warning* — mound: 13 mounds scattered without purpose.
- *warning* — flower: Flowers scattered randomly instead of in gardens.
- *warning* — grass: Bailey covered in grass instead of paved courtyard.

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| bailey | – | 1 | 2/10 **no** | 2/10 **no** | 2/10 **no** | Full of trees and bushes instead of clear courtyard. |
| bench | 1 | 1 | 2/10 **no** | 3/10 **no** | 2/10 **no** | Single bench with no table; benches belong near tables. |
| bridge | – | 2 | 6/10 ok | 4/10 **no** | 5/10 ok | Bridge through castle is odd; should be at gate only. |
| building | 7 | 7 | 2/10 **no** | 2/10 **no** | 2/10 **no** | Scattered randomly with no relation to walls or keep. |
| bush | 320 | 320 | 2/10 **no** | 2/10 **no** | 2/10 **no** | 320 bushes clutter the bailey courtyard. |
| cobble path | – | 1 | 2/10 **no** | 3/10 **no** | 2/10 **no** | Maze-like network doesn't connect gate to doors clearly. |
| flower | 367 | 367 | 3/10 **no** | 3/10 **no** | 3/10 **no** | Scattered randomly instead of in garden plots. |
| gatehouse | 1 | 1 | 4/10 **no** | 7/10 ok | 4/10 **no** | On the approach road but not integrated into a continuous wall. |
| grass | 653 | 653 | 5/10 ok | 5/10 ok | 5/10 ok | Covers ground but bailey should be paved courtyard. |
| mound | 13 | 13 | 3/10 **no** | 3/10 **no** | 2/10 **no** | Scattered randomly like noise. |
| road | – | 1 | 7/10 ok | 8/10 ok | 7/10 ok | Leads from west to gatehouse correctly. |
| rock | 38 | 38 | 2/10 **no** | 2/10 **no** | 1/10 **no** | 38 rocks scattered without purpose or grouping. |
| tower | 4 | 4 | 8/10 ok | 8/10 ok | 7/10 ok | Correctly at four corners of the site as required for a castle. |
| tree | 792 | 792 | 1/10 **no** | 1/10 **no** | 1/10 **no** | 792 trees inside bailey obstruct courtyard and paths. |
| wall | – | 1 | 1/10 **no** | 1/10 **no** | 1/10 **no** | Irregular maze-like lines, not a defensive curtain wall. |
| water | – | 1 | 9/10 ok | 9/10 ok | 8/10 ok | Correctly surrounds site as defensive moat. |
| well | 1 | 1 | 7/10 ok | 8/10 ok | 7/10 ok | Correctly centered in the bailey as a focal point. |

#### ⑤ Build check

Scores (layout / placement / fit / quality): 1.5 / 2.5 / 2.25 / 4. This is a rural landscape with scattered vegetation and placeholder buildings, not a medieval castle. The most critical fix is to add curtain walls with corner towers and replace the flat-colored houses with proper stone keep and barracks on the terrain. This reads as a generic village with a placeholder keep rather than a fortified castle, missing all defensive architecture. The critical fix is replacing the flat orange shape with a proper stone keep and adding the curtain walls with towers. This is far from a hand-crafted castle. The scene lacks curtain walls, a keep, and a gatehouse, and c…

- *error* — layout: No curtain walls enclose the castle; it's an open landscape
- *error* — keep: Keep is a flat-colored placeholder floating in black void
- *error* — barracks: Barracks is a simple house sprite floating in black void
- *error* — gatehouse: Gatehouse is isolated with no connecting walls
- *error* — tower: Tower is in the woods, not at a wall corner
- *error* — moat: River is decorative, not a defensive moat around the castle
- *warning* — layout: Buildings float on black background outside terrain
- *warning* — path: No clear paved path from gate to keep
- *warning* — ground: Ground texture appears repetitive and possibly tiled
- *warning* — fit: Art style mismatch between detailed stone gatehouse and flat tan buildings
- *error* — keep: Large flat orange rectangle is a placeholder, not a drawn building
- *error* — layout: No curtain walls enclosing the bailey

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| barracks | – | 1 | 1/10 **no** | 2/10 **no** | 2/10 **no** | Floating in black void, not against inner wall |
| bench | 1 | 1 | 6/10 ok | – | 6/10 ok | Next to well as intended, but isolated in open field |
| building | 7 | 5 | 0/10 **no** | 0/10 **no** | 0/10 **no** | Clustered on left edge, not against walls (none exist), wrong type for barracks/stables |
| bush | 320 | 320 | 5/10 ok | 7/10 ok | 7/10 ok | Scattered among trees and grass |
| dirt path | – | 4 | 3/10 **no** | 4/10 **no** | 6/10 ok | Winding path doesn't connect gatehouse to keep doors |
| floating buildings | 7 | 4 | 0/10 **no** | 0/10 **no** | 0/10 **no** | Clustered on left edge, not against walls (none exist), wrong type for barracks/stables |
| flower | 367 | 367 | 5/10 ok | 7/10 ok | 6/10 ok | Low clumps scattered on ground |
| gatehouse | 1 | 1 | 2/10 **no** | 4/10 **no** | 6/10 ok | Isolated on left with no connecting walls or road approach |
| grass | 653 | 512 | 5/10 ok | 7/10 ok | 7/10 ok | Tufts scattered across terrain |
| grass terrain | 653 | 1 | 5/10 ok | 7/10 ok | 7/10 ok | Tufts scattered across terrain |
| grass tuft | 653 | 141 | 5/10 ok | 7/10 ok | 7/10 ok | Tufts scattered across terrain |
| keep | – | 2 | 1/10 **no** | 0/10 **no** | 0/10 **no** | Floating in black void below terrain, not at heart of bailey |
| mound | 13 | 13 | 5/10 ok | 6/10 ok | 6/10 ok | Grassy humps on open ground |
| path | – | 6 | 3/10 **no** | 4/10 **no** | 6/10 ok | Winding path doesn't connect gatehouse to keep doors |
| placeholder building | 7 | 1 | 0/10 **no** | 0/10 **no** | 0/10 **no** | Clustered on left edge, not against walls (none exist), wrong type for barracks/stables |
| river | – | 4 | 3/10 **no** | 5/10 ok | 7/10 ok | Cuts through scene but not positioned as defensive moat around castle |
| rock | 38 | 38 | 5/10 ok | 7/10 ok | 7/10 ok | Scattered naturally on terrain |
| tower | 4 | 4 | 2/10 **no** | 4/10 **no** | 6/10 ok | Sits in woods near river, not at a wall corner |
| tree | 792 | 792 | 1/10 **no** | 2/10 **no** | 6/10 ok | 110 trees randomly scattered block movement and sightlines in bailey |
| wall | – | 1 | 1/10 **no** | 1/10 **no** | 0/10 **no** | Flat 2D placeholder with no isometric depth, texture, or detail |
| well | 1 | 1 | 6/10 ok | 7/10 ok | 7/10 ok | In plaza area but plaza is unpaved grass, not stone |

#### ⑤ Build check after the redo

Scores (layout / placement / fit / quality): 1.75 / 2.75 / 2.5 / 4.25. This is a procedural scatter of nature props with placeholder buildings and isolated castle pieces, not a hand-crafted fortified castle. The critical fix is to replace the placeholder buildings with stone structures and add the missing curtain walls, corner towers, and moat to create a proper concentric castle layout. This reads as a generic village in a field rather than a fortified medieval castle, primarily due to missing curtain walls and the flat placeholder building. The most critical fix is replacing the orange placeholder with proper pixel art and adding enclosing stone walls to creat…

- *error* — building: Two placeholder buildings with flat tan colors and brown roofs are not medieval stone structures.
- *error* — layout: No curtain wall encloses the castle; the tower and gatehouse are isolated in the field.
- *error* — layout: No moat or defensive ditch surrounds the castle; the river cuts through arbitrarily.
- *error* — tower: The tower floats in the middle of the site, not at a corner of a wall.
- *warning* — gatehouse: Gatehouse is at the map edge with no approach road or wall connection.
- *warning* — path: The path does not clearly connect the gate to the building doors.
- *warning* — gatehouse: Missing banners and torches on the gatehouse and walls.
- *warning* — scene: The ground has noisy texture that may show tile seams.
- *warning* — building: The keep is not at the heart of a walled inner bailey.
- *error* — building: The large orange building is a flat placeholder with no detail, texture, or pixel art
- *error* — layout: No curtain walls visible; bailey is completely open to surrounding landscape
- *error* — tower: Single tower is isolated in field, not connected to any wall

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| bench | 1 | 1 | 6/10 ok | 7/10 ok | 7/10 ok | Bench near well is a reasonable detail |
| building | 7 | 12 | 1/10 **no** | 1/10 **no** | 0/10 **no** | Two tan placeholder buildings sit in grass with no walls or courtyard; they look like modern houses, not medieval stone structures. |
| bush | 320 | 320 | 3/10 **no** | 7/10 ok | 7/10 ok | Random scatter doesn't fit a castle courtyard layout |
| flower | 367 | 367 | 3/10 **no** | 6/10 ok | 6/10 ok | Wildflowers don't fit a worn medieval military courtyard |
| gatehouse | 1 | 1 | 3/10 **no** | 6/10 ok | 7/10 ok | Isolated at the map edge with no curtain wall connecting to it and no approach road. |
| grass | 653 | 653 | 2/10 **no** | 1/10 **no** | 6/10 ok | Grass field instead of packed-earth courtyard |
| ground | – | 1 | 6/10 ok | 5/10 **no** | 6/10 ok | Grass field doesn't fit fortified courtyard which should be paved |
| mound | 13 | 13 | 5/10 ok | 6/10 ok | 6/10 ok | Small earth mounds scattered, fitting the gentle relief. |
| path | – | 7 | 4/10 **no** | 5/10 **no** | 5/10 ok | Winds through the site but does not clearly connect the gatehouse to the building doors. |
| river | – | 1 | 4/10 **no** | 4/10 **no** | 7/10 ok | Cuts through the site but does not function as a defensive moat around the castle. |
| rock | 38 | 38 | 5/10 ok | 7/10 ok | 7/10 ok | Scattered grey and pink rocks, fitting the landscape. |
| tower | 4 | 4 | 2/10 **no** | 4/10 **no** | 6/10 ok | Floating in the middle of the field, not at a corner of a curtain wall. |
| tree | 792 | 792 | 1/10 **no** | 2/10 **no** | 6/10 ok | Too many trees inside bailey; castle courtyard should be clear for defense |
| water | – | 3 | 4/10 **no** | 3/10 **no** | 6/10 ok | River runs along one side but is not a moat surrounding castle |
| well | 1 | 1 | 6/10 ok | 7/10 ok | 7/10 ok | Well in plaza is appropriate for a bailey |

### dungeon-r0

Place: `location/dungeon-of-whispers` (dungeon). Art: 23 pieces, 0 placeholders, – found pieces refused by the check.

| Step | layout / placement / fit / quality | errors | warnings |
|---|---|---|---|
| ③ Plan | 4 / 2 / 6 / 3 | 4 | 7 |
| ⑤ Build | 4.25 / 4.5 / 5.75 / 4.75 | 11 | 18 |

Images: [3-plan](../tests/quality/t2/dungeon-r0/3-plan.png) · [5-build-1](../tests/quality/t2/dungeon-r0/5-build-1.png) · [5-build-2](../tests/quality/t2/dungeon-r0/5-build-2.png) · [5-build-3](../tests/quality/t2/dungeon-r0/5-build-3.png) · [5-build-4](../tests/quality/t2/dungeon-r0/5-build-4.png) · [5-scene-detail](../tests/quality/t2/dungeon-r0/5-scene-detail.png) · [5-scene](../tests/quality/t2/dungeon-r0/5-scene.png)

![dungeon-r0 build](../tests/quality/t2/dungeon-r0/5-scene.png)

#### ③ Plan check

Scores (layout / placement / fit / quality): 4 / 2 / 6 / 3. This is clearly a procedural generation with items sprinkled randomly in room centers rather than placed by hand against walls and in corners, resulting in a cluttered, unreadable space that lacks the immersive atmosphere of a hand-crafted crypt. The most critical fix is to remove all floating lights and debris from room centers, place props against walls, arrange sarcophagi in rows, and add wall torches to create the oppressive, torch-lit mood required for the Dungeon of Whispers.

- *error* — light (I): 23 lights floating in the middle of rooms instead of mounted on walls as torches
- *error* — debris (D): 39 debris items scattered in room centers, blocking walkable paths and creating visual noise
- *error* — sarcophagus (N): Crypt room contains only 1 sarcophagus floating in the center instead of 2-3 in rows
- *error* — stairs (P): Only 1 set of stairs visible; brief requires 2 (entrance and exit)
- *warning* — layout: All rooms are rectangular boxes of similar proportions; no organic shapes or varied sizes by purpose
- *warning* — layout: No loops in the dungeon; all side rooms are dead ends, making navigation linear
- *warning* — bookshelf (B): Bookshelves floating in library center instead of against walls
- *warning* — bones (A): Bones scattered randomly in crypt center instead of in piles or rows
- *warning* — trap (R): Only 2 traps in trap room; brief specifies 4-5 for the shifting floors feature
- *warning* — table (Q): Tables floating in room centers with no chairs
- *warning* — rock (M): Rocks scattered in cave room center instead of forming walls or barriers

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| bones | 8 | 6 | **no** | ok | **no** | Scattered randomly in the middle of the crypt room floor, blocking the walkable space; should be piled in corners or arranged in rows again… |
| bookshelf | 4 | 2 | **no** | ok | **no** | Floating in the center of the library room with no wall contact; bookshelves belong against walls to create reading alcoves |
| chest | 2 | 1 | ok | ok | ok | Located in the treasure room as expected, though it could be more prominently placed in a corner or dead end |
| debris | 50 | 39 | **no** | ok | **no** | 39 instances scattered like confetti across every room center, creating visual noise and blocking paths; debris should be in corners, again… |
| desk | 1 | 1 | **no** | ok | **no** | Floating in the middle of the library with no orientation to walls; desks belong against walls with chairs |
| door | 20 | 20 | ok | ok | ok | Correctly placed at corridor junctions and room entrances, set into walls as expected |
| enemy | 7 | 8 | **no** | ok | **no** | Scattered in room centers rather than guarding doors, chests, or corridors; enemies should have tactical positions |
| key | 1 | 2 | ok | ok | ok | Placed in the entrance area near doors, which is logical for progression gating |
| light | 18 | 23 | **no** | ok | **no** | 23 lights floating in the middle of rooms like ceiling fixtures; in a crypt dungeon, lights should be wall torches on walls, not floating i… |
| locked-door | 2 | 2 | ok | ok | ok | Correctly placed at corridor junctions and room entrances, set into walls as expected |
| loot | 2 | 4 | **no** | ok | **no** | Scattered randomly in room centers; loot should be in chests, on tables, or in specific reward spots |
| pillar | 2 | 2 | ok | ok | ok | Placed in larger rooms to break up space, though could be more architecturally integrated |
| rock | 4 | 4 | **no** | ok | **no** | 4 rocks floating in the center of the cave room; rocks should form walls, barriers, or be against cave walls |
| sarcophagus | 3 | 1 | **no** | ok | **no** | Only 1 sarcophagus in the crypt room, floating in the center; a crypt should have 2-3 in rows against walls |
| secret-door | 2 | 1 | ok | ok | ok | Correctly placed at corridor junctions and room entrances, set into walls as expected |
| stairs | 2 | 1 | ok | ok | ok | Placed in a room as expected for level transition |
| table | 2 | 2 | **no** | ok | **no** | Floating in room centers with no chairs or wall orientation; tables belong against walls or with chairs around them |
| trap | 5 | 2 | **no** | ok | **no** | Only 2 traps in the trap room, placed in center; should be 4-5 and placed in corridors or pressure-plate patterns |

#### ⑤ Build check

Scores (layout / placement / fit / quality): 4.25 / 4.5 / 5.75 / 4.75. This reads as a procedurally generated grid of rectangular rooms with stamped wall torches and placeholder debris, lacking the hand-crafted variation, immersive lighting, and story-driven set dressing of a shipped dungeon. The most critical fixes are eliminating the visible floor grid seams, replacing the flat pink blocks with detailed crypt props, and adding the missing whispering mirror and spectral guardian. This is a competent but generic dungeon layout that suffers from a massive empty central void and a repetitive grid floor that breaks immersion. The individual sprites are well-crafted…

- *error* — floor-tile: Floor shows obvious grid seams and uniform tiling, breaking immersion and looking procedural
- *error* — debris: Debris appears as flat, untextured pink blocks that look like placeholder geometry rather than crypt props
- *error* — scene: Missing the whispering-mirror, a critical quest item for the scene 'The Warden's Fall'
- *warning* — wall-torch: Torches are placed at perfectly regular intervals, creating a stamped, procedural appearance
- *warning* — layout: Rooms are rectangular boxes arranged in a grid with unclear flow from entrance to deeper areas; lacks loops and varied shapes
- *warning* — lighting: Lighting is flat and uniform; lacks dramatic pools of warm light and deep shadow falloff
- *warning* — debris: Debris is scattered in room centers rather than against walls and corners, blocking walkable space
- *warning* — scene: Missing the spectral knight encounter specified in the brief
- *error* — layout: The large central chamber is a completely empty black void with no props, lighting, or architectural interest, creating a dead zone that feels unfinished
- *error* — floor: The floor displays an obvious repeating grid pattern with visible tile seams, looking like a spreadsheet rather than ancient dungeon stone
- *warning* — debris: Several debris piles are placed in the middle of walkways rather than against walls or in corners, obstructing movement
- *warning* — layout: Rooms are overly rectangular and grid-aligned, lacking the organic variation and alcoves of hand-crafted dungeons

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| bones | 8 | 8 | ok | – | – | Bones are appropriately placed on the crypt-room floor |
| bookshelf | 4 | 4 | ok | ok | ok | Bookshelves are placed against the library-room walls, fitting the room's purpose and leaving center walkable |
| chest | 2 | 2 | ok | ok | ok | Placed in the secret-room, appropriate for hidden treasure |
| chest crate | 2 | 2 | ok | ok | ok | Placed in the secret-room, appropriate for hidden treasure |
| debris | 50 | 50 | **no** | **no** | **no** | Debris is scattered randomly in room centers and open floor areas rather than against walls and in corners; blocks the walkable space and l… |
| desk | 1 | 1 | ok | ok | ok | Desk is positioned in the library-room near bookshelves, fitting the scholarly research theme |
| door | 20 | 20 | ok | ok | ok | Doors are set properly within corridor walls, connecting spaces logically |
| enemy | 7 | 7 | **no** | ok | **no** | Skeletons are positioned in open space rather than guarding the table, flanking doors, or holding corners |
| floor | – | 3 | **no** | **no** | **no** | Floor covers the area but is composed of obvious repeating tiles with visible seams, breaking the illusion of continuous stone |
| floor-tile | – | 1 | **no** | **no** | **no** | Floor covers the area but is composed of obvious repeating tiles with visible seams, breaking the illusion of continuous stone |
| key | 1 | 1 | ok | ok | ok | Placed near the rat in the guard-room, creating a risk/reward dynamic where the player must fight or sneak to get the key |
| light | 18 | 13 | ok | ok | ok | Lights are placed in guard-room and secret-room to illuminate key areas |
| locked-door | 2 | 2 | ok | ok | ok | Doors are set properly within corridor walls, connecting spaces logically |
| loot | 2 | 2 | ok | ok | ok | Loot is on the floor near the chest, logical placement for a treasure room |
| pillar | 2 | 2 | ok | ok | ok | Pillar is placed in a corridor, adding structural variety without blocking the path |
| rock | 4 | 16 | **no** | ok | **no** | Rocks are scattered in the middle of walkable room space rather than against walls or in corners, blocking movement and lacking purpose |
| sarcophagus | 3 | 3 | **no** | ok | **no** | Placed in the middle of rooms rather than against walls and in corners, blocking walkable space and violating the 'props against walls' rule |
| secret-door | 2 | 2 | ok | ok | ok | Doors are set properly within corridor walls, connecting spaces logically |
| stairs | 2 | 4 | **no** | ok | ok | Stairs appear disconnected from room flow; top stairs float in room center, bottom stairs are cut off |
| table | 2 | 2 | **no** | ok | ok | Table is floating in the exact center of the guard room with no chairs, context, or wall support |
| trap | 5 | 5 | ok | – | – | Traps are placed in the trap-room as intended |
| wall-block | – | 3 | **no** | ok | ok | Walls form rectangular rooms in a grid-like pattern rather than varied shapes; the layout lacks the organic flow of a hand-crafted dungeon … |
| wall-torch | – | 38 | **no** | ok | ok | Walls form rectangular rooms in a grid-like pattern rather than varied shapes; the layout lacks the organic flow of a hand-crafted dungeon … |

### dungeon-r2

Place: `location/dungeon-of-whispers` (dungeon). Art: 25 pieces, 0 placeholders, 20 found pieces refused by the check.

| Step | layout / placement / fit / quality | errors | warnings |
|---|---|---|---|
| ② Layout | 5 / 7 / 7 / 5 | 0 | 5 |
| ③ Plan | 4 / 5 / 6 / 4 | 2 | 8 |
| ⑤ Build | 3 / 4 / 6 / 5 | 3 | 5 |
| ⑤ Build after the redo | 4 / 7 / 8 / 7 | 3 | 5 |

Layout check, round by round: 1: 4 / 4 / 5 / 4 (2 errors); 2: 5 / 7 / 7 / 5 (0 errors)

Images: [2-layout-1](../tests/quality/t2/dungeon-r2/2-layout-1.png) · [2-layout-2](../tests/quality/t2/dungeon-r2/2-layout-2.png) · [2-layout](../tests/quality/t2/dungeon-r2/2-layout.png) · [3-plan](../tests/quality/t2/dungeon-r2/3-plan.png) · [5-build-1](../tests/quality/t2/dungeon-r2/5-build-1.png) · [5-scene-detail](../tests/quality/t2/dungeon-r2/5-scene-detail.png) · [5-scene](../tests/quality/t2/dungeon-r2/5-scene.png) · [6-build-1](../tests/quality/t2/dungeon-r2/6-build-1.png) · [6-scene-detail](../tests/quality/t2/dungeon-r2/6-scene-detail.png) · [6-scene](../tests/quality/t2/dungeon-r2/6-scene.png)

![dungeon-r2 build](../tests/quality/t2/dungeon-r2/5-scene.png)

#### ② Layout check, round 1

Scores (layout / placement / fit / quality): 4 / 4 / 5 / 4. The layout contains all required zones but suffers from poor connectivity, forcing players to backtrack from the boss chamber to the exit, while the trap room is misplaced near the exit rather than deeper in the dungeon. Most critically, connect the exit-tunnel directly to the ancient-chamber and move the trap-room to the corridor before the boss chamber.

- *error* — exit-tunnel: Connected to entrance not ancient-chamber, forces backtracking after boss fight
- *error* — crypt: Dead end off boss room ancient-chamber, illogical placement
- *warning* — trap-room: Located near exit not deeper in dungeon as story requires
- *warning* — unlabeled-room: Room at bottom center has no label
- *warning* — layout: All rooms are rectangular boxes on grid, lacks organic varied shapes
- *warning* — guard-room: Disconnected from main path and ancient-chamber

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| ancient-chamber | – | 1 | ok | ok | ok | Deepest point, proper for boss chamber |
| crypt | – | 1 | **no** | ok | ok | Dead end off boss room, illogical placement |
| entrance | – | 1 | ok | ok | ok | At edge, proper start point for dungeon |
| exit-tunnel | – | 1 | **no** | ok | ok | Connected to entrance not boss, forces backtrack |
| gallery | – | 1 | ok | ok | ok | Off main path, atmospheric side room |
| guard-room | – | 1 | **no** | **no** | **no** | Near exit not deeper in dungeon as required |
| secret-room | – | 1 | **no** | **no** | **no** | Near exit not deeper in dungeon as required |
| shrine | – | 1 | ok | ok | ok | Central hub on path to deep areas |
| storeroom | – | 1 | ok | ok | ok | Near entrance, logical for storage support |
| trap-room | – | 1 | **no** | **no** | **no** | Near exit not deeper in dungeon as required |
| unlabeled-room | – | 1 | **no** | **no** | **no** | Near exit not deeper in dungeon as required |

#### ② Layout check, round 2

Scores (layout / placement / fit / quality): 5 / 7 / 7 / 5. This is a functional dungeon layout with good connectivity and all required story zones, but it feels procedural with its grid of rectangular rooms. The most important fix is to vary the room shapes and sizes, and to expand the crypt into a proper multi-room area.

- *warning* — layout: All rooms are grid-aligned rectangles, lacking the varied shapes of a hand-crafted dungeon
- *warning* — crypt: Crypt is a single small room, but the theme suggests a larger burial area with halls
- *warning* — unlabeled-room: Room between trap-room and exit-tunnel has no label, unclear purpose
- *warning* — layout: Room count is 11, but brief specifies 12 rooms
- *warning* — rooms: Rooms are too similar in size, lacking the large boss chamber and small closets variety

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| ancient-chamber | – | 1 | ok | ok | ok | Deepest room, top right, good for boss/journal |
| crypt | – | 1 | ok | ok | **no** | Too small for a crypt, lacks halls or multiple chambers |
| entrance | – | 1 | ok | ok | ok | Left side entry point, connects to both upper and lower paths |
| exit-tunnel | – | 1 | ok | ok | ok | Bottom, connects to guard-room and loop, good exit |
| gallery | – | 1 | ok | ok | ok | Connects storeroom to shrine, transitional space |
| guard-room | – | 1 | **no** | **no** | **no** | No label, unclear purpose between trap-room and exit-tunnel |
| secret-room | – | 1 | **no** | **no** | **no** | No label, unclear purpose between trap-room and exit-tunnel |
| shrine | – | 1 | ok | ok | ok | Central hub connecting upper path, lower exit, and secret room |
| storeroom | – | 1 | ok | ok | ok | Near entrance, logical for storage |
| trap-room | – | 1 | **no** | **no** | **no** | No label, unclear purpose between trap-room and exit-tunnel |
| unlabeled-room | – | 1 | **no** | **no** | **no** | No label, unclear purpose between trap-room and exit-tunnel |

#### ③ Plan check

Scores (layout / placement / fit / quality): 4 / 5 / 6 / 4. This reads as a procedurally generated grid of rectangular rooms with props scattered by algorithm rather than a hand-crafted crypt. The most critical fixes are adding the missing bones, relocating keys near the locked doors they open, and removing the uniform debris spam while varying room shapes.

- *error* — bones: No bones placed in crypt rooms despite being required by brief and essential to theme
- *error* — key: Keys are in distant crypt rooms while locked doors are in center junction, breaking progression logic
- *warning* — debris: Debris spammed uniformly in every corner of every room, looks procedural
- *warning* — layout: Rooms are uniform rectangles in a grid, lacking varied shapes and loops
- *warning* — sarcophagus: Sarcophagi scattered randomly instead of arranged in rows
- *warning* — pillar: Pillars randomly placed in three rooms with no structural logic
- *warning* — stairs: Two staircases without clear up/down distinction
- *warning* — light: Lights placed in corners like debris, too regular, no atmospheric pools
- *warning* — enemy: Top-right crypt room is overcrowded with enemies
- *warning* — scene: Missing whispering mirror quest item required for scene s2-04

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| altar | 1 | 1 | ok | ok | ok | centered in shrine room with candles forming focal point |
| barrel | 2 | 2 | ok | ok | ok | clustered with crate and sack in storeroom corner |
| bones | 8 | – | – | – | – |  |
| candle | 4 | 4 | ok | ok | ok | arranged symmetrically around altar |
| chest | 1 | 1 | ok | ok | ok | isolated in dead-end side room |
| crate | 1 | 1 | ok | ok | ok | grouped with barrels in storeroom |
| debris | 32 | 32 | **no** | ok | **no** | spammed uniformly in every corner of every room, looks procedural |
| door | 20 | 20 | ok | ok | ok | placed in walls between rooms |
| enemy | 7 | 10 | ok | ok | **no** | top-right room overcrowded, unclear patrol paths |
| key | 1 | 2 | **no** | ok | **no** | placed in distant crypt rooms while locked doors are in center junction |
| light | 13 | 13 | **no** | ok | **no** | placed in corners like debris, too regular, no atmospheric pools |
| locked-door | 2 | 2 | ok | ok | ok | placed in walls between rooms |
| loot | 1 | 1 | ok | ok | ok | in chest room |
| pillar | 3 | 3 | **no** | ok | **no** | randomly placed in three rooms with no structural logic |
| sack | 1 | 1 | ok | ok | ok | in storeroom with barrels |
| sarcophagus | 7 | 7 | **no** | ok | **no** | scattered randomly instead of arranged in rows |
| secret-door | 2 | 2 | ok | ok | ok | placed in walls between rooms |
| stairs | 2 | 2 | **no** | ok | **no** | two staircases without clear up/down distinction |
| table | 1 | 1 | ok | ok | ok | in guard room |
| trap | 5 | 4 | ok | ok | ok | clustered in dedicated trap room |

#### ⑤ Build check

Scores (layout / placement / fit / quality): 3 / 4 / 6 / 5. This reads as a procedurally generated grid rather than a hand-crafted dungeon, with uniform rooms and props blocking walkways. The most critical fix is redesigning the layout with irregular, purposeful room shapes and pushing all props to walls and corners to create clear, atmospheric pathways.

- *error* — layout: Dungeon is a grid of identical rectangular rooms, not an organic labyrinth
- *error* — sarcophagus: Sarcophagi block the center of crypt rooms, impeding movement
- *error* — debris: Debris scattered randomly across floors like confetti, not environmental storytelling
- *warning* — lighting: Lighting is flat and even; lacks the oppressive, suffocating mood of a cursed dungeon
- *warning* — floors: Floor appears to be one repeated tile without variation or damage
- *warning* — whispering-mirror: Quest-critical mirror object is not visible in the shrine or secret room
- *warning* — trap-room: Traps are isolated in one room rather than integrated into the dungeon's flow
- *warning* — storeroom: Storage items float in the middle of the room instead of being stored against walls

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| altar | 1 | 1 | ok | – | – | Centered in shrine room as focal point, appropriate for the space |
| barrel | 2 | 2 | – | – | – |  |
| bones | 8 | 8 | **no** | – | – | Scattered randomly across floors like noise, not intentional clutter |
| candle | 4 | 4 | ok | – | – | Centered in shrine room as focal point, appropriate for the space |
| character | – | 2 | ok | – | ok | Garrick and Lyric visible in lower rooms, appropriate for scene |
| chest | 1 | 1 | ok | – | – | Placed in secret room, appropriate reward for exploration |
| crate | 1 | 1 | – | – | – |  |
| debris | 32 | 32 | **no** | – | – | Scattered randomly across floors like noise, not intentional clutter |
| door | 20 | 20 | ok | – | – | Thick walls with doors set in them, functional connectivity between spaces |
| enemy | 7 | 7 | – | – | – |  |
| key | 1 | 1 | ok | – | – | Placed in crypt room, logical placement for locked door elsewhere |
| light | 13 | 13 | **no** | – | – | Placed evenly throughout, lacks dramatic pools and shadows |
| locked-door | 2 | 2 | ok | – | – | Thick walls with doors set in them, functional connectivity between spaces |
| loot | 1 | 1 | ok | – | – | Placed in secret room, appropriate reward for exploration |
| pillar | 3 | 3 | ok | – | – | Placed in corridors and junctions, structurally logical |
| sack | 1 | 1 | – | – | – |  |
| sarcophagus | 7 | 7 | **no** | – | – | Placed in middle of crypt rooms, blocking walkways; should line walls |
| secret-door | 2 | 2 | ok | – | – | Thick walls with doors set in them, functional connectivity between spaces |
| stairs | 2 | 2 | ok | – | – | Entrance and exit stairs placed in separate rooms, logical progression |
| table | 1 | 1 | ok | – | – | In guard room, fits the room's purpose |
| trap | 5 | 5 | **no** | – | – | Clustered in one room rather than integrated into corridors for pathing challenge |
| wall-block | – | 1 | ok | – | **no** | Visible repeating tile pattern; lacks hand-placed variation and cracks |

#### ⑤ Build check after the redo

Scores (layout / placement / fit / quality): 4 / 7 / 8 / 7. This dungeon captures the crypt mood with consistent pixel art and proper prop placement, but the rigid grid of uniform rooms feels procedural rather than hand-crafted. The most urgent fix is adding the missing quest-critical whispering mirror and spectral knight encounter, then varying room shapes to create organic flow.

- *error* — whispering-mirror: Quest-critical mirror for scene s2-04 is completely missing from the dungeon layout
- *error* — spectral-knight: Lyric spectral encounter required by brief is absent, only regular skeletons present
- *error* — journal: Quest item journal needed for scene s2-06 is not placed anywhere in the dungeon
- *warning* — layout: Rooms are uniform rectangles arranged in a grid pattern, lacking varied shapes and organic flow
- *warning* — lighting: Lighting is somewhat flat despite torches, lacks deep shadows and dramatic contrast
- *warning* — floor: Floor texture appears repetitive without puddles, stains, or varied decals mentioned in goals
- *warning* — debris: Debris scattered randomly across floors rather than clustered in corners or against walls
- *warning* — layout: Some corridors are long and straight without visual interest or cover

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| altar | 1 | 1 | ok | ok | ok | Altar centered in shrine-room as strong focal piece |
| barrel | 2 | 2 | ok | ok | ok | Barrels clustered in storeroom corner, leaving walkable space |
| bones | 8 | 8 | ok | ok | ok | Bones scattered on crypt floor, adds atmosphere without obstruction |
| candle | 4 | 4 | ok | ok | ok | Candles surround altar in shrine-room, creating focal lighting |
| chest | 1 | 1 | ok | ok | ok | Chest tucked in secret-room corner as hidden reward |
| crate | 1 | 1 | ok | ok | ok | Crate placed beside barrels in storeroom, fits storage purpose |
| debris | 32 | 32 | **no** | ok | ok | Debris scattered randomly across floors rather than clustered in corners or against walls |
| door | 20 | 20 | ok | ok | ok | Doors set in wall openings connect rooms logically and block passages appropriately |
| enemy | 7 | 7 | ok | ok | ok | Skeletons positioned guarding crypt and guard-room areas |
| floor | – | 1 | ok | ok | ok | Continuous stone surface without visible tile seams or grid |
| key | 1 | 1 | ok | ok | ok | Key on crypt-room floor guards progression to locked areas |
| light | 13 | 13 | ok | ok | ok | Wall torches provide warm pools of light falling off into shadow |
| locked-door | 2 | 2 | ok | ok | ok | Doors set in wall openings connect rooms logically and block passages appropriately |
| loot | 1 | 1 | ok | ok | ok | Loot placed beside chest in secret-room, logical reward spot |
| pillar | 3 | 3 | ok | ok | ok | Pillars in corridors and junctions break up long passages naturally |
| sack | 1 | 1 | ok | ok | ok | Sack near crate adds clutter without blocking paths |
| sarcophagus | 7 | 7 | ok | ok | ok | Coffins arranged in rows and against walls in crypt-room, center kept clear |
| secret-door | 2 | 2 | ok | ok | ok | Doors set in wall openings connect rooms logically and block passages appropriately |
| stairs | 2 | 2 | ok | ok | ok | Entrance and exit stairs placed in dedicated rooms at logical points |
| table | 1 | 1 | ok | ok | ok | Table against wall in guard-room fits room purpose |
| trap | 5 | 5 | ok | ok | ok | Traps on trap-room floor create hazardous path through chamber |
| wall-block | – | 1 | ok | ok | ok | Thick continuous walls properly define rooms and corridors with solid rock outside |

### dungeon-r3

Place: `location/dungeon-of-whispers` (dungeon). Art: 26 pieces, 0 placeholders, 22 found pieces refused by the check.

| Step | layout / placement / fit / quality | errors | warnings |
|---|---|---|---|
| ② Layout | 6 / 7 / 6 / 5 | 0 | 5 |
| ③ Plan | 4 / 5 / 6 / 4 | 2 | 6 |
| ⑤ Build | 4.5 / 6.25 / 7.25 / 6.75 | 7 | 21 |
| ⑤ Build after the redo | 4.75 / 6 / 7 / 6.75 | 5 | 23 |

Layout check, round by round: 1: 0 / 0 / 0 / 0 (5 errors); 2: 6 / 7 / 6 / 5 (0 errors)

Images: [2-layout-1](../tests/quality/t2/dungeon-r3/2-layout-1.png) · [2-layout-2](../tests/quality/t2/dungeon-r3/2-layout-2.png) · [2-layout](../tests/quality/t2/dungeon-r3/2-layout.png) · [3-plan](../tests/quality/t2/dungeon-r3/3-plan.png) · [5-build-1](../tests/quality/t2/dungeon-r3/5-build-1.png) · [5-build-2](../tests/quality/t2/dungeon-r3/5-build-2.png) · [5-build-3](../tests/quality/t2/dungeon-r3/5-build-3.png) · [5-build-4](../tests/quality/t2/dungeon-r3/5-build-4.png) · [5-scene-detail](../tests/quality/t2/dungeon-r3/5-scene-detail.png) · [5-scene](../tests/quality/t2/dungeon-r3/5-scene.png) · [6-build-1](../tests/quality/t2/dungeon-r3/6-build-1.png) · [6-build-2](../tests/quality/t2/dungeon-r3/6-build-2.png) · [6-build-3](../tests/quality/t2/dungeon-r3/6-build-3.png) · [6-build-4](../tests/quality/t2/dungeon-r3/6-build-4.png) · [6-scene-detail](../tests/quality/t2/dungeon-r3/6-scene-detail.png) · [6-scene](../tests/quality/t2/dungeon-r3/6-scene.png)

![dungeon-r3 build](../tests/quality/t2/dungeon-r3/5-scene.png)

#### ② Layout check, round 1

Scores (layout / placement / fit / quality): 0 / 0 / 0 / 0. This is a single empty room, not a dungeon; it completely fails to create the Dungeon of Whispers. The builder must generate the full 12-room crypt with corridors, the whispering mirror, trap chamber, ancient chamber, and exit tunnel.

- *error* — layout: Only one room exists; brief requires 12 rooms across four scenes
- *error* — entrance: Room is isolated with no doors or corridors leading anywhere
- *error* — level-1: Missing all required zones: whispering-mirror, trap-chamber, ancient-chamber, exit-tunnel
- *error* — layout: No varied room shapes; single perfect square violates crypt theme
- *error* — layout: No path from entrance to deepest point (boss/amulet)

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| entrance | – | 1 | 1/10 **no** | 1/10 **no** | 1/10 **no** | Isolated square with no doors, corridors, or connection to other required chambers |
| level-1 | – | 1 | 0/10 **no** | 0/10 **no** | 0/10 **no** | Contains only one room; missing 11 rooms and all connecting structure |

#### ② Layout check, round 2

Scores (layout / placement / fit / quality): 6 / 7 / 6 / 5. A functional but generic dungeon that meets connectivity needs but lacks hand-crafted variety and loops. The most important fix is adding loops and varying room shapes to break the grid-like monotony.

- *warning* — layout: No loops or alternate routes; entirely linear tree structure
- *warning* — corridor-2: Long dead-end corridor with no purpose
- *warning* — layout: Uniform rectangular rooms lack varied shapes
- *warning* — storeroom: Thematic mismatch for cursed dungeon
- *warning* — secret: Only accessible via crypt, easily missed

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| ancient-chamber-cave | – | 1 | 8/10 ok | 8/10 ok | 6/10 ok | Early hazard room right after entrance, fits trap feature |
| ancient-chamber-shrine | – | 1 | 8/10 ok | 8/10 ok | 6/10 ok | Early hazard room right after entrance, fits trap feature |
| corridor-1 | – | 1 | 3/10 ok | 3/10 ok | 4/10 ok | Connects entrance to shrine branch |
| corridor-2 | – | 1 | 3/10 ok | 3/10 ok | 4/10 ok | Connects entrance to shrine branch |
| corridor-3 | – | 1 | 3/10 ok | 3/10 ok | 4/10 ok | Connects entrance to shrine branch |
| crypt | – | 1 | 8/10 ok | 9/10 ok | 6/10 ok | Edge location, thematic for crypt dungeon |
| entrance | – | 1 | 8/10 ok | 8/10 ok | 6/10 ok | Left-side entry, branches to trap-chamber and corridor-3 |
| exit-tunnel | – | 1 | 9/10 ok | 8/10 ok | 6/10 ok | Right-side exit, opposite entrance |
| junction | – | 1 | 8/10 ok | 7/10 ok | 6/10 ok | Hub connecting storeroom, crypt, and cave |
| secret | – | 1 | 7/10 ok | 8/10 ok | 6/10 ok | Hidden room off crypt, good for secrets |
| storeroom | – | 1 | 5/10 ok | 4/10 ok | 5/10 ok | Thematically odd for cursed dungeon; feels like castle storage |
| trap-chamber | – | 1 | 8/10 ok | 8/10 ok | 6/10 ok | Early hazard room right after entrance, fits trap feature |
| treasure | – | 1 | 9/10 ok | 8/10 ok | 6/10 ok | Dead-end reward room, classic placement |
| whispering-mirror | – | 1 | 8/10 ok | 9/10 ok | 6/10 ok | Quest-critical room at path end near exit |

#### ③ Plan check

Scores (layout / placement / fit / quality): 4 / 5 / 6 / 4. This looks like a procedurally generated grid of rectangular rooms with excessive debris scatter, lacking the varied shapes, sparse lighting, and narrative item placement of a hand-crafted crypt. The most important fix is to reduce debris by 80% and redesign the room shapes to be organic and varied.

- *error* — debris: 67 debris objects clutter every room and block paths.
- *error* — layout: Rooms are uniform rectangles in a grid, lacking varied shapes.
- *warning* — enemy: Only 2 enemies in a 12-room dungeon is too sparse.
- *warning* — light: 26 lights make the dungeon too bright, killing atmosphere.
- *warning* — trap: 4 traps in one room may make it impassable.
- *warning* — key: Keys may be placed behind their locked doors.
- *warning* — scene: Missing the whispering mirror quest item.
- *warning* — scene: No clear boss chamber at the deepest point.

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| altar | 1 | 1 | 9/10 ok | 9/10 ok | – | Central focal point in shrine room with candles arranged around it. |
| barrel | 3 | 3 | 7/10 ok | 8/10 ok | – | In storeroom against walls, grouped with crate and sack. |
| bones | 8 | 8 | 8/10 ok | 9/10 ok | – | In crypt room corners, fitting the tomb theme. |
| candle | 4 | 4 | 8/10 ok | 8/10 ok | – | Surrounding altar creating ritual circle. |
| chest | 2 | 2 | 8/10 ok | 8/10 ok | – | In corners of secret and treasure rooms. |
| crate | 1 | 1 | 7/10 ok | 8/10 ok | – | In storeroom against wall. |
| debris | 67 | 67 | 2/10 **no** | 3/10 **no** | 2/10 **no** | 67 instances scattered everywhere, blocking paths and cluttering rooms instead of corners. |
| door | 22 | 22 | 6/10 ok | 7/10 ok | – | Set in walls connecting rooms properly. |
| enemy | 2 | 2 | 6/10 ok | 5/10 **no** | – | Only 2 enemies doesn't fit a 12-room dungeon. |
| key | 2 | 2 | 5/10 **no** | 7/10 ok | – | Placement relative to locked doors unclear; may be behind them. |
| lever | 3 | 3 | 8/10 ok | 8/10 ok | – | In puzzle room near the gate they control. |
| light | 26 | 26 | 5/10 **no** | 6/10 ok | 5/10 **no** | 26 lights make dungeon too bright, killing atmosphere. |
| locked-door | 4 | 4 | 6/10 ok | 7/10 ok | – | Set in walls connecting rooms properly. |
| loot | 2 | 2 | 7/10 ok | 8/10 ok | – | In secret and treasure rooms. |
| pillar | 5 | 5 | 7/10 ok | 8/10 ok | – | In corridors and junctions for structure. |
| rock | 6 | 6 | 8/10 ok | 8/10 ok | – | In cave chamber corners. |
| sack | 1 | 1 | 7/10 ok | 8/10 ok | – | In storeroom against wall. |
| sarcophagus | 4 | 4 | 8/10 ok | 9/10 ok | – | Against walls in crypt room. |
| secret-door | 2 | 2 | 6/10 ok | 7/10 ok | – | Set in walls connecting rooms properly. |
| stairs | 2 | 2 | 8/10 ok | 9/10 ok | – | Entrance and exit placed logically. |
| trap | 5 | 5 | 4/10 **no** | 7/10 ok | – | 4 traps in one room may block entire space. |

#### ⑤ Build check

Scores (layout / placement / fit / quality): 4.5 / 6.25 / 7.25 / 6.75. This is a functional but generic dungeon layout that fails the core design brief with its rigid grid of identical rooms and missing quest-critical items. The pixel art quality is decent but the traps look like placeholders and the lighting lacks the oppressive atmosphere required for the Dungeon of Whispers. This is a passable procedural dungeon that captures the crypt mood but suffers from repetitive grid-like rooms and visible floor tiling that breaks immersion. The most important fix is to break up the floor grid with varied textures and irregular room shapes to match hand-crafted Diablo-s…

- *error* — layout: Rooms arranged in rigid grid of equal rectangles, violating 'never a grid of equal boxes'
- *error* — scene: Missing whispering-mirror quest item required for entrance-chamber
- *warning* — trap: Traps appear as flat blue triangles, looking like placeholders
- *warning* — lighting: Lighting too uniform, lacks dramatic shadow pools
- *warning* — floor: Floor appears to be one repeated tile without cracks or stains
- *warning* — rooms: No strong focal piece in any room (altar, throne, sarcophagus)
- *error* — floor: Floor shows visible tile grid seams and repetitive pattern, breaking immersion
- *warning* — layout: Rooms are too uniform and grid-like, lacking varied shapes and sizes
- *warning* — lighting: Lighting is too flat and uniform, lacks dramatic shadows and pools of light
- *warning* — scene: Missing quest-critical whispering mirror
- *warning* — scene: Missing spectral knight (Lyric) encounter actor
- *warning* — trap: Shifting floors trap feature is not visually distinct

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| altar | 1 | 1 | 8/10 ok | 9/10 ok | 9/10 ok | Central focal point in shrine room, appropriate for the space |
| barrel | 3 | 3 | 7/10 ok | 7/10 ok | 7/10 ok | Against walls in storeroom |
| bones | 8 | 8 | 7/10 ok | 8/10 ok | 7/10 ok | In corners as intended, fits crypt theme |
| candle | 4 | 4 | 9/10 ok | 8/10 ok | 8/10 ok | Clustered around altar as intended for shrine ambiance |
| chest | 2 | 2 | 7/10 ok | 8/10 ok | 8/10 ok | In secret room corner, logical placement |
| crate | 1 | 1 | 7/10 ok | 7/10 ok | 7/10 ok | In storeroom corner |
| debris | 67 | 67 | 6/10 ok | 6/10 ok | 6/10 ok | In corners and against walls, not blocking paths |
| door | 22 | 22 | 7/10 ok | 7/10 ok | 7/10 ok | Properly connects rooms in corridors |
| enemy | 2 | 2 | 6/10 ok | 7/10 ok | 6/10 ok | In room but placement is generic |
| floor | – | 3 | 4/10 **no** | 5/10 ok | 4/10 **no** | Visible tile seams and repetitive grid pattern break immersion |
| key | 2 | 2 | 7/10 ok | 7/10 ok | 6/10 ok | Placed in rooms, accessible to player |
| lever | 3 | 3 | 8/10 ok | 7/10 ok | 6/10 ok | Grouped in puzzle-room as intended |
| light | 26 | 14 | 7/10 ok | 6/10 ok | 7/10 ok | On walls providing illumination, correct placement |
| locked-door | 4 | 4 | 7/10 ok | 7/10 ok | 7/10 ok | Properly connects rooms in corridors |
| loot | 2 | 2 | 7/10 ok | 7/10 ok | 6/10 ok | In secret room, appropriate |
| pillar | 5 | 5 | 7/10 ok | 7/10 ok | 7/10 ok | In corridor adding architectural interest |
| rock | 6 | 23 | 3/10 **no** | 6/10 ok | 7/10 ok | Many placed in black void outside dungeon walls, floating without ground |
| sack | 1 | 1 | 7/10 ok | 7/10 ok | 7/10 ok | In storeroom |
| sarcophagus | 4 | 4 | 8/10 ok | 9/10 ok | 8/10 ok | Against walls in crypt rooms, correct scale and purpose |
| secret-door | 2 | 2 | 7/10 ok | 7/10 ok | 7/10 ok | Properly connects rooms in corridors |
| stairs | 2 | 3 | 8/10 ok | 7/10 ok | 7/10 ok | Connecting levels, proper placement |
| trap | 5 | 5 | 6/10 ok | 6/10 ok | 4/10 **no** | Blue triangles look like flat placeholder shapes, not detailed traps |
| wall-block | – | 1 | 5/10 **no** | 7/10 ok | 7/10 ok | Rooms form a rigid grid of similar-sized rectangles, lacking organic variation |
| wall-torch | – | 12 | 5/10 **no** | 7/10 ok | 7/10 ok | Rooms form a rigid grid of similar-sized rectangles, lacking organic variation |

#### ⑤ Build check after the redo

Scores (layout / placement / fit / quality): 4.75 / 6 / 7 / 6.75. The dungeon captures the oppressive crypt mood with solid wall and prop art, but the blue trap sprites are placeholder-quality and break visual cohesion. Replace the traps with detailed stone or rune-based designs that fit the palette, and add the missing key and mirror. This is a procedurally generated dungeon with competent pixel art assets but lacks the hand-crafted feel of varied room shapes, intentional prop placement, and atmospheric lighting. The most critical fix is breaking the rectangular grid layout and adding the missing whispering mirror quest object. This is a passable dungeon l…

- *error* — trap: Traps are flat blue squares that look like placeholders and clash with the crypt palette
- *warning* — layout: Rooms are rectangular grid boxes, not varied organic shapes
- *warning* — locked-door: No key visible near the locked door as required by design goals
- *warning* — trap: Traps arranged in a rigid grid pattern, looks artificial
- *warning* — scene: Missing the whispering-mirror quest item for this scene
- *warning* — floor: Floor is uniform stone without variation, stains, or rubble patches
- *error* — layout: Rooms form a grid of equal rectangles, lacking varied shapes like halls or boss chambers
- *error* — scene: Missing the whispering mirror quest object required for 'The Warden's Fall'
- *warning* — bones: Bones scattered in room centers instead of corners and against walls
- *warning* — debris: Debris randomly placed, cluttering walkable center space
- *warning* — lighting: Flat, even lighting lacks dramatic warm pools falling into shadow
- *warning* — floor: Uniform floor texture without cracks, rubble, or stains

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| altar | 1 | 1 | 7/10 ok | 9/10 ok | 8/10 ok | Central focal point in shrine room, but may block movement if too large |
| barrel | 3 | 3 | 8/10 ok | 8/10 ok | 8/10 ok | Clustered against storeroom walls, leaves center walkable |
| bones | 8 | 8 | 4/10 **no** | 8/10 ok | 7/10 ok | Scattered in room centers, not in corners or against walls |
| candle | 4 | 4 | 8/10 ok | 7/10 ok | 5/10 **no** | Appear as simple orange squares, look like placeholders rather than detailed candles |
| chest | 2 | 2 | 8/10 ok | 8/10 ok | 8/10 ok | In secret room as loot, properly placed |
| crate | 1 | 1 | 8/10 ok | 8/10 ok | 8/10 ok | Placed in storeroom corner with other storage items |
| debris | 67 | 67 | 4/10 **no** | 7/10 ok | 7/10 ok | Randomly scattered, clutters walkable center space |
| door | 22 | 22 | 5/10 **no** | 6/10 ok | 5/10 **no** | Not visually distinct as hidden; looks like normal wall |
| enemy | 2 | 2 | 6/10 ok | 7/10 ok | 7/10 ok | In crypt room, appropriate for dungeon |
| floor | – | 1 | 6/10 ok | 6/10 ok | 5/10 **no** | Too uniform dark color, lacks cracks, stains, or variation; looks flat |
| key | 2 | 2 | 7/10 ok | 7/10 ok | 7/10 ok | Placed in rooms, presumably on correct side of locked doors |
| lever | 3 | 3 | 8/10 ok | 8/10 ok | 7/10 ok | In puzzle-room as intended for puzzle mechanics |
| light | 26 | 8 | 6/10 ok | 7/10 ok | 7/10 ok | On walls as expected, but lighting is flat |
| locked-door | 4 | 4 | 5/10 **no** | 6/10 ok | 5/10 **no** | Not visually distinct as hidden; looks like normal wall |
| loot | 2 | 2 | 7/10 ok | 7/10 ok | 6/10 ok | Near chest in secret room, logical |
| pillar | 5 | 5 | 7/10 ok | 7/10 ok | 7/10 ok | Placed in corridor to break up long sightlines |
| rock | 6 | 6 | 6/10 ok | 7/10 ok | 7/10 ok | In cave areas, fits natural chamber theme |
| sack | 1 | 1 | 8/10 ok | 8/10 ok | 8/10 ok | In storeroom with barrels and crates, fits storage theme |
| sarcophagus | 4 | 4 | 8/10 ok | 9/10 ok | 8/10 ok | Against walls as tombs should be, correct scale |
| secret-door | 2 | 2 | 5/10 **no** | 6/10 ok | 5/10 **no** | Not visually distinct as hidden; looks like normal wall |
| stairs | 2 | 2 | 8/10 ok | 8/10 ok | 8/10 ok | Correctly placed in entrance room as the entry point |
| trap | 5 | 5 | 4/10 **no** | 3/10 **no** | 2/10 **no** | Arranged in a rigid grid pattern in the room, looks artificial |
| wall-block | – | 2 | 4/10 **no** | 6/10 ok | 6/10 ok | Forms rectangular grid of boxes, not varied organic shapes |
| wall-torch | – | 18 | 4/10 **no** | 6/10 ok | 6/10 ok | Forms rectangular grid of boxes, not varied organic shapes |

### interior-r0

Place: `location/enforcers-office` (interior). Art: 21 pieces, 0 placeholders, – found pieces refused by the check.

| Step | layout / placement / fit / quality | errors | warnings |
|---|---|---|---|
| ③ Plan | – / – / – / – | 0 | 0 |
| ⑤ Build | 2 / 2 / 3 / 2 | 4 | 3 |

Images: [3-plan](../tests/quality/t2/interior-r0/3-plan.png) · [5-build-1](../tests/quality/t2/interior-r0/5-build-1.png) · [5-scene-detail](../tests/quality/t2/interior-r0/5-scene-detail.png) · [5-scene](../tests/quality/t2/interior-r0/5-scene.png)

![interior-r0 build](../tests/quality/t2/interior-r0/5-scene.png)

#### ③ Plan check

The check failed: ["the answer wasn't JSON"]

#### ⑤ Build check

Scores (layout / placement / fit / quality): 2 / 2 / 3 / 2. This is a broken procedural generation with unrendered black walls and placeholder textures rather than a hand-crafted game environment. The critical fix is to render proper wall surfaces and establish coherent room divisions with logical furniture placement that tells the story of a decaying, haunted town hall.

- *error* — walls: Walls are rendered as solid black rectangles instead of textured stone/brick surfaces
- *error* — scene: White placeholder rectangle visible in the middle-right of the floor
- *error* — layout: No room structure - objects scattered in open space without walls defining distinct rooms
- *error* — doors: Doors appear as black voids or are missing entirely, blocking navigation
- *warning* — UI: Large white banner with 'MEJAM SUMMER EDITION 2019' text obscures top portion of the game scene
- *warning* — floor: Floor has dark border that appears to be void/background rather than building foundation
- *warning* — lighting: Lighting is too bright and even; does not match the oppressive, damp, haunted mood of the cursed town

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| banner | 2 | 2 | **no** | **no** | **no** | Banners are not clearly visible and appear to be placed on black wall voids rather than rendered surfaces |
| barrel | 4 | 4 | **no** | ok | ok | Barrels are scattered randomly in open space without storage room definition or grouping |
| bookshelf | 2 | 2 | **no** | ok | ok | Bookshelves are placed against black wall voids rather than proper room walls |
| carpet | 1 | 1 | **no** | ok | ok | Carpet is placed in cluttered open space without clear table and chair grouping |
| chair | 12 | 12 | **no** | ok | ok | Chairs are scattered randomly, some floating in open space, not arranged around tables facing them |
| counter | 1 | 1 | **no** | ok | ok | Counter is placed in undefined open space without room context or adjacent walls |
| crate | 3 | 3 | **no** | ok | ok | Crates are scattered without storage organization or logical grouping |
| decor | 9 | 9 | **no** | ok | ok | Decor items are scattered randomly on floor creating visual clutter rather than placed on surfaces |
| desk | 1 | 1 | **no** | ok | ok | Desk is placed in open space without office room definition or proper orientation |
| door | 13 | 13 | **no** | **no** | **no** | Doors appear as black voids or are missing entirely; no clear doorways connecting rooms |
| fireplace | 1 | 1 | **no** | ok | ok | Fireplace is present but not positioned as a proper hearth at the head of the hall or against a main wall |
| light | 7 | 7 | **no** | **no** | ok | Lights are scattered randomly without logical placement as wall sconces, table lamps, or lanterns |
| rug | 1 | 1 | **no** | ok | ok | Rug is placed in cluttered area without furniture grouping or clear purpose |
| sack | 5 | 5 | **no** | ok | ok | Sacks are scattered randomly on floor without storage context |
| shelf | 2 | 2 | **no** | ok | ok | Shelves are mounted on black wall voids rather than rendered walls |
| shelf2 | 2 | 2 | **no** | ok | ok | Shelf2 is placed in undefined space without room context |
| stand | 2 | 2 | **no** | ok | ok | Stands are placed randomly in chamber without logical positioning |
| table | 4 | 4 | **no** | ok | ok | Tables are scattered without chairs properly arranged around them or clear dining area definition |
| throne | 1 | 1 | **no** | ok | ok | Throne is surrounded by clutter rather than placed at the head of the great hall on a dais |
| window | 5 | 5 | **no** | **no** | **no** | Windows are not visible or are obscured by black walls; should show rainy exterior |

### interior-r1

Place: `location/enforcers-office` (interior). Art: 23 pieces, 0 placeholders, 10 found pieces refused by the check.

| Step | layout / placement / fit / quality | errors | warnings |
|---|---|---|---|
| ② Layout | 1 / 0 / 0 / 0 | 6 | 1 |
| ③ Plan | – / – / – / – | 0 | 0 |
| ⑤ Build | 2 / 4 / 7 / 7 | 2 | 5 |

Layout check, round by round: 1: 1 / 1 / 0 / 1 (6 errors); 2: 1 / 1 / 0 / 1 (6 errors); 3: 1 / 0 / 0 / 0 (6 errors)

Images: [2-layout-1](../tests/quality/t2/interior-r1/2-layout-1.png) · [2-layout-2](../tests/quality/t2/interior-r1/2-layout-2.png) · [2-layout-3](../tests/quality/t2/interior-r1/2-layout-3.png) · [2-layout](../tests/quality/t2/interior-r1/2-layout.png) · [3-plan](../tests/quality/t2/interior-r1/3-plan.png) · [5-build-1](../tests/quality/t2/interior-r1/5-build-1.png) · [5-scene-detail](../tests/quality/t2/interior-r1/5-scene-detail.png) · [5-scene](../tests/quality/t2/interior-r1/5-scene.png)

![interior-r1 build](../tests/quality/t2/interior-r1/5-scene.png)

#### ② Layout check, round 1

Scores (layout / placement / fit / quality): 1 / 1 / 0 / 1. This is a bare floor texture layout with no walls, rooms, furniture, or architectural structure, failing completely to represent a town hall interior. The two floors are incorrectly placed side-by-side rather than stacked, and every required feature from the brief is missing.

- *error* — layout: Two floor areas are placed side-by-side horizontally instead of stacked vertically (ground floor below upper floor)
- *error* — layout: No walls exist anywhere; rooms are not defined, only floor textures float in void
- *error* — layout: No doors or connections between the two floor areas or to the outside
- *error* — zones: Zones are not labeled; cannot identify which area is ground-floor vs upper-floor
- *error* — features: All required features are missing: dining-set, floor-clutter, wall-decor, room-lights
- *error* — ground-floor: Ground floor lacks the radial layout centered on a Great Hall as specified in the zone plan
- *warning* — stone floor: Stone and wood floors meet without a wall or threshold, creating an undefined boundary

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| stone floor | – | 1 | **no** | **no** | **no** | Floating in void with no walls, no connection to other areas, and mixed with stone floor without any separation or threshold |
| void | – | 1 | ok | – | – | Correctly represents empty space outside the building, though the gap between the two structures is a critical error |
| wood floor | – | 2 | **no** | **no** | **no** | Floating in void with no walls, no connection to other areas, and mixed with stone floor without any separation or threshold |

#### ② Layout check, round 2

Scores (layout / placement / fit / quality): 1 / 1 / 0 / 1. This output is merely two disconnected floor textures with no walls, rooms, furniture, or labels, failing completely to represent the town hall interior described in the brief. The critical fix is to add walls defining the Great Hall and private rooms, connect the floors with the ground floor south and upper floor north, and populate with the required furniture and lighting.

- *error* — layout: No walls exist; the entire interior is undefined floor floating in void
- *error* — layout: The two zones are separated by void with no connection or shared walls
- *error* — ground-floor: Zone is not labeled and has no internal room structure or furniture
- *error* — upper-floor: Zone is not labeled and has no internal room structure or furniture
- *error* — layout: Zones are positioned side-by-side (west/east) instead of north/south as planned in the brief
- *error* — layout: Missing all required features: dining-set, floor-clutter, wall-decor, room-lights
- *warning* — ground-floor: Mixed flooring (stone and wood) without walls to define separate rooms

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| ground-floor | – | 1 | **no** | **no** | **no** | Planned for the southern part of the site but appears as an unlabeled rectangle on the left (west) with no walls, doors, or internal rooms;… |
| stone floor | – | 1 | **no** | **no** | **no** | Planned for the southern part of the site but appears as an unlabeled rectangle on the left (west) with no walls, doors, or internal rooms;… |
| upper-floor | – | 1 | **no** | **no** | **no** | Planned for the southern part of the site but appears as an unlabeled rectangle on the left (west) with no walls, doors, or internal rooms;… |
| wood floor | – | 2 | **no** | **no** | **no** | Planned for the southern part of the site but appears as an unlabeled rectangle on the left (west) with no walls, doors, or internal rooms;… |

#### ② Layout check, round 3

Scores (layout / placement / fit / quality): 1 / 0 / 0 / 0. This is not a layout but merely two disconnected floor texture patches with no walls, rooms, furniture, or labels, completely failing to represent the Blackthorn Town Hall's structure or cursed atmosphere. The most important fix is to generate the actual architectural layout with walls, doors, rooms, and the required furniture and lighting for both floors.

- *error* — layout: No architectural structure generated - only floor texture patches with no walls, rooms, or doors
- *error* — ground-floor: Zone is unlabeled and lacks the required radial layout with Great Hall at center
- *error* — upper-floor: Zone is unlabeled and lacks the required linear layout for private/administrative spaces
- *error* — layout: The two floors are completely disconnected by void with no stairs or passage between them
- *error* — ground-floor: Missing all required features: dining-set, floor-clutter, wall-decor, room-lights
- *error* — upper-floor: Missing all required features for administrative spaces: desks, shelves, cabinets, wall-decor, room-lights
- *warning* — ground-floor: Arbitrary split between wood and stone flooring with no walls or architectural logic

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| ground-floor | – | 1 | **no** | **no** | **no** | Floating as a disconnected rectangle in void with no connection to the upper floor; should be the southern public level with a radial layou… |
| stone floor | – | 1 | **no** | **no** | **no** | Floating as a disconnected rectangle in void with no connection to the upper floor; should be the southern public level with a radial layou… |
| upper-floor | – | 1 | **no** | **no** | **no** | Floating as a disconnected rectangle in void with no connection to the upper floor; should be the southern public level with a radial layou… |
| void | – | 1 | **no** | – | – | Separates the two floors completely, making the place unnavigable; interiors should have no void gaps between connected spaces |
| wood floor | – | 2 | **no** | **no** | **no** | Floating as a disconnected rectangle in void with no connection to the upper floor; should be the southern public level with a radial layou… |

#### ③ Plan check

Scores (layout / placement / fit / quality): – / – / – / –. 


| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| barrel | 8 | 8 | **no** | – | – | In storage, barrels are scattered rather than stacked together near the storeroom door; in the tavern they are acceptable at corners but la… |
| bookshelf | 5 | 5 | ok | – | – | Placed against walls in library/study areas, facing into the room, which is correct. |
| chair | 15 | 15 | **no** | – | – | Chairs are scattered randomly across floors with no relation to tables or walls; they should be pulled up to tables or lined against walls. |
| counter | 1 | 1 | **no** | – | – | The bar counter floats in the middle of the tavern hall instead of being against a wall, which breaks the room's flow and realism. |
| crate | 2 | 2 | ok | – | – | In storage rooms, crates are on the floor near other storage items, which is acceptable. |
| cupboard | 1 | 1 | ok | – | – | Against the wall in the kitchen/storage area, fitting its purpose. |
| decor | 3 | 3 | ok | – | – | Wall-mounted decor items are placed on walls as intended. |
| desk | 4 | 4 | ok | – | – | Desks are placed in office-like rooms with chairs nearby, facing into the room. |
| door | 12 | 12 | **no** | – | – | All 12 doors are internal; there are no doors on the exterior walls, meaning the building has no entrance and is unreachable. |
| fireplace | 2 | 2 | – | – | – |  |
| light | 11 | 11 | – | – | – |  |
| sack | 2 | 2 | – | – | – |  |
| shelf | 4 | 4 | – | – | – |  |
| shelf2 | 4 | 4 | – | – | – |  |
| stairs | 2 | 2 | – | – | – |  |
| stool | 4 | 4 | – | – | – |  |
| stove | 1 | 1 | – | – | – |  |
| table | 2 | 2 | – | – | – |  |
| window | 18 | 18 | – | – | – |  |
| woodpile | 1 | 1 | – | – | – |  |

#### ⑤ Build check

Scores (layout / placement / fit / quality): 2 / 4 / 7 / 7. The individual sprites are high-quality pixel art that perfectly capture the cursed, damp mood of Blackthorn, but the layout is fundamentally broken with two disconnected rooms floating in a void rather than forming a cohesive town hall. The zones must be merged into a single connected structure with shared walls, proper door connections, and logical furniture placement.

- *error* — layout: The two zones are completely disconnected with a large void between them instead of sharing walls as adjacent rooms
- *error* — stairs: The staircase in the bottom room leads to empty space and does not connect to the upper zone
- *warning* — chair: Several chairs are placed in the middle of the room away from any table, appearing to float
- *warning* — barrel: Barrels are placed in the center of walkways, blocking movement
- *warning* — woodpile: The woodpile is placed in the middle of the dining area rather than near a fireplace or wall
- *warning* — table: No rug is placed under the dining table as required by the design goals
- *warning* — scene: The bottom room is too sparse and its function (library/chamber/storage) is unclear from the furniture present

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| barrel | 8 | 6 | **no** | ok | ok | Several barrels are placed in the middle of the room blocking walkways; they should be against walls or in storage corners |
| bookshelf | 5 | 4 | ok | ok | ok | Placed against walls, facing into the room |
| chair | 15 | 10 | **no** | ok | ok | Many chairs are floating in the center of the room away from tables; only some are properly placed around the dining table |
| counter | 1 | 1 | ok | ok | ok | Correctly placed against the wall in the tavern area, facing into the room |
| crate | 2 | 2 | ok | ok | ok | On floor as clutter, acceptable for storage areas |
| cupboard | 1 | – | – | – | – |  |
| decor | 3 | – | – | – | – |  |
| desk | 4 | 2 | ok | ok | ok | Placed against walls with space for a chair |
| door | 12 | 6 | **no** | ok | ok | Doors are in walls but the two rooms are completely disconnected; doors lead to voids or don't connect the zones |
| fireplace | 2 | 2 | ok | ok | ok | Properly placed on walls, providing light and fitting the room's function |
| floor | – | 2 | **no** | ok | ok | Two separate floor areas with a gap between them; should be one continuous floor for the building |
| light | 11 | 8 | ok | ok | ok | Well distributed throughout both rooms to create the flickering, dim atmosphere |
| sack | 2 | 2 | ok | ok | ok | On floor as clutter, fits the worn, poor condition |
| shelf | 4 | 4 | ok | ok | ok | Mounted on walls at appropriate height |
| shelf2 | 4 | – | – | – | – |  |
| stairs | 2 | 1 | **no** | ok | ok | The staircase leads upward but does not connect to the other visible zone; it appears to lead to empty space |
| stool | 4 | 4 | ok | ok | ok | Correctly placed near the bar counter |
| stove | 1 | – | – | – | – |  |
| table | 2 | 2 | ok | ok | ok | The long table is correctly placed in the center of the dining area, though it lacks a rug underneath |
| wall-block | – | 2 | **no** | ok | ok | Two completely separate wall structures with a large void between them; rooms should share walls |
| window | 18 | 12 | ok | ok | ok | Appropriately placed high in the walls |
| woodpile | 1 | 1 | **no** | ok | ok | Placed in the middle of the dining area; should be near a fireplace or against a wall |

### interior-r2

Place: `location/enforcers-office` (interior). Art: 22 pieces, 0 placeholders, 18 found pieces refused by the check.

| Step | layout / placement / fit / quality | errors | warnings |
|---|---|---|---|
| ② Layout | 4 / 3 / 5 / 4 | 2 | 4 |
| ③ Plan | 5 / 4 / 5 / 4 | 3 | 7 |
| ⑤ Build | 5 / 6 / 7 / 7 | 2 | 6 |
| ⑤ Build after the redo | 2 / 3 / 5 / 6 | 2 | 3 |

Layout check, round by round: 1: 3 / 4 / 5 / 4 (2 errors); 2: 4 / 5 / 7 / 4 (2 errors); 3: 4 / 3 / 5 / 4 (2 errors)

Images: [2-layout-1](../tests/quality/t2/interior-r2/2-layout-1.png) · [2-layout-2](../tests/quality/t2/interior-r2/2-layout-2.png) · [2-layout-3](../tests/quality/t2/interior-r2/2-layout-3.png) · [2-layout](../tests/quality/t2/interior-r2/2-layout.png) · [3-plan](../tests/quality/t2/interior-r2/3-plan.png) · [5-build-1](../tests/quality/t2/interior-r2/5-build-1.png) · [5-scene-detail](../tests/quality/t2/interior-r2/5-scene-detail.png) · [5-scene](../tests/quality/t2/interior-r2/5-scene.png) · [6-build-1](../tests/quality/t2/interior-r2/6-build-1.png) · [6-scene-detail](../tests/quality/t2/interior-r2/6-scene-detail.png) · [6-scene](../tests/quality/t2/interior-r2/6-scene.png)

![interior-r2 build](../tests/quality/t2/interior-r2/5-scene.png)

#### ② Layout check, round 1

Scores (layout / placement / fit / quality): 3 / 4 / 5 / 4. The layout captures the decaying town hall's grand public great-hall and secretive upper quarters, but critical errors—misaligned stairwells and no exterior entrance—render it unplayable. Align the stairwells vertically and add entrance doors immediately.

- *error* — stairwell: Stairwells on ground and upper floors are not vertically aligned (ground: NE, upper: SE)
- *error* — layout: No exterior doors visible; building is inaccessible from outside
- *warning* — upper-floor: Two rooms on upper floor lack labels
- *warning* — hall: Ground-floor 'hall' has no exterior access and unclear purpose

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| dining-room | – | 2 | ok | ok | **no** | Missing labels make purpose unclear to designers |
| great-hall | – | 1 | **no** | **no** | **no** | Top-left corner with no exterior door; unclear if entrance lobby |
| ground-floor | – | 1 | ok | ok | ok | Central corridor connects all ground rooms and great-hall |
| hall | – | 1 | **no** | **no** | **no** | Top-left corner with no exterior door; unclear if entrance lobby |
| stairwell | – | 2 | **no** | **no** | **no** | Ground floor at top-right, upper floor at bottom-right; not vertically aligned |
| storage | – | 1 | ok | ok | ok | Next to dining-room, logical for supplies in decaying hall |
| study | – | 2 | ok | ok | ok | Quiet room near storage, fits ground floor function |
| treasury | – | 1 | ok | ok | ok | Upper floor corner, stone floor, secure location for secrets |
| unlabeled-room | – | 2 | ok | ok | **no** | Missing labels make purpose unclear to designers |
| upper-floor | – | 1 | ok | ok | ok | Central corridor connects all ground rooms and great-hall |

#### ② Layout check, round 2

Scores (layout / placement / fit / quality): 4 / 5 / 7 / 4. This layout captures the decaying town hall's public and private zones well, but critical stairwell misalignment and inconsistent flooring prevent it from being playable. The most important fix is aligning the stairwells between floors.

- *error* — stairwell: Stairwell positions do not align between ground floor (top-right) and upper floor (bottom-right)
- *error* — great-hall: Stone floor in great-hall and treasury breaks the 'one continuous floor' requirement
- *warning* — unlabeled-room: Two rooms on upper floor have no labels, making their purpose unclear
- *warning* — layout: Great-hall is separated from entrance hall by corridor, potentially disconnecting the main public space

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| dining-room | – | 2 | **no** | **no** | **no** | Two rooms on upper floor lack labels, making their purpose unclear to players |
| great-hall | – | 1 | ok | ok | **no** | Stone floor inconsistent with wood flooring in other rooms, breaks continuity |
| ground-floor | – | 1 | ok | ok | ok | Corridor connects all ground floor rooms and provides access to great-hall |
| hall | – | 1 | ok | ok | **no** | Stone floor inconsistent with wood flooring in other rooms, breaks continuity |
| stairwell | – | 2 | **no** | ok | **no** | Ground floor stairwell at top-right does not align with upper floor stairwell at bottom-right |
| storage | – | 1 | ok | ok | ok | Adjacent to dining-room on ground floor for supplies and kitchen access |
| study | – | 2 | ok | ok | ok | Ground floor for public business; upper floor for private administrative work |
| treasury | – | 1 | ok | ok | **no** | Stone floor inconsistent with wood flooring in other rooms |
| unlabeled-room | – | 2 | **no** | **no** | **no** | Two rooms on upper floor lack labels, making their purpose unclear to players |
| upper-floor | – | 1 | ok | ok | ok | Corridor connects all ground floor rooms and provides access to great-hall |

#### ② Layout check, round 3

Scores (layout / placement / fit / quality): 4 / 3 / 5 / 4. The layout captures a two-story town hall but fails on critical stairwell alignment and lacks functional spaces like a kitchen; the most important fix is aligning the stairwells vertically and labeling all rooms.

- *error* — stairwell: Stairwells on ground and upper floors are in opposite corners (north-east vs south-east) and do not align vertically
- *error* — upper-floor: Two rooms on upper floor (north-west and north-center) have no labels
- *warning* — dining-room: Two rooms labeled 'dining-room' with no kitchen adjacent
- *warning* — study: Two rooms labeled 'study' without distinction
- *warning* — hall: Entrance 'hall' is far from 'great-hall', creating poor flow for a public building
- *warning* — layout: Upper floor dining-room is at far south-west, distant from stairs and kitchen

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| dining-room | – | 2 | **no** | **no** | **no** | Two identical labels confuse; upper one far from stairs and lacks kitchen |
| great-hall | – | 1 | **no** | **no** | **no** | Entrance at north-west forces long walk to great-hall at south |
| ground-floor | – | 1 | ok | ok | ok | Central corridor connects all ground floor rooms |
| hall | – | 1 | **no** | **no** | **no** | Entrance at north-west forces long walk to great-hall at south |
| stairwell | – | 2 | **no** | **no** | **no** | Ground floor stairwell at north-east, upper at south-east; they do not align vertically |
| storage | – | 1 | ok | ok | ok | Centrally placed near dining for serving |
| study | – | 2 | ok | ok | ok | Ground floor study near stairs is logical; upper study acceptable |
| treasury | – | 1 | ok | ok | ok | Upper floor north-east is secure and near stairs |
| upper-floor | – | 1 | ok | ok | ok | Central corridor connects all ground floor rooms |

#### ③ Plan check

Scores (layout / placement / fit / quality): 5 / 4 / 5 / 4. This looks like a procedural scatter of furniture with random lights and floating chairs, lacking the purposeful arrangement of a hand-crafted space. The most important fix is to remove the random lights and floating chairs, and arrange all furniture deliberately around tables and against walls.

- *error* — light: 12 lights scattered randomly throughout rooms instead of placed on walls or in specific spots
- *error* — chair: 7 chairs floating in rooms without tables, scattered randomly
- *error* — layout: two separate buildings with no visible connection or path between them
- *warning* — fireplace: fireplace in storage room is a fire hazard and wrong room type
- *warning* — chair: missing 7 chairs from brief (19 specified, 12 present)
- *warning* — table: missing 1 table from brief (3 specified, 2 present)
- *warning* — room: right building bottom-right room is empty except for a light
- *warning* — room: right building bottom-middle room has chair and two lights but no table or purpose
- *warning* — layout: no exterior doors visible, unclear how players enter
- *warning* — bed: bedrooms in a town hall are questionable for a public building

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| barrel | 5 | 4 | ok | ok | ok | clustered in storage rooms near shelves |
| bed | 2 | 2 | ok | **no** | ok | bedrooms in a public town hall are odd unless it's a residence |
| bookshelf | 2 | 2 | ok | ok | ok | against walls in study rooms |
| chair | 19 | 12 | **no** | ok | **no** | 7 chairs float in rooms without tables, scattered randomly |
| chest | 1 | 1 | ok | ok | ok | against wall in bedroom |
| crate | 2 | 1 | ok | ok | ok | in storage corner near shelves |
| decor | 2 | 2 | ok | ok | ok | placed in bedrooms for atmosphere |
| desk | 2 | 1 | ok | ok | ok | against wall with chair nearby |
| door | 13 | 12 | ok | ok | ok | correctly placed in walls connecting rooms |
| fireplace | 1 | 2 | **no** | ok | **no** | one in great hall is good, one in storage room is a fire hazard |
| light | 11 | 12 | **no** | ok | **no** | 12 lights scattered randomly on floor instead of walls or specific spots |
| nightstand | 1 | 1 | ok | ok | ok | correctly beside bed |
| rug | 1 | 1 | ok | ok | ok | under bed area |
| sack | 2 | 1 | ok | ok | ok | on floor in storage |
| shelf | 1 | 1 | ok | ok | ok | against wall in storage |
| shelf2 | 1 | 2 | ok | ok | ok | against wall in storage |
| stairs | 2 | 2 | ok | ok | ok | in hallway for vertical access |
| table | 3 | 2 | ok | ok | ok | in center of rooms with chairs around |
| wardrobe | 1 | 1 | ok | ok | ok | against wall in bedroom |
| window | 13 | 12 | ok | ok | ok | evenly spaced on exterior walls |

#### ⑤ Build check

Scores (layout / placement / fit / quality): 5 / 6 / 7 / 7. This is a passable but generic dungeon interior that captures the dark, oppressive mood through its palette and torchlight, but lacks the functional room definitions and careful furniture arrangements of a hand-crafted town hall. The most important fix is to clearly delineate the dining hall, study, and storage areas with proper walls and doors, and arrange the tables into proper dining sets with chairs on all sides.

- *error* — layout: Rooms are not functionally distinct - dining, study, bedroom, and storage areas appear as generic open halls with random furniture placement
- *error* — table: Dining tables lack proper chair arrangements - benches are missing from sides or placed randomly, not forming dining sets
- *warning* — ground: Floor is a flat, uniform brown surface without texture, cracks, or wear marks expected in a decaying town hall
- *warning* — bed: Beds are placed in open areas without enclosing walls, making them look like benches rather than bedroom furniture
- *warning* — wall-decor: Walls are bare except for torches; a town hall would have banners, notices, or shields
- *warning* — bookshelf: Bookshelves listed in placement are not visible or recognizable in the scene
- *warning* — lighting: Lighting is too uniform; lacks dark corners and dramatic shadows for the haunted mood
- *warning* — scene: The two floor areas float in a void with no environmental context (sky, adjacent buildings, or ceiling)

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| barrel | 5 | 4 | ok | ok | ok | clustered in corners and against walls, fits storage/clutter role |
| bed | 2 | 2 | **no** | **no** | **no** | placed in open areas without room enclosure, looks like benches |
| bookshelf | 2 | – | – | – | – |  |
| chair | 19 | 15 | ok | ok | ok | mostly placed around tables or against walls, not blocking paths |
| chest | 1 | 1 | ok | ok | ok | in corner, fits bedroom/storage |
| crate | 2 | 3 | ok | ok | ok | near barrels, fits storage room aesthetic |
| decor | 2 | 5 | ok | ok | ok | adds detail to walls |
| desk | 2 | 1 | ok | ok | ok | in corner with chair, suggests study area |
| door | 13 | 6 | ok | ok | ok | placed in walls between spaces, maintaining flow |
| fireplace | 1 | 1 | ok | ok | ok | central focal point in great hall area, fits town hall function |
| light | 11 | – | – | – | – |  |
| nightstand | 1 | – | – | – | – |  |
| rug | 1 | 1 | ok | ok | ok | under table, defines dining area |
| sack | 2 | 2 | ok | ok | ok | on floor near storage items, adds clutter |
| shelf | 1 | 2 | ok | ok | ok | against walls, appropriate for storage/study |
| shelf2 | 1 | – | – | – | – |  |
| stairs | 2 | 2 | ok | ok | ok | placed at edges, accessible, connecting the two floor areas |
| table | 3 | 4 | **no** | ok | ok | some tables float without proper chair arrangement; not all have seating on both sides |
| wall-block | – | 80 | ok | ok | ok | forms enclosed spaces with doors, though room functions are unclear |
| wall-torch | – | 10 | ok | ok | ok | forms enclosed spaces with doors, though room functions are unclear |
| wardrobe | 1 | – | – | – | – |  |
| window | 13 | 8 | ok | ok | ok | in outer walls, letting in dim light |

#### ⑤ Build check after the redo

Scores (layout / placement / fit / quality): 2 / 3 / 5 / 6. The pixel art assets are clean and the torch-lit, decaying mood suits the cursed town setting, but the layout fails as a town hall because the two floors are disconnected and lack internal walls to create the required rooms. The critical fix is to add internal walls dividing the spaces into distinct rooms and align the stairs between floors.

- *error* — layout: Two floor zones are disconnected islands floating in void with no aligned stairs connecting them
- *error* — layout: No internal walls create the rooms listed (bedroom, study, etc.); doors and furniture float in open halls
- *warning* — barrel: Storage items placed in dining/great hall instead of dedicated storage room
- *warning* — bed: Bedroom furniture listed (beds, rugs, wardrobes) not visible in the scene
- *warning* — scene: Floor zones float in dark void without environmental context

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| barrel | 5 | 3 | **no** | ok | ok | placed in dining/great hall area instead of storage room |
| bed | 2 | – | – | – | – |  |
| bookshelf | 2 | – | – | – | – |  |
| chair | 19 | 16 | ok | ok | ok | positioned around tables facing inward |
| chest | 1 | – | – | – | – |  |
| crate | 2 | 2 | **no** | ok | ok | mixed with dining furniture in open hall |
| decor | 2 | – | – | – | – |  |
| desk | 2 | – | – | – | – |  |
| door | 13 | 4 | **no** | ok | ok | float in open space without room walls to define doorways |
| fireplace | 1 | 1 | ok | ok | ok | against wall in large room, focal point |
| floor | – | 2 | ok | ok | ok | continuous texture without visible seams |
| light | 11 | 8 | ok | ok | ok | evenly spaced on walls, provides warm illumination |
| nightstand | 1 | – | – | – | – |  |
| rug | 1 | – | – | – | – |  |
| sack | 2 | – | – | – | – |  |
| shelf | 1 | 2 | **no** | ok | ok | placed in great hall instead of study or storage |
| shelf2 | 1 | – | – | – | – |  |
| stairs | 2 | 2 | **no** | ok | ok | not aligned between floors; appear disconnected |
| table | 3 | 5 | ok | ok | ok | placed in open floor areas with space around them |
| wall-block | – | 120 | ok | ok | ok | forms clear perimeter boundaries for both zones |
| wardrobe | 1 | – | – | – | – |  |
| window | 13 | 6 | ok | ok | ok | placed in perimeter walls appropriately |

### interior-r3

Place: `location/enforcers-office` (interior). Art: 21 pieces, 0 placeholders, 15 found pieces refused by the check.

| Step | layout / placement / fit / quality | errors | warnings |
|---|---|---|---|
| ② Layout | 5 / 6 / 7 / 4 | 1 | 4 |
| ③ Plan | 4 / 5 / 5 / 4 | 3 | 7 |
| ⑤ Build | 4 / 4.5 / 6 / 5 | 6 | 10 |
| ⑤ Build after the redo | 4.5 / 5 / 6.5 / 6.5 | 4 | 13 |

Layout check, round by round: 1: 4 / 3 / 3 / 3 (2 errors); 2: 4 / 4 / 4 / 3 (3 errors); 3: 5 / 6 / 7 / 4 (1 errors)

Images: [2-layout-1](../tests/quality/t2/interior-r3/2-layout-1.png) · [2-layout-2](../tests/quality/t2/interior-r3/2-layout-2.png) · [2-layout-3](../tests/quality/t2/interior-r3/2-layout-3.png) · [2-layout](../tests/quality/t2/interior-r3/2-layout.png) · [3-plan](../tests/quality/t2/interior-r3/3-plan.png) · [5-build-1](../tests/quality/t2/interior-r3/5-build-1.png) · [5-build-2](../tests/quality/t2/interior-r3/5-build-2.png) · [5-scene-detail](../tests/quality/t2/interior-r3/5-scene-detail.png) · [5-scene-floor-level-upper](../tests/quality/t2/interior-r3/5-scene-floor-level-upper.png) · [5-scene-floor-level](../tests/quality/t2/interior-r3/5-scene-floor-level.png) · [5-scene](../tests/quality/t2/interior-r3/5-scene.png) · [6-build-1](../tests/quality/t2/interior-r3/6-build-1.png) · [6-build-2](../tests/quality/t2/interior-r3/6-build-2.png) · [6-scene-detail](../tests/quality/t2/interior-r3/6-scene-detail.png) · [6-scene-floor-level-upper](../tests/quality/t2/interior-r3/6-scene-floor-level-upper.png) · [6-scene-floor-level](../tests/quality/t2/interior-r3/6-scene-floor-level.png) · [6-scene](../tests/quality/t2/interior-r3/6-scene.png)

![interior-r3 build](../tests/quality/t2/interior-r3/5-scene.png)

#### ② Layout check, round 1

Scores (layout / placement / fit / quality): 4 / 3 / 3 / 3. This is a functional but generic procedural grid that fails to capture the decaying, haunted atmosphere of Blackthorn Town Hall, feeling more like a modern office than a cursed medieval hall. The most critical fixes are adding stairs and a main entrance, then differentiating room sizes to support the story's faction conflict and imprisonment needs.

- *error* — floor-level: No stairs shown despite zone description mentioning them; upper/lower floors unreachable
- *error* — layout: No entrance door to the exterior; building is completely sealed
- *warning* — stone-room: Four identical large rooms provide no functional differentiation for gatherings vs authority
- *warning* — wood-room: Six narrow uniform strips lack guard post and feel like corridors not cells
- *warning* — layout: Overly rigid grid pattern lacks organic flow of hand-crafted decaying building
- *warning* — floor-level: No clear zoning between public hall (left) and prison (right) areas

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| corridor | – | 1 | 5/10 ok | – | – | Provides access to all rooms but is overly wide and straight, feeling procedural |
| floor-level | – | 1 | 3/10 **no** | 3/10 **no** | 3/10 **no** | Corridor connects all rooms but building has no entrance and no stairs to other floors |
| stone-room | – | 4 | 3/10 **no** | 4/10 ok | 2/10 **no** | Four identical large rooms lack hierarchy; no clear great hall vs offices |
| wood-room | – | 6 | 3/10 **no** | 4/10 ok | 2/10 **no** | Four identical large rooms lack hierarchy; no clear great hall vs offices |

#### ② Layout check, round 2

Scores (layout / placement / fit / quality): 4 / 4 / 4 / 3. This procedural layout is a generic grid of rooms that fails to evoke a decaying, haunted town hall, with critical flaws in room proportions and missing vertical circulation. The most important fix is to redesign the narrow right-side rooms and add the missing stairs and second zone.

- *error* — floor-level: No stairs present despite zone description requiring them for vertical circulation.
- *error* — floor-level: Right-side rooms are only 1-2 tiles wide, too narrow for offices or functional spaces.
- *error* — layout: Brief specifies 2 zones but only 1 floor is generated.
- *warning* — floor-level: No clear main entrance; entry is just a gap in the perimeter wall.
- *warning* — floor-level: Layout is overly symmetrical and grid-like, lacking hand-crafted character.
- *warning* — floor-level: Abrupt transition between stone and wood floors without architectural logic.

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| floor-level | – | 1 | 4/10 **no** | 4/10 **no** | 3/10 **no** | Ground floor position is correct for a town hall, but the internal layout features unusable narrow rooms on the right, lacks stairs, and ha… |

#### ② Layout check, round 3

Scores (layout / placement / fit / quality): 5 / 6 / 7 / 4. This is a functional but rigid procedural layout that captures the essential zones of a decaying town hall, yet lacks the organic spatial hierarchy and proper room sizing of a hand-crafted game space. The most important fix is to enlarge the cells area and add a proper entrance lobby.

- *error* — cells: Zone labeled 'cells' (plural) is too small for multiple jail cells.
- *warning* — layout: No entrance lobby or foyer; corridors lead directly to rooms without transitional space.
- *warning* — hall: Upper floor 'hall' label is confusing and duplicates ground floor 'great-hall' terminology.
- *warning* — great-hall: Brief requires dining-set but no kitchen is adjacent to serve the hall.
- *warning* — layout: Overly grid-based, symmetrical layout lacks hand-crafted organic flow.

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| cells | – | 1 | 4/10 **no** | 4/10 **no** | 3/10 **no** | Too narrow for plural 'cells'; oddly placed between throne-room and stairwell instead of secluded. |
| chamber | – | 2 | 7/10 ok | 7/10 ok | 5/10 ok | Private rooms on both floors; wood floor suits private quarters. |
| great-hall | – | 1 | 5/10 **no** | 5/10 **no** | 4/10 **no** | Label confusing (vs great-hall); position above great-hall suggests balcony but drawn as enclosed room. |
| hall | – | 1 | 5/10 **no** | 5/10 **no** | 4/10 **no** | Label confusing (vs great-hall); position above great-hall suggests balcony but drawn as enclosed room. |
| stairwell | – | 2 | 9/10 ok | 9/10 ok | 6/10 ok | Aligned vertically between floors at building edge; efficient circulation. |
| storage | – | 2 | 7/10 ok | 7/10 ok | 5/10 ok | Service rooms on both floors near stairs; practical placement. |
| throne-room | – | 1 | 8/10 ok | 8/10 ok | 5/10 ok | Front-left position suits authority; stone floor fits formal public space. |

#### ③ Plan check

Scores (layout / placement / fit / quality): 4 / 5 / 5 / 4. This procedural layout confuses room purposes, placing storage clutter in the throne room and mixing floor types across disconnected buildings. The critical fix is to clear the throne room of barrels and sacks, unify the flooring, and connect the two structures into one coherent town hall.

- *error* — throne-room: Throne room contains storage items (barrels, sacks, crates) instead of ceremonial furniture
- *error* — layout: Two separate buildings with void gap between them, not connected
- *error* — flooring: Mixed stone and wood floors in same building, breaks continuity
- *warning* — great-hall: Both carpet and rug present, redundant floor coverings
- *warning* — chair: Chairs scattered in great-hall not arranged around table
- *warning* — throne: Throne against side wall, not at far end of room
- *warning* — banner: Only one banner visible, should be two flanking throne
- *warning* — storage: Storage room contains desk and chair, mixing office with storage
- *warning* — light: Lights placed in uniform grid, not organic
- *warning* — rug: Rug not placed under table or fireplace

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| banner | 2 | 2 | 4/10 **no** | 7/10 ok | 4/10 **no** | Only one visible near throne; should flank throne symmetrically |
| barrel | 7 | 7 | 5/10 **no** | 4/10 **no** | 5/10 **no** | Crowding throne-room; should be in storage only |
| bookshelf | 3 | 3 | 7/10 ok | 8/10 ok | 7/10 ok | Against walls in office chambers, appropriate |
| carpet | 1 | 1 | 3/10 **no** | 6/10 ok | 3/10 **no** | In great-hall with rug also present; belongs in throne-room |
| chair | 8 | 8 | 4/10 **no** | 7/10 ok | 4/10 **no** | Scattered in great-hall, not around table; some floating |
| crate | 3 | 3 | 5/10 **no** | 4/10 **no** | 5/10 **no** | In throne-room with barrels; storage clutter in ceremonial space |
| decor | 6 | 6 | 6/10 ok | 7/10 ok | 6/10 ok | On walls as intended, adds worn detail |
| desk | 2 | 2 | 7/10 ok | 8/10 ok | 7/10 ok | With chair nearby, against wall in office |
| door | 13 | 13 | 7/10 ok | 8/10 ok | 7/10 ok | In walls between rooms, functional |
| fireplace | 1 | 1 | 8/10 ok | 9/10 ok | 8/10 ok | On wall in great-hall, focal point |
| light | 11 | 11 | 6/10 ok | 7/10 ok | 5/10 **no** | Too uniform, grid-like |
| rug | 1 | 1 | 4/10 **no** | 6/10 ok | 4/10 **no** | In great-hall but with carpet also; under no table |
| sack | 3 | 3 | 4/10 **no** | 4/10 **no** | 4/10 **no** | Piled in throne-room, wrong room type |
| shelf | 2 | 2 | 6/10 ok | 7/10 ok | 6/10 ok | Against walls in storage, appropriate |
| shelf2 | 4 | 4 | 6/10 ok | 7/10 ok | 6/10 ok | In storage, holds items |
| stairs | 2 | 2 | 6/10 ok | 7/10 ok | 6/10 ok | At building edges, connect floors |
| table | 1 | 1 | 7/10 ok | 8/10 ok | 7/10 ok | In dining area with chairs around |
| throne | 1 | 1 | 5/10 **no** | 8/10 ok | 5/10 **no** | Against side wall, not far end; room is cluttered |
| window | 16 | 16 | 7/10 ok | 7/10 ok | 7/10 ok | On exterior walls, appropriate |

#### ⑤ Build check

Scores (layout / placement / fit / quality): 4 / 4.5 / 6 / 5. This procedural build captures the dark, worn mood with recognizable assets, but the tiled floor, blocky walls, and misplaced furniture (throne off its carpet, table off its rug) prevent it from feeling hand-crafted. The most urgent fix is replacing the grid-based surfaces with continuous textures and correcting the throne room layout. This reads as a generic dungeon corridor rather than a hand-crafted town hall interior, lacking the required dining furniture and visible windows. The most important fix is adding tables with chairs for the dining set and rendering the windows, plus replacing m…

- *error* — floor: Floor has visible tile grid and seams instead of continuous surface
- *error* — wall-block: Walls are individual blocks with gaps, not whole wall pieces
- *error* — throne: Throne is not on its carpet and banners do not flank it
- *error* — table: Dining table is not on the rug; rug is misplaced by fireplace
- *warning* — chair: Several chairs sit on floating circular platforms disconnected from floor
- *warning* — decor: Wall shields are oversized and too bright for worn, oppressive mood
- *warning* — door: Nine doors create confusion; some block walkways or lack clear room connections
- *warning* — floor-clutter: Green patches read as graphical artifacts rather than intentional moss or debris
- *warning* — layout: Room zones bleed into each other without clear wall divisions
- *warning* — light: Light sources are scattered randomly rather than mounted on walls or as hearths
- *error* — dining-set: brief requires a dining-set but only a desk with one chair is present, no tables for dining
- *error* — window: 10 windows listed in placement data are not visible in the image

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| banner | 2 | 2 | 4/10 **no** | 7/10 ok | 7/10 ok | One banner far from throne, not flanking it symmetrically |
| barrel | 7 | 7 | 7/10 ok | 8/10 ok | 7/10 ok | Clustered in storage along walls, appropriate for purpose |
| bookshelf | 3 | 3 | 7/10 ok | 8/10 ok | 7/10 ok | In chamber near desk, fits office function |
| carpet | 1 | 1 | 3/10 **no** | 6/10 ok | 7/10 ok | Not under throne where it belongs |
| chair | 8 | 8 | 5/10 **no** | 7/10 ok | 6/10 ok | Several on floating circular platforms disconnected from floor |
| crate | 3 | 3 | 7/10 ok | 8/10 ok | 7/10 ok | In storage with barrels, appropriate |
| decor | 6 | 6 | 5/10 **no** | 4/10 **no** | 6/10 ok | Shields too large and prominent for worn common hall |
| desk | 2 | 2 | 8/10 ok | 8/10 ok | 7/10 ok | In chamber with chair, fits office purpose |
| door | 13 | 13 | 5/10 **no** | 7/10 ok | 7/10 ok | Too many doors, some placement unclear or blocking |
| fireplace | 1 | 1 | 8/10 ok | 8/10 ok | 8/10 ok | On wall in great-hall, provides warmth and light |
| floor | – | 1 | 2/10 **no** | 4/10 **no** | 2/10 **no** | Visible tile seams and grid pattern throughout |
| light | 11 | 11 | 5/10 **no** | 7/10 ok | 7/10 ok | Scattered randomly, not clearly wall-mounted or hearths |
| rug | 1 | 1 | 3/10 **no** | 6/10 ok | 7/10 ok | Not under table, misplaced by fireplace instead |
| sack | 3 | 3 | 7/10 ok | 8/10 ok | 8/10 ok | In storage with other goods, appropriate |
| shelf | 2 | 4 | 7/10 ok | 8/10 ok | 7/10 ok | In storage along walls, fits purpose |
| shelf2 | 4 | 2 | 7/10 ok | 8/10 ok | 7/10 ok | on wall in storage area |
| stairs | 2 | 2 | 8/10 ok | 7/10 ok | 7/10 ok | In hall, connects floors appropriately |
| table | 1 | 1 | 5/10 **no** | 7/10 ok | 8/10 ok | Not on rug, floating without proper grounding |
| throne | 1 | 1 | 3/10 **no** | 6/10 ok | 7/10 ok | Not on carpet, not centered at far end of room |
| wall-block | – | 1 | 3/10 **no** | 5/10 ok | 3/10 **no** | Individual blocks with gaps, not continuous wall pieces |
| window | 16 | 6 | 7/10 ok | 6/10 ok | 6/10 ok | Along upper walls, provides natural light |

#### ⑤ Build check after the redo

Scores (layout / placement / fit / quality): 4.5 / 5 / 6.5 / 6.5. This is a passable attempt at a dark town hall interior with good mood and consistent pixel art, but it suffers from visible floor tiling and illogical furniture placement that break the hand-crafted illusion. The most critical fix is removing the floor grid seams and properly defining the throne room as a separate space. This reads as a dungeon level rather than a town hall, with disconnected walls and missing the required dining area. The pixel art quality is decent and fits the dark mood, but the layout needs complete restructuring into proper adjacent rooms with shared walls.

- *error* — floor: visible tile grid seams on the floor, especially in the brown storage area
- *error* — layout: throne room opens directly into great hall without a separating wall or door
- *warning* — chair: chairs are scattered randomly, some floating in mid-room not facing anything
- *warning* — decor: heraldic shields are disproportionately large, almost throne-sized
- *warning* — table: dining table lacks a rug underneath as specified in goals
- *warning* — floor: abrupt color change from grey stone to brown dirt without architectural transition
- *warning* — light: some light sources appear to float without clear wall or floor attachment
- *error* — layout: Walls have gaps between rooms; rooms are not adjacent and sharing walls as required
- *error* — dining-set: Missing dining-set (tables with chairs) which is a required feature for this build type
- *warning* — layout: Layout resembles a dungeon corridor system rather than a town hall with distinct functional rooms
- *warning* — window: Windows appear on interior walls between rooms, which is illogical
- *warning* — floor-clutter: Floor clutter is too sparse; brief requires visible clutter

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| banner | 2 | 2 | 8/10 ok | 8/10 ok | 7/10 ok | flanking the throne symmetrically, appropriate for a throne room |
| barrel | 7 | 7 | 7/10 ok | 8/10 ok | 7/10 ok | lined up against storage wall, fits the storage purpose |
| bookshelf | 3 | 3 | 7/10 ok | 7/10 ok | 7/10 ok | in chamber near desk, logical for an office |
| carpet | 1 | 1 | 7/10 ok | 7/10 ok | 6/10 ok | under throne area, defines the royal space |
| chair | 8 | 8 | 4/10 **no** | 6/10 ok | 6/10 ok | scattered randomly, some floating, not all facing tables or walls |
| crate | 3 | 4 | 7/10 ok | 8/10 ok | 7/10 ok | against walls in storage, appropriate |
| decor | 6 | 6 | 6/10 ok | 7/10 ok | 5/10 ok | on walls as heraldic decor, but shields are very large |
| desk | 2 | 2 | 5/10 **no** | 7/10 ok | 7/10 ok | Isolated in corner without room enclosure; not forming a proper office |
| door | 13 | 13 | 5/10 **no** | 8/10 ok | 7/10 ok | Some doors lead to gaps or unclear spaces due to wall discontinuities |
| fireplace | 1 | 1 | 9/10 ok | 9/10 ok | 8/10 ok | on great hall wall with rug and chairs before it, perfect hearth setup |
| floor | – | 1 | 4/10 **no** | 4/10 **no** | 3/10 **no** | Too sparse; brief requires floor-clutter but only minimal debris visible |
| floor-clutter | – | 5 | 4/10 **no** | 4/10 **no** | 3/10 **no** | Too sparse; brief requires floor-clutter but only minimal debris visible |
| light | 11 | 7 | 7/10 ok | 8/10 ok | 7/10 ok | providing warm light in dark rooms, fits mood |
| rug | 1 | 1 | 8/10 ok | 7/10 ok | 6/10 ok | in front of fireplace, defines seating area |
| sack | 3 | 3 | 7/10 ok | 8/10 ok | 7/10 ok | on floor in storage, appropriate clutter |
| shelf | 2 | 4 | 7/10 ok | 7/10 ok | 7/10 ok | on storage wall, holding items |
| shelf2 | 4 | 2 | 7/10 ok | 7/10 ok | 7/10 ok | on storage wall, holding items |
| stairs | 2 | 3 | 7/10 ok | 7/10 ok | 7/10 ok | in corner, leading to next floor, standard placement |
| table | 1 | 1 | 7/10 ok | 7/10 ok | 7/10 ok | in center of great hall, but lacks rug underneath |
| throne | 1 | 1 | 8/10 ok | 8/10 ok | 7/10 ok | at far end of throne room, focal point |
| wall-block | – | 40 | 4/10 **no** | 7/10 ok | 7/10 ok | Walls have gaps between rooms; segments float disconnected; rooms not adjacent as required |
| wall-torch | – | 8 | 4/10 **no** | 7/10 ok | 7/10 ok | Walls have gaps between rooms; segments float disconnected; rooms not adjacent as required |
| window | 16 | 16 | 5/10 **no** | 6/10 ok | 7/10 ok | Some appear on interior walls between rooms, which is illogical |

### settlement-r0

Place: `location/dungeon-of-whispers-depths` (settlement). Art: 15 pieces, 1 placeholders, – found pieces refused by the check.

| Step | layout / placement / fit / quality | errors | warnings |
|---|---|---|---|
| ③ Plan | 3 / 2 / 5 / 2 | 4 | 4 |
| ⑤ Build | 1.75 / 1.75 / 0.75 / 2.75 | 18 | 11 |

Images: [3-plan](../tests/quality/t2/settlement-r0/3-plan.png) · [5-build-1](../tests/quality/t2/settlement-r0/5-build-1.png) · [5-build-2](../tests/quality/t2/settlement-r0/5-build-2.png) · [5-build-3](../tests/quality/t2/settlement-r0/5-build-3.png) · [5-build-4](../tests/quality/t2/settlement-r0/5-build-4.png) · [5-scene-detail](../tests/quality/t2/settlement-r0/5-scene-detail.png) · [5-scene](../tests/quality/t2/settlement-r0/5-scene.png)

![settlement-r0 build](../tests/quality/t2/settlement-r0/5-scene.png)

#### ③ Plan check

Scores (layout / placement / fit / quality): 3 / 2 / 5 / 2. This reads as a procedurally generated strategy game map rather than a hand-crafted narrative space, with grid-placed trees and randomly scattered props destroying the illusion of a lived-in, cursed frontier town. The most critical fix is to establish a clear town heart with a market square and redistribute all natural elements with irregular, purposeful placement that suggests age, ruin, and oppression.

- *error* — tree: Trees placed in a perfect grid in the cursed-thorn-forest, looking procedural and artificial
- *error* — layout: No clear town center, market square, or heart; settlement lacks focal point and flow
- *error* — building: Buildings not aligned to streets with doors facing roads; scattered without urban logic
- *error* — props: Grass, flowers, bushes, and rocks scattered randomly without purpose or grouping
- *warning* — water: River has hard, pixelated edges instead of soft, natural banks
- *warning* — stall: Only one stall in the entire settlement; no market area
- *warning* — plot boundary: Purple lines look like debug overlays rather than in-game fences
- *warning* — grass (A): Individual grass sprites with labels look like debug markers

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| bridge | – | 1 | ok | – | – | Functionally crosses the river connecting slum to forest areas |
| building | 16 | 16 | **no** | ok | **no** | Slum shacks are scattered without clear street frontage or orientation; docks warehouses are isolated blocks; no visible doors facing roads |
| bush | 204 | 204 | **no** | – | – | Random distribution throughout; no hedgerows, undergrowth clusters, or purposeful groupings |
| flower | 212 | 212 | **no** | – | **no** | Random scattering with no purpose; not in garden beds, borders, or meaningful clusters |
| grass | 446 | 446 | **no** | – | **no** | Scattered randomly as individual labeled sprites rather than continuous ground cover; looks like debug markers |
| mound | 13 | 13 | **no** | **no** | – | Random placement with unclear purpose; doesn't read as graves, ruins, or meaningful terrain |
| pier | 2 | 2 | ok | – | – | Extends into water at docks, appropriate location for a frontier trading post |
| plot boundary | – | 1 | **no** | – | **no** | Purple lines look like debug overlays or CAD markings, not in-game fences |
| road | – | 1 | **no** | – | **no** | Grid-like and blocky; looks like placed tiles rather than worn paths through grass |
| rock | 31 | 31 | **no** | – | – | Scattered randomly; not forming rocky outcrops, river banks, or ruin foundations |
| stall | 1 | 1 | **no** | **no** | – | Single stall isolated in docks with no market context; a town this size needs a market square |
| tree | 1473 | 1473 | **no** | ok | **no** | Placed in a perfect grid in the cursed-thorn-forest, looking procedural and artificial; too uniform and dense with no clearings |
| water | – | 1 | **no** | – | **no** | River has hard, pixelated edges; lacks soft banks, shallows, or natural irregularity |

#### ⑤ Build check

Scores (layout / placement / fit / quality): 1.75 / 1.75 / 0.75 / 2.75. This looks like a generic asset pack dump rather than a hand-crafted cursed frontier town. The most critical fix is replacing the crates with actual ruined shacks and recoloring the entire scene to match the dark, oppressive palette of blackened thorns and rot. This is not a settlement but a randomly scattered forest with three floating sprite previews; it completely fails to match the brief's ruined, cursed frontier town aesthetic. The most critical fix is to remove the white-boxed building placeholders, clear the random trees to create a proper town layout with roads and a plaza, and recolo…

- *error* — building: The 5 'shacks' are actually wooden crates/boxes, not buildings
- *error* — tree: Trees are placed on visible square tiles, breaking immersion
- *error* — scene: The palette is completely wrong - bright greens and pinks instead of dark, oppressive colors
- *error* — layout: The forest is a rigid grid of trees, not a natural woodland
- *warning* — ground: The ground shows visible tile repetition/seams
- *warning* — water: The river has hard, artificial edges
- *warning* — flower: Pink cherry blossoms don't fit the cursed, dying world theme
- *error* — building: Three buildings are rendered as white boxes with sprite previews floating over the scene, not placed in the world.
- *error* — layout: The scene is a dense forest/orchard with 129 randomly scattered trees, not a settlement with clearings, roads, and a heart.
- *error* — scene: The palette and mood are completely wrong: bright greens, pinks, and browns instead of dark, blackened, oppressive tones.
- *error* — ground: The ground is a flat brown polygon with no roads, paths, or texture variation.
- *error* — tree: Trees are healthy green deciduous trees, not blackened thorns or cursed vegetation, and many float on the black void.

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| building | 16 | 16 | **no** | **no** | **no** | These are crates, not shacks; placed in a loose cluster near the river without paths, fences, or context |
| bush | 204 | 204 | **no** | **no** | **no** | Bushes are scattered randomly without purposeful grouping in gardens or yards. |
| crate | – | 3 | **no** | **no** | **no** | Floating in black void on far right edge, not placed on ground plane; appears disconnected and broken. |
| flower | 212 | 212 | **no** | **no** | **no** | Pink cherry blossoms are scattered randomly and don't fit the cursed theme |
| grass | 446 | 446 | **no** | **no** | **no** | Grass tufts are scattered randomly without forming lawns, paths, or defined areas. |
| ground | – | 2 | **no** | **no** | **no** | The ground is a flat brown polygon with sharp edges against black void; no roads, paths, or texture variation. |
| modern house | – | 2 | **no** | **no** | **no** | Floating in the top right corner on white boxes; completely wrong style and not integrated into the scene |
| mound | 13 | 13 | **no** | **no** | – | Randomly scattered on open ground with no contextual purpose or grouping. |
| pier | 2 | 2 | **no** | **no** | **no** | Two pier structures appear as wooden crates on dry land at the terrain edge, not extending over water or arranged as functional docks. |
| river | – | 1 | ok | ok | ok | Forms a natural boundary on the left; banks have some vegetation |
| rock | 31 | 31 | **no** | **no** | **no** | Rocks are placed randomly without purpose as boundary markers or ruins. |
| stall | 1 | 1 | **no** | **no** | **no** | Isolated in the bottom right with no road or market square context |
| tree | 1473 | 1473 | **no** | **no** | **no** | Placed in a rigid grid pattern in the forest and on visible square tiles; lacks natural variation and clearings |
| water | – | 2 | **no** | **no** | **no** | Hard-edged banks with no soft transition to land; too bright blue |

### settlement-r2

Place: `location/dungeon-of-whispers-depths` (settlement). Art: 19 pieces, 0 placeholders, 17 found pieces refused by the check.

| Step | layout / placement / fit / quality | errors | warnings |
|---|---|---|---|
| ② Layout | 4 / 3 / 6 / 4 | 3 | 4 |
| ③ Plan | 3 / 3 / 5 / 2 | 4 | 8 |
| ⑤ Build | 2.25 / 2 / 1.75 / 3.5 | 15 | 20 |
| ⑤ Build after the redo | 2 / 2.5 / 2 / 3.5 | 16 | 20 |

Layout check, round by round: 1: 4 / 4 / 5 / 3 (4 errors); 2: 4 / 3 / 5 / 4 (2 errors); 3: 4 / 3 / 6 / 4 (3 errors)

Images: [2-layout-1](../tests/quality/t2/settlement-r2/2-layout-1.png) · [2-layout-2](../tests/quality/t2/settlement-r2/2-layout-2.png) · [2-layout-3](../tests/quality/t2/settlement-r2/2-layout-3.png) · [2-layout](../tests/quality/t2/settlement-r2/2-layout.png) · [3-plan](../tests/quality/t2/settlement-r2/3-plan.png) · [5-build-1](../tests/quality/t2/settlement-r2/5-build-1.png) · [5-build-2](../tests/quality/t2/settlement-r2/5-build-2.png) · [5-build-3](../tests/quality/t2/settlement-r2/5-build-3.png) · [5-build-4](../tests/quality/t2/settlement-r2/5-build-4.png) · [5-scene-detail](../tests/quality/t2/settlement-r2/5-scene-detail.png) · [5-scene](../tests/quality/t2/settlement-r2/5-scene.png) · [6-build-1](../tests/quality/t2/settlement-r2/6-build-1.png) · [6-build-2](../tests/quality/t2/settlement-r2/6-build-2.png) · [6-build-3](../tests/quality/t2/settlement-r2/6-build-3.png) · [6-build-4](../tests/quality/t2/settlement-r2/6-build-4.png) · [6-scene-detail](../tests/quality/t2/settlement-r2/6-scene-detail.png) · [6-scene](../tests/quality/t2/settlement-r2/6-scene.png)

![settlement-r2 build](../tests/quality/t2/settlement-r2/5-scene.png)

#### ② Layout check, round 1

Scores (layout / placement / fit / quality): 4 / 4 / 5 / 3. This layout has the right zones in the wrong places: the cursed forest should dominate the center, not the edge, and the slum should be outside the wall, not enclosed. The gate is a road crossing rather than a ward, and the entire place lacks the dense, organic plotting of a hand-crafted village.

- *error* — cursed-thornwood: Placed on the east edge instead of the center as the brief requires
- *error* — rotten-warren: Enclosed by a wall, contradicting the brief that it is outside the wall
- *error* — gate: Placed on the road/bridge rather than as a distinct ward with buildings
- *error* — rotten-warren: Located west of cursed-thornwood, not south as specified in the brief
- *warning* — layout: Zones are oversized and empty; a village should have smaller, denser plots
- *warning* — gate: No market square or central plaza exists for the town's heart
- *warning* — stable-yard: Stable-yard and inn-plot are disconnected fragments, not a cohesive gate ward
- *warning* — river: The river has an unnatural diagonal jog in the middle

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| black-docks | – | 1 | ok | ok | – | Correctly on the river inside the wall, north of gate, serving smuggling |
| cursed-thornwood | – | 1 | **no** | **no** | – | On east edge, not center as brief requires; should dominate the site's heart |
| dock-road | – | 1 | ok | ok | – | Correctly on the river inside the wall, north of gate, serving smuggling |
| gate | – | 1 | **no** | **no** | – | On the bridge/road, not a distinct ward; stable-yard and inn-plot are disconnected fragments |
| inn-plot | – | 1 | **no** | – | – | Floating near stable-yard, not fronting the main road |
| main-road | – | 1 | ok | ok | – | Connects docks to warren via bridge, forming town spine |
| river | – | 1 | ok | ok | – | North-south flow separates zones, bridge at gate enables crossing |
| rotten-warren | – | 1 | **no** | **no** | – | Walled despite being 'outside the wall'; also not south of cursed-thornwood |
| stable-yard | – | 1 | **no** | – | – | Isolated northwest, disconnected from gate ward and docks |
| wall | – | 1 | **no** | – | – | Encloses rotten-warren which should be outside; contradicts slum description |
| yard | – | 1 | **no** | – | – | Isolated northwest, disconnected from gate ward and docks |

#### ② Layout check, round 2

Scores (layout / placement / fit / quality): 4 / 3 / 5 / 4. The layout has the right zones but places the slum on the wrong side of the river, severing its connection to the gate, and suffers from unnaturally regular geometry. Move rotten-warren to the west bank adjacent to the gate and reshape the forest and roads to be more organic.

- *error* — rotten-warren: Zone is east of river; brief requires west near gate.
- *error* — gate: Separated from black-docks by river; not adjacent as required.
- *warning* — cursed-thornwood: Circular boundary is unnatural for a forest.
- *warning* — layout: Roads are straight and grid-like, lacking organic flow.
- *warning* — stable-yard: Large unplanned zone not in brief, disproportionate to gate.
- *warning* — rotten-warren: Appears enclosed by wall but should be outside.

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| black-docks | – | 1 | ok | ok | ok | Along river, inside wall, north of slum. Correct for smuggling docks, but gate is across river. |
| cursed-thornwood | – | 1 | ok | ok | **no** | Perfectly circular shape is unnatural; forests should have irregular, dense edges. |
| gate | – | 1 | **no** | ok | ok | West of river, separated from black-docks by water. Brief requires adjacency to docks. |
| rotten-warren | – | 1 | **no** | **no** | **no** | East of river, but brief requires west near gate. Separated from gate by river and docks. |
| stable-yard | – | 1 | **no** | **no** | **no** | Large zone south-west not in brief. Gate ward should be compact with stables inside. |

#### ② Layout check, round 3

Scores (layout / placement / fit / quality): 4 / 3 / 6 / 4. The layout captures the cursed frontier town concept with its forest, slum, and docks, but fails on critical spatial relationships: the wall excludes the docks, and the zones are arranged side-by-side rather than north-south as planned. The rigid grid roads betray the hand-crafted, organic goal. Most importantly, extend the wall to enclose black-docks and reposition the zones so black-docks sits north of rotten-warren inside the fortification.

- *error* — wall: The wall encloses only rotten-warren, leaving black-docks outside, but the brief requires both zones inside the fortification.
- *error* — black-docks: Black-docks is positioned south-west of rotten-warren, but the brief specifies it should be north of rotten-warren.
- *error* — rotten-warren: Rotten-warren is inside the wall, but the module describes slums as being outside the walls or at the edge.
- *warning* — black-docks: Roads form a rigid grid pattern, contradicting the organic, ruined aesthetic of the brief.
- *warning* — layout: No market square or central plaza exists at the heart of the town as required by the design goals.
- *warning* — cursed-thornwood: Forest edges are pixelated and uniform, lacking the dense, varied copses and soft banks of a hand-crafted wood.
- *warning* — gate: The gate ward is too small and cramped to contain an inn, stables, and market stalls as described.

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| black-docks | – | 1 | **no** | ok | **no** | Outside the wall (should be inside), and south-west of rotten-warren (should be north of it). |
| bridge | – | 1 | – | – | – |  |
| cursed-thornwood | – | 1 | ok | ok | **no** | Forest edges are pixelated and tile-based, not the soft, hand-painted banks and dense varied woods requested. |
| gate | – | 1 | ok | ok | – | Positioned at the river crossing between the two zones, serving as the entry point near black-docks as intended. |
| river | – | 1 | ok | – | – | Runs north-south separating the zones, enabling the smuggling route central to the story. |
| road | – | 2 | ok | – | – | Main road connects the zones via the bridge, facilitating trade and movement. |
| rotten-warren | – | 1 | **no** | ok | **no** | Enclosed by the wall, but the brief places it outside or at the edge; also positioned east of river while brief says west (though brief con… |
| wall | – | 1 | **no** | – | – | Only encloses rotten-warren; the brief requires it to enclose both black-docks and rotten-warren to contain the curse. |

#### ③ Plan check

Scores (layout / placement / fit / quality): 3 / 3 / 5 / 2. This is a clearly procedural layout with rigid grids, scattered noise props, and no central heart, far from the hand-crafted organic village of Anno or Stronghold. The most critical fix is replacing the grid of identical shacks with an organic arrangement around a central market square, varying building sizes, and clustering props purposefully.

- *error* — layout: No central market square or plaza - town lacks a heart
- *error* — building: Rotten-warren buildings in rigid 4x3 grid, not organic
- *error* — building: All 16 buildings identical size and style, no hierarchy
- *error* — stall: Only 2 stalls scattered randomly, no market
- *warning* — tree: 456 trees scattered as noise rather than forest clusters
- *warning* — road: Straight grid roads, not worn dirt paths
- *warning* — prop: Rocks, bushes, flowers scattered randomly as noise
- *warning* — landmark: No unique landmarks (sign, statue, gallows)
- *warning* — plot: No fences around gardens/plots
- *warning* — water: Hard-edged river banks
- *warning* — mound: Mounds scattered in forest, not arranged as graveyard
- *warning* — zone: Missing fields/farmland outside core

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| building | 16 | 16 | **no** | **no** | **no** | Rigid grid in rotten-warren, identical sizes, no hierarchy, doors unclear |
| bush | 213 | 223 | **no** | – | – | 223 scattered randomly across map |
| flower | 265 | 267 | **no** | – | – | 267 scattered as noise on roads and forest |
| grass | 405 | 431 | **no** | – | – | 431 tufts scattered everywhere as noise |
| mound | 6 | 7 | ok | ok | – | 7 mounds in cursed forest fit graveyard theme |
| pier | 2 | 2 | ok | ok | – | 2 piers at docks fit smuggling purpose |
| road | – | 1 | **no** | – | **no** | Straight tile-grid paths, not worn dirt |
| rock | 21 | 24 | **no** | – | – | 24 rocks scattered randomly |
| stall | 2 | 2 | **no** | **no** | – | Only 2 scattered, not forming a market |
| tree | 353 | 456 | **no** | ok | **no** | 456 scattered as noise, not clustered woods or copses |
| water | – | 1 | ok | – | **no** | Flat blue with hard tile edges |
| zone black-docks | – | 1 | **no** | ok | – | Grid layout too regular for slums |
| zone cursed-thornwood | – | 1 | **no** | ok | – | Grid layout too regular for slums |
| zone gate | – | 1 | **no** | ok | – | Grid layout too regular for slums |
| zone rotten-warren | – | 1 | **no** | ok | – | Grid layout too regular for slums |

#### ⑤ Build check

Scores (layout / placement / fit / quality): 2.25 / 2 / 1.75 / 3.5. This is a procedural scatter of bright, cheerful assets that completely miss the dark, cursed atmosphere of Blackthorn Hollow. The sprite sheets visible in the world are a critical error, and the lack of roads, plaza, or building arrangement makes this feel like a test scene rather than a hand-crafted village. This reads as a random scatter of nature assets rather than a hand-crafted frontier village, with bright pink flowers destroying the cursed atmosphere. The most critical fix is recoloring the flora to match the dark, oppressive palette and arranging buildings into a proper street layout…

- *error* — sprite-sheet: Four white boxes with building sprites visible in world bottom-left.
- *error* — flower: Bright pink/orange flowers clash with gritty cursed mood.
- *error* — layout: No roads, plaza, or building arrangement; random scatter.
- *error* — building: Only 1 building visible; brief requires 16 for village.
- *warning* — tree: Trees scattered randomly instead of dense woods at edges.
- *warning* — river: Water too bright blue for oppressive atmosphere.
- *warning* — grass: Bright green grass doesn't fit dying world.
- *warning* — ground: Possible tile seams in dirt texture.
- *error* — flower: Bright pink flowers clash violently with the dark, cursed, oppressive mood and palette
- *error* — layout: Buildings scattered randomly with no streets, square, or village structure
- *warning* — ground: Uniform dirt lacks the cracked cobblestones and worn roads specified in the brief
- *warning* — flower: Flowers scattered randomly rather than in purposeful garden groups

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| building | 16 | 18 | **no** | **no** | **no** | Only one building visible; brief requires 16. Not fronting any road. |
| bush | 213 | 213 | **no** | **no** | **no** | Scattered randomly instead of dense copses at edges. |
| fence | – | 1 | **no** | – | – | repetitive border, not purposeful plot fencing |
| flower | 265 | 265 | **no** | **no** | **no** | Random scatter; no gardens or purposeful groups. |
| grass | 405 | 405 | **no** | **no** | **no** | Random scatter; should form worn paths and clearings. |
| ground | – | 2 | **no** | **no** | **no** | flat brown texture, not continuous painted ground with roads |
| mound | 6 | 6 | ok | ok | ok | Placed naturally near water. |
| pier | 2 | 2 | **no** | **no** | ok | Isolated in water without supporting dock, boats, or fishing props |
| river | – | 2 | ok | **no** | ok | Too bright blue for oppressive mood. |
| rock | 21 | 21 | **no** | ok | ok | Random placement without purpose |
| sprite-preview | – | 2 | **no** | **no** | **no** | Editor artifacts visible in world. |
| sprite-sheet | – | 4 | **no** | **no** | **no** | Editor artifacts visible in world. |
| stall | 2 | 2 | **no** | **no** | ok | Floating in open ground, not at market square. |
| tree | 353 | 353 | **no** | **no** | ok | Scattered randomly; should form woods at edges. |
| water | – | 1 | **no** | **no** | – | hard edge, not soft banks |

#### ⑤ Build check after the redo

Scores (layout / placement / fit / quality): 2 / 2.5 / 2 / 3.5. This is a procedural test with critical rendering errors (white UI boxes) and a completely wrong color palette that makes a cursed, oppressive town look like a cheerful village. The most important fix is removing the white sprite boxes and recoloring the entire scene to dark, blackened tones with dead trees and murky water. This reads as a procedural scatter of assets rather than a hand-crafted settlement, lacking organization and mood coherence. The most critical fix is establishing a central plaza with proper roads and recoloring the vegetation to match the dark, cursed palette. This looks …

- *error* — building (sprite card): White rectangular boxes with building sprites floating in scene, looks like UI leaked into render
- *error* — scene: Wrong color palette: bright green and blue instead of dark, blackened, oppressive tones
- *error* — layout: No settlement structure: no roads, no plaza, buildings and props scattered randomly
- *warning* — tree (green): Lush green trees don't fit cursed, dying world of blackened thorns
- *warning* — bush: Green bushes instead of blackened thorn-choked vegetation
- *warning* — water: Bright blue water breaks gritty, rainy, oppressive mood
- *warning* — ground: Uniform dirt without worn paths, mud, or cracked cobblestones
- *warning* — building (shack): Too clean and intact; should show rot, damage, and ruined state
- *error* — ground: Visible tile grid seams break continuous painted ground illusion
- *error* — layout: No central square or organized street plan; buildings scattered randomly
- *error* — tree: Bright green trees clash with dark, cursed, dying world palette
- *error* — bush/flower/grass: Vegetation scattered randomly without purposeful grouping

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| building | 16 | 22 | **no** | **no** | **no** | isolated by water, no road access, not in settlement cluster |
| bush | 213 | 128 | **no** | **no** | ok | random scatter, not forming hedgerows or thorn barriers |
| fence | – | 2 | **no** | ok | ok | Repeating tile pattern along left edge, looks artificial |
| flower | 265 | 134 | **no** | **no** | ok | randomly scattered, inappropriate for cursed mood |
| grass | 405 | 229 | **no** | **no** | ok | uniform patches, no worn paths or variation |
| mound | 6 | 6 | **no** | ok | ok | random placement, not marking graves or ruins |
| pier | 2 | 2 | ok | **no** | ok | looks too functional/clean for a ruined, lawless dock |
| rock | 21 | 21 | **no** | ok | ok | random scatter without purpose |
| sprite-sheet | – | 2 | **no** | **no** | **no** | UI element placed in world space |
| stall | 2 | 2 | **no** | **no** | ok | isolated, not in market square or grouped with other stalls |
| tree | 353 | 332 | **no** | **no** | ok | scattered randomly, not forming woods at edges or lining roads |
| tree-bare | 353 | 14 | **no** | **no** | ok | scattered randomly, not forming woods at edges or lining roads |
| tree-green | 353 | 10 | **no** | **no** | ok | scattered randomly, not forming woods at edges or lining roads |
| water | – | 3 | **no** | **no** | ok | Hard geometric edge, no soft bank, isolated on right with no connection to town |

### settlement-r3

Place: `location/dungeon-of-whispers-depths` (settlement). Art: 30 pieces, 0 placeholders, 44 found pieces refused by the check.

| Step | layout / placement / fit / quality | errors | warnings |
|---|---|---|---|
| ② Layout | 4 / 5 / 5 / 4 | 2 | 6 |
| ③ Plan | 4 / 3 / 5 / 3 | 3 | 7 |
| ⑤ Build | 3.5 / 3.25 / 1.5 / 3.75 | 17 | 16 |
| ⑤ Build after the redo | 4 / 4.75 / 2.25 / 4.75 | 16 | 24 |

Layout check, round by round: 1: 3 / 3 / 4 / 5 (4 errors); 2: 3 / 2 / 3 / 3 (4 errors); 3: 4 / 5 / 5 / 4 (2 errors)

Images: [2-layout-1](../tests/quality/t2/settlement-r3/2-layout-1.png) · [2-layout-2](../tests/quality/t2/settlement-r3/2-layout-2.png) · [2-layout-3](../tests/quality/t2/settlement-r3/2-layout-3.png) · [2-layout](../tests/quality/t2/settlement-r3/2-layout.png) · [3-plan](../tests/quality/t2/settlement-r3/3-plan.png) · [5-build-1](../tests/quality/t2/settlement-r3/5-build-1.png) · [5-build-2](../tests/quality/t2/settlement-r3/5-build-2.png) · [5-build-3](../tests/quality/t2/settlement-r3/5-build-3.png) · [5-build-4](../tests/quality/t2/settlement-r3/5-build-4.png) · [5-scene-detail](../tests/quality/t2/settlement-r3/5-scene-detail.png) · [5-scene](../tests/quality/t2/settlement-r3/5-scene.png) · [6-build-1](../tests/quality/t2/settlement-r3/6-build-1.png) · [6-build-2](../tests/quality/t2/settlement-r3/6-build-2.png) · [6-build-3](../tests/quality/t2/settlement-r3/6-build-3.png) · [6-build-4](../tests/quality/t2/settlement-r3/6-build-4.png) · [6-scene-detail](../tests/quality/t2/settlement-r3/6-scene-detail.png) · [6-scene](../tests/quality/t2/settlement-r3/6-scene.png)

![settlement-r3 build](../tests/quality/t2/settlement-r3/5-scene.png)

#### ② Layout check, round 1

Scores (layout / placement / fit / quality): 3 / 3 / 4 / 5. This layout fails to establish the cursed Hollow as the town's heart and misorients the river and slum, breaking the smuggling narrative. The most critical fix is moving the Hollow to the center and rotating the river to flow east-west.

- *error* — hollow: Cursed forest at bottom-right edge instead of center
- *error* — blackthorn-river: River flows north-south instead of east-west
- *error* — slum: Slum at western edge instead of eastern near docks
- *error* — main-road: Road perpendicular to river instead of parallel
- *warning* — market-ward: Market at upper-right instead of center
- *warning* — docks: Docks south of Hollow instead of east
- *warning* — residential-ward: Does not surround market ward

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| beds garden | – | 3 | 7/10 ok | 7/10 ok | 7/10 ok | Gardens in residential plots |
| blackthorn-river | – | 1 | 1/10 **no** | 2/10 **no** | 2/10 **no** | Should flow east-west, flows north-south |
| docks | – | 1 | 4/10 **no** | 5/10 ok | 5/10 ok | Should be east of Hollow on river bank, is south |
| front-yard | – | 4 | 7/10 ok | 7/10 ok | 7/10 ok | Houses front streets correctly |
| hollow | – | 1 | 2/10 **no** | 2/10 **no** | 4/10 **no** | Should be central cursed heart, placed at edge |
| main-road | – | 1 | 3/10 **no** | 4/10 **no** | 4/10 **no** | Should parallel river, is perpendicular |
| market-square | – | 1 | 5/10 **no** | 6/10 ok | 6/10 ok | Should be center near Hollow, at upper-right edge |
| market-ward | – | 1 | 5/10 **no** | 6/10 ok | 6/10 ok | Should be center near Hollow, at upper-right edge |
| residential-ward | – | 1 | 5/10 **no** | 6/10 ok | 6/10 ok | Should be center near Hollow, at upper-right edge |
| ruined-inn yard | – | 1 | 7/10 ok | 7/10 ok | 7/10 ok | Houses front streets correctly |
| secondary-road | – | 3 | 3/10 **no** | 4/10 **no** | 4/10 **no** | Should parallel river, is perpendicular |
| shack-cluster-1 | – | 1 | 3/10 **no** | 3/10 **no** | 6/10 ok | In wrong slum location |
| shack-cluster-2 | – | 1 | 3/10 **no** | 3/10 **no** | 6/10 ok | In wrong slum location |
| shop-general shop-alchemy | – | 1 | 8/10 ok | 9/10 ok | 8/10 ok | Shops fit lawless trade |
| slum | – | 1 | 1/10 **no** | 2/10 **no** | 3/10 **no** | Should be eastern edge near docks, at western edge |
| stable-yard | – | 1 | 7/10 ok | 7/10 ok | 7/10 ok | Houses front streets correctly |
| well-plaza | – | 2 | 8/10 ok | 8/10 ok | 8/10 ok | Appropriate communal spaces |

#### ② Layout check, round 2

Scores (layout / placement / fit / quality): 3 / 2 / 3 / 3. This procedural layout fails to establish the cursed Hollow as the town's heart and misplaces the docks entirely off the water, breaking the smuggling narrative. The most critical fix is relocating the Hollow to the center, rotating the river to flow east-west, and placing the docks on the riverbank with the slum adjacent.

- *error* — docks: Docks are placed inland, not on the river bank as required.
- *error* — hollow: The cursed forest is in the corner instead of the center of the site.
- *error* — blackthorn-river: River flows north-south, but the brief requires east-west flow.
- *error* — slum: Slum is not near the docks; they are separated by the market-ward.
- *warning* — layout: Road network is too grid-like and regular for a ruined, organic frontier town.
- *warning* — market-ward: Missing ruined inns mentioned in the brief.
- *warning* — hollow: Hollow is too small to read as a 'dense forest'.
- *warning* — residential-ward: Layout is too regular; should be organic and decaying.
- *warning* — slum: Missing shack-cluster-1 (brief implies two clusters).
- *warning* — scene: Missing unique landmarks (town sign, statue, gallows).

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| beds | – | 3 | – | – | – |  |
| blackthorn-river | – | 1 | 2/10 **no** | – | – | Brief requires east-west flow, but river runs north-south. |
| docks | – | 1 | 1/10 **no** | 0/10 **no** | – | Must be on the river bank (rule: along parent.water), but placed inland far from water. |
| front-yard | – | 3 | – | – | – |  |
| garden | – | 3 | – | – | – |  |
| hollow | – | 1 | 2/10 **no** | 2/10 **no** | – | Should be the central cursed heart of the site, but placed in the corner. |
| main-road | – | 1 | 4/10 **no** | – | – | Should connect docks to market ward; currently connects to market but not to docks (which are misplaced). |
| market-square | – | 1 | 5/10 **no** | 6/10 ok | – | Correctly on the main road, but isolated from the Hollow it should loom over. |
| market-ward | – | 1 | 4/10 **no** | 4/10 **no** | – | Correctly on the main road, but isolated from the Hollow it should loom over. |
| residential-ward | – | 1 | 4/10 **no** | 4/10 **no** | – | Correctly on the main road, but isolated from the Hollow it should loom over. |
| secondary-road | – | 1 | 4/10 **no** | – | – | Should connect docks to market ward; currently connects to market but not to docks (which are misplaced). |
| shack-cluster-2 | – | 1 | – | – | – |  |
| shop-alchemy | – | 1 | – | – | – |  |
| shop-general | – | 1 | – | – | – |  |
| slum | – | 1 | 3/10 **no** | 3/10 **no** | – | Should be at the eastern edge near the docks; currently southeast, separated from docks by market-ward. |
| well-plaza | – | 2 | – | – | – |  |

#### ② Layout check, round 3

Scores (layout / placement / fit / quality): 4 / 5 / 5 / 4. This layout has the right zones in roughly the right places but fails on the main road orientation and the Hollow's central placement, resulting in a generic grid-like village rather than an organic, cursed frontier town. The most critical fix is rerouting the main road to run parallel to the river, connecting the docks to the market as the smuggling artery, and moving the Hollow to the center.

- *error* — main-road: Runs perpendicular to river instead of parallel, failing to connect docks to market ward as the smuggling route
- *error* — hollow: Placed on west edge instead of center; should be the cursed heart surrounded by town
- *warning* — roads: Grid pattern with right angles contradicts 'organic' layout and 'worn paths' goal
- *warning* — market-square: Disproportionately small for the town's functional heart and trading hub
- *warning* — residential-ward: Does not surround market-ward as intended; only to the south
- *warning* — slum: Zone too large for two shack clusters, creating empty unused space
- *warning* — plots: Front-yards and gardens too uniform in size and shape
- *warning* — layout: No agricultural fields outside the core as per design goals

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| beds0 garden | – | 3 | 6/10 ok | 6/10 ok | 5/10 ok | Small beds in yards, appropriate for poor town |
| blackthorn-river | – | 1 | 8/10 ok | 8/10 ok | 7/10 ok | Flows north-south with bridge, separating hollow from town |
| docks | – | 1 | 8/10 ok | 8/10 ok | 6/10 ok | Correctly on river's east bank at north, serving as smuggling hub |
| front-yard | – | 4 | 6/10 ok | 5/10 ok | 5/10 ok | Near market and inn, logical for mercenaries |
| hollow | – | 1 | 3/10 **no** | 4/10 **no** | 5/10 ok | On west edge, but brief requires 'in-center' as cursed heart surrounded by town |
| main-road | – | 1 | 2/10 **no** | 3/10 **no** | 3/10 **no** | Runs east-west perpendicular to river; should run north-south parallel connecting docks to market |
| market-square | – | 1 | 4/10 **no** | 5/10 **no** | 4/10 **no** | Too small and rectangular for the town's functional heart |
| market-ward | – | 1 | 4/10 **no** | 5/10 **no** | 4/10 **no** | South of market is logical, but doesn't surround it; brief said 'west of Hollow' (impossible) |
| residential-ward | – | 1 | 5/10 **no** | 6/10 ok | 5/10 ok | South of market is logical, but doesn't surround it; brief said 'west of Hollow' (impossible) |
| ruined-inn | – | 1 | 7/10 ok | 7/10 ok | 6/10 ok | Near market square, fits traveler/mercenary theme |
| secondary-road | – | 1 | 2/10 **no** | 3/10 **no** | 3/10 **no** | Runs east-west perpendicular to river; should run north-south parallel connecting docks to market |
| shack-cluster | – | 2 | 7/10 ok | 7/10 ok | 6/10 ok | Two clusters at slum edge near docks, logical for smuggling access |
| shop-alchemy | – | 1 | 7/10 ok | 7/10 ok | 6/10 ok | Alchemy and general shops at market edge, accessible |
| shop-general | – | 1 | 7/10 ok | 7/10 ok | 6/10 ok | Alchemy and general shops at market edge, accessible |
| slum | – | 1 | 7/10 ok | 7/10 ok | 5/10 ok | At eastern edge near docks, housing the desperate and lawless |
| stable-yard | – | 1 | 6/10 ok | 5/10 ok | 5/10 ok | Near market and inn, logical for mercenaries |
| well-plaza | – | 2 | 7/10 ok | 7/10 ok | 6/10 ok | Two plazas serve market and residential areas well |

#### ③ Plan check

Scores (layout / placement / fit / quality): 4 / 3 / 5 / 3. This is a procedural scatter on a zone map, not a hand-crafted cursed village. Remove the random noise of rocks and grass, add clear roads with houses fronting them, and transform the hollow into a blackened thorn-choked forest.

- *error* — rock/mound/grass/flower/bush: Props scattered randomly like noise instead of purposeful groups
- *error* — building: Buildings don't front streets with doors to road
- *error* — hollow: Forest lacks cursed, thorn-choked, blackened appearance
- *warning* — market-ward: Market square empty except centrepiece; stalls not in rows
- *warning* — layout: No fences around plots
- *warning* — docks: Piers lack boats and crates
- *warning* — building: No size hierarchy (manor vs house vs shack)
- *warning* — site: Missing unique landmarks like town sign or gallows
- *warning* — water: River banks are hard-edged
- *warning* — road: No clear main road connecting wards

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| bench | 2 | 2 | 8/10 ok | 8/10 ok | 7/10 ok | Near wells in plazas, purposeful |
| building | 16 | 16 | 4/10 **no** | 5/10 ok | 4/10 **no** | Do not front streets with doors to road; no size hierarchy |
| bush | 225 | 239 | 2/10 **no** | – | 2/10 **no** | Random scatter, not thickets or borders |
| centrepiece | 1 | 1 | 9/10 ok | 8/10 ok | 7/10 ok | Unique element in market square centre |
| crop | 7 | 6 | 7/10 ok | 7/10 ok | – | In garden beds behind houses, correct |
| flower | 259 | 240 | 2/10 **no** | – | 2/10 **no** | Scattered everywhere instead of in garden beds |
| grass | 446 | 507 | 2/10 **no** | – | 2/10 **no** | Uniform scatter creates visual noise, not worn paths |
| item | 1 | 1 | 8/10 ok | – | – | Beside door as doorstep item |
| mound | 9 | 14 | 2/10 **no** | 4/10 ok | 2/10 **no** | Scattered in forest instead of forming a graveyard |
| pier | 2 | 2 | 7/10 ok | 7/10 ok | – | Correctly along water in docks |
| plant | 7 | 9 | 7/10 ok | 7/10 ok | – | In residential gardens, appropriate |
| rock | 27 | 33 | 2/10 **no** | 4/10 ok | 2/10 **no** | Scattered randomly like noise, not clustered at ruins or graves |
| sign | 4 | 4 | 7/10 ok | – | – | Placed beside doors as intended |
| stall | 6 | 6 | 4/10 **no** | 6/10 ok | 4/10 **no** | Not arranged in rows facing each other in market square |
| tree | 539 | 820 | 6/10 ok | – | 5/10 ok | Dense in hollow forest but too uniform and green |
| well | 2 | 2 | 8/10 ok | 8/10 ok | 7/10 ok | Correctly placed in centre of plazas |

#### ⑤ Build check

Scores (layout / placement / fit / quality): 3.5 / 3.25 / 1.5 / 3.75. This looks like a generic bright fantasy meadow, not the cursed, gritty frontier town of Blackthorn Hollow. The most critical fix is removing the white-background sprite and repainting the entire scene with the dark, blackened, thorn-choked palette while adding actual buildings and settlement structure. This reads as a generic fantasy meadow, not a cursed frontier town—the bright greens and random scatter betray the gritty, oppressive mood. Remove the white-box sprite, repaint the ground in dark thorn-choked earth tones, and cluster buildings around a proper market square. This reads as a gen…

- *error* — building-sprite: Building on white background at bottom center, not integrated into scene
- *error* — palette: Entire scene uses bright, cheerful colors instead of dark, cursed, blackened palette
- *error* — layout: No settlement structure - just forest and river, missing buildings, roads, and market
- *error* — tree: Trees scattered randomly instead of forming dense woods at edges
- *warning* — props: Rocks and bushes scattered randomly without purpose
- *warning* — vegetation: Missing thorns and blackened, cursed plant life
- *warning* — river: Water too bright blue for oppressive mood
- *error* — building: Sprite on white background at left edge (sprite sheet artifact)
- *error* — ground: Visible tile seams/grid pattern in grass
- *error* — scene: Bright green palette contradicts gritty, cursed, oppressive mood
- *error* — tree: Green deciduous trees instead of blackened thorns
- *warning* — layout: No clear heart (market square/well) or defined wards

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| bench | 2 | 2 | 8/10 ok | 5/10 ok | 7/10 ok | Correctly placed near well in plaza |
| building | 16 | 17 | 0/10 **no** | 2/10 **no** | 0/10 **no** | Floating on white background, not integrated into isometric scene |
| building-sprite | 16 | 1 | 0/10 **no** | 2/10 **no** | 0/10 **no** | Floating on white background, not integrated into isometric scene |
| bush | 225 | 225 | 3/10 **no** | 3/10 **no** | 5/10 ok | Random scattering, not forming hedges or garden boundaries |
| centrepiece | 1 | 1 | 6/10 ok | 5/10 ok | 6/10 ok | Centered in market square |
| crop | 7 | 7 | – | – | – |  |
| fence | – | 1 | 7/10 ok | 5/10 ok | 7/10 ok | Encloses house yard appropriately |
| flower | 259 | 259 | 3/10 **no** | 2/10 **no** | 7/10 ok | Scattered randomly, not in garden plots or meaningful clusters |
| grass | 446 | 446 | 3/10 **no** | 2/10 **no** | 3/10 **no** | Random tufts, not forming meadows or path edges |
| ground | – | 1 | 3/10 **no** | 1/10 **no** | 3/10 **no** | Paths don't connect to buildings; visible tile seams |
| item | 1 | 1 | – | – | – |  |
| mound | 9 | 9 | 5/10 ok | 5/10 ok | 7/10 ok | Natural earth humps, acceptable but could be more purposeful |
| pier | 2 | 2 | 7/10 ok | 5/10 ok | 6/10 ok | Correctly along water edge |
| plant | 7 | 7 | – | – | – |  |
| river | – | 2 | 6/10 ok | 3/10 **no** | 6/10 ok | Bright blue water too cheerful for oppressive, rainy atmosphere |
| rock | 27 | 27 | 3/10 **no** | 5/10 ok | 7/10 ok | Scattered randomly with no purpose, not grouped at paths or building sites |
| sign | 4 | 4 | 8/10 ok | 5/10 ok | 7/10 ok | Placed beside doors |
| stall | 6 | 6 | 5/10 **no** | 4/10 **no** | 7/10 ok | Clustered bottom-left but not in a square or in front of shops |
| tree | 539 | 539 | 2/10 **no** | 1/10 **no** | 4/10 **no** | Scattered randomly across entire map instead of dense woods at edges with clearings for settlement |
| well | 2 | 2 | 8/10 ok | 4/10 **no** | 7/10 ok | Stone well but light colored, not blackened or cursed looking |

#### ⑤ Build check after the redo

Scores (layout / placement / fit / quality): 4 / 4.75 / 2.25 / 4.75. This is a procedural nature scatter on a tiled grid, not a hand-crafted cursed village. The bright green palette and healthy trees completely contradict the gritty, paranoid brief for Blackthorn Hollow. Replace the tiled ground with continuous muddy earth and recolor everything to the dark, thorn-choked palette. This looks like a generic bright meadow with scattered props, not a cursed frontier town. The white sprite-sheet box behind the left building and the tiled grid ground are immediate errors; the entire palette needs to shift to dark, muddy, blackened tones with thorn-choked vegetation.…

- *error* — ground: visible tile grid seams, not continuous painted ground
- *error* — palette: bright green colors contradict dark muddy brief
- *error* — mood: peaceful nature scene, not gritty cursed town
- *error* — trees: healthy green foliage, not blackened thorns
- *error* — buildings: only 2 of 16 buildings visible, no village center
- *error* — layout: no market square or plaza
- *warning* — props: rocks and bushes scattered randomly
- *warning* — river: hard straight banks, not soft
- *warning* — fences: no plots or gardens defined
- *warning* — house: bottom structure appears cut off
- *error* — building: Left building has white sprite-sheet box behind it
- *error* — ground: Visible tile grid/seams in grass texture

| Item | In the layout (PLL) | Seen | Placement | Fit | Quality | Why (what's wrong first) |
|---|---|---|---|---|---|---|
| bench | 2 | 2 | 8/10 ok | 6/10 ok | 8/10 ok | Placed beside well in plaza as seating |
| building | 16 | 17 | 3/10 **no** | 2/10 **no** | 2/10 **no** | isolated at edge, not clustered with roads |
| bush | 225 | 225 | 4/10 **no** | 3/10 **no** | 4/10 **no** | random scatter, not in copses or hedgerows |
| centrepiece | 1 | 1 | – | – | – |  |
| crop | 7 | 7 | 8/10 ok | 4/10 **no** | 8/10 ok | Neat vegetable gardens too orderly for lawless frontier town |
| fence | – | 1 | 4/10 **no** | – | – | cut off at edge, incomplete |
| flower | 259 | 259 | 5/10 **no** | 3/10 **no** | 5/10 **no** | scattered without garden context |
| grass | 446 | 446 | 5/10 **no** | 2/10 **no** | 4/10 **no** | random tufts, not defining paths or plots |
| ground | – | 2 | 4/10 **no** | 1/10 **no** | 2/10 **no** | Paths form artificial cross/grid; visible tile seams; wrong color |
| house | – | 1 | 4/10 **no** | – | 4/10 **no** | floating at bottom, unclear connection |
| item | 1 | 1 | 7/10 ok | 6/10 ok | 7/10 ok | Placed by doorstep of a house appropriately |
| mound | 9 | 9 | 7/10 ok | 6/10 ok | 8/10 ok | placed naturally on ground |
| pier | 2 | 2 | 7/10 ok | – | – | Along water edge correctly, but lacks props |
| plant | 7 | 7 | 8/10 ok | 4/10 **no** | 8/10 ok | Healthy plants don't fit dying, cursed world setting |
| river | – | 1 | 6/10 ok | 3/10 **no** | – | clean blue water, not murky cursed |
| rock | 27 | 27 | 4/10 **no** | 5/10 ok | 5/10 ok | scattered randomly without purpose |
| sign | 4 | 4 | – | – | – |  |
| stall | 6 | 6 | 5/10 **no** | 2/10 **no** | 6/10 ok | Clustered bottom-left but no market square or plaza; not fronting a road properly |
| tree | 539 | 539 | 4/10 **no** | 2/10 **no** | 4/10 **no** | dense uniform forest lacks variation and clearings |
| water | – | 1 | – | 4/10 **no** | – | Too bright blue, should be dark and murky for cursed atmosphere |
| well | 2 | 2 | 5/10 **no** | 6/10 ok | 7/10 ok | Plaza disconnected from road network, floating in grass |

