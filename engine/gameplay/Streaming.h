#pragma once

#include "ecs/Dormant.h"
#include "ecs/Registry.h"

namespace aige {

// Streaming of big scenes (development_plan.md 24): the scene is cut into square chunks;
// only the chunks around the player are simulated. Every entity that thinks — scripts,
// characters, routines, spawners — outside them is Dormant (and wakes, where it was, when
// the player comes near). Project Settings ▸ Streaming: on, the chunk size, how many chunks
// around the player stay awake. Returns how many entities are dormant.
struct StreamingStats {
    int awake = 0, dormant = 0, chunks = 0;
};
StreamingStats UpdateStreaming(Registry& registry, float playerX, float playerY, int chunk, int radius);
// Wakes everything (streaming turned off, a scene change).
void WakeAll(Registry& registry);

} // namespace aige
