#pragma once

#include "assets/AssetDatabase.h"
#include "assets/Catalog.h"
#include "assets/Prefab.h"
#include "ecs/Registry.h"
#include "editing/Autotile.h"
#include "editing/EditHistory.h"
#include "gameplay/Isometric.h"
#include "places/Ontology.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <utility>

namespace aige {

// Putting a solved layout into the scene (development_plan.md 7.7).
//
//   PlanPlaceAssets   each thing the layout needs (a cottage, a fence piece,
//                     a cabbage, grass) gets an asset: the catalog's best fit
//                     (RetrieveForSlot), made a prefab if it isn't one yet;
//                     with nothing that fits, a placeholder (a plain shape in
//                     a colour per role, under assets/generated/placeholders/)
//                     so the place is whole and the Result lists what to replace
//   BuildPlaceScene   the ground into a tile layer of its own ("place:<id>"),
//                     fitted by the autotiler; buildings, props, decals and
//                     fence pieces as prefab instances tagged with PlaceNode;
//                     a building blocks its footprint. A rebuild replaces the
//                     place's previous build and nothing else; one undo step
//   VerifyPlaceScene  the scene against the layout: every object there, at its
//                     cell; the ground whole; every door reachable from the
//                     entrances over the scene's real collision; no two solid
//                     things on one cell; placeholders counted
//   RepairPlaceScene  what Verify found, fixed where it can be: a missing
//                     object placed with a placeholder, an adaptable thing (not
//                     a building) blocking a door or overlapping one removed

struct PlaceSceneOptions {
    std::string place;          // the build's id
    int originX = 0, originY = 0; // the site's corner in the world, in cells
    IsoParams iso;
    bool isoGrid = true;
};

// Where a site of `side` cells can go without covering what's in the scene:
// beside it (to its right, `gap` cells off), or at 0,0 in an empty scene. The
// place's own earlier build doesn't count.
std::pair<int, int> FreeOrigin(Registry& registry, const std::string& place, int side, int gap = 8);

// {"needs": [{key, role, concepts, layer, w, h, uses, prefab, entry, source,
//   grade, fit, licence, why}], "terrains": {"grass": {terrain, source}},
//   "pieces": {...}}. Writes the prefabs (and placeholder images) it needs.
nlohmann::json PlanPlaceAssets(const Ontology& ontology, const nlohmann::json& spec, const nlohmann::json& solved,
                               const Catalog& catalog, PrefabLibrary& prefabs, const AssetDatabase& assets,
                               const TerrainSet& terrains, const PlaceSceneOptions& options);

// Builds it; returns the undo step (to push once verified and repaired) and
// fills `report` (cells per terrain, objects per layer, what couldn't be placed).
std::unique_ptr<EditCommand> BuildPlaceScene(Registry& registry, PrefabLibrary& prefabs, const nlohmann::json& solved,
                                             const nlohmann::json& assetPlan, const TerrainSet& terrains,
                                             const PlaceSceneOptions& options, nlohmann::json* report);

// [{id, what, ok, detail, items?}] and the cells or objects at fault.
nlohmann::json VerifyPlaceScene(Registry& registry, const nlohmann::json& solved, const nlohmann::json& assetPlan,
                                const PlaceSceneOptions& options);

// What it did: [{what, id, how}].
nlohmann::json RepairPlaceScene(Registry& registry, PrefabLibrary& prefabs, const nlohmann::json& solved,
                                const nlohmann::json& assetPlan, const nlohmann::json& verify, const PlaceSceneOptions& options);

// A rebuild of a place as one undo step: Undo takes the new build out and puts
// the previous one back (from its text), Redo the other way.
class RebuildPlaceCommand : public EditCommand {
public:
    RebuildPlaceCommand(std::string place, std::string previous) : place_(std::move(place)), previous_(std::move(previous)) {}
    void Undo(Registry& registry) override;
    void Redo(Registry& registry) override;
    std::string Label() const override { return "Build " + place_; }

private:
    std::string place_;
    std::string previous_; // the build before this one (empty: none)
    std::string built_;    // this build, while undone
};

} // namespace aige
