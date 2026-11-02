#pragma once

#include <nlohmann/json.hpp>

#include "gameplay/Items.h"

#include <optional>
#include <string>
#include <vector>

namespace aige {

// Recipes (docs/systems/actions.md §6): items made from items, read from the `recipe` records. Two items combine by
// using one on the other; a recipe of more, or one tied to a station, is made from the Crafting list.
struct Recipe {
    std::string id;
    std::string name;
    std::vector<ItemStack> inputs; // taken
    std::vector<std::string> keeps; // needed, not used up (a knife)
    std::vector<ItemStack> gives;
    std::string at; // a station ("stove": an Action with that station); empty: anywhere
    float seconds = 0.0f; // how long it takes: what it gives comes after this
    std::string clip;     // the clip the player plays
    std::string effect;   // an effect played on the player
    std::string text;     // shown when it's made
    std::string conditions; // "if": JSON, as dialogue's
    std::string doEffects;  // "do": JSON, as dialogue's: run when it's made
    std::string doStart;    // "start": run when it's begun (with seconds)
    std::string doInterrupted; // "interrupted": run if it's cut short (the player hit, a cutscene): its inputs are given back
};

Recipe RecipeFromJson(const std::string& id, const nlohmann::json& data);
// The active records', sorted by id.
std::vector<Recipe> AllRecipes();
std::optional<Recipe> FindRecipe(const std::string& id);
// The recipe made of exactly these items (in any order, what it keeps among them), at that station.
std::optional<Recipe> MatchRecipe(const std::vector<std::string>& items, const std::string& station);
// What's missing from the inventory, as words ("2 Flour, Knife"); empty: nothing.
std::string RecipeMissing(const Recipe& recipe, const Inventory* inventory);

} // namespace aige
