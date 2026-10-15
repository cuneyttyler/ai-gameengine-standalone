#pragma once

#include "reflection/Reflection.h"

#include <optional>
#include <string>

namespace aige {

// The engine's reflection registry, with every built-in component and
// setting registered (RegisterGameplayTypes). Process-wide; built on first
// use. Editor commands are added on top by RegisterEditingCommands.
ReflectionRegistry& EngineReflection();

// Registers every gameplay component and the project settings. Exposed for
// tests that want a fresh registry.
void RegisterGameplayTypes(ReflectionRegistry& registry);

// Shorthands over EngineReflection() for the many call sites that only ever
// use the engine's registry.
inline const std::vector<ComponentInfo>& Components() {
    return EngineReflection().Components();
}
inline const ComponentInfo* FindComponent(const std::string& name) {
    return EngineReflection().FindComponent(name);
}
inline bool AddComponent(Registry& registry, Entity entity, const std::string& name) {
    return AddComponent(EngineReflection(), registry, entity, name);
}
inline std::optional<std::string> RemoveComponent(Registry& registry, Entity entity, const std::string& name) {
    return RemoveComponent(EngineReflection(), registry, entity, name);
}

} // namespace aige
