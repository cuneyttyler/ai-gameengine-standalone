#include "gameplay/Crafting.h"

#include "gameplay/Interact.h"
#include "records/Records.h"

#include <algorithm>
#include <set>

namespace aige {

namespace {

std::vector<ItemStack> StacksFrom(const nlohmann::json& list) {
    std::vector<ItemStack> out;
    if (!list.is_array()) return out;
    for (const auto& entry : list) {
        if (!entry.is_object() || entry.value("item", std::string()).empty()) continue;
        out.push_back({entry.value("item", std::string()), std::max(1, entry.value("count", 1))});
    }
    return out;
}

} // namespace

Recipe RecipeFromJson(const std::string& id, const nlohmann::json& data) {
    Recipe recipe;
    recipe.id = id;
    recipe.name = data.value("name", id);
    recipe.inputs = StacksFrom(data.value("inputs", nlohmann::json::array()));
    recipe.gives = StacksFrom(data.value("gives", nlohmann::json::array()));
    for (const auto& keep : data.value("keeps", nlohmann::json::array())) {
        if (keep.is_string() && !keep.get<std::string>().empty()) recipe.keeps.push_back(keep.get<std::string>());
    }
    recipe.at = data.value("at", std::string());
    recipe.seconds = data.value("seconds", 0.0f);
    recipe.clip = data.value("clip", std::string());
    recipe.effect = data.value("effect", std::string());
    recipe.text = data.value("text", std::string());
    recipe.conditions = data.value("if", std::string());
    recipe.doEffects = data.value("do", std::string());
    return recipe;
}

std::vector<Recipe> AllRecipes() {
    std::vector<Recipe> out;
    for (const Record* record : ActiveRecords().OfType("recipe")) out.push_back(RecipeFromJson(record->id, record->resolved));
    return out;
}

std::optional<Recipe> FindRecipe(const std::string& id) {
    const Record* record = ActiveRecords().Find("recipe", id);
    if (!record) return std::nullopt;
    return RecipeFromJson(record->id, record->resolved);
}

std::optional<Recipe> MatchRecipe(const std::vector<std::string>& items, const std::string& station) {
    const std::set<std::string> given(items.begin(), items.end());
    for (const Recipe& recipe : AllRecipes()) {
        if (recipe.at != station) continue;
        std::set<std::string> needs(recipe.keeps.begin(), recipe.keeps.end());
        for (const auto& stack : recipe.inputs) needs.insert(stack.item);
        if (needs == given) return recipe;
    }
    return std::nullopt;
}

std::string RecipeMissing(const Recipe& recipe, const Inventory* inventory) {
    std::string missing;
    auto add = [&](const std::string& item, int count) {
        if (inventory && CountItem(*inventory, item) >= count) return;
        missing += (missing.empty() ? "" : ", ") + (count > 1 ? std::to_string(count) + " " : std::string()) + ItemName(item);
    };
    for (const auto& stack : recipe.inputs) add(stack.item, stack.count);
    for (const auto& keep : recipe.keeps) add(keep, 1);
    return missing;
}

} // namespace aige
