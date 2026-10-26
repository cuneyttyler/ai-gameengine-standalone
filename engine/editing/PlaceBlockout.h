#pragma once

#include "renderer2d/ImageData.h"

#include <nlohmann/json.hpp>

namespace aige {

class Ontology;

// The plan drawn in the game's isometric view, without any art (docs/build-fixes.md, the "Painted"
// build method): each terrain a flat colour, each building a box with a roof and its door, trees and
// bushes as blobs, rocks, props as small boxes, fences and walls as strips along their cell edges. It
// is what an image model repaints (FLUX.3 Image Klein, …): the layout is exactly the plan's, the look
// comes from the prompt (the story, the place's descriptions).
//
// Colour-coded: each kind of building and thing in the plan (its slot: house, warehouse, workshop,
// well, stall…) has a colour of its own, strong and named, from a set chosen to tell apart; plants
// keep their natural greens; the ground muted natural colours. `legend` (if given) gets one entry per
// colour: {colour (its name), hex, shape, kind, what (the ontology's words for it), count}, for the
// prompt to say what each colour stands for. Works for any place and any prefab: the colours follow
// whatever the plan holds.
//
// [x0, x1) x [y0, y1): the site cells drawn (OccupiedBounds); `width`: the image's width in pixels
// (the height follows from the view). Indoors (walls of rooms on void or rock), walls are drawn low,
// as a cutaway, so the rooms show.
ImageData RenderPlanBlockout(const nlohmann::json& solved, int x0, int y0, int x1, int y1, int width, const Ontology* ontology = nullptr,
                             nlohmann::json* legend = nullptr);

} // namespace aige
