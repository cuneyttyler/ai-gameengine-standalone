#pragma once

#include "ecs/Registry.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace aige {

class AssetDatabase;
class PrefabLibrary;
class ReflectionRegistry;
struct Project;

struct ContextOptions {
    std::optional<Entity> selection;
    std::vector<Entity> selected;  // everything selected (a box selection), the first being `selection`
    std::string focus;             // "Skeleton.Health.max" when asked from an Inspector field
    std::string scene;             // the open scene ("scenes/main.aige"); the others are listed too (5.3)
    size_t maxEntities = 200;      // keeps prompts bounded in big scenes
};

// What the AI service is told about the open project (ai_service/app/models.py
// EditorContext): non-tile entities with their current component values
// (registry JSON form: enums by name, references by id), a tile summary,
// prefabs with their values, assets, scripts and project settings. Entity
// ids are only meaningful for this request — proposals are validated against
// the live scene before they're shown.
nlohmann::json BuildEditorContext(Registry& registry, const ReflectionRegistry& reflection, PrefabLibrary& prefabs,
                                  const AssetDatabase& assets, const Project& project,
                                  const ContextOptions& options = {});

} // namespace aige
