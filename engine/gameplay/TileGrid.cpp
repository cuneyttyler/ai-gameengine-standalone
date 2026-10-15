#include "gameplay/TileGrid.h"

#include "gameplay/Components.h"
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
    // Obstacles make the cell they stand in solid (the tile entity stays
    // what At() reports).
    for (auto [entity, world] : registry.view<Obstacle, WorldTransform2D>().each()) {
        auto it = grid.cells_.find(Key(static_cast<int>(std::floor(world.x)), static_cast<int>(std::floor(world.y))));
        if (it != grid.cells_.end()) {
            it->second.solid = true;
        }
    }
    return grid;
}

bool TileGrid::IsSolid(int gx, int gy) const {
    auto it = cells_.find(Key(gx, gy));
    return it == cells_.end() || it->second.solid;
}

Entity TileGrid::At(int gx, int gy) const {
    auto it = cells_.find(Key(gx, gy));
    return it == cells_.end() ? Entity{entt::null} : it->second.entity;
}

} // namespace aige
