#include "gameplay/Pathfinding.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>

namespace aige {

std::optional<std::vector<std::pair<int, int>>> FindPath(const SolidQueryFn& isSolid, std::pair<int, int> from,
                                                         std::pair<int, int> to, int maxExpanded) {
    using Cell = std::pair<int, int>;
    if (isSolid(to.first, to.second) || isSolid(from.first, from.second)) {
        return std::nullopt;
    }
    constexpr float kDiagonal = 1.41421356f;
    auto heuristic = [&](Cell c) { // octile distance
        float dx = static_cast<float>(std::abs(c.first - to.first));
        float dy = static_cast<float>(std::abs(c.second - to.second));
        return (dx + dy) + (kDiagonal - 2.0f) * std::min(dx, dy);
    };
    struct Open {
        float f;
        float g;
        Cell cell;
        bool operator>(const Open& o) const { return f > o.f || (f == o.f && cell > o.cell); }
    };
    std::priority_queue<Open, std::vector<Open>, std::greater<Open>> open;
    std::map<Cell, float> cost;
    std::map<Cell, Cell> came;
    open.push({heuristic(from), 0.0f, from});
    cost[from] = 0.0f;
    int expanded = 0;
    while (!open.empty()) {
        Open current = open.top();
        open.pop();
        if (current.cell == to) {
            std::vector<Cell> path = {to};
            while (path.back() != from) path.push_back(came[path.back()]);
            std::reverse(path.begin(), path.end());
            return path;
        }
        if (current.g > cost[current.cell] || ++expanded > maxExpanded) {
            if (expanded > maxExpanded) return std::nullopt;
            continue;
        }
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                if (dx == 0 && dy == 0) continue;
                Cell next{current.cell.first + dx, current.cell.second + dy};
                if (isSolid(next.first, next.second)) continue;
                if (dx != 0 && dy != 0 &&
                    (isSolid(current.cell.first + dx, current.cell.second) ||
                     isSolid(current.cell.first, current.cell.second + dy))) {
                    continue; // no squeezing past a wall corner
                }
                float g = current.g + (dx != 0 && dy != 0 ? kDiagonal : 1.0f);
                auto it = cost.find(next);
                if (it == cost.end() || g < it->second) {
                    cost[next] = g;
                    came[next] = current.cell;
                    open.push({g + heuristic(next), g, next});
                }
            }
        }
    }
    return std::nullopt;
}

} // namespace aige
