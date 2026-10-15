#pragma once

#include "ecs/Registry.h"
#include "scripting/Components.h"

#include <sol/sol.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace aige {

class LuaVM;

// Called once right after an entity's script is (re)loaded, before its Lua
// chunk runs, so the caller can expose whatever C++ state that particular
// script needs (e.g. a bound get/set position pair) into its environment.
// Kept as a callback rather than baked into this module so ScriptSystem
// itself stays ignorant of any specific gameplay component type.
using ScriptBinder = std::function<void(sol::environment&, Entity)>;

// For every entity with a LuaScript: hot-reloads it if its file's mtime has
// changed (or it hasn't loaded yet), then calls its `update(dt)` Lua
// function if one is defined. Load/reload/runtime errors are logged to
// stderr and leave the entity's previous (or no) behavior in place rather
// than throwing — a bad script shouldn't crash the game. Relative script paths
// resolve against `baseDir` (the project root), so scenes stay portable.
// Called for every load or runtime error (after it's logged), e.g. so a
// host can show it or a dry run can collect it.
using ScriptErrorFn = std::function<void(Entity, const std::string& path, const std::string& message)>;

void UpdateScripts(Registry& registry, LuaVM& vm, float dt, const ScriptBinder& binder = {},
                   const std::filesystem::path& baseDir = {}, const ScriptErrorFn& onError = {});



// Pushes an entity's property values into its loaded script's `properties`
// table (creating the table if the script declared none). Called after
// every (re)load; call it again after editing values on a running script.
void ApplyScriptProperties(LuaVM& vm, LuaScript& script);

struct ScriptProperty {
    std::string name;
    ScriptValue value; // the script's default
};

// The properties a script declares with a top-level `properties = { ... }`
// table (number, boolean and string entries; sorted by name), found by
// running it in a sandbox where every unknown global is a no-op — so the
// editor can list them without a running game. Empty, with `error` set, if
// the script fails to load.
std::vector<ScriptProperty> ReadScriptProperties(LuaVM& vm, const std::filesystem::path& path,
                                                 std::string* error = nullptr);

} // namespace aige
