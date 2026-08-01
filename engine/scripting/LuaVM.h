#pragma once

#include <sol/sol.hpp>

namespace aige {

// Thin owner of the shared Lua state (technical.md §2-3: gameplay scripting
// is Lua/sol2, embedded — never a runtime dependency of anything outside
// this process). One VM per process; individual scripts get their own
// sol::environment (see Components.h) so their globals don't collide with
// each other while still sharing one lua_State.
class LuaVM {
public:
    LuaVM();

    sol::state& State() { return lua_; }

private:
    sol::state lua_;
};

} // namespace aige
