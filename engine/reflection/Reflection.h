#pragma once

#include "ecs/Registry.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace aige {

class PrefabLibrary;
struct Project;

// The Reflection & Command Registry (technical.md §7): the one place the
// engine describes everything that can be edited — components and their
// fields, project settings, and commands — with types, human-readable
// descriptions and constraints. Scene files, prefabs, the Inspector, Lua's
// get_field/set_field and AI proposals all go through it, so a change made
// by hand, by a script or by the AI is the same change.
//
// Registration is explicit (no static-initialization tricks): each module
// has a Register...() function that adds its types to a registry, e.g.
//
//   registry.Component<Health>("Health", "Hit points; at 0 the entity dies")
//       .Field("current", &Health::current, "Current hit points").Min(0)
//       .Field("max", &Health::max, "Maximum hit points").Min(1);

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
    Prefab,    // project-relative .prefab path (command parameters)
    Component, // a registered component name (command parameters)
};

const char* FieldTypeName(FieldType type);

using FieldValue = std::variant<float, int, bool, std::string, Entity>;

// What every field, setting and command parameter shares: identity, type,
// documentation, constraints.
struct FieldMeta {
    std::string name;
    std::string description;
    FieldType type = FieldType::Float;
    std::vector<std::string> options; // Enum
    std::optional<double> min;        // Float/Int
    std::optional<double> max;
};

// A value that breaks `meta`'s type or constraints, as a message; nullopt
// when valid. (Entity references are checked for existence by the caller,
// which knows the registry.)
std::optional<std::string> Validate(const FieldMeta& meta, const FieldValue& value);

struct FieldInfo : FieldMeta {
    std::function<FieldValue(Registry&, Entity)> get;
    std::function<void(Registry&, Entity, const FieldValue&)> set;
};

struct ComponentInfo {
    std::string name;
    std::string description;
    std::vector<FieldInfo> fields;
    // Users (and the AI) can add/remove it; false for structural components
    // like Tile, whose identity depends on them.
    bool userEditable = true;
    // Shown as an Inspector section (false for bookkeeping like PrefabInstance).
    bool inspectorVisible = true;
    // Components this one needs; adding it adds them too, and they can't be
    // removed while it's present.
    std::vector<std::string> dependencies;

    std::function<bool(Registry&, Entity)> has;
    std::function<void(Registry&, Entity)> add; // with defaults
    std::function<void(Registry&, Entity)> remove;
    // True when the values are recomputed every frame from other components
    // (ScreenTransform2D of a character or tile), so edits wouldn't stick.
    std::function<bool(Registry&, Entity)> derived;
    // Per-entity fields beyond `fields` (a script's properties): extra
    // `key=value` pairs written after the regular fields (values already
    // formatted, see QuoteValue) and parsed back on load (false for keys it
    // doesn't own).
    std::function<std::vector<std::pair<std::string, std::string>>(Registry&, Entity)> writeExtra;
    std::function<bool(Registry&, Entity, const std::string& key, const std::string& text)> readExtra;

    const FieldInfo* Field(const std::string& fieldName) const;
};

// Project-wide settings (renderer, camera, combat, physics): one instance
// per section, bound when registered.
struct SettingField : FieldMeta {
    std::function<FieldValue()> get;
    std::function<void(const FieldValue&)> set;
};

struct SettingsInfo {
    std::string name;
    std::string description;
    std::vector<SettingField> fields;

    const SettingField* Field(const std::string& fieldName) const;
};

// What a command acts on. Hosts fill in what they have; commands that need
// something missing fail with a message instead of crashing.
struct CommandContext {
    Registry& registry;
    PrefabLibrary* prefabs = nullptr;
    const Project* project = nullptr;
};

struct CommandResult {
    bool ok = true;
    std::string error;
    std::optional<Entity> created; // for commands that create an entity
};

// An action with typed parameters ("spawn this prefab here", "set this
// field"). Every AI proposal is a list of command invocations, and they run
// through the same code as the equivalent manual edit.
struct CommandInfo {
    std::string name;
    std::string description;
    std::vector<FieldMeta> params;
    std::function<CommandResult(CommandContext&, const std::vector<FieldValue>&)> run;
};

class ReflectionRegistry;

template <typename C>
class ComponentBuilder;
template <typename S>
class SettingsBuilder;
class CommandBuilder;

class ReflectionRegistry {
public:
    template <typename C>
    ComponentBuilder<C> Component(std::string name, std::string description);

    template <typename S>
    SettingsBuilder<S> Settings(std::string name, std::string description, S* instance);

    CommandBuilder Command(std::string name, std::string description);

    const std::vector<ComponentInfo>& Components() const { return components_; }
    const ComponentInfo* FindComponent(const std::string& name) const;
    const std::vector<SettingsInfo>& AllSettings() const { return settings_; }
    const SettingsInfo* FindSettings(const std::string& name) const;
    const std::vector<CommandInfo>& Commands() const { return commands_; }
    const CommandInfo* FindCommand(const std::string& name) const;

    // Registration-time access for builders and special cases.
    ComponentInfo& MutableComponent(const std::string& name);
    // Ready-made entries, for types defined at runtime (Lua modules, 4.8).
    // Register before anything holds pointers into the registry.
    void AddComponentInfo(ComponentInfo info) { components_.push_back(std::move(info)); }
    void AddCommandInfo(CommandInfo info) { commands_.push_back(std::move(info)); }
    // And their removal (tests; unloading a module).
    void RemoveComponentInfo(const std::string& name) {
        std::erase_if(components_, [&](const ComponentInfo& c) { return c.name == name; });
    }
    void RemoveCommandInfo(const std::string& name) {
        std::erase_if(commands_, [&](const CommandInfo& c) { return c.name == name; });
    }

    // Checks `args` against the command's parameters, then runs it.
    CommandResult Run(CommandContext& context, const std::string& command, const std::vector<FieldValue>& args) const;

    // The whole registry as JSON: what the AI service turns into its tool
    // schema (docs/technical.md §7.3), and what its contract test checks.
    nlohmann::json ExportSchema() const;

private:
    friend class CommandBuilder;
    template <typename C>
    friend class ComponentBuilder;
    template <typename S>
    friend class SettingsBuilder;

    std::vector<ComponentInfo> components_;
    std::vector<SettingsInfo> settings_;
    std::vector<CommandInfo> commands_;
};

// --- Value conversion ---------------------------------------------------------

float ToFloat(const FieldValue& value);
int ToInt(const FieldValue& value);
bool ToBool(const FieldValue& value);
std::string ToString(const FieldValue& value);
Entity ToEntity(const FieldValue& value);

// The FieldType a C++ member of type M is presented as by default.
template <typename M>
constexpr FieldType DefaultFieldType() {
    if constexpr (std::is_same_v<M, Entity>) {
        return FieldType::EntityRef; // before is_enum: entt::entity is an enum type
    } else if constexpr (std::is_enum_v<M>) {
        return FieldType::Enum;
    } else if constexpr (std::is_same_v<M, float>) {
        return FieldType::Float;
    } else if constexpr (std::is_same_v<M, int>) {
        return FieldType::Int;
    } else if constexpr (std::is_same_v<M, bool>) {
        return FieldType::Bool;
    } else {
        static_assert(std::is_same_v<M, std::string>, "unsupported reflected field type");
        return FieldType::String;
    }
}

template <typename M>
FieldValue ToFieldValue(const M& value) {
    if constexpr (std::is_same_v<M, Entity>) {
        return value;
    } else if constexpr (std::is_enum_v<M>) {
        return static_cast<int>(value);
    } else {
        return FieldValue(value);
    }
}

template <typename M>
M FromFieldValue(const FieldValue& value) {
    if constexpr (std::is_same_v<M, float>) {
        return ToFloat(value);
    } else if constexpr (std::is_same_v<M, int>) {
        return ToInt(value);
    } else if constexpr (std::is_same_v<M, bool>) {
        return ToBool(value);
    } else if constexpr (std::is_same_v<M, std::string>) {
        return ToString(value);
    } else if constexpr (std::is_same_v<M, Entity>) {
        return ToEntity(value);
    } else {
        static_assert(std::is_enum_v<M>);
        return static_cast<M>(ToInt(value));
    }
}

// --- Builders -------------------------------------------------------------------

// Constraint setters shared by every builder; they apply to the field (or
// parameter) added last.
template <typename Derived>
class MetaSetters {
public:
    Derived& Min(double value) {
        Last().min = value;
        return Self();
    }
    Derived& Max(double value) {
        Last().max = value;
        return Self();
    }
    Derived& Range(double low, double high) {
        Last().min = low;
        Last().max = high;
        return Self();
    }
    Derived& Options(std::vector<std::string> options) {
        Last().options = std::move(options);
        return Self();
    }
    // Presents the field as a richer type than its C++ type implies
    // (a std::string that holds a texture path, ...).
    Derived& As(FieldType type) {
        Last().type = type;
        return Self();
    }

protected:
    virtual FieldMeta& Last() = 0;
    Derived& Self() { return static_cast<Derived&>(*this); }
};

template <typename C>
class ComponentBuilder : public MetaSetters<ComponentBuilder<C>> {
public:
    ComponentBuilder(ReflectionRegistry& registry, size_t index) : registry_(registry), index_(index) {}

    template <typename M>
    ComponentBuilder& Field(std::string name, M C::*member, std::string description) {
        FieldInfo field;
        field.name = std::move(name);
        field.description = std::move(description);
        field.type = DefaultFieldType<M>();
        field.get = [member](Registry& registry, Entity entity) -> FieldValue {
            return ToFieldValue(registry.get<C>(entity).*member);
        };
        field.set = [member](Registry& registry, Entity entity, const FieldValue& value) {
            registry.get<C>(entity).*member = FromFieldValue<M>(value);
        };
        Info().fields.push_back(std::move(field));
        return *this;
    }

    ComponentBuilder& DependsOn(std::vector<std::string> components) {
        Info().dependencies = std::move(components);
        return *this;
    }
    // Not addable/removable by users (structural, like Tile).
    ComponentBuilder& Structural() {
        Info().userEditable = false;
        return *this;
    }
    // Bookkeeping not shown as an Inspector section (PrefabInstance).
    ComponentBuilder& Hidden() {
        Info().inspectorVisible = false;
        Info().userEditable = false;
        return *this;
    }
    ComponentBuilder& DerivedWhen(std::function<bool(Registry&, Entity)> predicate) {
        Info().derived = std::move(predicate);
        return *this;
    }
    // Replaces the setter of the field added last (side effects such as
    // reloading a script when its path changes).
    ComponentBuilder& OnSet(std::function<void(Registry&, Entity, const FieldValue&)> setter) {
        Info().fields.back().set = std::move(setter);
        return *this;
    }

    ComponentInfo& Info() { return registry_.components_[index_]; }

protected:
    FieldMeta& Last() override { return Info().fields.back(); }

private:
    ReflectionRegistry& registry_;
    size_t index_;
};

template <typename S>
class SettingsBuilder : public MetaSetters<SettingsBuilder<S>> {
public:
    SettingsBuilder(ReflectionRegistry& registry, size_t index, S* instance)
        : registry_(registry), index_(index), instance_(instance) {}

    template <typename M>
    SettingsBuilder& Field(std::string name, M S::*member, std::string description) {
        SettingField field;
        field.name = std::move(name);
        field.description = std::move(description);
        field.type = DefaultFieldType<M>();
        S* instance = instance_;
        field.get = [instance, member]() -> FieldValue { return ToFieldValue(instance->*member); };
        field.set = [instance, member](const FieldValue& value) { instance->*member = FromFieldValue<M>(value); };
        registry_.settings_[index_].fields.push_back(std::move(field));
        return *this;
    }

protected:
    FieldMeta& Last() override { return registry_.settings_[index_].fields.back(); }

private:
    ReflectionRegistry& registry_;
    size_t index_;
    S* instance_;
};

class CommandBuilder : public MetaSetters<CommandBuilder> {
public:
    CommandBuilder(ReflectionRegistry& registry, size_t index) : registry_(registry), index_(index) {}

    CommandBuilder& Param(std::string name, FieldType type, std::string description) {
        FieldMeta param;
        param.name = std::move(name);
        param.type = type;
        param.description = std::move(description);
        registry_.commands_[index_].params.push_back(std::move(param));
        return *this;
    }
    CommandBuilder& Run(std::function<CommandResult(CommandContext&, const std::vector<FieldValue>&)> run) {
        registry_.commands_[index_].run = std::move(run);
        return *this;
    }

protected:
    FieldMeta& Last() override { return registry_.commands_[index_].params.back(); }

private:
    ReflectionRegistry& registry_;
    size_t index_;
};

template <typename C>
ComponentBuilder<C> ReflectionRegistry::Component(std::string name, std::string description) {
    ComponentInfo info;
    info.name = std::move(name);
    info.description = std::move(description);
    info.has = [](Registry& registry, Entity entity) { return registry.all_of<C>(entity); };
    info.add = [](Registry& registry, Entity entity) { registry.emplace_or_replace<C>(entity); };
    info.remove = [](Registry& registry, Entity entity) { registry.remove<C>(entity); };
    info.derived = [](Registry&, Entity) { return false; };
    components_.push_back(std::move(info));
    return ComponentBuilder<C>(*this, components_.size() - 1);
}

template <typename S>
SettingsBuilder<S> ReflectionRegistry::Settings(std::string name, std::string description, S* instance) {
    settings_.push_back({std::move(name), std::move(description), {}});
    return SettingsBuilder<S>(*this, settings_.size() - 1, instance);
}

// --- Component operations ---------------------------------------------------------

// Adds `name` (and its dependencies) with default values. False if it's
// unknown or already present.
bool AddComponent(const ReflectionRegistry& reflection, Registry& registry, Entity entity, const std::string& name);

// Removes `name`. Fails (returning the reason) if another present component
// depends on it.
std::optional<std::string> RemoveComponent(const ReflectionRegistry& reflection, Registry& registry, Entity entity,
                                           const std::string& name);

// --- Text form (scene files, prefabs) -----------------------------------------------

// Text form of a value, as written to scene files. `entityIndex` maps an
// entity to its index in the file (EntityRef only).
std::string FormatField(const FieldMeta& field, const FieldValue& value,
                        const std::function<std::optional<size_t>(Entity)>& entityIndex);

// Parses the text form. EntityRef parses to the referenced index, returned
// via `refIndex` (resolved once the whole scene is loaded); nullopt for
// malformed input.
std::optional<FieldValue> ParseField(const FieldMeta& field, const std::string& text,
                                     std::optional<size_t>* refIndex = nullptr);

// Quotes a string value when it would otherwise break the scene format's
// whitespace-separated `key=value` pairs.
std::string QuoteValue(const std::string& value);

// Whether a field holds where this particular entity is (position, grid
// cell) or another entity, rather than what kind of thing it is — such
// fields are never copied from a prefab to its instances.
bool IsInstanceLocal(const std::string& component, const FieldMeta& field);

// JSON forms of values, for the editor UI and the AI service. Enums are
// option names; entity references are ids (or null).
nlohmann::json ValueToJson(const FieldMeta& meta, const FieldValue& value);
// Nullopt when the JSON has the wrong shape for the field's type.
std::optional<FieldValue> ValueFromJson(const FieldMeta& meta, const nlohmann::json& json);

} // namespace aige
