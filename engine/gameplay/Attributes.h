#pragma once

#include "ecs/Registry.h"

#include <map>
#include <string>
#include <vector>

namespace aige {

// Attributes (development_plan.md 13.1): one model for a character's numbers.
// The component holds base values; equipment, perks, difficulty and effects add
// modifiers; each step the effective values are worked out and written where
// the rest of the engine reads them (Stats, Health.max, Resources' maxima), so
// combat, movement and scripts that read Stats keep working.
struct Attributes {
    float maxHealth = 10.0f;
    float maxMana = 0.0f;
    float maxStamina = 0.0f;
    float attack = 0.0f;
    float defense = 0.0f;
    float moveSpeed = 3.0f;
    float healthRegen = 0.0f; // per second
    float manaRegen = 1.0f;
    float staminaRegen = 5.0f;
    // Resistances: the share of that kind of damage taken away (0..0.9).
    float resistFire = 0.0f;
    float resistFrost = 0.0f;
    float resistShock = 0.0f;
    float resistPoison = 0.0f;
};

// Mana and stamina now (health is Health.current); the maxima are effective values.
struct Resources {
    float mana = 0.0f;
    float stamina = 0.0f;
    float maxMana = 0.0f;    // worked out from Attributes
    float maxStamina = 0.0f; // worked out from Attributes
};

enum class ModifierOp {
    Add,
    Multiply, // 1.1 = +10%
};

struct AttributeModifier {
    std::string attribute; // an Attributes field name
    ModifierOp op = ModifierOp::Add;
    float value = 0.0f;
    std::string source;      // "equip:weapon", "perk:tough", "effect:..." — what removes it
    float remaining = -1.0f; // seconds; < 0: until removed
};

// Timed and lasting modifiers on an entity (not saved with the scene: they come
// back from equipment and perks; timed ones are short).
struct AttributeModifiers {
    std::vector<AttributeModifier> list;
};

// The attribute names, in the order shown.
const std::vector<std::string>& AttributeNames();

// One attribute's effective value: base, plus every Add, times every Multiply,
// from the entity's modifiers and its equipment's and perks' (records).
float EffectiveAttribute(Registry& registry, Entity entity, const std::string& name);

void AddModifier(Registry& registry, Entity entity, AttributeModifier modifier);
void RemoveModifiers(Registry& registry, Entity entity, const std::string& source);

// One fixed step: timed modifiers run down, effective values are written to
// Stats, Health.max and Resources, and health, mana and stamina regenerate.
void UpdateAttributes(Registry& registry, float dt);

// Progression (13.2): experience and levels. The curve and what each level
// gives come from the progression record "default" (records/progression/),
// else built-in defaults.
struct Progression {
    int level = 1;
    float experience = 0.0f; // towards the next level
    int perkPoints = 0;
    std::string perks; // taken perks' record ids, comma-separated
};

// What killing an entity gives (13.2, 13.4): experience to its killer, gold and
// loot dropped where it fell.
struct Rewards {
    float experience = 0.0f;
    int goldMin = 0;
    int goldMax = 0;
    std::string loot; // a loot record id
};

// Experience needed to go from `level` to the next.
float ExperienceForLevel(int level);
// Gives experience; returns how many levels were gained (applying each level's
// gains). Emits nothing itself: the caller reports level_up.
int AwardExperience(Registry& registry, Entity entity, float amount);
// Takes a perk (records/perk/<id>): checks level and points; its modifiers apply
// from then on. The reason it can't, else "".
std::string TakePerk(Registry& registry, Entity entity, const std::string& perk);

} // namespace aige
