# The place ontology — knowledge the builder reasons with

Status: **draft for review (2026-10-18)**. Working name **GBO** (GenBuilding Ontology);
alternatives are under discussion. It sits above the schemas in
`docs/place-schema.md` and holds the modules of `docs/place-modules.md` as a
knowledge graph.

## 1. Why a knowledge graph, not only module files

Module files describe each module **on its own**. The builder also needs to know how
things **relate across modules**. For example:
- a water mill *requires* a river;
- a tannery is *typically downstream of* houses;
- "smithy", "forge" and "blacksmith" are *the same concept*;
- a well is *a kind of* water source;
- an asset tagged `rose` *can fill* the `flower` role;
- patricians *prefer* high ground.

In a graph this is one queryable structure. The builder consults it at every step:

| Step | Asks the ontology |
|---|---|
| Interpret | Which concepts do the query's words name (labels, synonyms, *broader*)? Which build type? |
| Confirm | Which components does this build type allow? Which fields are asked? What do size words mean in numbers? |
| Plan | What may this node contain, how many, how typically (weights)? What does it require or exclude? Which defaults fit these qualities? |
| Compile | Which relations hold by default between these modules? What does each relation mean geometrically? |
| Assets | Which roles can this asset fill (`canFill`)? Which assets fill this role, in this perspective? |
| Verify | Which rules apply to this node and its subtree? |
| Explain | Why was this placed here? (Each decision cites the facts it used.) |

**The module files are generated from the ontology** (or validated against it), so there
is one source of truth.

## 2. What it contains

**Classes** (the TBox):

| Class | Meaning | Examples |
|---|---|---|
| `BuildType` | a kind of place, with its stages and Brief schema | Settlement, Interior, Dungeon, Region |
| `Module` | a part, at a `Level` | Garden ⊑ Feature, HousePlot ⊑ Parcel, ResidentialWard ⊑ Zone |
| `Field` | a quantity or quality field of a module | Garden.flowers (density), Garden.kind (enum) |
| `Concept` | a vocabulary value | Rose ⊑ Flower, Smithy ⊑ Workshop, Cobble ⊑ Surface |
| `Role` | an asset role that a slot needs | FencePieceSet, Flower, HouseBody |
| `Asset` | a catalogued asset (instances come from the project) | `assets/pack/rose.png` |
| `Relation` | a spatial relation type, with its scoring function | Behind, Near, AlongEdge, Faces |
| `Rule` | a design rule or constraint, with its source | "density falls outward" (Lynch, Watabou) |
| `ScaleClass` | a size class with numbers | LargeVillage: 60–150 buildings |

**Properties** (the edges):

| Property | Between | Carries |
|---|---|---|
| `subClassOf` | classes | taxonomy (Garden ⊑ Feature) |
| `consistsOf` | Module → Module | **weight** (typical share), **cardinality** (min/max), condition (`when`). This is Hierarchical WFC's DAG |
| `allowedIn` | Module → BuildType | — |
| `requires` / `excludes` | Module ↔ Module or Feature | WaterMill requires River; Docks require Water |
| `typicallyRelated` | Module × Module | a `Relation` with a weight (Tannery *downstream-of* HouseArea) |
| `prefersSite` | Module → interest criterion | weight and sign (Church: high ground +0.8). These are Emilien's interest functions |
| `hasField` | Module → Field | the type, range, default and density map |
| `needsRole` | Module → Role | essential or adaptable; joins |
| `canFill` | Asset → Role | from the catalog: confidence, perspective, footprint |
| `label` / `altLabel` | any → text | names and synonyms in several languages, for interpretation |
| `broader` / `narrower` | Concept ↔ Concept | the SKOS hierarchy of vocabulary values |
| `source` | any fact → source | a paper, a design rule, a user, or a verified build |

**Rules** in the graph are the constraints of `place-schema.md` §4, attached to the
classes they constrain, so the builder can ask *"which rules apply to a garden in a
manor plot?"* and get the inherited set.

## 3. Representation

- **Inside the engine:** a typed property graph loaded into memory, in a simple JSON
  format (`places/ontology/*.json`), with stable ids. No triple store is needed at run
  time. The graph is a few thousand nodes.
- **Exchange:** export and import as standard Semantic Web formats: **OWL** for classes
  and properties, **SKOS** for the vocabularies (labels, synonyms, broader / narrower),
  and **SHACL** for field constraints. The aggregate arithmetic of §4.3 goes beyond SHACL
  core and stays in our expression language. Standard formats let others reuse the
  ontology and let us cite it.
- **For the AI:** the builder never pastes the whole graph into a prompt. Each step
  retrieves the **relevant subgraph**: the node's module, its allowed children with
  weights, its rules, its relations, and the matching concepts and roles. This keeps
  prompts small and grounded.

## 4. How it grows

- **Hand-authored core:** the shared and settlement modules, vocabularies and design rules
  from the literature (each fact carries its `source`).
- **AI-proposed extensions, reviewed:** a new module or concept (e.g. "a lighthouse")
  is proposed as a small subgraph, validated like any document, and added after review.
  This is the offline database authoring of the database-driven framework.
- **Learned from verified builds:** approved builds adjust `consistsOf` weights and
  `typicallyRelated` weights (how often gardens appear in rich wards, say), and store
  examples. Learned facts are marked `source: learned`, so they stay separable from
  authored ones.
- **Per project:** a project can add concepts, modules and roles (its own art style, its
  own culture) without editing the engine's core.

## 5. Language: our own, aligned with GLDL

The Layout IR is **our own language**, but it keeps GLDL's core concepts with the same
meaning, so the correspondence can be documented and compared:

| GLDL (Xu & Verbrugge 2025) | Our IR |
|---|---|
| *PredefinedFacility* | `fixed` |
| *NewFacility* | `place` (and `region` for areas) |
| *Points* | `anchor` |
| basic constraints (PlaceAroundPosition, PlaceInRange, PlaceByWall) | global relations (`center`, `near`, `along-edge`) |
| relationship constraints (Near/Far, CanSee, Focus, Alignment/Orientation) | distance, visibility, alignment, facing relations |
| penalty + cluster + sparsity objective | `objective` + relation scores |

GLDL can't serve as our language directly:
- It describes **one 3D room of cuboid facilities**. It has no hierarchy of parts, and no
  quantities, densities or qualities.
- It has no linear features (fences, walls), networks (roads, rivers), area subdivision,
  scatter, rows or terrain.
- It has no tiles, perspectives, n-ary group relations or provenance links back to a
  plan.
- It is a research notation defined in a paper appendix, without a published grammar or
  tools to build on.

Adopting it would mean extending nearly all of it. Keeping its terms where they overlap
gives us comparability (an interior room could be exported to GLDL) without its limits.
