#pragma once

#include "ecs/Registry.h"

#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace aige {

// A hand-written description of every component an editor user can see,
// add, remove and edit, and that scene files store: the single place
// component fields are listed (development_plan.md milestone 1.4). SceneIO,
// the Inspector and component authoring are all driven from it, so adding a
// component means adding one table entry.
//
// This is deliberately the seed of Phase 2's Reflection & Command Registry
// (technical.md §7): same shape (types, fields, typed get/set), written by
// hand for now instead of generated from registration macros.

enum class FieldType {
    Float,
    Int,
    Bool,
    String,
    Enum,      // stored as the option index; serialized by option name
    EntityRef, // another entity; serialized by its index in the scene file
    Texture,   // project-relative texture asset path ("" = built-in atlas)
    Script,    // project-relative .lua path
    Audio,     // project-relative audio asset path ("" = silent)
};

using FieldValue = std::variant<float, int, bool, std::string, Entity>;

struct FieldDesc {
    std::string name;
    FieldType type = FieldType::Float;
    std::vector<std::string> options; // Enum only
    std::function<FieldValue(Registry&, Entity)> get;
    std::function<void(Registry&, Entity, const FieldValue&)> set;
};

struct ComponentDesc {
    std::string name;
    std::vector<FieldDesc> fields;
    // Users can add/remove it in the Inspector (false for structural
    // components like Tile, whose identity depends on them).
    bool userEditable = true;
    // Components this one needs; adding it adds them too, and they can't be
    // removed while it's present.
    std::vector<std::string> dependencies;

    std::function<bool(Registry&, Entity)> has;
    std::function<void(Registry&, Entity)> add; // with defaults
    std::function<void(Registry&, Entity)> remove;
    // True when the values are computed every frame from other components
    // (ScreenTransform2D of a character or tile), so edits wouldn't stick.
    std::function<bool(Registry&, Entity)> derived;

    const FieldDesc* Field(const std::string& fieldName) const;
};

// Every known component, in a stable, readable order (also the order they
// are written to scene files and shown in the Inspector).
const std::vector<ComponentDesc>& Components();
const ComponentDesc* FindComponent(const std::string& name);

// Adds `name` (and its dependencies) with default values. Returns false
// if it's unknown or already present.
bool AddComponent(Registry& registry, Entity entity, const std::string& name);

// Removes `name`. Fails (returning the reason) if another present component
// depends on it.
std::optional<std::string> RemoveComponent(Registry& registry, Entity entity, const std::string& name);

// Text form of a value, as written to scene files. `entityIndex` maps an
// entity to its index in the file (EntityRef only).
std::string FormatField(const FieldDesc& field, const FieldValue& value,
                        const std::function<std::optional<size_t>(Entity)>& entityIndex);

// Parses the text form. EntityRef parses to the referenced index, returned
// via `refIndex` (resolved to an entity once the whole scene is loaded);
// nullopt for malformed input.
std::optional<FieldValue> ParseField(const FieldDesc& field, const std::string& text,
                                     std::optional<size_t>* refIndex = nullptr);

} // namespace aige
