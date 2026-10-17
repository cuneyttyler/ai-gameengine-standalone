#include "gameplay/VillageGen.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace aige {

namespace {

// The same small generator as LevelGen (splitmix64): a seed means the same
// village on every compiler and platform.
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

private:
    uint64_t state_;
};

constexpr int kMaxSide = 240; // cells: a village bigger than this is not a village

// The cells an item's footprint covers.
std::vector<VillageCell> Footprint(const VillageItem& item) {
    const int left = static_cast<int>(std::floor(item.x - item.size / 2.0f + 0.01f));
    const int top = static_cast<int>(std::floor(item.y - item.size / 2.0f + 0.01f));
    std::vector<VillageCell> cells;
    for (int dy = 0; dy < item.size; ++dy)
        for (int dx = 0; dx < item.size; ++dx) cells.push_back({left + dx, top + dy});
    return cells;
}

} // namespace

const char* VillageRoleName(VillageRole role) {
    switch (role) {
    case VillageRole::House: return "house";
    case VillageRole::Well: return "well";
    case VillageRole::Tree: return "tree";
    case VillageRole::Bush: return "bush";
    case VillageRole::Rock: return "rock";
    case VillageRole::Decoration: return "decoration";
    case VillageRole::Entrance: return "entrance";
    case VillageRole::Landmark: return "landmark";
    case VillageRole::FenceH: return "fence (left-right)";
    case VillageRole::FenceV: return "fence (up-down)";
    }
    return "?";
}

std::optional<std::string> CheckVillageParams(const VillageParams& p) {
    if (p.houses < 1 || p.houses > 60) return "houses must be 1-60";
    if (p.roadWidth < 1 || p.roadWidth > 3) return "roadWidth must be 1-3";
    if (p.houseCells < 1 || p.houseCells > 6) return "houseCells must be 1-6";
    if (!(p.nature >= 0.0f && p.nature <= 1.0f)) return "nature must be 0-1";
    return std::nullopt;
}

std::optional<VillageLayout> GenerateVillage(const VillageParams& p, std::string* error) {
    if (auto problem = CheckVillageParams(p)) {
        if (error) *error = *problem;
        return std::nullopt;
    }
    Rng rng(p.seed);
    const int hc = p.houseCells;
    const int rw = p.roadWidth;
    // A plot: one cell of yard each side of the house, a front yard row on the
    // road, the house, then a two-row garden behind it.
    const int plotW = hc + 2;
    const int plotD = 1 + hc + 2;
    const int bandH = 2 * plotD + rw; // plots, road, plots
    const int gap = 1;                // between one street's back gardens and the next's
    const int regionW = p.square ? rw + 4 : rw; // the cross street (and the square on it)
    const int margin = std::max(3, hc + 2);     // the green edge: nature, entrances, the landmark

    // How many streets: the shape closest to a square.
    int streets = 1, perSide = p.houses;
    double best = 1e9;
    for (int s = 1; s <= 12; ++s) {
        const int per = (p.houses + 2 * s - 1) / (2 * s);
        if (per < 1) break;
        const double w = per * plotW + regionW;
        const double h = s * bandH + (s - 1) * gap;
        const double off = std::abs(std::log(w / h));
        if (off < best - 1e-9) {
            best = off;
            streets = s;
            perSide = per;
        }
    }
    const int leftCount = perSide / 2; // plots left of the cross street; the rest are right of it
    const int innerW = perSide * plotW + regionW;
    const int innerH = streets * bandH + (streets - 1) * gap;

    VillageLayout layout;
    layout.x0 = p.x0;
    layout.y0 = p.y0;
    layout.x1 = p.x0 + innerW + 2 * margin - 1;
    layout.y1 = p.y0 + innerH + 2 * margin - 1;
    if (layout.x1 - layout.x0 + 1 > kMaxSide || layout.y1 - layout.y0 + 1 > kMaxSide) {
        if (error) *error = "the village would be over " + std::to_string(kMaxSide) + " cells across; use fewer houses or smaller ones";
        return std::nullopt;
    }
    const int ix = p.x0 + margin, iy = p.y0 + margin;
    const int ix1 = ix + innerW - 1, iy1 = iy + innerH - 1;
    const int rx = ix + leftCount * plotW;                 // the cross street region's left column
    const int vx = p.square ? rx + 2 : rx;                 // the cross street's left column
    const int mainStreet = streets / 2;
    auto roadTopOf = [&](int s) { return iy + s * (bandH + gap) + plotD; };

    std::set<VillageCell> taken; // cells something already stands on
    auto place = [&](VillageItem item) {
        for (const auto& cell : Footprint(item)) taken.insert(cell);
        item.pick = static_cast<int>(rng.Next() % 1024);
        layout.items.push_back(item);
    };
    auto isFree = [&](int x, int y) {
        return !taken.contains({x, y}) && !layout.road.contains({x, y}) && !layout.square.contains({x, y});
    };
    auto cellItem = [](VillageRole role, int x, int y) {
        VillageItem item{role};
        item.x = x + 0.5f;
        item.y = y + 0.5f;
        return item;
    };

    // Streets: one per band left-right; the main one leaves the village at both
    // ends. One cross street up-down through all of them, leaving at both ends.
    for (int s = 0; s < streets; ++s) {
        const int top = roadTopOf(s);
        const int from = s == mainStreet ? layout.x0 : ix;
        const int to = s == mainStreet ? layout.x1 : ix1;
        for (int y = top; y < top + rw; ++y)
            for (int x = from; x <= to; ++x) layout.road.insert({x, y});
    }
    for (int y = layout.y0; y <= layout.y1; ++y)
        for (int x = vx; x < vx + rw; ++x) layout.road.insert({x, y});

    // The square where the cross street meets the main one, a well in it and
    // something at its corners.
    const int mainTop = roadTopOf(mainStreet);
    if (p.square) {
        for (int y = mainTop - 2; y < mainTop + rw + 2; ++y)
            for (int x = rx; x < rx + regionW; ++x) {
                layout.square.insert({x, y});
                layout.road.erase({x, y});
            }
        const int cx = rx + regionW / 2, cy = mainTop + rw / 2;
        place(cellItem(VillageRole::Well, cx, cy));
        const int sy0 = mainTop - 2, sy1 = mainTop + rw + 1, sx1 = rx + regionW - 1;
        for (auto [x, y] : std::vector<VillageCell>{{rx, sy0}, {sx1, sy0}, {rx, sy1}, {sx1, sy1}}) {
            if (rng.Unit() < 0.75f) place(cellItem(VillageRole::Decoration, x, y));
        }
    }

    // Plots along both sides of every street, left of the cross street and right of it.
    for (int s = 0; s < streets; ++s) {
        const int top = roadTopOf(s);
        for (int side = 0; side < 2; ++side) {
            const bool north = side == 0;
            const int py0 = north ? top - plotD : top + rw;
            for (int k = 0; k < perSide; ++k) {
                const int px = k < leftCount ? ix + k * plotW : rx + regionW + (k - leftCount) * plotW;
                layout.plots.push_back({px, py0, px + plotW - 1, py0 + plotD - 1, north});
            }
        }
    }

    // Houses on the plots nearest the square first, with a little shuffle.
    const float cx = rx + regionW / 2.0f, cy = mainTop + rw / 2.0f;
    std::vector<std::pair<float, size_t>> order;
    for (size_t i = 0; i < layout.plots.size(); ++i) {
        const auto& plot = layout.plots[i];
        const float d = std::abs((plot.x0 + plot.x1) / 2.0f - cx) + std::abs((plot.y0 + plot.y1) / 2.0f - cy);
        order.push_back({d + rng.Unit() * plotW, i});
    }
    std::sort(order.begin(), order.end());
    for (int h = 0; h < p.houses && h < static_cast<int>(order.size()); ++h) {
        layout.plots[order[static_cast<size_t>(h)].second].hasHouse = true;
    }

    for (const auto& plot : layout.plots) {
        const int front = plot.northOfRoad ? plot.y1 : plot.y0;     // the yard row on the road
        const int houseTop = plot.northOfRoad ? front - hc : front + 1;
        const int gardenFrom = plot.northOfRoad ? plot.y0 : houseTop + hc; // two rows
        const int back = plot.northOfRoad ? plot.y0 : plot.y1;
        if (!plot.hasHouse) {
            // Left to nature: a tree or a bush here and there, off the road side.
            for (int y = plot.y0; y <= plot.y1; ++y)
                for (int x = plot.x0; x <= plot.x1; ++x) {
                    if (y == front || !isFree(x, y)) continue;
                    const float roll = rng.Unit();
                    if (roll < p.nature * 0.2f) place(cellItem(VillageRole::Tree, x, y));
                    else if (roll < p.nature * 0.3f) place(cellItem(VillageRole::Bush, x, y));
                }
            continue;
        }
        VillageItem house{VillageRole::House};
        house.size = hc;
        house.x = plot.x0 + 1 + hc / 2.0f;
        house.y = houseTop + hc / 2.0f;
        house.mirror = rng.Unit() < 0.5f;
        place(house);
        ++layout.houses;
        // Something by the door: a barrel, a cart.
        const int doorRow = plot.northOfRoad ? houseTop + hc - 1 : houseTop;
        if (rng.Unit() < 0.5f) {
            const int x = rng.Unit() < 0.5f ? plot.x0 : plot.x1;
            if (isFree(x, doorRow)) place(cellItem(VillageRole::Decoration, x, doorRow));
        }
        if (p.fences) {
            for (int x = plot.x0; x <= plot.x1; ++x) {
                if (isFree(x, back)) place(cellItem(VillageRole::FenceH, x, back));
            }
            for (int y = gardenFrom; y < gardenFrom + 2; ++y) {
                if (y == back) continue;
                for (int x : {plot.x0, plot.x1}) {
                    // The neighbour's fence already runs along this side: not a second one.
                    if (x == plot.x0 && taken.contains({x - 1, y})) continue;
                    if (isFree(x, y)) place(cellItem(VillageRole::FenceV, x, y));
                }
            }
        }
        // The garden: a bush or a tree.
        const int gardenRow = plot.northOfRoad ? back + 1 : back - 1;
        const int gx = rng.Range(plot.x0 + 1, plot.x1 - 1);
        if (p.nature > 0.0f && rng.Unit() < 0.2f + 0.6f * p.nature && isFree(gx, gardenRow)) {
            VillageItem green = cellItem(rng.Unit() < 0.5f ? VillageRole::Bush : VillageRole::Tree, gx, gardenRow);
            green.mirror = rng.Unit() < 0.5f;
            place(green);
        }
    }

    // Entrances: something either side of each road where it leaves the village.
    const int mainBottom = mainTop + rw - 1;
    for (const VillageCell cell : std::vector<VillageCell>{
             {ix - 1, mainTop - 1}, {ix - 1, mainBottom + 1},          // west
             {ix1 + 1, mainTop - 1}, {ix1 + 1, mainBottom + 1},        // east
             {vx - 1, iy - 1}, {vx + rw, iy - 1},                      // north
             {vx - 1, iy1 + 1}, {vx + rw, iy1 + 1}}) {                 // south
        if (isFree(cell.first, cell.second)) place(cellItem(VillageRole::Entrance, cell.first, cell.second));
    }
    // One landmark by the west entrance, north of the road.
    {
        VillageItem landmark{VillageRole::Landmark};
        landmark.size = hc;
        landmark.x = ix - 2 - hc / 2.0f;
        landmark.y = mainTop - 1 - hc / 2.0f;
        const auto cells = Footprint(landmark);
        if (std::all_of(cells.begin(), cells.end(), [&](const VillageCell& c) {
                return isFree(c.first, c.second) && c.first >= layout.x0 && c.second >= layout.y0;
            })) {
            place(landmark);
        }
    }

    // The green edge: trees in clumps, bushes, a rock now and then; never two
    // trees side by side, never on or beside a road.
    auto nearRoad = [&](int x, int y) {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (layout.road.contains({x + dx, y + dy})) return true;
        return false;
    };
    std::set<VillageCell> trees;
    for (int y = layout.y0; y <= layout.y1; ++y)
        for (int x = layout.x0; x <= layout.x1; ++x) {
            const bool inner = x >= ix && x <= ix1 && y >= iy && y <= iy1;
            if (inner || !isFree(x, y) || nearRoad(x, y)) continue;
            bool crowded = false;
            for (int dy = -1; dy <= 1 && !crowded; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if ((dx || dy) && trees.contains({x + dx, y + dy})) crowded = true;
            const float roll = rng.Unit();
            VillageItem item = cellItem(VillageRole::Tree, x, y);
            item.mirror = rng.Unit() < 0.5f;
            if (roll < p.nature * 0.35f) {
                if (crowded) continue;
                trees.insert({x, y});
            } else if (roll < p.nature * 0.5f) {
                item.role = VillageRole::Bush;
            } else if (roll < p.nature * 0.56f) {
                item.role = VillageRole::Rock;
            } else {
                continue;
            }
            place(item);
        }
    return layout;
}

std::string VillageLayout::Ascii() const {
    std::map<VillageCell, char> marks;
    for (const auto& item : items) {
        char ch = '?';
        switch (item.role) {
        case VillageRole::House: ch = 'H'; break;
        case VillageRole::Well: ch = 'w'; break;
        case VillageRole::Tree: ch = 'T'; break;
        case VillageRole::Bush: ch = 'b'; break;
        case VillageRole::Rock: ch = 'r'; break;
        case VillageRole::Decoration: ch = 'd'; break;
        case VillageRole::Entrance: ch = '!'; break;
        case VillageRole::Landmark: ch = 'L'; break;
        case VillageRole::FenceH:
        case VillageRole::FenceV: ch = 'f'; break;
        }
        for (const auto& cell : Footprint(item)) marks[cell] = ch;
    }
    std::string out;
    for (int y = y0; y <= y1; ++y) {
        std::string line;
        for (int x = x0; x <= x1; ++x) {
            char ch = '.';
            if (road.contains({x, y})) ch = '=';
            if (square.contains({x, y})) ch = '#';
            if (auto it = marks.find({x, y}); it != marks.end()) ch = it->second;
            line += ch;
        }
        out += line + "\n";
    }
    return out;
}

std::vector<VillageCell> VillageFootprint(const VillageItem& item) { return Footprint(item); }

} // namespace aige
