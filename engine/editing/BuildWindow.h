#pragma once

#include "assets/Catalog.h"
#include "places/Brief.h"
#include "places/Ontology.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace aige {

// The confirmation window (development_plan.md 7.4, place-interaction.md A3),
// generated from the build type's Brief schema: round 1 (the Request step:
// size with numbers, layout, components, style, assumptions, the open
// questions) and round 2 (the Assets step: densities, asked module fields and
// the asset plan). The editor's Build panel draws it; edits come back as
// (path, value) and are applied to the Brief.

// One piece of art the build will need: a slot of a module the Brief turns on.
struct AssetNeed {
    std::string component;  // the Brief component it comes from
    std::string module;     // the module whose slot it is
    std::string slot;
    std::string role;       // "crop|flower": either
    std::vector<std::string> concepts;
    bool essential = true;
    bool joined = false;    // a piece set (fences, walls, hedges)
    float minCells = 0.0f, maxCells = 1e9f;
};

// The needs of every enabled component: its own slots and those of the
// modules it typically contains (two levels down), one per role and concepts.
std::vector<AssetNeed> AssetNeeds(const Ontology& ontology, const nlohmann::json& brief);

// The asset plan (place-assets.md §6): each need, the best project candidates,
// and the proposed source. `auto` takes the project's art when some fits,
// else proposes generating it; an explicit source is kept.
nlohmann::json AssetPlan(const Ontology& ontology, const Catalog& catalog, const nlohmann::json& brief, bool isoGrid);

// The window for `round` (1 or 2). `merge` (optional) marks disputed and
// low-confidence fields; `catalog` (optional) adds thumbnails and, in round 2,
// the asset plan. Validation problems are included.
nlohmann::json BriefWindow(const Ontology& ontology, const nlohmann::json& brief, const BriefMerge* merge,
                           const Catalog* catalog, bool isoGrid, int round);

// Applies one edit from the window: sets the value, keeps the scale class and
// numbers consistent, and drops a disabled component's settings.
void ApplyWindowEdit(const Ontology& ontology, nlohmann::json& brief, const std::string& path, const nlohmann::json& value);

// The Build panel's choices (structured input beside the request's words), from
// the ontology: per build type its size classes (with the unit's numbers), its
// substantial settings ({path, label, values} or {path, label, type: count|bool,
// min, max}: walls, layout, style, the root's asked fields as "root.<field>"),
// its features, and its main parts (zones, networks, distinctive parcels).
nlohmann::json BuildChoices(const Ontology& ontology);

// Choices are {Brief path: value} ("buildType", "scale.class", "scale.buildings",
// "walled", "shape.layout", "style.theme", "root.levels", "components.moat.enabled",
// "features": ["river"]). Applied over an interpreted Brief, they win: a build type
// chosen drops what isn't the type's (its components, its scale, its root settings).
nlohmann::json ApplyChoices(const Ontology& ontology, nlohmann::json brief, const nlohmann::json& choices);

// A request in words for choices given without any ("a large castle with a moat").
std::string ChoicesQuery(const Ontology& ontology, const nlohmann::json& choices);

} // namespace aige
