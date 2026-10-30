#include "gameplay/TileGrid.h"

#include "gameplay/Components.h"
#include "gameplay/Interiors.h"
#include "gameplay/TileLayer.h"
#include "physics2d/Components.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>

namespace aige {

TileGrid TileGrid::Build(Registry& registry, const IsoParams* iso) {
    TileGrid grid;
    auto view = registry.view<Tile>();
    for (auto entity : view) {
        const auto& tile = view.get<Tile>(entity);
        grid.cells_[Key(tile.gx, tile.gy)] = {entity, tile.solid};
    }
    // Layer cells are read in place, not copied: a built place has far more
    // of them than hand-painted tiles.
    if (const TileLayers* layers = FindLayers(registry)) grid.layers_ = layers->Ordered();
    // Obstacles make the cell they stand in solid (the tile entity stays
    // what At() reports).
    for (auto [entity, obstacle, world] : registry.view<Obstacle, WorldTransform2D>().each()) {
        if (const auto* door = registry.try_get<Door>(entity); door && door->open) continue; // an open door (13.5)
        const int sx = static_cast<int>(std::floor(world.x));
        const int sy = static_cast<int>(std::floor(world.y));
        auto block = [&](int gx, int gy) {
            auto it = grid.cells_.find(Key(gx, gy));
            if (it != grid.cells_.end()) it->second.solid = true;
            else grid.blocked_.insert(Key(gx, gy));
        };
        // A place one can go into: its walls only, with a doorway (Phase 27).
        if (const auto* interior = registry.try_get<Interior>(entity)) {
            const int left = std::max(0, obstacle.left), right = std::max(0, obstacle.right), back = std::max(0, obstacle.back);
            // Its door shut (locked, or opened by use and not yet): its doorway too (docs/assets/animations.md §4.2).
            const bool shut = DoorwayShut(registry, entity);
            if (interior->kind == "scene") { // its own scene: solid but for its doorway, which leads there
                ForEachSceneInteriorCell(sx, sy, left, right, back, interior->door, block);
                if (int dx = 0, dy = 0; shut && InteriorDoor(registry, entity, *interior, dx, dy)) block(dx, dy);
                continue;
            }
            // Painted from a layout: its walls the painted room's edges (both from inside and out), open at its ways in
            // (InteriorRoomWays) or, none drawn, where its doors meet them; inside them all walkable.
            if (RoomFrame room; !interior->texture.empty() && iso && InteriorRoom(registry, entity, *interior, *iso, room)) {
                grid.AddRoom(registry, entity, *interior, room);
                continue;
            }
            if (!interior->texture.empty()) {
                // Its other doors (§4.3): each one's doorway in its walls, passable while it is. Its ways in set by
                // hand (§5b): open, unless a shut door stands at one.
                std::set<std::pair<int, int>> through, doorCells;
                if (const auto* open = registry.try_get<PartOpen>(entity)) {
                    for (size_t i = 0; i < open->cells.size(); ++i) {
                        doorCells.insert(open->cells[i]);
                        if (i < open->passable.size() && open->passable[i]) through.insert(open->cells[i]);
                    }
                }
                for (const auto& way : InteriorEntries(registry, entity, *interior)) {
                    if (!doorCells.contains(way)) through.insert(way);
                }
                const int x0 = sx - left, x1 = sx + right, y0 = sy - back, y1 = sy;
                // A gap in a way-in cell: the doorway's cell (from outside), or the cell outside it where the wall ring opens
                // (from inside), is wall but for [lo, hi] along the wall on `side` (0 back row, 1 front, 2 left, 3 right).
                auto gap = [&](int cx, int cy, int s, float lo, float hi) {
                    if (interior->shown) {
                        if (s == 0) cy = y0 - 1;
                        else if (s == 1) cy = y1 + 1;
                        else if (s == 2) cx = x0 - 1;
                        else cx = x1 + 1;
                    }
                    const bool alongX = s <= 1; // the wall runs along x: the gap spans x
                    const float c0 = alongX ? static_cast<float>(cx) : static_cast<float>(cy);
                    if (alongX) {
                        grid.AddPart(cx, cy, c0, cy, lo, cy + 1.0f);
                        grid.AddPart(cx, cy, hi, cy, c0 + 1.0f, cy + 1.0f);
                        grid.passPoints_[Key(cx, cy)] = {(lo + hi) * 0.5f, cy + 0.5f};
                    } else {
                        grid.AddPart(cx, cy, cx, c0, cx + 1.0f, lo);
                        grid.AddPart(cx, cy, cx, hi, cx + 1.0f, c0 + 1.0f);
                        grid.passPoints_[Key(cx, cy)] = {cx + 0.5f, (lo + hi) * 0.5f};
                    }
                };
                // Ways in drawn as quarter cells: one goes through only where they're painted along the wall (a drawn
                // way in wins over its door's own gap). A corner cell, or one painted all the way along, stays open.
                std::set<std::pair<int, int>> drawnGaps;
                for (const auto& [cell, quarters] : InteriorWayQuarters(registry, entity, *interior)) {
                    const auto [cx, cy] = cell;
                    const bool onRow = cy == y0 || cy == y1, onColumn = cx == x0 || cx == x1;
                    if (onRow == onColumn || !through.contains(cell)) continue; // a corner, inside, or shut
                    const int s = onRow ? (cy == y0 ? 0 : 1) : (cx == x0 ? 2 : 3);
                    float lo = 1e9f, hi = -1e9f;
                    for (const auto& [qx, qy] : quarters) {
                        const float along = onRow ? qx : qy;
                        lo = std::min(lo, along), hi = std::max(hi, along + 0.5f);
                    }
                    drawnGaps.insert(cell);
                    const float base = onRow ? static_cast<float>(cx) : static_cast<float>(cy);
                    if (lo > base + 1e-3f || hi < base + 1.0f - 1e-3f) gap(cx, cy, s, lo, hi);
                }
                // A door narrower than its cell: through the door's own gap (PartOpen::gaps).
                if (const auto* open = registry.try_get<PartOpen>(entity)) {
                    for (size_t i = 0; i < open->gaps.size() && i < open->cells.size(); ++i) {
                        const auto& [middle, half, side] = open->gaps[i];
                        if (side < 0.0f || !(i < open->passable.size() && open->passable[i])) continue;
                        if (drawnGaps.contains(open->cells[i])) continue;
                        gap(open->cells[i].first, open->cells[i].second, static_cast<int>(side), middle - half, middle + half);
                    }
                }
                if (interior->shown) {
                    // Its solid things on the floor showing (Interior::solids): their quarter cells block; a cell all four
                    // of whose quarters do is a wall (paths go round it).
                    std::map<std::pair<int, int>, int> quarters;
                    for (const auto& [qx, qy] : InteriorSolids(*interior, interior->floor)) {
                        const float wx = x0 + qx, wy = y0 + qy;
                        const int gx = static_cast<int>(std::floor(wx)), gy = static_cast<int>(std::floor(wy));
                        grid.AddPart(gx, gy, wx, wy, wx + 0.5f, wy + 0.5f);
                        ++quarters[{gx, gy}];
                    }
                    for (const auto& [cell, count] : quarters) {
                        if (count >= 4) block(cell.first, cell.second);
                    }
                    // Inside (docs/places/interiors.md §5): all of its footprint walkable (its edge cells were its walls, and
                    // a small building's edge is most of it), its walls the ring of cells round it, open in front of each
                    // passable doorway (the way out).
                    int dx = INT32_MIN, dy = INT32_MIN;
                    if (!shut && InteriorDoor(registry, entity, *interior, dx, dy)) through.insert({dx, dy});
                    std::set<std::pair<int, int>> out;
                    for (const auto& [cx, cy] : through) {
                        if (cx == x0) out.insert({x0 - 1, cy});
                        if (cx == x1) out.insert({x1 + 1, cy});
                        if (cy == y0) out.insert({cx, y0 - 1});
                        if (cy == y1) out.insert({cx, y1 + 1});
                    }
                    for (int gx = x0 - 1; gx <= x1 + 1; ++gx) {
                        for (int gy = y0 - 1; gy <= y1 + 1; ++gy) {
                            const bool ring = gx == x0 - 1 || gx == x1 + 1 || gy == y0 - 1 || gy == y1 + 1;
                            if (ring && !out.contains({gx, gy})) block(gx, gy);
                            // Its floor: walkable though the place's layers mark the building's cells solid, or a prop
                            // placed on it blocks (the cutaway draws what's inside).
                            const auto full = quarters.find({gx, gy});
                            if (!ring && (full == quarters.end() || full->second < 4)) grid.inside_.insert(Key(gx, gy));
                        }
                    }
                    continue;
                }
                ForEachInteriorWall(sx, sy, left, right, back, interior->door, [&](int gx, int gy) {
                    if (!through.contains({gx, gy})) block(gx, gy);
                });
                if (int dx = 0, dy = 0; shut && InteriorDoor(registry, entity, *interior, dx, dy)) block(dx, dy);
                continue;
            }
        }
        for (int gy = sy - std::max(0, obstacle.back); gy <= sy; ++gy) {
            for (int gx = sx - std::max(0, obstacle.left); gx <= sx + std::max(0, obstacle.right); ++gx) {
                auto it = grid.cells_.find(Key(gx, gy));
                if (it != grid.cells_.end()) {
                    it->second.solid = true;
                } else {
                    grid.blocked_.insert(Key(gx, gy));
                }
            }
        }
    }
    return grid;
}

bool TileGrid::IsSolid(int gx, int gy) const {
    if (inside_.contains(Key(gx, gy))) return false;
    auto it = cells_.find(Key(gx, gy));
    if (it != cells_.end()) return it->second.solid; // a painted tile wins over layers
    for (auto layer = layers_.rbegin(); layer != layers_.rend(); ++layer) {
        if (const TileDef* def = (*layer)->Def((*layer)->Get(gx, gy))) return def->solid || blocked_.contains(Key(gx, gy));
    }
    return true;
}

void TileGrid::AddBox(float x0, float y0, float x1, float y1) {
    for (int gx = static_cast<int>(std::floor(x0)); gx <= static_cast<int>(std::floor(x1 - 1e-4f)); ++gx) {
        for (int gy = static_cast<int>(std::floor(y0)); gy <= static_cast<int>(std::floor(y1 - 1e-4f)); ++gy) AddPart(gx, gy, x0, y0, x1, y1);
    }
}

void TileGrid::AddPart(int gx, int gy, float x0, float y0, float x1, float y1) {
    x0 = std::max(x0, static_cast<float>(gx)), y0 = std::max(y0, static_cast<float>(gy));
    x1 = std::min(x1, gx + 1.0f), y1 = std::min(y1, gy + 1.0f);
    if (x1 - x0 < 1e-3f || y1 - y0 < 1e-3f) return;
    parts_[Key(gx, gy)].push_back({x0, y0, x1, y1});
}

void TileGrid::AddRoom(Registry& registry, Entity entity, const Interior& interior, const RoomFrame& room) {
    // Its four edges (in its picture's terms: side 0 its back-right wall, 1 its back-left, 2 its front-left, 3 its
    // front-right), each from a corner to a corner in the world, and its middle (which way is out).
    const float bx = room.bx, by = room.by;
    const float corners[4][2][2] = {{{bx, by}, {bx + room.exx, by + room.exy}},
                                    {{bx, by}, {bx + room.eyx, by + room.eyy}},
                                    {{bx + room.eyx, by + room.eyy}, {bx + room.eyx + room.exx, by + room.eyy + room.exy}},
                                    {{bx + room.exx, by + room.exy}, {bx + room.exx + room.eyx, by + room.exy + room.eyy}}};
    const float mx = bx + (room.exx + room.eyx) * 0.5f, my = by + (room.exy + room.eyy) * 0.5f;
    // Its gaps: each side's open stretches (0-1 along it).
    std::vector<std::pair<float, float>> open[4];
    const auto ways = InteriorRoomWays(interior);
    const auto* doors = registry.try_get<PartOpen>(entity);
    for (const auto& [side, index] : ways) {
        if (side < 0 || side > 3) continue;
        const int n = RoomSideCells(room, side);
        open[side].push_back({static_cast<float>(index) / n, static_cast<float>(index + 1) / n});
    }
    // A door on a side (its foot nearest it): open where it is, as wide as it is, while it's passable; shut, it closes a
    // drawn way in it.
    if (doors) {
        for (size_t i = 0; i < doors->feet.size(); ++i) {
            const auto& [fx, fy, half] = doors->feet[i];
            if (half <= 0.0f) continue;
            int best = -1;
            float bestDistance = 1e9f, bestT = 0.0f, bestLength = 1.0f;
            for (int side = 0; side < 4; ++side) {
                const float ax = corners[side][0][0], ay = corners[side][0][1];
                const float vx = corners[side][1][0] - ax, vy = corners[side][1][1] - ay;
                const float length2 = std::max(1e-4f, vx * vx + vy * vy);
                const float t = std::clamp(((fx - ax) * vx + (fy - ay) * vy) / length2, 0.0f, 1.0f);
                const float distance = std::hypot(ax + vx * t - fx, ay + vy * t - fy);
                if (distance < bestDistance) best = side, bestDistance = distance, bestT = t, bestLength = std::sqrt(length2);
            }
            if (best < 0 || bestDistance > 1.5f) continue;
            const bool passable = i < doors->passable.size() && doors->passable[i];
            const float lo = bestT - half / bestLength, hi = bestT + half / bestLength;
            if (!passable) {
                auto& gaps = open[best];
                gaps.erase(std::remove_if(gaps.begin(), gaps.end(), [&](const auto& g) { return g.first < hi && g.second > lo; }), gaps.end());
            } else if (ways.empty()) {
                open[best].push_back({lo, hi});
            }
        }
    }
    // Each side a thin wall just outside it, but its open stretches.
    constexpr float kThick = 0.2f;
    for (int side = 0; side < 4; ++side) {
        const float ax = corners[side][0][0], ay = corners[side][0][1];
        const float vx = corners[side][1][0] - ax, vy = corners[side][1][1] - ay;
        // Out: away from the middle.
        float nx = -vy, ny = vx;
        const float nl = std::max(1e-4f, std::hypot(nx, ny));
        nx /= nl, ny /= nl;
        if ((ax + vx * 0.5f - mx) * nx + (ay + vy * 0.5f - my) * ny < 0.0f) nx = -nx, ny = -ny;
        auto& gaps = open[side];
        std::sort(gaps.begin(), gaps.end());
        float from = 0.0f;
        // In short pieces (a quarter cell long), so a wall not quite along the grid's axes stays thin along its line.
        auto wall = [&](float t0, float t1) {
            if (t1 - t0 < 1e-3f) return;
            const float length = std::hypot(vx, vy) * (t1 - t0);
            const int pieces = std::max(1, static_cast<int>(std::ceil(length / 0.25f)));
            const float ox = nx * kThick, oy = ny * kThick;
            for (int p = 0; p < pieces; ++p) {
                const float a = t0 + (t1 - t0) * p / pieces, b = t0 + (t1 - t0) * (p + 1) / pieces;
                const float x0 = ax + vx * a, y0 = ay + vy * a, x1 = ax + vx * b, y1 = ay + vy * b;
                AddBox(std::min({x0, x1, x0 + ox, x1 + ox}), std::min({y0, y1, y0 + oy, y1 + oy}),
                       std::max({x0, x1, x0 + ox, x1 + ox}), std::max({y0, y1, y0 + oy, y1 + oy}));
            }
        };
        for (const auto& [lo, hi] : gaps) {
            wall(from, std::clamp(lo, 0.0f, 1.0f));
            from = std::max(from, std::clamp(hi, 0.0f, 1.0f));
        }
        wall(from, 1.0f);
    }
    // Inside (and the edge cells' bits outside it, reached only through its gaps): walkable whatever the place's layers
    // say; its solid things on the floor showing (Interior::solids, quarter cells from its footprint's top-left).
    const float xs[4] = {bx, bx + room.exx, bx + room.eyx, bx + room.exx + room.eyx};
    const float ys[4] = {by, by + room.exy, by + room.eyy, by + room.exy + room.eyy};
    const int gx0 = static_cast<int>(std::floor(*std::min_element(xs, xs + 4))), gx1 = static_cast<int>(std::floor(*std::max_element(xs, xs + 4)));
    const int gy0 = static_cast<int>(std::floor(*std::min_element(ys, ys + 4))), gy1 = static_cast<int>(std::floor(*std::max_element(ys, ys + 4)));
    for (int gx = gx0; gx <= gx1; ++gx) {
        for (int gy = gy0; gy <= gy1; ++gy) inside_.insert(Key(gx, gy));
    }
    // Its solid things on its room's grid: each cell's box (its corners' bounds).
    if (interior.shown) {
        const int cols = RoomSideCells(room, 0), rows = RoomSideCells(room, 1);
        for (const auto& [i, j] : InteriorRoomSolids(interior, interior.floor)) {
            float cx[4], cy[4];
            for (int c = 0; c < 4; ++c) {
                const float u = static_cast<float>(i + (c == 1 || c == 2)) / cols, v = static_cast<float>(j + (c >= 2)) / rows;
                cx[c] = bx + room.exx * u + room.eyx * v, cy[c] = by + room.exy * u + room.eyy * v;
            }
            AddBox(*std::min_element(cx, cx + 4), *std::min_element(cy, cy + 4), *std::max_element(cx, cx + 4), *std::max_element(cy, cy + 4));
        }
    }
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (interior.shown && InteriorFootprint(registry, entity, interior, x0, y0, x1, y1)) {
        for (const auto& [qx, qy] : InteriorSolids(interior, interior.floor)) {
            const float wx = x0 + qx, wy = y0 + qy;
            AddPart(static_cast<int>(std::floor(wx)), static_cast<int>(std::floor(wy)), wx, wy, wx + 0.5f, wy + 0.5f);
        }
    }
}

bool TileGrid::BlocksBox(float x0, float y0, float x1, float y1) const {
    if (parts_.empty()) return false;
    for (int gx = static_cast<int>(std::floor(x0)); gx <= static_cast<int>(std::floor(x1)); ++gx) {
        for (int gy = static_cast<int>(std::floor(y0)); gy <= static_cast<int>(std::floor(y1)); ++gy) {
            const auto it = parts_.find(Key(gx, gy));
            if (it == parts_.end()) continue;
            for (const auto& b : it->second) {
                if (x0 < b[2] && x1 > b[0] && y0 < b[3] && y1 > b[1]) return true;
            }
        }
    }
    return false;
}

bool TileGrid::PassPoint(int gx, int gy, float& x, float& y) const {
    const auto it = passPoints_.find(Key(gx, gy));
    if (it == passPoints_.end()) return false;
    x = it->second.first, y = it->second.second;
    return true;
}

Entity TileGrid::At(int gx, int gy) const {
    auto it = cells_.find(Key(gx, gy));
    return it == cells_.end() ? Entity{entt::null} : it->second.entity;
}

} // namespace aige
