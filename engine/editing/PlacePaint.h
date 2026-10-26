#pragma once

#include "ecs/Registry.h"
#include "editing/PlaceScene.h"

#include <nlohmann/json.hpp>

namespace aige {

class Ontology;
class PlaceBuild;

// Painted places (docs/build-fixes.md, the "Painted" build method): a place, or each level of it, is
// one painting, a backdrop (as in Baldur's Gate or Pillars of Eternity), painted from the plan's words
// by an image model that paints big pictures from words (Mistral's FLUX); the plan and the painting's
// map (the vision model's reading of it) supply what the player walks on, the doors and the anchors.

// The bodies for POST /places/backdrop: one per level (a place of the ontology's class Level), else
// one for the place; each with the place, its look and story, where each zone, building and kind of
// thing lies (compass words from the plan's coordinates) and the anchors to find (`anchors`: the
// story's, [{id, about}]). Each body also carries its "level" and its "area" (site cells).
nlohmann::json BackdropRequests(const PlaceBuild& build, const Ontology* ontology, const nlohmann::json& anchors);

// The bodies for POST /places/paint-blockout (the Painted method as the app runs it): the plan's
// isometric block-out (PlaceBlockout) rendered and saved in the place's folder (blockout[-<level>].png,
// and its colour key, .legend.json: each kind in the ontology's words), with the place, its story and
// its look; the chosen Paint Provider model paints it into painting[-<level>].png. One per level, else
// one. Each: {path, body, level, area, blockout, legend} (for PlaceBuild::Paint).
nlohmann::json BlockoutPaintRequests(const PlaceBuild& build, const Ontology* ontology, int width = 2816);

// The scene from the backdrops ([{level, area: [x0, y0, x1, y1], backdrop, size: [w, h], map}],
// PlaceBuild::Backdrops): each painting at the back of its area, an invisible walk layer (what its map
// says stands there is blocked, the dark round it too), each tall thing cut out of it as an occluder
// (characters walk behind it), and markers at the anchors, the doors and the way in ("arrive").
// Returns what was made (cells open and blocked, occluders, markers).
nlohmann::json BuildBackdropScene(Registry& registry, const PlaceBuild& build, const nlohmann::json& backdrops, const PlaceSceneOptions& options);

} // namespace aige
