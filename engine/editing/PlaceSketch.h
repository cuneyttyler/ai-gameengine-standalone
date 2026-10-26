#pragma once

#include <nlohmann/json.hpp>

namespace aige {

class Ontology;
class PlaceBuild;

// The place sketched first, as a concept artist would (docs/build-fixes.md): one painting of the whole
// place (one per level, for a place of levels), drawn by Mistral's image model from the plan's words
// alone (its zones, every kind of thing in it in the ontology's words, the look and mood). The
// painting decides how things look, not where they stand. The vision model finds each kind of thing
// in it; the crop is the reference its own sprite is drawn from, by the project's art provider.

// The bodies for POST /assets/sketch, from the place's plan and its asset plan (PlanPlaceAssets):
// each with the place, its look and story, its zones, and its items ({key, type, drawn, count}).
nlohmann::json SketchRequests(const PlaceBuild& build, const nlohmann::json& assetPlan, const Ontology* ontology);

} // namespace aige
