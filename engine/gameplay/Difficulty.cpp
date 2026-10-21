#include "gameplay/Difficulty.h"

#include "gameplay/GameSession.h"
#include "gameplay/Attributes.h"
#include "gameplay/Combat.h"
#include "gameplay/Components.h"
#include "gameplay/Settings.h"
#include "records/Records.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace aige {

namespace {

int Clamp(int step) { return std::clamp(step, 1, 5); }

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

bool OwnerMatches(const std::string& owner, DifficultyOwner who) {
    return owner.empty() || owner == "any" || owner == DifficultyOwnerName(who);
}

std::string Format(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%g", std::round(value * 100.0f) / 100.0f);
    return text;
}

} // namespace

const char* DifficultyOwnerName(DifficultyOwner owner) {
    switch (owner) {
        case DifficultyOwner::Player: return "player";
        case DifficultyOwner::Enemy: return "enemy";
        default: return "ally";
    }
}

const std::vector<std::string>& DifficultyCategories() {
    static const std::vector<std::string> categories = {"player", "enemies", "allies", "stealth", "resources"};
    return categories;
}

std::string DifficultyCategory(const std::string& kind, DifficultyOwner owner) {
    if (kind == "gold" || kind == "loot" || kind == "price" || kind == "xp") return "resources";
    if (kind == "perception" && owner == DifficultyOwner::Enemy) return "stealth";
    switch (owner) {
        case DifficultyOwner::Player: return "player";
        case DifficultyOwner::Enemy: return "enemies";
        default: return "allies";
    }
}

// --- Table ------------------------------------------------------------------------

DifficultyTable DifficultyTable::Default() {
    // Higher steps are harder in every category: the player and allies weaker,
    // enemies stronger and sharper-eyed, resources scarcer and dearer.
    DifficultyTable t;
    t.rows["player"] = {{"health", {2.0f, 1.5f, 1.0f, 0.8f, 0.6f}},
                        {"damage", {2.0f, 1.5f, 1.0f, 0.8f, 0.6f}},
                        {"defense", {2.0f, 1.5f, 1.0f, 0.8f, 0.6f}},
                        {"speed", {1.1f, 1.05f, 1.0f, 1.0f, 1.0f}}};
    t.rows["allies"] = {{"health", {2.0f, 1.5f, 1.0f, 0.8f, 0.6f}},
                        {"damage", {2.0f, 1.5f, 1.0f, 0.8f, 0.6f}},
                        {"defense", {2.0f, 1.5f, 1.0f, 0.8f, 0.6f}}};
    t.rows["enemies"] = {{"health", {0.5f, 0.75f, 1.0f, 1.5f, 2.0f}},
                         {"damage", {0.5f, 0.75f, 1.0f, 1.5f, 2.5f}},
                         {"defense", {0.5f, 0.75f, 1.0f, 1.25f, 1.5f}},
                         {"speed", {0.85f, 0.92f, 1.0f, 1.08f, 1.15f}},
                         {"count", {0.5f, 0.75f, 1.0f, 1.25f, 1.5f}}};
    t.rows["stealth"] = {{"perception", {0.6f, 0.8f, 1.0f, 1.25f, 1.5f}}};
    t.rows["resources"] = {{"gold", {1.5f, 1.25f, 1.0f, 0.8f, 0.6f}},
                           {"loot", {1.5f, 1.25f, 1.0f, 0.8f, 0.6f}},
                           {"xp", {1.5f, 1.25f, 1.0f, 0.9f, 0.8f}},
                           {"price", {0.7f, 0.85f, 1.0f, 1.2f, 1.5f}}};
    return t;
}

float DifficultyTable::Multiplier(const std::string& category, const std::string& kind, int step) const {
    auto c = rows.find(category);
    if (c == rows.end()) return 1.0f;
    auto k = c->second.find(kind);
    return k == c->second.end() ? 1.0f : k->second[Clamp(step) - 1];
}

nlohmann::json DifficultyTable::ToJson() const {
    nlohmann::json list = nlohmann::json::array();
    for (const auto& [category, kinds] : rows) {
        for (const auto& [kind, steps] : kinds) {
            list.push_back({{"category", category}, {"kind", kind}, {"steps", std::vector<float>(steps.begin(), steps.end())}});
        }
    }
    return {{"rows", list}};
}

DifficultyTable DifficultyTable::FromJson(const nlohmann::json& json) {
    DifficultyTable table = Default();
    if (!json.is_object()) return table;
    for (const auto& row : json.value("rows", nlohmann::json::array())) {
        if (!row.is_object()) continue;
        const auto steps = row.value("steps", nlohmann::json::array());
        if (!steps.is_array() || steps.size() != 5) continue;
        std::array<float, 5> values{};
        bool ok = true;
        for (size_t i = 0; i < 5; ++i) {
            ok = ok && steps[i].is_number();
            values[i] = ok ? steps[i].get<float>() : 1.0f;
        }
        if (ok) table.rows[row.value("category", "")][row.value("kind", "")] = values;
    }
    return table;
}

// --- Levels -------------------------------------------------------------------------

DifficultyLevel DifficultyLevelFromJson(const std::string& id, const nlohmann::json& json) {
    DifficultyLevel level;
    level.id = id;
    level.name = json.value("name", id);
    level.description = json.value("description", "");
    level.order = json.value("order", 0);
    const auto categories = json.value("categories", nlohmann::json::object());
    for (const auto& category : DifficultyCategories()) {
        level.steps[category] = Clamp(categories.is_object() ? categories.value(category, 3) : 3);
    }
    for (const auto& m : json.value("modifiers", nlohmann::json::array())) {
        if (!m.is_object()) continue;
        level.modifiers.push_back({m.value("target", ""), m.value("owner", "any"), m.value("tag", ""), m.value("op", "mul"),
                                   m.value("value", 1.0f)});
    }
    level.xp = json.value("xp", 1.0f);
    level.permadeath = json.value("permadeath", false);
    return level;
}

DifficultyModel DifficultyModel::BuiltIn() {
    DifficultyModel model;
    model.builtIn_ = true;
    model.table_ = DifficultyTable::Default();
    struct Preset {
        const char* id;
        const char* name;
        const char* description;
        int player, enemies, resources;
        float xp;
    };
    const Preset presets[] = {
        {"story", "Story", "For the story: enemies are weak and you are strong.", 1, 1, 1, 1.0f},
        {"easy", "Easy", "A gentle challenge.", 2, 2, 2, 1.0f},
        {"normal", "Normal", "The game as designed.", 3, 3, 3, 1.0f},
        {"hard", "Hard", "Enemies hit harder and resources are scarce.", 3, 4, 4, 1.1f},
        {"nightmare", "Nightmare", "Every fight is a threat.", 4, 5, 5, 1.25f},
    };
    int order = 1;
    for (const auto& p : presets) {
        DifficultyLevel level;
        level.id = p.id;
        level.name = p.name;
        level.description = p.description;
        level.order = order++;
        level.steps = {{"player", p.player}, {"allies", p.player}, {"enemies", p.enemies}, {"stealth", p.enemies},
                       {"resources", p.resources}};
        level.xp = p.xp;
        model.levels_.push_back(level);
    }
    return model;
}

DifficultyModel DifficultyModel::FromRecords(const RecordDatabase& records) {
    DifficultyModel model = BuiltIn();
    std::vector<DifficultyLevel> levels;
    for (const Record* record : records.OfType("difficulty")) levels.push_back(DifficultyLevelFromJson(record->id, record->resolved));
    if (!levels.empty()) {
        std::stable_sort(levels.begin(), levels.end(),
                         [](const DifficultyLevel& a, const DifficultyLevel& b) { return a.order < b.order; });
        model.levels_ = std::move(levels);
        model.builtIn_ = false;
    }
    if (const Record* table = records.Find("difficulty-table", "default")) model.table_ = DifficultyTable::FromJson(table->resolved);
    return model;
}

const DifficultyLevel* DifficultyModel::Find(const std::string& id) const {
    for (const auto& level : levels_) {
        if (level.id == id) return &level;
    }
    return nullptr;
}

std::string DifficultyModel::DefaultLevel() const {
    if (Find(ActiveSettings().difficulty.defaultLevel)) return ActiveSettings().difficulty.defaultLevel;
    if (Find("normal")) return "normal";
    return levels_.empty() ? std::string() : levels_[levels_.size() / 2].id;
}

DifficultyModel::Value DifficultyModel::Effective(float base, const FieldMeta& field, const std::string& path,
                                                  DifficultyOwner owner, const std::string& name, const std::string& level,
                                                  const nlohmann::json& overrides, int shift) const {
    Value out;
    out.value = base;
    const DifficultyLevel* l = Find(level);
    // A value set by hand for this level wins.
    if (overrides.is_object() && overrides.contains(path) && overrides[path].is_object() && overrides[path].contains(level) &&
        overrides[path][level].is_number()) {
        out.value = overrides[path][level].get<float>();
        out.overridden = true;
        out.why.push_back("set by hand for " + (l ? l->name : level));
        return out;
    }
    if (l == nullptr || field.scalable.empty()) return out;
    const std::string category = DifficultyCategory(field.scalable, owner);
    auto it = l->steps.find(category);
    int step = it == l->steps.end() ? 3 : it->second;
    if (shift != 0 && (category == "enemies" || category == "stealth")) step = Clamp(step + shift);
    const float m = table_.Multiplier(category, field.scalable, step);
    out.value = base * m;
    if (m != 1.0f) out.why.push_back(category + " " + field.scalable + " x" + Format(m) + " (step " + std::to_string(step) + ")");
    float add = 0.0f;
    std::optional<float> set, low, high;
    for (const auto& mod : l->modifiers) {
        if (mod.target != path || !OwnerMatches(mod.owner, owner)) continue;
        if (!mod.tag.empty() && Lower(name).find(Lower(mod.tag)) == std::string::npos) continue;
        if (mod.op == "mul") {
            out.value *= mod.value;
            out.why.push_back("modifier x" + Format(mod.value));
        } else if (mod.op == "add") {
            add += mod.value;
            out.why.push_back("modifier +" + Format(mod.value));
        } else if (mod.op == "set") {
            set = mod.value;
        } else if (mod.op == "min") {
            low = mod.value;
        } else if (mod.op == "max") {
            high = mod.value;
        }
    }
    out.value += add;
    if (field.min) out.value = std::max(out.value, static_cast<float>(*field.min));
    if (field.max) out.value = std::min(out.value, static_cast<float>(*field.max));
    if (set) {
        out.value = *set;
        out.why.push_back("modifier set " + Format(*set));
    }
    if (low && out.value < *low) {
        out.value = *low;
        out.why.push_back("at least " + Format(*low));
    }
    if (high && out.value > *high) {
        out.value = *high;
        out.why.push_back("at most " + Format(*high));
    }
    if (field.type == FieldType::Int) out.value = std::round(out.value);
    return out;
}

// --- On entities ----------------------------------------------------------------------

DifficultyOwner OwnerOf(Registry& registry, Entity entity) {
    const Team team = TeamOf(registry, entity);
    if (team == Team::Player) return DifficultyOwner::Player;
    if (team == Team::Enemy) return DifficultyOwner::Enemy;
    if (auto player = FindPlayer(registry); player && *player != entity && AreHostile(registry, entity, *player)) {
        return DifficultyOwner::Enemy;
    }
    return DifficultyOwner::Ally;
}

namespace {

nlohmann::json OverridesOf(Registry& registry, Entity entity) {
    const auto* o = registry.try_get<DifficultyOverrides>(entity);
    if (!o || o->values.empty()) return nlohmann::json::object();
    nlohmann::json json = nlohmann::json::parse(o->values, nullptr, false);
    return json.is_object() ? json : nlohmann::json::object();
}

std::string NameOf(Registry& registry, Entity entity) {
    const auto* name = registry.try_get<Name>(entity);
    return name ? name->value : std::string();
}

float AsFloat(const FieldValue& value) {
    if (std::holds_alternative<float>(value)) return std::get<float>(value);
    if (std::holds_alternative<int>(value)) return static_cast<float>(std::get<int>(value));
    return 0.0f;
}

FieldValue AsValue(float value, FieldType type) {
    if (type == FieldType::Int) return static_cast<int>(std::lround(value));
    return value;
}

// Sets a field, keeping Health's current share of its max.
void SetScaled(Registry& registry, Entity entity, const ComponentInfo& component, const FieldInfo& field, float value) {
    float share = -1.0f;
    if (component.name == "Health" && field.name == "max") {
        const auto& health = registry.get<Health>(entity);
        if (health.max > 0.0f) share = health.current / health.max;
    }
    field.set(registry, entity, AsValue(value, field.type));
    if (share >= 0.0f) {
        auto& health = registry.get<Health>(entity);
        health.current = std::min(health.max, std::max(health.current > 0.0f ? 0.01f : 0.0f, share * health.max));
    }
}

} // namespace

int ApplyDifficulty(Registry& registry, const ReflectionRegistry& reflection, const DifficultyModel& model,
                    const std::string& level, int shift) {
    int changed = 0;
    for (const auto& component : reflection.Components()) {
        const bool scaled = std::any_of(component.fields.begin(), component.fields.end(),
                                        [](const FieldInfo& f) { return !f.scalable.empty(); });
        if (!scaled || !component.has) continue;
        std::vector<Entity> entities;
        for (auto entity : registry.view<entt::entity>()) {
            if (component.has(registry, entity)) entities.push_back(entity);
        }
        for (Entity entity : entities) {
            const DifficultyOwner owner = OwnerOf(registry, entity);
            const nlohmann::json overrides = OverridesOf(registry, entity);
            const std::string name = NameOf(registry, entity);
            for (const auto& field : component.fields) {
                if (field.scalable.empty()) continue;
                const std::string path = component.name + "." + field.name;
                auto& base = registry.get_or_emplace<DifficultyBase>(entity);
                auto it = base.values.find(path);
                if (it == base.values.end()) it = base.values.emplace(path, AsFloat(field.get(registry, entity))).first;
                const float value = model.Effective(it->second, field, path, owner, name, level, overrides, shift).value;
                if (AsFloat(field.get(registry, entity)) != value) {
                    SetScaled(registry, entity, component, field, value);
                    ++changed;
                }
            }
        }
    }
    return changed;
}

void RestoreDifficultyBase(Registry& registry, const ReflectionRegistry& reflection) {
    std::vector<Entity> entities;
    for (auto [entity, base] : registry.view<DifficultyBase>().each()) entities.push_back(entity);
    for (Entity entity : entities) {
        const auto values = registry.get<DifficultyBase>(entity).values;
        for (const auto& [path, value] : values) {
            const auto dot = path.find('.');
            const ComponentInfo* component = reflection.FindComponent(path.substr(0, dot));
            const FieldInfo* field = component ? component->Field(path.substr(dot + 1)) : nullptr;
            if (field && component->has(registry, entity)) SetScaled(registry, entity, *component, *field, value);
        }
        registry.remove<DifficultyBase>(entity);
    }
}

nlohmann::json DifficultyPreview(Registry& registry, const ReflectionRegistry& reflection, Entity entity,
                                 const DifficultyModel& model) {
    nlohmann::json out = nlohmann::json::array();
    if (!registry.valid(entity)) return out;
    const DifficultyOwner owner = OwnerOf(registry, entity);
    const nlohmann::json overrides = OverridesOf(registry, entity);
    const std::string name = NameOf(registry, entity);
    const auto* bases = registry.try_get<DifficultyBase>(entity);
    for (const auto& component : reflection.Components()) {
        if (!component.has || !component.has(registry, entity)) continue;
        for (const auto& field : component.fields) {
            if (field.scalable.empty()) continue;
            const std::string path = component.name + "." + field.name;
            float base = AsFloat(field.get(registry, entity));
            if (bases) {
                if (auto it = bases->values.find(path); it != bases->values.end()) base = it->second;
            }
            nlohmann::json levels = nlohmann::json::array();
            for (const auto& level : model.Levels()) {
                const auto v = model.Effective(base, field, path, owner, name, level.id, overrides);
                levels.push_back({{"level", level.id}, {"name", level.name}, {"value", v.value}, {"overridden", v.overridden},
                                  {"why", v.why}});
            }
            out.push_back({{"field", path}, {"base", base}, {"kind", field.scalable},
                           {"category", DifficultyCategory(field.scalable, owner)}, {"levels", levels}});
        }
    }
    return out;
}

std::vector<CombatPreviewRow> CombatPreview(Registry& registry, const ReflectionRegistry& reflection, Entity enemy,
                                            Entity player, const DifficultyModel& model) {
    std::vector<CombatPreviewRow> rows;
    if (!registry.valid(enemy) || !registry.valid(player)) return rows;
    // A field's value at a level, or `fallback` when the entity lacks it.
    auto at = [&](Entity e, DifficultyOwner owner, const char* component, const char* fieldName, const std::string& level,
                  float fallback) {
        const ComponentInfo* c = reflection.FindComponent(component);
        const FieldInfo* f = c ? c->Field(fieldName) : nullptr;
        if (!f || !c->has(registry, e)) return fallback;
        float base = AsFloat(f->get(registry, e));
        if (const auto* b = registry.try_get<DifficultyBase>(e)) {
            if (auto it = b->values.find(std::string(component) + "." + fieldName); it != b->values.end()) base = it->second;
        }
        return model.Effective(base, *f, std::string(component) + "." + fieldName, owner, NameOf(registry, e), level,
                               OverridesOf(registry, e)).value;
    };
    const float minimum = std::max(0.01f, ActiveSettings().combat.minimumDamage);
    auto attackOf = [&](Entity e, DifficultyOwner owner, const std::string& level, float& cooldown) {
        cooldown = 1.0f;
        float damage = 0.0f;
        if (registry.all_of<MeleeAttack>(e)) {
            damage = at(e, owner, "MeleeAttack", "damage", level, 0.0f);
            cooldown = std::max(0.1f, registry.get<MeleeAttack>(e).cooldown);
        } else if (registry.all_of<RangedAttack>(e)) {
            damage = at(e, owner, "RangedAttack", "damage", level, 0.0f);
            cooldown = std::max(0.1f, registry.get<RangedAttack>(e).cooldown);
        }
        return damage + at(e, owner, "Stats", "attack", level, 0.0f);
    };
    for (const auto& level : model.Levels()) {
        CombatPreviewRow row;
        row.level = level.id;
        float enemyCooldown = 1.0f, playerCooldown = 1.0f;
        row.enemyHealth = at(enemy, DifficultyOwner::Enemy, "Health", "max", level.id, 1.0f);
        row.playerHealth = at(player, DifficultyOwner::Player, "Health", "max", level.id, 1.0f);
        row.enemyDamage = std::max(minimum, attackOf(enemy, DifficultyOwner::Enemy, level.id, enemyCooldown) -
                                                at(player, DifficultyOwner::Player, "Stats", "defense", level.id, 0.0f));
        row.playerDamage = std::max(minimum, attackOf(player, DifficultyOwner::Player, level.id, playerCooldown) -
                                                 at(enemy, DifficultyOwner::Enemy, "Stats", "defense", level.id, 0.0f));
        row.hitsToKillEnemy = static_cast<int>(std::ceil(row.enemyHealth / row.playerDamage));
        row.hitsToKillPlayer = static_cast<int>(std::ceil(row.playerHealth / row.enemyDamage));
        row.secondsToKillEnemy = row.hitsToKillEnemy * playerCooldown;
        row.secondsToKillPlayer = row.hitsToKillPlayer * enemyCooldown;
        rows.push_back(row);
    }
    return rows;
}

// --- Level scaling ------------------------------------------------------------------------

int EncounterLevel(Registry& registry, int spawnerLevel, int playerLevel) {
    const auto& settings = ActiveSettings().difficulty;
    std::optional<Zone> zone;
    for (auto [entity, z] : registry.view<Zone>().each()) {
        zone = z;
        break;
    }
    const int scaled = std::max(1, playerLevel + settings.levelOffset);
    switch (settings.scaling) {
        case LevelScaling::FixedZones:
            return zone ? std::clamp(spawnerLevel, zone->minLevel, std::max(zone->minLevel, zone->maxLevel)) : spawnerLevel;
        case LevelScaling::Scaled: return scaled;
        case LevelScaling::Hybrid:
            return zone ? std::clamp(scaled, zone->minLevel, std::max(zone->minLevel, zone->maxLevel)) : scaled;
        default: return spawnerLevel;
    }
}

// --- Curve --------------------------------------------------------------------------------------

std::vector<CurveProblem> CheckDifficultyCurve(const std::vector<CurveChapter>& chapters, bool sawtooth, int maxJump) {
    std::vector<CurveProblem> problems;
    const CurveChapter* previous = nullptr;
    for (const auto& chapter : chapters) {
        if (chapter.minLevel <= 0) {
            problems.push_back({chapter.scene, "has no Zone, so it has no level band"});
            continue;
        }
        for (int level : chapter.enemyLevels) {
            if (level < chapter.minLevel || level > chapter.maxLevel) {
                problems.push_back({chapter.scene, "a level " + std::to_string(level) + " enemy is outside its band (" +
                                                       std::to_string(chapter.minLevel) + "-" +
                                                       std::to_string(chapter.maxLevel) + ")"});
                break;
            }
        }
        if (previous) {
            if (chapter.minLevel > previous->maxLevel + maxJump) {
                problems.push_back({chapter.scene, "too hard: its band starts at " + std::to_string(chapter.minLevel) +
                                                       " right after " + previous->scene + " ends at " +
                                                       std::to_string(previous->maxLevel)});
            } else if (!sawtooth && chapter.maxLevel < previous->minLevel) {
                problems.push_back({chapter.scene, "easier than " + previous->scene + " before it"});
            }
        }
        previous = &chapter;
    }
    return problems;
}

// --- Adaptive --------------------------------------------------------------------------------

void AdaptiveDifficulty::OnPlayerDeath() {
    cleanWins = 0;
    if (++deaths >= 3 && shift > -1) {
        --shift;
        deaths = 0;
    }
}

void AdaptiveDifficulty::OnFightWon(bool tookDamage) {
    if (tookDamage) return;
    deaths = 0;
    if (++cleanWins >= 2 && shift < 0) {
        ++shift;
        cleanWins = 0;
    }
}

nlohmann::json AdaptiveDifficulty::ToJson() const {
    return {{"shift", shift}, {"deaths", deaths}, {"cleanWins", cleanWins}};
}

AdaptiveDifficulty AdaptiveDifficulty::FromJson(const nlohmann::json& json) {
    AdaptiveDifficulty a;
    if (!json.is_object()) return a;
    a.shift = std::clamp(json.value("shift", 0), -1, 1);
    a.deaths = json.value("deaths", 0);
    a.cleanWins = json.value("cleanWins", 0);
    return a;
}

// --- Assists -----------------------------------------------------------------------------------

float Assists::Reduction() const {
    float r = std::clamp(damageReduction, 0.0f, 0.9f);
    if (growingReduction) r += 0.02f * static_cast<float>(deaths);
    return std::min(r, std::max(0.8f, std::clamp(damageReduction, 0.0f, 0.9f)));
}

Assists& ActiveAssists() {
    static Assists assists;
    return assists;
}

} // namespace aige
