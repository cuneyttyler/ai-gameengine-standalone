#pragma once

#include "ecs/Registry.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace aige {

// Abilities and magic (development_plan.md Phase 19, docs/systems/magic.md).
//
//   records/magic-effect/<id>   what changes: damage, heal, modify (an attribute for a
//                               while), over-time, absorb, state (tags: stunned, rooted,
//                               silenced, invisible), summon, displace, dispel, light
//   records/ability/<id>        how: activation (press, hold, toggle, passive), delivery
//                               (self, touch, projectile, area, cone, aura), cost,
//                               cooldown, cast time, the effects, cues, AI hints
// Engine presets of both live in data/magic; a project's record of the same id wins.
// Potions and scrolls use abilities, books teach them, enchantments apply effects while
// worn or on hit, and perks scale abilities by tag.

struct MagicEffectDef {
    std::string id;
    std::string archetype = "damage"; // damage | heal | modify | over-time | absorb | state | summon | displace | dispel | light
    std::string attribute;             // modify: which; over-time: "health" (damage) or "heal"
    std::string op = "add";            // modify: add | mul
    std::string magnitude = "10";      // a number or a formula ("10 + 2 * caster.level")
    float duration = 0.0f;             // 0: instant
    float period = 1.0f;               // over-time
    float area = 0.0f;                 // cells around the target; 0: the target only
    std::string damageType = "physical";
    std::string stacking = "refresh";  // refresh | stack | strongest | separate
    int maxStacks = 3;
    std::vector<std::string> tags;     // granted while active (state.stunned, element.fire)
    std::vector<std::string> requiresTags, blockedBy; // tags the target must or mustn't have
    std::string cue;                   // a visual effect (records "effect") on apply
    std::string prefab;                // summon
    bool nonLethal = false;            // over-time: can't kill (poison)
};

struct AbilityDef {
    std::string id;
    std::string name;
    std::string kind = "spell";      // spell | skill | power | passive | item-use | shout
    std::string school;
    std::string activation = "press"; // press | hold | toggle | passive
    std::string delivery = "self";    // self | touch | projectile | area | cone | aura
    float range = 6.0f;               // cells (projectile, area, cone, touch)
    float radius = 1.5f;              // area, aura
    float speed = 8.0f;               // projectile, cells per second
    std::string costAttribute = "mana"; // mana | stamina | health
    float cost = 0.0f;                // per use, or per second for hold
    float cooldown = 0.0f, castTime = 0.0f, duration = 0.0f; // duration: aura and toggle
    struct Use {
        std::string effect;
        std::optional<float> magnitude, duration;
    };
    std::vector<Use> effects;
    std::vector<std::string> tags;    // element.fire, school.destruction (perks scale by them)
    std::string animation = "casting"; // the caster's animator trigger
    std::string castCue, impactCue;   // visual effects
    int requiresLevel = 0;
    struct AiHint {
        std::string use = "attack";   // attack | heal-self | buff | debuff
        std::string when;             // "health < 0.4", "distance < 5"
        int priority = 0;
    };
    std::optional<AiHint> ai;
};

MagicEffectDef MagicEffectFromJson(const std::string& id, const nlohmann::json& json);
AbilityDef AbilityFromJson(const std::string& id, const nlohmann::json& json);
std::optional<MagicEffectDef> FindMagicEffect(const std::string& id);
std::optional<AbilityDef> FindAbility(const std::string& id);
std::vector<std::string> MagicEffectPresets();
std::vector<std::string> AbilityPresets();
std::vector<std::string> ValidateAbility(const AbilityDef& ability);

// A magnitude formula: numbers, + - * / and parentheses, and variables
// (caster.level, caster.<attribute>, charge). Unknown variables are 0.
float EvaluateFormula(const std::string& formula, const std::map<std::string, float>& variables);

// The cost an ability's effects suggest (magic.md §6), with the game's constants
// (records/magic-balance/default: base, magnitudeExponent, durationExponent, areaExponent).
float FormulaCost(const AbilityDef& ability);
// Damage or healing per point of cost, and per second (the editor's figures).
struct AbilityFigures {
    float formulaCost = 0, setCost = 0, output = 0, outputPerCost = 0, outputPerSecond = 0;
    bool outlier = false; // the set cost is far from the formula's
};
AbilityFigures FiguresOf(const AbilityDef& ability);

// --- On entities ---------------------------------------------------------------------------

// What a character can do: known abilities and the quick slots (both "a, b, c").
struct Abilities {
    std::string known;
    std::string slots;
};

// Runtime: cooldowns, toggles, a cast in progress, the effects on it and its tags.
struct ActiveEffect {
    std::string effect;
    Entity source = entt::null;
    float magnitude = 0, remaining = 0, period = 1, timer = 0;
    int stacks = 1;
};
struct MagicState {
    std::map<std::string, float> cooldowns;
    std::map<std::string, float> toggles; // ability -> time left (aura)
    std::vector<ActiveEffect> effects;
    std::map<std::string, int> tags;
    std::string casting; // an ability being cast (cast time)
    float castLeft = 0;
    float castX = 0, castY = 0; // its aim
    Entity castTarget = entt::null;
};

// An enchanted item's effects (on the item record: "enchantment": {"effects": [...], "on": "worn"|"hit"}).
// A projectile fired by an ability (carries it to whatever it hits).
struct AbilityProjectile {
    std::string ability;
    Entity caster = entt::null;
};

std::vector<std::string> SplitList(const std::string& list);

// The system, owned by the session and reachable from the registry's context (so item
// use in any module can cast).
class MagicSystem {
public:
    explicit MagicSystem(Registry& registry);
    ~MagicSystem(); // leaves the registry's context if it's still there
    MagicSystem(const MagicSystem&) = delete;
    MagicSystem& operator=(const MagicSystem&) = delete;

    // Hooks to the rest of the game.
    std::function<void(const std::string& effect, float x, float y, Entity target, Entity source)> playCue;
    std::function<std::optional<Entity>(const std::string& prefab, float x, float y)> spawn;
    std::function<void(const std::string& name, Entity entity, Entity other, double amount, const std::string& text)> emit;
    std::function<void(float x, float y, float radius, float r, float g, float b, float seconds)> light;

    // Casts a known (or item-given) ability toward a point (or at a target). False and a
    // reason when it can't: cooldown, cost, a silenced caster, an unknown ability.
    bool Cast(Entity caster, const std::string& ability, float aimX, float aimY, Entity target = entt::null,
              std::string* why = nullptr, bool ignoreKnown = false);
    // Applies a magic effect (from an ability, an item, an enchantment, Lua).
    bool ApplyEffect(Entity target, const std::string& effect, Entity source, std::optional<float> magnitude = std::nullopt,
                     std::optional<float> duration = std::nullopt);
    void RemoveEffects(Entity target, const std::string& tag);
    bool HasTag(Entity entity, const std::string& tag) const;
    void AddTag(Entity entity, const std::string& tag, int count = 1);
    float CooldownLeft(Entity entity, const std::string& ability) const;
    void Learn(Entity entity, const std::string& ability);
    bool Knows(Entity entity, const std::string& ability) const;

    // Uses an item record's "use" with magic in it ({"ability": id} casts, {"learn": id}
    // teaches, {"effects": [...]} applies to self). False when the item has none.
    bool UseItem(Entity user, const nlohmann::json& use, std::string* why);

    // One step: casts finishing, auras, periodic effects, expiry, passives, enchantments
    // while worn; projectile hits and touch hits come in through OnHit.
    void Update(float dt);
    // A hit by `attacker` on `target`: its weapon's enchantment on hit, a touch ability, an
    // ability projectile's effects.
    void OnHit(Entity attacker, Entity target, const std::string& projectileAbility);

    // Character AI (19.5): casts the ability whose hint holds, toward `enemy`.
    bool ThinkFor(Entity actor, Entity enemy);

private:
    Registry& registry_;
    std::mt19937 rng_{0x3A61u};
    std::map<std::string, float> Variables(Entity caster, float charge) const;
    float Scale(Entity caster, const AbilityDef& ability, const char* stat) const; // perks by tag
    void Deliver(Entity caster, const AbilityDef& ability, float aimX, float aimY, Entity target);
    void ApplyAbilityEffects(Entity caster, const AbilityDef& ability, Entity target);
    void Tick(Entity entity, ActiveEffect& effect, const MagicEffectDef& def, float amount);
    void Expire(Entity entity, const ActiveEffect& effect, const MagicEffectDef& def);
    std::map<Entity, std::string> wornEnchantments_; // what's applied from equipment
};

// The session's magic, from the registry (null without one).
MagicSystem* MagicOf(Registry& registry);

} // namespace aige
