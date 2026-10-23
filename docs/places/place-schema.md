# Place schemas — the building procedure, its documents and their constraints

Status: **draft, reviewed 2026-10-18**. Nothing in Phase 7 is built before these
schemas are agreed. The design set, and what each document is the authority for:

| Document | Authority for |
|---|---|
| `ai-building.md` | why: goals, decisions, research, principles |
| **`place-schema.md`** (this) | the procedure, the documents, field types, constraints, editing |
| `layout-ir.md` | PLL, the intermediate language: statements, relations, solving order, checks |
| `place-ontology.md` | the knowledge graph: classes, properties, rules; the module library lives in it |
| `place-modules.md` | the catalogue of modules per build type |
| `place-assets.md` | where each asset comes from (project, library, web, generated) |
| `place-interaction.md` | what the user sees; how each AI call is made (thinking, answer, validation) |

Where two documents touch the same subject, the one above is the authority and the other
only summarises.

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
| 2b Assets | Brief → **asset plan** (needs → sources), confirmed in round 2; resolved (fetched, generated) when the user presses Build, in parallel with Plan | the decision procedure of `place-assets.md`; the user per row | asset plan schema; licences |
| 3 Plan | Brief → **Spec**: the tree of parts, each with **quantity** and **quality** fields | AI for the upper levels; module defaults and rules for the lower ones | module schemas + constraints (§4) |
| 4 Compile | Spec → **Layout IR**: regions, anchors, placements, lines, scatters, relations | each module's compile rules; AI adds relations at the upper levels | IR schema + references + feasibility pre-checks |
| 5 Solve & build | IR → geometry → engine bulk commands | seeded solvers and generators | hard constraints |
| 6 Verify | scene + Spec + IR → **report** | checks + vision critique | tolerances (§4.6) |

Every document is JSON, kept in the project under `places/<place>/`:

| File | Written by | Read by |
|---|---|---|
| `brief.json` | Confirm | Assets, Plan |
| `assets.json` | Assets | Compile, Solve, Verify |
| `spec.json` | Plan | Compile, Verify, editing |
| `layout.ir.json` | Compile | Solve |
| `layout.solved.json` | Solve | Build, Verify, editing (instance matching) |
| `report.json` | Verify | the Build panel's Result step, editing |
| `log.jsonl` | every AI call (inputs, thinking, answer, validation) | debugging, replay, tests |

Each step reads only documents written before it, so any step can be re-run alone, and
every built entity can be traced back to the Spec node that made it. How each AI call is
made (thinking first, then a strict answer, then validation) is defined in
`place-interaction.md`, Part B.

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
| `assetChoice` | `{"source": "auto" \| "project" \| "library" \| "web" \| "generate", "assets": [...], "exclude": [...]}` | where a component's art comes from (`place-assets.md` §5) | per component |
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
    "residential-ward": {"enabled": true, "count": [3, 5], "density": "common", "assets": {"source": "auto"}},
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
  table. The user may override them; if the new numbers leave the class's range, the
  class follows the numbers (asking for 200 buildings in a "village" makes it a town,
  and the window says so). "Large" is never left as a bare word.
- Every component is a module allowed at a top level of this build type.
- A component with `enabled: false` has no other fields.
- The `assets.source` of every enabled component is set (default `auto`).
- Every `params` field is a quality or quantity field of the component's module, with a
  value of the field's type.
- A feature such as `river` requires the build type to allow it.

The **confirmation window** is generated from the Brief schema: one row per component,
the field controls by type (`count` → a number or range box, `density` → a five-level
select, `enum` → a select, `mix` → sliders, `assetChoice` → a source select plus
thumbnails). Fields with low confidence, or where the interpreter's drafts disagreed, are
highlighted. What the window shows, round by round, is defined in
`place-interaction.md` A2.

**When the window opens:** for every new build, and for any regeneration that changes a
Brief field (size, components, style). Edits to a single node's fields in the Inspector
don't open it: the edit is the confirmation.

### 3.2 Spec (step 3): the plan tree

The Spec is a tree of **nodes**. Each node is an instance of a **module** (§3.3) and
carries that module's quantity and quality fields, filled in:

```json
{
  "schema": "aige.spec/1",
  "brief": "brief.json", "seed": 7,
  "root": {
    "id": "riverford", "module": "settlement",
    "quantity": {"buildings": {"count": 82}, "area": {"area": 19600}},
    "quality": {"layout": "organic", "condition": "lived-in"},
    "children": [
      {"id": "w-old", "module": "residential-ward",
       "quantity": {"plots": {"count": 28}, "area": {"area": [2600, 3200]}},
       "quality": {"wealth": "common", "density": "dense", "street-style": "organic"},
       "rel": [{"type": "inside", "of": "walls-1"}, {"type": "adjacent", "to": "market"}],
       "children": [
         {"id": "w-old/plots", "module": "house-plot", "repeat": 28,
          "quantity": {"size": {"w": [5, 7], "h": [7, 10]}},
          "quality": {"house-kind": {"mix": {"cottage": 0.7, "townhouse": 0.3}}, "fenced": true},
          "children": [
            {"id": "w-old/plots/veg-garden", "module": "garden", "share": 0.36,
             "quantity": {"size": {"w": [3, 5], "h": [2, 4]}, "beds": {"count": [2, 4]}},
             "quality": {"kind": "vegetable", "crop-mix": {"mix": {"cabbage": 0.5, "carrot": 0.5}}, "fenced": true}},
            {"id": "w-old/plots/flower-garden", "module": "garden", "share": 0.24,
             "quantity": {"size": {"w": [3, 5], "h": [2, 4]}, "beds": {"count": [1, 3]}, "flowers": "common"},
             "quality": {"kind": "flower", "flower-mix": {"mix": {"rose": 0.5, "tulip": 0.3, "daisy": 0.2}},
                         "fenced": true, "path": "gravel"}}
          ]}
       ]},
      {"id": "market", "module": "market-square",
       "quantity": {"stalls": {"count": [6, 10]}, "size": {"w": [10, 14], "h": [8, 12]}},
       "quality": {"centrepiece": "well"}}
    ]
  }
}
```

(Other children of the settlement, such as the walls `walls-1`, are left out of the
example.)

- `quantity` and `quality` hold the node's fields; their names and types come from the
  module.
- `repeat` (n instances from one template) and `share` (that fraction of the parent's
  instances get this child) keep large Specs small: 28 plots are one node until the
  solver needs to tell them apart. Instances get ids like `w-old/plots#14`. In the
  example, the Brief's gardens are "common" (60% of plots) with a 60 / 40 vegetable–flower
  mix, so the shares are 0.36 and 0.24. Shares of siblings of the same module must sum to
  ≤ 1.
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
    "fence": {"role": "fence-piece-set", "when": "fenced", "essential": false},
    "plant": {"essential": true, "bind": [
               {"when": "kind == 'vegetable'", "role": "crop",   "concepts": "crop-mix"},
               {"when": "kind == 'flower'",    "role": "flower", "concepts": "flower-mix"},
               {"when": "kind == 'herb'",      "role": "herb"},
               {"when": "kind == 'mixed'",     "role": "crop|flower", "concepts": "crop-mix + flower-mix"}]},
    "bed":   {"role": "garden-bed", "essential": false, "fallback": {"fill": "tilled"}},
    "extra": {"role": "garden-prop", "concepts": ["scarecrow", "bench", "beehive"], "essential": false}
  },
  "children": {"vegetable-bed": {"min": 0, "max": 12, "count": "beds", "weight": 1.0}},
  "constraints": [
    "size.w * size.h >= 4",
    "beds * 2 <= size.w * size.h",
    "fenced implies asset(fence) != 'missing'"
  ],
  "relations": [
    {"type": "behind", "members": ["self", "parent.primary"], "weight": 3},
    {"type": "adjacent", "members": ["self", "parent.primary.back"], "weight": 2},
    {"type": "away-from", "members": ["self", "parent.street"], "args": {"d": 2}, "weight": 1}
  ],
  "compile": "garden.compile.json"
}
```

- **Slots** bind to ontology **roles**. `concepts` names a quality field whose values
  (a `mix` or `enumSet`) say *which* concepts the art must show, or lists them literally.
  `bind` picks the role by a condition. `fallback` says what stands in when no asset is
  found (here: tilled ground instead of a bed sprite).
- **Children** are authoritative: a module lists what it may contain (the ontology's
  `consistsOf`, with its weight). Which parents a module can have is derived, never
  written twice.
- **Vocabularies** are shared ontology concept schemes (`flower-kind`, `crop-kind`…),
  never defined inside a module.
- **Accessibility** and **checks** are written in the compile template as PLL `keepFree`
  fields and `check` statements, so they have one meaning.

#### Compile templates

A module's compile template (`<id>.compile.json`) is a list of PLL statements
(`layout-ir.md`) with **placeholders** for the node's values:
- `${size}`, `${beds}`, `${flowers.number}` (a density's numeric value), `${self}` (the
  node's region id), `${parent.primary}` (the parent's primary object);
- `"when": "<expression>"` on a statement includes it only when the condition holds;
- `"each": "children(vegetable-bed)"` repeats a statement per child.

```json
[
  {"op": "region", "id": "${self}", "in": "${parent}", "shape": "rect", "size": "${size}"},
  {"op": "anchor", "id": "${self}.gate", "at": "${self}.side(toward: ${parent.primary}.back)", "kind": "gate"},
  {"op": "line", "id": "${self}.fence", "slot": "fence", "along": "${self}.edge", "gaps": ["${self}.gate"],
   "essential": false, "when": "fenced"},
  {"op": "rows", "id": "${self}.beds", "in": "${self}.inner", "slot": "bed", "count": "${beds}", "orientation": "long-axis"},
  {"op": "scatter", "id": "${self}.plants", "in": "${self}.inner - ${self}.beds", "slot": "plant",
   "density": "${flowers.number}", "spacing": {"min": 1}},
  {"op": "relation", "type": "path-between", "members": ["${self}.gate", "${parent.primary}.back"], "hard": true},
  {"op": "check", "predicate": "closed", "args": {"ref": "${self}.fence"}, "when": "fenced"}
]
```

The module's `relations` are emitted as `relation` statements, and every statement gets
`node` set to the instance's Spec id.

A module therefore defines:
- its **fields**: quantity and quality, with types, ranges, defaults, and density-to-number
  maps;
- its **slots**: asset roles, essential or adaptable, joining rules, fallbacks;
- its allowed **children and parents**;
- its **constraints** (§4);
- its default **relations**;
- its **compile template** to PLL (above), which also carries its accessibility and
  checks;
- which fields are **asked** in Confirm.

Module files are **part of the ontology**: the ontology loader reads them as `Module`
classes with their properties (`place-ontology.md` §1). There is one source, not a copy.
They are called *place modules* to keep them apart from the engine's gameplay modules
(4.8).

Vocabularies are shared files (`flower-kind`, `wealth`, `condition`, `house-kind`…),
so the same word means the same thing everywhere.

### 3.4 Layout IR (step 4): the building language

The full definition of the language (PLL) is in **`docs/places/layout-ir.md`**:
its space model, references, all statements, relation score functions, solving phases,
objective, checks, errors and repair, and text form. This section is a summary.

In short: a PLL document is a flat list of typed statements:
- givens: `fixed`, `anchor`;
- areas: `region`, `partition`, `subdivide`, `floorplan`, `graph`;
- networks: `path`, `network`;
- openings: `connect`;
- objects: `place`, `line`, `rows`, `grid`, `scatter`;
- ground: `fill`;
- rules: `relation`, `objective`, `check`.

Each statement carries the Spec `node` that caused it. The solver works through the
region tree from outer to inner, and inside each region in a fixed order (`layout-ir.md`
§7). The compile templates above are written in it.

### 3.5 Nested places

Very big builds contain whole places: a region's towns, a town's castle, a building's
interior. A Spec node of module `subplace` names a **child place** (`places/<parent>/<child>/`)
with its own Brief, Spec, PLL and report. Its Brief is generated from the node's fields
and confirmed in the parent's window (one row per subplace kind, not per instance). In the
parent's PLL, the child is a `region` whose boundary and entry anchors (gates, road ends,
the door) are solved by the parent. The child solves inside that boundary with those
anchors as `fixed`. Children are built after their parent's level that places them, and
can be built in parallel.

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

- **Classes:** `descendants(class: BuildingParcel)` selects nodes by ontology class
  (every parcel module whose primary slot is a building), so counts don't depend on
  listing module names.
- **Tolerance:** `within(a, b, tol)`, where `tol` is a tolerance name from
  `aige.tolerance/1`.

Examples:

```
# settlement
within(self.buildings, count(descendants(class: BuildingParcel)), tol: 'count')
sum(children().area) <= 0.85 * self.area                          # room for streets
# ward
count(children(class: BuildingParcel)) >= 1
# house-plot
count(children(garden)) <= 1
# market-square
stalls * 6 <= self.size.w * self.size.h                           # a stall and its frontage
```

**Ranges.** A field may still hold a range during planning. A constraint over ranges is
**satisfiable** when some choice of values within the ranges meets it, checked with
interval arithmetic. The compiler then picks values that meet every constraint of the
node (seeded, recorded in the PLL). A constraint that no choice can meet is an error
against the node.

**Plan time vs solve time.** Spec constraints use only Spec values: counts, sizes,
qualities and shares. Anything that depends on where things land (footprint density,
reachability, a relation's score) is a PLL `check` (`layout-ir.md` §8.3), emitted by the
compile template. For example, the ward's density band becomes
`check footprint-density(${self}, [0.35, 0.65])`.

A failed aggregate constraint goes back to the Plan step for that node, with the
violated expression as the reason.

### 4.4 Spatial relations and 4.5 hard rules

These are defined in PLL: the relation vocabulary with each relation's score function
and default hardness in `layout-ir.md` §6, and the rules that always hold in §8.1. They
cover inside, no overlaps, allowed terrain, kept-free cells, reachability (respecting
locks and keys), continuous linear features, perspective compatible with the grid, and
every asset's licence allowed.

### 4.6 Conformity tolerances (Verify)
Built vs Spec: counts within ±5% (or exactly, for `count` < 10); densities within one
level's band; areas within ±10%; every `essential` slot filled; every soft relation's
score reported. Missing art is reported per slot. Tolerances are part of the schema
(`aige.tolerance/1`), not hard-coded.

## 5. Editing after the build, in schema terms

- Every entity carries `PlaceNode {place, node, statement, instance}`.
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
5. ~~Text form of the IR~~, defined in `layout-ir.md` §10.
