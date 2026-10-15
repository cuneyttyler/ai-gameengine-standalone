#pragma once

#include "ecs/Registry.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Isometric.h"

#include <cstddef>
#include <optional>

namespace aige {

// The scene-mutation API behind the editor's viewport tools. Deliberately
// free of any UI, windowing or IPC so every operation is unit-testable on a
// bare registry (development_plan.md milestone 1.2). All positions are scene
// pixels (ScreenTransform2D space, before the editor camera).

// The grid cell under a scene point.
void CellAt(float sceneX, float sceneY, const IsoParams& iso, int& gx, int& gy);

// What a click at a scene point hits: the frontmost non-tile sprite whose
// rect contains the point, else the tile in the cell under it. Tiles are
// picked by cell rather than by sprite rect because neighboring diamonds'
// bounding boxes overlap.
std::optional<Entity> PickEntity(Registry& registry, float sceneX, float sceneY, const IsoParams& iso);

// Places a tile, replacing whatever tile already occupied the cell.
Entity PaintTile(Registry& registry, int gx, int gy, TileKind kind);
Entity PaintTile(Registry& registry, int gx, int gy, const TileStyle& style);

enum class CharacterKind {
    Npc,
    Player,
};

// Places a character at the cell's center. There is only ever one player:
// placing a player when one exists moves the existing one instead.
Entity PlaceCharacter(Registry& registry, CharacterKind kind, int gx, int gy);

void DeleteEntity(Registry& registry, Entity entity);

// Copies every component onto a new entity, offset one cell along +x so the
// copy is visible. Tiles aren't duplicated (a cell holds one tile); returns
// nullopt for them.
std::optional<Entity> DuplicateEntity(Registry& registry, Entity entity);

// Moves an entity by the drag from one scene point to another. Tiles move by
// whole cells and refuse to land on another tile; characters move by the
// world-space delta; anything else by the scene-pixel delta. Returns whether
// the entity actually moved.
bool DragEntity(Registry& registry, Entity entity, float fromX, float fromY, float toX, float toY,
                const IsoParams& iso);

// An entity's position in EntitiesInOrder, and back — stable across
// SerializeScene/DeserializeScene, unlike the entity id itself.
std::optional<size_t> IndexOfEntity(Registry& registry, Entity entity);
std::optional<Entity> EntityAtIndex(Registry& registry, size_t index);

} // namespace aige
