#pragma once

#include "ecs/Registry.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace aige {

class PrefabLibrary;
class ReflectionRegistry;

// The single write path for field-level edits, shared by every surface: the
// Inspector and applied AI proposals.
// Because both call exactly these functions, an edit made either way
// produces identical project files (product.md §4.3, technical.md §11) —
// enforced by construction, and checked by EditParityTest.
//
// Each validates against the reflection registry, writes, and records the
// change as an override when the entity is a prefab copy. They return why
// not, or nullopt on success.

std::optional<std::string> EditField(const ReflectionRegistry& reflection, Registry& registry, Entity entity,
                                     const std::string& component, const std::string& field,
                                     const nlohmann::json& value);

// Adds a component (and its dependencies) with optional initial field
// values. Re-adding a component a prefab copy had removed restores the
// prefab's values first (needs `prefabs`; may be null otherwise).
std::optional<std::string> EditAddComponent(const ReflectionRegistry& reflection, Registry& registry,
                                            PrefabLibrary* prefabs, Entity entity, const std::string& component,
                                            const nlohmann::json& fields = nlohmann::json::object());

std::optional<std::string> EditRemoveComponent(const ReflectionRegistry& reflection, Registry& registry, Entity entity,
                                               const std::string& component);

// Sets a project setting (Renderer, Camera, ...).
std::optional<std::string> EditSetting(const ReflectionRegistry& reflection, const std::string& section,
                                       const std::string& field, const nlohmann::json& value);

// Sets a script property (number, boolean or text) on the entity's script.
std::optional<std::string> EditScriptProperty(Registry& registry, Entity entity, const std::string& name,
                                              const nlohmann::json& value);

} // namespace aige
