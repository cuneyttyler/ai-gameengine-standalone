#pragma once

#include "ecs/Registry.h"

namespace aige {

// The performance benchmark (development_plan.md 4.6): a `side` x `side`
// field of ground tiles with a wall ring, the player in the middle, and
// `characters` melee enemies spread over it (their default script, so the
// update cost includes Lua). 100 x 100 and 500 is the plan's target.
void BuildBenchmarkScene(Registry& registry, int side = 100, int characters = 500);

} // namespace aige
