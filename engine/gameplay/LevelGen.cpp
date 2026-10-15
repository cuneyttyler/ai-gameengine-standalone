#include "gameplay/LevelGen.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>

namespace aige {

namespace {

// A small, fully specified generator (splitmix64), so a seed means the same
// level on every compiler and platform — std:: distributions don't promise that.
class Rng {
public:
    explicit Rng(uint64_t seed) : state_(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}
    uint64_t Next() {
        uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    int Range(int lo, int hi) { // inclusive
        return lo + static_cast<int>(Next() % static_cast<uint64_t>(hi - lo + 1));
    }
    float Unit() { return static_cast<float>(Next() >> 40) / static_cast<float>(1ull << 24); }
    template <typename T>
    void Shuffle(std::vector<T>& items) {
        for (size_t i = items.size(); i > 1; --i) {
            std::swap(items[i - 1], items[static_cast<size_t>(Next() % i)]);
        }
    }

private:
    uint64_t state_;
};

bool Overlaps(const LevelRoom& a, const LevelRoom& b, int margin) {
    return a.x0 - margin <= b.x1 && b.x0 - margin <= a.x1 && a.y0 - margin <= b.y1 && b.y0 - margin <= a.y1;
}

int Distance2(Cell a, Cell b) {
    int dx = a.first - b.first;
    int dy = a.second - b.second;
    return dx * dx + dy * dy;
}

// An L-shaped corridor between two cells, `width` cells wide.
void CarveCorridor(std::set<Cell>& floor, Cell a, Cell b, int width, bool horizontalFirst) {
    auto carve = [&](int gx, int gy) {
        for (int o = 0; o < width; ++o) {
            int off = o - (width - 1) / 2;
            floor.insert({gx + off, gy});
            floor.insert({gx, gy + off});
        }
    };
    Cell corner = horizontalFirst ? Cell{b.first, a.second} : Cell{a.first, b.second};
    const std::pair<Cell, Cell> legs[] = {{a, corner}, {corner, b}};
    for (const auto& [from, to] : legs) {
        int dx = to.first > from.first ? 1 : to.first < from.first ? -1 : 0;
        int dy = to.second > from.second ? 1 : to.second < from.second ? -1 : 0;
        Cell c = from;
        carve(c.first, c.second);
        while (c != to) {
            c.first += dx;
            c.second += dy;
            carve(c.first, c.second);
        }
    }
}

} // namespace

std::optional<std::string> CheckLevelParams(const LevelParams& p) {
    if (p.x1 <= p.x0 || p.y1 <= p.y0) return "the area needs x1 > x0 and y1 > y0";
    if ((p.x1 - p.x0 + 1) * (p.y1 - p.y0 + 1) > 20000) return "the area is too big (at most 20000 cells)";
    if (p.roomMin < 2 || p.roomMax < p.roomMin) return "rooms need 2 <= roomMin <= roomMax";
    if (p.rooms < 2 || p.rooms > 40) return "between 2 and 40 rooms";
    if (p.corridorWidth < 1 || p.corridorWidth > 3) return "corridors are 1-3 cells wide";
    if (p.enemies < 0 || p.pickups < 0) return "counts can't be negative";
    if (p.x1 - p.x0 + 1 < p.roomMin + 2 || p.y1 - p.y0 + 1 < p.roomMin + 2) return "the area is smaller than one room";
    return std::nullopt;
}

std::vector<std::pair<Cell, int>> FloorDistances(const std::set<Cell>& floor, Cell from) {
    std::map<Cell, int> distance;
    std::deque<Cell> queue;
    if (floor.contains(from)) {
        distance[from] = 0;
        queue.push_back(from);
    }
    while (!queue.empty()) {
        Cell c = queue.front();
        queue.pop_front();
        for (Cell n : {Cell{c.first + 1, c.second}, Cell{c.first - 1, c.second}, Cell{c.first, c.second + 1},
                       Cell{c.first, c.second - 1}}) {
            if (floor.contains(n) && !distance.contains(n)) {
                distance[n] = distance[c] + 1;
                queue.push_back(n);
            }
        }
    }
    return {distance.begin(), distance.end()};
}

std::optional<LevelLayout> GenerateLevel(const LevelParams& p, std::string* error) {
    if (auto problem = CheckLevelParams(p)) {
        if (error) *error = *problem;
        return std::nullopt;
    }
    Rng rng(p.seed);
    LevelLayout layout;

    // Rooms: random rectangles inside the area (one cell kept for walls),
    // at least two cells apart so each keeps its own wall.
    const int innerX0 = p.x0 + 1, innerY0 = p.y0 + 1, innerX1 = p.x1 - 1, innerY1 = p.y1 - 1;
    for (int attempt = 0; attempt < p.rooms * 60 && static_cast<int>(layout.rooms.size()) < p.rooms; ++attempt) {
        int w = rng.Range(p.roomMin, p.roomMax);
        int h = rng.Range(p.roomMin, p.roomMax);
        if (w > innerX1 - innerX0 + 1 || h > innerY1 - innerY0 + 1) continue;
        int x = rng.Range(innerX0, innerX1 - w + 1);
        int y = rng.Range(innerY0, innerY1 - h + 1);
        LevelRoom room{x, y, x + w - 1, y + h - 1};
        if (std::none_of(layout.rooms.begin(), layout.rooms.end(),
                         [&](const LevelRoom& other) { return Overlaps(room, other, 2); })) {
            layout.rooms.push_back(room);
        }
    }
    if (layout.rooms.size() < 2) {
        if (error) *error = "fewer than two rooms fit in the area; make it bigger or the rooms smaller";
        return std::nullopt;
    }
    for (const auto& room : layout.rooms) {
        for (int gx = room.x0; gx <= room.x1; ++gx)
            for (int gy = room.y0; gy <= room.y1; ++gy) layout.floor.insert({gx, gy});
    }

    // Corridors: a minimum spanning tree over the room centers (Prim), so
    // every room is reachable, plus a few of the shortest remaining links as
    // loops.
    const size_t n = layout.rooms.size();
    std::vector<bool> inTree(n, false);
    std::vector<int> best(n, std::numeric_limits<int>::max());
    std::vector<size_t> parent(n, 0);
    best[0] = 0;
    std::set<std::pair<size_t, size_t>> linked;
    for (size_t step = 0; step < n; ++step) {
        size_t next = n;
        for (size_t i = 0; i < n; ++i)
            if (!inTree[i] && (next == n || best[i] < best[next])) next = i;
        inTree[next] = true;
        if (step > 0) {
            layout.corridors.emplace_back(parent[next], next);
            linked.insert(std::minmax(parent[next], next));
        }
        for (size_t i = 0; i < n; ++i) {
            int d = Distance2(layout.rooms[next].Center(), layout.rooms[i].Center());
            if (!inTree[i] && d < best[i]) {
                best[i] = d;
                parent[i] = next;
            }
        }
    }
    std::vector<std::pair<int, std::pair<size_t, size_t>>> extra;
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            if (!linked.contains({i, j})) extra.push_back({Distance2(layout.rooms[i].Center(), layout.rooms[j].Center()), {i, j}});
    std::sort(extra.begin(), extra.end());
    size_t loopsWanted = static_cast<size_t>(std::round(p.loops * static_cast<float>(n)));
    for (size_t i = 0; i < extra.size() && i < loopsWanted; ++i) layout.corridors.push_back(extra[i].second);
    for (const auto& [a, b] : layout.corridors) {
        CarveCorridor(layout.floor, layout.rooms[a].Center(), layout.rooms[b].Center(), p.corridorWidth,
                      rng.Unit() < 0.5f);
    }
    // Corridors never leave the area's inner part (centers are inside it and
    // width is clamped), but stay safe:
    for (auto it = layout.floor.begin(); it != layout.floor.end();) {
        bool inside = it->first >= innerX0 && it->first <= innerX1 && it->second >= innerY0 && it->second <= innerY1;
        it = inside ? std::next(it) : layout.floor.erase(it);
    }

    // Walls: every non-floor cell touching floor (8 neighbours).
    for (const Cell& c : layout.floor) {
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy) {
                Cell w{c.first + dx, c.second + dy};
                if (!layout.floor.contains(w)) layout.walls.insert(w);
            }
    }

    // The start: the room nearest the area's first corner (the entrance).
    Cell corner{p.x0, p.y0};
    for (size_t i = 1; i < n; ++i)
        if (Distance2(layout.rooms[i].Center(), corner) < Distance2(layout.rooms[layout.startRoom].Center(), corner))
            layout.startRoom = i;
    layout.playerStart = layout.rooms[layout.startRoom].Center();
    std::map<Cell, int> distance;
    for (const auto& [cell, d] : FloorDistances(layout.floor, layout.playerStart)) distance[cell] = d;
    for (size_t i = 0; i < n; ++i) {
        layout.roomDistance.push_back(distance.at(layout.rooms[i].Center()));
        if (layout.roomDistance[i] > layout.roomDistance[layout.farthestRoom]) layout.farthestRoom = i;
    }
    layout.pathLength = layout.roomDistance[layout.farthestRoom];

    // Enemies: never in the start room, and more of them further in — each
    // room's share grows with its distance from the start.
    std::vector<size_t> others;
    for (size_t i = 0; i < n; ++i)
        if (i != layout.startRoom) others.push_back(i);
    std::set<Cell> taken = {layout.playerStart};
    auto freeCellIn = [&](const LevelRoom& room) -> std::optional<Cell> {
        std::vector<Cell> cells;
        for (int gx = room.x0; gx <= room.x1; ++gx)
            for (int gy = room.y0; gy <= room.y1; ++gy)
                if (!taken.contains({gx, gy}) && std::abs(gx - layout.playerStart.first) + std::abs(gy - layout.playerStart.second) > 3)
                    cells.push_back({gx, gy});
        if (cells.empty()) return std::nullopt;
        return cells[rng.Next() % cells.size()];
    };
    if (p.enemies > 0 && !others.empty()) {
        float maxD = static_cast<float>(std::max(1, layout.pathLength));
        std::vector<float> weight;
        float total = 0.0f;
        for (size_t i : others) {
            weight.push_back(1.0f + static_cast<float>(layout.roomDistance[i]) / maxD);
            total += weight.back();
        }
        // Largest-remainder split of the count by weight.
        std::vector<int> share(others.size());
        std::vector<std::pair<float, size_t>> remainder;
        int given = 0;
        for (size_t k = 0; k < others.size(); ++k) {
            float exact = static_cast<float>(p.enemies) * weight[k] / total;
            share[k] = static_cast<int>(exact);
            given += share[k];
            remainder.push_back({exact - static_cast<float>(share[k]), k});
        }
        std::sort(remainder.rbegin(), remainder.rend());
        for (size_t k = 0; given < p.enemies && k < remainder.size(); ++k, ++given) share[remainder[k].second]++;
        for (size_t k = 0; k < others.size(); ++k) {
            for (int e = 0; e < share[k]; ++e) {
                if (auto cell = freeCellIn(layout.rooms[others[k]])) {
                    layout.enemies.push_back(*cell);
                    taken.insert(*cell);
                }
            }
        }
    }
    // Pickups: spread over rooms (the start room included), one per room in
    // shuffled order, then round again if there are more.
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    rng.Shuffle(order);
    for (int k = 0; k < p.pickups && k < static_cast<int>(n) * 4; ++k) {
        if (auto cell = freeCellIn(layout.rooms[order[static_cast<size_t>(k) % n]])) {
            layout.pickups.push_back(*cell);
            taken.insert(*cell);
        }
    }
    return layout;
}

std::string LevelLayout::Ascii(const LevelParams& params) const {
    std::set<Cell> enemyCells(enemies.begin(), enemies.end());
    std::set<Cell> pickupCells(pickups.begin(), pickups.end());
    std::string out;
    for (int gy = params.y1; gy >= params.y0; --gy) {
        std::string line;
        for (int gx = params.x0; gx <= params.x1; ++gx) {
            Cell c{gx, gy};
            char ch = ' ';
            if (walls.contains(c)) ch = '#';
            if (floor.contains(c)) ch = '.';
            if (pickupCells.contains(c)) ch = '+';
            if (enemyCells.contains(c)) ch = 'e';
            if (c == playerStart) ch = '@';
            line += ch;
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out += line + "\n";
    }
    return out;
}

} // namespace aige
