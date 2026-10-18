# Place schemas — the building procedure, its documents and their constraints

Status: **draft for review (2026-10-18)**. Companion to `docs/ai-building.md` (why) and
`docs/place-modules.md` (the module catalogue). Nothing in Phase 7 is built before these
schemas are agreed.

## 1. The procedure

```
 Query ──► 1 Interpret ──► 2 Confirm ──► 3 Plan ──► 4 Compile ──► 5 Solve & build ──► 6 Verify
 (text)     Brief drafts    Brief (fixed)  Spec tree   Layout IR     scene (cells,       report
                                                                      entities)
                ▲                ▲             ▲            ▲              │                │
                └── questions ───┘             └── repair ──┴──────────────┴────────────────┘
```

| Step | Input → output | Who decides | Validated by |
|---|---|---|---|
| 1 Interpret | query → **Brief drafts** (several) | AI | Brief schema |
| 2 Confirm | drafts → **Brief** (confirmed) | user, in the window built from the schema | Brief schema + constraints |
| 3 Plan | Brief → **Spec**: the tree of parts, each with **quantity** and **quality** fields | AI for the upper levels; module defaults and rules for the lower ones | module schemas + constraints (§4) |
| 4 Compile | Spec → **Layout IR**: regions, anchors, placements, lines, scatters, relations | each module's compile rules; AI adds relations at the upper levels | IR schema + references + feasibility pre-checks |
| 5 Solve & build | IR → geometry → engine bulk commands | seeded solvers and generators | hard constraints |
| 6 Verify | scene + Spec + IR → **report** | checks + vision critique | tolerances (§4.6) |

Every document is JSON, kept in the project (`places/<place>/brief.json`,
`spec.json`, `layout.ir.json`). Each step reads only the previous document, so any step
can be re-run alone, and every built entity can be traced back to the Spec node that made
it.

**Why an intermediate language.** The Spec says *what and how much* ("12 gardens,
flowers common, roses and tulips"). The IR says *where, by which rules* ("region g7
behind plot p3, 4×3 cells; scatter flower-slot density 0.3, spacing ≥ 1; fence along
g7's edge except a 1-cell gate facing p3's back door").
- **Two kinds of decision.** The first is semantic and suits the AI; the second is
  geometric and suits solvers. LLMs are poor at coordinates and good at relations.
- **The IR is checkable before any cell changes:** references resolve, areas fit their
  parents, and relations are satisfiable.
- **It is deterministic** (IR + seed → the same place).
- **Diffable and editable:** a change to one node recompiles one subtree.

The sources show the same split: GLDL in "Constraint Is All You Need", Holodeck's
relations, SceneCraft's relation graph, and CityGenAgent's programs.

## 2. Common field types

Every field in every document is one of these. A field always has a **unit**, a
**range or vocabulary**, a **default**, and these bookkeeping flags:

| Flag | Values | Meaning |
|---|---|---|
| `source` | `user`, `ai`, `default`, `derived` | who set it (shown in the window and Inspector) |
| `confidence` | 0–1 | how sure the interpreter is; low → asked in Confirm |
| `locked` | bool | regeneration may not change it (user edits lock automatically) |

### Quantity types

| Type | JSON | Unit / meaning | Example |
|---|---|---|---|
| `count` | `{"count": 12}` or `{"count": [10, 14]}` | items; a range lets the solver choose | houses: 40–60 |
| `density` | `"common"` | one of `none, rare, some, common, abundant`. **Each field maps the levels to numbers in its module schema** (e.g. flowers per cell 0, 0.05, 0.15, 0.3, 0.5), so the word is never ambiguous | flowers |
| `share` | `{"share": 0.3}` | fraction of the parent's quantity, among siblings | 30% of the houses are rich |
| `area` | `{"area": [400, 600]}` | cells² | ward area |
| `size` | `{"w": [3, 5], "h": [2, 4]}` | cells | garden 3–5 × 2–4 |
| `length` | `{"length": 12}` | cells | wall run |
| `spacing` | `{"min": 1, "max": 3}` | cells between items | trees |
| `width` | `{"width": 2}` | cells | street width |
| `ratio` | `{"ratio": [1, 2]}` | w : h | parcel shape |

### Quality types

| Type | JSON | Meaning | Example |
|---|---|---|---|
| `enum` | `"rich"` | one value of a **closed vocabulary** given by the field | wealth: poor / common / rich |
| `enumSet` | `["rose", "tulip"]` | several values from a vocabulary | flower kinds |
| `mix` | `{"rose": 0.5, "tulip": 0.3, "daisy": 0.2}` | weighted vocabulary values (weights sum to 1) | flower mix |
| `tags` | `["mossy", "old"]` | open words, for **asset retrieval and generation only**, never logic | style hints |
| `description` | `"low stone walls…"` | free text, for **retrieval, generation and the critic only**, never logic | |
| `assetChoice` | `{"source": "pack" \| "generate" \| "auto", "assets": [...], "exclude": [...]}` | where a slot's art comes from | per component |
| `bool` | `true` | | fenced |

**Rule:** decisions that change the layout are made only through quantity, `enum`,
`enumSet`, `mix` or `bool` fields, all of them closed and checkable. Free text never
drives logic.

## 3. Documents

### 3.1 Brief (steps 1–2)

The Brief is the confirmed request. Its schema is **per build type** (the build type's
"brief fields" in `place-modules.md`), with this common core:

```json
{
  "schema": "aige.brief/1",
  "buildType": "settlement",
  "name": "Riverford",
  "scale": {"class": "large-village", "buildings": {"count": [70, 90]}, "area": {"area": [16000, 22000]}},
  "style": {"era": "medieval", "culture": "western", "biome": "temperate", "condition": "lived-in",
            "tags": ["cosy", "rustic"], "palette": "from-pack"},
  "shape": {"layout": "organic", "growth": [{"phase": "walled-core"}, {"phase": "farms-outside"}],
            "description": "grew along the river; old walled core on the north bank"},
  "features": [{"module": "river", "through": "middle", "width": {"width": 4}}],
  "location": {"anchor": {"x": 200, "y": 0}, "keepAway": ["dungeon-entrance"]},
  "components": {
    "residential-ward": {"enabled": true, "count": [3, 5], "density": "common", "assets": {"source": "pack"}},
    "market-square":    {"enabled": true, "count": 1},
    "garden":           {"enabled": true, "density": "common", "params": {"kind": {"mix": {"vegetable": 0.6, "flower": 0.4}}}},
    "walls":            {"enabled": true, "params": {"material": "stone"}, "assets": {"source": "generate"}},
    "farmland-ring":    {"enabled": true, "density": "some"}
  },
  "gameplay": {"playerStart": "south-gate", "npcs": "later"}
}
```

**Brief constraints (Brief schema):**
- `scale.class` must be in the build type's scale table. Its numbers default from the
  table and may be overridden within the table's range ×2. "Large" is never left as a
  bare word.
- Every component is a module allowed at a top level of this build type.
- A component with `enabled: false` has no other fields.
- The `assets.source` of every enabled component is set (the window asks).
- A feature such as `river` requires the build type to allow it.

The **confirmation window** is generated from the Brief schema: one row per component,
the field controls by type (`count` → a number or range box, `density` → a five-level
select, `enum` → a select, `mix` → sliders, `assetChoice` → pack / generate / auto plus
thumbnails). Fields with low confidence, or where the interpreter's drafts disagreed, are
highlighted.

### 3.2 Spec (step 3): the plan tree

The Spec is a tree of **nodes**. Each node is an instance of a **module** (§3.3) and
carries that module's quantity and quality fields, filled in:

```json
{
  "schema": "aige.spec/1",
  "brief": "brief.json", "seed": 7,
  "root": {
    "id": "riverford", "module": "settlement",
    "q": {"buildings": {"count": 82}, "area": {"area": 19600}},
    "k": {"layout": "organic", "condition": "lived-in"},
    "children": [
      {"id": "w-old", "module": "residential-ward",
       "q": {"plots": {"count": 28}, "area": {"area": [2600, 3200]}},
       "k": {"wealth": "common", "density": "dense", "street-style": "organic"},
       "rel": [{"type": "inside", "of": "walls-1"}, {"type": "adjacent", "to": "market"}],
       "children": [
         {"id": "w-old/plots", "module": "house-plot", "repeat": 28,
          "q": {"size": {"w": [5, 7], "h": [7, 10]}},
          "k": {"house-kind": {"mix": {"cottage": 0.7, "townhouse": 0.3}}, "fenced": true},
          "children": [
            {"id": "w-old/plots/garden", "module": "garden", "share": 0.6,
             "q": {"size": {"w": [3, 5], "h": [2, 4]}, "beds": {"count": [1, 3]}, "flowers": "common"},
             "k": {"kind": "flower", "flower-mix": {"mix": {"rose": 0.5, "tulip": 0.3, "daisy": 0.2}},
                   "fenced": true, "path": "gravel"}}
          ]}
       ]},
      {"id": "market", "module": "market-square",
       "q": {"stalls": {"count": [6, 10]}, "size": {"w": [10, 14], "h": [8, 12]}},
       "k": {"centrepiece": "well"}}
    ]
  }
}
```

- `q` holds the **quantity** fields and `k` the **quality** fields; their names and types
  come from the module.
- `repeat` (n instances from one template) and `share` (a fraction of the parent's
  instances get this child) keep large Specs small: 28 plots are one node until the
  solver needs to tell them apart. Instances get ids like `w-old/plots#14`.
- `rel` holds the node's spatial relations (§4.4), expanded into the IR.
- Nodes can be **left open**: a field may be a range or absent, and the compiler or
  solver resolves it from the range or the module default, recording the value it chose.

**How the plan is made (level by level).** The AI fills the top levels in the Brief's
terms: settlement → wards → per-ward totals and qualities. Lower levels come from the
module's **expansion rules**: defaults and distributions keyed on the build type, the
scale and the parent's qualities, i.e. the "encyclopedia". The AI is asked about a lower
node only where its quality fields need judgment (e.g. a named landmark). That keeps a
city to tens of AI calls, not thousands.

### 3.3 Module definition (the schema of a node)

Modules are data (`places/modules/<id>.module.json`; the engine ships the library and
projects may add or override):

```json
{
  "schema": "aige.module/1",
  "id": "garden", "name": "Garden", "level": "feature",
  "buildTypes": ["settlement", "farmland", "castle"],
  "parents": ["house-plot", "manor-plot", "farmstead", "inn-plot", "bailey"],
  "quantity": {
    "size":    {"type": "size", "default": {"w": [3, 5], "h": [2, 4]}, "min": {"w": 2, "h": 2}, "max": {"w": 12, "h": 10}},
    "beds":    {"type": "count", "default": [1, 3], "min": 0, "max": 12},
    "flowers": {"type": "density", "levels": [0, 0.05, 0.15, 0.3, 0.5], "unit": "per free cell", "default": "some"}
  },
  "quality": {
    "kind":       {"type": "enum", "values": ["vegetable", "flower", "herb", "mixed"], "default": "vegetable", "ask": true},
    "flower-mix": {"type": "mix", "vocabulary": "flower-kind", "when": "kind in [flower, mixed]"},
    "fenced":     {"type": "bool", "default": true, "ask": true},
    "path":       {"type": "enum", "values": ["none", "dirt", "gravel", "stone"], "default": "dirt"}
  },
  "slots": {
    "fence":  {"role": "fence-piece-set", "when": "fenced", "essential": false, "joins": "edges"},
    "plant":  {"role": "crop|flower|herb by kind", "essential": true, "affordance": "environmental"},
    "bed":    {"role": "garden-bed", "essential": false, "fallback": "terrain:tilled"},
    "extra":  {"role": "scarecrow|bench|beehive", "essential": false, "density": "rare"}
  },
  "children": {"vegetable-bed": {"min": 0, "max": 12, "count": "beds"}},
  "constraints": [
    "size.w * size.h >= 4",
    "beds * 2 <= size.w * size.h",
    "fenced implies slot(fence).available or assets.source != 'pack'"
  ],
  "relations": [
    {"type": "behind", "of": "parent.building", "weight": 3},
    {"type": "adjacent", "to": "parent.building.back-door", "weight": 2},
    {"type": "away-from", "of": "street", "weight": 1}
  ],
  "compile": "garden.compile.json",
  "checks": ["fence-closed-except-gate", "path-gate-to-door"],
  "accessibility": {"keepFree": "gate-cell + 1"},
  "vocabularies": {"flower-kind": ["rose", "tulip", "daisy", "sunflower", "lavender", "poppy"]}
}
```

A module therefore defines:
- its **fields**: quantity and quality, with types, ranges, defaults, and density-to-number
  maps;
- its **slots**: asset roles, essential or adaptable, joining rules, fallbacks;
- its allowed **children and parents**;
- its **constraints** (§4);
- its default **relations**;
- its **compile rules** to IR (§3.4);
- its **checks**;
- which fields are **asked** in Confirm.

Vocabularies are shared files (`flower-kind`, `wealth`, `condition`, `house-kind`…),
so the same word means the same thing everywhere.

### 3.4 Layout IR (step 4): the building language

The IR is a flat list of typed **statements**. Each one carries the Spec node that
caused it, so the result is always traceable:

| Statement | Fields | Meaning |
|---|---|---|
| `region` | `id`, `parent`, `shape` (`rect` · `poly` · `from:<generator>`), `size` / `area`, `node` | an area the solver must find inside its parent |
| `fixed` | `id`, `cells` or `entity` | something already there, or a locked node: constrains, never moves (GLDL's *PredefinedFacility*) |
| `anchor` | `id`, `at` (cell, edge point, or `region.side`) | a named point (gate, door, player start; GLDL's *Points*) |
| `partition` | `region`, `into` [ids], `by` (relations + interest map) | split into zones |
| `subdivide` | `region`, `style` (`skeleton` · `split` · `mixed`), `area`, `width`, `irregularity`, `access` | blocks into parcels |
| `path` | `id`, `from`, `to`, `via`, `width`, `terrain`, `class` (main · street · lane · trail), `reuse` | roads, trails, rivers (least-cost) |
| `network` | `region`, `style` (`grid` · `radial` · `organic` · `growth`), `spacing`, `width` | street networks |
| `fill` | `region` or `cells`, `terrain`, `background` | autotiled ground (6.4) |
| `place` | `id`, `slot` / `asset`, `in`, `footprint`, `facing`, `essential` | one object, placed by the solver |
| `line` | `id`, `slot` (piece set), `along` (region edge · path · cells), `gaps` [anchors], `essential` | fences, walls, hedges on cell edges |
| `rows` / `grid` | `slot`, `in`, `count` / `spacing`, `orientation` | beds, crops, pews, stalls |
| `scatter` | `slot` or `mix`, `in`, `density` (number), `spacing`, `mask`, `rules` | plants, rocks, clutter |
| `relation` | `type`, `members` [ids] (n-ary), `args`, `hard` / `weight` | constraint between statements (§4.4) |
| `objective` | `cluster`, `sparsity`, `variety` weights | layout preferences for a region |

Example, compiled from one garden instance:

```json
[
  {"op": "region", "id": "g14", "parent": "p14", "shape": "rect", "size": {"w": 4, "h": 3}, "node": "w-old/plots#14/garden"},
  {"op": "relation", "type": "behind", "members": ["g14", "p14.house"], "weight": 3},
  {"op": "anchor", "id": "g14.gate", "at": "g14.side(toward:p14.house.back-door)"},
  {"op": "fill", "region": "g14", "terrain": "grass"},
  {"op": "line", "id": "g14.fence", "slot": "fence", "along": "g14.edge", "gaps": ["g14.gate"], "essential": false},
  {"op": "rows", "slot": "bed", "in": "g14.inner", "count": 2, "orientation": "long-axis"},
  {"op": "scatter", "mix": {"rose": 0.5, "tulip": 0.3, "daisy": 0.2}, "slot": "plant", "in": "g14.inner - beds",
   "density": 0.3, "spacing": {"min": 1}},
  {"op": "relation", "type": "path-between", "members": ["g14.gate", "p14.house.back-door"], "hard": true}
]
```

**Statements are solved in a fixed order**, outer to inner and big to small, and are
never revisited once checked:
1. fixed and anchors;
2. partition;
3. paths and network;
4. subdivide;
5. regions;
6. `place` (anchor first, large first; greedy, then annealing);
7. line;
8. rows;
9. scatter;
10. fill.

## 4. Constraints

Constraints are part of the schemas and are checked at the step where they apply. They
come in six kinds.

### 4.1 Field constraints (types)
Type, unit, range, vocabulary, required/optional, `when` conditions
(`flower-mix` exists only when `kind ∈ {flower, mixed}`). Checked on every document write.

### 4.2 Structure constraints (the tree)
- A node's module must allow its parent, and its parent must allow it as a child.
- Children counts must stay within each child's `min`/`max`.
- Levels must descend (site → zone → parcel → feature → dressing).
- Ids must be unique, and every reference (`of`, `to`, `in`) must resolve.

### 4.3 Aggregate constraints (arithmetic across nodes)
These are written in a small expression language:
- **Paths:** `self.<field>`, `parent.<field>`, `children(<module>).<field>`,
  `descendants(<module>)`, `slot(<name>).available`.
- **Aggregates:** `count`, `sum`, `min`, `max`, `avg`.
- **Arithmetic:** `+ - * /`.
- **Comparison:** `== != < <= > >=`.
- **Logic:** `and or not implies`, plus `in [...]`.

Examples:

```
# settlement
self.buildings == sum(descendants(building-plot).count)          ± tolerance
sum(children().area) <= 0.85 * self.area                          # room for streets
# ward
footprintDensity(self) in [0.35, 0.65]   when self.density == 'common'
count(children(house-plot)) >= 1
# house-plot
children(garden).count <= 1
# market-square
stalls * 6 <= self.size.w * self.size.h                           # a stall and its frontage
```

A failed aggregate constraint goes back to the Plan step for that node, with the
violated expression as the reason.

### 4.4 Spatial relations (IR)
These come from Holodeck's vocabulary, extended for 2D and groups (SceneCraft). Each
relation is **n-ary**, has a **scoring function in [0, 1]**, and is either `hard` or
weighted:

| Kind | Relations |
|---|---|
| global | `center`, `edge`, `corner`, `along-edge(side)`, `anywhere` |
| distance | `adjacent`, `near(d0,d1)`, `far(d)`, `away-from(d)` |
| position | `in-front-of`, `behind`, `beside`, `between` (relative to facing) |
| alignment | `aligned`, `in-row`, `in-grid`, `symmetric(axis)`, `repeat(spacing)` |
| facing | `faces(target)` |
| linear | `along(path)`, `encloses`, `crosses`, `path-between(a,b)` |
| containment | `inside`, `outside`, `on` (small on large) |
| visibility | `visible-from(anchor)`, `focus(anchor)` (a landmark at the end of a view) |

### 4.5 Hard global rules (always)
- Inside the parent region.
- No overlapping footprints on the same layer.
- Allowed terrain only.
- **Reachability:** every `anchor` of type door, gate or entrance is reachable from the
  place's entrance.
- **Accessibility** cells kept free.
- Linear features continuous, with joined pieces.
- Zero autotile mismatches.
- Perspective compatible with the grid.
- Sizes within slot ranges.

### 4.6 Conformity tolerances (Verify)
Built vs Spec: counts within ±5% (or exactly, for `count` < 10); densities within one
level's band; areas within ±10%; every `essential` slot filled; every soft relation's
score reported. Missing art is reported per slot. Tolerances are part of the schema
(`aige.tolerance/1`), not hard-coded.

## 5. Editing after the build, in schema terms

- Every entity carries `PlaceNode {place, node, statement}`.
- Changing a field in the Inspector **changes the Spec node** (marked `source: user`,
  `locked`) and **recompiles that subtree**. Siblings and parents become `fixed` for the
  solve.
- A hand edit inside a node **locks** it: its entities become `fixed` statements on the
  next regeneration.
- Regenerating a parent **matches** old children to new ones by id and position (as
  Vanegas et al. do for parcels), so locks and edits survive.
- The **module brush** paints a new node of a chosen module over cells. It is inserted
  into the Spec under the node that owns those cells, then compiled and solved like any
  other node.

## 6. Open points to settle before coding

1. The **vocabularies**: the first set for settlements (house kinds, wealth, condition,
   materials, flower and crop kinds…).
2. The **scale tables** per build type (numbers per class).
3. ~~Density maps~~, decided (2026-10-18; left to us): **per field**, starting from
   one shared default scale (0, 0.05, 0.15, 0.3, 0.5 per free cell). A field overrides it
   only where its unit differs (trees per 10 cells, stalls per side). Values are tuned
   from verified builds.
4. ~~Tolerances~~, decided (2026-10-18; left to us): the defaults in §4.6, kept in
   `aige.tolerance/1` and tuned during 7.7 testing.
5. The exact **text form** of the IR for humans (JSON is the canonical form; a readable
   rendering is shown in review).
