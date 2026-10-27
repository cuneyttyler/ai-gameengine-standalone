#include "gameplay/TileGrid.h"

#include "gameplay/Components.h"
#include "gameplay/Interiors.h"
#include "gameplay/TileLayer.h"
#include "physics2d/Components.h"

#include <cmath>

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
            if (interior->kind == "scene") { // its own scene: solid but for its doorway, which leads there
                ForEachSceneInteriorCell(sx, sy, left, right, back, interior->door, block);
                continue;
            }
            if (!interior->texture.empty()) {
                ForEachInteriorWall(sx, sy, left, right, back, interior->door, block);
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
