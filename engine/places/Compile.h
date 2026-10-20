#pragma once

#include "places/Ontology.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace aige {

// Compile (development_plan.md 7.6, place-schema.md §3.3): the Spec becomes a
// PLL document (aige.ir/1) the solver can solve.
//
// Every `repeat` is written out as instances (28 cottages are 28 plots, each
// with its own statements) and every `share` child goes to exactly that share
// of its parent's instances, picked by a seeded hash. Each module's
// statements come from its compile template: the default one for its level
// and class (below), unless its module file gives its own.
//
//   root (site)      the site region, its ground fill, entrances, the main
//                    roads (crossing for a town), a river, walls with gates
//                    where roads cross, and the partition into zones
//   zone             its street network (the zone's street style, or the
//                    settlement's layout mix), its parcels by subdivision
//                    along the streets; nature zones scatter their plants
//   parcel           its primary object at the front, facing the street;
//                    its features as regions related to it; its surface
//   feature          fences and hedges along edges, beds and fields in rows,
//                    orchards and graves on a grid, plants scattered
//   relations        each module's default relations, with self, parent,
//                    parent.primary, parent.street and parent.door resolved
//                    to the instance's statements
//   checks           parcels placed, primaries filled, fences closed, every
//                    door reachable, footprint density per ward

struct CompileOptions {
    uint64_t seed = 7;
    int side = 0;         // the site's side in cells; 0: from the root's area
    bool isoGrid = true;
    // A slot's footprint in cells (from the asset plan); none: the default
    // for its role (a building takes ~60% x 45% of its plot, a prop one cell).
    std::function<std::optional<std::pair<int, int>>(const std::string& module, const std::string& slot)> footprint;
    // More land for a zone than its plan says (zone id -> factor): the fitting
    // rounds give it to a zone that couldn't place all its parcels.
    std::map<std::string, double> zoneScale;
};

nlohmann::json CompileSpec(const Ontology& ontology, const nlohmann::json& spec, const CompileOptions& options = {});

} // namespace aige
