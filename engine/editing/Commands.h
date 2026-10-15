#pragma once

#include "reflection/Reflection.h"

namespace aige {

// Registers the editor's commands — the actions (as opposed to field edits)
// that the AI's proposals and future automation can invoke, each with typed,
// validated parameters:
//
//   scene.spawn(prefab, x, y)                 place a prefab copy
//   scene.paintRect(prefab, x0, y0, x1, y1)   fill a rectangle of cells with a tile prefab
//   entity.delete(entity)
//   prefab.createFromEntity(entity, name)     save an entity as prefabs/<name>.prefab
//   script.write(path, source)                create or replace a Lua script under scripts/
//
// Safe to call more than once (later calls are no-ops).
void RegisterEditingCommands(ReflectionRegistry& registry);

} // namespace aige
