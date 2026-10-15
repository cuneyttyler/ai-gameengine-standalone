#include "ai_bridge/Proposal.h"

#include "assets/Prefab.h"
#include "editing/Edits.h"
#include "assets/Project.h"
#include "gameplay/Components.h"
#include "reflection/Reflection.h"
#include "scripting/Components.h"

namespace aige {

namespace {

enum class Mode { Resolve, Apply };

nlohmann::json MetaJson(const FieldMeta& meta) {
    nlohmann::json json = {{"name", meta.name}, {"type", FieldTypeName(meta.type)}};
    if (!meta.options.empty()) json["options"] = meta.options;
    if (meta.min) json["min"] = *meta.min;
    if (meta.max) json["max"] = *meta.max;
    return json;
}

std::string EntityLabel(Registry& registry, Entity entity) {
    std::string id = "#" + std::to_string(static_cast<uint32_t>(entity));
    if (const auto* name = registry.try_get<Name>(entity); name != nullptr && !name->value.empty()) {
        return name->value + " " + id;
    }
    return "Entity " + id;
}

std::optional<Entity> ReadEntity(const nlohmann::json& op, Registry& registry, std::string& error) {
    if (!op.contains("entity") || !op["entity"].is_number_integer()) {
        error = "missing entity";
        return std::nullopt;
    }
    Entity entity = static_cast<Entity>(op["entity"].get<uint32_t>());
    if (!registry.valid(entity)) {
        error = "entity #" + std::to_string(op["entity"].get<uint32_t>()) + " no longer exists";
        return std::nullopt;
    }
    return entity;
}

// Converts and validates a JSON value for a field; sets `error` on failure.
std::optional<FieldValue> ReadValue(const FieldMeta& meta, const nlohmann::json& value, Registry& registry,
                                    std::string& error) {
    auto converted = ValueFromJson(meta, value);
    if (!converted) {
        error = meta.name + " can't be " + value.dump();
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

std::optional<ScriptValue> ReadScriptValue(const nlohmann::json& value) {
    if (value.is_boolean()) return ScriptValue(value.get<bool>());
    if (value.is_number()) return ScriptValue(value.get<double>());
    if (value.is_string()) return ScriptValue(value.get<std::string>());
    return std::nullopt;
}

// Resolves one operation (and, in Apply mode, performs it). Everything about
// an operation — what it targets, validation, before/after — lives here once,
// so review and apply can't disagree.
ChangeItem Process(const nlohmann::json& op, Mode mode, Registry& registry, const ReflectionRegistry& reflection,
                   PrefabLibrary& prefabs, const Project& project, ApplyOutcome* outcome) {
    ChangeItem item;
    item.op = op;
    const std::string kind = op.value("op", "");
    std::string& error = item.error;
    const bool apply = mode == Mode::Apply;

    if (kind == "set_field") {
        item.kind = "field";
        const std::string componentName = op.value("component", "");
        const std::string fieldName = op.value("field", "");
        auto entity = ReadEntity(op, registry, error);
        if (!entity) return item;
        item.target = EntityLabel(registry, *entity) + " · " + componentName + "." + fieldName;
        const ComponentInfo* component = reflection.FindComponent(componentName);
        const FieldInfo* field = component ? component->Field(fieldName) : nullptr;
        if (field == nullptr) {
            error = "unknown field " + componentName + "." + fieldName;
            return item;
        }
        item.field = MetaJson(*field);
        if (!component->has(registry, *entity)) {
            error = "has no " + componentName;
            return item;
        }
        item.before = ValueToJson(*field, field->get(registry, *entity));
        auto value = ReadValue(*field, op.value("value", nlohmann::json()), registry, error);
        if (!value) return item;
        item.after = ValueToJson(*field, *value);
        if (apply) {
            if (auto problem = EditField(reflection, registry, *entity, componentName, fieldName, op["value"])) {
                error = *problem;
            }
        }

    } else if (kind == "add_component") {
        item.kind = "add_component";
        const std::string componentName = op.value("component", "");
        auto entity = ReadEntity(op, registry, error);
        if (!entity) return item;
        item.target = EntityLabel(registry, *entity) + " · + " + componentName;
        const ComponentInfo* component = reflection.FindComponent(componentName);
        if (component == nullptr) {
            error = "unknown component " + componentName;
            return item;
        }
        if (!component->userEditable) {
            error = componentName + " can't be added by hand";
            return item;
        }
        if (component->has(registry, *entity)) {
            error = "already has " + componentName;
            return item;
        }
        nlohmann::json fields = op.value("fields", nlohmann::json::object());
        std::vector<std::pair<const FieldInfo*, FieldValue>> values;
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            const FieldInfo* field = component->Field(it.key());
            if (field == nullptr) {
                error = componentName + " has no field " + it.key();
                return item;
            }
            auto value = ReadValue(*field, it.value(), registry, error);
            if (!value) return item;
            values.emplace_back(field, *value);
        }
        item.after = fields;
        if (apply) {
            if (auto problem = EditAddComponent(reflection, registry, &prefabs, *entity, componentName, fields)) {
                error = *problem;
            }
        }

    } else if (kind == "remove_component") {
        item.kind = "remove_component";
        const std::string componentName = op.value("component", "");
        auto entity = ReadEntity(op, registry, error);
        if (!entity) return item;
        item.target = EntityLabel(registry, *entity) + " · − " + componentName;
        const ComponentInfo* component = reflection.FindComponent(componentName);
        if (component == nullptr || !component->has(registry, *entity)) {
            error = "has no " + componentName;
            return item;
        }
        if (!component->userEditable) {
            error = componentName + " can't be removed by hand";
            return item;
        }
        if (apply) {
            if (auto problem = EditRemoveComponent(reflection, registry, *entity, componentName)) {
                error = *problem;
            }
        }

    } else if (kind == "set_prefab_field") {
        item.kind = "prefab_field";
        item.touchesFiles = true;
        const std::string path = op.value("prefab", "");
        const std::string componentName = op.value("component", "");
        const std::string fieldName = op.value("field", "");
        const PrefabInfo* info = prefabs.Find(path);
        item.target = (info ? info->name : path) + " (prefab) · " + componentName + "." + fieldName;
        const ComponentInfo* component = reflection.FindComponent(componentName);
        const FieldInfo* field = component ? component->Field(fieldName) : nullptr;
        if (info == nullptr) {
            error = "no prefab " + path;
            return item;
        }
        if (field == nullptr) {
            error = "unknown field " + componentName + "." + fieldName;
            return item;
        }
        item.field = MetaJson(*field);
        bool has = false;
        prefabs.VisitTemplate(path, [&](Registry& r, Entity e) {
            has = component->has(r, e);
            if (has) {
                item.before = ValueToJson(*field, field->get(r, e));
            }
        });
        if (!has) {
            error = path + " has no " + componentName;
            return item;
        }
        auto value = ReadValue(*field, op.value("value", nlohmann::json()), registry, error);
        if (!value) return item;
        item.after = ValueToJson(*field, *value);
        if (apply) {
            if (auto problem = prefabs.SetTemplateField(path, componentName, fieldName, *value)) {
                error = *problem;
                return item;
            }
            prefabs.SyncAll(registry, path);
            if (outcome) outcome->prefabsChanged = true;
        }

    } else if (kind == "set_setting") {
        item.kind = "setting";
        item.touchesFiles = true;
        const std::string sectionName = op.value("section", "");
        const std::string fieldName = op.value("field", "");
        item.target = "Settings · " + sectionName + "." + fieldName;
        const SettingsInfo* section = reflection.FindSettings(sectionName);
        const SettingField* field = section ? section->Field(fieldName) : nullptr;
        if (field == nullptr) {
            error = "no setting " + sectionName + "." + fieldName;
            return item;
        }
        item.field = MetaJson(*field);
        item.before = ValueToJson(*field, field->get());
        auto value = ReadValue(*field, op.value("value", nlohmann::json()), registry, error);
        if (!value) return item;
        item.after = ValueToJson(*field, *value);
        if (apply) {
            if (auto problem = EditSetting(reflection, sectionName, fieldName, op["value"])) {
                error = *problem;
                return item;
            }
            if (outcome) outcome->settingsChanged = true;
        }

    } else if (kind == "set_script_property") {
        item.kind = "script_property";
        const std::string name = op.value("name", "");
        auto entity = ReadEntity(op, registry, error);
        if (!entity) return item;
        item.target = EntityLabel(registry, *entity) + " · script." + name;
        auto* script = registry.try_get<LuaScript>(*entity);
        if (script == nullptr) {
            error = "has no script";
            return item;
        }
        auto value = ReadScriptValue(op.value("value", nlohmann::json()));
        if (!value || name.empty()) {
            error = "script properties are numbers, true/false or text";
            return item;
        }
        if (auto it = script->properties.find(name); it != script->properties.end()) {
            std::visit([&](const auto& v) { item.before = v; }, it->second);
        }
        item.after = op["value"];
        if (apply) {
            if (auto problem = EditScriptProperty(registry, *entity, name, op["value"])) {
                error = *problem;
            }
        }

    } else if (kind == "command") {
        item.kind = "command";
        const std::string name = op.value("name", "");
        item.target = name;
        const CommandInfo* command = reflection.FindCommand(name);
        if (command == nullptr) {
            error = "unknown command " + name;
            return item;
        }
        item.touchesFiles = name == "script.write" || name == "prefab.createFromEntity";
        nlohmann::json args = op.value("args", nlohmann::json::object());
        std::vector<FieldValue> values;
        for (const auto& param : command->params) {
            if (!args.contains(param.name)) {
                error = "needs " + param.name;
                return item;
            }
            auto value = ReadValue(param, args[param.name], registry, error);
            if (!value) return item;
            values.push_back(*value);
        }
        if (name == "script.write") {
            item.target = "Write " + args.value("path", std::string());
            item.detail = args.value("source", std::string());
            std::error_code ec;
            if (std::filesystem::exists(project.Resolve(args.value("path", std::string())), ec)) {
                item.before = "(replaces the existing file)";
            }
        } else {
            nlohmann::json shown = args;
            item.detail = shown.dump();
        }
        item.after = args;
        if (apply) {
            CommandContext context{registry, &prefabs, &project};
            CommandResult result = reflection.Run(context, name, values);
            if (!result.ok) {
                error = result.error;
                return item;
            }
            if (name == "prefab.createFromEntity" && outcome) {
                outcome->prefabsChanged = true;
            }
        }

    } else {
        item.kind = "unknown";
        item.target = kind.empty() ? "(missing op)" : kind;
        error = "unknown operation " + kind;
    }
    return item;
}

} // namespace

bool ResolvedProposal::AllValid() const {
    for (const auto& item : items) {
        if (!item.error.empty()) {
            return false;
        }
    }
    return true;
}

ResolvedProposal ResolveProposal(const nlohmann::json& proposal, Registry& registry,
                                 const ReflectionRegistry& reflection, PrefabLibrary& prefabs,
                                 const Project& project) {
    ResolvedProposal resolved;
    resolved.summary = proposal.value("summary", "");
    resolved.notes = proposal.value("notes", "");
    for (const auto& op : proposal.value("operations", nlohmann::json::array())) {
        resolved.items.push_back(Process(op, Mode::Resolve, registry, reflection, prefabs, project, nullptr));
    }
    return resolved;
}

ApplyOutcome ApplyOperations(const std::vector<nlohmann::json>& operations, Registry& registry,
                             const ReflectionRegistry& reflection, PrefabLibrary& prefabs, const Project& project) {
    ApplyOutcome outcome;
    for (const auto& op : operations) {
        ChangeItem item = Process(op, Mode::Apply, registry, reflection, prefabs, project, &outcome);
        if (item.error.empty()) {
            ++outcome.applied;
        } else {
            outcome.errors.push_back(item.target + ": " + item.error);
        }
    }
    return outcome;
}

} // namespace aige
