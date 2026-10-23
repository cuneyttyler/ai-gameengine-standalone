#pragma once

#include <nlohmann/json.hpp>

#include "ecs/Registry.h"
#include "gameplay/InputMap.h"
#include "gameplay/Story.h"
#include "ui/GameUi.h"

#include <optional>
#include <random>
#include <string>
#include <vector>

namespace aige {

// The windows over play (development_plan.md 13.3–13.4): the inventory (use,
// equip, unequip, with the character's numbers) and a merchant's shop (buy and
// sell), and the quest journal (15.4). The engine's built-in look until the game
// UI (Phase 17) makes them customisable documents. The game is paused while one is open.
class PlayWindows {
public:
    enum class Kind { None, Inventory, Shop, Journal, Spellbook, Achievements };

    void OpenInventory(Entity player);
    // Restocks the merchant from its loot table when it's a new day.
    void OpenShop(Registry& registry, Entity player, Entity merchant, int day, int level, std::mt19937& rng);
    // The journal: every quest started, active ones first; the highlighted one's
    // stages so far and what's left to do.
    void OpenJournal(Entity player, const StoryState& story, const StoryLibrary& library);
    // The spellbook (19): known abilities; picking one puts it in the first quick slot.
    void OpenSpellbook(Entity player);
    // The game's achievements (24): AchievementSystem::View().
    void OpenAchievements(Entity player, const nlohmann::json& view);
    void Close() { kind_ = Kind::None; }
    Kind Open() const { return kind_; }

    // Menu input for this frame ("menu" context; inventory or Esc closes).
    void HandleInput(Registry& registry, const InputState& input);
    // The same, by index (tests).
    void Choose(Registry& registry, int index);

    std::optional<MenuView> View(Registry& registry) const;
    // The last thing that happened ("Bought Iron sword", "Not enough gold"), once.
    std::string TakeMessage() {
        std::string m;
        m.swap(message_);
        return m;
    }

private:
    struct Line {
        std::string action; // equip:<item>, use:<item>, unequip:<slot>, buy:<item>, sell:<item>, close
        std::string label;
        bool enabled = true;
    };
    std::vector<Line> Lines(Registry& registry) const;

    Kind kind_ = Kind::None;
    std::string achievementPoints_;
    Entity player_ = entt::null;
    Entity merchant_ = entt::null;
    int highlighted_ = 0;
    std::string message_;
    struct JournalQuest {
        std::string label; // "The hunt", "The hunt (done)"
        std::string body;  // its journal lines and open objectives
    };
    std::vector<JournalQuest> journal_;
};

} // namespace aige
