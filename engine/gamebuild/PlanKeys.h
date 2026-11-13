#pragma once

#include "gameplay/InputMap.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace aige {

// The keys the AI Planner's features ask for, given to the game (development_plan.md 32.7): each planned action
// {name, key, group, nearOnly, screen} added to the input map, or its key changed when it asks; a key another
// action of the same context already has (and doesn't share with it as a group) goes to a free one instead.
// Returns what was done, a line each ("cook: G", "dodge: asked for E, taken by interact: R").
std::vector<std::string> ApplyPlannedActions(InputMap& map, const nlohmann::json& actions);

// A key as the AI writes it ("1", "shift", "space", "lmb", "e") in the engine's names ("Num1", "LeftShift", ...).
std::string NormaliseKeyName(std::string key);

// Pairs of actions of one context bound to the same key and not grouped together ("attack/use: E"), one of them
// not the engine's own (whose defaults share keys by design).
// ⑦ Systems ▸ Input's design ({actions: [{name, also, description, context, keys, pad, screen}]}) put into the map as it
// is: each action (and the names it's "also" known by) with exactly its keys; what was done, a line each.
std::vector<std::string> ApplyDesignedInput(InputMap& map, const nlohmann::json& design);
std::vector<std::string> KeyClashes(const InputMap& map);

} // namespace aige
