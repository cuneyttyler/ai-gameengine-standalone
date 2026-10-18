# GBO — the GenBuilding Ontology

GBO is the knowledge the AIGE builder uses to generate game places (settlements,
interiors, dungeons, regions) from a request:
- the **parts** a place is made of: modules, at levels from site to dressing;
- each part's **quantity and quality fields**;
- what each part **may contain**, how many and how typically;
- the **spatial relations** between parts;
- the **asset roles** their art must fill;
- the **vocabularies** their qualities use;
- **design rules**, each with its source.

Design: [`docs/place-ontology.md`](../docs/place-ontology.md). Schemas:
[`docs/place-schema.md`](../docs/place-schema.md).

## Files

| File | What | Written |
|---|---|---|
| `gbo.ttl` | **Core vocabulary** (OWL 2): places and levels, modules, fields and field types, slots and roles, child rules, relation types and uses, site preferences, build types and scale classes, design rules and provenance | by hand |
| `gbo-library.ttl` | **The library** (OWL 2 + SKOS): every module with its fields, slots, child rules (also as OWL cardinality restrictions on `gbo:hasPart`), constraints, relations and site preferences; module classes; build types and scale tables; concept schemes; roles; relation types; design rules | generated |
| `gbo-shapes.ttl` | **SHACL shapes**: checks of the library itself (every module has a level and an id, every field a type, every design rule a source, scale ranges run low to high), plus one shape per module for built places described in GBO terms | generated |

## One source

The engine works from JSON: `data/places/*.json` and
`data/places/modules/*.module.json`. A project can add its own in `<project>/places/`.
The two generated files are exported from that JSON by
`engine/places/OntologyExport.cpp`, so the JSON and the published ontology never
drift apart:
- The test `PlacesOntologyTest.PublishedTurtleIsCurrent` fails when the published
  files differ from what the JSON generates.
- To regenerate, run the tests with `AIGE_WRITE_ONTOLOGY=1`:

  ```
  AIGE_WRITE_ONTOLOGY=1 build/tests/Debug/aige_tests --gtest_filter=PlacesOntologyTest.PublishedTurtleIsCurrent
  ```

## Namespaces

| Prefix | Namespace | Holds |
|---|---|---|
| `gbo:` | `https://w3id.org/aige/gbo#` | the core vocabulary |
| `gbl:` | `https://w3id.org/aige/gbo/library#` | modules (`gbl:HousePlot`), module classes (`gbl:BuildingParcel`), fields (`gbl:HousePlot.size`), slots (`gbl:HousePlot.slot.house`), child rules (`gbl:HousePlot.child.garden`) |
| `gbb:` | `https://w3id.org/aige/gbo/build#` | build types (`gbb:settlement`), scale classes (`gbb:settlement.large-village`) |
| `gbv:` | `https://w3id.org/aige/gbo/vocab#` | concept schemes (`gbv:flower-kind`) and concepts (`gbv:flower-kind.rose`) |
| `gbr:` | `https://w3id.org/aige/gbo/role#` | asset roles (`gbr:fence-piece-set`) |
| `gbx:` | `https://w3id.org/aige/gbo/relation#` | relation types (`gbx:away-from`) |
| `gbd:` | `https://w3id.org/aige/gbo/rule#` | design rules (`gbd:density-falls-outward`) |
| `gbs:` | `https://w3id.org/aige/gbo/shapes#` | SHACL shapes |

Ids are stable: a module, concept or role is never renamed, so older documents keep
resolving. `gbo:moduleId` and `skos:notation` hold the JSON id of each term.

## Modelling notes

- **Modules are classes and individuals at once** (OWL 2 punning). As a class,
  `gbl:Garden` sits under its level (`gbo:Feature`) and its module classes
  (`gbl:PlantedArea`); a built garden is an instance of it. As an individual, it
  carries its definition (`gbo:hasField`, `gbo:hasChildRule`, ...).
- **Child rules** keep their weight and count field in a `gbo:ChildRule`.
  `gbo:consistsOf` is the shortcut (a property chain). Fixed cardinalities are also
  stated as OWL qualified-cardinality restrictions, and as SHACL qualified counts for
  built places.
- **Fields** are properties of the module's places, so a built place can be written
  as plain RDF and checked against `gbo-shapes.ttl`.
- **Beyond OWL and SHACL Core:** conditions (`gbo:when`) and aggregate constraints
  (`gbo:constraint`) are strings in the GBO expression language
  (`docs/place-schema.md` §4.3), evaluated by the engine on intervals. Defaults,
  density maps and relation arguments are `rdf:JSON` literals in the module-file form.

## Checking

The files parse with any Turtle parser. The library passes its own shapes:

```
uv run --no-project --with rdflib --with pyshacl python -c "
import rdflib, pyshacl
g = rdflib.Graph().parse('ontology/gbo.ttl').parse('ontology/gbo-library.ttl')
s = rdflib.Graph().parse('ontology/gbo-shapes.ttl')
print(pyshacl.validate(g, shacl_graph=s, inference='rdfs', advanced=True)[0])"
```

## Publishing

- **Licence:** [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) (stated in
  each file's `dcterms:license`). This applies to the ontology files only, not the
  engine's code.
- **Versions:** each file has `owl:versionInfo` and an `owl:versionIRI`
  (`https://w3id.org/aige/gbo/0.1.0`). The version is `kGboVersion` in
  `engine/places/OntologyExport.h` (and in `gbo.ttl`). Bump it on every published
  change: minor for additions, major for removals or changed meaning.
- **Permanent IRIs:** `https://w3id.org/aige/gbo` must be registered with
  [w3id.org](https://github.com/perma-id/w3id.org) (a pull request adding an
  `.htaccess` that redirects to where these files are hosted). Until then, the IRIs
  are names that don't resolve yet.
- **Documentation:** the Turtle files carry labels and comments on every term, so
  standard generators (pyLODE, WIDOCO) can build an HTML reference from them.
