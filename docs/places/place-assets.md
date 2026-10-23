# Place assets — finding, fetching or making the art a build needs

Status: **draft for review (2026-10-18)**. Part of the Phase 7 design. It decides,
for every asset a build needs, **where it comes from**:
1. the **project**;
2. the **AIGE library** (a curated asset library, planned);
3. the **web**;
4. the **Art tool** (generation).

It also decides what happens when none of them gives a usable asset. It runs between
**Confirm** and **Compile** (`docs/places/place-schema.md` §1).

## 1. Terms

| Term | Meaning |
|---|---|
| **need** | what a slot asks for: an ontology **role** (`fence-piece-set`, `flower`, `house-body`), the **concepts** it must show (`rose`, `cottage`), a footprint range, a layer, and the perspective the grid accepts. One need per (role, concept) combination of the place, not per instance. |
| **candidate** | an asset that might fill a need, from any source, with its catalog entry (`ai-building.md` §6: role, affordance, footprint, anchor, perspective, palette, piece set…). |
| **fit** | a candidate's score for a need, in [0, 1] (§4). |
| **source** | `project`, `library`, `web` or `generate`. |
| **policy** | the user's choice per component in the confirmation window: `auto`, `project`, `library`, `web`, `generate` (§5). |
| **style profile** | the place's look, measured from the assets it already uses: palette, pixels per cell, outline weight and colour, shading style, perspective. Generated and fetched art are judged against it. |

## 2. Sources

| Source | What it is | Cost | Style match | Licence |
|---|---|---|---|---|
| **project** | assets already imported and analysed in this project | none | best (it *is* the style) | the project's |
| **library** | the **AIGE library**: a curated, pre-analysed collection (catalog v2 metadata, linked to ontology roles and concepts), organised in style families, installed per project | a copy | good *within its style family* | known, recorded per asset |
| **web** | search on approved sites (free game-art sites: OpenGameArt, itch.io free assets, Kenney, …) through the web tool, then download, segment and analyse | search + analysis time | uncertain: varies by author | **must be verified per asset**; unknown → rejected |
| **generate** | the Art tool (existing image generation and art history), prompted with the need **and the style profile**, including reference images from the project | model calls | good with references; verified by the critic | the project's (generated) |

**The AIGE library** (to be built separately) has the same interface as every other
source, so the procedure works before it exists. Until then the library source simply
returns no candidates. Each library asset carries:
- its catalog entry;
- its ontology links (`canFill` roles, concepts);
- its style family;
- its licence;
- an embedding for retrieval.

## 3. Collecting the needs (the asset plan)

From the confirmed Brief, before any plan exists:
1. For every enabled component, take its module and its descendants' **slots** from the
   ontology.
2. Resolve each slot's role and concepts from the component's quality fields (a flower
   garden with a rose / tulip mix needs `flower:rose` and `flower:tulip`).
3. Merge identical needs across components. Record which components use each need, and
   whether any of them marks it **essential**.
4. Give each need an **importance**:
   - `critical`: essential, **and** large or structural (buildings, terrain, roads,
     walls, fences);
   - `normal`: essential, small;
   - `optional`: adaptable.
5. Mark each need **style-critical** if it is a building, terrain or tile set, a piece
   set (pieces must join), or something seen many times. A style-critical need must
   match the style profile closely.

The result is the **asset plan**: a table of needs, shown in the second round of the
confirmation window (§6), and resolved by §5.

## 4. Fit

**Hard filters** (a candidate failing any one is not a candidate):
- it fills the role (`canFill`) and shows the concept (the ontology's `broader` counts:
  any `rose` fills `flower`);
- its **perspective is compatible** with the grid (`ai-building.md` §6);
- its footprint fits the need's range (after allowed scaling);
- its layer is right;
- its **licence is known and allowed** (§7);
- for a piece set: the pieces the line needs are present (straight + corner at least),
  or the missing ones can be generated from it (§5, rule G3).

**Score** of the remaining candidates:

```
fit = 0.40 · semantic     how well it shows the concept (vision description vs need; catalog tags)
    + 0.35 · style        closeness to the style profile: palette ΔE, pixels-per-cell ratio,
                          outline, shading (1 = same pack)
    + 0.15 · size         closeness of its natural size to the need's footprint (no heavy rescaling)
    + 0.10 · quality      clean edges, complete (not a partial tile), resolution
```

For **style-critical** needs, the style term must be at least 0.7 on its own.

**Thresholds:** `fit ≥ 0.75` is **good**. `0.55 ≤ fit < 0.75` is **usable** (it may need
an adaptation: recolour to the palette, rescale, mirror). Below 0.55 the candidate is
**rejected**.

## 5. The decision procedure

For each need, in order of importance (critical first), with the component's policy:

**Policy `auto` (the default):**

| Step | Rule |
|---|---|
| A1 | **Project** has a *good* candidate → use it. |
| A2 | **Library** has a *good* candidate → import it into the project (copied, analysed, licence recorded) and use it. |
| A3 | The need is **style-critical** → **generate** (rule G). Art for buildings, terrain and piece sets fetched from the web rarely matches the pack. |
| A4 | Otherwise, if web search is enabled for the project → search the **web** (rule W). A *good* result is fetched and used. |
| A5 | Otherwise → **generate** (rule G). |
| A6 | Nothing good yet, but a *usable* candidate exists from steps A1–A5 → use the best usable one, adapted. It is reported. |
| A7 | Still nothing: an `optional` need is **dropped** (its slots stay empty, as allowed); a `normal` or `critical` need gets a **labelled placeholder** and is reported as missing. |

**Other policies** narrow the order:

| Policy | Order |
|---|---|
| `project` | A1, then A6 (project only); then A7 |
| `library` | A1, A2, then A6; then A7 |
| `web` | A1, A4 (even for style-critical needs, with the user warned), then A6; then A7 |
| `generate` | G directly (the project and library are still checked for references) |

**Rule G — generate**
1. **G1.** Prompt the Art tool with the need (role, concept, description from the module
   and Brief quality fields, footprint, perspective, layer) **and the style profile**,
   with 1–3 reference images from the project that are closest in role.
2. **G2.** Analyse the result like any asset (catalog v2). Then the critic checks it:
   - does it show the concept;
   - does it match the style profile (style ≥ 0.7 for style-critical needs);
   - are the footprint and perspective right.
3. **G3.** For **piece sets**, generate the **missing pieces from an existing piece** as
   the reference (a corner from a straight), so they join. Verify the joins by edge
   analysis.
4. **G4.** Up to 3 attempts, each with the critic's reasons added to the prompt. A
   passing result is good; the best failing one may be usable (A6).

**Rule W — web**
1. **W1.** Search the approved sites with queries built from the concept, its synonyms
   (ontology labels), the perspective and the style words ("top-down 32px rose
   flower").
2. **W2.** Download candidates **into an isolated folder**. Check the file type and size.
   Read the licence and author from the asset's page.
3. **W3.** Segment tilesheets into whole objects, then analyse each (catalog v2).
4. **W4.** Score as in §4. Keep a good one, with its provenance (§7).
5. **W5.** At most 3 pages of results per need.

**Batching.** All needs are resolved before compiling. Generation and web requests for
independent needs run in parallel.

## 6. What the user sees and decides

In the **second round** of the confirmation window, the asset plan is a table:

| Need | Used by | Importance | Proposed | Source | Alternatives |
|---|---|---|---|---|---|
| fence pieces | gardens, pastures | critical | (thumbnail) wooden fence set | project | 2 more, or generate |
| rose | flower gardens | normal | — | **generate** (no match) | — |
| market stall | market | critical | (thumbnail) | library | — |

Per row, the user can **change the source**, **pick an alternative**, or **exclude** the
need (its slots stay empty). A summary line counts the needs by source (from the project,
from the library, from the web, to generate, missing). The decided plan is saved as
`places/<place>/assets.json`. Generation and fetching start when the user presses
**Build**.

## 7. Licences and provenance

- Every asset used in a place has a **known licence**, recorded in its `.meta`:
  `licence`, `author`, `source` (`project` · `library` · `web` · `generated`), `url`,
  `retrieved` (date).
- **Allowed licences** (a project setting): by default CC0, CC-BY and OGA-BY (attribution
  required); CC-BY-SA only if the project allows share-alike; never "unknown", "personal
  use only" or no licence. A web candidate whose licence can't be read is rejected.
- Assets needing attribution are added to the project's **`CREDITS.md`** automatically,
  and the export step (5.1) includes it.
- A **hard check** in Verify: every built entity's asset has an allowed licence.

## 8. Missing assets, the cases

| Case | What happens |
|---|---|
| A whole style-critical family is missing (the pack has no walls) | generate the family from the style profile; the window warns that it's generated |
| A piece set is partial (straight fence only) | generate the missing pieces from the existing piece (G3) |
| A concept is missing but a broader one exists (no "tulip", only "flower") | a broader asset is a usable candidate (lower semantic score); generate a better one if the policy allows |
| An `optional` need is missing | dropped; listed in the report |
| An `essential` need can't be found or made | labelled placeholder; the report lists it; the part still builds |
| The user excluded a need | its slots stay empty; essential slots that depend on it are reported, not filled |

## 9. Tests (for milestone 7.9)

- The decision table on synthetic catalogs: each policy, each importance, each threshold
  edge.
- Licence filtering: unknown or disallowed licences are never used; CREDITS is written.
- Piece-set completion: the generated pieces join (edge analysis).
- The asset plan validates, and the window's choices round-trip into `assets.json`.
