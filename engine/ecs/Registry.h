#pragma once

#include <entt/entt.hpp>

namespace aige {

// Thin naming seam over EnTT (technical.md §6): the rest of the engine
// depends on aige::Registry/Entity rather than entt:: directly, so the ECS
// backend can change — or grow a reflection-aware wrapper in Phase 2 — without
// a project-wide rename.
using Registry = entt::registry;
using Entity = entt::entity;

} // namespace aige
