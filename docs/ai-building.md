# AI-Generated Building — design (Phase 7)

Status: **design, revised after discussion (2026-10-18)**. The module catalogue is in
`docs/place-modules.md`; the milestones are in `development_plan.md`, Phase 7.

## 1. Goal

One instruction — *"build a large medieval town by a river"*, *"a three-floor tavern"*, *"a
dungeon under the castle"* — becomes a finished, playable place, fully automatically if
wanted. Places can be small (a hut) or **very big** (a region of towns joined by roads).
The engine must:

1. understand **what was asked**, including how big, and **confirm it** with the user;
2. understand **its art**: what each asset is, how big, how it faces, how it joins;
3. **plan** with a fixed, well-defined methodology: the whole place first, then the parts;
4. **build** step by step, big places included;
5. **test and verify** every part, and repair what fails;
6. let the user **edit any part after the build** and regenerate it.

## 2. Decisions (2026-10-18)

| Question | Decision |
|---|---|
| Target size | **Very big**: up to regions of several settlements. Big scenes (§9) are a prerequisite, not an afterthought. |
| Confirmation | **Every build** shows the confirmation window. |
| Editing | **After the build**: every built part stays selectable as its module, with its parameters, and can be regenerated. |
| Perspective | **Both** top-down and ¾ / isometric art, mixed in one project where they're compatible (§6). |
| Assets | Per component, **use the pack's or generate**, as the user chooses in the confirmation window. |
| Model budget | **No limit**. Quality first; calls are still made per module, not per entity. |

## 3. What we take from existing work

| Source | What it does | What we take |
|---|---|---|
| **Holodeck** (CVPR 2024) | A fixed chain of modules: floor & walls → doors & windows → object selection → constraint-based layout. One LLM query per module (task, output format, one-shot example). The LLM writes **relations**, not coordinates; a DFS solver on a grid places objects, anchor first and large objects first, with hard rules (no collisions, inside the room) and soft relations. Assets were annotated by a vision model and retrieved by visual + text similarity − size mismatch. | **Pipeline stages as fixed modules** per build type, each one query with a schema and an example. **The relation vocabulary** (global, distance, position, alignment, facing), hard vs soft. **Anchor-first, large-first grid solver**. **The retrieval score**, with perspective and footprint as hard filters. |
| **Infinigen Indoors** (CVPR 2024) | A declarative constraint language over **abstract classes** ("furniture", "storage"): quantity, relations, symmetry, accessibility ("free space in front of appliances"); a solver maximises satisfaction. | Constraints on **roles, not assets** ("seating near the hearth"); **accessibility** as a constraint (free cells in front of doors, stalls, chests). |
| **WorldCraft** (2025) | A coordinator agent splits a big request into **sub-spaces** and delegates to specialists (object creation, arrangement). Layout is a hierarchical object tree solved per sub-tree. A critic renders the result and approves it; approved (request, program) pairs grow a **manual** of verified examples. | **Coordinator + specialist stages**; **hierarchical decomposition** keeps solving tractable; the **verified-examples manual**: every approved module build becomes an example for later prompts. |
| **3D-GPT** (3DV 2025) | The LLM drives a library of procedural generator functions, each given documentation, inputs and a usage example; output becomes an editable configuration. | Every generator is a **documented tool** with a schema and examples; outputs are **editable parameters**, not baked results. |
| **Unexplored** (Dormans) | An abstract **graph skeleton first** (start, goal, loop with two arcs, one of 24 cycle types), refined by ~50 rule modules; **non-terminal placeholders** ("Obstacle") resolved late; the **biome chosen early** so every system agrees; lock–key relations kept to the end for validation and repair. | **Skeleton first, details late**; **placeholders** resolved by later modules; a **style/biome set at the start**; **relations kept in the result** so checks and repairs can use them. |
| **Dormans: missions and spaces** (2010) | A level is two structures: the **mission** (what the player does) and the **space** (where), generated in two steps. | Dungeons and gameplay places plan **mission graph → space graph → geometry**. |
| **Watabou's city generator** | A city of 12–40 Voronoi "patches" grouped into **wards**: gate, craftsmen, market, merchant, patriciate, slum. Walls and gates. Streets thin out toward the outskirts while buildings stay dense along them. | **Wards as district modules**; **density falling outward**; walls and gates as a network module. |
| **CityGenAgent** (2026) | City = **block program** + **building program**, both editable by language after generation. | Two-level programs: a **district plan** and **parcel plans**, editable after the build. |
| **Emilien et al.** (2012) | Villages grow from **interest maps** (slope, water, roads): seeds, roads between them, then parcels. | **Interest maps** for siting districts and landmarks on terrain. |
| **ProcTHOR** | Houses from a room specification (types, adjacency) → floor plan → doors → objects. | The **interior pipeline** from a room programme. |
| **Hierarchical WFC** (2023) | Sketch with abstract tiles, resolve each area into concrete tiles; at scale, repair locally instead of backtracking. | **Local repair**; abstract-to-concrete resolution for ground and decoration. |
| **Clarification research** (ClarEval, Active Disambiguation, 2025–26) | Models usually guess silently; a question pays off only when it narrows the result. | The confirmation window asks what changes the result; everything else has visible defaults. |

A closer reading of each source (2026-10-18) adds these specifics:

| Source | Finding | What we do with it |
|---|---|---|
| **SceneCraft** (ICML 2024) | The scene is a **bipartite graph**: asset nodes and relation nodes, each relation linking any *subset* of assets ("these houses in a row", "a lamp by each house"). Each relation type has a **scoring function in [0, 1]** (proximity is 1 below a minimum, 0 beyond a maximum, linear between); the layout maximises the sum. Relation types: proximity, direction, alignment, symmetry, overlap, parallelism, perpendicularity, hierarchy, rotation, **repetition**, scaling. The inner loop (render → vision critique → revise) gave the biggest gain; removing the graph was the most damaging ablation (constraint score 88.9 → 3.2). An outer loop merged improved relation functions across scenes into a library. | Relations are **n-ary nodes** with **scoring functions**, including group relations (`in-row`, `repeat`, `symmetric`). The **render-critique loop is not optional**. Relation functions improve from failures over time. |
| **Constraint Is All You Need** (FDG 2025) | GLDL has **definition sentences**: *PredefinedFacility* (fixed; not a variable), *NewFacility* (placed by the optimiser), *Points* (anchors). It also has **constraint sentences**: *basic* (PlaceAroundPosition, PlaceInRange, PlaceByWall) and *relationship* (Near/Far, CanSee, Focus, Alignment/Orientation). The objective is weighted penalties (overlap and bounds weight 30, so they act as guardrails) + **cluster degree** (Σ 1/distance) + **sparsity** (the largest distance from any cell to any object, so no big empty voids). Simulated annealing beat greedy and genetic search. Agent simulation still found 9.5% unreachable keys, 88% valid in all. | Everything already in the scene, and every **locked or hand-edited** part, is **predefined**: fixed, but constraining. **Anchor points** (gates, the player start, a quest spot) are first-class. The solver objective includes **clustering and sparsity terms**. **Annealing** refines after a greedy anchor-first start. **Reachability is tested, not assumed.** |
| **Database-driven 3D levels** (AIIDE 2025) | The LLM builds **databases offline**, reviewed by experts:<ul><li>*facilities*: size, fixed/variable, constraints, instance-count guideline, tags;</li><li>*rooms*: default size, characteristic facilities, instance limit, AdjacentTo/SeparateFrom;</li><li>*mechanics*: topological Precedes/Near/Far.</li></ul>Generation is then optimisation with **no LLM calls**. A two-phase repair (flood-fill and nudge *adaptable* facilities, then an agent walk that removes blockers) validated 95% of 6,000 levels. | The **module library is our database**. The AI may **author new modules offline** (reviewed, versioned), and big places need few AI calls: the **AI decides the upper levels and the solvers fill the lower ones**. Every slot is marked **essential or adaptable**, so repair knows what it may move or remove. |
| **CityGenAgent** (2026) | A block program is a list of {id, type, polygon, floors, facade description}; a building program is {component type, description}. Rewards include **overlap** and a **footprint-density band (0.5–0.8)**, plus a VLM scoring *semantic alignment* and *global plausibility* **from a rendered 2D layout**. Edits go to the program at the right level. | A **density band per zone** is a check. The **visual critique also looks at the plan view**, not only the built art. Edits are **routed to the right node**. |
| **Narrative-to-Scene** (2025) | Layers: terrain, environment, interactive, item and character, each kept as a **numeric matrix** that is the source of truth for checks. A cellular-automata walkable region comes first. Without conflict resolution, only **72% of relations held**, and affordance matching by text embedding alone was **0.42**. | Each place keeps a **semantic grid per layer** for checks. A **real solver** resolves conflicts. **Retrieval needs more than text similarity**: role, affordance, perspective and footprint filters first. |
| **GameTileNet** (AIIDE 2025) | 2,142 objects from 32-px top-down tiles, each labelled: name → group → supercategory → **affordance** (multi-label: *terrain, environmental object, interactive object, item, character*); **tile type** (complete, partial, texture, tileable texture); 8-direction connectivity. Objects in tilesheets span several tiles: region-growing segmentation recovered 56.8% usable objects. Detectors with real-world classes fail on fantasy art; captioning works better **after upscaling**. | The catalog adopts the **affordance taxonomy** and **tile type**. Tilesheets are **segmented into whole objects** before analysis. Images are **upscaled before the vision model** looks at them. |
| **Villages on arbitrary terrains** (Emilien et al. 2012) | A **village type** (defensive, farming, fishing…) and a **building type** select parameters from a **"building encyclopedia"**. Buildings are seeded stochastically where an **interest function** accepts them: Σ weights × criteria in [−1, 1], **any −1 vetoes**. Criteria: sociability (attraction–repulsion at preferred distances per pair of types), worship, road access, slope, water, fortification, high ground. Each new building is joined by a **least-cost road** with **reuse of existing roads nearly free** (w ≪ 1), plus **cycles** toward nearby roads. Parcels come from **road conquest → corner conquest → anisotropic conquest** (houses grow into quads, farms into fields), then simplification. A **growth scenario** changes the village type over time (defensive, then prosperous). | An **organic growth** street generator next to the planned ones: villages that grow along roads and terrain. **Interest maps with vetoes** site wards and buildings. Module parameters per settlement type form our **encyclopedia**. **Growth scenarios** ("an old walled core, newer farms outside") are a shape option. |
| **Procedural parcels** (Vanegas et al. 2012) | Blocks split into parcels with attributes: area range, width range, **split irregularity**. **Straight-skeleton** subdivision gives every parcel street access (with an optional central courtyard). **OBB recursive split** gives perimeter and interior parcels reached by alleys; the two can be mixed. Parcels are **mapped from before to after an edit**, so customisations survive changes to the roads around them. | `subdivide` offers **skeleton / split / mixed** with those attributes. **Persistence**: when a parent is regenerated, children are **matched to their previous selves** and keep their customisations and locks. |
| **Hierarchical Semantic WFC** (FDG 2023) | A **"consists-of" DAG** with **weights** (what fraction of x is y). Cells collapse level by level from the root. Constraints set on a meta-tile apply to its whole subtree. A weight of 0 forbids a child except as filler. The designer **brushes a meta-tile** ("village") at any level of detail. **Collapse paths are stored**, so "all houses" can be selected and regenerated as houses. | The module tree **is** this DAG. **Densities are edge weights**; constraints written at a parent apply to its subtree. A **module brush** paints a module over an area after the build ("forest here", "a market here"). The stored module path on each entity enables **semantic selection and regeneration**. |
| **ClarEval** (2026) | Ambiguity comes as a **missing goal**, **missing premises** or **ambiguous terminology**; vague terms ("large", "organize") are the hardest. Good agents **batch questions into few turns** and flag vague descriptors. Failures: silent guessing, generic questions, invented constraints, fragmented asking. | The window **batches** everything into one or two rounds. **Vague size words are always shown with their numbers.** Questions are specific (the field, options, a default), never "tell me more". Assumptions are labelled as assumptions. |
| **Active Task Disambiguation** (ICLR 2025) | **Sample several diverse interpretations**, then pick the questions whose answers **split them most evenly** (highest answer entropy). Answers become requirements. No principled stopping rule; fixed budgets were used. | Before the window opens, the AI drafts **several Briefs** for the instruction. Fields where the drafts **disagree** are highlighted as the questions; fields where they agree are pre-filled. Shape choices can show **alternative sketches** to pick from. |

What none of them do, and we must: **2D sprite packs** in two perspectives, **linear pieces**
(fences, walls) that join, **very large** 2D places, and **edit-after-build** in a game editor.

## 4. Two kinds of modules

The word *module* covers two things; both are fixed per build type.

1. **Stages** (Holodeck's modules): the pipeline steps a build type always runs, in
   order, e.g. *site → networks → districts → parcels → dressing*. Each stage is one
   kind of decision, made by one LLM query per node with its own schema and example.
2. **Place modules**: the reusable **parts** a place is made of: a *garden*, a
   *market square*, a *house plot*, a *tavern hall*, a *guard room*. A place is a
   **tree of module instances**: town → old-town ward → house plot → garden → vegetable
   bed.

Each stage decides which modules go where in its level of the tree; each module's
generator lays itself out. The catalogue of modules per build type is
`docs/place-modules.md`.

## 5. The place module contract

A module is **data** (`places/modules/<id>.module.json`; the engine ships a library and a
project can add or override). It defines:

| Part | Meaning | Example (garden) |
|---|---|---|
| `id`, `name`, `buildTypes` | identity; where it can appear | `garden`; settlement, farmstead, castle |
| `level` | site, zone, parcel, feature or dressing | feature |
| `size` | footprint range and shape | 3×2 to 8×6 cells, rectangle |
| `params` | typed, with defaults; shown in the confirmation window and the Inspector | `kind`: vegetable / flower / herb; `fenced`: yes / no; `density`: none–abundant |
| `slots` | asset roles it needs: required or optional, with counts | fence pieces (if fenced), crop or flower plants (required), path tiles, scarecrow (optional) |
| `children` | modules it may contain, with counts or densities | vegetable bed ×1–4, small shed (rare) |
| `relations` | to its parent and siblings, from the shared vocabulary | *behind* the house, *adjacent* to the back door, *away from* the street |
| `generator` | which engine algorithm lays it out, and with what | `rows` (beds), then `edge-line` (fence), then `scatter` (plants) |
| `checks` | its own verification rules | fence closed except a gate; ≥ 1 path from the gate to the door |
| `questions` | what the confirmation window asks about it, if anything | kind, fenced |
| `examples` | approved builds (the verified manual), used as few-shot examples | added automatically |

Missing assets for a slot follow the user's choice for that component: use the closest
pack asset, generate one in the pack's style, or leave the slot empty and report it.

### The relation vocabulary (2D, from Holodeck's)

| Kind | Relations |
|---|---|
| global (in the parent) | `center`, `edge`, `corner`, `along-edge(side)`, `anywhere` |
| distance | `adjacent`, `near`, `far`, `away-from` |
| position (relative to facing) | `in-front-of`, `behind`, `beside`, `between` |
| alignment | `aligned-with`, `in-row`, `in-grid`, `symmetric-about` |
| facing | `faces(target)`: street, square, object |
| linear | `along(road / river / wall)`, `encloses`, `crosses` |
| containment | `inside`, `outside`, `on` (small things on large ones: a pot on a table) |
| quantity | `count(n)`, `density(level)`, `cluster` vs `spread` |

**Hard** (never broken): inside the parent, no overlapping footprints, allowed terrain,
passability kept (doors, gates and entrances reachable), accessibility cells kept free.
**Soft** (weighted, best effort): everything else. Where no solution satisfies every soft
relation, the solver keeps the best and the checks report what was dropped.

### Generators (the tool library)

Each is a seeded engine command with a documented schema and examples, so the LLM uses it
like a tool (3D-GPT):

| Generator | Lays out | Used by |
|---|---|---|
| `partition` | zones from relations and an interest map (Voronoi / weighted growth) | site → districts |
| `route` | roads, rivers and walls as least-cost paths over terrain | networks |
| `street-network` | organic, grid or radial streets inside a zone | districts |
| `subdivide` | blocks into plots along streets | districts → parcels |
| `place` | relational grid solver, anchor first, large first (Holodeck's DFS) | parcels, features, rooms |
| `rows` / `grid` | crops, beds, pews, market stalls, shelves | features |
| `scatter` | Poisson disk with density maps and rules | dressing, nature |
| `edge-line` | fences, walls and hedges along cell edges, with piece sets | features, networks |
| `terrain` | autotiled regions: water, fields, sand, paths (6.4) | all |
| `floorplan` | rooms from a programme (types, sizes, adjacency) | interiors |
| `graph` | mission and space graphs, cycles, lock–key | dungeons |
| `cave` | cellular automata areas | dungeons, wilderness |

## 6. Understanding the art (asset catalog v2)

| Field | Found by | Used for |
|---|---|---|
| category, function, tags | vision model | slot matching |
| **footprint** (cells w×h) | alpha mask: the opaque base, not the image size | plots, collision, spacing |
| **anchor** | lowest opaque row and shadow | standing on the right cell |
| **sprite size** | from footprint and aspect, never guessed | scale |
| **perspective**: top-down, ¾ front, isometric | vision model + shape cues | compatibility (below) |
| **facing and variants** | name groups, mirror detection, vision | doors toward streets |
| **piece sets** for linear features | edge analysis like tiles | fences, walls, hedges |
| palette | dominant colours | coherent style; generated art matches |
| layer: ground, decal, prop, building, overhead | kind + transparency | draw order |
| passability, accessibility, placement rules | vision + per-category defaults | constraints |

**Both perspectives** are supported:
- **Top-down** art lies flat: ground, rugs, roads, crops.
- **¾ and isometric** art stands upright on its anchor: houses, trees, characters.

A project may mix them, as most packs do: a top-down ground with ¾ buildings. What must
match is the **grid**. Top-down linear pieces (fences drawn straight across the screen)
join only on a **square** grid; isometric pieces only on an **isometric** grid. The
catalog checks this when a module needs joined pieces. If they don't match, it offers to
generate matching pieces or to use the other grid.

Retrieval for a slot works in two steps:
1. Hard filters: role, perspective compatible, footprint fits.
2. Ranking by text and visual similarity, closeness to the size the slot wants, palette
   fit, and variety (not the same asset next door).

## 7. The methodology: stages per build type

Every build runs the same procedure. Its documents, the intermediate language and the
constraints are defined in `docs/place-schema.md`:

```
 Query → Interpret (Brief drafts) → Confirm (Brief) → Plan (Spec: quantity + quality per part)
       → Compile (Layout IR) → Solve & build → Verify        (any part, any time after: edit → regenerate)
```

The stages below are how **Plan** and **Compile** proceed through the levels of each
build type.

Each stage makes one kind of decision for every node at its level:
1. The LLM receives the node, its parent, the Brief, the module manual and verified
   examples.
2. It answers in the stage's schema: which child modules, their parameters and their
   relations.
3. Generators turn that into cells.
4. The checks run; failures are repaired locally (§8).

| Build type | Stages (in order) |
|---|---|
| **Settlement** (hamlet → city) | site & terrain → networks (main roads, river, walls & gates) → wards / districts → streets & plots → parcels (buildings, yards, features) → dressing → population (later) |
| **Region** (very big) | region terrain & biomes → settlements and landmarks (each a nested build) → roads between them → wilderness fill |
| **Interior** (house, tavern, shop, temple, castle hall) | room programme → floor plan → doors & windows → furniture groups (anchor first) → small things on surfaces → wall decor & lights |
| **Dungeon / cave** | mission graph (start, goal, keys, locks, boss, rewards) → space graph (cycles, branches) → geometry (rooms, corridors, caves) → room modules by type → encounters & loot → dressing |
| **Castle / fortress** | site & terrain → curtain wall, towers & gatehouse → baileys → buildings (keep, barracks, chapel) → features → dressing |
| **Wilderness** | biome & terrain → water & relief → trails → points of interest → vegetation |
| **Camp / outpost** | site → enclosure → tent and building clusters → features → dressing |
| **Farmland** | parcels (fields, pastures) → farmsteads → hedges, fences & tracks → dressing |

The style (biome, era, culture, mood) is set in the Brief **before** any stage, so every
module and asset choice agrees (Unexplored).

Design knowledge lives in two places: the stage prompts and the checks.
- **Settlements:** Lynch's paths, edges, districts, nodes and landmarks; a road hierarchy;
  a focal centre; landmarks at the ends of views; density falling outward; buildings
  facing streets; trades zoned by nuisance; natural boundaries; variety without chaos.
- **Interiors:** functional groups; circulation paths kept clear; furniture against walls;
  light by the work.
- **Dungeons:** the critical path; loops; pacing; landmark rooms; rising difficulty.

### The Brief and the confirmation window (every build)

The Brief holds:
- the build type and **scale**: a class plus numbers, from a scale table in project
  data, so *large village ≈ 80 buildings*, *town 150–500*, *city 500+*;
- the style;
- **components**: the top modules, with counts or densities;
- the shape or layout description, terrain features, location and gameplay hooks.

Each field has a confidence score.

**Round 1** (an editable table):
- size;
- shape (with a preview);
- components (rows for the build type's modules, each with a checkbox, the matching
  pack assets as thumbnails, and **source: pack / generate / auto**);
- style and location.

**Round 2**, for the chosen components:
- density (*none / rare / some / common / abundant*);
- module parameters marked as questions (e.g. garden kind);
- variants to include or exclude.

Buttons: **Build**, **Back**, **Cancel**. Answers become per-project defaults.

## 8. Verification and repair

Each stage's output is checked before the next stage builds inside it:

- **Hard rules, exact:**
  - footprints inside parents, no overlaps, allowed terrain;
  - every door, gate and entrance reachable (pathfinding);
  - roads connected;
  - zero autotile mismatches; linear features continuous with the right pieces;
  - accessibility cells free;
  - sizes within each slot's range.
- **Module checks:** each module's own (§5).
- **Brief conformity, measured:** counts, densities, area and shape within tolerance.
- **Visual critique:** the part is rendered headless, and a vision model answers the
  module's checklist (*floating, giant or tiny things? houses facing streets? varied?
  does it read as a market?*).
- **Gameplay (dungeons, later everything):** completability via lock–key relations; a
  walk-through agent.

Failures go back to the stage that made the part, with the reason as a constraint, for
**local repair** (that node only). After N tries, the part is kept with a report. Every
approved module build is stored as a **verified example** for future prompts
(WorldCraft's manual).

## 9. Building very big places

- **Chunked tile layers** (arrays per chunk, not one entity per tile).
- **Command-based undo** for bulk operations: a whole district is one cheap step.
- Scenes stored and loaded in **regions**; culling and rendering fast at 100k+ sprites.
- The **place document** (`places/<name>.place.json`) holds the module tree, parameters,
  seeds and relations. It is the source of truth for regeneration and editing.
- Stages run node by node with progress, resume after a restart, and parallel requests
  where nodes are independent (districts of one town).

## 10. Editing after the build

Every built entity carries its module instance (`PlaceNode {place, node}`):
1. **Click** something: the Inspector offers *select its garden / plot / ward*.
2. The module shows its **parameters**; change them (e.g. *density: abundant*) and
   **Regenerate** that node alone, keeping its parent and siblings.
3. **Lock** a node so later regenerations leave it alone; **hand edits** inside a node
   mark it locked.
4. Ask the AI: *"more trees in the east gardens"* → the right nodes, the right parameter,
   a regeneration, a check.

## Sources

- Holodeck — https://arxiv.org/html/2312.09067
- Infinigen Indoors — https://arxiv.org/pdf/2406.11824
- WorldCraft — https://arxiv.org/html/2502.15601v1
- 3D-GPT — https://arxiv.org/pdf/2310.12945
- Dungeon generation in Unexplored (Boris the Brave) — https://www.boristhebrave.com/2021/04/10/dungeon-generation-in-unexplored/
- Dormans, Adventures in Level Design — https://www.pcgworkshop.com/archive/dormans2010adventures.pdf
- Medieval Fantasy City Generator (Watabou) — https://watabou.itch.io/medieval-fantasy-city-generator
- CityGenAgent — https://arxiv.org/pdf/2602.05362
- Constraint Is All You Need — https://www.pcgworkshop.com/archive/xu2025constraint.pdf
- SceneCraft — https://arxiv.org/pdf/2403.01248
- Narrative-to-Scene Generation — https://arxiv.org/pdf/2509.04481
- GameTileNet — https://arxiv.org/pdf/2507.02941
- Procedural Generation of Villages on Arbitrary Terrains — https://perso.liris.cnrs.fr/egalin/Articles/2012-villages.pdf
- Procedural Generation of Parcels in Urban Modeling — https://www.cs.purdue.edu/cgvlab/papers/aliaga/eg2012.pdf
- Hierarchical Semantic WFC — https://pcgworkshop.com/archive/alaka2023hierarchical.pdf
- ClarEval — https://arxiv.org/pdf/2603.00187
- Active Task Disambiguation with LLMs — https://arxiv.org/pdf/2502.04485
