# Matched art: a place's art made to fit together

Status: **design v1 (2026-10-26)**. Replaces cutting assets out of a painting of the whole place (overlaps and drift made the cuts unreliable) and the Painted method.

**The problem.** The Assembled method makes each asset alone (Mistral or PixelLab, from words). Each looks fine by
itself, but on the grid they don't belong together: different palettes, outlines and light, random sizes, slightly
different angles, and pieces that must join (a wall and its door) never seen side by side.

**The solution: one pipeline, any art provider.**
1. Every asset is made from shared images and our own geometry, not from words alone.
2. Cutting, scaling and checking are done by our code, the same for every provider.
3. Which provider draws is a Preference:
   - **BFL** (a model: FLUX.2 klein, FLUX.3 Image, FLUX.2 pro);
   - **PixelLab**;
   - **Mistral**.

**The model to keep in mind: a brief to a human designer.** We give the art provider what we'd give a designer: the
place and its mood (from the request and the Brief), the list of assets each sheet holds, which of them work together
(a door and its wall, §2a), and how each should stand. The designer paints them on one sheet so they match; we cut,
fit and place them on the grid. The background is the same: the plan's ground handed over as a map to be painted ([background.md](background.md)).

## 1. Terms

| Term | Meaning |
|---|---|
| **need** | what the plan needs art for: a role with a footprint (an office tower 4×4, a car, a lamp post), from the asset plan (`PlanPlaceAssets`) |
| **box** | a need drawn as a block-out at its exact grid size, in our iso projection, its height from the ontology (`PlaceBlockout`) |
| **sheet** | several boxes laid out apart on a flat key-colour background, each position known |
| **run** | a box three cells long painted as one continuous wall, cut into cell pieces (§2b) |
| **style anchor** | the place's look, made once: the first sheet's painted pieces, their palette, and their look in words |

## 2. Steps

1. **Needs.** The asset plan lists each need: its role, footprint and layer, the object that shows it, and the ground
   it stands on in the place (the layout's terrain under its footprint).
2. **Style sheet.** Boxes of a few representative needs (the place's biggest kinds of building, its most used
   small things) are painted first, apart on the key colour. Its items are found (each blob on the key colour),
   named (the vision model matches them to the kinds) and checked. Its best building and its best small thing are
   the **examples**: the look everything else is made in.
3. **A small-thing example** is made alone, close up, if the style sheet had no good one.
4. **Buildings** are painted in batches (up to 6 on a sheet, far apart, at true scale), the example building as
   their reference.
5. **Small things** are never painted on a sheet: drawn at their true size they get too little detail, and scaled
   up their box looks like a building's. Each is an **edit of the example small thing** ("turn this into a lamp
   post"): it keeps the example's angle, close-up framing, finish and clean separation.
6. **Fix.** Every piece is checked once (the vision model: one thing, of its kind, cleanly separated). A wrong one
   is edited from its example again, the reason said. Pieces the user marks are remade the same way, in batches
   (`remake`).
7. **Cut and fit.** The key colour is keyed out (soft edges, its tint taken off), specks dropped. Each piece is
   scaled so its base spans its footprint's width on screen and set where the builder stands it, marked
   `drawnToGrid`.
8. **Lock the palette.** Each piece is pulled towards the style sheet's palette (softly).
9. **Ground.** A sheet of terrain patches, each unwarped to a seamless texture.
10. **Catalog and build.** The pieces are written as project assets with their catalog fields (role, layer,
    footprint, cells, `matched: 1`). The Build step puts them on the grid, unchanged. The engine adds the shadows
    and the colour grade, the same for every asset.

Things that work together (a door and its wall) are found from the plan and made on one sheet: §2a.

**The rules every prompt carries:**
- **Clean separation first.** Each asset stands completely clear: the key colour all round it, nothing attached.
- **How a piece stands.** A building stands on its own lot: a thin plinth exactly its footprint, of the ground it
  stands on in this place, with only what belongs to it. A small thing is one clean object with nothing under it.

**Nothing is about one kind of place.** The words come from the plan and the ontology: the roles' `draw` words, the
footprints, the ground under each piece, the build type's look. The same steps make a city block, a village, a
dungeon's furniture or a farm's tools.

## 2a. Things that work together

Some assets are seen side by side and must look made for each other: a door and the wall it stands in, a torch and the
wall it hangs on, a fuel pump and its gas station. Made on different sheets, they come out in different heights,
stones and woods, and the scene looks put together from different games. So they're made together, on purpose.

**1. Find them, from the plan** (`MatchedArtSheets`, MatchedArt.cpp). Each piece gets a `with` list: the other pieces
it works with. Two needs work together when:

| Rule | From | Example |
|---|---|---|
| a thing fills a doorway | its role's look `doorway: true` (ontology), and the place has a wall part | door, locked door + wall block |
| a thing hangs on a wall | it's one of the build type's parts, or its role's look `at: wall` | wall torch + wall block |
| a thing depends on another | its role's look `dependsOn` (ontology), and a need of that role (or a narrower one) | door + wall block |

None of these name a kind of place: the ontology says what fills a doorway, hangs on a wall or depends on what.
Things that merely stand near each other aren't linked (it linked every car and street light to thirty buildings).

**2. Draw them to match.** A doorway thing in a wall that runs (§2b) is painted inside a run of that wall and cut out
of it, so it fits by construction; otherwise its box is as tall as the wall's (three storeys).

**3. Put them on a sheet of their own** (`_groups`, matched.py). The `with` links are joined into sets (a wall, its
doors, its torches). Each set gets its own call with nothing else on it; then the rest, one of each kind first.

**4. Say it in the prompt**, plainly, for the pieces on that sheet:

> These objects go together in the game and must match: door, locked door, wall block, wall torch. Use the same
> materials, colours and detail for them. A door or gate fits in its wall: the same height as the wall, in the same
> stone or wood. Something that hangs on a wall is made for that wall.

**Later sheets** get the style sheet as image 2, so what works together keeps matching the rest of the place.

**Not covered yet:** a set too big for one sheet (more than 4 buildings or 6 small things) is split over sheets in
order, still all with the style sheet as reference. Fence and hedge lines (the line pieces) aren't made as runs yet.

## 2b. How things stand in the scene: runs, facing, variants

Traits of a role, in its `look` in `data/places/roles.json`, inherited through `broader` (set once on a broad role,
overridden where needed). They decide what is painted and how the scene uses it, for any kind of place. The plan's
arrangement (docs/places/dungeon.md §0) can set an object's purpose, size, relations and facing rule over them.

| Trait | Values (default) | Set on, e.g. |
|---|---|---|
| `run` | true (absent: false) | `wall-block`, `piece-set`, `hedge`: copies stand in a line and join into one |
| `orient` | `front`, `along`, `none` (absent: `none`) | `prop` front; `vehicle` along; plants, rocks, barrels, lights, bollards, doors none |
| `variety` | `high`, `medium`, `low` (absent: `medium`) | `building`, `car` high; other vehicles medium; bicycles, street lights, bollards, benches, walls, doors low |
| `relations`, `purpose`, `faces`, `views`, `turns` | see docs/places/dungeon.md §0, §0b | `door` set-in wall, `stairs` leads-to a level and against a wall |

### Runs: walls with no gaps

A wall painted as a lone block comes out with bevelled edges, so two side by side show a groove. So a run is painted
**three cells long**, as one continuous wall, and **one cell is cut out of it**: flat-sided, it joins its neighbours.

- **Its own sheet.** Each run (the plain wall, the wall with a door, the wall with a light) is painted alone on its
  sheet: no look-alike beside it for the naming to take (`_groups`).
- **At the grid's angle, or painted again.** A run must be painted isometric: its foot along its front slopes about
  1 in 2 (`_runs_on_grid`: 0.3–0.75). A wall painted flat, facing the viewer, can't be cut into the grid's cells; it
  fails and is painted again, up to three rounds.
- **Cut by the painting, not only the box.** Before cutting, the run is levelled to 1 in 2 (`Level`) and fitted to
  its box (`FitFootprint`: its foot's corners measured, the picture re-proportioned so the front corner and both edges
  are the box's). Then `KitCell` cuts a cell: for each pixel, which face it shows (top, front, end) and so where along
  the run it lies.
- **Which cell.** The plain run gives two pieces: its **middle** cell (flat on both sides: a cell inside a run) and
  its **last** cell (its end painted: where a run stops; `-run-end`, cut from the same painting). A door is painted in
  the middle of a run (`-inwall`) and a wall light on one (`-onwall`); the cell cut is **where the thing was painted**,
  found by comparing the run with the plain one (`FindInRun`: the columns that differ most, a cell wide).
- **In the scene.** Each wall cell takes the middle piece, or the end piece where the run stops; a cell with a light
  hung on it (a relation whose art is `on-run`) takes the `-onwall` piece instead of a lamp on the floor. Each stands
  exactly on its cell, a cell wide, seated on its front corner (§2d), mirrored where the wall shows its face to the
  east, so a corridor running north–south has its walls turned.

### Facing: things turn with their place

Each object's facing comes from the plan's arrangement (`FaceObjects`, by its rule), else from where it stands
(`FacingFromPlace`):
- `away-from-wall` (`front` things by default): its back to the nearest edge of its room or area; on a tie, the way
  with more room in front.
- `room`: towards its room's middle; `door`: towards its room's nearest door; `front`: towards what it's used with.
- `path` (`along` things): down the long way of the ground it's on (the road), in the right-hand lane.
- buildings keep the layout's facing (towards their street).

Art is drawn facing one way: the front to the viewer's lower left. A back view shows its back to the lower left. The
vision model checks which way each one's front points and mirrors or swaps pieces that came out the other way
(`face_pieces`). The scene shows:

| Facing | Shown |
|---|---|
| south (lower left) | front view |
| east (lower right) | front view, mirrored |
| north (upper right) | back view |
| west (upper left) | back view, mirrored |

### Variants: not one shop six times

How many different looks a kind gets comes from how often the place uses it and its `variety`: high
`ceil(√uses / 1.5)` (at most 4 for a building, 6 for a thing), medium `ceil(√uses / 3)` (at most 3), low 1. A city's
400 cars get 6; its 12 shops of one footprint 3; its bicycles and street lights 1. In the scene each kind's things are
numbered in reading order and take the looks in turn, so neighbours differ.

**Other looks and back views are edits of the first piece** (`derive_pieces`), not boxes on a sheet: a small box
labelled "a bicycle, version 2" or "a car, seen from behind" was often painted as a little building. The accepted
first look is given as image 1 with "another bicycle of the same kind: different colours and details, same size,
angle and style" or "the same bicycle, seen from behind"; it starts from the right thing, so it stays that thing.
Fronts first, then their backs (each from its own front).

**Catalog fields:** `variantKey` (the kind's need), `variant` (1…n), `orient`, `purpose`; a back view has `backOf`
(its front's path) and no `roles`, so it's never chosen alone; a run's piece has `run: 1` (`end` for a run's end); a
wall cell with a thing on it has `onWallOf` (that thing's role).

### Earlier art is kept

A piece already made (its `pieces/<slug>.png`) isn't painted again: a new round paints only what's missing (new
variants, back views, runs), with the earlier style sheet as the style reference. Making matched art again keeps the
folder's `pieces/`, `style-sheet.png` (and `checked.json`); everything else is made anew. A run or a door cut from a run
has a slug of its own (`-run`, `-inwall`, `-onwall`), so an old lone block isn't reused for it.

## 2c. Checks

What the checks look at, and what they never do:

| Check | On | Fails when |
|---|---|---|
| vision (`check_pieces`) | every piece not yet checked (`checked.json`: those that passed, in any round) | not one isolated thing of its kind: two things, a scene, a building for a bicycle |
| shape (`_fits_its_box`) | pieces painted in this round only | much wider or narrower than its box: two found as one, or half of one |
| footprint (`_foot_fits`) | buildings painted in this round only | its painted foot's front corner far from w/(w+h) across: a square house for a 4×7 lot |
| angle (`_runs_on_grid`) | runs | its foot doesn't slope about 1 in 2: painted flat-on |

- **A check that can't be made isn't a pass.** If the vision model fails (asked twice), its pieces stay unchecked
  (checked next round), never marked as passed.
- **A failed repaint doesn't replace a good earlier piece.** An earlier piece the vision check rejects is kept beside
  its repaint; if the repaint fails too, the earlier one comes back.
- The shape and footprint tests run on new pieces only: an earlier building is usually taller than its flat box, which
  these tests can't tell from a merge, so they rejected good buildings (the city's twelve needless fix sheets).
- Up to three fix rounds; what still fails after them is kept as it is and said so.
- What the checks said is saved with the run (`steps`: `checked`, `unchecked`, `wrong`, `said`).

## 2d. Standing on the grid

- **Centred where the scene stands it.** A building's picture is padded so its painted front corner is the middle
  of the picture: the scene centres a sprite on its stand point (the footprint's front cell).
- **Kept as painted, scaled to fit.** A building's painting is never warped (re-proportioning it bent roofs and
  gables): it's scaled whole so both sides of its painted foot fit its footprint (the tighter side decides), its front
  corner on the footprint's. A building painted deeper than its lot comes out a little smaller, never over its street.
- **On the ground, not floating.** A sprite's bottom stands a quarter cell (of height) below its point. Matched art's
  lowest pixel is its footprint's front edge, so it's set forward by half its width (in cells) less a quarter, along x
  and y: a building, a wall piece, a car a quarter of a cell; a lamp not at all. The position check allows it.
- **Shadows by height.** A thing's shadow is cast by its height, not its width: a car's stays under it.
- **Multi-cell things.** A prop bigger than a cell (stairs to a level above, 2×3) is scaled to its footprint.
- **The map's edge** (a city block the edge cuts). A building on a lot the edge cuts is shown at its kind's own size:
  the footprint the same kind has away from the edge, most often, facing the same way (`OwnSize`). It reaches out past
  the edge with the art made for that size, and is cut at the edge (`ClipToMap`: each pixel placed by its box's faces,
  what falls outside the map cleared). The map reads as a window on a bigger place, not a row of shrunken buildings.
- **Toned to the background.** The things' colours on average are pulled half way to the background's (each channel
  scaled down, never brighter), so walls and props sit in the ground's light.

## 2e. Not made any more

- **Ground textures.** The ground is the place's background, painted whole (docs/places/background.md); matched art
  makes no ground sheet. The scene lays the ground from the background picture alone.
- **The Painted method** (a place painted as one backdrop, walls read from it) is removed, with its Paint Provider.

## 3. The providers

| | BFL | PixelLab | Mistral |
|---|---|---|---|
| **Per call** | the style sheet and building batches: a whole sheet; a small thing: one edit | one asset | one asset |
| **Shape from** | the sheet's block-out; a small thing: the example it's edited from | the box as its init image | words only: the box's size and angle in words; step 7 fixes the scale |
| **Style from** | the example pieces (image 2, or the image edited) | example pieces as style images (style-v2), the palette as its colour image | the style line and palette in words |
| **Background** | magenta, keyed out | transparent | magenta, keyed out |
| **Runs** | a run painted alone, three cells long, cells cut from it (§2b) | not yet | not yet |
| **Best for** | any style; large art (up to 4 MP a sheet) for close cameras | pixel art; transparent sprites | any style; quick tries |

The style sheet is made by the same provider: a BFL sheet, or PixelLab's or Mistral's pieces made one by one (the
first ones made are the anchor for the rest).

## 4. Preferences

**Preferences ▸ AI ▸ Prefab art** is one choice: *BFL* (with a model: FLUX.2 klein, FLUX.3 Image, FLUX.2 pro),
*PixelLab* or *Mistral*. **Preferences ▸ AI ▸ Background** chooses what paints backgrounds (FLUX.3 Image for now).
Both are the app's defaults; a project can have its own (**Project Settings ▸ AI**: provider, model, art, background,
each "Use app default (…)" unless set), chosen in the New Project window or later. The Assets step makes the art with
the project's choice; the asset rows say which provider made each piece.

## 5. Tests

`tests/quality/bfl_results/city/test_8/<provider>/`, from the selected block-out (scale_blocks24_grouped-filler):
`style-sheet.png`, `sheets.png` (or the assets sheet), `assets.png`, `scene.png`. One folder per provider:
`bfl-klein`, `bfl-flux3`, `pixellab`, `mistral`.
