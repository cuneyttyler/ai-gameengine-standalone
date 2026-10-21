#include "gameplay/Encounters.h"

#include "gameplay/GameSession.h"
#include "gameplay/Attributes.h"
#include "gameplay/Difficulty.h"

#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "records/Records.h"
#include "renderer2d/Components.h"

#include <cmath>
#include <map>

namespace aige {

std::optional<std::pair<std::string, int>> RollEncounter(const std::string& encounter, int level, std::mt19937& rng) {
    const Record* record = ActiveRecords().Find("encounter", encounter);
    if (!record) return std::nullopt;
    std::vector<const nlohmann::json*> entries;
    float total = 0.0f;
    const nlohmann::json list = record->resolved.value("entries", nlohmann::json::array());
    for (const auto& e : list) {
        const int lo = e.value("levelMin", 0), hi = e.value("levelMax", 0);
        if ((lo > 0 && level < lo) || (hi > 0 && level > hi) || e.value("prefab", std::string()).empty()) continue;
        entries.push_back(&e);
        total += std::max(0.0f, e.value("weight", 1.0f));
    }
    if (entries.empty() || total <= 0.0f) return std::nullopt;
    float pick = std::uniform_real_distribution<float>(0.0f, total)(rng);
    const nlohmann::json* chosen = entries.back();
    for (const auto* e : entries) {
        pick -= std::max(0.0f, e->value("weight", 1.0f));
        if (pick <= 0.0f) {
            chosen = e;
            break;
        }
    }
    const int lo = std::max(1, chosen->value("min", 1)), hi = std::max(lo, chosen->value("max", lo));
    return std::pair{chosen->value("prefab", std::string()), std::uniform_int_distribution<int>(lo, hi)(rng)};
}

std::vector<Entity> UpdateSpawners(Registry& registry, float dt, const SpawnPrefabFn& spawn,
                                   const std::function<bool(float, float)>& isFree, std::mt19937& rng) {
    std::vector<Entity> made;
    if (!spawn) return made;
    // How many of each spawner's are alive.
    std::map<Entity, int> alive;
    for (auto [entity, by] : registry.view<SpawnedBy>(entt::exclude<Dead>).each()) ++alive[by.spawner];
    std::vector<Entity> spawners;
    for (auto entity : registry.view<Spawner, WorldTransform2D>()) spawners.push_back(entity);
    int playerLevel = 1;
    if (auto player = FindPlayer(registry)) {
        if (const auto* progression = registry.try_get<Progression>(*player)) playerLevel = progression->level;
    }
    for (Entity entity : spawners) {
        if (!registry.valid(entity)) continue;
        auto& spawner = registry.get<Spawner>(entity);
        const auto at = registry.get<WorldTransform2D>(entity);
        int want = 0;
        if (!spawner.started) {
            spawner.started = true;
            want = spawner.maxAlive;
        } else if (alive[entity] < spawner.maxAlive && spawner.respawnSeconds > 0.0f) {
            spawner.timer += dt;
            if (spawner.timer >= spawner.respawnSeconds) {
                spawner.timer = 0.0f;
                want = 1;
            }
        }
        for (int made_here = 0; want > 0 && alive[entity] < spawner.maxAlive && made_here < 32;) {
            // The level by Project Settings ▸ Difficulty.scaling (16.3).
            const int level = EncounterLevel(registry, spawner.level, playerLevel);
            auto roll = RollEncounter(spawner.encounter, level, rng);
            if (!roll) break;
            for (int n = 0; n < roll->second && want > 0 && alive[entity] < spawner.maxAlive; ++n, ++made_here) {
                // A free spot within the radius (a few tries), else the spawner's own cell.
                float x = at.x, y = at.y;
                for (int attempt = 0; attempt < 12; ++attempt) {
                    const float angle = std::uniform_real_distribution<float>(0.0f, 6.2831853f)(rng);
                    const float r = std::uniform_real_distribution<float>(0.0f, spawner.radius)(rng);
                    const float cx = std::floor(at.x + std::cos(angle) * r) + 0.5f;
                    const float cy = std::floor(at.y + std::sin(angle) * r) + 0.5f;
                    if (!isFree || isFree(cx, cy)) {
                        x = cx;
                        y = cy;
                        break;
                    }
                }
                if (auto spawned = spawn(roll->first, x, y)) {
                    registry.emplace_or_replace<SpawnedBy>(*spawned, entity);
                    if (auto* progression = registry.try_get<Progression>(*spawned)) progression->level = level;
                    made.push_back(*spawned);
                    ++alive[entity];
                    --want;
                }
            }
            if (roll->second <= 0) break;
        }
    }
    return made;
}

std::vector<Entity> DropLoot(Registry& registry, float x, float y, const std::vector<ItemStack>& items, int gold,
                             std::mt19937& rng) {
    std::vector<Entity> made;
    std::vector<ItemStack> all = items;
    if (gold > 0) all.push_back({"gold", gold});
    for (const auto& stack : all) {
        const Entity e = registry.create();
        const float dx = std::uniform_real_distribution<float>(-0.45f, 0.45f)(rng);
        const float dy = std::uniform_real_distribution<float>(-0.45f, 0.45f)(rng);
        registry.emplace<WorldTransform2D>(e, x + dx, y + dy);
        registry.emplace<ScreenTransform2D>(e);
        registry.emplace<ItemPickup>(e, stack.item, stack.count, false);
        Sprite& sprite = registry.emplace<Sprite>(e);
        sprite.width = 20.0f;
        sprite.height = 20.0f;
        sprite.flat = true;
        if (const Record* record = ActiveRecords().Find("item", stack.item)) {
            sprite.texture = record->resolved.value("icon", std::string());
        }
        if (sprite.texture.empty()) {
            // A coin-coloured or plain token where there's no icon.
            const bool coins = stack.item == "gold";
            sprite.r = coins ? 1.0f : 0.8f;
            sprite.g = coins ? 0.82f : 0.8f;
            sprite.b = coins ? 0.2f : 0.9f;
            sprite.width = sprite.height = 12.0f;
        }
        registry.emplace<Name>(e, stack.item == "gold" ? std::to_string(stack.count) + " gold" : stack.item);
        made.push_back(e);
    }
    return made;
}

} // namespace aige
