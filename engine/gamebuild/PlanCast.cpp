#include "gamebuild/PlanCast.h"

#include "gameplay/Attributes.h"
#include "gameplay/Behaviour.h"
#include "gameplay/Components.h"
#include "gameplay/Items.h"
#include "gameplay/Magic.h"
#include "physics2d/Components.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace aige {

using json = nlohmann::json;

PlanRecords::PlanRecords(const json& systems) {
    for (const auto& r : systems.value("records", json::array())) {
        if (!r.is_object()) continue;
        const std::string type = r.value("type", ""), id = r.value("id", "");
        if (type.empty() || id.empty()) continue;
        byType_[type][id] = r.value("record", json::object());
    }
}

const json* PlanRecords::Find(const std::string& type, const std::string& id) const {
    const auto t = byType_.find(type);
    if (t == byType_.end()) return nullptr;
    const auto r = t->second.find(id);
    return r == t->second.end() ? nullptr : &r->second;
}

std::vector<std::string> PlanRecords::Ids(const std::string& type) const {
    std::vector<std::string> out;
    if (const auto t = byType_.find(type); t != byType_.end()) {
        for (const auto& [id, r] : t->second) out.push_back(id);
    }
    return out;
}

namespace {

std::set<std::string> Words(std::string text) {
    for (auto& c : text) c = std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : ' ';
    std::istringstream in(text);
    std::set<std::string> out;
    for (std::string w; in >> w;) {
        if (w.size() > 2) out.insert(w);
    }
    return out;
}

// What a kind of character is at level 1 and gains each level, before its class.
struct KindDefaults {
    float maxHealth, attack, defense, moveSpeed, damage;
    float healthPerLevel, attackPerLevel, defensePerLevel;
    const char* combat;
    float experience; // its killer's, per level
};

KindDefaults DefaultsFor(CastKind kind) {
    switch (kind) {
    case CastKind::Enemy: return {8.0f, 0.0f, 0.0f, 2.2f, 2.0f, 4.0f, 0.5f, 0.25f, "melee", 10.0f};
    case CastKind::Boss: return {30.0f, 1.0f, 1.0f, 2.0f, 4.0f, 10.0f, 1.0f, 0.5f, "melee", 50.0f};
    case CastKind::Player: return {20.0f, 1.0f, 0.0f, 4.2f, 2.0f, 0.0f, 0.0f, 0.0f, "melee", 0.0f};
    case CastKind::Civilian: break;
    }
    return {10.0f, 0.0f, 0.0f, 1.6f, 1.0f, 2.0f, 0.0f, 0.0f, "none", 5.0f};
}

float* AttributeField(Attributes& a, const std::string& name) {
    static const std::map<std::string, float Attributes::*> fields = {
        {"maxHealth", &Attributes::maxHealth},       {"maxMana", &Attributes::maxMana},
        {"maxStamina", &Attributes::maxStamina},     {"attack", &Attributes::attack},
        {"defense", &Attributes::defense},           {"moveSpeed", &Attributes::moveSpeed},
        {"healthRegen", &Attributes::healthRegen},   {"manaRegen", &Attributes::manaRegen},
        {"staminaRegen", &Attributes::staminaRegen}, {"resistFire", &Attributes::resistFire},
        {"resistFrost", &Attributes::resistFrost},   {"resistShock", &Attributes::resistShock},
        {"resistPoison", &Attributes::resistPoison}};
    const auto it = fields.find(name);
    return it == fields.end() ? nullptr : &(a.*(it->second));
}

DamageType DamageNamed(const std::string& name) {
    if (name == "fire") return DamageType::Fire;
    if (name == "frost") return DamageType::Frost;
    if (name == "shock") return DamageType::Shock;
    if (name == "poison") return DamageType::Poison;
    return DamageType::Physical;
}

std::string Joined(const std::vector<std::string>& list) {
    std::string out;
    for (const auto& s : list) {
        if (s.empty()) continue;
        out += (out.empty() ? "" : ", ") + s;
    }
    return out;
}

std::vector<std::string> Strings(const json& list) {
    std::vector<std::string> out;
    if (list.is_string()) {
        // "a, b" as well as ["a", "b"]
        std::istringstream in(list.get<std::string>());
        for (std::string s; std::getline(in, s, ',');) {
            s.erase(0, s.find_first_not_of(' '));
            s.erase(s.find_last_not_of(' ') + 1);
            if (!s.empty()) out.push_back(s);
        }
        return out;
    }
    for (const auto& v : list.is_array() ? list : json::array()) {
        if (v.is_string() && !v.get<std::string>().empty()) out.push_back(v.get<std::string>());
        else if (v.is_object() && !v.value("id", "").empty()) out.push_back(v.value("id", ""));
    }
    return out;
}

} // namespace

std::string JoinItems(const json& list) { return Joined(Strings(list)); }

std::string ClassFor(const PlanRecords& records, const std::string& wanted, CastKind kind) {
    if (!wanted.empty() && records.Find("character-class", wanted)) return wanted;
    const std::set<std::string> want = Words(wanted);
    std::string best;
    int bestScore = 0;
    for (const auto& id : records.Ids("character-class")) {
        const json& r = *records.Find("character-class", id);
        const std::string role = r.value("role", "");
        int score = 0;
        for (const auto& w : Words(id + " " + r.value("name", ""))) score += want.contains(w) ? 3 : 0;
        // Of its sort: a boss's class for a boss, a fighting one for an enemy, a peaceful one for a villager.
        if (kind == CastKind::Boss && role == "boss") score += 2;
        if (kind == CastKind::Enemy && (role == "fighter" || role == "archer" || role == "caster" || role == "beast")) score += 1;
        if (kind == CastKind::Civilian && role == "civilian") score += 1;
        if (score > bestScore) best = id, bestScore = score;
    }
    // A shared word, or (with no words to go by) one of its sort.
    return bestScore >= (want.empty() ? 1 : 3) ? best : std::string();
}

json ApplyCast(Registry& registry, Entity entity, const CastSpec& given, const PlanRecords& records) {
    CastSpec spec = given;
    if (!spec.gameplay.is_object()) spec.gameplay = json::object(); // (an enemy class's prefab has no character's)
    const KindDefaults d = DefaultsFor(spec.kind);
    const json* cls = spec.cls.empty() ? nullptr : records.Find("character-class", spec.cls);
    const json klass = cls ? *cls : json::object();
    const int level = std::max(1, spec.level);
    const bool hostile = spec.kind == CastKind::Enemy || spec.kind == CastKind::Boss;

    // Attributes: the kind's, then its class's at level 1, then what each level after adds.
    Attributes a;
    a.maxHealth = d.maxHealth, a.attack = d.attack, a.defense = d.defense, a.moveSpeed = d.moveSpeed;
    if (spec.kind == CastKind::Player) a.maxStamina = 10.0f;
    for (const auto& v : klass.value("attributes", json::array())) {
        if (float* f = AttributeField(a, v.value("attribute", "")); f && v.contains("value") && v["value"].is_number()) *f = v["value"].get<float>();
    }
    const json perLevel = klass.value("perLevel", json::array());
    if (perLevel.empty() || !cls) {
        a.maxHealth += d.healthPerLevel * (level - 1), a.attack += d.attackPerLevel * (level - 1), a.defense += d.defensePerLevel * (level - 1);
    }
    for (const auto& v : perLevel) {
        if (float* f = AttributeField(a, v.value("attribute", "")); f && v.contains("value") && v["value"].is_number()) {
            *f += v["value"].get<float>() * (level - 1);
        }
    }
    if (hostile) a.maxHealth *= spec.difficulty;
    for (float* r : {&a.resistFire, &a.resistFrost, &a.resistShock, &a.resistPoison}) *r = std::clamp(*r, 0.0f, 0.9f);
    a.maxHealth = std::max(1.0f, a.maxHealth);
    a.moveSpeed = std::clamp(a.moveSpeed, 0.5f, 8.0f);
    registry.emplace_or_replace<Attributes>(entity, a);
    registry.emplace_or_replace<Health>(entity, a.maxHealth, a.maxHealth);
    registry.emplace_or_replace<Stats>(entity, a.attack, a.defense, a.moveSpeed);
    if (a.maxMana > 0.0f || a.maxStamina > 0.0f) {
        auto& res = registry.get_or_emplace<Resources>(entity);
        res.mana = res.maxMana = a.maxMana;
        res.stamina = res.maxStamina = a.maxStamina;
    }
    registry.get_or_emplace<Progression>(entity).level = level;

    // How it fights.
    std::string combat = klass.value("combat", std::string(d.combat));
    if (spec.kind == CastKind::Player && !spec.fights) combat = "none";
    const float damage = klass.value("damage", d.damage) * (hostile ? spec.difficulty : 1.0f);
    const DamageType type = DamageNamed(klass.value("damageType", ""));
    if (combat == "melee" || combat == "caster") {
        // (A caster has a weak swing for when it's cornered.)
        const float swing = combat == "caster" ? std::max(1.0f, damage * 0.5f) : damage;
        registry.emplace_or_replace<MeleeAttack>(entity, swing, 0.7f, 0.45f, spec.kind == CastKind::Player ? 0.35f : spec.kind == CastKind::Boss ? 0.9f : 1.2f,
                                                 spec.kind == CastKind::Boss ? 0.5f : 0.3f);
    }
    if (combat == "ranged") {
        RangedAttack r;
        r.damage = damage, r.damageType = type;
        registry.emplace_or_replace<RangedAttack>(entity, r);
    }
    if (combat != "none") {
        registry.get_or_emplace<Hurtbox>(entity);
        if (!registry.all_of<Collider2D>(entity)) registry.emplace<Collider2D>(entity, 0.3f, 0.3f);
    }
    // Abilities: its class's and its own (the character's document).
    std::vector<std::string> abilities = Strings(klass.value("abilities", json::array()));
    for (const auto& s : Strings(spec.gameplay.value("abilities", json::array()))) {
        if (std::find(abilities.begin(), abilities.end(), s) == abilities.end()) abilities.push_back(s);
    }
    if (!abilities.empty()) {
        auto& ab = registry.get_or_emplace<Abilities>(entity);
        ab.known = Joined(abilities);
        // The player's quick slots: the first four.
        if (spec.kind == CastKind::Player && ab.slots.empty()) {
            ab.slots = Joined(std::vector<std::string>(abilities.begin(), abilities.begin() + std::min<size_t>(4, abilities.size())));
        }
    }

    // Its temper, and its walk (the character AI's).
    if (spec.kind != CastKind::Player && registry.all_of<Behaviour>(entity)) {
        auto& b = registry.get<Behaviour>(entity);
        const std::string aggression = klass.value("aggression", std::string(hostile ? "aggressive" : "peaceful"));
        b.aggression = aggression == "frenzied" ? Aggression::Frenzied : aggression == "aggressive" ? Aggression::Aggressive
                     : aggression == "defensive" ? Aggression::Defensive : Aggression::Peaceful;
        const std::string confidence = klass.value("confidence", std::string(spec.kind == CastKind::Boss ? "foolhardy" : "average"));
        b.confidence = confidence == "cowardly" ? Confidence::Cowardly : confidence == "cautious" ? Confidence::Cautious
                     : confidence == "brave" ? Confidence::Brave : confidence == "foolhardy" ? Confidence::Foolhardy : Confidence::Average;
        b.speed = std::min(a.moveSpeed, hostile ? 3.0f : 2.0f);
    }
    if (auto* f = registry.try_get<Follow>(entity)) f->speed = std::min(a.moveSpeed, 3.0f);

    // Its kit: carried, worn and wielded.
    const std::string carries = JoinItems(klass.value("carries", json::array()));
    const int gold = klass.value("gold", 0);
    if (!carries.empty() || gold > 0 || spec.kind == CastKind::Player) {
        auto& inv = registry.get_or_emplace<Inventory>(entity);
        if (!carries.empty()) inv.items = inv.items.empty() ? carries : inv.items + ", " + carries;
        inv.gold += gold;
    }
    json worn = json::object();
    for (const auto& e : klass.value("equipment", json::array())) {
        const std::string slot = e.value("slot", ""), item = e.value("item", "");
        if (slot.empty() || item.empty()) continue;
        auto& eq = registry.get_or_emplace<Equipment>(entity);
        std::string* at = slot == "weapon" ? &eq.weapon : slot == "offhand" ? &eq.offhand : slot == "head" ? &eq.head : slot == "body" ? &eq.body
                        : slot == "hands" ? &eq.hands : slot == "feet" ? &eq.feet : slot == "ring" ? &eq.ring : slot == "amulet" ? &eq.amulet : nullptr;
        if (at) *at = item, worn[slot] = item;
    }

    // A merchant: its stock.
    const std::string stock = klass.value("stock", spec.stock);
    if (!stock.empty()) registry.emplace_or_replace<Merchant>(entity).stock = stock;

    // What killing it gives.
    if (hostile) {
        Rewards r;
        r.experience = klass.value("experience", d.experience) * level;
        r.loot = klass.value("loot", "");
        r.goldMin = spec.kind == CastKind::Boss ? 10 * level : level;
        r.goldMax = spec.kind == CastKind::Boss ? 30 * level : 5 * level;
        registry.emplace_or_replace<Rewards>(entity, r);
    }
    return {{"class", cls ? spec.cls : ""}, {"level", level}, {"maxHealth", a.maxHealth}, {"attack", a.attack}, {"defense", a.defense},
            {"moveSpeed", a.moveSpeed}, {"combat", combat}, {"carries", carries}, {"equipment", worn}};
}

} // namespace aige
