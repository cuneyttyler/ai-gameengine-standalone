#pragma once

#include "ecs/Registry.h"
#include "physics2d/Collision.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace aige {

class StoryState;

// Character AI (development_plan.md 14, ai-packages.md): what a character does,
// when and where, as a stack of packages (records/package/<id>.json); each
// package runs a procedure (a small behaviour tree) from its template; combat
// and reactions take over when perception or events call for it, and the
// package resumes after.

enum class Aggression { Peaceful, Defensive, Aggressive, Frenzied };
enum class Confidence { Cowardly, Cautious, Average, Brave, Foolhardy };
enum class Assistance { Nobody, Allies, Friends };

struct Behaviour {
    std::string packages;   // package record ids, first that holds runs: "innkeeper-work, innkeeper-sleep"
    std::string home;       // aliases packages use (@home, @work, @bed): marker names
    std::string work;
    std::string bed;
    Aggression aggression = Aggression::Peaceful;
    Confidence confidence = Confidence::Average;
    Assistance assistance = Assistance::Allies;
    std::string combatStyle; // a combat-style record id ("" = melee, flee at 20%)
    float speed = 1.6f;      // walking speed, cells per second
};

// What a character notices (14.5).
struct Perception {
    float sight = 7.0f;   // cells
    float angle = 140.0f; // degrees, the cone it sees in
    float hearing = 6.0f; // cells it hears an ordinary noise from
};

// Something characters use (14.3): a bed, a chair, a counter, a forge.
struct SmartObject {
    std::string type;        // bed, chair, table, counter, anvil, fireplace, well, bookshelf, altar...
    std::string affordances; // sleep, sit, eat, drink, work, read, pray, warm: comma-separated
    int slots = 1;           // users at once
    std::string owner;       // a character's name or World Bible key ("" = anyone)
    float minDuration = 4.0f;
    float maxDuration = 12.0f;
};

// One-line remarks (14.5): alternatives separated by '|'.
struct Barks {
    std::string alert; // when it notices the player as an enemy
    std::string hit;   // when it's hurt
    std::string idle;  // now and then, out of combat
    std::string flee;  // when it runs
};

// The same character in every scene (14.4): its key (World Bible key or a
// unique name) and the prefab it's made from when it walks into another scene.
struct Actor {
    std::string key;
    std::string prefab;
};

// --- Runtime ------------------------------------------------------------------------------

enum class AiStatus { Running, Success, Failure };

struct ProcedureNode {
    std::string type;
    nlohmann::json params;
    std::vector<int> children;
};

// A template's procedure, flattened (node 0 is the root).
struct Procedure {
    std::vector<ProcedureNode> nodes;
};

// The built-in templates (ai-packages.md §5) plus the project's procedure
// records, by name.
const Procedure* FindProcedure(const std::string& name);
// Parses a procedure tree: {"type": "sequence", "children": [...], ...params}.
std::optional<Procedure> ParseProcedure(const nlohmann::json& tree, std::string* error = nullptr);
std::vector<std::string> ProcedureNames();

struct BehaviourState {
    enum class Mode { Package, Combat, Flee, Search };
    Mode mode = Mode::Package;
    std::string package;                        // the active package's id
    std::vector<std::string> pushed;            // packages pushed on top (quests, scripts)
    std::map<int, nlohmann::json> nodeState;    // per procedure node
    std::map<std::string, nlohmann::json> blackboard;
    float reconsider = 0.0f;                    // seconds to the next look at the stack
    // Moving.
    std::vector<std::pair<int, int>> path;
    size_t next = 0;
    std::pair<int, int> goal{INT32_MIN, INT32_MIN};
    float stuck = 0.0f;
    // Using a smart object.
    Entity using_ = entt::null;
    bool hidden = false;
    float savedAlpha = 1.0f;
    // Awareness of the player (0..1) and the target in combat.
    float awareness = 0.0f;
    Entity target = entt::null;
    float lastX = 0.0f, lastY = 0.0f; // where the target was last seen or heard
    float lostFor = 0.0f;
    float barkCooldown = 0.0f;
    float idleBarkIn = 20.0f;
};

// A noise someone might hear (a swing, a shot, a door).
struct AiNoise {
    float x = 0.0f, y = 0.0f;
    float loudness = 1.0f; // 1 = an ordinary noise: heard within hearing cells
    Entity source = entt::null;
};

// What the AI needs from the game around it.
struct AiContext {
    Registry& registry;
    const SolidQueryFn& isSolid;
    float minuteOfDay = 0.0f;
    int day = 1;
    const StoryState* story = nullptr;
    std::string scene;
    std::mt19937* rng = nullptr;
    std::vector<AiNoise> noises;
    std::function<void(Entity speaker, const std::string& text)> bark;
    std::function<void(Entity speaker, const std::string& dialogue)> startDialogue;
    std::function<void(const std::string& event, Entity entity)> emit;
    // A scripted package's step (14.6): run its Lua; false when it's finished.
    std::function<bool(Entity actor, const std::string& script, float dt)> runScript;
};

// Is the package's schedule and are its conditions true now?
bool PackageHolds(const nlohmann::json& package, const AiContext& context, Entity actor);

// One fixed step for every character with Behaviour: perception, the package
// stack, the active procedure or combat, movement.
void UpdateBehaviours(AiContext& context, float dt);

// Something happened to an actor (hurt): it turns on the attacker.
void AlertActor(Registry& registry, Entity actor, Entity attacker);

// Pushes a package on top of an actor's stack until popped (quests, scripts).
void PushPackage(Registry& registry, Entity actor, const std::string& package);
void PopPackage(Registry& registry, Entity actor, const std::string& package);

// The affordances a catalog role gives a built object ("bed" -> "sleep"), empty
// when it isn't something characters use.
std::string AffordancesForRole(const std::string& role);

// A short description of what an actor is doing ("innkeeper-work: use-object"),
// for the debug overlay.
std::string DescribeBehaviour(Registry& registry, Entity actor);

} // namespace aige
