#pragma once

#include "ecs/Registry.h"
#include "reflection/Reflection.h"

#include <string>
#include <vector>

namespace aige {

// Serializes every entity's known components to a plain-text, git-diffable
// scene file (technical.md §13): a blank-line-separated block per entity,
// one line per component, `Key=Value` fields. Which components and fields
// exist comes from the component table (gameplay/ComponentTable.h).
void SaveScene(Registry& registry, const std::string& path);

// Clears `registry` and repopulates it from a scene file written by
// SaveScene. Returns false (leaving registry untouched) if the file doesn't
// exist. Unrecognized component lines are skipped rather than failing the
// whole load, so scene files stay loadable across small engine changes.
bool LoadScene(Registry& registry, const std::string& path);

// In-memory equivalents of SaveScene/LoadScene, same text format. Used for
// the editor's Play-mode snapshot: serialize on Play, deserialize on Stop, so
// nothing that happens during a playtest leaks into the edited scene.
// DeserializeScene clears `registry` first, like LoadScene.
std::string SerializeScene(Registry& registry);
void DeserializeScene(Registry& registry, const std::string& text);

// Copies the scene into `to` (cleared first) keeping every entity's
// identifier, so references by id — like an AI proposal's — mean the same
// entity in both. `to` should be a fresh registry.
void CloneScene(Registry& from, Registry& to);

// The same format for a chosen subset of entities, and loading entities into
// a registry without clearing it (returns them in file order). Entity
// references to entities outside the subset are written as `none`. Used for
// prefabs (one entity per file).
std::string SerializeEntities(Registry& registry, const std::vector<Entity>& entities);
std::vector<Entity> AppendEntities(Registry& registry, const std::string& text);

// Project settings (every registered settings section), one line per
// section in the same `key=value` style. LoadSettings returns false if the
// file doesn't exist (settings keep their defaults); unknown sections or
// fields and invalid values are skipped.
void SaveSettings(const ReflectionRegistry& reflection, const std::string& path);
bool LoadSettings(const ReflectionRegistry& reflection, const std::string& path);

} // namespace aige
