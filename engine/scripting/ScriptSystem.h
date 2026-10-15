#pragma once

#include "ecs/Registry.h"

#include <sol/sol.hpp>

#include <filesystem>
#include <functional>

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
void UpdateScripts(Registry& registry, LuaVM& vm, float dt, const ScriptBinder& binder = {},
                   const std::filesystem::path& baseDir = {});

} // namespace aige
