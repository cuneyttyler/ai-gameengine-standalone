#pragma once

#include "assets/Project.h"
#include "ecs/Registry.h"
#include "gameplay/Isometric.h"

#include <nlohmann/json.hpp>

#include <string>

namespace aige {

class Ontology;
class PlaceBuild;

// The vision checks of a place's build (docs/build-fixes.md): the bodies for the AI service's
// POST /places/inspect, one per step, each with the images it looks at written into the build's
// folder. Every check identifies the elements (zones, or each kind of thing), judges their places
// in the whole and against their neighbours, their visual fit and their quality, and scores them.

// The goal for a kind of place (buildtypes.json "look": its reference games and what it needs).
nlohmann::json LookFor(const std::string& buildType);

// ② Layout: the zones alone over the ground, with their walls, zones and rooms labelled
// (inspect-layout.png); the zones planned, each with what its module is (with `ontology`).
nlohmann::json InspectLayoutRequest(const PlaceBuild& build, const Ontology* ontology = nullptr);

// ③ Plan: the solved plan drawn with every kind of thing coded (inspect-plan.png), with how many of
// each there are and in which zones.
nlohmann::json InspectPlanRequest(const PlaceBuild& build, const Ontology* ontology = nullptr);

// ⑤ Result: the built scene drawn as the game draws it, in windows of at most `window` cells a side
// (inspect-build-<n>.png), each with what was placed in it. `originX/Y`: the site's corner.
nlohmann::json InspectBuildRequest(Registry& registry, const Project& project, const IsoParams& iso, const PlaceBuild& build,
                                   int originX, int originY, int window = 56, const Ontology* ontology = nullptr);

// A check's answer in a line, for logs: its scores and how many errors and warnings.
std::string InspectSummary(const nlohmann::json& answer);

} // namespace aige
