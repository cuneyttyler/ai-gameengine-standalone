#include "gameplay/TileGrid.h"

#include "gameplay/Components.h"
#include "gameplay/Interiors.h"
#include "gameplay/TileLayer.h"
#include "physics2d/Components.h"

#include <cmath>
#include <cstdint>
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
                if (interior->shown) {
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
    auto it = cells_.find(Key(gx, gy));
    if (it != cells_.end()) return it->second.solid; // a painted tile wins over layers
    for (auto layer = layers_.rbegin(); layer != layers_.rend(); ++layer) {
        if (const TileDef* def = (*layer)->Def((*layer)->Get(gx, gy))) return def->solid || blocked_.contains(Key(gx, gy));
    }
    return true;
}

Entity TileGrid::At(int gx, int gy) const {
    auto it = cells_.find(Key(gx, gy));
    return it == cells_.end() ? Entity{entt::null} : it->second.entity;
}

} // namespace aige
