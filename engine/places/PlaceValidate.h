#pragma once

#include "places/Expr.h"
#include "places/Ontology.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace aige {

// Validators for the documents of the building procedure
// (docs/place-schema.md, docs/layout-ir.md; development_plan.md 7.1):
//   Brief  aige.brief/1   the confirmed request
//   Spec   aige.spec/1    the plan tree: quantity and quality fields per part
//   PLL    aige.ir/1      the Place Layout Language
// Each returns every problem it finds, located (a node id, a statement id,
// a Brief path), so a failing step can be repaired precisely.

struct PlaceProblem {
    std::string where;    // "w-old/plots", "statement g14.fence", "components.garden.params.kind"
    std::string message;
    bool warning = false; // reported, but the document is still usable
};

// The value a field holds, read for expressions: numbers and ranges become
// intervals, a density word its number (the field's own scale), a size an
// object {w, h}, an enum a string, an enumSet a list, a mix an object of
// weights, a bool a truth value. Missing: the field's default, else Unknown.
places::Value FieldValueOf(const FieldDef& field, const nlohmann::json& value);

// Why `value` isn't valid for `field` (type, unit, range, vocabulary,
// mix weights), or empty.
std::string CheckFieldValue(const Ontology& ontology, const FieldDef& field, const nlohmann::json& value);

std::vector<PlaceProblem> ValidateBrief(const Ontology& ontology, const nlohmann::json& brief);

// Draft: fields and children may still be missing (the lower levels are
// filled in later); minimum counts, counts tied to a field and constraints
// over children or descendants aren't enforced. Final: the Spec is what
// will be compiled.
enum class SpecStage { Draft, Final };

// What the asset plan decided for a node's slot ("project", "generate",
// "missing", ...); empty when there's no plan yet. Used by asset(slot).
using AssetPlanLookup = std::function<std::string(const std::string& node, const std::string& slot)>;

std::vector<PlaceProblem> ValidateSpec(const Ontology& ontology, const nlohmann::json& spec, SpecStage stage,
                                       const std::string& buildType = {}, const AssetPlanLookup& assets = {});

// Static checks of a PLL document: known statements with their required
// fields, unique ids, every reference resolving (with its selectors), each
// reference to something solved earlier (relations aside), relation types
// and their member counts, check predicates, and sub-areas that can't fit
// their parent.
std::vector<PlaceProblem> ValidateLayout(const Ontology& ontology, const nlohmann::json& ir);

// The PLL statement kinds (docs/layout-ir.md §5).
const std::vector<std::string>& LayoutOps();

} // namespace aige
