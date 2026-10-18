#pragma once

#include "places/Ontology.h"

#include <string>

namespace aige {

// The place ontology as Semantic Web documents, for publishing
// (ontology/README.md). The core vocabulary (ontology/gbo.ttl) is written by
// hand; these two are generated from the loaded JSON library, so there is
// one source:
//
//   gbo-library.ttl  OWL + SKOS: modules (punned as classes under their
//                    level), module classes, fields, slots, child rules (also
//                    as OWL restrictions on gbo:hasPart), relation uses, site
//                    preferences, build types and scale classes, concept
//                    schemes, roles, relation types, design rules
//   gbo-shapes.ttl   SHACL: shapes for the library itself (every rule has a
//                    source, every field a type, ...) and one shape per module
//                    for built places described in GBO terms
//
// Both are deterministic (sorted), so a test can compare them with the
// published files.

inline constexpr const char* kGboVersion = "0.1.0";
inline constexpr const char* kGboNamespace = "https://w3id.org/aige/gbo";

std::string ExportLibraryTurtle(const Ontology& ontology);
std::string ExportShapesTurtle(const Ontology& ontology);

// "house-plot" -> "HousePlot".
std::string GboClassName(const std::string& id);

} // namespace aige
