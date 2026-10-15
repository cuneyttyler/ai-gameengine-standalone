#include "editing/Edits.h"

#include "assets/Prefab.h"
#include "gameplay/Components.h"
#include "reflection/Reflection.h"
#include "scripting/Components.h"

namespace aige {

namespace {

// JSON -> validated field value, with reference checks only the live scene can do.
std::optional<FieldValue> ReadValue(const FieldMeta& meta, const nlohmann::json& value, Registry& registry,
                                    std::string& error) {
    auto converted = ValueFromJson(meta, value);
    if (!converted) {
        error = meta.name + " can't be set to " + value.dump();
        return std::nullopt;
    }
    if (auto problem = Validate(meta, *converted)) {
        error = *problem;
        return std::nullopt;
    }
    if (meta.type == FieldType::EntityRef) {
        Entity target = ToEntity(*converted);
        if (target != entt::null && !registry.valid(target)) {
            error = meta.name + " refers to an entity that no longer exists";
            return std::nullopt;
        }
    }
    return converted;
}

} // namespace

std::optional<std::string> EditField(const ReflectionRegistry& reflection, Registry& registry, Entity entity,
                                     const std::string& componentName, const std::string& fieldName,
                                     const nlohmann::json& value) {
    if (!registry.valid(entity)) {
        return "that entity no longer exists";
    }
    const ComponentInfo* component = reflection.FindComponent(componentName);
    if (component == nullptr || !component->has(registry, entity)) {
        return "this entity has no " + componentName;
    }
    const FieldInfo* field = component->Field(fieldName);
    if (field == nullptr) {
        return componentName + " has no field " + fieldName;
    }
    std::string error;
    auto converted = ReadValue(*field, value, registry, error);
    if (!converted) {
        return error;
    }
    field->set(registry, entity, *converted);
    RecordOverride(registry, entity, componentName, fieldName);
    return std::nullopt;
}

std::optional<std::string> EditAddComponent(const ReflectionRegistry& reflection, Registry& registry,
                                            PrefabLibrary* prefabs, Entity entity, const std::string& componentName,
                                            const nlohmann::json& fields) {
    if (!registry.valid(entity)) {
        return "that entity no longer exists";
    }
    const ComponentInfo* component = reflection.FindComponent(componentName);
    if (component == nullptr) {
        return "unknown component " + componentName;
    }
    if (!component->userEditable) {
        return componentName + " can't be added by hand";
    }
    if (component->has(registry, entity)) {
        return "already has " + componentName;
    }
    // Validate every initial value before changing anything.
    std::vector<std::pair<const FieldInfo*, FieldValue>> values;
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const FieldInfo* field = component->Field(it.key());
        if (field == nullptr) {
            return componentName + " has no field " + it.key();
        }
        std::string error;
        auto value = ReadValue(*field, it.value(), registry, error);
        if (!value) {
            return error;
        }
        values.emplace_back(field, *value);
    }

    AddComponent(reflection, registry, entity, componentName);
    if (prefabs != nullptr && HasOverride(registry, entity, "-" + componentName)) {
        prefabs->Revert(registry, entity, "-" + componentName); // re-adding a removed prefab component
    }
    for (const auto& [field, value] : values) {
        field->set(registry, entity, value);
        RecordOverride(registry, entity, componentName, field->name);
    }
    return std::nullopt;
}

std::optional<std::string> EditRemoveComponent(const ReflectionRegistry& reflection, Registry& registry, Entity entity,
                                               const std::string& componentName) {
    if (!registry.valid(entity)) {
        return "that entity no longer exists";
    }
    const ComponentInfo* component = reflection.FindComponent(componentName);
    if (component != nullptr && component->has(registry, entity) && !component->userEditable) {
        return componentName + " can't be removed by hand";
    }
    if (auto problem = RemoveComponent(reflection, registry, entity, componentName)) {
        return problem;
    }
    RecordOverride(registry, entity, componentName, "");
    return std::nullopt;
}

std::optional<std::string> EditSetting(const ReflectionRegistry& reflection, const std::string& sectionName,
                                       const std::string& fieldName, const nlohmann::json& value) {
    const SettingsInfo* section = reflection.FindSettings(sectionName);
    const SettingField* field = section ? section->Field(fieldName) : nullptr;
    if (field == nullptr) {
        return "no setting " + sectionName + "." + fieldName;
    }
    auto converted = ValueFromJson(*field, value);
    if (!converted) {
        return fieldName + " can't be set to " + value.dump();
    }
    if (auto problem = Validate(*field, *converted)) {
        return problem;
    }
    field->set(*converted);
    return std::nullopt;
}

std::optional<std::string> EditScriptProperty(Registry& registry, Entity entity, const std::string& name,
                                              const nlohmann::json& value) {
    if (!registry.valid(entity)) {
        return "that entity no longer exists";
    }
    auto* script = registry.try_get<LuaScript>(entity);
    if (script == nullptr) {
        return "has no script";
    }
    if (name.empty()) {
        return "a script property needs a name";
    }
    ScriptValue converted;
    if (value.is_boolean()) {
        converted = value.get<bool>();
    } else if (value.is_number()) {
        converted = value.get<double>();
    } else if (value.is_string()) {
        converted = value.get<std::string>();
    } else {
        return "script properties are numbers, true/false or text";
    }
    script->properties[name] = converted;
    if (registry.all_of<PrefabInstance>(entity)) {
        AddOverride(registry, entity, "LuaScript.prop." + name);
    }
    return std::nullopt;
}

} // namespace aige
