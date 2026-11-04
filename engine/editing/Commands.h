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

class PrefabLibrary;
struct Project;

// A character prefab from a character sheet (assets/characters/<n>/<n>.character.anim.json): an NPC or the player,
// showing its first idle frame facing the camera, animated by the humanoid animator, about two cells tall. Saved at
// `prefabPath` ("prefabs/<name>.prefab"); `displayName` empty = the sheet's name. Returns why not, or "" when made.
// The editor's Make NPC / Make player and the AI's prefab.createCharacter both make them this way.
std::string CreateCharacterPrefab(PrefabLibrary& prefabs, const Project& project, const std::string& sheetPath,
                                  const std::string& prefabPath, bool npc, const std::string& displayName,
                                  float tileHeight);

} // namespace aige
