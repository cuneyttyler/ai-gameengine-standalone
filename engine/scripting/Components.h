#pragma once

#include <sol/sol.hpp>

#include <filesystem>
#include <string>

namespace aige {

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
};

} // namespace aige
