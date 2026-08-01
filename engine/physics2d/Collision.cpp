#include "physics2d/Collision.h"

#include <cmath>

namespace aige {

namespace {

bool Overlaps(Vec2 pos, Vec2 halfSize, const SolidQueryFn& isSolid) {
    constexpr float kEpsilon = 1e-4f;
    int minX = static_cast<int>(std::floor(pos.x - halfSize.x + kEpsilon));
    int maxX = static_cast<int>(std::floor(pos.x + halfSize.x - kEpsilon));
    int minY = static_cast<int>(std::floor(pos.y - halfSize.y + kEpsilon));
    int maxY = static_cast<int>(std::floor(pos.y + halfSize.y - kEpsilon));

    for (int gx = minX; gx <= maxX; ++gx) {
        for (int gy = minY; gy <= maxY; ++gy) {
            if (isSolid(gx, gy)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

Vec2 ResolveAabbMovement(Vec2 pos, Vec2 halfSize, Vec2 delta, const SolidQueryFn& isSolid) {
    Vec2 result = pos;

    Vec2 afterX{result.x + delta.x, result.y};
    if (!Overlaps(afterX, halfSize, isSolid)) {
        result.x = afterX.x;
    }

    Vec2 afterY{result.x, result.y + delta.y};
    if (!Overlaps(afterY, halfSize, isSolid)) {
        result.y = afterY.y;
    }

    return result;
}

} // namespace aige
