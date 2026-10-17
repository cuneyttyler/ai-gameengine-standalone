# AI-Generated Building — design (Phase 7)

Status: **proposal for discussion** (2026-10-17). Milestones are in
`development_plan.md`, Phase 7.

## 1. Goal

One instruction — *"build a large medieval town by a river"* — becomes a finished,
playable place, fully automatically if wanted, at any size: a hamlet of 10 houses or
a town of 400, a three-room cave or a 12-level dungeon. The engine must:

1. understand **what was asked**, including how big, and ask when it matters;
2. understand **its art**: what each asset is, how big, how it faces, how it joins;
3. **plan** in a structured, repeatable way: the whole place first, then the parts;
4. **build** step by step, big places included;
5. **test and verify** every part, and repair what fails.

## 2. What we learned from 6.5

The village generator was a single fixed recipe (streets in bands, plots, a square).
Testing showed its limits:

| Problem | Cause |
|---|---|
| "Large" gave 20–30 houses | No notion of scale: the AI picked a number. |
| Fences wrong | Two causes. (a) The project is **isometric** but the pack is drawn **top-down**: its fences run left–right and up–down *on screen*, while isometric grid edges are diagonals, so no piece can line up. (b) One piece per cell at its centre, with no corners, ends or gates. |
| Some props mis-sized | Sizes were guessed from pixels when the art had no analysis; nothing checked the result. |
| No variety in shape | One layout recipe; no way to say "along the river", "walled", "radial". |
| Failures discovered by the user | Nothing looked at the result. |

The research points the same way.

## 3. What exists (research)

- **The LLM decides intent, a solver decides geometry.** In Holodeck, the LLM proposes
  spatial *relations* and an optimizer places objects to satisfy them. SceneCraft turns a
  scene graph into constraints and refines it from rendered images. "Constraint Is All You
  Need" (FDG 2025) turns narrative into a level description language, solved as
  facility-layout optimization, and checks navigability by simulation. LLMs are weak at
  raw coordinates and good at relations and counts.
- **Hierarchical, program-like plans.** CityGenAgent (2026) splits a city into a *Block
  Program* and a *Building Program*: interpretable and editable in natural language after
  generation. Text-to-3D City uses a planner that writes a structured plan and a
  procedural implementer.
- **Classic settlement generation.** Emilien et al. (*Procedural Generation of Villages on
  Arbitrary Terrains*, 2012) work from interest maps, growing settlement seeds and the
  roads that join them, then parcels and buildings. Vanegas et al. (2012) subdivide blocks
  into parcels. CityEngine uses L-systems for roads and grammars for blocks.
- **Large tile maps.** Hierarchical Semantic WFC (FDG 2023) sketches big areas with
  abstract tiles and resolves each into concrete ones. Layered generators go region →
  block → tile. At scale, *local repair* replaces global backtracking.
- **2D asset semantics.** GameTileNet (AIIDE 2025) and *Narrative-to-Scene* (2025) label
  low-resolution game art and map relations (above, left-of, on-top-of) to placements.
  They report that small pixel art is hard for vision models, so pixel analysis must back
  the model up.
- **Clarifying questions.** Work on ambiguous instructions (ClarEval, Active Task
  Disambiguation, Learning to Ask) finds models usually guess silently. Asking pays off
  only when the question narrows the result, and the agent must also know when to stop
  asking.

## 4. Principles

1. **Plans are data.** Every level of the plan is a document the user can read, edit and
   regenerate from, not a chat message.
2. **The AI decides; algorithms place.** The model writes intent: zones, relations,
   counts, styles. Deterministic, seeded generators turn it into cells. Same plan and
   seed, same place.
3. **Coarse to fine.** Site → districts → blocks and plots → buildings → dressing. Each
   level is fixed and checked before the next is built inside it.
4. **Understand the art before using it.** Footprint, anchor, perspective, facing,
   variants and joints are measured, not assumed.
5. **Verify everything that can be measured; look at the rest.** Rules check structure;
   a vision model checks what rules can't.
6. **Ask little, but ask the right things.** One confirmation card with good defaults, a
   second only when the choice of components opens new questions, and "just build it"
   always available.

## 5. The pipeline

```
 instruction ──► 1 Brief ──► 2 Confirm ──► 3 Site plan ──► 4 Part plans ──► 5 Build ──► 6 Verify
                   ▲            (user)         │                 │              │            │
                   │                           └──── edit / regenerate a part ◄─┴── repair ◄─┘
             0 Asset catalog (always available)
```

### 0. Asset catalog: understanding the art

The 6.2 analysis becomes a full catalog entry per asset, from pixels first, then a vision
model, confirmable by the user:

| Field | How it's found | Used for |
|---|---|---|
| category, function, tags | vision model | roles: house, well, market stall |
| **footprint** (cells w×h) | alpha mask: the opaque base, not the image size | plots, collision, spacing |
| **anchor** (base point) | lowest opaque row / shadow | standing on the right cell |
| **sprite size** | footprint + aspect | correct scale, never guessed |
| **perspective** (top-down, ¾, isometric) | vision + shape cues | matching the grid; warning on mismatch |
| **facing / variants** | name groups, mirrored-image detection, vision | doors toward streets |
| **joint pieces** (linear features) | edge analysis, like tiles | fences, walls, hedges: straight, corner, end, T, gate |
| **colors / palette** | dominant colours, clustering | coherent style; matching generated art |
| **layer** (ground, decal, prop, building, overhead) | kind + transparency | draw order, what can overlap |
| **passability**, placement rules | vision + defaults per category | "on grass", "by water", "against a wall" |

Linear features get their own model. A fence or wall is a **piece set**, and it is laid
along cell *edges* with corners, ends and gates chosen like autotiles. A pack with only
straight pieces gets posts at the corners, or a warning that names what's missing.

**Perspective check.** Art drawn top-down on an isometric grid (or the reverse) is
flagged before building, with the fix: switch the grid style or use another pack.

### 1. Brief: understanding the request

The AI turns the instruction into a **Brief** with a confidence score for every field:

| Field | Example | Notes |
|---|---|---|
| place type | village, town, city, castle, dungeon, cave, camp, farmstead | picks the methodology |
| **scale** | class + numbers: *large village ≈ 80 buildings, ~120×120 cells* | from a scale table, below |
| theme / style | medieval, desert, ruined | picks assets |
| **components** | houses, market, well, church, farms, walls, river, forest | each with a count or density |
| **shape** | organic, grid, radial, linear (along a road or river), walled, terraced | or a free description |
| features | river through the middle, hill, lake, existing gate to connect | terrain and anchors |
| placement | where in the scene; what to keep away from | |
| gameplay | player start, entrances, NPCs, quest spots | optional |

**Scale table** (project data, editable). This is what makes "large" mean large:

| Class | Buildings | Area (cells, about) | Built by |
|---|---|---|---|
| hamlet | 5–15 | 40×40 | one step |
| village | 15–60 | 80×80 | one step |
| large village | 60–150 | 140×140 | districts |
| town | 150–500 | 250×250 | districts, chunked |
| city | 500+ | 400×400+ | districts, chunked, over several sessions |

Dungeons use their own measures: rooms, levels, critical-path length.

### 2. Confirm: the questions

**Round 1, the Brief card** (an editable table in the AI panel):

- Size: class (select) plus building count and area (numbers).
- Shape: select plus a description field. A small preview diagram per shape.
- Components: rows with checkboxes, each showing the assets found for it as thumbnails
  ("none in the pack" in red, with *find* / *generate*).
- Style and location.
- Buttons: **Continue**, **Just build it** (all defaults), **Cancel**.

**Round 2, densities** (only for the chosen components): a categorical select per
component (*none / rare / some / common / abundant*), plus asset variants to include or
exclude. Defaults come from the place type and are remembered per project.

The AI asks only about fields whose confidence is low **and** whose answer changes the
result. A detailed instruction can skip straight to the plan.

### 3. Site plan: the whole place

The AI writes the site plan in a small, typed **plan language**, which a validator checks
like proposals are checked today:

```json
{
  "bounds": [0, 0, 250, 250], "seed": 7,
  "terrain": [{"river": {"through": [[0,120],[250,140]], "width": 4}},
              {"forest": {"area": "edge", "density": "some"}}],
  "roads": [{"main": {"from": "west-gate", "to": "east-gate", "via": "market"}},
            {"ring": {"around": "old-town"}}],
  "districts": [
    {"id": "market", "role": "center", "near": "river", "size": "small", "contains": ["market-square", "well", "church"]},
    {"id": "old-town", "role": "residential", "around": "market", "density": "dense"},
    {"id": "crafts", "role": "workshops", "downstream-of": "old-town"},
    {"id": "farms", "role": "fields", "outside": "walls", "along": "river"}],
  "walls": {"around": ["market", "old-town", "crafts"], "gates": ["west-gate", "east-gate"]},
  "landmarks": [{"castle": {"on": "highest", "view-from": "main"}}]
}
```

The site generators turn this into geometry:
- zone partitioning from relations (near, around, outside, along, downstream-of);
- arterial roads routed by cost over terrain;
- wall rings with gates where roads cross.

The **plan view** draws the result over the viewport (zones, roads, walls, labels) so it
can be checked and edited *before anything is built*.

The design knowledge lives in the prompt **and** in the critics. Kevin Lynch's five
elements (paths, edges, districts, nodes, landmarks) are the vocabulary, with these rules
of thumb:
- a road hierarchy (main → street → lane);
- a focal point at the heart, and landmarks at the ends of main views;
- density falling from the centre to the edge;
- buildings facing streets;
- noisy or smelly trades downstream or outside;
- natural edges (rivers, hills) as boundaries;
- variety without chaos: no identical neighbours, mirrored and alternate variants;
- readable navigation for the player.

For dungeons, the same roles map to:
- a critical path with side branches;
- loops (cyclic generation);
- locks and keys;
- difficulty rising with depth;
- landmark rooms.

### 4. Part plans: the districts, then the plots

Each district is planned by itself, inside its fixed borders and road entries:
1. **streets** (organic, grid or radial generator);
2. **blocks → plots** (parcel subdivision);
3. **buildings on plots**, facing the street, with gardens and fences;
4. **open spaces** (squares, yards).

Then **dressing**: Poisson-disk scatter with density maps and placement rules (barrels by
doors, carts by the market, trees thicker toward the forest edge, flowers in gardens).
Every generator is a seeded engine command, so a part can be **regenerated alone**:
*"redo the crafts district with more workshops"*.

### 5. Build: step by step, at any size

- Built level by level and district by district. Each part is one **command** (bulk, one
  undo step), never thousands of single operations.
- Large places go chunk by chunk with progress, can be resumed, and keep the plan
  document in the project (`places/<name>.place.json`).
- Review policy: auto-apply each verified part, or review each one. The plan view shows
  what is done.

**Engine prerequisites for big places** (an honest list):
- At 250×250 cells, one entity per tile is 62,500 entities before any props.
- Whole-scene snapshot undo was already slow for resizing (fixed by grouping, but still
  O(scene) per step).
- Needed:
  - **chunked tile layers** (tiles as arrays per chunk, not entities);
  - **command-based undo** for bulk operations;
  - scene **regions** loaded and saved in parts;
  - culling that stays fast at 100k sprites.

### 6. Verify: test every part

Each part passes its checks before the next is built on it:

- **Structure** (exact):
  - footprints inside plots, nothing overlapping, nothing standing on roads;
  - every door reachable from an entrance (pathfinding);
  - roads connected;
  - zero autotile mismatches;
  - fence and wall lines continuous, with the right pieces;
  - assets only on allowed terrain;
  - sizes within each role's range.
- **Brief** (measured): counts, densities, area and shape within tolerance. "Large" is
  checked against the scale table.
- **Visual** (vision model; this is 6.7 grown up): the part is rendered headless, and the
  image is checked against the Brief plus a checklist:
  - Does anything look floating, giant or tiny?
  - Do houses face the streets?
  - Is it varied?
  - Does it read as a market square?
  Any findings come back as targeted fixes.
- **Play** (later): an agent walks entrance → landmarks → every district and reports
  dead ends, chokepoints and travel times.

Failures trigger **local repair** (regenerate that part with the reason as a
constraint), up to a limit, then a report to the user.

### 7. Assets: find or generate

For each needed component:
1. the project catalog;
2. the user's indexed libraries (other packs on disk);
3. generation in the pack's style, using the art pipeline with the palette, perspective
   and a reference image from the catalog;
4. a labelled placeholder, reported.

Generated assets go through the same catalog analysis, so they are sized and joined
correctly.

## 8. Open questions for discussion

1. **Target scale for 7.x.** Is a town (~250×250, ~300 buildings) the goal, or a city?
   This decides how much of the engine work in §5 comes first.
2. **Questions:** always show the Brief card, or only when the instruction is vague? (The
   proposal: always for new places, with *Just build it*.)
3. **Plan editing:** is editing the plan view by hand (drag zones, reroute roads) needed in
   7, or is editing through chat enough at first?
4. **Grid and perspective:** should a project be able to mix perspectives (top-down
   ground with ¾ buildings, common in packs), or is one style per project enough?
5. **Asset sourcing:** which libraries may be searched (local folders only, or online free
   packs), and is generation allowed by default?
6. **Model budget:** a town is dozens of model calls (planning, critique). What's an
   acceptable time and cost per place?

## Sources

- Holodeck: Language Guided Generation of 3D Embodied AI Environments — https://arxiv.org/pdf/2312.09067
- SceneCraft: An LLM Agent for Synthesizing 3D Scenes as Blender Code — https://arxiv.org/pdf/2403.01248
- Constraint Is All You Need: Optimization-Based 3D Level Generation with LLMs — https://www.pcgworkshop.com/archive/xu2025constraint.pdf
- A Database-Driven Framework for 3D Level Generation with LLMs — https://arxiv.org/html/2508.18533v1
- Imagine a City: CityGenAgent for Procedural 3D City Generation — https://arxiv.org/pdf/2602.05362
- Narrative-to-Scene Generation: An LLM-Driven Pipeline for 2D Game Environments — https://arxiv.org/pdf/2509.04481
- GameTileNet: A Semantic Dataset for Low-Resolution Game Art in PCG — https://arxiv.org/pdf/2507.02941
- Procedural Generation of Villages on Arbitrary Terrains (Emilien et al.) — https://perso.liris.cnrs.fr/egalin/Articles/2012-villages.pdf
- Procedural Generation of Parcels in Urban Modeling (Vanegas et al.) — https://www.cs.purdue.edu/cgvlab/papers/aliaga/eg2012.pdf
- Hierarchical Semantic Wave Function Collapse — https://pcgworkshop.com/archive/alaka2023hierarchical.pdf
- ClarEval: clarification under ambiguous instructions — https://arxiv.org/pdf/2603.00187
- Active Task Disambiguation with LLMs — https://arxiv.org/pdf/2502.04485
