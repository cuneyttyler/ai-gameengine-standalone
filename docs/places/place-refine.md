# Refining a built place: assets, ground and the final look

Status: **2026-10-21**. Written from a quality check of the whole Build pipeline (Confirm,
Layout, Plan, Assets, Result) on a real request, judged against a hand-made 2D game of the 90s,
then against Age of Empires II. The runs, their pictures and the measurements are in
`docs/test.md`. Each section says what is **done** and what is **open**.

The bar: a 90s map looks good with a few hundred hand-drawn tiles because every piece was drawn
by the same hands, for the same grid, at the same scale, under the same light, and because its
ground doesn't show its cells. A built place failed that bar not because of its layout (the
layout steps are sound) but because of its **art and how the art was used**.

---

## 0. What was wrong

| # | Problem | Where | Effect on screen | |
|---|---|---|---|---|
| 1 | One ground tile, one turn, on every cell | `BuildPlaceScene` | a carpet; the grid shows | done §1 |
| 2 | Generated tiles framed and lit from the middle | the image model | a dark smudge on every cell | done §1 |
| 3 | Grounds meet in hard, stair-stepped edges | per-cell tiles | a board game, a square river | done §1, §6 |
| 4 | Every generated prop one cell wide | the critic's `cells` | a bench as wide as a cottage | done §2 |
| 5 | Fixed sprite heights | `SendArt` | pixel densities that differ | done §2 |
| 6 | "a single {need's name}" | `images.py`, `SendArt` | one flower; a desk lamp; a door for a sign | done §2, §8 |
| 7 | No shared art direction; "pixel art" with nothing about the era | `SendArt` | a collage | done §3 |
| 8 | Ground colour drifts run to run | the image model | mustard grass | done §3 |
| 9 | Nothing casts a shadow | — | things float | done §3 |
| 10 | The critic never checks the view | critic | a top-down well in an isometric scene | done §4 |
| 11 | Fence pieces never made; when made, drawn the wrong way, or a box | `PlanPlaceAssets`, critic | grey bars; X's; little boxes | done §5 |
| 12 | A fence round every 2-cell bed | the compiler | clutter | done §5 |
| 13 | Water never generated | `PlanPlaceAssets` | a flat blue ribbon | done §6 |
| 14 | Trees in small copses only | the landscape module | no woods | done §7 |
| 15 | Flat land | — | no relief | done §10 |
| 16 | Brief features as strings fail validation | `PlaceValidate`, `Planner` | the build stops at Plan | done §9 |
| 17 | Branch roads routed over the main road | `Compile` | the north and south roads missing | done §9 |

---

## 1. The ground: painted, not tiled

**Done.** The ground of a place (every terrain made by the Art tool, a placeholder, or a pack's
plain tile) is painted at build time into pictures of 16 × 16 cells (`PaintGround`,
`engine/editing/PlaceScene.cpp`), and each cell's tile is its square of the picture (a UV
range on the tile's sprite). Nothing else changes: the tile layer, saving, the editor's tools.
A terrain with transition tiles of its own (a pack's grass-to-dirt edges) keeps them, fitted
per cell by the autotiler.

- **No repeat.** Each terrain's texture is sampled at world position and spans about four
  cells (tiles are generated at 256 px); slow noise mixes it with itself turned and moved
  (texture bombing), and a slow change of light runs across the place.
- **Soft, ragged borders.** Where it looks up its ground, each point is moved by noise, and
  four looks round it are averaged.
- **Even light.** `flatten_tile` (`ai_service/app/sprites.py`, asked for with `evenLight`)
  trims a tile's frame and takes out its slow change of light, keeping its fine detail. Asking
  the model for "low contrast, even light" made tiles worse (measured); the post-process works.

Open: detail tiles (a stone, a clover patch) at a low rate; worn verges along roads; ground
under things (packed earth round a house, a ring of stones round a well).

## 2. Size

**Done.** Each role in `data/places/roles.json` has `size` (cells across), `tall` (height for
its width) and `draw` (what to draw: "a small loose clump of wildflowers among their green
leaves, low to the ground, with no pot and no planter", not "flower"); `Ontology::RoleSize`,
`RoleTall` and `RoleDraw` walk the broader roles. Generated art is drawn at a height that follows
the size, and the critic writes the role's size into the catalog: a bench 0.6 cells, a lantern
0.25, a tree 1.2. Measured: 12 of 19 props off by more than 40% before, none after.

Open: one scale check over the whole place, for web and pack art too.

## 3. One look: direction, palette, shadows

**Done.**

- **Art direction.** `PlaceBuild::ArtDirection()` adds the same words to every piece: a 16-bit
  1990s RPG pixel-art look, soft daylight from the upper left, a warm, muted, earthy palette,
  gentle shading, a darker outline, plus the Brief's style. A thing is drawn "standing alone
  with no square of ground, base or plinth under it".
- **Palette lock.** Each painted ground is moved most of the way to its terrain's colour
  (`LockPalette`: its average shifted, its detail kept): grass the same muted green whatever
  the model drew.
- **Shadows.** Trees, bushes, wells, stalls and buildings cast soft shadows to the lower right,
  painted into the ground. They don't follow a thing moved by hand until the next build.

Open: quantising every sprite to one palette; a one-pixel dark outline; a reference image for
the image model (the first approved piece as the style for the rest).

## 4. View

**Done.** The critic fails a building or thing drawn from straight above in an isometric scene,
and a ground tile drawn in perspective; the reason goes into the next prompt.

Open: facing (an isometric house faces down-left or down-right); mirrored facings instead of a
second drawing.

## 5. Fences, walls, hedges

**Done.**

- **Made.** A line with no piece set in the project gets one section from the Art tool: "one
  straight section of wooden fence, one cell long, running diagonally ... a single straight run
  only: not a closed square, not an enclosure".
- **Measured.** The critic measures the section's shape (`PieceKind`): anything but one straight
  diagonal run (`straight-x`, `straight-y`) fails, and is drawn again with the reason. The image
  model often draws a little box; that is now caught.
- **Filed by its direction.** The section is filed by the way it is actually drawn (its long
  axis), not the way it was asked for; the other direction is it mirrored, so the two match.
- **Fitted.** A made section spans the cell's edge (half a tile across) and stands on the middle
  of its slanting foot.
- **Placed as a yard.** A garden by a house fences the plot (one fence, open at the front), not
  each bed; a garden of its own is fenced at 3 × 3 and more; a fence inside a fenced plot isn't
  drawn.

Open: corner posts where a line turns; gates as pieces; thicker palisades for a camp or a
castle.

## 6. Water and shapes

**Done.** Water is generated like any ground ("calm blue-green river water with small, gentle
ripples, only water"). Land beside it turns to sand and the water beside the land is shallower.
Natural grounds (water, grass, roads) take smooth shapes: each point's ground is weighed from the
cells round it, so a river laid cell by cell becomes a curve; fields and squares keep their
straight edges.

Open: moving water (two or three frames), foam at the shore; the pond as ground (the layout asks
for it as an object of role `water-terrain`, so it stays a placeholder).

## 7. Woods

**Done.** The landscape module's wild land has `woods`: stands of 40 to 180 cells, 90% filled,
seeded nearest the site's edge first (`cluster.prefer: "edge"`), on a forest floor
(`cluster.terrain: "forest-floor"`, a terrain the Art tool makes like any other). Every natural
thing (trees, bushes, flowers, rocks) is moved a little in its cell, mirrored half the time and
scaled ±12%, so three hundred copies of one tree don't look stamped.

Open: two or three tree kinds per wood; a darker edge of undergrowth round a wood.

## 8. Titles and descriptions

**Done.** Generation draws from the role's `draw` words, not the need's name: a "lamp" is "a
street lantern on a wooden post", a "sign" "a small wooden signpost".

Open: search words for the web from the same `draw` words and the place's style, shown in the
Assets step.

## 9. Layout

**Done.** Brief features are read as strings or objects; a branch road runs from its side of the
site to the middle (and walls leave it a gate).

Open: the layout reports relations it didn't meet and doesn't try again; a second pass moving
only those zones (`RelayoutMoving`) would fix most.

## 10. High ground

**Done.**

- **The land's height.** `MakeHills` gives each cell corner a height: low rolling hills in the
  wild land, level where it's built, a road, water or a field, rising gently away from them.
- **Drawn.** A sprite has a `lift` (raised by so many pixels) and a flat sprite four corner lifts
  (`cornerLift`), applied in `SpriteQuadOf`, so both the GPU renderer and the preview bend each
  tile over the land; things stand at the height under their foot. Saved with the tile layer
  (`"corners"`) and in reflection (`Sprite.lift`).
- **Shaded.** Slopes facing the light are lighter and the far side darker, painted into the
  ground.
- **Edited.** The editor's **Height** tool (toolbar: Raise, Lower, Level, a radius) moves the
  ground's corners round the cursor with a soft falloff; what stands there rides with it; one
  undo step per stroke for the ground.

- **Walked.** Moving characters stand at the height under their feet (eased). A cell much higher or lower than the one
  the player stands on (a step of more than 14 px) is a cliff and can't be walked up or down, except onto or off a
  cell with a **Ramp** (stairs, a ramp, a ladder: the builder's stairs pieces get one; add it in the Inspector).

Open: cliff faces drawn (a sharp step with its side); characters (NPCs) obeying cliffs on their paths; picking a
raised cell under the cursor (it picks the flat cell).

## 11. In the ontology and the Build panel

**Done.** The outdoor build types' root modules (settlement, farmland, wilderness, camp, castle,
region) have two new asked fields, shown in the Build panel's Confirm step with the others and
read by the interpreter from the request:

| Field | Values | Effect |
|---|---|---|
| `relief` | flat, gentle (default), hilly | the hills' height: 0, 36, 60 px (`HillsFor`) |
| `woods` | none, few, some (default), dense | the woods' density: 0, 0.08, 0.2, 0.38 of the wild land |

The forest floor is a terrain kind (`vocabularies.json`); the ontology's published files
(`ontology/gbo-*.ttl`) are regenerated.

## 12. Dungeons and caves: the Diablo pass

The bar: Diablo (1996) and its kind. A dungeon reads by its **light**: dark, warm pools round its
torches, walls throwing hard shadows, a beam through a doorway; by its **walls**: stone standing
behind the floor, the ones in front cut away so the rooms show; by **big rooms** joined by real
corridors; and by its dressing (pillars, braziers, bones, coffins).

| Before (`tests/quality/dungeon-before/`) | Done |
|---|---|
| flat, evenly bright; no light at all | indoor light: a cold dark, each light's warm pool, walls stopping it (a ray per point, four to a cell); torches along the back walls where a room has too few lights |
| walls as flat dark ground | a standing wall block on each wall cell behind the floor (the Art tool makes it); front walls cut away |
| tan sandstone, a sand "shore" along the rock (the shore took rock for water) | a cold grey floor and near-black rock (their terrain's tone); a shore only by water |
| small rooms, short corridors | dungeon rooms 1.6 × bigger, the levels 1.5 ×; rooms farther apart (a 3-cell margin, 1.3 × their size + 4 between) |
| doors facing the viewer, two kinds of two sizes | a door turned across its doorway (the corridor's way, against its picture's measured axis); one size and look for both kinds |
| shadows only under trees and houses | every standing thing casts one, its role's size, in every kind of place (indoors close under it) |

Side by side: `tests/quality/compare-dungeon.png`. Open: the wall block came out squat (a
battlement, not a wall 1.5 cells tall); the rock between rooms shows as cobbles (it should be
near black); caves (blob rooms) want rock edges and stalagmites; the replay is slow (78 s).

## 13. Generic: the ontology says how things look

None of the above names a terrain, a role or a build type in the engine. They say it in the
ontology, and the scene reads it (the asset plan's `look`, written by `PlanPlaceAssets`):

| Where | Field | Used for |
|---|---|---|
| a role (`roles.json`, inherited by `broader`) | `size`, `tall` | its picture's size, its shadow's |
| | `look.vary` | moved, mirrored and resized a little where it stands (plants, rocks) |
| | `look.light {radius, strength}` | gives light indoors (a lamp, a torch) |
| | `look.doorway` | turned across its doorway (a door) |
| a terrain kind (`vocabularies.json`) | `look.tone` | its colour (the palette lock) |
| | `look.made` | straight edges (a field, a floor) |
| | `look.water`, `look.shore` | a shore by it; what a shore is (sand) |
| | `look.solid`, `look.blocksLight` | its tiles block the way; it stops light (a wall) |
| | `look.hills` | the land rises there |
| a build type (`buildtypes.json`) | `look.lighting` | `sun` or `indoor` |
| | `look.parts [{key, role, at}]` | what it stands up: on each wall edge (`wall-edge`), where its lights are (`lights`) |

So a user's new terrain (lava with `tone`, `blocksLight` false, `water` false), a new light (a
brazier role with `look.light`) or a new build type (a mine with `lighting: indoor` and its own
`parts`) shows as its data says, with no engine change.

## 14. The art brief: what to draw, for this place

The image model was given a sentence ("a street lantern on a wooden post", "a door"), the same in
a village and a crypt. Now a piece is drawn in two steps (`ai_service/app/art_brief.py`):

1. The text model writes the brief, given the piece's **context**: the place (its name, build
   type, style, description), the role and its words, the part it's for, the other pieces of the
   place, the view and the way it **runs** (a door in a wall running diagonally, a fence section),
   its size and height for its width, the shared art direction, and what went wrong last time. It
   chooses the subject that fits the place, and writes its materials, colours, view, proportions
   and framing (alone, no ground patch, no cast shadow; a thing hung on a wall without the wall).
2. The image model draws from the brief; the brief is kept in the image's `.gen.json` (`artBrief`).

Measured on the dungeon (`tests/quality/dungeon-after2/`): the room light became "a wall-mounted
iron torch bracket with a lit torch" (was a pink street lantern), the doors "heavy iron-bound crypt
doors in stone arches", the wall a grey stone block. The critic now also fails a piece that runs
along a line (a door, a fence section) drawn facing the viewer in an isometric scene, so it's drawn
again with that reason. Indoors the dark is lighter (0.42 of daylight), so the unlit floor and
what's on it still show.
