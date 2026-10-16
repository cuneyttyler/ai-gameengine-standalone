#pragma once

#include "ecs/Registry.h"
#include "gameplay/Isometric.h"

#include <optional>
#include <string>
#include <vector>

namespace aige {

class ReflectionRegistry;

// Scene editing conveniences (development_plan.md 4.3), UI-free like
// SceneEditing so each is testable on a bare registry.

// Makes an entity bigger or smaller: its sprite and its collision boxes
// (Collider2D, Hurtbox) together, through the shared edit path, so on a
// prefab copy it's recorded as an override. Tiles don't scale (they're the
// grid). Returns why not.
std::optional<std::string> ScaleEntity(const ReflectionRegistry& reflection, Registry& registry, Entity entity,
                                       float factor);

// Moves an entity by a world-space step: tiles by whole cells (rounded, and
// not onto another tile), everything else exactly. Returns whether it moved.
bool NudgeEntity(Registry& registry, Entity entity, float dx, float dy, const IsoParams& iso);

// Copy and paste: the entities as scene text, and pasting it so the first
// lands on a cell (the rest keep their offsets). A pasted tile replaces the
// tile already in its cell. Returns the new entities.
std::string CopyEntities(Registry& registry, const std::vector<Entity>& entities);
std::vector<Entity> PasteEntities(Registry& registry, const std::string& text, int gx, int gy);

} // namespace aige
