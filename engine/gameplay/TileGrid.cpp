#include "gameplay/TileGrid.h"

#include "gameplay/Components.h"

namespace aige {

TileGrid TileGrid::Build(Registry& registry) {
    TileGrid grid;
    auto view = registry.view<Tile>();
    for (auto entity : view) {
        const auto& tile = view.get<Tile>(entity);
        grid.cells_[Key(tile.gx, tile.gy)] = {entity, tile.solid};
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
