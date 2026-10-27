#pragma once

#include "ecs/Registry.h"
#include "gameplay/Isometric.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aige {

class Ontology;

// Painted places (docs/places/painted-places.md): a place painted whole from its plan's block-out, and the marks
// the user makes over the painting (what can be walked on, and the occluders drawn again over a character behind
// them). Files in the place's folder (places/<id>/): blockout.png, blockout-ids.png, blockout.legend.json,
// painting.png, painting.json, marks.json and marks/<occluder>.png.

// The block-out's view: cell (x, y) at height `up` lands at painting pixel Pixel(x, y, up); a pixel on the ground
// is at Cell(px, py).
struct PaintView {
    int x0 = 0, y0 = 0, x1 = 1, y1 = 1;
    double tw = 64.0, th = 32.0, top = 0.0;

    static PaintView From(const nlohmann::json& view);
    std::pair<double, double> Pixel(double x, double y, double up = 0.0) const;
    std::pair<double, double> Cell(double px, double py) const;
};

// Whether a plan object is in the painting: buildings, plants, rocks, décor and anything else of an environmental
// role (its role's affordance, or the nearest broader role's). Things the player uses, items and characters aren't:
// they're sprites over it, made by ⑥ Assets.
bool PaintedObject(const Ontology* ontology, const nlohmann::json& object);
// The ids of the plan's objects that are in the painting.
std::set<std::string> PaintedObjectIds(const Ontology* ontology, const nlohmann::json& solved);

// ④ Paint, before the provider paints: the block-out of the painted things (blockout.png, its id map and legend)
// drawn `width` px across and painting.json started ({size, view, objects: the ids in the id map's order}).
// Returns painting.json.
nlohmann::json PrepareBlockout(const nlohmann::json& solved, const Ontology* ontology, const std::filesystem::path& folder, int width = 2560);

// painting.json, or null.
nlohmann::json LoadPainting(const std::filesystem::path& folder);
// The painting arrived: its file, provider, prompt and time noted in painting.json ("paintedAt": now).
void PaintingMade(const std::filesystem::path& folder, const nlohmann::json& answer);

// ⑤ Mark, pre-filled from the plan (painted-places.md §3): walkable cells from the solid ground and the things that
// block, an occluder for each tall thing (its pixels in the id map, its base along its footprint's front edges) and
// for each straight run of a wall or fence. An occluder the user edited (`edited`) is kept as it is, unless
// `replaceEdited`. Writes marks.json and the masks; returns marks.json.
nlohmann::json PrefillMarks(const nlohmann::json& solved, const Ontology* ontology, const std::filesystem::path& folder, bool replaceEdited = false);

// marks.json, or null.
nlohmann::json LoadMarks(const std::filesystem::path& folder);
// What the editor sends: {walk: {rows}, occluders: [{id, name, object?, box, base, edited, prefilled, png?: base64}],
// removed?: [ids]}. The masks sent are written; removed occluders' masks deleted. "" or why not.
std::string SaveMarks(const std::filesystem::path& folder, const nlohmann::json& marks);
// ⑤ Mark's Continue: the marks confirmed by the user (until the place is painted again or pre-filled anew).
std::string ConfirmMarks(const std::filesystem::path& folder);
// For the build card and the AI: {painted, paintedAt, marked, confirmed, occluders, edited, blocked, walkable, stale}.
nlohmann::json MarksSummary(const std::filesystem::path& folder);

// What looks wrong in the marks, in words (the AI panel's check; the user decides): a building or tall thing with no
// occluder, an occluder with no base line or an empty region, a building whose footprint can be walked through.
std::vector<std::string> CheckMarks(const nlohmann::json& solved, const Ontology* ontology, const std::filesystem::path& folder);

// The painted place in the scene (painted-places.md §5): the painting at the back and each occluder as strips half a
// tile wide, each drawn at the depth of its base line under it. `folder` is the place's folder, `relative` the same
// relative to the project ("places/<id>"). Each entity gets PlaceNode(place, "", "painting#…"). Returns
// {backdrop, occluders, strips} or {error}.
nlohmann::json AddPaintingEntities(Registry& registry, const std::filesystem::path& folder, const std::string& relative,
                                   const std::string& place, int originX, int originY, const IsoParams& iso);
// The walkable cells from marks.json: [x][y] site cells, true where blocked; empty without marks.
std::vector<std::vector<bool>> MarkedBlocked(const nlohmann::json& marks, int w, int h);

// Base64 (standard alphabet, padding optional) to bytes.
std::vector<unsigned char> DecodeBase64(const std::string& text);

} // namespace aige
