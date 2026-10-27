#pragma once

#include <nlohmann/json.hpp>

namespace aige {

class Ontology;

// The plan's arrangement (docs/dungeon.md §0, §0b): for each kind of placed thing (its role), what it's for in this
// place (purpose), how big it is (footprint), its relations to other kinds (the ontology's relation types: set-in,
// mounted-on, against, leads-to, lights, part-of, used-with, debris-of, and the spatial ones), where it stands and
// which way it faces. The LLM writes it in the plan step (/places/arrange); the ontology gives every default.
//
//   {"kinds": {"<role>": {"purpose": "...", "footprint": {"w": 2, "h": 3}, "position": "wall",
//                         "faces": "away-from-wall", "relations": [{"type": "leads-to", "to": "level"}]}}}

// The kinds a compiled layout places (its place and scatter statements), each with the ontology's defaults and how
// many there are: what the LLM is asked to arrange.
nlohmann::json ArrangementKinds(const Ontology& ontology, const nlohmann::json& ir);

// The arrangement from the ontology alone (each role's look: purpose, relations, faces).
nlohmann::json DefaultArrangement(const Ontology& ontology, const nlohmann::json& ir);

// Each place and scatter statement given its kind's arrangement (missing keys from the ontology): its footprint, its
// position (the arrangement's, else its relations' — a thing set in, mounted on, against or leading out of a wall
// stands at a wall, not where its module put it), its facing rule ("faces") and its purpose and relations (kept for
// the art and the scene). What the solver places by.
void ApplyArrangement(const Ontology& ontology, nlohmann::json& ir, const nlohmann::json& arrangement);

// Each solved object turned (after solving, where its room, walls, doors and paths are known) by its first relation whose
// type has a facing (aligned: along the nearest road, wall, ground or thing it names within the type's reach, on its
// right-hand side; against, mounted-on: away from it; used-with: towards it; docs/places/ontology-in-use.md), else by
// its statement's facing rule: away-from-wall (back to its room's nearest edge), room (towards its room's middle), door (towards
// its room's nearest door), path (down the long way of its ground, on the right-hand side), front (towards the
// nearest thing of the kind it's used with). Marks each "faced" (the relation or the rule). Returns how many were turned.
int FaceObjects(const Ontology& ontology, nlohmann::json& solved, const nlohmann::json& ir);

} // namespace aige
