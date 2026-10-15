#pragma once

#include "ecs/Registry.h"

#include <cstdint>
#include <unordered_map>

namespace aige {

// Snapshot of every Tile in a registry, keyed by cell, for collision queries
// and "what's in this cell" lookups. Cheap to rebuild; GameSession rebuilds
// it once per Advance so tiles edited mid-play take effect immediately.
// Cells holding an Obstacle entity count as solid.
class TileGrid {
public:
    static TileGrid Build(Registry& registry);

    // Empty cells count as solid: the walkable area is exactly the set of
    // placed, non-solid tiles.
    bool IsSolid(int gx, int gy) const;

    // The tile entity in a cell, or entt::null.
    Entity At(int gx, int gy) const;

private:
    struct Cell {
        Entity entity;
        bool solid;
    };

    static int64_t Key(int gx, int gy) {
        return (static_cast<int64_t>(gx) << 32) ^ static_cast<uint32_t>(gy);
    }

    std::unordered_map<int64_t, Cell> cells_;
};

} // namespace aige
