#pragma once

#include "ecs/Registry.h"
#include "reflection/Reflection.h"

#include <nlohmann/json.hpp>

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aige {

class RecordDatabase;

// Difficulty (development_plan.md Phase 16, docs/difficulty.md). Files hold base
// values; a difficulty level scales the fields registered as scalable, by the
// field's kind and its owner's side, through the game's category table, plus the
// level's own modifiers and per-entity overrides set by hand.
//
//   records/difficulty/<id>.json        a level: category steps (1-5), modifiers, rewards
//   records/difficulty-table/default.json   multipliers per category, kind and step

// Whose a field is: the player's, an enemy's or an ally's.
enum class DifficultyOwner { Player, Enemy, Ally };
const char* DifficultyOwnerName(DifficultyOwner owner);

// The category a scalable field falls in, from its kind and owner: health on an
// enemy is "enemies", on the player "player"; gold and loot are "resources"; an
// enemy's perception is "stealth".
std::string DifficultyCategory(const std::string& kind, DifficultyOwner owner);
const std::vector<std::string>& DifficultyCategories();

struct DifficultyModifier {
    std::string target;        // "Health.max", "MeleeAttack.damage"
    std::string owner = "any"; // player | enemy | ally | any
    std::string tag;           // only entities with this tag (a Name containing it, or a Tags component)
    std::string op = "mul";    // mul | add | set | min | max
    float value = 1.0f;
};

struct DifficultyLevel {
    std::string id;
    std::string name;
    std::string description;
    int order = 0;
    std::map<std::string, int> steps; // category -> 1..5 (3: as designed)
    std::vector<DifficultyModifier> modifiers;
    float xp = 1.0f;          // experience rewards x this
    bool permadeath = false;  // one save; death ends the run
};

// Multipliers per category and kind, for steps 1-5.
struct DifficultyTable {
    std::map<std::string, std::map<std::string, std::array<float, 5>>> rows;
    static DifficultyTable Default();
    float Multiplier(const std::string& category, const std::string& kind, int step) const;
    nlohmann::json ToJson() const;
    static DifficultyTable FromJson(const nlohmann::json& json);
};

class DifficultyModel {
public:
    // The project's levels and table (records), else the built-in five levels
    // (Story, Easy, Normal, Hard, Nightmare) and the default table.
    static DifficultyModel FromRecords(const RecordDatabase& records);
    static DifficultyModel BuiltIn();

    const std::vector<DifficultyLevel>& Levels() const { return levels_; }
    const DifficultyLevel* Find(const std::string& id) const;
    const DifficultyTable& Table() const { return table_; }
    // Project Settings ▸ Difficulty.defaultLevel when it's one of them, else
    // "normal", else the middle one.
    std::string DefaultLevel() const;
    bool BuiltInLevels() const { return builtIn_; }

    struct Value {
        float value = 0.0f;
        bool overridden = false;
        std::vector<std::string> why; // "enemies health x1.5 (step 4)", "modifier x2"
    };
    // A field's value at a level. `overrides` is the entity's {"Health.max": {"hard": 20}};
    // `shift` moves every category's step (adaptive difficulty, bounded to 1-5).
    Value Effective(float base, const FieldMeta& field, const std::string& path, DifficultyOwner owner,
                    const std::string& name, const std::string& level, const nlohmann::json& overrides,
                    int shift = 0) const;

private:
    std::vector<DifficultyLevel> levels_;
    DifficultyTable table_;
    bool builtIn_ = false;
};

DifficultyLevel DifficultyLevelFromJson(const std::string& id, const nlohmann::json& json);

// --- On entities ------------------------------------------------------------------

// Hand-set values for some levels, as text: {"Health.max": {"hard": 20}}.
struct DifficultyOverrides {
    std::string values;
};

// Runtime: the values the scene gave each scaled field, so changing level
// recomputes from them and nothing scaled is ever written back.
struct DifficultyBase {
    std::map<std::string, float> values; // "Health.max" -> 10
};

DifficultyOwner OwnerOf(Registry& registry, Entity entity);

// Scales every entity's scalable fields for `level` (from their base values),
// keeping each Health's current share of its max. Returns how many fields changed.
int ApplyDifficulty(Registry& registry, const ReflectionRegistry& reflection, const DifficultyModel& model,
                    const std::string& level, int shift = 0);
// The base values back (leaving play, saving a scene).
void RestoreDifficultyBase(Registry& registry, const ReflectionRegistry& reflection);

// Every scalable field of an entity at every level, for the editor's preview:
// [{"field": "Health.max", "base": 10, "kind": "health", "category": "enemies",
//   "levels": [{"level": "hard", "value": 15, "overridden": false, "why": [...]}]}]
nlohmann::json DifficultyPreview(Registry& registry, const ReflectionRegistry& reflection, Entity entity,
                                 const DifficultyModel& model);

// An enemy against the player at each level (§6): hits each way, seconds to kill.
struct CombatPreviewRow {
    std::string level;
    float enemyHealth = 0, enemyDamage = 0, playerHealth = 0, playerDamage = 0;
    int hitsToKillEnemy = 0;  // the player's hits
    int hitsToKillPlayer = 0; // the enemy's hits
    float secondsToKillEnemy = 0, secondsToKillPlayer = 0;
};
std::vector<CombatPreviewRow> CombatPreview(Registry& registry, const ReflectionRegistry& reflection, Entity enemy,
                                            Entity player, const DifficultyModel& model);

// --- Level scaling (16.3) -----------------------------------------------------------

// A scene's level band: enemies there are made at a level within it.
struct Zone {
    int minLevel = 1;
    int maxLevel = 1;
};
// The level a spawner makes its encounter at, by Project Settings ▸ Difficulty.scaling.
int EncounterLevel(Registry& registry, int spawnerLevel, int playerLevel);

// --- The difficulty curve (16.5, difficulty.md §6) -----------------------------------

// A chapter of the critical path: a scene, its Zone band and its enemies' levels.
struct CurveChapter {
    std::string scene;
    int minLevel = 0, maxLevel = 0; // 0: no Zone
    std::vector<int> enemyLevels;
};
struct CurveProblem {
    std::string scene;
    std::string message;
};
// Bands should rise along the path (`sawtooth` allows a dip after a peak), not jump
// by more than `maxJump` levels, and hold their enemies.
std::vector<CurveProblem> CheckDifficultyCurve(const std::vector<CurveChapter>& chapters, bool sawtooth = false,
                                               int maxJump = 2);

// --- Adaptive difficulty (16.4) -------------------------------------------------------

// Moves the enemies' step by at most one from the chosen level: down after three
// deaths without a win, back after two clean fights.
struct AdaptiveDifficulty {
    int shift = 0; // -1, 0 or +1 (applied to the enemies and stealth categories)
    int deaths = 0;
    int cleanWins = 0;
    void OnPlayerDeath();
    void OnFightWon(bool tookDamage);
    nlohmann::json ToJson() const;
    static AdaptiveDifficulty FromJson(const nlohmann::json& json);
};

// --- Assists (16.4) -------------------------------------------------------------------

// Help a player can turn on, apart from the level (the player's settings).
struct Assists {
    bool invincible = false;
    float damageReduction = 0.0f; // 0-0.9 of every hit on the player
    bool growingReduction = false; // +2% per death, up to 80% (Hades' God Mode)
    float gameSpeed = 1.0f;       // 0.5-1
    int deaths = 0;               // runtime, for the growing reduction
    float Reduction() const;
};
Assists& ActiveAssists();

} // namespace aige
