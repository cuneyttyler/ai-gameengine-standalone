#pragma once

#include "places/Ontology.h"
#include "places/PlaceValidate.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace aige {

// Plan: the Brief expanded into the Spec, level by level
// (development_plan.md 7.5, place-schema.md §3.2):
//   1. the root step (AI): the zones, networks and ground, with their fields;
//   2. a zone step per zone (AI): its parcels as templates (repeat, share);
//   3. the lower levels from the modules' expansion rules (no AI): each
//      parcel's typical children by their weights, the Brief's densities and
//      settings applied;
//   4. building counts rebalanced to the Brief's number (no AI);
// each step validated (ValidateSpec), a broken node repaired once by the AI.

// What the AI is told at each step: the module's subgraph and those of what
// it may contain, the Brief's requests for them, the scale and the rules.
nlohmann::json PlanRootContext(const Ontology& ontology, const nlohmann::json& brief);
nlohmann::json PlanZoneContext(const Ontology& ontology, const nlohmann::json& brief, const std::string& zoneModule);

// The Spec from the Brief and the root step's answer: the root node (its
// fields from the Brief, the answer's where the Brief says nothing) and its
// children (ids made unique, the Brief's settings for components applied,
// requested components the answer left out added with their defaults).
nlohmann::json SpecFromRoot(const Ontology& ontology, const nlohmann::json& brief, const nlohmann::json& answer,
                            const std::string& placeId);

// The root's children to plan zone by zone (zone-level modules), in order.
std::vector<std::string> ZoneIds(const Ontology& ontology, const nlohmann::json& spec);

// Puts a zone step's answer into the Spec: its fields and its children.
void ApplyZoneAnswer(const Ontology& ontology, const nlohmann::json& brief, nlohmann::json& spec, const std::string& zoneId,
                     const nlohmann::json& answer);

// The lower levels, from the expansion rules.
void ExpandSpec(const Ontology& ontology, const nlohmann::json& brief, nlohmann::json& spec);

// Fixes that need no AI, before each validation: a mix's or set's values that
// aren't in the vocabulary are matched by their synonyms ("onions") or dropped,
// a mix's weights are rescaled to add up to 1, an unknown enum value falls
// back to the default, and a child counted by a field ("piers") gets as many
// as the field says. Returns what it changed, as warnings.
std::vector<PlaceProblem> TidySpec(const Ontology& ontology, nlohmann::json& spec);

// Fixes the structure and the fields so the Spec follows the module rules,
// without the AI, and returns what it did (as warnings). Run after
// TidySpec, before the final validation:
// - nodes of unknown modules, children a parent can't hold, parts at an
//   outer level, and parts that need something the place can't have (docks
//   without water) or exclude something it has, are left out;
// - numbers and sizes outside their range are clamped; fields that don't
//   apply (their `when` is false) or can't be read are removed (the default
//   applies);
// - too many children of a rule are scaled down, shares adding up to more
//   than 1 scaled to 1;
// - a node breaking one of its module's constraints has the fields the
//   constraint names reset to their defaults.
std::vector<PlaceProblem> ResolveSpec(const Ontology& ontology, const nlohmann::json& brief, nlohmann::json& spec);

// When the AI can't answer (it's off, unreachable, or answers nonsense twice):
// the root step's answer from the Brief alone (each enabled zone component,
// its count from the Brief, the buildings shared by the zones' weights, the
// area by their plots), and a zone step's from its module's child rules (its
// plots shared among its building parcels by their weights).
nlohmann::json FallbackRootAnswer(const Ontology& ontology, const nlohmann::json& brief);
nlohmann::json FallbackZoneAnswer(const Ontology& ontology, const nlohmann::json& zone);

// The share of parents with a component, for a Brief density word
// (common: 60% of house plots have a garden).
double DensityShare(const std::string& word);

// Counts the building parcels the Spec stands for (repeat x share down the
// tree) and, outside the Brief's range, scales each zone's building-parcel
// repeats to its middle; the root's buildings and the zones' plots follow.
// Returns the count after.
int RebalanceBuildings(const Ontology& ontology, const nlohmann::json& brief, nlohmann::json& spec);
int CountBuildings(const Ontology& ontology, const nlohmann::json& spec);

// A node by id (nullptr if none), and its JSON path for patching.
nlohmann::json* SpecNode(nlohmann::json& spec, const std::string& id);

// For the ③ Plan step: one row per root child (module, plots, buildings,
// area, key qualities, rationale) and the totals.
nlohmann::json PlanSummary(const Ontology& ontology, const nlohmann::json& spec);

// A schematic of the zones in the site (a squarified treemap of their areas,
// largest first), in cells: what the plan view shows before the solver
// places anything for real.
nlohmann::json PlanSketch(const Ontology& ontology, const nlohmann::json& spec, int side);

} // namespace aige
