#include "gameplay/Streaming.h"

#include "gameplay/Behaviour.h"
#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "scripting/Components.h"

#include <cmath>
#include <set>
#include <vector>

namespace aige {

StreamingStats UpdateStreaming(Registry& registry, float playerX, float playerY, int chunk, int radius) {
    StreamingStats stats;
    chunk = std::max(4, chunk);
    const int pcx = static_cast<int>(std::floor(playerX / static_cast<float>(chunk)));
    const int pcy = static_cast<int>(std::floor(playerY / static_cast<float>(chunk)));
    stats.chunks = (2 * radius + 1) * (2 * radius + 1);
    std::vector<Entity> sleep, wake;
    auto consider = [&](Entity e, const WorldTransform2D& at) {
        const int cx = static_cast<int>(std::floor(at.x / static_cast<float>(chunk)));
        const int cy = static_cast<int>(std::floor(at.y / static_cast<float>(chunk)));
        const bool near = std::abs(cx - pcx) <= radius && std::abs(cy - pcy) <= radius;
        const bool dormant = registry.all_of<Dormant>(e);
        if (near && dormant) wake.push_back(e);
        if (!near && !dormant) sleep.push_back(e);
        (near ? stats.awake : stats.dormant)++;
    };
    std::set<Entity> seen;
    auto each = [&](auto view) {
        for (auto e : view) {
            if (!seen.insert(e).second) continue;
            if (const auto* at = registry.try_get<WorldTransform2D>(e)) consider(e, *at);
        }
    };
    each(registry.view<LuaScript>());
    each(registry.view<Behaviour>());
    each(registry.view<Routine>());
    each(registry.view<Spawner>());
    for (auto e : sleep) registry.emplace_or_replace<Dormant>(e);
    for (auto e : wake) registry.remove<Dormant>(e);
    return stats;
}

void WakeAll(Registry& registry) { registry.clear<Dormant>(); }

} // namespace aige
