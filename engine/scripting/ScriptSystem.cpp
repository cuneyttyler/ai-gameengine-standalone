#include "scripting/ScriptSystem.h"

#include "scripting/Components.h"
#include "scripting/LuaVM.h"

#include <iostream>
#include <algorithm>
#include <optional>
#include <system_error>

namespace aige {

namespace {

sol::object ToLua(sol::state& lua, const ScriptValue& value) {
    return std::visit([&lua](const auto& v) { return sol::make_object(lua, v); }, value);
}

std::optional<ScriptValue> FromLua(const sol::object& value) {
    switch (value.get_type()) {
        case sol::type::number:
            return ScriptValue(value.as<double>());
        case sol::type::boolean:
            return ScriptValue(value.as<bool>());
        case sol::type::string:
            return ScriptValue(value.as<std::string>());
        default:
            return std::nullopt; // tables, functions, ...: not editable properties
    }
}

} // namespace

void ApplyScriptProperties(LuaVM& vm, LuaScript& script) {
    sol::object existing = script.env["properties"];
    sol::table properties = existing.is<sol::table>() ? existing.as<sol::table>() : vm.State().create_table();
    for (const auto& [name, value] : script.properties) {
        properties[name] = ToLua(vm.State(), value);
    }
    script.env["properties"] = properties;
}

std::vector<ScriptProperty> ReadScriptProperties(LuaVM& vm, const std::filesystem::path& path, std::string* error) {
    sol::state& lua = vm.State();

    // A throwaway environment where every unknown global is a do-nothing
    // function, so top-level calls into the gameplay API (get_position(),
    // find_nearest(), ...) don't fail when there's no game running.
    static const char* kMakeIndex =
        "return function(g)\n"
        "  local noop = function() return nil end\n"
        "  return function(_, k) local v = g[k]; if v ~= nil then return v end; return noop end\n"
        "end\n";
    sol::protected_function makeIndex = lua.script(kMakeIndex);
    sol::table meta = lua.create_table();
    meta["__index"] = makeIndex(lua.globals()).get<sol::object>();
    sol::environment env(lua, sol::create);
    env[sol::metatable_key] = meta;

    std::vector<ScriptProperty> result;
    auto loaded = lua.script_file(path.string(), env, sol::script_pass_on_error);
    if (!loaded.valid()) {
        if (error) {
            *error = loaded.get<sol::error>().what();
        }
        return result;
    }
    sol::object properties = env.raw_get<sol::object>("properties");
    if (!properties.is<sol::table>()) {
        return result;
    }
    for (const auto& [key, value] : properties.as<sol::table>()) {
        if (!key.is<std::string>()) {
            continue;
        }
        if (auto converted = FromLua(value)) {
            result.push_back({key.as<std::string>(), *converted});
        }
    }
    std::sort(result.begin(), result.end(),
              [](const ScriptProperty& a, const ScriptProperty& b) { return a.name < b.name; });
    return result;
}

void UpdateScripts(Registry& registry, LuaVM& vm, float dt, const ScriptBinder& binder,
                   const std::filesystem::path& baseDir) {
    auto view = registry.view<LuaScript>();
    for (auto entity : view) {
        auto& script = view.get<LuaScript>(entity);
        std::filesystem::path scriptPath = script.path;
        if (scriptPath.is_relative() && !baseDir.empty()) {
            scriptPath = baseDir / scriptPath;
        }

        std::error_code ec;
        auto mtime = std::filesystem::last_write_time(scriptPath, ec);
        // Load when the file changed since the last attempt (Unload() resets
        // the time to force one). A failed load isn't retried every frame —
        // only once the file is saved again.
        bool changed = !ec && mtime != script.lastWriteTime;

        if (changed) {
            script.lastWriteTime = mtime;
            script.env = sol::environment(vm.State(), sol::create, vm.State().globals());
            if (binder) {
                binder(script.env, entity);
            }

            auto result = vm.State().script_file(scriptPath.string(), script.env, sol::script_pass_on_error);
            if (!result.valid()) {
                sol::error err = result;
                std::cerr << "[Lua] " << script.path << ": " << err.what() << std::endl;
                script.loaded = false;
            } else {
                script.loaded = true;
                ApplyScriptProperties(vm, script);
                std::cout << "[Lua] (re)loaded " << script.path << std::endl;
            }
        }

        if (!script.loaded) {
            continue;
        }

        sol::protected_function updateFn = script.env["update"];
        if (!updateFn.valid()) {
            continue;
        }
        auto callResult = updateFn(dt);
        if (!callResult.valid()) {
            sol::error err = callResult;
            std::cerr << "[Lua] " << script.path << " update(): " << err.what() << std::endl;
        }
    }
}

} // namespace aige
