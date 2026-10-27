# How a plan and its block-out are made

Status: **reference, v1 (2026-10-26)**. This describes the code as it is: what happens from a request to a plan and
its block-out, what the parameters are and where each comes from, and how the place ontology (`data/places/`) is
used at each step. It is the basis for changing any of it.

---

## 1. The steps

| # | Step | Input → output | Who decides | Code |
|---|---|---|---|---|
| 1 | **Read the request** | words (+ the user's choices) → **Brief** | the AI (three readings, merged), or the words alone if the AI is off; the user's choices always win | `PlaceBuild::SendReadings`, `/places/interpret`; `Brief.cpp` (`FallbackDraft`, `CompleteBrief`, `ApplyChoices`) |
| 2 | **Questions** (optional) | Brief → questions → answers | the AI asks, the user answers (or not) | `/places/questions` |
| 3 | **Plan the top** | Brief → the plan tree's root: its zones and networks, with fields, placement and relations | the AI (`/places/plan-root`), or the rules (`FallbackRootAnswer`) | `PlaceBuild::PlanFromBrief`; `places.py` |
| 4 | **Plan each zone** | a zone → its parcels and features, with counts and fields | the AI (`/places/plan-zone`, one call per zone), or the rules | `PlaceBuild`; `Planner.cpp` |
| 5 | **Check and repair** | the plan tree → problems → fixes | the ontology finds the problems; the AI repairs (`/places/repair-node`) or the rules do | `PlaceValidate.cpp` |
| 6 | **Compile** | the plan tree (**Spec**) → layout statements (**PLL**) | code only | `Compile.cpp` |
| 7 | **Solve** | statements → cells: zones, streets, lots, buildings, things, terrain (**solved layout**) | code only (seeded, repeatable) | `Solver.cpp` |
| 8 | **Draw** | solved layout → layout map, plan map, block-out | code only | `LayoutImage.cpp`, `PlaceBlockout.cpp` |

Only steps 1–5 involve the AI, and each has a rule-based fallback, which is what runs offline and in the unit tests.
Steps 6–8 are always code: the same plan always gives the same layout and the same pictures.

## 2. What each step decides

**Brief (step 1):**
- **build type**: settlement, modern-city, dungeon, …;
- **scale**: a class (block, neighbourhood, …), with a number of buildings and an area;
- **style**: era, culture, condition;
- **street pattern**: `shape.layout`, a mix of organic, grid, radial and linear;
- **components**: which zones are wanted, how many, and their fields;
- **features**: a river, a lake, a coast;
- **root fields**: the layout form, §3.

**Plan tree (steps 3–5):**
- the zones, each a module such as `downtown`, with its fields (density, wealth, street style), a placement sentence and relations (near, north-of, …);
- the networks: main roads, a river, walls;
- each zone's parcels, with how many (`repeat`) and their fields (size, kind);
- features: gardens, fields.

**Compile (step 6)** turns each node into statements, as §4 lists.

**Solve (step 7)** carries them out on the grid, in this order:
1. paths and networks;
2. partition into zones, and subdivide zones into lots;
3. each lot's main building;
4. other regions;
5. connections;
6. other things;
7. lines (fences, walls);
8. rows and grids (crops, graves);
9. scatters (props, plants, cars).

## 3. The parameters

Parameters come from five places. When two say different things, the higher one in this list wins.

| Source | Set by | Examples |
|---|---|---|
| **A. The user's choices** | the Brief window, Project Settings | build type, scale class, any field the user sets |
| **B. The plan** | the AI (or the rules) | which zones, how many of each parcel, a zone's fields, placements |
| **C. The build type and its root module's defaults** | the ontology (`buildtypes.json`, the root module) | scale tables, a modern city's layout form defaults |
| **D. Module and role data** | the ontology (`modules/*.json`, `roles.json`, `vocabularies.json`) | a parcel's size range, a district's filler kinds, a role's size and storeys |
| **E. Engine constants** | code | how much area a lot needs, street spacing from lot depth |

### 3.1 The layout form (root fields; any place with zones)

| Field | Values | Controls | Village | Modern city |
|---|---|---|---|---|
| `shape.layout` | organic, grid, radial, linear-along-road, linear-along-river (a mix) | the street pattern | organic | grid |
| `zone-shape` | grown, blocks | zones grown from seeds, or rectangles cut along the main roads | grown | blocks |
| `street-grid` | per-zone, shared | each zone its own streets, or one network for the place (a grid lattice, or one radial plan) | per-zone | shared |
| `block-size` | cells (w × h) | the shared grid's spacing | 16–20 | 16–20 |
| `block-fill` | frontage, full | lots along the streets with land behind, or blocks built through | frontage | full |
| `coverage` | partial, full | open land between zones, or zones covering the site (leftover lots get filler) | partial | full |
| `lot-layout` | packed, rows | lots fitted one by one, or each block cut into equal rows | packed | rows |
| `block-use` | mixed, grouped | kinds mixed in a block, or one kind per block | mixed | grouped |
| `frontage` | varied, aligned | each building its own setback, or one building line | varied | aligned |
| `building-cover` | planned, fit | a building keeps its planned footprint, or fills its lot as its kind does | planned | fit |
| `arrangement` | natural, ordered | small things scattered, or set out evenly in lines and pairs | natural | ordered |

Each value has a prompt in `vocabularies.json`. The AI reads it with the field (as "meaning"), and the Brief shows it with the choice.

### 3.2 Per-module data that shapes the layout

| Data | Where | Effect |
|---|---|---|
| `quantity` fields | each module | sizes (a lot's w × h), counts (plots), densities (trees), areas; their defaults apply when the plan doesn't set them |
| `quality` fields | each module | kinds, wealth, condition, surface; enums from vocabularies |
| `slots` | each module | the things it holds, each a role (its primary building, its props) |
| `children` | each module | what it may contain: min, max, weight (how typical); the AI and the rules plan within these |
| `relations`, `prefersSite` | each module | where it goes: near the centre, by water, faces the street |
| `compile` hints | each module | how it's laid out: contents (what's scattered in it, how many, where), surface, network (street class, width, terrain, edge), traffic, filler, and for a root: ground, roads, landscape set |

### 3.3 Role data (each kind of thing)

| Data | Effect |
|---|---|
| `size`, `tall` | how big it is (cells across, height for width) |
| `draw` | the words its art is made from |
| `look.at`, `look.near` | where it stands in a room: against a wall, in a corner, in the middle, beside something |
| `look.vary` | plants: in groves, jittered, varied in size |
| `look.floors`, `look.roof`, `look.cover` | a building's storeys, roof, and how much of its lot it fills |
| `look.light` | a light's radius and strength |

### 3.4 Engine constants (not yet parameters)

| Constant | Where | Now |
|---|---|---|
| The area a lot needs, for the site's size | `Compile.cpp` Root | lot area × 1.9; the site is at least `sqrt(required / 0.6)` across, and at least the root's `area` |
| Street spacing | `Compile.cpp` Zone | 2 × lot depth + 1 + street width (when not on a shared grid) |
| Filler count | `Compile.cpp` Zone | up to 60 lots per zone, cycling through its filler kinds |
| Ordered spacing | `Solver.cpp` Scatter | from the free cells and the count wanted |
| Partition quotas | `Solver.cpp` Partition | a zone's area field; scaled to the site when coverage is full |

## 4. From plan tree to statements (step 6)

| Plan node | Statements |
|---|---|
| root | the site (a square; its size from §3.4), its ground fill, its entrances; the main roads (path: width, surface, sidewalk edge); a crossroads |
| river, wall | a path of water; a line round the site with gates |
| the zones | one partition (method grown or blocks; cover; the shared lattice) |
| each zone | its network (style, spacing, width, edge, lattice); a subdivide into lots (packed or rows; grouped; frontage or block; its parcels plus filler); traffic on its streets |
| each parcel | its main building (footprint from the module's size and its slot's role; setback; facing the street; fit by cover); its surface; its contents (scatters); its features |
| features | gardens, beds, fields, orchards, graveyards: rows and grids; else contents |
| the land left over | the landscape's scatters (woods, yards, verges, banks), from the root's landscape set |

## 5. How the ontology is used, step by step

| Step | Ontology data used |
|---|---|
| 1. Read the request | build types (words, scale tables, root fields), components and their fields with values and meanings, vocabularies |
| 3–4. Plan | modules' children rules (what may contain what, how many, how typically), fields and defaults, classes, relations |
| 5. Check | children rules (min, max), classes, constraints, requires |
| 6. Compile | the root's layout form and compile hints; modules' fields, slots (each slot's role decides its layer: building, prop, decal), compile hints, relations and site preferences |
| 7. Solve | roles' looks (where a thing stands, plants in groves, ordered placement); classes (rooms, dungeon rooms) |
| 8. Draw | terrain tones (vocabularies), roles' storeys, roofs and cover, the legend's words (roles' draw, modules' names and descriptions) |
| Art (after the plan) | roles' draw words, sizes; modules' descriptions |

## 6. What's unclear or overlapping today

- **The site's size has three sources:** the scale's area, the plan's lots (§3.4), and coverage, which stretches the zones to fill the site. With few buildings and full coverage, most blocks are left to filler. The site should be sized either to the scale or to the plan's content, by a parameter, not both.
- **Filler is an engine rule (60 lots of open-space kinds),** not a parameter. How much open space a district has should be a parameter, and filler should come from the district's own kinds.
- **Counts are decided twice:** the Brief's scale (a number of buildings), then the plan (each parcel's `repeat`). The plan wins, so the scale's count is only a hint to the AI.
- **Engine constants (§3.4) shape every layout** but aren't visible or settable.
- **Precedence isn't shown anywhere:** what the user chose, what the AI decided, and what is a default. The Brief should show where each value comes from.
