# From a request to a layout

Status: **2026-10-20**. How AIGE turns a natural-language request ("build a village by a
river with several parts") into a laid-out place, with every decision taken on the way
and why. It is written as source material for the paper (`docs/paper/gbo-scene-generation.tex`)
and uses its terms: **GBO** (the GenBuilding Ontology), the **Brief**, the **Spec**, and
**PLL** (the Place Layout Language). Citation keys in brackets are the paper's.

The document has four parts:
1. the pipeline as it is built (§1–§9);
2. a case study of a bad layout and why it happened (§10);
3. the changes proposed to make layouts reliably good (§11);
4. a table of the design decisions, and open questions (§12–§13).

Related documents: `place-interaction.md` (what the user sees, how the AI is asked, B7: the
three readings), `place-schema.md` (the Brief and the Spec), `place-ontology.md` (GBO),
`layout-ir.md` (PLL and its solver), `fixes.md` (changes after Phase 7).

---

## Terms

| Term | What it is |
|---|---|
| **Build type** | settlement, farmland, wilderness, camp, castle, region, dungeon, interior. Each has a root module, a scale table and a **unit** it is counted in (buildings, rooms, tents, sites...). |
| **Module** | A kind of part (GBO): a level (site, zone, network, parcel, feature, dressing), typed quantity and quality fields, asset slots, child rules, relations, site preferences. |
| **Zone** | A module of level *zone*: a ward of a settlement, a dungeon level, a floor. The layout is the arrangement of zones. |
| **Brief** | The confirmed description of what to build: build type, scale (a class and numbers), components (on/off, how many, how common, their fields), style, features. |
| **Spec** | The plan tree: root → zones (and networks) → parcels → features, each with its fields, relations and rationale. |
| **PLL / IR** | The compiled layout program: regions, partitions, paths, networks, subdivisions, places, scatters, fills, checks (`layout-ir.md`). |
| **Solved layout** | Every region's cells, every object's footprint, every path's cells, the terrain. |
| **Layout (the step)** | The zones alone, solved and drawn, before anything inside them is planned. |

---

## 1. The pipeline

```
request (words, and optionally the choices form)
  │
  ├─ 1 Read: three readings → three Briefs → checked, repaired → merged    (§2)
  ├─ 2 Complete the Brief: scale numbers, unit, defaults                     (§3)
  ├─ 3 Plan the zones (the Spec's top level): the AI                         (§4)
  ├─ 4 Compile the zones into a PLL partition                                (§5)
  ├─ 5 Solve the partition: seeds, growth by interest                        (§6)
  ├─ 6 Show the layout; the user edits, paints, revises                      (§7, §8)
  └─ 7 Plan inside the zones, compile and solve the whole place               (§9)
```

Steps 1–6 are the **① Confirm** step of the Build panel (tabs *Contents* and *Layout*);
step 7 is **② Plan**. The design principle throughout: the AI decides *what* and *where in
words and relations*; deterministic, seeded code decides *which cells*. Every AI answer is
validated against GBO before it's used, and every step has a fallback that needs no AI.

---

## 2. Reading the request

### 2.1 Three readings

The request is read three times, in parallel, each with its own instruction:

| Reading | Instruction |
|---|---|
| literal | decide only what the request says or clearly implies; defaults otherwise, said in the assumptions |
| typical | as an experienced level designer would: what a place like this typically has, sized to what was asked |
| generous | a rich version (more components, livelier densities), never contradicting the request |

**Decision: several readings, not one.** Their disagreements are exactly what the request
leaves open, so they become the questions shown to the user. This follows active task
disambiguation, which samples several interpretations to find what to ask [kobalczyk2025],
and avoids the silent guessing that clarification benchmarks penalise [clareval].
Cost: each reading is two calls (below), six in all, usually 30–60 s together. One reading
would cost a third, without the disagreement marks.

### 2.2 What the AI is given and asked

- **Input:** the request; the build type's options from GBO (its components with their
  asked fields and allowed values, its scale table, its Brief fields, the vocabularies);
  and the choices the user set in the form, if any.
- **Only the named build type** goes in the context: the one the request's words name
  (each build type has naming words: "cave", "crypt" → dungeon; "village" → settlement;
  a `rank` breaks ties toward the containing type, "a kingdom with towns" → region), or the
  one chosen in the form; all types only when the words name none. **Decision:** with eight
  build types the full context is 36 KB; a village's alone is 8 KB. The larger prompt made
  reading several times slower without helping.
- **Output** (strict JSON): the Brief; a confidence per field; **decisions**, each
  `{question, assumed, path, options}` (an open point, the answer this reading chose, the
  Brief field it sets); other assumptions; a rationale.

### 2.3 How the AI is asked: think, then answer

Every AI step has the same shape: inputs → **think** (free text) → **answer** (strict
schema) → validate → repair once (`place-interaction.md` B1–B6).

- **Two-pass** by default: call 1 thinks in free text with all inputs; call 2 writes the
  answer in the schema, given the inputs and the thinking. **Decision:** strict formats
  degrade reasoning [tam2024], so the reasoning is never asked to be JSON. Models that think
  natively (Claude with adaptive thinking) use one call.
- **A ceiling on each call:** about 3,000 tokens of thinking and 6,000 of answer, and 180 s
  before a call is retried (three attempts). **Decision:** without a ceiling, a call that
  ran on occasionally held a step for five minutes.
- **A self-check in the thinking:** each step's task ends with a checklist (do the counts
  add up, is every component placed...).

### 2.4 Check, repair, merge

1. Each Brief is completed (§3) and validated against GBO (`ValidateBrief`): components of
   the build type only, values of their fields' types and vocabularies, the scale class
   consistent with its numbers.
2. A Brief that breaks a rule goes back to the AI **once**, with the exact violations.
3. What still breaks and needs no question is fixed without AI: an unknown component is
   dropped, an unreadable value falls back to its default (with a notice).
4. **Merge** (`MergeBriefs`): each field takes the value most readings agree on, a tie
   going to the typical reading; a component is on if most readings have it on. A field
   the readings disagree on is marked *differ*, with the others' values to pick; one with
   confidence below 0.6 is *guessed*. Decisions merge one per point (by field, else by
   question), the other readings' answers becoming its options.
5. **The form wins:** what the user set in the choices form is applied over the merged
   Brief and is never marked as disputed.

### 2.5 Normalising what the AI writes

Language models write near-misses. These are mapped, not rejected:
- density words ("medium", "dense", "sparse") → the five levels (some, common, rare...);
- a zone's count far above what its scale class has in zones ("count: 10–40" for the
  residential ward meant houses) → its buildings (`params.plots`), the count back to 1;
- synonyms of vocabulary values (each concept's `alt` labels).

---

## 3. The Brief, completed

- **Scale:** a class (hamlet, village, large village, town, city...) with its numbers
  (buildings, area side, zone count), from the build type's scale table; the numbers win
  over the class and move it.
- **Unit:** what the place is counted in (`unit`: field, class, label). The Brief's
  `scale.buildings` counts it; a settlement counts building parcels, a dungeon rooms, a
  region sites. **Decision:** one size field per build type, named by its unit, instead of
  a "buildings" field that means rooms for a dungeon.
- **Defaults** from the project's earlier answers (style, asset sources), then GBO's.

---

## 4. Planning the zones

### 4.1 The step

One AI call (two-pass) plans the Spec's top level: the zones (wards, farmland, a dungeon's
levels), the networks (roads, river, walls) and the ground, each with its quantity and
quality fields.

- **Input:** the confirmed Brief; GBO's root module and the modules it may hold (their
  fields, child rules, site preferences, the design rules that apply); the scale class's
  zone count; verified examples this project kept (WorldCraft-style manual [worldcraft]);
  and **the user's answers** to the confirming questions, in their words.
- **Output:** for each child: id, module, quantity (`plots` or the zone's own count field,
  `area`), quality (its own street pattern, `street-style`), **relations** to other parts,
  a **placement** sentence ("along the river's west bank, north of the market"), and a
  rationale.
- **The prompt's design guidance** (as built): each ward gets its count of parcels and its
  area; each ward its own street pattern, the main one for most, another where the ward
  calls for it (a grid for a merchant ward, radial round a temple); include the networks
  the Brief has; the user's answers decide where things go.

### 4.2 Relations: a closed vocabulary

The layout understands exactly these zone relations (GBO `relations.json`):

| Relation | Meaning | Target |
|---|---|---|
| `near`, `adjacent`, `beside` | close to | a zone, the river, a road |
| `along` | a strip beside, within about 5 cells | usually the river or a road |
| `far`, `away-from` | far from | a zone |
| `north-of`, `south-of`, `east-of`, `west-of` | on that side of, and close by | a zone, the river (per row: a bank), a road |
| `in-north`, `in-south`, `in-east`, `in-west` | in that part of the map itself | `site` |

**Normalisation** (`NormalizeRelations`): what the AI writes is mapped onto these:
- a compound direction is two relations ("north-west-of X" → north-of X and west-of X);
- map words ("below", "above", "left", "right") → south-of, north-of, west-of, east-of;
- a direction to the map ("in the north-west", to "site", "map" or nothing) → in-north and
  in-west;
- anything still unknown is dropped.

**Decision:** a closed, small vocabulary with a score function each (§6) instead of free
spatial language. The cost: the AI must translate "the east bank" into `east-of river`, and
it doesn't always (§10).

### 4.3 Without the AI, or when it fails

- **Fallback zones** (`FallbackRootAnswer`): each zone the Brief turns on; a zone of more
  than about 90 plots is split into several (within the scale class's zone count), the
  buildings shared by the zones' weights (residential 3, craftsmen 1.5, others 1, nature 0);
  each zone's land by its plots (about 110 cells each), not a share of the whole site.
- **Repairs:** a node that breaks its module's rules goes back once (`/places/repair-node`);
  then the fixes that need no AI (`TidySpec`, `ResolveSpec`): clamped numbers, required
  children added, a zone without parcels given its module's typical ones, what the Brief
  turned on placed in the zone that most typically holds it.
- **Never stuck:** what can't be fixed is kept, said so, and the plan stays approvable.

---

## 5. Compiling the zones (PLL)

The zones become one `partition` statement over the site (`Compile.cpp`, `Root`):

| Field | From |
|---|---|
| `area` | the zone's planned area, or what its parcels need if more |
| `min` | what its parcels need: their lots (each deepened for the gardens and yards it gets) × **1.9** (streets and open land) |
| `flexible` | a zone without parcels (farmland, a green): takes what the others leave |
| `prefers` | the module's site preferences (centre, edge, water, main road, flat, high ground...) |
| `rel` | its relations (normalised), with weights |
| `seed` | where it was before (a revision keeps the zones it isn't about) |
| `box` / `paint` | drawn or painted by the user: exactly those cells |

Before the partition: the site region, the ground fill, two entrances, the river (a path
from a source on the north edge to a mouth on the south edge, meandering by noise), the main
road(s) between the entrances through the centre (a crossroads from a town up). A castle's
enclosure (moat, curtain wall, towers, gatehouse) puts the partition inside its inner ward.

**Site size.** The Brief's area gives a side; it grows when the zones need more:
`side = max(brief side, √(Σ zone need / 0.6))`. **Decision:** 40% of the site stays open
land between zones; at the layout step (zones alone) each zone's area is at least its need
(§7.2), and the side is taken from the zones' total area the same way.

---

## 6. Solving the partition

The partition's solver (`Solver::Partition`) is a seeded growth in the manner of
Emilien et al.'s village generator [emilien2012]: each cell has an **interest** for each
zone, seeds are placed by interest, and zones grow from them.

### 6.1 Quotas

Each zone's quota is its area. If the zones want more than 92% of the free land
(not road, water or building), quotas are scaled: zones without buildings give way first
(they keep 10% of the room between them); the built zones share the rest.

### 6.2 Interest

For a zone *z* and a cell *c* at (x, y), with `reach = max(4, half × 0.35)` (*half*: half
the site's diagonal), *d* the distance to the target (another zone's seed, or a path's
distance field), and per relation weight *w*:

| Relation | Score added |
|---|---|
| along (or adjacent to a path) | `2w · (1 − min(2, d/5))` |
| near, adjacent, beside | `2w · (1 − min(2, d/reach))` |
| far, away-from | `2w · (2·min(1, d/half) − 1)` |
| north/south/east/west-of | side `s = clamp(±(Δ)/reach, −1, 1)` (for a path, Δ from the path's position at this row or column: a bank); `v = 0.7s + 0.3(1 − min(2, d/2reach))`; score `w(3v − 3·[s < 0])`: on the wrong side is a miss |
| in-north/south/east/west | `3w · clamp(1.5 · f, −1, 1)`, *f* the cell's position from the site's middle toward that side (−1…1) |

Then the module's site preferences, each in [−1, 1] times its weight: centre, edge,
main road (distance to a road of rank ≥ 3), water (distance to water), and so on.

### 6.3 Seeds

- **Order:** zones with a kept seed first (a revision keeps the zones it isn't about); then
  zones whose relations don't name another zone; then by the strength of their
  preferences. A zone related to another is seeded after it.
- **Placement:** the cell of best interest (sampled, about 3,000 candidates), penalised when
  closer than `0.8·√(room/n)` to another zone's seed (by up to 4 points). A kept seed moves
  to the nearest free cell if its own is gone.
- **Drawn and painted zones** take their cells first and don't grow; a seed that fell in
  them moves to the nearest free cell.

Land that is already a zone's (drawn, painted, or kept from the approved layout, §9) is taken
before any seed is placed. A seed is chosen only on the free land left, and only in a connected
stretch of it (joined across roads, as growth is) of at least 70% of the zone's quota, or 90% of
the widest stretch if none is that big: a seed in a pocket closed off by kept zones, water and
roads would leave its zone a sliver.

### 6.4 Growth

Repeatedly, the zone furthest below its quota (by filled fraction) takes the best cell on
its frontier. A frontier cell's priority is
`−k_d · distance to the seed + k_i · interest`, with `(k_d, k_i) = (1, 0.5)` for a zone
without relations (compact) and `(0.45, 1.5)` for one with relations (it follows them: along
the river becomes a strip). A zone reaches across a road (up to 4 cells): roads run
through wards; the road cells stay nobody's. Growth stops when every zone has its quota or
no frontier is left.

**Decision:** growth by interest, not a treemap or a Voronoi split. Zones take irregular,
organic shapes that follow rivers and roads, and relations act directly on the cells. The
cost: nothing in the growth guarantees a good *arrangement* as a whole (§10).

---

## 7. The layout step

### 7.1 What is shown

The zones alone are solved and drawn (no parcels). Beside the map, each zone with:
- its description (GBO), its count of buildings and its land;
- **where it was meant to go:** the AI's placement sentence and its relations;
- **where it lies:** a sentence from the solved layout (`places/Where`): its part of the
  map, the river ("along the river, its west bank": the river within 2 cells over its
  rows), the zones it touches and where they lie;
- a **space check:** its land against what its buildings need.

### 7.2 Each zone's need

`need = n · (mean lot area of the zone's parcel kinds) · 1.9`, *n* its count of buildings,
the lots from the parcel modules' default sizes. **Decisions:** the layout gives every zone
at least its need, so the user isn't shown a cramped layout that the plan step would
silently enlarge; a warning appears only for a zone the user painted smaller.

### 7.3 Patterns

The place has a main street pattern (or a mix with shares: organic, grid, radial, linear
along a road or a river); each zone with buildings may take its own (`street-style`),
unset meaning the main one. The AI sets it per zone when it plans; the user changes it.

---

## 8. User control

The layout is the AI's proposal; the user shapes it before anything inside is planned.

### 8.1 Direct editing

- **Contents tab:** every field of the Brief (size, components on/off and how many, their
  fields, style). A change the zones depend on marks the layout *out of date*; planning
  waits for "Lay out the zones again" (the zones re-planned from the Brief and the answers).
- **To decide:** each decision with its assumed answer and a control (the field's own, or
  words that go to the zones' planning).
- **Sizes and patterns:** each zone's land and count, its pattern.
- **Painting** (the drawing tool): a brush (1–16 cells, and erase) paints a zone; it takes
  exactly its painted cells, taken from any other zone; the first stroke adds to its
  current land. Painted zones keep the layout's site size (their cells are its cells) and
  scale with the site in the plan.

### 8.2 Revise in words

The user writes a change ("put the residential ward along the river, east of it, the
others west of it"). The AI receives the request, **where things are now** (the `Where`
description with a coarse text map), the plan, and what each part may hold, and answers
with operations:
- `move` (relations only), `set` (fields), `zone` (re-plan a zone's parcels), `add`, `remove`;
- `brief` changes (the main pattern, the size, parts on or off and how many);
- **`expect`**: every spatial fact the request asks for, one relation each.

Then, in order:
1. **Normalise** the relations and facts (§4.2).
2. **Scope guard:** a change to a zone the request doesn't name is dropped (a small request
   stays small), unless the request is about "the others", "the rest", "all", "every".
   **What moves and what it moves by:** in each clause, a zone named before the first word of
   place ("put the craftsmen's ward | between the temple ward and the docks") is what moves;
   one named after it is a **landmark** and keeps its place (the AI's new relations for it are
   dropped, with a note); "leave / keep / don't move X" pins X.
3. **Direction words** in the request ("below the docks") are added as relations when the
   AI lost them, but only in a clause that names exactly the zone and its target. A zone with
   a `move` keeps the move's relations (a `set` in the same answer that restates its old ones
   is ignored).
4. **The moved zone's place is composed** from its references (§8.3).
5. **Local edits:** taking out a part of a zone edits the layout in place, nothing re-laid.
6. Otherwise the moved zones are placed anew (letting go of any painting, which would hold
   them where they were) and **every other zone keeps its exact cells**; their land is taken
   before any seed is placed, and a moved zone's seed goes on the free land in a stretch big
   enough for it (§6.3). Only if a moved zone then lacks room do the zones the request didn't
   name give way (each from its seed); landmarks and pinned zones never do.
6. **Facts checked** on the new layout (`CheckExpectations`): a direction holds when ≥ 70% of
   the zone's cells are beyond the target (85% for a path, by its position per row or
   column); along, when the path is within 2 cells over ≥ 60% of the zone's length; near,
   when touching or within 0.65·(√a + √b); in-north etc., when the zone's middle is in that
   42% of the map. **Failing facts are pulled harder** (their weights × 2.5) and the layout is
   solved again, twice at most (re-laying only the failing zone); what still fails is reported
   beside the revision.
8. **Undo** restores the saved plan and its seeds.

### 8.3 Composing a moved zone's place

**The problem.** "Put the market ward to the south of the temple ward by the river": the temple
ward lies on the river's east bank, the market ward on the west. The model answered "south of
the temple ward, along the river's west bank": it kept the market's old bank, though it had
just read (in its own description of WHERE) that the temple is on the east bank. A person reads
the request as "below the temple ward", which puts the market on the east bank: being south of
something says where you are along one axis and, across it, leaves you where that thing is.

**The reasoning.** This is composition of qualitative spatial relations: from *A south of B*
and *B east of the river*, infer *A east of the river*. Qualitative spatial reasoning formalises
it with relation calculi and their composition tables: cardinal directions [frank1991,
ligozat1998], topology [randell1992]. A cardinal direction is a pair of independent axis
relations (north-south, east-west), so the composition is per axis: a relation that moves along
one axis says nothing new about the other, and there the reference's relations carry over.
LLMs are known to be weak at exactly this multi-hop spatial inference [mirzaee2021, shi2022];
writing the steps out helps [wei2022], and so does a symbolic form of the space [hu2023]. We do
both, and check the result in code.

**In the prompt** (revise at the layout), the model reasons in its thinking, for each zone the
request moves:
1. the zone that moves, and its references (zones, the river, a road, the map's edge); a zone
   named only as a reference stays;
2. where each reference is *now*, from WHERE: its part of the map, its bank, its side of the
   main road, what it touches;
3. compose: a relation moves the zone along one axis; across, it stays where the reference is
   ("south of the temple ward", the temple on the east bank: the east bank, and the east of the
   map if the temple is there); "near" or "beside" keeps the reference's side of everything; a
   compound ("south-east") inherits only where it points the way the reference already lies;
   the moved zone's own old place doesn't count; what the request says itself wins;
4. check there is free land there, or say which unnamed zones give way;
5. write every relation that follows, asked and composed, in the `move` and in `expect`.

**In code** (`ComposePlace`), after the answer, for each moved zone, from the solved layout:
- **Dividers:** the river and the main roads; each runs north-south (sides east, west) or
  west-east (sides north, south), its line taken per row or column. A zone's side is the one
  holding more than 85% of its cells; otherwise it lies on both.
- **Parts of the map:** a zone's middle in the west or east 42% (north or south) of the site.
- **Each relation to a reference zone is a step** (dx, dy): north-of (0, −1), east-of (+1, 0),
  a compound the sum of its two; near, adjacent and along are "beside": no step, the same side
  of every divider, the part of the map not implied.
- **Carry over:** on an axis where the step is 0, or points the way the reference already lies
  (east of an east-bank zone), the moved zone takes the reference's side of every divider
  crossing that axis and its part of the map on that axis.
- **What the request says wins:** a side it names for that divider ("east of the river", "on
  the west bank", "across the river", "the other side") or a part ("in the north") is not
  overridden. References that disagree on an axis (between a west-bank and an east-bank zone)
  leave it open, and the note says so.
- The composed relations are **added** to the zone's relations and to the facts checked
  (`expect`); relations that contradict them (the model's guess of the old bank) are
  **dropped**; each is explained in the revision's notes ("Market ward: east of the River, as
  the Temple ward it's placed by is").

For the example: south-of temple-ward is a step (0, +1); the temple's east bank is on the
east-west axis, where the step is 0, so `east-of river` is added and the model's `west-of river`
dropped; the temple's middle is in the east of the map, so `in-east site` too; its side of the
main road (north) is on the axis the step crosses, so nothing is implied there.

References: [frank1991] A. U. Frank, "Qualitative spatial reasoning about cardinal
directions," Proc. Auto-Carto 10, 1991. [ligozat1998] G. Ligozat, "Reasoning about cardinal
directions," J. Visual Languages and Computing 9(1), 1998. [randell1992] D. A. Randell, Z. Cui,
A. G. Cohn, "A spatial logic based on regions and connection," Proc. KR, 1992. [mirzaee2021]
R. Mirzaee et al., "SpartQA: A textual question answering benchmark for spatial reasoning,"
Proc. NAACL, 2021. [shi2022] Z. Shi, Q. Zhang, A. Lipani, "StepGame: A new benchmark for robust
multi-hop spatial reasoning in texts," Proc. AAAI, 2022. [wei2022] J. Wei et al.,
"Chain-of-thought prompting elicits reasoning in large language models," NeurIPS, 2022.
[hu2023] H. Hu et al., "Chain-of-symbol prompting elicits planning in large language models,"
2023 (arXiv:2305.10276).

---

## 9. From the layout to the plan: the layout is frozen

The layout the user approves in ① Confirm is the plan's, cell for cell. Earlier versions kept
only the zones' seeds and solved again on the plan's (bigger) site, with fitting rounds that
grew short zones: the plan's zones came out different from the layout's, and going back to
the Layout tab solved them again and changed it once more. Now:

- **The layout is data.** After every layout solve, each zone's cells are stored on its spec
  node (`layoutCells`, cell runs), with the site's side and the seed of the networks
  (`root.layoutSide`, `root.layoutSeed`).
- **The plan fills the zones.** The compiler gives a zone its painted cells, else its kept
  cells, as exact land (§6: it takes them first and doesn't grow). The plan solves on the
  layout's side and seed, so the river and roads are where they were; there is one round,
  no fitting growth, no scaling.
- **Only the Layout tab changes the layout**, and only by what the user does there:
  painting or drawing a zone (it takes its cells; the others lose those cells, nothing else
  moves), a zone's size (laid out again, each zone from its seed), Revise at the layout (the
  zones it moves from scratch, the rest from their seeds), "Let it be placed", and "Lay out
  the zones again". Going back to the Layout tab from the plan shows the same layout.
- **The plan's Revise and the AI's review can't move zones.** The AI is told the zones are
  fixed; what it still answers about zones (a move, a zone added or removed, a zone's area
  or relations, a Brief change that turns a zone on or off or changes the size class, a
  spatial fact about a zone) is dropped, with the note "Where the zones lie is set in
  ① Confirm → Layout". What's inside the zones (parcels, counts, styles) it changes as before.
- **A zone asked for more than it holds.** The plan's Revise is given each zone's land and
  about how many parcels it holds (its cells × planned ÷ need, §7.2). The AI is asked to fit
  more in first (smaller parcels or building kinds, a denser street pattern, less open land)
  and to ask for no more than the zone then holds. After the solve, the solver's count decides:
  a zone that placed fewer than planned keeps what it placed (each template's count set to
  what was placed; a template none of which fitted is dropped), and the plan and the revision
  say so: "The craftsmen's ward has room for 18 of the 25 buildings planned; it has 18. To fit
  them all, make it bigger in ① Confirm → Layout." The user changes the layout there.
- Inside each zone, streets are spaced by the depth most lots have (80th percentile), not
  the deepest.

---

## 10. Case study: a bad layout

Request: "build a village by river with several parts (marketplace, accommodations etc)"
(Test1, Mistral Large, 2026-10-20). The layout had: two "Residential ward" labels and two
craftsmen's wards; a temple ward inside a thin ring of market ward; every zone on the east
bank, the west bank empty but for a round commons; about 60% of the site bare.

What the zone plan contained:

| Child | Module | Relations | Placement sentence |
|---|---|---|---|
| core-ward | residential-ward | near market-ward | "encompasses the market, residential, and temple wards" |
| market-ward | market-ward | center core-ward; along main-road | "at the centre of the settlement" |
| temple-ward | temple-ward | near market-ward | "near the market ward, within the core" |
| industrial-ward | craftsmen-ward | along river; edge core-ward | "along the river's east bank, downstream" |
| docks | docks | along river | "along the river's east bank, north of the craftsmen" |
| craftsmen-ward | craftsmen-ward | along river | "along the river's east bank, south of the docks" |
| commons | commons | edge core-ward | "on the western edge" |

Causes, from the most to the least important:

1. **The AI's model of a zone was wrong.** It read "a village has 1–3 zones" from the
   scale table, decided six zones were too many and grouped them: a `core-ward` that
   "encompasses" others. Zones don't nest: the residential "core" became one more zone,
   and the market and temple were squeezed against it (the ring). It added an
   `industrial-ward` of the same module as the craftsmen ward it also kept.
2. **Placement in words, not relations.** Every placement sentence named a bank; no
   relation did (`east-of river` is never written). `center`, `edge` and `crosses` aren't
   zone relations and were dropped. Nothing pulled any zone west, so the seeds clustered
   by the remaining interest (water, the main road) on one bank.
3. **The AI never sees the map.** It plans in text and relations without a picture of
   where the river runs or how big the site is, and it can't check its arrangement.
4. **Open land.** 9,600 cells of zones on a 24,000-cell site (156²): the 40% open land, with
   no flexible zone (farmland, outskirts) to take it, shows as bare green.
5. **Growth has no global criterion.** Each zone grows greedily toward its quota from its
   seed; nothing measures compactness, enclosure or balance across the river.

In contrast, the plan step's parcels are placed well: inside a zone, lots are placed by
fixed rules (streets, frontage, setbacks) that don't depend on the model's spatial judgement.

---

## 11. Making layouts reliably good (implemented)

These answer §10's causes. They apply to every build type whose zones share one site (a
settlement's wards, a castle's baileys and courtyards, a farm's fields, a camp's areas, a
region's biomes, a wilderness's areas); a dungeon's levels and a building's floors are laid out
by their own operations (rooms on a floorplan), so they get no grid.

### 11.1 The AI sketches the layout on a grid

1. Before the zones are planned, the place is compiled from the Brief alone with its zones
   empty, and solved: the networks and features of its build type (a river, roads, walls, a
   moat, the inside of the walls) on the site.
2. That is drawn as a 16×16 text grid, north at the top, with a symbol per kind of cell:
   `~` water (river, moat, coast), `=` a road or path, `#` a wall, `x` outside the walls (no
   inner zone there), `.` open land. Only the symbols present are explained. The prompt
   gives the cell size (each grid cell is about k×k tiles) so the AI can size zones by area.
   A type whose compile has no zone partition gets no grid.
3. The zone-planning prompt includes the grid and asks, with the zones, for a **sketch**:
   the grid with a letter per zone (a legend of zone ids), each zone one contiguous patch
   about its area, on open land or over roads, never on water or outside; and relations that
   agree with it.
4. The sketch is stored on the root (legend ids mapped to the final ids). At the layout, each
   zone's sketched grid cells become its **sketch cells** at full resolution, and the
   partition uses them as a strong guide, not a border: the seed is the sketch's middle; the
   interest is +4 in the zone's own sketched cells, −2 in another zone's, −0.5 elsewhere; and
   a sketched zone grows by interest as much as by distance. Zone sizes still come from
   their need (§7.2): a sketch that draws a zone too small doesn't starve it.
5. A zone that a revision moves loses its sketch (it would pull it back); painting, drawing
   or a kept layout override it.

Why soft and not exact: the AI's areas are often off, and an exact sketch would carry that
into the layout; as a guide, the arrangement is the AI's and the sizes are the rules'.

### 11.2 Prompting

The zone-planning prompt now says, for every build type:
- design the layout first, in the thinking, as a designer of this kind of place would: its
  heart, its edges, the water, the routes in, the walls, what each zone needs beside it (a
  dock on the water, fields outside the walls, a camp's fire in its middle, a keep in the
  inner ward), what is kept apart;
- zones are areas side by side, each contiguous, none inside another (no zone that
  "encompasses" others);
- one zone per component unless the Brief's count says more; the scale's usual zone count
  is a guide, not a reason to group or split;
- placements are said in relations of the allowed types (listed), compound directions as
  two relations, and every direction of the placement sentence is also a relation.

And in code, **no duplicate zones**: a zone of a module beyond the Brief's count for it (one
when not given) is dropped, and the relations that named it go to the zone kept (the
`industrial-ward` beside the `craftsmen-ward` of §10). Zones tied to a root field (a
dungeon's levels, a building's floors) are exempt.

### 11.3 Quality checks, best of four, and "Fix with the AI"

Measured on the solved zones, no AI involved (`LayoutQuality`):

| Check | Measure | Weight |
|---|---|---|
| one piece | the zone's pieces (cells up to 5 apart across a road are joined); pieces under 8% ignored | 3 |
| compact | area / bounding box ≥ 0.4 | 1 |
| not inside another | no more than 70% of its edge on one other zone | 3 |
| room | at least 90% of its need | 2 |
| relations met | each of its relations, checked as in §8.2 | 2 each |
| unique | no more zones of a module than the Brief asks | 2 |
| coverage | the zones cover at least 45% of the site | 1 |

A fresh layout (nothing kept, painted or seeded) is solved with four seeds; the lowest score
wins (an early 0 stops), and its seed is kept for the networks (§9). What still fails is
shown in the Layout tab under "The layout could be better", with **Fix with the AI**: the
failing checks sent as a revision at the layout.

### 11.4 Less bare land

The layout's site is sized for the zones to cover three quarters of it (total need / 0.75,
was / 0.6), when the Brief's own size isn't larger.

## 12. Decisions

| Decision | Alternatives considered | Why |
|---|---|---|
| Three readings of the request, merged by agreement | one reading; ask the user everything | disagreement shows what's open [kobalczyk2025]; one reading guesses silently [clareval] |
| Think in free text, then answer in a schema (two-pass) | one JSON call with a thinking field | strict formats degrade reasoning [tam2024] |
| A ceiling on each call (≈3k thinking, 6k answer, 180 s) | unlimited | a runaway call held steps for minutes |
| Only the named build type in the reading's context | every build type | 36 KB → 8 KB for a village; much faster, same answers |
| A count unit per build type | "buildings" everywhere | a dungeon is counted in rooms, a region in sites |
| The layout before the plan (① Confirm) | plan everything, then revise | the arrangement is the first thing to agree; fixing it after planning re-plans everything |
| A closed vocabulary of zone relations with score functions | free spatial language solved by the AI | checkable, solvable, explainable; normalisation absorbs near-misses |
| Growth by interest from seeds [emilien2012] | treemap, Voronoi, the AI's coordinates | organic shapes that follow rivers and roads; relations act on cells |
| Each zone's land at least its need (lots × 1.9) | the AI's area | the layout the user approves is the one the plan keeps |
| Painted cells are exact | painted cells as hints | what the user draws is what they get |
| Revision with operations, a scope guard and checked facts | re-plan from the request | small requests stay small; facts are verified, not trusted |
| Direction words only from clauses naming two parts | the whole request | applied to every zone, one request moved every zone to one bank |
| Fallbacks without AI at every step | fail when the AI does | the build is never stuck |
| The AI sketches on a grid of the type's networks, used as a soft guide | relations only; the sketch as exact cells | the AI sees and checks its own arrangement; sizes stay the rules' (§11.1) |
| The approved layout is frozen as cells; the plan fills it | keep seeds and re-solve | the plan's zones were different from the approved layout (§9) |
| Several seeds, scored by quality checks | one seed | a bad growth is usually a bad seed; checks are cheap (§11.3) |
| A moved zone's place composed per axis from its references, in the prompt and checked in code | trust the model's relations | models are weak at multi-hop spatial inference; composition is cheap to compute exactly (§8.3) |
| In a revision, the zones not moved keep their exact cells; landmarks never move | keep their seeds and re-grow | re-grown zones drifted; a landmark moved by the request about another zone (§8.2) |

---

## 13. Open questions and evaluation

- **How much to trust the sketch:** we chose strong seeds and interest (§11.1); to evaluate:
  how often the sketch's arrangement survives (each zone's overlap with its sketch).
- **Grid resolution:** 16×16 is readable to a model and coarse enough to be quick to
  write; finer grids may help large places (a city's dozens of wards).
- **Measures for the paper:** relation satisfaction rate, the quality checks of §11.3,
  revisions needed until approval, and human ranking of layouts; ablations of the sketch,
  the three readings, the relation normalisation, the quality-driven best-of-N.
- **Models:** the case study used Mistral Large; Claude was more faithful in revisions
  (`fixes.md`). The pipeline is provider-independent (`place-interaction.md` B3), so the
  comparison is part of the evaluation.
