#include "game/PlayWindows.h"

#include "gameplay/Attributes.h"
#include "gameplay/Components.h"
#include "gameplay/Crafting.h"
#include "gameplay/GameSession.h"
#include "gameplay/Interact.h"
#include "gameplay/Items.h"
#include "gameplay/Localization.h"
#include "gameplay/Magic.h"
#include "records/Records.h"

#include <algorithm>
#include <cmath>

namespace aige {

namespace {

const nlohmann::json* Item(const std::string& id) {
    const Record* record = ActiveRecords().Find("item", id);
    return record ? &record->resolved : nullptr;
}

bool Equippable(const std::string& id) {
    const nlohmann::json* r = Item(id);
    return r && r->value("slot", std::string("none")) != "none";
}

bool Usable(const std::string& id) {
    const nlohmann::json* r = Item(id);
    if (!r || !r->contains("use") || !(*r)["use"].is_object()) return false;
    const nlohmann::json& use = (*r)["use"];
    return use.value("heal", 0.0f) > 0.0f || use.value("mana", 0.0f) > 0.0f || use.value("stamina", 0.0f) > 0.0f ||
           use.value("effect", std::string("none")) != "none" || !use.value("attribute", std::string()).empty();
}

std::string Counted(const std::string& id, int count) {
    return ItemName(id) + (count > 1 ? " x" + std::to_string(count) : "");
}

} // namespace

void PlayWindows::OpenInventory(Entity player) {
    kind_ = Kind::Inventory;
    player_ = player;
    merchant_ = entt::null;
    highlighted_ = 0;
}

void PlayWindows::OpenShop(Registry& registry, Entity player, Entity merchant, int day, int level, std::mt19937& rng) {
    auto* shop = registry.try_get<Merchant>(merchant);
    if (!shop) return;
    if (!shop->stock.empty() && (shop->items.empty() || day > shop->restockDay)) {
        const LootResult loot = RollLoot(shop->stock, level, rng);
        auto items = ParseItems(shop->items);
        for (const auto& stack : loot.items) {
            auto it = std::find_if(items.begin(), items.end(), [&](const ItemStack& s) { return s.item == stack.item; });
            if (it != items.end()) it->count = std::max(it->count, stack.count);
            else items.push_back(stack);
        }
        shop->items = FormatItems(items);
        shop->gold += loot.gold;
        shop->restockDay = day;
    }
    kind_ = Kind::Shop;
    player_ = player;
    merchant_ = merchant;
    highlighted_ = 0;
}

void PlayWindows::OpenJournal(Entity player, const StoryState& story, const StoryLibrary& library) {
    kind_ = Kind::Journal;
    player_ = player;
    highlighted_ = 0;
    journal_.clear();
    std::vector<JournalQuest> finished;
    // Newest first within each group.
    const auto& order = story.StartedOrder();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const Quest* quest = library.FindQuest(*it);
        if (quest == nullptr) continue;
        const auto state = story.Quest(*it).state;
        JournalQuest entry;
        entry.label = Tr("quest/" + quest->id + "/name", quest->name);
        if (state == StoryState::QuestState::Completed) entry.label += " (" + Tr("journal.done", "done") + ")";
        if (state == StoryState::QuestState::Failed) entry.label += " (" + Tr("journal.failed", "failed") + ")";
        std::string body;
        if (!quest->description.empty()) body = Tr("quest/" + quest->id + "/description", quest->description) + "\n";
        for (const auto& line : story.Journal()) {
            if (line.quest == quest->id) body += "- " + line.text + "\n";
        }
        for (const QuestObjective* o : story.OpenObjectives(quest->id, library)) {
            body += "[ ] " + Tr("quest/" + quest->id + "/" + o->id, o->text) +
                    (o->optional ? " (" + Tr("journal.optional", "optional") + ")" : std::string()) + "\n";
        }
        entry.body = body;
        (state == StoryState::QuestState::Active ? journal_ : finished).push_back(std::move(entry));
    }
    for (auto& entry : finished) journal_.push_back(std::move(entry));
}

void PlayWindows::OpenAchievements(Entity player, const nlohmann::json& view) {
    kind_ = Kind::Achievements;
    player_ = player;
    highlighted_ = 0;
    journal_.clear();
    for (const auto& a : view.value("achievements", nlohmann::json::array())) {
        std::string label = (a.value("unlocked", false) ? "* " : "  ") + a.value("name", "");
        if (!a.value("unlocked", false) && a.value("count", 1) > 1) {
            label += "  (" + std::to_string(a.value("progress", 0)) + "/" + std::to_string(a.value("count", 1)) + ")";
        }
        journal_.push_back({label, a.value("description", "")});
    }
    achievementPoints_ = std::to_string(view.value("earned", 0)) + " / " + std::to_string(view.value("points", 0));
}

void PlayWindows::OpenSpellbook(Entity player) {
    kind_ = Kind::Spellbook;
    player_ = player;
    highlighted_ = 0;
}

std::vector<PlayWindows::Line> PlayWindows::Lines(Registry& registry) const {
    std::vector<Line> lines;
    if (kind_ == Kind::Spellbook) {
        if (const auto* a = registry.valid(player_) ? registry.try_get<Abilities>(player_) : nullptr) {
            const auto slots = SplitList(a->slots);
            for (const auto& id : SplitList(a->known)) {
                auto ability = FindAbility(id);
                std::string label = ability ? ability->name : id;
                if (ability && ability->cost > 0) {
                    char cost[48];
                    std::snprintf(cost, sizeof(cost), "  (%g %s)", ability->cost, ability->costAttribute.c_str());
                    label += cost;
                }
                auto at = std::find(slots.begin(), slots.end(), id);
                if (at != slots.end()) label = "[" + std::to_string(at - slots.begin() + 1) + "] " + label;
                lines.push_back({"slot:" + id, label, ability && ability->activation != "passive"});
            }
        }
        lines.push_back({"close", Tr("menu.close", "Close")});
        return lines;
    }
    if (kind_ == Kind::Journal || kind_ == Kind::Achievements) {
        for (size_t i = 0; i < journal_.size(); ++i) lines.push_back({"quest:" + std::to_string(i), journal_[i].label});
        lines.push_back({"close", Tr("menu.close", "Close")});
        return lines;
    }
    if (!registry.valid(player_)) return lines;
    const auto* inventory = registry.try_get<Inventory>(player_);
    if (kind_ == Kind::Crafting) {
        if (session_) {
            for (const Recipe& recipe : AllRecipes()) {
                if (!recipe.at.empty() && recipe.at != craftStation_) continue;
                const auto uses = [&](const std::string& item) {
                    return std::any_of(recipe.inputs.begin(), recipe.inputs.end(), [&](const ItemStack& s) { return s.item == item; }) ||
                           std::find(recipe.keeps.begin(), recipe.keeps.end(), item) != recipe.keeps.end();
                };
                if (!craftItem_.empty() && !uses(craftItem_)) continue;
                lines.push_back({"craft:" + recipe.id, recipe.name, session_->CraftProblem(recipe.id, craftStation_).empty()});
            }
        }
        lines.push_back({"close", Tr("menu.close", "Close")});
        return lines;
    }
    if (kind_ == Kind::Inventory && !itemMenu_.empty()) {
        if (Equippable(itemMenu_)) lines.push_back({"equip:" + itemMenu_, Tr("inventory.equip", "equip")});
        else if (Usable(itemMenu_)) lines.push_back({"use:" + itemMenu_, Tr("inventory.use", "use")});
        lines.push_back({"hold:" + itemMenu_, Tr("inventory.useOn", "Use on...")});
        lines.push_back({"crafting:" + itemMenu_, Tr("inventory.crafting", "Crafting")});
        lines.push_back({"back", Tr("menu.back", "Back")});
        return lines;
    }
    const std::string held = session_ && kind_ == Kind::Inventory ? session_->HeldItem() : std::string();
    if (!held.empty()) lines.push_back({"put", Tr("inventory.putBack", "Put back") + " " + ItemName(held)});
    if (kind_ == Kind::Inventory) {
        if (const auto* equipment = registry.try_get<Equipment>(player_)) {
            auto copy = *equipment;
            for (const auto& slot : EquipmentSlots()) {
                const std::string* item = EquipmentSlot(copy, slot);
                if (item && !item->empty()) {
                    lines.push_back({"unequip:" + slot, "[" + Tr("slot." + slot, slot) + "] " + ItemName(*item)});
                }
            }
        }
        if (inventory) {
            for (const auto& stack : ParseItems(inventory->items)) {
                std::string hint = Equippable(stack.item) ? "  - " + Tr("inventory.equip", "equip")
                                   : Usable(stack.item)   ? "  - " + Tr("inventory.use", "use")
                                                          : "";
                // With an item in hand: this one combined with it. Nothing to do with it itself: taken in hand.
                const bool does = Equippable(stack.item) || Usable(stack.item);
                if (!held.empty()) lines.push_back({"combine:" + stack.item, Counted(stack.item, stack.count)});
                else if (!does && session_) lines.push_back({"hold:" + stack.item, Counted(stack.item, stack.count)});
                else lines.push_back({Equippable(stack.item) ? "equip:" + stack.item : "use:" + stack.item,
                                      Counted(stack.item, stack.count) + hint, does});
            }
        }
    } else if (kind_ == Kind::Shop && registry.valid(merchant_)) {
        const auto* shop = registry.try_get<Merchant>(merchant_);
        if (shop) {
            const int gold = inventory ? inventory->gold : 0;
            for (const auto& stack : ParseItems(shop->items)) {
                const int price = ItemPrice(stack.item, shop->buyRate);
                lines.push_back({"buy:" + stack.item,
                                 Tr("shop.buy", "Buy") + "  " + Counted(stack.item, stack.count) + "  " + std::to_string(price) + " " +
                                     Tr("shop.gold", "gold"),
                                 gold >= price});
            }
            if (inventory) {
                for (const auto& stack : ParseItems(inventory->items)) {
                    const int price = std::max(1, ItemPrice(stack.item, shop->sellRate));
                    lines.push_back({"sell:" + stack.item,
                                     Tr("shop.sell", "Sell") + "  " + Counted(stack.item, stack.count) + "  " +
                                         std::to_string(price) + " " + Tr("shop.gold", "gold"),
                                     shop->gold >= price});
                }
            }
        }
    }
    lines.push_back({"close", Tr("menu.close", "Close")});
    return lines;
}

void PlayWindows::Choose(Registry& registry, int index) {
    const auto lines = Lines(registry);
    if (index < 0 || index >= static_cast<int>(lines.size()) || !lines[index].enabled) return;
    const std::string action = lines[index].action;
    const auto colon = action.find(':');
    const std::string verb = action.substr(0, colon);
    const std::string what = colon == std::string::npos ? std::string() : action.substr(colon + 1);
    if (verb == "close") {
        Close();
        return;
    }
    if (verb == "back") {
        itemMenu_.clear();
        highlighted_ = 0;
        return;
    }
    if (session_ && verb == "put") {
        session_->PutBackItem();
        return;
    }
    if (session_ && verb == "hold") {
        session_->HoldItem(what);
        itemMenu_.clear();
        highlighted_ = 0;
        return;
    }
    if (session_ && verb == "combine") {
        message_ = session_->CombineHeldWith(what);
        highlighted_ = std::min(highlighted_, static_cast<int>(Lines(registry).size()) - 1);
        return;
    }
    if (verb == "crafting") {
        const Entity player = player_;
        itemMenu_.clear();
        OpenCrafting(player, "", what);
        return;
    }
    if (session_ && verb == "craft") {
        const auto recipe = FindRecipe(what);
        const std::string why = session_->Craft(what, craftStation_);
        message_ = why.empty() ? (recipe && recipe->seconds > 0.0f ? "Making " : "Made ") + (recipe ? recipe->name : what) : why;
        highlighted_ = std::min(highlighted_, static_cast<int>(Lines(registry).size()) - 1);
        return;
    }
    if (verb == "slot") {
        // To the first quick slot (the others move along; four at most).
        auto& a = registry.get_or_emplace<Abilities>(player_);
        auto slots = SplitList(a.slots);
        slots.erase(std::remove(slots.begin(), slots.end(), what), slots.end());
        slots.insert(slots.begin(), what);
        if (slots.size() > 4) slots.resize(4);
        std::string joined;
        for (const auto& s : slots) joined += (joined.empty() ? "" : ", ") + s;
        a.slots = joined;
        message_ = Tr("spellbook.slotted", "In slot 1: ") + what;
        return;
    }
    std::string why;
    if (verb == "equip") why = Equip(registry, player_, what), message_ = why.empty() ? "Equipped " + ItemName(what) : why;
    else if (verb == "unequip") why = Unequip(registry, player_, what), message_ = why.empty() ? "Took it off" : why;
    else if (verb == "use") why = UseItem(registry, player_, what), message_ = why.empty() ? "Used " + ItemName(what) : why;
    else if (verb == "buy" || verb == "sell") {
        auto& inventory = registry.get_or_emplace<Inventory>(player_);
        auto* shop = registry.try_get<Merchant>(merchant_);
        if (!shop) return;
        Inventory stock{shop->items};
        if (verb == "buy") {
            const int price = ItemPrice(what, shop->buyRate);
            if (inventory.gold < price) message_ = "Not enough gold";
            else if (AddItem(inventory, what, 1) == 0) message_ = "No room to carry it";
            else {
                RemoveItem(stock, what, 1);
                inventory.gold -= price;
                shop->gold += price;
                message_ = "Bought " + ItemName(what);
            }
        } else {
            const int price = std::max(1, ItemPrice(what, shop->sellRate));
            if (shop->gold < price) message_ = "They can't pay for it";
            else if (RemoveItem(inventory, what, 1) == 1) {
                AddItem(stock, what, 1);
                inventory.gold += price;
                shop->gold -= price;
                message_ = "Sold " + ItemName(what);
            }
        }
        shop->items = stock.items;
    }
    // Keep the highlight on a line that still exists.
    highlighted_ = std::min(highlighted_, static_cast<int>(Lines(registry).size()) - 1);
}

void PlayWindows::OpenCrafting(Entity player, const std::string& station, const std::string& item) {
    kind_ = Kind::Crafting;
    player_ = player;
    craftStation_ = station;
    craftItem_ = item;
    highlighted_ = 0;
}

void PlayWindows::OpenItemMenu(Registry& registry, int index) {
    if (kind_ != Kind::Inventory || !session_ || !itemMenu_.empty()) return;
    const auto lines = Lines(registry);
    if (index < 0 || index >= static_cast<int>(lines.size())) return;
    const std::string& action = lines[index].action;
    const auto colon = action.find(':');
    if (colon == std::string::npos) return;
    const std::string verb = action.substr(0, colon);
    if (verb != "equip" && verb != "use" && verb != "hold") return; // an item carried (not what's worn, not a combine)
    itemMenu_ = action.substr(colon + 1);
    highlighted_ = 0;
}

void PlayWindows::OpenAsked(Registry& registry, GameSession& session, Entity player) {
    SetSession(&session);
    if (session.Input().Pressed("inventory")) OpenInventory(player);
    if (session.Input().Pressed("spellbook")) OpenSpellbook(player);
    if (session.Input().Pressed("crafting")) OpenCrafting(player, "");
    if (const auto panel = session.TakePanelRequest()) {
        if (*panel == "inventory") OpenInventory(player);
        else if (*panel == "spellbook") OpenSpellbook(player);
        else if (*panel == "journal" && session.StoryLibraryInUse()) OpenJournal(player, session.Story(), *session.StoryLibraryInUse());
        else if (panel->rfind("crafting", 0) == 0) OpenCrafting(player, panel->size() > 9 ? panel->substr(9) : std::string());
    }
    (void)registry;
}

void PlayWindows::HandleInput(Registry& registry, const InputState& input) {
    if (kind_ == Kind::None) return;
    const auto lines = Lines(registry);
    const int count = static_cast<int>(lines.size());
    if (input.Pressed("ui_up")) highlighted_ = (highlighted_ - 1 + count) % count;
    if (input.Pressed("ui_down")) highlighted_ = (highlighted_ + 1) % count;
    if (input.Pressed("ui_select")) Choose(registry, highlighted_);
    else if (input.Pressed("ui_context")) OpenItemMenu(registry, highlighted_);
    else if (!itemMenu_.empty() && (input.Pressed("ui_back") || input.Pressed("menu"))) itemMenu_.clear(), highlighted_ = 0;
    else if (input.Pressed("ui_back") || input.Pressed("menu") || input.Pressed("inventory") || input.Pressed("journal")) Close();
}

std::optional<MenuView> PlayWindows::View(Registry& registry) const {
    if (kind_ == Kind::None || !registry.valid(player_)) return std::nullopt;
    MenuView view;
    const auto* inventory = registry.try_get<Inventory>(player_);
    const int gold = inventory ? inventory->gold : 0;
    if (kind_ == Kind::Crafting) {
        view.title = Tr("crafting.title", "Crafting");
        // The highlighted recipe: what it uses, and why it can't be made.
        const auto lines = Lines(registry);
        if (highlighted_ >= 0 && highlighted_ < static_cast<int>(lines.size()) && lines[highlighted_].action.rfind("craft:", 0) == 0) {
            const std::string id = lines[highlighted_].action.substr(6);
            if (const auto recipe = FindRecipe(id)) {
                for (const auto& stack : recipe->inputs) view.body += (view.body.empty() ? "" : ", ") + Counted(stack.item, stack.count);
                for (const auto& keep : recipe->keeps) view.body += (view.body.empty() ? "" : ", ") + ItemName(keep);
                if (session_) {
                    if (const std::string why = session_->CraftProblem(id, craftStation_); !why.empty()) view.body += "\n" + why;
                }
            }
        } else if (lines.size() <= 1) {
            view.body = Tr("crafting.empty", "Nothing to make here.");
        }
    } else if (kind_ == Kind::Spellbook) {
        view.title = Tr("spellbook.title", "Abilities");
        view.body = Tr("spellbook.help", "Pick one to put it in quick slot 1 (keys 1-4 use the slots).");
    } else if (kind_ == Kind::Achievements) {
        view.title = Tr("achievements.title", "Achievements") + "  " + achievementPoints_;
        view.body = journal_.empty() ? Tr("achievements.empty", "This game has no achievements.")
                    : highlighted_ < static_cast<int>(journal_.size()) ? journal_[highlighted_].body
                                                                       : std::string();
    } else if (kind_ == Kind::Journal) {
        view.title = Tr("journal.title", "Journal");
        view.body = journal_.empty() ? Tr("journal.empty", "No quests yet.")
                    : highlighted_ < static_cast<int>(journal_.size()) ? journal_[highlighted_].body
                                                                       : std::string();
    } else if (kind_ == Kind::Inventory) {
        view.title = Tr("inventory.title", "Inventory");
        // The character's numbers, as a body line.
        std::string body = Tr("inventory.gold", "Gold") + " " + std::to_string(gold);
        if (const auto* progression = registry.try_get<Progression>(player_)) {
            body += "   " + Tr("inventory.level", "Level") + " " + std::to_string(progression->level) + " (" +
                    std::to_string(static_cast<int>(progression->experience)) + "/" +
                    std::to_string(static_cast<int>(ExperienceForLevel(progression->level))) + ")";
        }
        if (const auto* health = registry.try_get<Health>(player_)) {
            body += "   " + Tr("inventory.health", "Health") + " " + std::to_string(static_cast<int>(std::ceil(health->current))) +
                    "/" + std::to_string(static_cast<int>(health->max));
        }
        if (const auto* stats = registry.try_get<Stats>(player_)) {
            body += "   " + Tr("inventory.attack", "Attack") + " " + std::to_string(static_cast<int>(std::round(stats->attack))) +
                    "   " + Tr("inventory.defense", "Defense") + " " + std::to_string(static_cast<int>(std::round(stats->defense)));
        }
        view.body = body;
        if (!itemMenu_.empty()) view.title = ItemName(itemMenu_);
        if (session_ && !session_->HeldItem().empty()) {
            view.body += "\n" + Tr("inventory.inHand", "In hand:") + " " + ItemName(session_->HeldItem()) + ". " +
                         Tr("inventory.inHandHelp", "Pick another item to combine, or close and use it on something.");
        }
    } else {
        const auto* name = registry.valid(merchant_) ? registry.try_get<Name>(merchant_) : nullptr;
        view.title = name && !name->value.empty() ? name->value : Tr("shop.title", "Shop");
        const auto* shop = registry.valid(merchant_) ? registry.try_get<Merchant>(merchant_) : nullptr;
        view.body = Tr("shop.your_gold", "Your gold") + " " + std::to_string(gold) +
                    (shop ? "   " + Tr("shop.their_gold", "Their gold") + " " + std::to_string(shop->gold) : "");
    }
    for (const auto& line : Lines(registry)) view.items.push_back({line.label, line.enabled});
    view.highlighted = highlighted_;
    view.footer = Tr("menu.footer", "Up/Down to choose, Enter to pick, Esc to go back");
    return view;
}

} // namespace aige
