#pragma once

#include "ecs/Registry.h"
#include "gameplay/Isometric.h"

#include <string>

namespace aige {

// The hardcoded 8x8 test level from Phase 0 (formerly private to
// apps/sandbox), shared so the sandbox and the editor's Play mode agree on
// projection and collision. Temporary: once tilemaps carry their own
// collision data (milestone 1.2+), the level stops being code.
constexpr int kBootstrapGridSize = 8;

// Projection the bootstrap scene's baked ScreenTransform2D values were
// authored with (originally centered in the sandbox's 1280px window).
IsoParams BootstrapIso();

// Border ring + one small interior blob are solid; everything else in the
// grid is walkable.
bool IsBootstrapSolidCell(int gridX, int gridY);

// Builds the bootstrap level from scratch: tile grid, a static NPC, a
// Lua-scripted NPC running `npcScriptPath`, and the player. Returns the
// player entity.
Entity BuildBootstrapScene(Registry& registry, const std::string& npcScriptPath);

} // namespace aige
