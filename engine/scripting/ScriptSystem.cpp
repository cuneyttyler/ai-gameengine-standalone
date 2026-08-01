#include "scripting/ScriptSystem.h"

#include "scripting/Components.h"
#include "scripting/LuaVM.h"

#include <iostream>
#include <system_error>

namespace aige {

void UpdateScripts(Registry& registry, LuaVM& vm, float dt, const ScriptBinder& binder) {
    auto view = registry.view<LuaScript>();
    for (auto entity : view) {
        auto& script = view.get<LuaScript>(entity);

        std::error_code ec;
        auto mtime = std::filesystem::last_write_time(script.path, ec);
        bool changed = !ec && (!script.loaded || mtime != script.lastWriteTime);

        if (changed) {
            script.env = sol::environment(vm.State(), sol::create, vm.State().globals());
            if (binder) {
                binder(script.env, entity);
            }

            auto result = vm.State().script_file(script.path, script.env, sol::script_pass_on_error);
            if (!result.valid()) {
                sol::error err = result;
                std::cerr << "[Lua] " << script.path << ": " << err.what() << std::endl;
                script.loaded = false;
            } else {
                script.loaded = true;
                script.lastWriteTime = mtime;
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
