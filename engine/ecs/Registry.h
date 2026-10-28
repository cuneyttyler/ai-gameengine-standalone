#pragma once

#include <entt/entt.hpp>

#include <algorithm>
#include <vector>

namespace aige {

// Thin naming seam over EnTT (technical.md §6): the rest of the engine
// depends on aige::Registry/Entity rather than entt:: directly, so the ECS
// backend can change — or grow a reflection-aware wrapper in Phase 2 — without
// a project-wide rename.
using Registry = entt::registry;
using Entity = entt::entity;

// Every live entity in storage order (EnTT's all-entities view iterates it
// in reverse). Storage order equals creation order for a freshly loaded
// registry, and the scene writer uses this order, so an entity's index here
// survives a save/load round trip even though its id does not — which is
// how the editor keeps the selection across undo and Play/Stop.
// Made at run time from something else (a sword shown from Equipment): never saved, never in the order.
struct Transient {};

inline std::vector<Entity> EntitiesInOrder(Registry& registry) {
    std::vector<Entity> entities;
    for (auto entity : registry.view<Entity>()) {
        if (!registry.all_of<Transient>(entity)) entities.push_back(entity);
    }
    std::reverse(entities.begin(), entities.end());
    return entities;
}

} // namespace aige
