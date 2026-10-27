#pragma once

#include "ecs/Registry.h"

#include <optional>
#include <string>

namespace aige {

// Interiors (development_plan.md Phase 27): a building with an Interior shows it while the player stands inside its
// footprint, its exterior otherwise.

// Each Interior's `shown` from where the player stands (Play), then the pictures as `shown` says.
void UpdateInteriors(Registry& registry, std::optional<Entity> player);
// The pictures as each Interior's `shown` says (the interior's texture, or the exterior put back): every frame in the
// editor too, so the Inspector's shown previews it.
void ApplyInteriors(Registry& registry);
// Its footprint in world cells, inclusive: `cells` when set (a painted building), else its Obstacle's around its cell.
struct Interior;
bool InteriorFootprint(Registry& registry, Entity entity, const Interior& interior, int& x0, int& y0, int& x1, int& y1);

// The cells a building with an Interior blocks: its footprint's edge, less its doorway (`door`: s, e, n, w). Calls
// `blocked` for each. `sx`, `sy`: its world cell; left, right, back: its Obstacle's.
template <typename Fn>
void ForEachInteriorWall(int sx, int sy, int left, int right, int back, const std::string& door, Fn&& blocked) {
    const int x0 = sx - left, x1 = sx + right, y0 = sy - back, y1 = sy;
    const int mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
    const int dx = door == "e" ? x1 : door == "w" ? x0 : mx;
    const int dy = door == "s" ? y1 : door == "n" ? y0 : my;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const bool edge = x == x0 || x == x1 || y == y0 || y == y1;
            if (edge && !(x == dx && y == dy)) blocked(x, y);
        }
    }
}

} // namespace aige
