#pragma once

#include "ecs/Registry.h"

#include <optional>
#include <random>
#include <string>
#include <vector>

namespace aige {

class StoryState;

// Using things (development_plan.md 13.5): doors, locks and keys, levers, signs,
// chests, merchants and items picked up with the interact action. The entity's
// components decide what happens; GameSession calls these for the player.

// The nearest thing the player can use within its range, or nothing. Talk
// entities are the session's own (TalkTarget).
std::optional<Entity> FindInteractable(Registry& registry, Entity player);

// What it says on the prompt ("Open Chest", "Read Sign", "Trade with Mira").
std::string InteractPrompt(Registry& registry, Entity target);

struct InteractResult {
    std::string message;     // shown as a notice ("Locked: needs the Iron Key", "Took 12 gold")
    std::string text;        // a readable's text, shown longer
    std::string event;       // the Interactable's event, for scripts
    bool openShop = false;   // a Merchant: the host opens its shop
    bool used = false;       // something happened
};

// Uses `target` as `player`: unlocks with a key, opens or closes a door, takes a
// container's contents (rolled from its loot the first time), picks up an item,
// sets the flag, and reports what happened.
InteractResult Interact(Registry& registry, Entity player, Entity target, StoryState& story, int level,
                        std::mt19937& rng);

// Picks up every walk-over ItemPickup the player stands on; returns notices
// ("Iron sword", "12 gold").
std::vector<std::string> CollectItemPickups(Registry& registry, Entity player);

// The item's name from its record (or the id).
std::string ItemName(const std::string& item);

} // namespace aige
