#include "modules/Modules.h"

#include "scripting/LuaVM.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>

namespace aige {

namespace fs = std::filesystem;

namespace {

ModuleLoadResult g_lastLoad;

// One Lua state per module's script. The environment is declared after the
// VM so it is released first (whatever outlives whom at exit).
struct ModuleScript {
    std::shared_ptr<LuaVM> vm;
    sol::environment env;
    CommandContext* current = nullptr; // the command being run
};

lua_Integer ToId(Entity entity) { return static_cast<lua_Integer>(static_cast<uint32_t>(entity)); }
Entity ToEntity(lua_Integer id) { return static_cast<Entity>(static_cast<uint32_t>(id)); }

std::optional<FieldType> ParseType(const std::string& name, bool forCommand) {
    static const std::map<std::string, FieldType> types = {
        {"float", FieldType::Float},     {"int", FieldType::Int},       {"bool", FieldType::Bool},
        {"string", FieldType::String},   {"enum", FieldType::Enum},     {"texture", FieldType::Texture},
        {"audio", FieldType::Audio},     {"script", FieldType::Script}, {"prefab", FieldType::Prefab},
        {"entity", FieldType::EntityRef}, {"component", FieldType::Component},
    };
    auto it = types.find(name);
    if (it == types.end()) return std::nullopt;
    if (!forCommand && (it->second == FieldType::EntityRef || it->second == FieldType::Component)) {
        return std::nullopt; // per-entity references aren't supported in module components yet
    }
    return it->second;
}

// A field's metadata and default from its JSON; an error message on failure.
std::variant<std::pair<FieldMeta, FieldValue>, std::string> ParseField(const nlohmann::json& j, bool forCommand) {
    if (!j.is_object() || !j.contains("name") || !j["name"].is_string() || j["name"].get<std::string>().empty()) {
        return std::string("a field needs a \"name\"");
    }
    FieldMeta meta;
    meta.name = j["name"].get<std::string>();
    meta.description = j.value("description", "");
    auto type = ParseType(j.value("type", "float"), forCommand);
    if (!type) {
        return "field " + meta.name + ": unknown type \"" + j.value("type", "") + "\"";
    }
    meta.type = *type;
    if (j.contains("min") && j["min"].is_number()) meta.min = j["min"].get<double>();
    if (j.contains("max") && j["max"].is_number()) meta.max = j["max"].get<double>();
    if (meta.type == FieldType::Enum) {
        for (const auto& option : j.value("options", nlohmann::json::array())) {
            if (option.is_string()) meta.options.push_back(option.get<std::string>());
        }
        if (meta.options.empty()) {
            return "field " + meta.name + ": an enum needs \"options\"";
        }
    }
    FieldValue value;
    const nlohmann::json def = j.value("default", nlohmann::json());
    switch (meta.type) {
        case FieldType::Float:
            value = def.is_number() ? def.get<float>() : static_cast<float>(meta.min.value_or(0.0));
            break;
        case FieldType::Int:
            value = def.is_number() ? def.get<int>() : static_cast<int>(meta.min.value_or(0.0));
            break;
        case FieldType::Bool:
            value = def.is_boolean() && def.get<bool>();
            break;
        case FieldType::Enum: {
            int index = 0;
            if (def.is_string()) {
                auto it = std::find(meta.options.begin(), meta.options.end(), def.get<std::string>());
                index = it == meta.options.end() ? 0 : static_cast<int>(it - meta.options.begin());
            }
            value = index;
            break;
        }
        case FieldType::EntityRef:
            value = Entity{entt::null};
            break;
        default:
            value = def.is_string() ? def.get<std::string>() : std::string();
    }
    if (auto problem = Validate(meta, value)) {
        return "field " + meta.name + ": default " + *problem;
    }
    return std::pair{meta, value};
}

ComponentInfo MakeComponent(const std::string& name, const std::string& description,
                            std::vector<std::pair<FieldMeta, FieldValue>> fields) {
    ComponentInfo info;
    info.name = name;
    info.description = description;
    auto defaults = std::make_shared<std::map<std::string, FieldValue>>();
    for (const auto& [meta, value] : fields) {
        (*defaults)[meta.name] = value;
    }
    info.has = [name](Registry& r, Entity e) {
        const auto* data = r.try_get<ModuleData>(e);
        return data && data->values.contains(name);
    };
    info.add = [name, defaults](Registry& r, Entity e) {
        r.get_or_emplace<ModuleData>(e).values[name] = *defaults;
    };
    info.remove = [name](Registry& r, Entity e) {
        if (auto* data = r.try_get<ModuleData>(e)) {
            data->values.erase(name);
            if (data->values.empty()) r.remove<ModuleData>(e);
        }
    };
    info.derived = [](Registry&, Entity) { return false; };
    for (const auto& [meta, value] : fields) {
        FieldInfo field;
        static_cast<FieldMeta&>(field) = meta;
        const std::string fieldName = meta.name;
        const FieldValue fallback = value;
        field.get = [name, fieldName, fallback](Registry& r, Entity e) -> FieldValue {
            const auto* data = r.try_get<ModuleData>(e);
            if (!data) return fallback;
            auto component = data->values.find(name);
            if (component == data->values.end()) return fallback;
            auto it = component->second.find(fieldName);
            return it == component->second.end() ? fallback : it->second;
        };
        field.set = [name, fieldName](Registry& r, Entity e, const FieldValue& v) {
            r.get_or_emplace<ModuleData>(e).values[name][fieldName] = v;
        };
        info.fields.push_back(std::move(field));
    }
    return info;
}

sol::object ToLua(lua_State* L, Registry& registry, const FieldMeta& field, const FieldValue& value) {
    if (field.type == FieldType::Enum) {
        int index = ToInt(value);
        return index >= 0 && index < static_cast<int>(field.options.size()) ? sol::make_object(L, field.options[index])
                                                                             : sol::make_object(L, index);
    }
    if (const auto* target = std::get_if<Entity>(&value)) {
        return registry.valid(*target) ? sol::make_object(L, ToId(*target)) : sol::lua_nil;
    }
    return std::visit([L](const auto& v) -> sol::object {
        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, Entity>) {
            return sol::lua_nil;
        } else {
            return sol::make_object(L, v);
        }
    }, value);
}

std::optional<FieldValue> FromLua(const FieldMeta& field, const sol::object& value) {
    switch (field.type) {
        case FieldType::Float:
            if (value.is<double>()) return static_cast<float>(value.as<double>());
            return std::nullopt;
        case FieldType::Int:
            if (value.is<double>()) return static_cast<int>(value.as<double>());
            return std::nullopt;
        case FieldType::Bool:
            if (value.is<bool>()) return value.as<bool>();
            return std::nullopt;
        case FieldType::Enum:
            if (value.is<std::string>()) {
                auto it = std::find(field.options.begin(), field.options.end(), value.as<std::string>());
                if (it != field.options.end()) return static_cast<int>(it - field.options.begin());
            }
            return std::nullopt;
        case FieldType::EntityRef:
            if (value.is<double>()) return ToEntity(static_cast<lua_Integer>(value.as<double>()));
            return std::nullopt;
        default:
            if (value.is<std::string>()) return value.as<std::string>();
            return std::nullopt;
    }
}

void BindApi(ModuleScript& script, ReflectionRegistry& reflection, const std::string& moduleName) {
    sol::environment& env = script.env;
    lua_State* L = script.vm->State().lua_state();
    ModuleScript* self = &script;
    env["get_field"] = [self, &reflection, L](lua_Integer id, const std::string& componentName,
                                             const std::string& fieldName) -> sol::object {
        if (!self->current) return sol::lua_nil;
        Registry& registry = self->current->registry;
        Entity entity = ToEntity(id);
        const ComponentInfo* component = reflection.FindComponent(componentName);
        const FieldInfo* field = component ? component->Field(fieldName) : nullptr;
        if (!field || !registry.valid(entity) || !component->has(registry, entity)) return sol::lua_nil;
        return ToLua(L, registry, *field, field->get(registry, entity));
    };
    env["set_field"] = [self, &reflection](lua_Integer id, const std::string& componentName,
                                          const std::string& fieldName, sol::object value) {
        if (!self->current) return false;
        Registry& registry = self->current->registry;
        Entity entity = ToEntity(id);
        const ComponentInfo* component = reflection.FindComponent(componentName);
        const FieldInfo* field = component ? component->Field(fieldName) : nullptr;
        if (!field || !registry.valid(entity) || !component->has(registry, entity)) return false;
        auto converted = FromLua(*field, value);
        if (!converted || Validate(*field, *converted)) return false;
        field->set(registry, entity, *converted);
        return true;
    };
    env["entities_with"] = [self, &reflection, L](const std::string& componentName) {
        sol::table ids = sol::state_view(L).create_table();
        const ComponentInfo* component = reflection.FindComponent(componentName);
        if (!self->current || !component) return ids;
        Registry& registry = self->current->registry;
        int n = 1;
        for (auto entity : registry.view<entt::entity>()) {
            if (component->has(registry, entity)) ids[n++] = ToId(entity);
        }
        return ids;
    };
    env["log"] = [moduleName](const std::string& text) { std::printf("[module %s] %s\n", moduleName.c_str(), text.c_str()); };
}

CommandResult RunLuaCommand(const std::shared_ptr<ModuleScript>& script, const std::string& function,
                            const std::vector<FieldMeta>& params, CommandContext& context,
                            const std::vector<FieldValue>& args) {
    sol::protected_function fn = script->env[function];
    if (!fn.valid()) {
        return {false, "the module has no function " + function};
    }
    lua_State* L = script->vm->State().lua_state();
    sol::table table = script->vm->State().create_table();
    for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
        table[params[i].name] = ToLua(L, context.registry, params[i], args[i]);
    }
    script->current = &context;
    sol::protected_function_result result = fn(table);
    script->current = nullptr;
    if (!result.valid()) {
        sol::error error = result;
        return {false, error.what()};
    }
    if (result.return_count() >= 1) {
        sol::object first = result.get<sol::object>(0);
        if (first.is<bool>() && !first.as<bool>()) {
            std::string why = result.return_count() >= 2 && result.get<sol::object>(1).is<std::string>()
                                  ? result.get<std::string>(1)
                                  : function + " failed";
            return {false, why};
        }
    }
    return {};
}

} // namespace

ModuleLoadResult LoadModules(ReflectionRegistry& reflection, const fs::path& projectRoot) {
    ModuleLoadResult result;
    std::error_code ec;
    const fs::path dir = projectRoot / "modules";
    std::vector<fs::path> folders;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_directory() && fs::exists(entry.path() / "module.json")) folders.push_back(entry.path());
    }
    std::sort(folders.begin(), folders.end());

    for (const fs::path& folder : folders) {
        const std::string moduleName = folder.filename().string();
        const std::string where = "modules/" + moduleName + "/module.json";
        std::ifstream in(folder / "module.json");
        nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
        if (!j.is_object()) {
            result.errors.push_back(where + ": not valid JSON");
            continue;
        }
        LoadedModule loaded;
        loaded.name = moduleName;
        loaded.description = j.value("description", "");

        for (const auto& c : j.value("components", nlohmann::json::array())) {
            const std::string name = c.is_object() ? c.value("name", "") : "";
            if (name.empty()) {
                result.errors.push_back(where + ": a component needs a \"name\"");
                continue;
            }
            if (reflection.FindComponent(name)) {
                result.errors.push_back(where + ": component " + name + " already exists");
                continue;
            }
            std::vector<std::pair<FieldMeta, FieldValue>> fields;
            bool ok = true;
            for (const auto& f : c.value("fields", nlohmann::json::array())) {
                auto parsed = ParseField(f, false);
                if (auto* error = std::get_if<std::string>(&parsed)) {
                    result.errors.push_back(where + ": " + name + ": " + *error);
                    ok = false;
                    break;
                }
                fields.push_back(std::get<0>(parsed));
            }
            if (!ok) continue;
            reflection.AddComponentInfo(MakeComponent(name, c.value("description", ""), std::move(fields)));
            loaded.components.push_back(name);
        }

        std::shared_ptr<ModuleScript> script;
        const auto commands = j.value("commands", nlohmann::json::array());
        if (!commands.empty()) {
            const fs::path scriptPath = folder / j.value("script", "module.lua");
            script = std::make_shared<ModuleScript>();
            script->vm = std::make_shared<LuaVM>();
            script->env = sol::environment(script->vm->State(), sol::create, script->vm->State().globals());
            BindApi(*script, reflection, moduleName);
            std::string loadError;
            {
                // The result refers to the VM's stack: it must be gone before the VM is.
                auto loadedScript =
                    script->vm->State().script_file(scriptPath.string(), script->env, sol::script_pass_on_error);
                if (!loadedScript.valid()) {
                    sol::error error = loadedScript;
                    loadError = error.what();
                }
            }
            if (!loadError.empty()) {
                result.errors.push_back(where + ": " + loadError);
                script.reset();
            }
        }
        for (const auto& c : commands) {
            const std::string name = c.is_object() ? c.value("name", "") : "";
            const std::string function = c.is_object() ? c.value("function", "") : "";
            if (name.empty() || function.empty()) {
                result.errors.push_back(where + ": a command needs a \"name\" and a \"function\"");
                continue;
            }
            if (!script) {
                result.errors.push_back(where + ": command " + name + " has no script to run");
                continue;
            }
            if (reflection.FindCommand(name)) {
                result.errors.push_back(where + ": command " + name + " already exists");
                continue;
            }
            CommandInfo info;
            info.name = name;
            info.description = c.value("description", "");
            bool ok = true;
            for (const auto& p : c.value("params", nlohmann::json::array())) {
                auto parsed = ParseField(p, true);
                if (auto* error = std::get_if<std::string>(&parsed)) {
                    result.errors.push_back(where + ": " + name + ": " + *error);
                    ok = false;
                    break;
                }
                info.params.push_back(std::get<0>(parsed).first);
            }
            if (!ok) continue;
            if (!sol::object(script->env[function]).is<sol::function>()) {
                result.errors.push_back(where + ": " + name + ": the script has no function " + function);
                continue;
            }
            info.run = [script, function, params = info.params](CommandContext& context,
                                                                const std::vector<FieldValue>& args) {
                return RunLuaCommand(script, function, params, context, args);
            };
            reflection.AddCommandInfo(std::move(info));
            loaded.commands.push_back(name);
        }
        result.modules.push_back(std::move(loaded));
    }
    g_lastLoad = result;
    return result;
}

const ModuleLoadResult& LastModuleLoad() { return g_lastLoad; }

} // namespace aige
