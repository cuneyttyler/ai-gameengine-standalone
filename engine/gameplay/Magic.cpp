#include "gameplay/Magic.h"

#include "gameplay/Animation.h"
#include "gameplay/Attributes.h"
#include "gameplay/Combat.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/Items.h"
#include "physics2d/Components.h"
#include "records/Records.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>

namespace aige {

namespace fs = std::filesystem;

namespace {

fs::path MagicDir() {
#ifdef AIGE_MAGIC_DATA_DIR
    return fs::path(AIGE_MAGIC_DATA_DIR);
#else
    return fs::path("data") / "magic";
#endif
}

std::optional<nlohmann::json> Preset(const std::string& folder, const std::string& id) {
    std::ifstream in(MagicDir() / folder / (id + ".json"), std::ios::binary);
    if (!in) return std::nullopt;
    nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
    if (json.is_discarded() || !json.is_object()) return std::nullopt;
    return json;
}

std::vector<std::string> Presets(const std::string& folder) {
    std::vector<std::string> ids;
    std::error_code ec;
    for (const auto& f : fs::directory_iterator(MagicDir() / folder, ec)) {
        if (f.path().extension() == ".json") ids.push_back(f.path().stem().string());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<std::string> Strings(const nlohmann::json& json, const char* key) {
    std::vector<std::string> out;
    const auto v = json.value(key, nlohmann::json::array());
    if (v.is_array()) {
        for (const auto& s : v) {
            if (s.is_string()) out.push_back(s.get<std::string>());
        }
    } else if (v.is_string()) {
        out = SplitList(v.get<std::string>());
    }
    return out;
}

DamageType DamageTypeOf(const std::string& name) {
    if (name == "fire") return DamageType::Fire;
    if (name == "frost") return DamageType::Frost;
    if (name == "shock") return DamageType::Shock;
    if (name == "poison") return DamageType::Poison;
    return DamageType::Physical;
}

float* ResourceOf(Registry& registry, Entity entity, const std::string& attribute) {
    if (attribute == "health") {
        auto* h = registry.try_get<Health>(entity);
        return h ? &h->current : nullptr;
    }
    auto* r = registry.try_get<Resources>(entity);
    if (!r) return nullptr;
    if (attribute == "mana") return &r->mana;
    if (attribute == "stamina") return &r->stamina;
    return nullptr;
}

} // namespace

std::vector<std::string> SplitList(const std::string& list) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(',', start);
        if (end == std::string::npos) end = list.size();
        std::string part = list.substr(start, end - start);
        part.erase(0, part.find_first_not_of(" \t"));
        part.erase(part.find_last_not_of(" \t") + 1);
        if (!part.empty()) out.push_back(part);
        start = end + 1;
    }
    return out;
}

MagicEffectDef MagicEffectFromJson(const std::string& id, const nlohmann::json& j) {
    MagicEffectDef d;
    d.id = id;
    d.archetype = j.value("archetype", d.archetype);
    d.attribute = j.value("attribute", "");
    d.op = j.value("op", d.op);
    if (j.contains("magnitude")) d.magnitude = j["magnitude"].is_string() ? j["magnitude"].get<std::string>() : j["magnitude"].dump();
    d.duration = j.value("duration", 0.0f);
    d.period = std::max(0.1f, j.value("period", 1.0f));
    d.area = j.value("area", 0.0f);
    d.damageType = j.value("damageType", d.damageType);
    d.stacking = j.value("stacking", d.stacking);
    d.maxStacks = j.value("maxStacks", d.maxStacks);
    d.tags = Strings(j, "tags");
    d.requiresTags = Strings(j, "requires");
    d.blockedBy = Strings(j, "blockedBy");
    d.cue = j.value("cue", "");
    d.prefab = j.value("prefab", "");
    d.nonLethal = j.value("nonLethal", false);
    return d;
}

AbilityDef AbilityFromJson(const std::string& id, const nlohmann::json& j) {
    AbilityDef a;
    a.id = id;
    a.name = j.value("name", id);
    a.kind = j.value("kind", a.kind);
    a.school = j.value("school", "");
    a.activation = j.value("activation", a.activation);
    a.delivery = j.value("delivery", a.delivery);
    a.range = j.value("range", a.range);
    a.radius = j.value("radius", a.radius);
    a.speed = j.value("speed", a.speed);
    if (j.contains("cost") && j["cost"].is_object()) {
        a.costAttribute = j["cost"].value("attribute", "mana");
        a.cost = j["cost"].value("amount", 0.0f);
    } else {
        a.cost = j.value("cost", 0.0f);
        a.costAttribute = j.value("costAttribute", a.costAttribute);
    }
    a.cooldown = j.value("cooldown", 0.0f);
    a.castTime = j.value("castTime", 0.0f);
    a.duration = j.value("duration", 0.0f);
    for (const auto& e : j.value("effects", nlohmann::json::array())) {
        if (e.is_string()) a.effects.push_back({e.get<std::string>(), std::nullopt, std::nullopt});
        else if (e.is_object()) {
            AbilityDef::Use use{e.value("effect", ""), std::nullopt, std::nullopt};
            if (e.contains("magnitude") && e["magnitude"].is_number()) use.magnitude = e["magnitude"].get<float>();
            if (e.contains("duration") && e["duration"].is_number()) use.duration = e["duration"].get<float>();
            a.effects.push_back(use);
        }
    }
    a.tags = Strings(j, "tags");
    if (!a.school.empty()) a.tags.push_back("school." + a.school);
    a.animation = j.value("animation", a.animation);
    if (j.contains("cue") && j["cue"].is_object()) {
        a.castCue = j["cue"].value("cast", "");
        a.impactCue = j["cue"].value("impact", "");
    }
    a.requiresLevel = j.value("requiresLevel", 0);
    if (j.contains("ai") && j["ai"].is_object()) {
        a.ai = AbilityDef::AiHint{j["ai"].value("use", "attack"), j["ai"].value("when", ""), j["ai"].value("priority", 0)};
    }
    return a;
}

std::optional<MagicEffectDef> FindMagicEffect(const std::string& id) {
    if (const Record* r = ActiveRecords().Find("magic-effect", id)) return MagicEffectFromJson(id, r->resolved);
    if (auto p = Preset("effects", id)) return MagicEffectFromJson(id, *p);
    return std::nullopt;
}

std::optional<AbilityDef> FindAbility(const std::string& id) {
    if (const Record* r = ActiveRecords().Find("ability", id)) return AbilityFromJson(id, r->resolved);
    if (auto p = Preset("abilities", id)) return AbilityFromJson(id, *p);
    return std::nullopt;
}

std::vector<std::string> MagicEffectPresets() { return Presets("effects"); }
std::vector<std::string> AbilityPresets() { return Presets("abilities"); }

std::vector<std::string> ValidateAbility(const AbilityDef& a) {
    std::vector<std::string> problems;
    static const std::set<std::string> activations = {"press", "hold", "toggle", "passive"};
    static const std::set<std::string> deliveries = {"self", "touch", "projectile", "area", "cone", "aura"};
    if (!activations.contains(a.activation)) problems.push_back("activation is press, hold, toggle or passive");
    if (!deliveries.contains(a.delivery)) problems.push_back("delivery is self, touch, projectile, area, cone or aura");
    if (a.effects.empty()) problems.push_back("has no effects");
    for (const auto& use : a.effects) {
        if (!FindMagicEffect(use.effect)) problems.push_back("no magic effect '" + use.effect + "'");
    }
    if (a.costAttribute != "mana" && a.costAttribute != "stamina" && a.costAttribute != "health") {
        problems.push_back("cost is paid in mana, stamina or health");
    }
    if (a.ai && !a.ai->when.empty()) {
        static const std::set<std::string> known = {"health", "mana", "stamina", "distance", "enemyHealth"};
        for (const auto& term : SplitList([&] {
                 std::string w = a.ai->when;
                 for (size_t at = w.find("&&"); at != std::string::npos; at = w.find("&&")) w.replace(at, 2, ",");
                 return w;
             }())) {
            std::string name;
            for (char c : term) {
                if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') name += c;
                else if (!name.empty()) break;
            }
            if (!name.empty() && !known.contains(name)) problems.push_back("the AI hint asks about '" + name + "' (health, mana, stamina, distance, enemyHealth)");
        }
    }
    return problems;
}

// --- Formulas and balance ---------------------------------------------------------------------

namespace {

struct Parser {
    const std::string& s;
    const std::map<std::string, float>& vars;
    size_t at = 0;
    void Skip() {
        while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at]))) ++at;
    }
    float Primary() {
        Skip();
        if (at < s.size() && s[at] == '(') {
            ++at;
            const float v = Expr();
            Skip();
            if (at < s.size() && s[at] == ')') ++at;
            return v;
        }
        if (at < s.size() && s[at] == '-') {
            ++at;
            return -Primary();
        }
        if (at < s.size() && (std::isdigit(static_cast<unsigned char>(s[at])) || s[at] == '.')) {
            size_t used = 0;
            const float v = std::stof(s.substr(at), &used);
            at += used;
            return v;
        }
        std::string name;
        while (at < s.size() && (std::isalnum(static_cast<unsigned char>(s[at])) || s[at] == '.' || s[at] == '_')) name += s[at++];
        auto it = vars.find(name);
        return it == vars.end() ? 0.0f : it->second;
    }
    float Term() {
        float v = Primary();
        for (;;) {
            Skip();
            if (at < s.size() && s[at] == '*') {
                ++at;
                v *= Primary();
            } else if (at < s.size() && s[at] == '/') {
                ++at;
                const float d = Primary();
                v = d != 0.0f ? v / d : 0.0f;
            } else {
                return v;
            }
        }
    }
    float Expr() {
        float v = Term();
        for (;;) {
            Skip();
            if (at < s.size() && s[at] == '+') {
                ++at;
                v += Term();
            } else if (at < s.size() && s[at] == '-') {
                ++at;
                v -= Term();
            } else {
                return v;
            }
        }
    }
};

nlohmann::json Balance() {
    if (const Record* r = ActiveRecords().Find("magic-balance", "default")) return r->resolved;
    return nlohmann::json::object();
}

} // namespace

float EvaluateFormula(const std::string& formula, const std::map<std::string, float>& variables) {
    try {
        Parser p{formula, variables};
        return p.Expr();
    } catch (...) {
        return 0.0f;
    }
}

float FormulaCost(const AbilityDef& ability) {
    const nlohmann::json b = Balance();
    const float base = b.value("base", 0.6f), me = b.value("magnitudeExponent", 1.1f), de = b.value("durationExponent", 0.35f),
                ae = b.value("areaExponent", 0.5f);
    float total = 0.0f;
    for (const auto& use : ability.effects) {
        auto def = FindMagicEffect(use.effect);
        if (!def) continue;
        const float magnitude = use.magnitude.value_or(EvaluateFormula(def->magnitude, {{"caster.level", 1}}));
        const float duration = use.duration.value_or(def->duration);
        const float area = std::max(def->area, ability.delivery == "area" || ability.delivery == "aura" ? ability.radius : 0.0f);
        total += base * std::pow(std::max(0.0f, std::abs(magnitude)), me) * std::pow(std::max(duration, 1.0f) / 10.0f, de) *
                 std::pow(1.0f + area / 4.0f, ae);
    }
    return std::round(total * 10.0f) / 10.0f;
}

AbilityFigures FiguresOf(const AbilityDef& ability) {
    AbilityFigures f;
    f.formulaCost = FormulaCost(ability);
    f.setCost = ability.cost;
    for (const auto& use : ability.effects) {
        auto def = FindMagicEffect(use.effect);
        if (!def) continue;
        const float magnitude = use.magnitude.value_or(EvaluateFormula(def->magnitude, {{"caster.level", 1}}));
        const float duration = use.duration.value_or(def->duration);
        if (def->archetype == "damage" || def->archetype == "heal" || def->archetype == "absorb") f.output += magnitude;
        else if (def->archetype == "over-time") f.output += magnitude * std::max(1.0f, duration / std::max(0.1f, def->period));
    }
    f.outputPerCost = f.setCost > 0.0f ? f.output / f.setCost : 0.0f;
    const float cycle = std::max(ability.cooldown, ability.castTime + 0.5f);
    f.outputPerSecond = f.output / std::max(0.5f, cycle);
    f.outlier = f.formulaCost > 0.0f && (f.setCost < f.formulaCost * 0.5f || f.setCost > f.formulaCost * 2.0f);
    return f;
}

// --- The system ----------------------------------------------------------------------------------

MagicSystem* MagicOf(Registry& registry) {
    auto* holder = registry.ctx().find<MagicSystem*>();
    return holder ? *holder : nullptr;
}

MagicSystem::MagicSystem(Registry& registry) : registry_(registry) {
    registry_.ctx().insert_or_assign<MagicSystem*>(this);
}

std::map<std::string, float> MagicSystem::Variables(Entity caster, float charge) const {
    std::map<std::string, float> v{{"charge", charge}, {"caster.level", 1.0f}};
    if (!registry_.valid(caster)) return v;
    if (const auto* p = registry_.try_get<Progression>(caster)) v["caster.level"] = static_cast<float>(p->level);
    for (const char* attribute : {"strength", "agility", "intelligence", "willpower", "attack", "defense", "magic"}) {
        v[std::string("caster.") + attribute] = EffectiveAttribute(const_cast<Registry&>(registry_), caster, attribute);
    }
    return v;
}

float MagicSystem::Scale(Entity caster, const AbilityDef& ability, const char* stat) const {
    // Perks (records "perk", taken by the caster) scale abilities by tag:
    // "abilityModifiers": [{"tag": "element.fire", "stat": "magnitude", "value": 1.25}].
    float k = 1.0f;
    const auto* progression = registry_.valid(caster) ? registry_.try_get<Progression>(caster) : nullptr;
    if (!progression) return k;
    for (const auto& perk : SplitList(progression->perks)) {
        const Record* r = ActiveRecords().Find("perk", perk);
        if (!r) continue;
        for (const auto& m : r->resolved.value("abilityModifiers", nlohmann::json::array())) {
            if (!m.is_object() || m.value("stat", "") != stat) continue;
            const std::string tag = m.value("tag", "");
            if (tag.empty() || std::find(ability.tags.begin(), ability.tags.end(), tag) != ability.tags.end()) {
                k *= m.value("value", 1.0f);
            }
        }
    }
    return k;
}

bool MagicSystem::Knows(Entity entity, const std::string& ability) const {
    const auto* a = registry_.valid(entity) ? registry_.try_get<Abilities>(entity) : nullptr;
    if (!a) return false;
    const auto known = SplitList(a->known);
    return std::find(known.begin(), known.end(), ability) != known.end();
}

void MagicSystem::Learn(Entity entity, const std::string& ability) {
    if (!registry_.valid(entity) || Knows(entity, ability)) return;
    auto& a = registry_.get_or_emplace<Abilities>(entity);
    a.known = a.known.empty() ? ability : a.known + ", " + ability;
    auto slots = SplitList(a.slots);
    if (slots.size() < 4) a.slots = a.slots.empty() ? ability : a.slots + ", " + ability;
    if (emit) emit("ability_learned", entity, entt::null, 0.0, ability);
}

bool MagicSystem::HasTag(Entity entity, const std::string& tag) const {
    const auto* s = registry_.valid(entity) ? registry_.try_get<MagicState>(entity) : nullptr;
    if (!s) return false;
    auto it = s->tags.find(tag);
    return it != s->tags.end() && it->second > 0;
}

void MagicSystem::AddTag(Entity entity, const std::string& tag, int count) {
    if (!registry_.valid(entity)) return;
    auto& tags = registry_.get_or_emplace<MagicState>(entity).tags;
    tags[tag] += count;
    if (tags[tag] <= 0) tags.erase(tag);
}

float MagicSystem::CooldownLeft(Entity entity, const std::string& ability) const {
    const auto* s = registry_.valid(entity) ? registry_.try_get<MagicState>(entity) : nullptr;
    if (!s) return 0.0f;
    auto it = s->cooldowns.find(ability);
    return it == s->cooldowns.end() ? 0.0f : std::max(0.0f, it->second);
}

bool MagicSystem::Cast(Entity caster, const std::string& id, float aimX, float aimY, Entity target, std::string* why,
                       bool ignoreKnown) {
    auto fail = [&](const std::string& reason) {
        if (why) *why = reason;
        return false;
    };
    if (!registry_.valid(caster) || registry_.all_of<Dead>(caster)) return fail("can't");
    auto ability = FindAbility(id);
    if (!ability) return fail("no ability '" + id + "'");
    if (!ignoreKnown && !Knows(caster, id)) return fail("doesn't know " + ability->name);
    if (ability->activation == "passive") return fail(ability->name + " is always on");
    auto& state = registry_.get_or_emplace<MagicState>(caster);
    if (HasTag(caster, "state.silenced") && ability->kind == "spell") return fail("silenced");
    if (HasTag(caster, "state.stunned")) return fail("stunned");
    if (!state.casting.empty()) return fail("already casting");
    if (CooldownLeft(caster, id) > 0.0f) return fail(ability->name + " isn't ready");
    if (ability->requiresLevel > 0) {
        const auto* p = registry_.try_get<Progression>(caster);
        if (!p || p->level < ability->requiresLevel) return fail("needs level " + std::to_string(ability->requiresLevel));
    }
    // A toggle switches off again.
    if (ability->activation == "toggle" && state.toggles.contains(id)) {
        state.toggles.erase(id);
        return true;
    }
    const float cost = ability->cost * Scale(caster, *ability, "cost");
    if (cost > 0.0f) {
        float* pool = ResourceOf(registry_, caster, ability->costAttribute);
        if (!pool || *pool < cost) return fail("not enough " + ability->costAttribute);
        *pool -= cost;
    }
    state.cooldowns[id] = ability->cooldown;
    TriggerAnimation(registry_, caster, ability->animation);
    if (const auto* w = registry_.try_get<WorldTransform2D>(caster); w && playCue && !ability->castCue.empty()) {
        playCue(ability->castCue, w->x, w->y, caster, caster);
    }
    if (emit) emit("ability_used", caster, target, cost, id);
    if (ability->castTime > 0.0f) {
        state.casting = id;
        state.castLeft = ability->castTime;
        state.castX = aimX;
        state.castY = aimY;
        state.castTarget = target;
        return true;
    }
    if (ability->activation == "toggle") state.toggles[id] = ability->duration > 0.0f ? ability->duration : 1e9f;
    Deliver(caster, *ability, aimX, aimY, target);
    return true;
}

void MagicSystem::Deliver(Entity caster, const AbilityDef& ability, float aimX, float aimY, Entity target) {
    const auto* from = registry_.try_get<WorldTransform2D>(caster);
    if (!from) return;
    const float cx = from->x, cy = from->y;
    float dx = aimX - cx, dy = aimY - cy;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-3f) {
        const auto* facing = registry_.try_get<Facing>(caster);
        dx = facing ? facing->x : 1.0f;
        dy = facing ? facing->y : 0.0f;
        length = std::max(1e-3f, std::sqrt(dx * dx + dy * dy));
    }
    dx /= length;
    dy /= length;
    registry_.emplace_or_replace<Facing>(caster, dx, dy);
    // Who's in reach, for touch, area and cone.
    auto enemiesWhere = [&](const std::function<bool(float, float)>& inside) {
        std::vector<Entity> hit;
        for (auto [e, health, w] : registry_.view<Health, WorldTransform2D>(entt::exclude<Dead>).each()) {
            if (e == caster || !AreHostile(registry_, caster, e)) continue;
            if (inside(w.x, w.y)) hit.push_back(e);
        }
        return hit;
    };
    if (ability.delivery == "self" || ability.delivery == "aura") {
        ApplyAbilityEffects(caster, ability, caster);
        if (ability.delivery == "aura") registry_.get_or_emplace<MagicState>(caster).toggles[ability.id] = std::max(1.0f, ability.duration);
    } else if (ability.delivery == "touch") {
        auto hit = enemiesWhere([&](float x, float y) { return std::hypot(x - cx, y - cy) <= std::max(1.2f, ability.range); });
        std::sort(hit.begin(), hit.end(), [&](Entity a, Entity b) {
            const auto& wa = registry_.get<WorldTransform2D>(a);
            const auto& wb = registry_.get<WorldTransform2D>(b);
            return std::hypot(wa.x - cx, wa.y - cy) < std::hypot(wb.x - cx, wb.y - cy);
        });
        if (!hit.empty()) ApplyAbilityEffects(caster, ability, hit.front());
    } else if (ability.delivery == "projectile") {
        Entity shot = registry_.create();
        registry_.emplace<WorldTransform2D>(shot, cx + dx * 0.5f, cy + dy * 0.5f);
        Projectile& p = registry_.emplace<Projectile>(shot);
        p.owner = caster;
        p.team = TeamOf(registry_, caster);
        p.damage = 0.0f; // the ability's effects do the work
        p.vx = dx * ability.speed;
        p.vy = dy * ability.speed;
        p.remaining = ability.range / std::max(0.1f, ability.speed);
        registry_.emplace<AbilityProjectile>(shot, ability.id, caster);
        Sprite& sprite = registry_.emplace<Sprite>(shot);
        sprite.width = sprite.height = 12.0f;
        sprite.texture = "fx:dot";
        sprite.emissive = true;
        const bool fire = std::find(ability.tags.begin(), ability.tags.end(), "element.fire") != ability.tags.end();
        const bool frost = std::find(ability.tags.begin(), ability.tags.end(), "element.frost") != ability.tags.end();
        sprite.r = fire ? 1.0f : frost ? 0.6f : 0.8f;
        sprite.g = fire ? 0.55f : frost ? 0.85f : 0.6f;
        sprite.b = fire ? 0.15f : 1.0f;
        registry_.emplace<ScreenTransform2D>(shot);
    } else if (ability.delivery == "area") {
        // At the aim (within range), everyone hostile within the radius.
        float ax = aimX, ay = aimY;
        if (std::hypot(ax - cx, ay - cy) > ability.range) {
            ax = cx + dx * ability.range;
            ay = cy + dy * ability.range;
        }
        if (playCue && !ability.impactCue.empty()) playCue(ability.impactCue, ax, ay, entt::null, caster);
        for (Entity e : enemiesWhere([&](float x, float y) { return std::hypot(x - ax, y - ay) <= ability.radius; })) {
            ApplyAbilityEffects(caster, ability, e);
        }
    } else if (ability.delivery == "cone") {
        for (Entity e : enemiesWhere([&](float x, float y) {
                 const float ex = x - cx, ey = y - cy, d = std::hypot(ex, ey);
                 return d <= ability.range && d > 0.0f && (ex * dx + ey * dy) / d >= 0.7f; // about 90 degrees
             })) {
            ApplyAbilityEffects(caster, ability, e);
        }
    }
}

void MagicSystem::ApplyAbilityEffects(Entity caster, const AbilityDef& ability, Entity target) {
    if (!registry_.valid(target)) return;
    if (playCue && !ability.impactCue.empty() && ability.delivery != "area") {
        if (const auto* w = registry_.try_get<WorldTransform2D>(target)) playCue(ability.impactCue, w->x, w->y, target, caster);
    }
    const float magnitudeScale = Scale(caster, ability, "magnitude");
    const auto vars = Variables(caster, 1.0f);
    for (const auto& use : ability.effects) {
        auto def = FindMagicEffect(use.effect);
        if (!def) continue;
        const float magnitude = use.magnitude.value_or(EvaluateFormula(def->magnitude, vars)) * magnitudeScale;
        // An effect with an area reaches the target's neighbours too.
        std::vector<Entity> targets = {target};
        if (def->area > 0.0f) {
            if (const auto* at = registry_.try_get<WorldTransform2D>(target)) {
                for (auto [e, health, w] : registry_.view<Health, WorldTransform2D>(entt::exclude<Dead>).each()) {
                    if (e != target && std::hypot(w.x - at->x, w.y - at->y) <= def->area &&
                        (def->archetype == "heal" ? !AreHostile(registry_, caster, e) : AreHostile(registry_, caster, e))) {
                        targets.push_back(e);
                    }
                }
            }
        }
        for (Entity t : targets) ApplyEffect(t, use.effect, caster, magnitude, use.duration);
    }
}

bool MagicSystem::ApplyEffect(Entity target, const std::string& id, Entity source, std::optional<float> magnitudeIn,
                              std::optional<float> durationIn) {
    if (!registry_.valid(target) || registry_.all_of<Dead>(target)) return false;
    auto def = FindMagicEffect(id);
    if (!def) return false;
    for (const auto& tag : def->requiresTags) {
        if (!HasTag(target, tag)) return false;
    }
    for (const auto& tag : def->blockedBy) {
        if (HasTag(target, tag)) return false;
    }
    const float magnitude = magnitudeIn.value_or(EvaluateFormula(def->magnitude, Variables(source, 1.0f)));
    const float duration = durationIn.value_or(def->duration);
    const auto* at = registry_.try_get<WorldTransform2D>(target);
    if (playCue && !def->cue.empty() && at) playCue(def->cue, at->x, at->y, target, source);
    if (emit) emit("effect_applied", target, source, magnitude, id);
    const std::string& a = def->archetype;
    // Instant archetypes.
    if (a == "damage") {
        DealDamage(registry_, target, magnitude, source, nullptr, true, false, DamageTypeOf(def->damageType));
        return true;
    }
    if (a == "heal") {
        Heal(registry_, target, magnitude);
        return true;
    }
    if (a == "absorb") {
        const float taken = DealDamage(registry_, target, magnitude, source, nullptr, true, false, DamageTypeOf(def->damageType));
        if (registry_.valid(source)) Heal(registry_, source, taken);
        return true;
    }
    if (a == "displace") {
        if (at && registry_.valid(source)) {
            if (const auto* from = registry_.try_get<WorldTransform2D>(source)) {
                float dx = at->x - from->x, dy = at->y - from->y;
                const float l = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
                // Pushed straight away from the source (walls aren't checked here: keep it short).
                auto& to = registry_.get<WorldTransform2D>(target);
                to.x += dx / l * std::min(magnitude, 3.0f);
                to.y += dy / l * std::min(magnitude, 3.0f);
            }
        }
        return true;
    }
    if (a == "dispel") {
        for (const auto& tag : def->tags) RemoveEffects(target, tag);
        return true;
    }
    if (a == "summon") {
        if (spawn && at && !def->prefab.empty()) {
            const float angle = std::uniform_real_distribution<float>(0.0f, 6.28f)(rng_);
            if (auto made = spawn(def->prefab, at->x + std::cos(angle), at->y + std::sin(angle)); made && duration > 0.0f) {
                registry_.get_or_emplace<MagicState>(*made).effects.push_back({"summoned", source, 0, duration, 1, 0, 1});
            }
        }
        return true;
    }
    if (a == "light") {
        if (light && at) light(at->x, at->y, magnitude, 1.0f, 0.9f, 0.6f, std::max(1.0f, duration));
        return true;
    }
    // Lasting archetypes: modify, over-time, state.
    auto& state = registry_.get_or_emplace<MagicState>(target);
    auto existing = std::find_if(state.effects.begin(), state.effects.end(), [&](const ActiveEffect& e) {
        return e.effect == id && (def->stacking != "separate" || e.source == source);
    });
    if (existing != state.effects.end()) {
        if (def->stacking == "stack" && existing->stacks < def->maxStacks) {
            ++existing->stacks;
            if (a == "modify") {
                AddModifier(registry_, target, {def->attribute, def->op == "mul" ? ModifierOp::Multiply : ModifierOp::Add,
                                                magnitude, "magic:" + id, -1.0f});
            }
        } else if (def->stacking == "strongest") {
            existing->magnitude = std::max(existing->magnitude, magnitude);
        }
        existing->remaining = std::max(existing->remaining, duration); // refresh
        return true;
    }
    ActiveEffect effect{id, source, magnitude, duration, def->period, 0.0f, 1};
    for (const auto& tag : def->tags) AddTag(target, tag);
    if (a == "modify" && !def->attribute.empty()) {
        AddModifier(registry_, target, {def->attribute, def->op == "mul" ? ModifierOp::Multiply : ModifierOp::Add, magnitude,
                                        "magic:" + id, -1.0f});
    }
    if (a == "state") {
        // Stunned and rooted can't move (and stunned can't attack).
        const bool stun = std::find(def->tags.begin(), def->tags.end(), "state.stunned") != def->tags.end();
        const bool root = std::find(def->tags.begin(), def->tags.end(), "state.rooted") != def->tags.end();
        if (stun || root) AddModifier(registry_, target, {"moveSpeed", ModifierOp::Multiply, 0.0f, "magic:" + id, -1.0f});
        if (stun) {
            if (auto* melee = registry_.try_get<MeleeAttack>(target)) melee->cooldownRemaining = std::max(melee->cooldownRemaining, duration);
        }
        if (std::find(def->tags.begin(), def->tags.end(), "state.invisible") != def->tags.end()) {
            if (auto* sprite = registry_.try_get<Sprite>(target)) sprite->a = 0.35f;
        }
    }
    registry_.get<MagicState>(target).effects.push_back(effect);
    return true;
}

void MagicSystem::Expire(Entity entity, const ActiveEffect& effect, const MagicEffectDef& def) {
    for (const auto& tag : def.tags) AddTag(entity, tag, -1);
    if (def.archetype == "modify" || def.archetype == "state") RemoveModifiers(registry_, entity, "magic:" + def.id);
    if (def.archetype == "state" && std::find(def.tags.begin(), def.tags.end(), "state.invisible") != def.tags.end()) {
        if (auto* sprite = registry_.try_get<Sprite>(entity)) sprite->a = 1.0f;
    }
    if (emit) emit("effect_removed", entity, effect.source, 0.0, def.id);
}

void MagicSystem::RemoveEffects(Entity target, const std::string& tag) {
    auto* state = registry_.valid(target) ? registry_.try_get<MagicState>(target) : nullptr;
    if (!state) return;
    std::vector<ActiveEffect> keep;
    for (const auto& e : state->effects) {
        auto def = FindMagicEffect(e.effect);
        const bool match = def && (e.effect == tag || std::find(def->tags.begin(), def->tags.end(), tag) != def->tags.end());
        if (match) Expire(target, e, *def);
        else keep.push_back(e);
    }
    registry_.get<MagicState>(target).effects = std::move(keep);
}

void MagicSystem::Tick(Entity entity, ActiveEffect& effect, const MagicEffectDef& def, float amount) {
    if (def.archetype != "over-time") return;
    if (def.attribute == "heal" || def.attribute == "health+") {
        Heal(registry_, entity, amount);
    } else if (def.attribute == "mana" || def.attribute == "stamina") {
        if (float* pool = ResourceOf(registry_, entity, def.attribute)) *pool += amount;
    } else {
        DealDamage(registry_, entity, amount * effect.stacks, effect.source, nullptr, false, def.nonLethal, DamageTypeOf(def.damageType));
    }
}

void MagicSystem::Update(float dt) {
    std::vector<Entity> entities;
    for (auto [e, state] : registry_.view<MagicState>().each()) entities.push_back(e);
    for (Entity e : entities) {
        if (!registry_.valid(e)) continue;
        auto& state = registry_.get<MagicState>(e);
        for (auto& [id, left] : state.cooldowns) left -= dt;
        // A cast finishing.
        if (!state.casting.empty() && (state.castLeft -= dt) <= 0.0f) {
            const std::string id = state.casting;
            state.casting.clear();
            if (auto ability = FindAbility(id)) {
                if (ability->activation == "toggle") registry_.get<MagicState>(e).toggles[id] = ability->duration > 0 ? ability->duration : 1e9f;
                Deliver(e, *ability, state.castX, state.castY, state.castTarget);
            }
        }
        if (!registry_.valid(e)) continue;
        auto& s = registry_.get<MagicState>(e);
        // Auras and toggles: their effects on those around, each second.
        for (auto it = s.toggles.begin(); it != s.toggles.end();) {
            it->second -= dt;
            auto ability = FindAbility(it->first);
            if (it->second <= 0.0f || !ability) {
                it = s.toggles.erase(it);
                continue;
            }
            const float before = it->second + dt;
            if (std::floor(before) != std::floor(it->second) && ability->delivery == "aura") {
                const auto* w = registry_.try_get<WorldTransform2D>(e);
                for (auto [o, health, ow] : registry_.view<Health, WorldTransform2D>(entt::exclude<Dead>).each()) {
                    if (o != e && w && AreHostile(registry_, e, o) && std::hypot(ow.x - w->x, ow.y - w->y) <= ability->radius) {
                        ApplyAbilityEffects(e, *ability, o);
                    }
                }
            }
            ++it;
        }
        // Periodic ticks and expiry.
        std::vector<ActiveEffect> keep;
        for (auto effect : registry_.get<MagicState>(e).effects) {
            if (effect.effect == "summoned") {
                if ((effect.remaining -= dt) <= 0.0f) {
                    if (auto* h = registry_.try_get<Health>(e)) h->current = 0.0f; // the summoning ends
                } else {
                    keep.push_back(effect);
                }
                continue;
            }
            auto def = FindMagicEffect(effect.effect);
            if (!def) continue;
            if (def->archetype == "over-time") {
                effect.timer += dt;
                while (effect.timer >= effect.period) {
                    effect.timer -= effect.period;
                    Tick(e, effect, *def, effect.magnitude);
                }
            }
            if (effect.remaining > 1e8f || (effect.remaining -= dt) > 0.0f) keep.push_back(effect);
            else Expire(e, effect, *def);
        }
        if (registry_.valid(e)) registry_.get<MagicState>(e).effects = std::move(keep);
    }
    // Passives and what's worn: constant effects while known or equipped.
    for (auto [e, abilities] : registry_.view<Abilities>().each()) {
        for (const auto& id : SplitList(abilities.known)) {
            auto ability = FindAbility(id);
            if (!ability || ability->activation != "passive") continue;
            for (const auto& use : ability->effects) {
                const auto* state = registry_.try_get<MagicState>(e);
                const bool active = state && std::any_of(state->effects.begin(), state->effects.end(),
                                                         [&](const ActiveEffect& a) { return a.effect == use.effect; });
                if (!active) ApplyEffect(e, use.effect, e, use.magnitude, 1e9f);
            }
        }
    }
    for (auto [e, equipment] : registry_.view<Equipment>().each()) {
        std::string worn;
        for (const auto& slot : EquipmentSlots()) {
            Equipment copy = equipment;
            const std::string* item = EquipmentSlot(copy, slot);
            if (!item || item->empty()) continue;
            const Record* r = ActiveRecords().Find("item", *item);
            if (!r || !r->resolved.contains("enchantment")) continue;
            const auto& ench = r->resolved["enchantment"];
            if (ench.value("on", "worn") != "worn") continue;
            for (const auto& eff : ench.value("effects", nlohmann::json::array())) {
                if (eff.is_string()) worn += eff.get<std::string>() + ",";
            }
        }
        const std::string before = wornEnchantments_[e];
        if (worn == before) continue;
        for (const auto& id : SplitList(before)) RemoveEffects(e, id);
        for (const auto& id : SplitList(worn)) ApplyEffect(e, id, e, std::nullopt, 1e9f);
        wornEnchantments_[e] = worn;
    }
}

void MagicSystem::OnHit(Entity attacker, Entity target, const std::string& projectileAbility) {
    if (!registry_.valid(target)) return;
    if (!projectileAbility.empty()) {
        if (auto ability = FindAbility(projectileAbility)) ApplyAbilityEffects(attacker, *ability, target);
        return;
    }
    // The attacker's weapon enchanted "on hit".
    const auto* equipment = registry_.valid(attacker) ? registry_.try_get<Equipment>(attacker) : nullptr;
    if (!equipment || equipment->weapon.empty()) return;
    const Record* r = ActiveRecords().Find("item", equipment->weapon);
    if (!r || !r->resolved.contains("enchantment")) return;
    const auto& ench = r->resolved["enchantment"];
    if (ench.value("on", "worn") != "hit") return;
    for (const auto& eff : ench.value("effects", nlohmann::json::array())) {
        if (eff.is_string()) ApplyEffect(target, eff.get<std::string>(), attacker);
    }
}

bool MagicSystem::UseItem(Entity user, const nlohmann::json& use, std::string* why) {
    bool did = false;
    if (use.contains("learn") && use["learn"].is_string()) {
        const std::string ability = use["learn"].get<std::string>();
        if (!FindAbility(ability)) {
            if (why) *why = "no ability '" + ability + "'";
            return false;
        }
        Learn(user, ability);
        did = true;
    }
    if (use.contains("ability") && use["ability"].is_string()) {
        const auto* w = registry_.try_get<WorldTransform2D>(user);
        const auto* facing = registry_.try_get<Facing>(user);
        const float ax = w ? w->x + (facing ? facing->x : 1.0f) * 3.0f : 0.0f;
        const float ay = w ? w->y + (facing ? facing->y : 0.0f) * 3.0f : 0.0f;
        // A scroll casts what the user may not know, for free, without its cooldown.
        auto ability = FindAbility(use["ability"].get<std::string>());
        if (!ability) {
            if (why) *why = "no ability '" + use["ability"].get<std::string>() + "'";
            return false;
        }
        AbilityDef free = *ability;
        free.cost = 0.0f;
        free.castTime = 0.0f;
        Deliver(user, free, ax, ay, entt::null);
        did = true;
    }
    for (const auto& eff : use.value("effects", nlohmann::json::array())) {
        if (eff.is_string()) did = ApplyEffect(user, eff.get<std::string>(), user) || did;
    }
    return did;
}

bool MagicSystem::ThinkFor(Entity actor, Entity enemy) {
    const auto* abilities = registry_.try_get<Abilities>(actor);
    if (!abilities || registry_.all_of<Dead>(actor)) return false;
    std::map<std::string, float> p;
    if (const auto* h = registry_.try_get<Health>(actor); h && h->max > 0) p["health"] = h->current / h->max;
    if (const auto* r = registry_.try_get<Resources>(actor)) {
        p["mana"] = r->maxMana > 0 ? r->mana / r->maxMana : 0.0f;
        p["stamina"] = r->maxStamina > 0 ? r->stamina / r->maxStamina : 0.0f;
    }
    const auto* me = registry_.try_get<WorldTransform2D>(actor);
    const auto* them = registry_.valid(enemy) ? registry_.try_get<WorldTransform2D>(enemy) : nullptr;
    p["distance"] = me && them ? std::hypot(them->x - me->x, them->y - me->y) : 999.0f;
    if (const auto* h = registry_.valid(enemy) ? registry_.try_get<Health>(enemy) : nullptr; h && h->max > 0) {
        p["enemyHealth"] = h->current / h->max;
    }
    // The highest-priority ability whose hint holds and that's ready.
    std::optional<AbilityDef> best;
    for (const auto& id : SplitList(abilities->known)) {
        auto a = FindAbility(id);
        if (!a || !a->ai || a->activation == "passive" || CooldownLeft(actor, id) > 0.0f) continue;
        const std::string& use = a->ai->use;
        if ((use == "attack" || use == "debuff") && (!them || p["distance"] > a->range + 0.5f)) continue;
        if (!AnimConditionHolds(a->ai->when, p)) continue;
        if (!best || a->ai->priority > best->ai->priority) best = a;
    }
    if (!best) return false;
    const bool self = best->ai->use == "heal-self" || best->ai->use == "buff";
    const float ax = self || !them ? (me ? me->x : 0) : them->x, ay = self || !them ? (me ? me->y : 0) : them->y;
    return Cast(actor, best->id, ax, ay, self ? actor : enemy);
}

} // namespace aige
