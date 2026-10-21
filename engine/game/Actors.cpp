#include "game/Actors.h"

#include "gameplay/Behaviour.h"
#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "records/Records.h"

#include <vector>

namespace aige {

namespace {

// The scene an actor's active package puts it in ("" = wherever it is).
std::string PackageScene(Registry& registry, Entity entity) {
    const auto* state = registry.try_get<BehaviourState>(entity);
    if (!state || state->package.empty()) return {};
    const Record* record = ActiveRecords().Find("package", state->package);
    if (!record) return {};
    return record->resolved.value("location", nlohmann::json::object()).value("scene", std::string());
}

} // namespace

void ActorRoster::Leave(Registry& registry, const std::string& scene) {
    for (auto [entity, actor] : registry.view<Actor>().each()) {
        if (actor.key.empty()) continue;
        auto& entry = entries_[actor.key];
        entry.prefab = actor.prefab.empty() ? entry.prefab : actor.prefab;
        entry.alive = !registry.all_of<Dead>(entity);
        if (const auto* health = registry.try_get<Health>(entity)) entry.health = health->current;
        const std::string due = PackageScene(registry, entity);
        if (!due.empty() && due != scene) {
            entry.scene = due; // its day takes it there
            entry.x = entry.y = NAN;
        } else {
            entry.scene = scene;
            if (const auto* w = registry.try_get<WorldTransform2D>(entity)) {
                entry.x = w->x;
                entry.y = w->y;
            }
        }
    }
}

void ActorRoster::Enter(Registry& registry, const std::string& scene, const SpawnFn& spawn) {
    std::map<std::string, Entity> present;
    std::vector<Entity> remove;
    for (auto [entity, actor] : registry.view<Actor>().each()) {
        if (actor.key.empty()) continue;
        auto it = entries_.find(actor.key);
        if (it == entries_.end()) {
            // First seen: it belongs here.
            ActorEntry entry;
            entry.scene = scene;
            entry.prefab = actor.prefab;
            entries_[actor.key] = entry;
            present[actor.key] = entity;
            continue;
        }
        if (!it->second.alive || (!it->second.scene.empty() && it->second.scene != scene) || present.contains(actor.key)) {
            remove.push_back(entity); // dead, elsewhere, or a second copy
            continue;
        }
        present[actor.key] = entity;
        if (std::isfinite(it->second.x)) {
            auto& w = registry.get<WorldTransform2D>(entity);
            w.x = it->second.x;
            w.y = it->second.y;
        }
        if (auto* health = registry.try_get<Health>(entity); health && it->second.health >= 0.0f) health->current = it->second.health;
    }
    for (Entity entity : remove) registry.destroy(entity);
    // Those due here who weren't placed in this scene arrive.
    if (!spawn) return;
    for (auto& [key, entry] : entries_) {
        if (!entry.alive || entry.scene != scene || present.contains(key) || entry.prefab.empty()) continue;
        float x = entry.x, y = entry.y;
        if (!std::isfinite(x)) {
            // Arriving: at the scene's first marker (its routine walks it on from there).
            x = y = NAN;
            for (auto [m, mk, w] : registry.view<Marker, WorldTransform2D>().each()) {
                if (!std::isfinite(x)) {
                    x = w.x;
                    y = w.y;
                }
            }
        }
        if (!std::isfinite(x)) continue;
        if (auto made = spawn(entry.prefab, x, y)) {
            registry.emplace_or_replace<Actor>(*made, key, entry.prefab);
            if (auto* health = registry.try_get<Health>(*made); health && entry.health >= 0.0f) health->current = entry.health;
        }
    }
}

void ActorRoster::OnDeath(Registry& registry, Entity entity) {
    if (const auto* actor = registry.valid(entity) ? registry.try_get<Actor>(entity) : nullptr; actor && !actor->key.empty()) {
        entries_[actor->key].alive = false;
    }
}

nlohmann::json ActorRoster::ToJson() const {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [key, e] : entries_) {
        nlohmann::json j = {{"scene", e.scene}, {"alive", e.alive}, {"prefab", e.prefab}, {"health", e.health}};
        if (std::isfinite(e.x)) {
            j["x"] = e.x;
            j["y"] = e.y;
        }
        out[key] = j;
    }
    return out;
}

void ActorRoster::FromJson(const nlohmann::json& json) {
    entries_.clear();
    if (!json.is_object()) return;
    for (const auto& [key, j] : json.items()) {
        ActorEntry e;
        e.scene = j.value("scene", std::string());
        e.alive = j.value("alive", true);
        e.prefab = j.value("prefab", std::string());
        e.health = j.value("health", -1.0f);
        if (j.contains("x")) {
            e.x = j.value("x", 0.0f);
            e.y = j.value("y", 0.0f);
        }
        entries_[key] = e;
    }
}

} // namespace aige
