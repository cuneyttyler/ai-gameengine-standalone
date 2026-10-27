# Dungeon build: open issues

From the dungeon test (`tests/quality/method_4/dungeon_test/scene_v3`, 2026-10-26): Matched art with runs, views and
variants (docs/places/matched-art.md §2b) and the layered background (docs/places/background.md). Each issue has what
we see, why it happens, and the proposed approach. Every fix must stay generic: for any kind of place and any asset.

## Status (2026-10-27)

Implemented, first version (test: `dungeon_test/scene_v4`):
- §0 functional relations in `relations.json` (each with `use`: position, facing, art); role defaults in `roles.json`
  (`relations`, `purpose`, `faces`, `views`, `axis`, `turns`).
- The plan's arrangement: `/places/arrange` (LLM, per kind: purpose, footprint, position, faces, relations);
  `ApplyArrangement` before solving, `FaceObjects` after (engine/places/Arrange.cpp); PlaceBuild asks for it once the
  plan is laid out.
- §2 doors found in their run by difference with the plain run (`FindInRun`); §4 runs levelled to 1 in 2 (`Level`).
- §3 purpose in the art words; size from the arrangement's footprint (props bigger than a cell scaled to it).
- §6 a light mounted on a wall: a wall cell painted with it (`-onwall` kit), put where it hangs.
- §7 the things toned to the background in the scene (each channel by half the difference, never brighter).
- Also: every matched piece stood on the ground by its width (nothing floats); shadows cast by height; a building
  scaled whole into its footprint (never warped) and, on a lot the map's edge cuts, shown at its kind's own size and
  cut at the edge (`OwnSize`, `ClipToMap`).

Learned in the tests (scene_v4, scene_v5), and fixed (docs/places/matched-art.md §2b–§2d):
- klein sometimes paints a wall run flat, facing the viewer: it can't be cut into the grid's cells. Runs are checked
  for the grid's angle and painted again (three rounds), one run to a sheet, and fitted to their box before cutting.
- Back views and other looks painted as boxes on a sheet came out as other things (a stone staircase behind a wooden
  one, a hut for a bicycle): they're now edits of the accepted first piece.
- A failed vision check counted as a pass: now it leaves the pieces unchecked.

## 0. Relationships first (the base of §1, §3, §5 and §6)

Every placed object stands in some relation to others: a door is set in a wall, a torch is mounted on a wall, stairs
lead from this room to the level above, a chest stands against a wall facing the room, debris lies near the collapsed
wall it came from. Placement, facing, size and art should all follow from these relations, decided once, before
anything is placed.

**What exists.** The ontology's relation types (`data/places/relations.json`) are spatial: `inside`, `edge`,
`corner`, `adjacent`, `near`, `in-front-of`, `behind`, `beside`, `faces`, `along`, `on`, `encloses`… The solver
scores them (Solver.cpp), but they're written per module (a house faces its street), never per object, and nothing
outside layout uses them (not facing, not art).

**Proposed.**
1. **Functional relation types in the ontology**, beside the spatial ones. Each says what it means for layout,
   facing and art:

   | Relation | Meaning | Layout | Facing | Art |
   |---|---|---|---|---|
   | `set-in` | a fills a gap in b (door in wall) | on b's line, in a gap | across b | painted inside b's run, cut out (§2) |
   | `mounted-on` | a hangs on b (torch on wall) | on b's face | away from b | painted on b's run (§6) |
   | `against` | a stands with its back to b (chest, shelf) | adjacent to b | back to b | on b's sheet if b is architecture |
   | `leads-to` | a connects here to b (stairs to a level, a gate to a road) | at the edge towards b | towards b | sized for it (§3) |
   | `lights` | a lights b (lamp, brazier) | near b or on its wall | — | with the architecture |
   | `part-of` | a belongs to b (a counter of a shop) | inside b | b's | on b's sheet, told to match |
   | `used-with` | a is used with b (chair at a table) | near b | towards b | on one sheet |
   | `debris-of` | a came from b (rubble of a wall) | near b | — | b's material |

   Each maps onto the spatial relations the solver already scores (`against` = `adjacent` + `behind`), so the solver
   doesn't change much. Roles give defaults (a door is `set-in` a wall, a wall torch `mounted-on` one: today's
   `doorway`, `at: wall`, `dependsOn` and `hang` looks become these).
2. **The plan decides them first.** In the plan step, before placement, the LLM writes each object's purpose (§3)
   and its relations to the others, using these types: "stairs-1 `leads-to` level above, `against` the north wall";
   "torch-2 `mounted-on` wall, `lights` the entrance". Validated against the ontology (allowed pairs, one `set-in`
   per gap), defaults filled from the roles.
3. **Everything after uses them.**
   - Placement: the solver places by the relations.
   - Facing: from `against`, `faces`, `leads-to`, `mounted-on` (§5), not guessed.
   - Size: from the purpose and its relations (§3).
   - Art: `set-in` and `mounted-on` make kit pieces from the wall's run (§2, §6). `part-of`, `used-with` and the
     architecture share sheets and are told to match (§3). This replaces today's `with`.

## 0b. Directions and rotations in the ontology

What a role says about direction, so neither the LLM nor the code has to guess per object. Today only `orient`
(front / along / none, docs/places/matched-art.md §2b) exists. Proposed, in each role's `look`, inherited:

| Key | Values | Says | Example |
|---|---|---|---|
| `views` | `1`, `2`, `4` | how many sides look different: 1 the same from every side (a barrel, a tree); 2 front and back differ, left and right are mirrors (a chest, a car: front + back view, mirrored); 4 every side differs (rare: a shop corner) | barrel 1, chest 2, car 2 |
| `axis` | true | it has a direction but no front: it runs along x or y (a wall, a fence, a bench seen from both sides); drawn along x, mirrored for y | wall-block, fence, hedge |
| `faces` | `away-from-wall`, `room`, `path`, `door`, `<relation>` | its default facing when the plan gives none: back to the nearest wall, towards its room's middle, down the path it's on (right-hand side), towards the nearest door, or by one of its relations (§0: `against` → back to b) | chest away-from-wall, car path, throne door, chair `used-with` |
| `turns` | `free`, `wall-only`, `never-wall` | facings it may have: a fireplace or a bed's head only against a wall; a door only across its wall; most things free | fireplace wall-only |
| `footprint` | `turns` | its footprint turns with it (a 1×2 bed facing east is 2×1); the solver does this today for buildings | bed, car, bench |

**One drawing direction for all art.** Every asset is drawn facing south (its front to the viewer's lower left); a
back view shows its back to the lower left. The ontology says it once; the prompt says it; the vision check (face_pieces)
enforces it. The scene turns any facing into a view and a mirror by one table (front, front mirrored, back, back
mirrored for south, east, north, west).

**Frames.** Absolute directions (north, south, east, west: the map) for where things are and which way they face;
relative ones (front, back, left, right: the object's own frame) for relations between things, as `in-front-of`,
`behind` and `beside` already are. A relation (§0) can say which way its subject faces relative to its object:
`against` = back to it, `faces` = front to it, `mounted-on` = away from it, `set-in` = across it.

**What it gives.**
- Art: only the views a role needs are made (`views` 1: one piece; 2: front and back; `axis`: a run).
- Plan: the LLM sets facings only where a role allows a choice (`turns`), checked against it.
- Layout: footprints turn with facing.
- Scene: facing by the plan, else by `faces`, never a guess per object.

## 1. Stairs that lead nowhere

**Seen.** Stairs stand in the middle of rooms and against walls with nothing above them.

**Why.** The layout places the `stairs` slot like any prop. Nothing says what stairs are for: that they connect this
level to another, so they must start at a room's edge (or a stairwell) and rise towards that other level.

**Approach.** Comes from the object's purpose (§3): "stairs leading up to the level above". The purpose drives where
it stands (at a wall or a room's edge, its top towards the wall, never free in the middle), how big it is (§3) and
which way it faces (§5).

## 2. The door is missing part of itself

**Seen.** The locked door's piece is a slice of wall edge (a red column); the plain door is cut at its sides.

**Why.** A door is painted in the middle of a wall run and its middle cell is cut by the box's geometry
(`KitCell`). The model doesn't put the door exactly in the middle third, or makes it wider than a cell, so a
fixed cut misses it.

**Approach.** Find the door in its painting: compare the door run with the plain wall run (same wall, same size) and
take the columns where they differ; centre the cut on them, as wide as the door (a cell, or two for a wide door).

## 3. Purpose: what each object is for

**Seen.** Stairs to the upper level are drawn as a small 1-cell prop, smaller than the walls, with a few steps.

**Why.** An object is only its role and footprint (`stairs`, 1×1). Nothing says what it's for in this place, so
the art and its size follow the role's default.

**Approach.** A **purpose** for every object of the plan, written by the LLM in the plan step: one plain sentence of
what it is for in this place ("stairs leading up to the level above", "rubble from the collapsed ceiling", "a lamp
lighting the entrance hall"). The ontology gives a default per role; the LLM makes it specific. The purpose is used:
- **in the art words**: what's painted ("a wide stone staircase rising to the level above, many steps, as tall as
  the walls"), not just the role's name;
- **for its size**: the LLM sets the footprint and height the purpose needs (stairs to a level above: 2×3 cells,
  as tall as the walls), within what the room allows;
- **for what goes together on a sheet**: objects whose purposes belong together share a sheet and are told to match.
  The architecture of a place (walls, doors, stairs, the lights on its walls) goes together; loose things (crates,
  sacks) go with each other. Today `with` comes only from `doorway`, `at: wall` and `dependsOn`; stairs aren't in it.

## 4. Wall angle doesn't match the grid

**Seen.** Walls side by side show small corners and peaks along their top edge.

**Why.** Our grid is 2:1 (a cell is 64×32: its edges at 26.6°, not 45° or the 30° of true isometric). The block-out
box is drawn 2:1, but the model paints its own idea of "isometric", so the painted wall's edges slope a little
differently. Cut by the box's geometry, each piece's top edge doesn't continue the next one's.

**Approach.** Measure the painted wall's edges (its top and bottom lines along the run) and correct the picture to
2:1 before cutting (one shear and scale per run). Also say the angle in the prompt ("2:1 isometric: edges rise one
pixel for every two across").

## 5. Rotation and direction from the plan

**Seen.** Debris on the east side of a room looks placed as if on the north side.

**Why.** Facing is worked out by code from where a thing stands (`FacingFromPlace`: away from the nearest edge). It
doesn't know what the thing is for, and the art's own direction isn't always the one asked for.

**Approach.** The plan step's LLM sets each object's facing (north, south, east, west) with its purpose (§3), from
the room it's in, its walls and doors. Assets are drawn in one direction (facing south, along north–south); the scene
turns them to the plan's facing (the front or back view, mirrored), as now. Code's facing stays the fallback.

## 6. Lamps on the ground

**Seen.** Lamps stand on the floor in front of walls and look out of place.

**Why.** The layout places `light-prop` things (lanterns) on floor cells near walls; the wall torch (the build type's
part) is a separate small piece hung on wall cells. Neither is made with the wall.

**Approach.** By purpose (§3):
- **A light on a wall** is part of the wall: painted in the wall's run ("a straight wall, three cells long, with a
  torch on its middle cell") and cut out of it, so it's the same stone and light. The run gives two wall pieces: plain,
  and with a torch; the scene uses the torch piece where the plan puts a light on a wall.
- **A free-standing light** (a brazier in the middle of a hall, a lamp post outside) is a thing of its own, made with
  the architecture (§3) so it matches.

## 7. The background and the assets don't match

**Seen.** The floor (the background) and the walls and props look like two different games: different palette,
light and finish.

**Why.** The background is painted by FLUX.3 Image from the plan alone; the assets by klein from the style sheet.
Nothing ties one to the other.

**Approach.** One look for both:
- **Order:** paint the background first; it becomes part of the style the assets are made in (the walls' stone is
  the floor's stone). Not as a reference image in the background prompt (the model paints the assets in), but the
  other way round: the background as an image the asset sheets match.
- **Same words:** the background description (the planner's) names the materials ("old worn grey-brown stone");
  the assets' prompts use the same words.
- **Colour matching in code:** after both are made, the assets' palette pulled towards the background's (as pieces
  are pulled towards the style sheet's today), or the background graded to the assets', so tone, saturation and
  light agree.
