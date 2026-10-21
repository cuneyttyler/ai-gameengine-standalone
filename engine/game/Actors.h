#pragma once

#include "ecs/Registry.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace aige {

// Persistent actors (development_plan.md 14.4, ai-packages.md §7): a character
// with an Actor key is one person in the whole game. The roster remembers where
// each is (which scene, where in it), whether it's alive and its health; when a
// scene starts, copies of actors who are elsewhere or dead are removed, and
// actors whose day brings them here (their package's location.scene) are made
// from their prefab.
struct ActorEntry {
    std::string scene;
    bool alive = true;
    std::string prefab;
    float x = NAN, y = NAN; // where it was last seen in its scene (NaN: arriving; at its package's place)
    float health = -1.0f;   // < 0: as the prefab has it
};

class ActorRoster {
public:
    using SpawnFn = std::function<std::optional<Entity>(const std::string& prefab, float x, float y)>;

    // The player leaves `scene`: each actor there is remembered as it is; one whose
    // package says it's due somewhere else goes there.
    void Leave(Registry& registry, const std::string& scene);
    // `scene` has just been loaded (a new game, travel, a load): removes copies of
    // actors who are dead or elsewhere, and makes the ones who belong here now.
    void Enter(Registry& registry, const std::string& scene, const SpawnFn& spawn);
    void OnDeath(Registry& registry, Entity entity);
    void OnDeath(const std::string& key) {
        if (!key.empty()) entries_[key].alive = false;
    }

    void Clear() { entries_.clear(); }
    const std::map<std::string, ActorEntry>& Entries() const { return entries_; }
    nlohmann::json ToJson() const;
    void FromJson(const nlohmann::json& json);

private:
    std::map<std::string, ActorEntry> entries_;
};

} // namespace aige
