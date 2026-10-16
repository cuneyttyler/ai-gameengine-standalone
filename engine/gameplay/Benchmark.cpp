#include "gameplay/Benchmark.h"

#include "gameplay/Archetypes.h"

namespace aige {

void BuildBenchmarkScene(Registry& registry, int side, int characters) {
    for (int gx = 0; gx < side; ++gx) {
        for (int gy = 0; gy < side; ++gy) {
            const bool edge = gx == 0 || gy == 0 || gx == side - 1 || gy == side - 1;
            CreateTile(registry, gx, gy, edge ? TileKind::Wall : ((gx * 7 + gy * 3) % 5 == 0 ? TileKind::Dirt : TileKind::Grass));
        }
    }
    Entity player = CreatePlayer(registry, side * 0.5f + 0.5f, side * 0.5f + 0.5f);
    // A deterministic spread over the inner cells.
    const int inner = side - 2;
    for (int i = 0; i < characters; ++i) {
        const int cell = (i * 7919) % (inner * inner);
        CreateEnemy(registry, 1.5f + static_cast<float>(cell % inner), 1.5f + static_cast<float>(cell / inner), player);
    }
}

} // namespace aige
