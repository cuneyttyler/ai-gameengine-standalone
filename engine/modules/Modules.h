#pragma once

#include "ecs/Registry.h"
#include "reflection/Reflection.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace aige {

// Lua modules (development_plan.md 4.8): a project adds components and
// commands without C++. Each `<project>/modules/<name>/module.json` declares
//
//   {
//     "description": "...",
//     "script": "module.lua",                      // optional: the commands' code
//     "components": [{"name": "Lantern", "description": "...",
//                     "fields": [{"name": "radius", "type": "float", "default": 3,
//                                 "min": 0.5, "max": 20, "description": "..."}]}],
//     "commands": [{"name": "lantern.setAll", "description": "...",
//                   "params": [{"name": "lit", "type": "bool"}], "function": "set_all"}]
//   }
//
// Field types: float, int, bool, string, enum (with "options"), texture,
// audio, script, prefab; command parameters may also be "entity" or
// "component". Everything joins the reflection registry, so the Inspector,
// scenes and prefabs, Lua's get_field/set_field and the AI see module
// components and commands like built-in ones.
//
// A command runs `function(args)` from the module's script, with `args`
// keyed by parameter name. In that script: get_field(id, component, field),
// set_field(id, component, field, value) -> true when set (validated),
// entities_with(component) -> ids, log(text). Returning false (or
// false, "why") or raising an error fails the command.

// Where module components keep their values: component -> field -> value.
// Bookkeeping, not registered; each module component is.
struct ModuleData {
    std::map<std::string, std::map<std::string, FieldValue>> values;
};

struct LoadedModule {
    std::string name; // the folder name
    std::string description;
    std::vector<std::string> components;
    std::vector<std::string> commands;
};

struct ModuleLoadResult {
    std::vector<LoadedModule> modules;
    std::vector<std::string> errors; // "modules/x/module.json: ..." — the rest still loads
};

// Registers every module under `projectRoot/modules`. Call once, before
// loading scenes (so their module components are understood). A name that
// is already registered is refused.
ModuleLoadResult LoadModules(ReflectionRegistry& reflection, const std::filesystem::path& projectRoot);

// What the last LoadModules call in this process did (for hosts to report).
const ModuleLoadResult& LastModuleLoad();

} // namespace aige
