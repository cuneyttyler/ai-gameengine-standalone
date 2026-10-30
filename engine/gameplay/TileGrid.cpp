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

TileGrid TileGrid::Build(Registry& registry) {
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
                // A door narrower than its cell: its doorway's cell (from outside), or the cell outside it where the wall
                // ring opens (from inside), is wall but for the door's gap (PartOpen::gaps).
                if (const auto* open = registry.try_get<PartOpen>(entity)) {
                    for (size_t i = 0; i < open->gaps.size() && i < open->cells.size(); ++i) {
                        const auto& [middle, half, side] = open->gaps[i];
                        if (side < 0.0f || !(i < open->passable.size() && open->passable[i])) continue;
                        auto [cx, cy] = open->cells[i];
                        const int s = static_cast<int>(side);
                        if (interior->shown) {
                            if (s == 0) cy = y0 - 1;
                            else if (s == 1) cy = y1 + 1;
                            else if (s == 2) cx = x0 - 1;
                            else cx = x1 + 1;
                        }
                        const bool alongX = s <= 1; // the wall runs along x: the gap spans x
                        const float c0 = (alongX ? cx : cy), lo = middle - half, hi = middle + half;
                        if (alongX) {
                            grid.AddPart(cx, cy, c0, cy, lo, cy + 1.0f);
                            grid.AddPart(cx, cy, hi, cy, c0 + 1.0f, cy + 1.0f);
                            grid.passPoints_[Key(cx, cy)] = {middle, cy + 0.5f};
                        } else {
                            grid.AddPart(cx, cy, cx, c0, cx + 1.0f, lo);
                            grid.AddPart(cx, cy, cx, hi, cx + 1.0f, c0 + 1.0f);
                            grid.passPoints_[Key(cx, cy)] = {cx + 0.5f, middle};
                        }
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

void TileGrid::AddPart(int gx, int gy, float x0, float y0, float x1, float y1) {
    x0 = std::max(x0, static_cast<float>(gx)), y0 = std::max(y0, static_cast<float>(gy));
    x1 = std::min(x1, gx + 1.0f), y1 = std::min(y1, gy + 1.0f);
    if (x1 - x0 < 1e-3f || y1 - y0 < 1e-3f) return;
    parts_[Key(gx, gy)].push_back({x0, y0, x1, y1});
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
