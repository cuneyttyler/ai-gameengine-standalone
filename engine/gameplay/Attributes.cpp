#include "gameplay/Attributes.h"

#include "gameplay/Components.h"
#include "gameplay/Items.h"
#include "records/Records.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace aige {

namespace {

float* Field(Attributes& a, const std::string& name) {
    if (name == "maxHealth") return &a.maxHealth;
    if (name == "maxMana") return &a.maxMana;
    if (name == "maxStamina") return &a.maxStamina;
    if (name == "attack") return &a.attack;
    if (name == "defense") return &a.defense;
    if (name == "moveSpeed") return &a.moveSpeed;
    if (name == "healthRegen") return &a.healthRegen;
    if (name == "manaRegen") return &a.manaRegen;
    if (name == "staminaRegen") return &a.staminaRegen;
    if (name == "resistFire") return &a.resistFire;
    if (name == "resistFrost") return &a.resistFrost;
    if (name == "resistShock") return &a.resistShock;
    if (name == "resistPoison") return &a.resistPoison;
    return nullptr;
}

std::vector<std::string> SplitIds(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream in(text);
    std::string part;
    while (std::getline(in, part, ',')) {
        part.erase(0, part.find_first_not_of(' '));
        part.erase(part.find_last_not_of(' ') + 1);
        if (!part.empty()) out.push_back(part);
    }
    return out;
}

// Modifiers a record carries: [{"attribute", "op": "add"|"multiply", "value"}].
void CollectRecordModifiers(const nlohmann::json& record, const std::string& name, float& add, float& mul) {
    for (const auto& m : record.value("modifiers", nlohmann::json::array())) {
        if (!m.is_object() || m.value("attribute", "") != name) continue;
        if (m.value("op", std::string("add")) == "multiply") mul *= m.value("value", 1.0f);
        else add += m.value("value", 0.0f);
    }
}

nlohmann::json ProgressionRecord() {
    if (const Record* r = ActiveRecords().Find("progression", "default")) return r->resolved;
    return nlohmann::json::object();
}

} // namespace

const std::vector<std::string>& AttributeNames() {
    static const std::vector<std::string> names = {"maxHealth", "maxMana",      "maxStamina", "attack",      "defense",
                                                   "moveSpeed", "healthRegen",  "manaRegen",  "staminaRegen", "resistFire",
                                                   "resistFrost", "resistShock", "resistPoison"};
    return names;
}

float EffectiveAttribute(Registry& registry, Entity entity, const std::string& name) {
    auto* attributes = registry.try_get<Attributes>(entity);
    if (!attributes) return 0.0f;
    float* base = Field(*attributes, name);
    if (!base) return 0.0f;
    float add = 0.0f, mul = 1.0f;
    if (const auto* modifiers = registry.try_get<AttributeModifiers>(entity)) {
        for (const auto& m : modifiers->list) {
            if (m.attribute != name) continue;
            if (m.op == ModifierOp::Multiply) mul *= m.value;
            else add += m.value;
        }
    }
    // Equipment: each item's modifiers; a weapon's damage adds to attack, armour to defense.
    if (auto* equipment = registry.try_get<Equipment>(entity)) {
        for (const auto& slot : EquipmentSlots()) {
            const std::string* item = EquipmentSlot(*equipment, slot);
            const Record* record = item && !item->empty() ? ActiveRecords().Find("item", *item) : nullptr;
            if (!record) continue;
            CollectRecordModifiers(record->resolved, name, add, mul);
            if (name == "attack") add += record->resolved.value("damage", 0.0f);
            if (name == "defense") add += record->resolved.value("armor", 0.0f);
        }
    }
    // Perks taken.
    if (const auto* progression = registry.try_get<Progression>(entity)) {
        for (const auto& perk : SplitIds(progression->perks)) {
            if (const Record* record = ActiveRecords().Find("perk", perk)) CollectRecordModifiers(record->resolved, name, add, mul);
        }
    }
    return (*base + add) * mul;
}

void AddModifier(Registry& registry, Entity entity, AttributeModifier modifier) {
    auto& modifiers = registry.get_or_emplace<AttributeModifiers>(entity);
    // The same source and attribute is refreshed, not stacked.
    for (auto& m : modifiers.list) {
        if (m.source == modifier.source && m.attribute == modifier.attribute && !modifier.source.empty()) {
            m = modifier;
            return;
        }
    }
    modifiers.list.push_back(std::move(modifier));
}

void RemoveModifiers(Registry& registry, Entity entity, const std::string& source) {
    if (auto* modifiers = registry.try_get<AttributeModifiers>(entity)) {
        std::erase_if(modifiers->list, [&](const AttributeModifier& m) { return m.source == source; });
    }
}

void UpdateAttributes(Registry& registry, float dt) {
    for (auto [entity, modifiers] : registry.view<AttributeModifiers>().each()) {
        // Timed ones run down; < 0 lasts until removed.
        std::erase_if(modifiers.list, [dt](AttributeModifier& m) {
            if (m.remaining < 0.0f) return false;
            m.remaining -= dt;
            return m.remaining <= 0.0f;
        });
    }
    for (auto entity : registry.view<Attributes>(entt::exclude<Dead>)) {
        auto& stats = registry.get_or_emplace<Stats>(entity);
        stats.attack = EffectiveAttribute(registry, entity, "attack");
        stats.defense = EffectiveAttribute(registry, entity, "defense");
        stats.moveSpeed = std::max(0.0f, EffectiveAttribute(registry, entity, "moveSpeed"));
        if (auto* health = registry.try_get<Health>(entity)) {
            health->max = std::max(1.0f, EffectiveAttribute(registry, entity, "maxHealth"));
            health->current = std::min(health->max, health->current + EffectiveAttribute(registry, entity, "healthRegen") * dt);
        }
        const float maxMana = std::max(0.0f, EffectiveAttribute(registry, entity, "maxMana"));
        const float maxStamina = std::max(0.0f, EffectiveAttribute(registry, entity, "maxStamina"));
        if (maxMana > 0.0f || maxStamina > 0.0f || registry.all_of<Resources>(entity)) {
            auto& resources = registry.get_or_emplace<Resources>(entity);
            resources.maxMana = maxMana;
            resources.maxStamina = maxStamina;
            resources.mana = std::clamp(resources.mana + EffectiveAttribute(registry, entity, "manaRegen") * dt, 0.0f, maxMana);
            resources.stamina =
                std::clamp(resources.stamina + EffectiveAttribute(registry, entity, "staminaRegen") * dt, 0.0f, maxStamina);
        }
    }
}

float ExperienceForLevel(int level) {
    const nlohmann::json p = ProgressionRecord();
    const float base = p.value("baseExperience", 100.0f);
    const float growth = p.value("growth", 1.5f);
    return std::round(base * std::pow(growth, static_cast<float>(std::max(0, level - 1))));
}

int AwardExperience(Registry& registry, Entity entity, float amount) {
    auto* progression = registry.try_get<Progression>(entity);
    if (!progression || amount <= 0.0f) return 0;
    const nlohmann::json p = ProgressionRecord();
    const int maxLevel = p.value("maxLevel", 50);
    progression->experience += amount;
    int gained = 0;
    while (progression->level < maxLevel && progression->experience >= ExperienceForLevel(progression->level)) {
        progression->experience -= ExperienceForLevel(progression->level);
        ++progression->level;
        ++gained;
        progression->perkPoints += p.value("perkPointsPerLevel", 1);
        // Each level's gains to the base attributes: [{"attribute", "value"}].
        if (auto* attributes = registry.try_get<Attributes>(entity)) {
            const nlohmann::json gains = p.contains("perLevel") ? p["perLevel"]
                                                                : nlohmann::json::parse(R"([{"attribute":"maxHealth","value":2},{"attribute":"attack","value":0.5}])");
            for (const auto& g : gains) {
                if (float* field = Field(*attributes, g.value("attribute", ""))) *field += g.value("value", 0.0f);
            }
        }
        if (auto* health = registry.try_get<Health>(entity)) health->current = health->max; // a level up heals
    }
    return gained;
}

std::string TakePerk(Registry& registry, Entity entity, const std::string& perk) {
    auto* progression = registry.try_get<Progression>(entity);
    const Record* record = ActiveRecords().Find("perk", perk);
    if (!progression) return "no progression";
    if (!record) return "no perk '" + perk + "'";
    auto taken = SplitIds(progression->perks);
    if (std::find(taken.begin(), taken.end(), perk) != taken.end()) return "already taken";
    if (progression->perkPoints <= 0) return "no perk points";
    if (progression->level < record->resolved.value("level", 1)) return "needs level " + std::to_string(record->resolved.value("level", 1));
    for (const auto& required : record->resolved.value("requires", nlohmann::json::array())) {
        if (required.is_string() && std::find(taken.begin(), taken.end(), required.get<std::string>()) == taken.end()) {
            return "needs " + required.get<std::string>() + " first";
        }
    }
    --progression->perkPoints;
    progression->perks += (progression->perks.empty() ? "" : ", ") + perk;
    return "";
}

} // namespace aige
