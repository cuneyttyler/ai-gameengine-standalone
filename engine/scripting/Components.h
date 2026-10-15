#pragma once

#include <sol/sol.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <variant>

namespace aige {

// A script property value: what a script's `properties = { ... }` table can
// declare (numbers, booleans, strings).
using ScriptValue = std::variant<double, bool, std::string>;

// A per-entity Lua behavior. `path` is watched for changes (ScriptSystem's
// UpdateScripts) and hot-reloaded automatically whenever its on-disk mtime
// changes — no app restart needed. Reload replaces `env` entirely, so
// script-local state (e.g. `local t = 0` at chunk scope) resets on reload;
// that's an intentional bootstrap simplification, not a bug.
struct LuaScript {
    std::string path;
    sol::environment env;
    std::filesystem::file_time_type lastWriteTime{};
    bool loaded = false;
    // This entity's values for the script's declared properties, overriding
    // the script's defaults (edited in the Inspector, saved with the scene).
    std::map<std::string, ScriptValue> properties;

    // Forces a fresh load on the next update (path changed, entity copied).
    void Unload() {
        env = {};
        lastWriteTime = {};
        loaded = false;
    }
};

} // namespace aige
