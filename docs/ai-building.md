# AI-Generated Building — design (Phase 7)

Status: **design, revised after discussion (2026-10-18)**. The module catalogue is in
`docs/place-modules.md`; the milestones are in `development_plan.md`, Phase 7.

## 0. Approach (article form)

Recent work on language-driven scene and level generation agrees on a central point:
large language models should decide *what* a place contains and how its parts relate,
while numerical solvers decide *where* things go. Holodeck asks an LLM for spatial
relations, which a constraint solver then satisfies [1]. SceneCraft represents a scene as
a relation graph with differentiable scores and revises it from rendered images [2].
GLDL turns narrative into definition and constraint sentences that are solved as a
facility-layout problem [3]. The database-driven framework moves the LLM's knowledge into
reviewed databases, so assembly needs no model calls [4]. CityGenAgent splits a city into
editable block and building programs [5]. In parallel, procedural modelling has developed
strong algorithms for the geometric layer: villages grown from interest maps [6], parcel
subdivision with street access [7], hierarchical tile synthesis [8], and mission-and-space
graphs for dungeons [9]. Semantic approaches to game worlds attach classes and placement
rules to objects [10, 11].

We combine these strands in a single procedure for 2D sprite-based worlds of any scale:
*query → interpret → confirm → plan → compile → solve → verify*. Each step writes a typed,
constrained document, as follows.
- **Brief.** The confirmed request. The user confirms it in a window generated from its
  schema. Questions are batched and placed where several sampled interpretations
  disagree [12, 13].
- **Spec.** A plan tree whose nodes carry quantity fields (counts, densities with numeric
  meaning, areas) and quality fields (closed vocabularies). Aggregate constraints are
  checked across the tree.
- **Layout IR.** A constraint language that keeps GLDL's core terms [3] and extends them
  with hierarchy, n-ary group relations [2], linear features, networks, subdivision,
  scatter and terrain.

The main contribution is an explicit **place ontology**: a knowledge graph of build types,
modules, fields, vocabularies, asset roles, spatial relations and design rules, each fact
with its source. Every step queries the ontology:
- the interpreter maps words to concepts;
- the planner draws allowed children, typical weights, requirements and interest
  preferences from it;
- the compiler takes the meaning of relations from it;
- asset retrieval matches catalogued sprites (with measured footprint, anchor and
  perspective) to roles;
- verification applies the rules the ontology attaches to each class.

Earlier semantic libraries guided layout solvers [10, 11], and LLM systems used ad-hoc
prompts or learned programs [1–5]. To our knowledge, no prior system uses a shared
ontology both to **ground the LLM's planning** and to **define the constraints that are
compiled and checked**. Because the ontology is explicit, the system can explain its
decisions. It can also grow by reviewed, AI-proposed extensions and by weights learned
from verified builds.

The approach adds three further elements:
- **Scale:** the LLM plans only the upper levels, and ontology-driven expansion rules fill
  the rest, so a city needs tens of model calls rather than thousands.
- **Verification:** each level is verified (hard rules, conformity tolerances, rendered
  critique [2, 5]) and repaired locally before the next is built inside it.
- **Editing after generation:** every built entity keeps its link to its plan node. An
  edited or regenerated subtree preserves locked work, by matching children across
  regenerations as in parcel persistence [7].

**References**

1. Y. Yang et al. *Holodeck: Language Guided Generation of 3D Embodied AI Environments.* CVPR 2024.
2. Z. Hu et al. *SceneCraft: An LLM Agent for Synthesizing 3D Scenes as Blender Code.* ICML 2024.
3. K. Xu, C. Verbrugge. *Constraint Is All You Need: Optimization-Based 3D Level Generation with LLMs.* FDG 2025.
4. *A Database-Driven Framework for 3D Level Generation with LLMs.* AIIDE 2025.
5. Z. Liu et al. *Imagine a City: CityGenAgent for Procedural 3D City Generation.* arXiv:2602.05362, 2026.
6. A. Emilien, A. Bernhardt, A. Peytavie, M.-P. Cani, E. Galin. *Procedural Generation of Villages on Arbitrary Terrains.* The Visual Computer, 2012.
7. C. Vanegas, T. Kelly, B. Weber, J. Halatsch, D. Aliaga, P. Müller. *Procedural Generation of Parcels in Urban Modeling.* Eurographics 2012.
8. S. Alaka, R. Bidarra. *Hierarchical Semantic Wave Function Collapse.* FDG 2023.
9. J. Dormans. *Adventures in Level Design: Generating Missions and Spaces for Action Adventure Games.* PCG Workshop 2010.
10. T. Tutenel, R. M. Smelik, R. Bidarra, K. J. de Kraker. *Using Semantics to Improve the Design of Game Worlds.* AIIDE 2009.
11. R. M. Smelik, T. Tutenel, K. J. de Kraker, R. Bidarra. *A Declarative Approach to Procedural Modeling of Virtual Worlds.* Computers & Graphics 35, 2011.
12. *ClarEval: A Benchmark for Evaluating Clarification Skills of Code Agents under Ambiguous Instructions.* arXiv:2603.00187, 2026.
13. *Active Task Disambiguation with LLMs.* arXiv:2502.04485, 2025.

Also used in the design: Infinigen Indoors (Raistrick et al., CVPR 2024), WorldCraft
(arXiv:2502.15601), 3D-GPT (Sun et al.), Narrative-to-Scene Generation (Chen & Jhala,
2025), GameTileNet (AIIDE 2025), Unexplored's cyclic generation (Dormans), Watabou's
Medieval Fantasy City Generator, Parish & Müller (SIGGRAPH 2001), Lynch, *The Image of
the City* (1960).

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
| Assets | Per component, the source is chosen in the confirmation window: **auto**, the **project**, the **AIGE library** (planned), the **web**, or **generate** with the Art tool. `auto` follows a fixed decision procedure (`place-assets.md`). |
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

## 5. Modules, relations and generators, in brief

The definitions live elsewhere; this is the overview.
- **A place module** is data: its quantity and quality fields, asset **slots** bound to
  ontology roles, allowed children with typical weights, aggregate constraints, default
  relations, and a **compile template** written in PLL. It is defined in
  `place-schema.md` §3.3; the catalogue is `place-modules.md`; the files are part of the
  ontology (`place-ontology.md`).
- **Relations** are n-ary, each with a score function in [0, 1], hard or weighted. The
  vocabulary covers global position, distance, relative position, alignment and groups,
  facing, linear features, containment, visibility, and interest maps. It is defined in
  `layout-ir.md` §6.
- **Generators** are the PLL statements, each one a seeded solver:

| What it lays out | PLL statement | Used by |
|---|---|---|
| zones from relations and interest maps | `partition` | site → districts |
| roads, rivers (least-cost, road reuse) | `path` | networks |
| street networks (grid, radial, organic, growth) | `network` | districts |
| blocks into plots with street access | `subdivide` | districts → parcels |
| rooms from a programme | `floorplan` | interiors |
| mission and space graphs, lock–key | `graph` | dungeons |
| cave-shaped areas | `region` with `shape: cave` | dungeons, wilderness |
| doors and openings between areas | `connect` | interiors, dungeons, castles |
| objects by relations (anchor first, large first, then annealing) | `place` | parcels, features, rooms |
| beds, crops, pews, stalls; orchards, graves | `rows`, `grid` | features |
| plants, rocks, clutter by density | `scatter` | dressing, nature |
| fences, walls, hedges on cell edges | `line` | features, networks |
| autotiled terrain | `fill` | all |

## 6. Understanding the art (asset catalog v2)

| Field | Found by | Used for |
|---|---|---|
| category, function, tags | vision model | slot matching |
| **footprint** (cells w×h) | alpha mask: the opaque base, not the image size | plots, collision, spacing |
| **anchor** | lowest opaque row and shadow | standing on the right cell |
| **sprite size** | from footprint and aspect, never guessed | scale |
| **perspective**: top-down, ¾ front, isometric | vision model + shape cues | compatibility (below) |
| **facing and variants** (`facings`: which sides the art can face: flat art any, upright art drawn + mirrored, plus directional variants) | name groups, mirror detection, vision | doors toward streets |
| **anchors** on the art: door, gate, seat, counter front | vision model on the upscaled image, then confirmed | `x.door` references; doors face streets; paths to back doors |
| **piece sets** for linear features | edge analysis like tiles | fences, walls, hedges |
| palette | dominant colours | coherent style; generated art matches |
| layer: ground, decal, prop, building, overhead | kind + transparency | draw order |
| passability, accessibility, placement rules | vision + per-category defaults | constraints |
| affordance (GameTileNet's five), tile type (complete, partial, texture) | vision + segmentation | retrieval filters |
| licence, author, source, url | the source (project, library, web, generated) | the licence check; CREDITS |

**Both perspectives** are supported:
- **Top-down** art lies flat: ground, rugs, roads, crops.
- **¾ and isometric** art stands upright on its anchor: houses, trees, characters.

A project may mix them, as most packs do: a top-down ground with ¾ buildings. What must
match is the **grid**. Top-down linear pieces (fences drawn straight across the screen)
join only on a **square** grid; isometric pieces only on an **isometric** grid. The
catalog checks this when a module needs joined pieces. If they don't match, it offers to
generate matching pieces or to use the other grid.

Where each needed asset comes from (the project, the AIGE library, the web or
generation), how candidates are filtered and scored, and what happens when nothing fits:
all of this is defined in `place-assets.md`.

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
1. The LLM receives the node, its parent, the Brief, the relevant ontology subgraph and
   verified examples.
2. It **thinks** in free text, then answers in the stage's schema: which child modules,
   their fields, their relations, and a short rationale. How each call is made is defined
   in `place-interaction.md` Part B.
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
  assets as thumbnails, and a **source**: auto / project / library / web / generate);
- style and location.

**Round 2**, for the chosen components:
- density (*none / rare / some / common / abundant*);
- module parameters marked as questions (e.g. garden kind);
- variants to include or exclude;
- the **asset plan**: every needed asset, its proposed source and alternatives
  (`place-assets.md` §6).

Buttons: **Build**, **Back**, **Cancel**. Answers become per-project defaults. For large
places, a **plan preview** in the viewport follows before building. All of this happens
in a dedicated, dockable **Build panel**, not the narrow AI chat panel, which keeps a
compact card per build. Everything the user sees is defined in `place-interaction.md`
Part A.

## 8. Verification and repair

Each stage's output is checked before the next stage builds inside it:

- **Hard rules, exact:**
  - footprints inside parents, no overlaps, allowed terrain;
  - every door, gate and entrance reachable (pathfinding);
  - roads connected;
  - zero autotile mismatches; linear features continuous with the right pieces;
  - accessibility cells free;
  - sizes within each slot's range.
- **Module checks:** the `check` statements of each module's compile template.
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
- The **place documents** (`places/<place>/`: Brief, assets, Spec, PLL, solved layout,
  report, log; `place-schema.md` §1) are the source of truth for regeneration and
  editing. Nested places (a region's towns, a town's castle) each have their own folder.
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

## 11. Design review (2026-10-18)

Before development, the design documents were reviewed together. These changes came out
of it:

| Finding | Change |
|---|---|
| **Solving-order bug:** a garden (a region) had to be solved before the house it sits behind (a placement), so global phases could not express "behind the house" | PLL now solves **top-down through the region tree**, with a fixed order of steps inside each region. Each region's **primary** object (house, well, anchor furniture) comes before its sub-regions (`layout-ir.md` §7). |
| Constraints over **ranges** had no defined meaning | Interval semantics: satisfiable if some choice within the ranges meets it; the compiler picks and records the values (`place-schema.md` §4.3). |
| A plan constraint used a value known only after solving (footprint density) | Plan-time constraints use Spec values only. Solve-time conditions are PLL `check`s emitted by compile templates. |
| Interiors and dungeons had no statements for rooms, doors, graphs or caves | Added `floorplan`, `graph`, `connect` (with locks and keys, and lock-aware reachability), and `shape: cave`. |
| The module contract, relation list and generators were **duplicated** across documents, with drifting names | One authority per subject (the table at the top of `place-schema.md`); the others summarise and point to it. |
| Slots named roles informally ("crop or flower by kind") | Slots **bind** to ontology roles, with concepts drawn from quality fields and conditional bindings. |
| Compile rules had no format | **Compile templates**: PLL with placeholders, `when` and `each`. |
| A module listed both its parents and its children | Children are authoritative; parents are derived. |
| Module files and the ontology were two possible sources | Module files **are part of** the ontology. |
| The Spec example's garden mix didn't match its Brief | Two garden nodes with shares 0.36 and 0.24 (60% of plots, 60 / 40 mix). |
| Doors were referenced (`p14.door`) but the catalog had no door positions | The catalog records **anchors** on the art and the **facings** each asset supports. |
| Asset sources were only "pack / generate / auto" | Project, AIGE library, web and generate, with a decision procedure (`place-assets.md`). |
| No licence rule | An allowed licence is a hard rule, with CREDITS written automatically. |
| Nested places were not defined | `subplace` regions and child place folders (`place-schema.md` §3.5). |
| When the confirmation window opens was unclear | Every new build, and any regeneration that changes Brief fields; single-node edits don't open it. |
| What the user is shown, and how the AI is asked, were not defined | `place-interaction.md`: the stages, the result card and "Why?"; thinking before a strict answer. |

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
