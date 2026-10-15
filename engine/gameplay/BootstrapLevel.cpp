#include "gameplay/BootstrapLevel.h"

#include "gameplay/Archetypes.h"

namespace aige {

IsoParams BootstrapIso() {
    return {64.0f, 32.0f, 640.0f, 96.0f};
}

Entity BuildBootstrapScene(Registry& registry, const std::string& npcScriptPath) {
    for (int gx = 0; gx < kBootstrapGridSize; ++gx) {
        for (int gy = 0; gy < kBootstrapGridSize; ++gy) {
            bool border = gx == 0 || gy == 0 || gx == kBootstrapGridSize - 1 || gy == kBootstrapGridSize - 1;
            bool interiorWall = gx == 2 && (gy == 2 || gy == 3);
            bool path = gx == kBootstrapGridSize / 2 || gy == kBootstrapGridSize / 2;

            TileKind kind = TileKind::Grass;
            if (border || interiorWall) {
                kind = TileKind::Wall;
            } else if (path) {
                kind = TileKind::Dirt;
            }
            CreateTile(registry, gx, gy, kind);
        }
    }

    // Characters stand at cell centers (n + 0.5) so they never straddle a
    // cell boundary and clip into a neighboring wall before moving.
    CreateNpc(registry, 5.5f, 2.5f);
    CreateNpc(registry, 5.5f, 5.5f, npcScriptPath);
    return CreatePlayer(registry, 3.5f, 3.5f);
}

} // namespace aige
