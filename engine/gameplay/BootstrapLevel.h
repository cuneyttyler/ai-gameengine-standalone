#pragma once

#include "ecs/Registry.h"
#include "gameplay/Isometric.h"

#include <string>

namespace aige {

// The 8x8 test level from Phase 0, used whenever no scene file exists yet.
constexpr int kBootstrapGridSize = 8;

// The projection every app uses for now: 2:1 tiles, world origin placed so
// the bootstrap level sits centered in a 1280px-wide view.
// The projection for the active project: the Grid ▸ style setting decides
// between diamond cells (64×32) and square ones (64×64, top-down).
IsoParams BootstrapIso();

// Builds the bootstrap level: an 8x8 tile grid with a wall border and a small
// interior wall, a static NPC, a Lua-scripted NPC running `npcScriptPath`,
// and the player. Returns the player entity.
Entity BuildBootstrapScene(Registry& registry, const std::string& npcScriptPath);

} // namespace aige
