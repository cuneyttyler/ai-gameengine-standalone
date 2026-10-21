#pragma once

#include "ecs/Registry.h"

#include "gameplay/TileLayer.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace aige {

class PrefabLibrary;
class ReflectionRegistry;
struct Project;

// One reviewable change from an AI proposal, resolved against the live
// project: what it touches, the value before and after, and why it can't be
// applied (if it can't). Rendered by the Diff Review panel.
struct ChangeItem {
    nlohmann::json op;      // the operation as proposed (or as edited in review)
    std::string kind;       // field | add_component | remove_component | prefab_field | setting
                            // | script_property | command
    std::string target;     // human-readable: "Skeleton #42 · Health.max"
    nlohmann::json before;  // current value, when there is one
    nlohmann::json after;   // proposed value
    nlohmann::json field;   // field metadata (type/options/min/max) for inline editing, if applicable
    std::string detail;     // longer content (a script's source, a command's arguments)
    bool touchesFiles = false; // writes outside the scene (prefab, script, settings): not covered by scene undo
    std::string error;
};

struct ResolvedProposal {
    std::string summary;
    std::string notes;
    std::vector<ChangeItem> items;

    bool AllValid() const;
};

// The editor-side half of the defense in depth (technical.md §7.5): checks
// every operation against the live registry and scene (types, constraints,
// that referenced entities/prefabs/components still exist) and computes
// before/after values for review. Never modifies anything.
ResolvedProposal ResolveProposal(const nlohmann::json& proposal, Registry& registry,
                                 const ReflectionRegistry& reflection, PrefabLibrary& prefabs, const Project& project);

struct ApplyOutcome {
    int applied = 0;
    std::vector<std::string> errors; // per failed item, prefixed with its target
    bool settingsChanged = false;
    bool prefabsChanged = false;
    // One per operation: the entity it created (scene.spawn), else entt::null.
    std::vector<Entity> created;
    // Tile-layer changes (terrain.replace on a built place's ground): undone on their own.
    std::vector<TileLayerEdit> layerEdits;
};

// Applies the accepted operations, in order, through the same registry
// setters, component operations, prefab library and commands the manual
// editor uses — an AI change is indistinguishable from a hand-made one. Each
// operation is re-validated at apply time; a failing one is reported and
// skipped, the rest still apply.
ApplyOutcome ApplyOperations(const std::vector<nlohmann::json>& operations, Registry& registry,
                             const ReflectionRegistry& reflection, PrefabLibrary& prefabs, const Project& project);

} // namespace aige
