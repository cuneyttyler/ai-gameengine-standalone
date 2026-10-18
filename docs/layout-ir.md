# PLL — the Place Layout Language

Status: **draft specification, v1 (2026-10-18)**. Schema id `aige.ir/1`. *PLL* is a
working name.

PLL is the intermediate language between the **Spec** (the plan tree: *what and how much*)
and the **scene** (cells and entities). A PLL document says *where things go and by which
rules*. It is produced by compiling the Spec (`docs/place-schema.md` §1, step 4), read by
the solvers (step 5), and kept with the place for verification and editing. Its terms come
from the place ontology (`docs/place-ontology.md`) and stay aligned with GLDL (§12).

---

## 1. Design goals

1. **Declarative.** Statements say what must hold, not how to compute it. Solvers decide
   the order and method.
2. **Checkable before solving.** Every reference resolves, every value has a type and a
   unit, and obvious infeasibility (children too big for their parent) is caught
   statically.
3. **Deterministic.** The same document and seed always give the same result.
4. **Traceable.** Every statement names the Spec node it came from. Every solved value
   and every built entity can be traced back.
5. **Hierarchical and local.** Regions nest. A subtree can be re-solved alone while
   everything outside it is held fixed.
6. **2D-grid native.** Cells, cell edges, sides, layers, perspective-aware facing, linear
   pieces, terrain.
7. **Small.** Fifteen statement kinds and one relation vocabulary. Anything else is
   added through the ontology (§11).

## 2. Space and units

| Concept | Definition |
|---|---|
| **cell** | integer grid position `[x, y]`. *x* grows east, *y* grows south. Directions are **grid directions** (`n e s w`), not screen directions; on an isometric grid, *north* is still −y. |
| **point** | a world position `[x, y]` in cells (a cell's centre is `x + 0.5, y + 0.5`). |
| **edge** | the boundary between two adjacent cells: `[x, y, "n" \| "w"]` (the cell's north or west side). Fences and walls lie on edges. |
| **size** | `{"w": W, "h": H}` in cells. Either may be a range `[min, max]`. |
| **area** | cells². |
| **footprint** | the cells an object occupies (from the catalog: the opaque base of its art), `w × h`, rotated with its facing. |
| **facing** | `n e s w`: the direction the object's front (door, face) points. |
| **layer** | `ground`, `decal`, `prop`, `building`, `overhead`. Footprints may overlap only across layers. |
| **distance** | `dist(a, b)`: the smallest Chebyshev distance between any cell of `a` and any cell of `b`. It is 0 when they touch or overlap. |

All lengths and distances are in **cells**. All values carry their unit implicitly by
field type (§4).

## 3. Document

```json
{
  "schema": "aige.ir/1",
  "place": "riverford",
  "spec": "spec.json",
  "seed": 7,
  "grid": {"style": "topdown", "bounds": [0, 0, 160, 160]},
  "statements": [ ... ],
  "objectives": [ ... ]
}
```

- `statements` is a flat list. **Order does not matter**; solving order is fixed by
  phases (§7).
- Every statement has `op`, an `id` (unique in the document; generated as
  `<node>/<local>` when omitted), and `node`: the Spec node id it came from.
- The canonical form is JSON. The **text form** (§10) is for reading and review.

## 4. Values and references

### 4.1 Value types

| Type | JSON | Notes |
|---|---|---|
| `int`, `number` | `3`, `0.3` | |
| `range` | `[2, 5]` | inclusive; the solver chooses within it and records the choice |
| `size` | `{"w": 4, "h": [2, 3]}` | |
| `cell` / `point` | `[12, 40]` / `[12.5, 40.5]` | |
| `side` | `"n" \| "e" \| "s" \| "w"` | |
| `corner` | `"ne" \| "nw" \| "se" \| "sw"` | |
| `ref` | `"g14"`, `"g14.edge"` | a reference (§4.2) |
| `refList` | `["a", "b"]` or `"plots[*]"` | |
| `slot` | `"fence"` | a slot name of the node's module; resolved to assets at solve time |
| `mix` | `{"rose": 0.5, "tulip": 0.5}` | ontology concepts with weights summing to 1 |
| `terrain` | `"dirt"` | a terrain concept from the ontology / tile analysis |

### 4.2 References and selectors

A reference names a statement's **solved result**, optionally narrowed by selectors:

| Selector | Applies to | Gives |
|---|---|---|
| `x` | any | the whole result: a region's cells, an object's footprint, a line's edges, a path's cells |
| `x.cells` | region, place, path | its cells |
| `x.edge` | region, place | its boundary edges (clockwise from the north-west corner) |
| `x.inner` | region | its cells minus a 1-cell border |
| `x.border` | region | its 1-cell border ring |
| `x.side(s)` | region, place | the edges or cells on side `s` |
| `x.side(toward: y)` | region, place | the side facing `y` |
| `x.center` | any | its centre point |
| `x.corner(c)` | region, place | a corner cell |
| `x.front` / `x.back` / `x.left` / `x.right` | place (with facing) | the cells just outside that side of its footprint |
| `x.door`, `x.anchor:name` | place | named anchors of the art (doors come from the catalog) |
| `x[i]`, `x[*]` | place with `count`, scatter, subdivide | one instance, or all instances |
| `parent` | inside a statement | the `in` region of this statement |

**Region expressions** combine cell sets: `a | b` (union), `a & b` (intersection), `a - b`
(difference), for example `"g14.inner - g14.beds"`.

References may point only to statements solved in an **earlier or the same phase** (§7).
A reference to a later phase is a static error.

## 5. Statements

Every statement also accepts `node` (required), `id`, `tags` (free words, never logic) and
`essential` (default `true` for `place`, `line` and `path`; `false` for `scatter` and
`rows`). The repair step may move or drop non-essential statements' results (§9).

### 5.1 Givens

| op | Fields | Meaning |
|---|---|---|
| **`fixed`** | `cells` or `entity` or `ref` (a solved result from a previous solve), `layer` | Something that exists and must not change: scene content outside the place, locked or hand-edited parts. It occupies its cells for every hard rule. GLDL's *PredefinedFacility*. |
| **`anchor`** | `at`: a cell, point or selector (`"p14.door"`, `"site.side(s)"`); `kind`: `entrance` · `gate` · `door` · `spawn` · `marker` | A named point that others refer to. Entrances, gates and doors are reachability targets (§8). GLDL's *Points*. |

### 5.2 Areas

| op | Fields | Meaning |
|---|---|---|
| **`region`** | `in` (parent ref; omitted only for the root), `shape`: `rect` · `poly` · `cells` · `any`, `size` and/or `area`, `ratio`, `cells` (for `cells`), `layer` (default `ground`) | An area the solver must find inside its parent. Regions are the hierarchy: plots, gardens, rooms. Sibling regions never overlap. |
| **`partition`** | `in`, `into`: a list of `{id, area or share, prefers: [criterion: weight]}`, `method`: `voronoi` · `growth`, `interest`: interest map names | Splits a region into zones. Each zone's `prefers` uses interest criteria from the ontology (`center`, `high-ground`, `water`, `road`, `away-from:<ref>`), each in [−1, 1]; −1 vetoes a cell (Emilien et al.). Each zone becomes a region named by its `id`. |
| **`subdivide`** | `in`, `style`: `skeleton` · `split` · `mixed`, `area`, `width`, `irregularity` (0–1), `access`: `street` · `any`, `count` (optional) | Splits a block into parcels (Vanegas et al.). With `access: street`, every parcel touches a path. Instances: `id[i]`. |

### 5.3 Networks

| op | Fields | Meaning |
|---|---|---|
| **`path`** | `from`, `to`, `via` (refs), `width`, `class`: `main` · `street` · `lane` · `trail` · `river`, `terrain`, `reuse` (cost factor for existing paths, default 0.1), `avoid` (refs), `cost` (criterion weights) | A least-cost route over the grid between anchors. Existing paths are almost free to reuse, so networks join instead of running in parallel. |
| **`network`** | `in`, `style`: `grid` · `radial` · `organic` · `growth`, `spacing`, `width`, `class`, `connect` (anchor refs), `loops` (0–1) | A street network filling a region. `growth` grows streets toward seeded buildings. Every result counts as a `path`. |

### 5.4 Objects

| op | Fields | Meaning |
|---|---|---|
| **`place`** | `in`, `slot` (or `asset`), `count` (int or range; default 1), `footprint` (override; default from the catalog), `facing` (side, `any`, or `toward:<ref>`), `layer` (default from the asset), `size` (sprite cells, default from the slot), `keepFree` (selectors such as `"front"`: cells kept walkable) | One or more objects placed by the solver. Assets are chosen per instance from the slot by catalog retrieval (role, perspective, footprint, variety). GLDL's *NewFacility*. |
| **`line`** | `slot` (a piece set), `along` (region `.edge`, path, or an edge list), `gaps` (anchor refs; each leaves a gate or opening), `closed` (bool) | A linear feature on cell **edges**: fence, wall, hedge. Pieces (straight, corner, end, T, cross, gate) are chosen per edge from the piece set, like autotiling. |
| **`rows`** | `in`, `slot` or `mix`, `count` or `spacing`, `orientation`: `long-axis` · `short-axis` · side, `margin` | Parallel rows: beds, crops, pews, stalls. |
| **`grid`** | `in`, `slot` or `mix`, `spacing` (x, y), `jitter` (0–1) | A regular grid: orchards, graves. |
| **`scatter`** | `in` (region expression), `slot` or `mix`, `density` (objects per free cell, 0–1), `spacing` (`min`, `max`), `mask` (refs or criteria), `cluster` (0 = even … 1 = clumped), `falloff` (`center` · `edge` · ref, with strength) | Poisson-disk scatter with a density map: plants, rocks, clutter. Spec density words have already become numbers. |

### 5.5 Ground

| op | Fields | Meaning |
|---|---|---|
| **`fill`** | `in` (region expression or cells), `terrain`, `background` | Paints terrain with fitted (autotiled) tiles. Paths and regions with a surface emit `fill`s at compile time. |

### 5.6 Relations, objectives and checks

| op | Fields | Meaning |
|---|---|---|
| **`relation`** | `type` (§6), `members` (refs, n-ary; the type fixes the arity), `args`, `hard` (bool) or `weight` (> 0) | A constraint between solved results. A hard relation must hold; a soft one adds `weight × (1 − score)` to the objective. |
| **`objective`** | `in` (region), `cluster`, `sparsity`, `variety`, `coverage` (weights) | Layout preferences for a region (§8.2). |
| **`check`** | `predicate` (§8.3), `args`, `tolerance` | A condition verified **after** solving. Failures go to repair with this statement's `node`. |

## 6. Relations

Each relation has a **score** `s ∈ [0, 1]` computed from solved results. Notation: `a`,
`b` are members; `d = dist(a, b)`; for objects with facing, `(f, l)` are the forward and
lateral offsets of `a.center` in `b`'s facing frame; `wb`, `hb` are `b`'s footprint width
and depth.

| Type | Arity | Args | Score |
|---|---|---|---|
| `inside` | 2 | — | 1 if every cell of a is in b, else 0 (hard by default) |
| `outside` | 2 | — | 1 if no cell of a is in b |
| `center` | 1 (+ parent) | — | 1 − dist(a.center, parent.center) / (half the parent's diagonal) |
| `edge` | 1 | `side?` | 1 if a touches the parent's border (on `side` if given), else decays with distance to it, reaching 0 at 4 cells |
| `corner` | 1 | `corner?` | as `edge`, to the nearest (or given) corner |
| `adjacent` | 2 | — | 1 if a and b share an edge without overlapping, else max(0, 1 − d/2) |
| `near` | 2 | `d0, d1` | 1 if d ≤ d0, 0 if d ≥ d1, linear between (as SceneCraft) |
| `far` / `away-from` | 2 | `d` | min(1, d / d_arg) |
| `in-front-of` | 2 | `gap?` | f > 0 and \|l\| ≤ wb/2: 1 − clamp((f − hb/2 − gap) / 4); otherwise 0 |
| `behind` | 2 | `gap?` | as `in-front-of` with −f |
| `beside` | 2 | — | \|l\| > wb/2 and \|f\| ≤ hb/2: 1 − clamp((\|l\| − wb/2) / 4); otherwise 0 |
| `between` | 3 | — | 1 − (distance of a.center from the segment b–c) / 3 |
| `faces` | 2 | — | 1 if a's facing points toward b's nearest cell, 0.5 if perpendicular, 0 if away |
| `aligned` | n | `axis: x · y` | 1 − (spread of centres on the other axis) / n |
| `in-row` | n | `axis, spacing?` | `aligned`, and gaps within ±1 of `spacing` (or of their mean) |
| `symmetric` | n | `axis` of the parent | the fraction of members with a mirrored partner (same slot) within 1 cell |
| `repeat` | n | `spacing` | 1 − mean deviation of neighbour gaps from `spacing` / spacing |
| `along` | 2 | `offset` | 1 − \|dist(a, b) − offset\| / 3, where b is a path or line |
| `encloses` | 2 | — | 1 if line a forms a closed loop around region b (gaps allowed only at gates) |
| `crosses` | 2 | — | 1 if path a passes through b (river crossing → a bridge is placed) |
| `path-between` | 2 | `layer?` | 1 if a walkable route exists between a and b (hard by default; see §8.1) |
| `visible-from` | 2 | `radius` | 1 if a straight line from anchor b to a crosses no blocking cell |
| `focus` | 2 | — | 1 if a lies at the end of a straight path run that leaves anchor b (a landmark closing a view) |
| `on` | 2 | — | a's footprint lies within b's top surface and a is on a higher layer (a pot on a table) |
| `interest` | 1 | `criterion, weight` | the interest map value at a's cells, mapped from [−1, 1] to [0, 1]; −1 is a veto (hard) |

**Hard by default:** `inside`, `outside`, `encloses`, `path-between`, `on`, and `interest`
vetoes. Any relation can be made hard with `"hard": true`. A hard relation that can't be
met makes the solve **unsatisfied** (§9).

New relation types are registered in the ontology with their arity, arguments and score
function (§11).

## 7. Solving order (phases)

The solver runs these phases in order. Within a phase, statements are taken by depth
(outer first), then by footprint size (large first), then by id, so the order is
deterministic.

| Phase | Statements | Notes |
|---|---|---|
| 0 | `fixed`, `anchor` (absolute) | givens occupy their cells |
| 1 | `region` (root), `partition` | zones; interest maps evaluated |
| 2 | `path`, `network` | main roads first (`main` → `street` → `lane` → `trail`); rivers before roads |
| 3 | `subdivide` | parcels with street access |
| 4 | `region` (nested), `anchor` (relative) | plots' gardens, rooms; anchors on solved regions |
| 5 | `place` | anchor objects first (most relations), large before small; greedy placement, then simulated annealing over the soft objective |
| 6 | `line` | fences and walls on solved edges; gates at gap anchors |
| 7 | `rows`, `grid` | |
| 8 | `scatter` | free cells only; densities applied to free area |
| 9 | `fill` | terrain painted and fitted last, around everything |
| 10 | `check` | verification (§8.3) |

A phase's results are **frozen** when the next phase starts. Repair re-opens only the
failing statement's subtree (§9).

**Determinism.** Each statement's random stream is seeded with `hash(document seed,
statement id)`, so adding or removing one statement doesn't change the others' results.

## 8. Rules, objective and checks

### 8.1 Hard rules (always, for every statement)

1. Results lie inside their `in` region, and the root lies inside `grid.bounds`.
2. No two footprints overlap on the same layer. `fixed` cells count.
3. Objects stand only on terrain their asset allows (from the catalog).
4. `keepFree` cells stay walkable.
5. Every `anchor` of kind `entrance`, `gate` or `door` is reachable from the place's main
   entrance on walkable cells.
6. `line`s are continuous along their edges, with valid joins; openings only at `gaps`.
7. Every asset chosen is perspective-compatible with `grid.style`.

### 8.2 Objective (soft), per region

```
cost = Σ_soft weight_r · (1 − score_r)
     + w_cluster  · C      C = Σ_{i<j} 1 / (dist(i, j) + 1)          (crowding; GLDL)
     + w_sparsity · S      S = max over free cells of the distance to the nearest object
                                                                       (empty voids; GLDL)
     + w_variety  · V      V = share of neighbouring instances with the same asset
     + w_coverage · K      K = |footprint density − target| for the region's density band
```

Default weights come from the module (via the ontology); `objective` statements override
them per region.

### 8.3 Check predicates

| Predicate | Args | Holds when |
|---|---|---|
| `count` | `ref`, `range` | the number of solved instances is within the range |
| `density` | `ref` (scatter), `band` | the achieved density is within the band |
| `area` | `ref`, `range` | the region's area is within the range |
| `footprint-density` | `ref` (region), `band` | built cells / region cells is within the band |
| `reachable` | `from`, `to` | §8.1 rule 5, for specific anchors |
| `closed` | `ref` (line) | the line closes, except at its gaps |
| `score` | `ref` (relation), `min` | the relation's score is ≥ min |
| `filled` | `ref` (place) | every essential instance got an asset |

`tolerance` relaxes a predicate within the place's tolerance document
(`aige.tolerance/1`).

## 9. Results, errors and repair

**Solving writes `layout.solved.json`:** for each statement, its resolved values (cells,
positions, facing, chosen assets per instance, choices made within ranges), each
relation's score, the objective terms, and each check's outcome. The builder turns this
into engine commands. Edits and regenerations read it back, which keeps instances
matched across re-solves.

**Errors carry a code, the statement id and its `node`:**

| Code | When | Typical repair |
|---|---|---|
| `E-SCHEMA` | invalid field or type | compile error (a bug) |
| `E-REF` | unresolved reference, or a reference to a later phase | compile error |
| `E-AREA` | static: the children's minimum areas exceed the parent's area | re-plan the node (fewer or smaller children) |
| `E-CONFLICT` | static: contradictory hard relations (`inside a` and `outside a`) | re-plan |
| `S-UNSAT` | a hard rule or hard relation can't be met | re-solve with a new seed, relax adaptable items, then re-plan |
| `S-TIMEOUT` | the solver's budget ran out | keep the best solution; report |
| `C-FAIL` | a `check` failed after solving | move or drop non-essential results, re-solve the subtree, or re-plan with the predicate as the reason |

Repair order (Database-driven framework, two-phase repair):
1. **adjust**: move adaptable results locally;
2. **remove**: drop adaptable results that block a hard rule;
3. **re-solve** the subtree with a new seed;
4. **re-plan** the Spec node, passing the failed rule.

## 10. Text form

A readable rendering used in review panels and logs. It maps one-to-one to JSON.

```
stmt     := op id? fields ("@" node)?
fields   := (key value)*            value := number | range | size | ref | list | string
range    := "[" number "," number "]"     size := number "x" number
ref      := name ("." selector)*          selector := name ("(" args ")")? | "[" (int | "*") "]"
list     := "[" (value ("," value)*)? "]"
relation := "relation" type "(" ref ("," ref)* ")" ("hard" | "weight" number) args?
```

Example: one garden, compiled from `w-old/plots#14/garden`.

```
region   g14 in p14 shape rect size 4x3                                @w-old/plots#14/garden
relation behind(g14, p14.house) weight 3
relation adjacent(g14, p14.house.back) weight 2
anchor   g14.gate at g14.side(toward: p14.house.back) kind gate
line     g14.fence slot fence along g14.edge gaps [g14.gate] essential false
rows     g14.beds in g14.inner slot bed count 2 orientation long-axis
scatter  g14.flowers in g14.inner - g14.beds mix {rose: 0.5, tulip: 0.3, daisy: 0.2}
         slot plant density 0.3 spacing [1, 3]
relation path-between(g14.gate, p14.house.back) hard
check    count(g14.beds, [1, 3])
check    closed(g14.fence)
```

The same garden in canonical JSON:

```json
[
  {"op": "region", "id": "g14", "in": "p14", "shape": "rect", "size": {"w": 4, "h": 3}, "node": "w-old/plots#14/garden"},
  {"op": "relation", "type": "behind", "members": ["g14", "p14.house"], "weight": 3, "node": "w-old/plots#14/garden"},
  {"op": "relation", "type": "adjacent", "members": ["g14", "p14.house.back"], "weight": 2, "node": "w-old/plots#14/garden"},
  {"op": "anchor", "id": "g14.gate", "at": "g14.side(toward: p14.house.back)", "kind": "gate", "node": "w-old/plots#14/garden"},
  {"op": "line", "id": "g14.fence", "slot": "fence", "along": "g14.edge", "gaps": ["g14.gate"], "essential": false, "node": "w-old/plots#14/garden"},
  {"op": "rows", "id": "g14.beds", "in": "g14.inner", "slot": "bed", "count": 2, "orientation": "long-axis", "node": "w-old/plots#14/garden"},
  {"op": "scatter", "id": "g14.flowers", "in": "g14.inner - g14.beds", "slot": "plant",
   "mix": {"rose": 0.5, "tulip": 0.3, "daisy": 0.2}, "density": 0.3, "spacing": {"min": 1, "max": 3}, "node": "w-old/plots#14/garden"},
  {"op": "relation", "type": "path-between", "members": ["g14.gate", "p14.house.back"], "hard": true, "node": "w-old/plots#14/garden"},
  {"op": "check", "predicate": "count", "args": {"ref": "g14.beds", "range": [1, 3]}, "node": "w-old/plots#14/garden"},
  {"op": "check", "predicate": "closed", "args": {"ref": "g14.fence"}, "node": "w-old/plots#14/garden"}
]
```

A larger excerpt: a ward with streets and plots.

```
region    w-old in riverford shape any area [2600, 3200]                       @w-old
relation  inside(w-old, walls-1.inner) hard
relation  adjacent(w-old, market) weight 4
network   w-old.streets in w-old style organic spacing [8, 12] width 1 class street
          connect [market.gate-n, walls-1.gate-w] loops 0.3
subdivide p in w-old - w-old.streets style mixed area [35, 70] width [5, 9]
          irregularity 0.3 access street count 28
place     p[*].house in p[*] slot house facing toward: p[*].street count 1
          keepFree [front]
relation  in-front-of(p[*].street, p[*].house) weight 3
objective in w-old cluster 0.2 sparsity 0.5 coverage 2
check     footprint-density(w-old, [0.35, 0.65])
check     count(p, [26, 30])
```

## 11. Extending the language

- **Statement kinds** and **relation types** are registered in the place ontology (as
  `Generator` and `Relation` classes). Each entry gives its fields with types and
  defaults, its phase, and (for relations) its arity, arguments, score function and
  default hardness. The validator and the solver both read the registry, so adding one
  never changes the parser.
- **Interest criteria** (`high-ground`, `water`, `road`, `away-from`…) are ontology
  entries with a map function.
- **Versioning:** `aige.ir/1` documents stay valid. New fields are optional, and
  removed or renamed fields need a new major version with a converter.

## 12. Correspondence with GLDL

| GLDL (Xu & Verbrugge, FDG 2025) | PLL |
|---|---|
| *PredefinedFacility* | `fixed` |
| *NewFacility* | `place`; areas are `region` |
| *Points* | `anchor` |
| *PlaceAroundPosition*, *PlaceInRange* | `near`, `inside` with a region |
| *PlaceByWall* | `edge` / `along` |
| *Near*, *Far* | `near`, `far` |
| *CanSee*, *Focus* | `visible-from`, `focus` |
| *Alignment*, *Orientation* | `aligned`, `faces` |
| penalty + cluster degree + sparsity | `objective` (§8.2) |
| agent simulation of navigability | `path-between`, `reachable`, and the walk-through agent in Verify |

PLL adds:
- **hierarchy** (`region` in `region`) and **provenance** (`node`);
- **networks**, **subdivision**, **linear features**, **rows, grids and scatter**, and
  **terrain**;
- **n-ary group relations** (`in-row`, `symmetric`, `repeat`);
- **checks** in the document;
- **2D grid semantics** (edges, sides, layers, facing, perspective).

A single interior room without nesting can be exported to GLDL for comparison.
