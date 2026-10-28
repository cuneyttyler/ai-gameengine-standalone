#pragma once

#include "assets/AssetDatabase.h"
#include "assets/Catalog.h"
#include "assets/Prefab.h"
#include "assets/Sourcing.h"
#include "ecs/Registry.h"
#include "editing/Autotile.h"
#include "editing/EditHistory.h"
#include "gameplay/Isometric.h"
#include "places/Ontology.h"

#include <nlohmann/json.hpp>

#include <array>

#include <memory>
#include <set>
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
    // Editing after the build (7.8): only these parts are replaced (empty: all of
    // the place), and locked parts never are: a hand-edited house stays as edited.
    std::set<std::string> only;
    std::set<std::string> locked;
    // The ground made by the Art tool painted as one picture, its grounds blended where they
    // meet (place-refine.md §1); else a tile per cell.
    bool paintGround = true;
    // High ground (place-refine.md §10): the wild land rises in low hills, up to this many
    // pixels; the built land, roads, water and fields stay level. 0: flat.
    float hills = 40.0f;
    // The light (place-refine.md §12): "sun" (outdoors: one light from the upper left) or
    // "indoor" (dark, lit by what gives light, walls in the way). Empty: the build type's look.
    std::string lighting;
    // Whether the corridor at a door cell runs along x (its door then stands across it, along
    // y); set by BuildPlaceScene from the layout.
    std::function<bool(int x, int y)> doorAcrossX;
    // The place's colour grade (docs/build-fixes.md), from its mood (SetMoodGrade): the ground
    // painted through it, every thing tinted by it, as a game grades a level as one picture.
    float gradeSaturation = 1.0f, gradeBrightness = 1.0f;
    std::array<float, 3> gradeTint{1.0f, 1.0f, 1.0f};
    // The ground as one picture (Matched art's painted ground plan, docs/places/matched-art.md): top-down, the site's
    // cell (0, 0) at its corner, `groundPictureCellPx` pixels a cell. Its colours are the ground's; the shadows,
    // shore and light are laid over them as on any ground. Empty: the ground from its terrains' textures.
    std::string groundPicture;
    int groundPictureCellPx = 0;
    // A painted place (docs/places/painted-places.md): its folder (places/<id>, absolute). The ground is then the
    // marks' walk layer (invisible, solid where blocked); the things in the painting (`paintedIds`) and the walls and
    // fences aren't placed; the painting and its occluders are added at the end.
    std::string paintedFolder;
    std::set<std::string> paintedIds;
};

// A place's levels (docs/build-fixes.md): its zones of the ontology's class Level (a dungeon's
// levels, a building's floors), each with the cells it covers (site cells) and where its stairs
// stand. Two or more: each becomes a scene of its own; fewer: none (the place is one scene).
struct PlaceLevelArea {
    std::string id;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    std::vector<std::pair<int, int>> stairs;
};
std::vector<PlaceLevelArea> PlaceLevelAreas(const Ontology& ontology, const nlohmann::json& spec, const nlohmann::json& solved);

// The cells the place really covers {x0, y0, x1, y1}: its ground that isn't "void" (an interior's
// building, not the empty site round it); the whole site when it has none.
std::array<int, 4> OccupiedBounds(const nlohmann::json& solved);

// ③ Assets' rows from PlanPlaceAssets' answer: each need, then the ground (each terrain), a
// dungeon's standing parts and the lines' pieces, as rows of the same shape.
nlohmann::json AssetNeedRows(const nlohmann::json& assets);

// The hills' height (PlaceSceneOptions::hills) for a Brief's `root.relief`: flat, gentle, hilly.
float HillsFor(const nlohmann::json& brief);

// The colour grade for a place's mood, from its Brief's style and its story's mood words: a cursed,
// ruined or oppressive place muted, darker and cool; a cheerful one a touch brighter; else none.
void SetMoodGrade(PlaceSceneOptions& options, const nlohmann::json& brief, const nlohmann::json& story);

// Where a site of `side` cells can go without covering what's in the scene:
// beside it (to its right, `gap` cells off), or at 0,0 in an empty scene. The
// place's own earlier build doesn't count.
std::pair<int, int> FreeOrigin(Registry& registry, const std::string& place, int side, int gap = 8);

// How the art is sourced (7.9): the Brief's per-component policies, the user's
// choices per need (places/<id>/assets.json), the library, what was tried.
struct AssetSourcing {
    nlohmann::json brief = nlohmann::json::object();     // components.<id>.assets.source: auto, project, library, web, generate
    nlohmann::json overrides = nlohmann::json::object(); // {need key: {source?, exclude?}}
    SourcingSettings settings; // web search on or off, the licences allowed
    bool canGenerate = false;  // the AI is there to make art
    std::map<std::string, Tried> tried;
    Catalog library;           // LoadLibrary, and the user's library (UserLibrary::AsCatalog)
    // Copies the user's library item (its id, after "user:") into the project; its project path.
    std::function<std::string(const std::string& item)> importLibrary;
    // A build from the AI Planner: the plan's art strategy (settings.defaultSource) is used for every
    // part, whatever a component of the place's Brief says (only "placeholder" stands).
    bool strategyRules = false;
    // Found art (the project's, a library's, the web's) is looked at by the vision model before it's
    // used (docs/build-fixes.md): a need whose art isn't yet approved for this place is pending a
    // "check"; refused art is skipped next time (Tried::refused), down to the Art tool.
    bool checkFound = false;
    // Needs the place's sketch painted (PlaceBuild::SketchCrops): made by the Art tool from their crop,
    // so every piece keeps the painting's look; found art only if that fails.
    std::set<std::string> sketched;
    // Each building type's interior chosen in the Build panel (places/<id>/interiors.json, {role: cutaway | scene | none}).
    nlohmann::json interiors = nlohmann::json::object();
};

// {"needs": [{key, role, concepts, layer, w, h, uses, importance, styleCritical,
//   decision: {step, action, reason}, prefab, entry, source, grade, fit, licence,
//   why}], "terrains": {"grass": {terrain, source}}, "pieces": {...},
//   "pending": [{key, action (generate | web), role, concepts, layer, w, h, query,
//   description}]}. Writes the prefabs (and placeholder images) it needs; a need
// still to be generated or fetched shows a placeholder meanwhile.
nlohmann::json PlanPlaceAssets(const Ontology& ontology, const nlohmann::json& spec, const nlohmann::json& solved,
                               const Catalog& catalog, PrefabLibrary& prefabs, const AssetDatabase& assets,
                               const TerrainSet& terrains, const PlaceSceneOptions& options, const AssetSourcing* sourcing = nullptr);

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
// Scenes built before roles said what's solid (roles.json "solid"): each placed thing (its PlaceNode) blocks its whole
// footprint when its role is solid and nothing when it isn't, read from its build's plan (places/<id>/spec.json and
// layout.solved.json). Buildings are left as they are. Returns how many it changed.
int RepairPlaceSolidity(Registry& registry, const Project& project, const Ontology& ontology, bool topDown);
// A thing blocking several cells (an isometric building) blocks no further than its art reaches: its walls' width near
// the ground, either side of the point it stands on, in cells (its eaves left out). Only ever shrinks its Obstacle.
// Returns how many it changed.
int FitObstaclesToArt(Registry& registry, const Project& project, const IsoParams& iso);

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
