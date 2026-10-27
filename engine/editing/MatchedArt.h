#pragma once

#include "renderer2d/ImageData.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace aige {

class AssetDatabase;
class Ontology;
struct Project;

// Matched art (docs/places/matched-art.md): a place's art made to fit together, by any art provider. Each need
// of the asset plan is drawn as a box (its footprint and height, in the iso projection, on a flat key colour),
// a few boxes to a sheet; the first sheet is the style sheet, whose painted pieces are the style every other
// piece is made in. The provider paints the sheets (BFL) or each piece from its box (PixelLab, Mistral); the
// pieces are cut, fitted to the grid, their palette pulled to the style sheet's, checked, and written as the
// place's assets.

// The key colour every sheet stands on, and a one-piece provider paints its piece on (where it can't leave
// the background clear).
inline constexpr int kMatchedKey[3] = {255, 0, 255};

// The sheets for a place's needs, written into `dir`: <name>.png (the block-out on the key colour) and
// <name>-ids.png (its id map), and sheets.json. The first sheet ("style") holds the `styleCount` needs that show
// the place best (its biggest kinds of building, then its most used things); the others ("things-N") the rest,
// `perSheet` to a sheet; the last ("ground") a patch of each ground. Returns sheets.json:
// {sheets: [{name, kind: style | things | ground, png, ids, size, tw, th, pieces: [{key, role, layer, words, w,
// h, id, slug, box, left, right, front, stand}], grounds: [{terrain, words, slug, cell}]}]}; points in the
// sheet's pixels.
nlohmann::json MatchedArtSheets(const nlohmann::json& solved, const nlohmann::json& assetPlan, const Ontology* ontology,
                                const std::filesystem::path& dir, int perSheet = 6, int styleCount = 6);

// The art cut from what the provider made, in `dir`: each sheet's painting (<name>-painted.png), else each
// piece's own image (pieces/<slug>.png: clear or on the key colour). Fitted to the grid (as wide as the sheet
// had it, standing where the builder stands it: drawnToGrid), the colours of every piece but the style sheet's
// pulled towards the style sheet's palette, checked against it, and written as assets/matched/<place>/... with
// their catalog fields. Returns the report: {pieces: [{key, role, path, sheet, cells, size, palette, fill,
// flagged}], grounds: [{terrain, path}], skipped: [{key, why}], palette: [hex...]}.
nlohmann::json CutMatchedArt(AssetDatabase& assets, const Project& project, const nlohmann::json& sheets, const std::filesystem::path& dir,
                             const Ontology* ontology, const std::string& place);

// The pieces side by side on a grey board: what was made, at a glance.
ImageData MatchedArtBoard(const Project& project, const nlohmann::json& report, int width = 1800);

} // namespace aige
