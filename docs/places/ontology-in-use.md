# How places use the ontology (2026-10-27)

What the plan, the asset step and the scene read from the ontology (the ontology itself: docs/places/place-ontology.md).

Facts the scene, the asset step and the plan step read for any kind of place (docs/places/matched-art.md §2b–§2d,
docs/places/dungeon.md §0–§0b). All of them sit in a role's `look` in `data/places/roles.json` and are inherited
through `broader` (set once on a broad role, overridden where needed); the plan's arrangement can set them per kind.

## Role traits (`look`)

| Key | Values (default) | Says | Set on, e.g. |
|---|---|---|---|
| `orient` | `front`, `along`, `none` (`none`) | how a thing turns with its place: it has a front; it follows the path it's on; it looks the same from every side | `prop` front; `vehicle` along; plants, rocks, barrels, lights, bollards, doors none |
| `views` | `1`, `2`, `4` | how many sides look different (1: one picture; 2: front and back, mirrored for the other two) | `prop` 2; barrels, plants, lights 1 |
| `axis` | true | it has a direction but no front: it runs along x or y (drawn along x, mirrored for y) | `wall-block`, `piece-set` |
| `faces` | `away-from-wall`, `room`, `door`, `path`, `front`, `across` | its facing when the plan gives none | chest away-from-wall; prop room; door across |
| `turns` | `free`, `wall-only`, `never-wall` | which facings it may have | fireplace, bed, stairs, door wall-only |
| `run` | true | copies stand in a line and join into one: painted as a block, the things set in it or on it made from it | `wall-block`, `piece-set`, `hedge` |
| `variety` | `high`, `medium`, `low` (`medium`) | how much real copies differ: with how often the place uses the kind, how many looks are made | `building`, `car` high; other vehicles medium; bicycles, street lights, bollards, benches, walls, doors low |
| `relations` | `[{type, to}]` | the role's default relations to other kinds (below) | door set-in wall-block; stairs leads-to a level and against wall-block; vehicle aligned with road-terrain |
| `found` | `roads`, `solid`, `ground`, `objects` (`objects`) | where a solved plan has things of this role, for a relation to find them: the roads' cells, the solid cells (walls, rock), ground of that name, placed things of that kind | `road-terrain` roads; `wall-terrain`, `wall-block` solid |
| `purpose` | text | what it's for, by default (the plan's arrangement makes it specific) | stairs: "stairs leading to another level" |

## Functional relations (`data/places/relations.json`)

Beside the spatial relation types the solver scores (`near`, `adjacent`, `faces`, `along`, `edge`…), relations that
say what one thing is *to* another. Each has a `use`: where its subject stands (`position`, the solver's place rule),
which way it faces relative to the other (`facing`), how far to look for the other (`within`, cells), and how its art
is made (`art`).

| Relation | Means | position | facing | art |
|---|---|---|---|---|
| `set-in` | a fills a gap in b (a door in its wall) | wall | across | `in-run`: an edit of b's block |
| `mounted-on` | a hangs on b (a torch on a wall) | wall | away | `on-run`: an edit of b's block |
| `against` | a stands with its back to b (a chest against a wall) | wall | away | on b's sheet |
| `leads-to` | a connects here to b (stairs to another level) | wall | away | on b's sheet |
| `aligned` | a lies along b, parallel (a car on its road, on its side) | — | along | — |
| `lights` | a lights b | near | — | on b's sheet |
| `part-of` | a belongs to b (a counter of a shop) | — | — | on b's sheet |
| `used-with` | a is used with b (a chair at a table) | near | front | on b's sheet |
| `debris-of` | a came from b (rubble of a wall) | near | — | b's material |
| `made-with` | a's art is made together with b's (stairs with the walls they stand against) | — | — | `with`: b's picture shown when a's sheet is painted |
| `coloured-by` | a's art takes its colours from b (a wall from the background it stands on) | — | — | `colour`: b's colours said and shown when a is painted |

What uses them:
- **The plan** (`/places/arrange`, `ApplyArrangement`): each kind's purpose, footprint, relations, position and
  facing rule, the LLM's within these types, the ontology's by default; a relation with a `position` places the thing
  (stairs that lead out of a room stand at a wall, not in its middle).
- **Facing** (`FaceObjects`): first from the relations: the first whose type has a `facing`, towards the nearest
  thing it names within `within` cells (found where its role's `found` says). `along`: along the way that thing
  runs there, on its right-hand side (a car in its lane: the south half of a road along x drives east, the east half
  of one along y drives north); `away`: back to it; `front`: towards it. Else the role's `faces` rule. The object is
  marked `faced` with the relation or rule that turned it.
- **The scene's turning** (`PlaceObject`): a picture of an `orient: along` thing (a car) may be painted along either
  of the grid's ways. Where its lowest point is says which (towards the right: along x, a front facing east or a back
  facing west; towards the left: along y), and it's mirrored when that differs from its facing. In the middle: as
  if drawn facing south.
- **Art** (`MatchedArtSheets`, `make_blocks`): `in-run` and `on-run` make a piece an edit of the block it depends on
  (the door from the wall), so it fits it; the other `art` values put related kinds on one sheet. They replace the old
  "touching in the layout" links. Kinds of one family (one role narrower than the other) set in or mounted on the wall the same way share
  one edit: the kind whose own role sets it in or on the wall is made, else the broader one; the other gets a copy
  (`sameAs`). A locked door uses the door's art; a light the plan put on a wall (`light-prop`) uses the wall torch's,
  since `wall-torch` is a kind of `light-prop` that is mounted on walls by definition.
- **The scene**: a thing `mounted-on` a wall is drawn as that wall cell, painted with it.
- **Colour** (`coloured-by` → `background`): walls, wall piece sets, pillars, stairs and debris take the colours of the
  place's background. `background` is a role (`found: background`: the place's ground picture, painted or picked).
  When the art is made with a background set, a piece coloured by it gets the background's average colour in words
  and hex ("dark greyish brown (#544638)"); a wall block also gets a crop of the background as a reference image. What
  is made as an edit of the wall (doors, wall lights) keeps the wall's colours. In the scene every picture is still
  toned towards the background (each picture most of the way to its colour, a little lighter, never brighter).

## Changes to existing facts

- `PlacesSettings.method` (Assembled / Painted) is gone: there is one way to build places.
- Project settings have an `AI` section (provider, model, art, background; each empty for the app's choice).
- Doors, windows, wall torches, banners and wall décor carry `relations` instead of only `dependsOn` / `at: wall` /
  `hang`; those keys still work.

## Layout rules

A module says how its things are laid out, by slot, in `layout` (in its module file). The solver reads it while placing
(no AI call); the plan step can later let the LLM adjust it for one place. Each rule has a `why`: the design
principle in words. An `op` rule is emitted by the compiler as that solver statement; a `pattern` rule shapes a scatter.

| Pattern | Fields | Means | On |
|---|---|---|---|
| `bays` (a scatter's pattern) | `length` (a row's depth: a car's length, 2), `aisle` (cells, 2), `ends` (cross aisles, 1), `facing: aisle` | rows along the area's long side, each a car's length deep (it stands in the cell by its aisle, the rest is its tail's); the first against the edge, then an aisle and two rows back to back, over and over; a cross aisle at each end; things side by side in the rows, each facing its aisle, so every one can drive out | `parking-lot` (`car`) |
| `op: grid` | `spacing` {x, y}, `count` (a field), `default`, `name` | a regular grid (the solver's grid op) | `orchard` (`tree`), `graveyard` (`grave`) |
| `op: rows` | `count` (a field), `per` (things a row: the count divided), `default`, `margin`, `name` | parallel rows (the solver's rows op) | `field` (`crop`), `market-square` (`stall`) |

A thing placed by its layout is marked `faced: layout`, and `FaceObjects` leaves its facing alone (a parked car faces
its aisle, not the road).
