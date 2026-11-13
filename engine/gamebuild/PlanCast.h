#pragma once

#include "ecs/Registry.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace aige {

// The AI Planner's characters made real (development_plan.md 32.2-32.4): a character's class (a
// character-class record ⑦ wrote), level and part in the story give its attributes, how it fights,
// what it carries and wears, what it sells and what killing it gives.

// ⑦'s records, by type and id (before they're written to the project).
class PlanRecords {
public:
    PlanRecords() = default;
    explicit PlanRecords(const nlohmann::json& systems);
    const nlohmann::json* Find(const std::string& type, const std::string& id) const;
    std::vector<std::string> Ids(const std::string& type) const;

private:
    std::map<std::string, std::map<std::string, nlohmann::json>> byType_;
};

enum class CastKind { Civilian, Enemy, Boss, Player };

struct CastSpec {
    CastKind kind = CastKind::Civilian;
    int level = 1;
    std::string cls;            // a character-class id (gameplay.class); "" or unknown: the kind's defaults
    nlohmann::json gameplay = nlohmann::json::object(); // the character's document's gameplay {roles, abilities, stock, ...}
    float difficulty = 1.0f;    // enemies' health and damage, times this
    bool fights = true;         // the player: the game has combat
    std::string stock;          // a merchant's stock (a loot id), when its class names none
};

// The class a character is made from: `wanted` when ⑦ wrote it, else one of the same sort (a boss's,
// a fighter's) whose id or name shares a word with it, else "".
std::string ClassFor(const PlanRecords& records, const std::string& wanted, CastKind kind);

// Its numbers, fighting, kit and rewards on the entity (Attributes, Health, Stats, Progression, attacks,
// Abilities, Inventory, Equipment, Merchant, Rewards, Behaviour's temper and speed). What was applied, for
// the build's report: {class, level, maxHealth, attack, defense, moveSpeed, combat, carries, equipment}.
nlohmann::json ApplyCast(Registry& registry, Entity entity, const CastSpec& spec, const PlanRecords& records);

// Items "potion*2" and ids, comma-joined as Inventory.items holds them.
std::string JoinItems(const nlohmann::json& list);

} // namespace aige
