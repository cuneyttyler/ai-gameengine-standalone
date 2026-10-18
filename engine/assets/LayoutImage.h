#pragma once

#include "renderer2d/ImageData.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>

namespace aige {

// A solved layout (aige.solved/1) drawn as a map (development_plan.md 7.6):
// what the Build panel's ③ Plan step shows, and places/<id>/layout.png. It's
// a picture of the plan, top-down whatever the game's grid: terrain, roads,
// water, zones with their names, parcels, gardens and beds, buildings with
// roofs and doors, trees, flowers and crops, fences and walls.

struct LayoutImageOptions {
    int cellPixels = 0;      // 0: chosen so the image is about `targetSize` wide
    int targetSize = 1200;
    std::string title;       // drawn along the bottom, e.g. "Riverford: 82 buildings in 4 zones"
    std::map<std::string, std::string> names; // region id -> its label (zones)
    std::map<std::string, std::string> modules; // Spec node id -> module (roof colours by kind)
    std::map<std::string, std::string> moduleNames; // module id -> its name ("House plot"), for the legend
    // A legend down the right side: what each colour and mark means, for
    // what this map shows (each zone by name, each kind of building, ...).
    bool legend = true;
};

ImageData RenderLayoutImage(const nlohmann::json& solved, const LayoutImageOptions& options = {});

} // namespace aige
