#pragma once

#include "ecs/Registry.h"
#include "gameplay/Items.h"

#include <functional>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace aige {

// Spawners (development_plan.md 13.7) and what falls when something dies (13.4).

using SpawnPrefabFn = std::function<std::optional<Entity>(const std::string& prefab, float x, float y)>;

// One fixed step for every Spawner: the first time, fill up to maxAlive from
// its encounter record; after a death, wait respawnSeconds and make another.
// `isFree(x, y)` keeps them off walls. Returns what was made.
std::vector<Entity> UpdateSpawners(Registry& registry, float dt, const SpawnPrefabFn& spawn,
                                   const std::function<bool(float, float)>& isFree, std::mt19937& rng);

// Picks an entry of an encounter record (records/encounter/<id>) for `level`:
// the prefab and how many.
std::optional<std::pair<std::string, int>> RollEncounter(const std::string& encounter, int level, std::mt19937& rng);

// Leaves items and gold on the ground around (x, y) as ItemPickups, each with
// its item's icon. Returns the pickups.
std::vector<Entity> DropLoot(Registry& registry, float x, float y, const std::vector<ItemStack>& items, int gold,
                             std::mt19937& rng);

} // namespace aige
