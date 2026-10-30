#include "physics2d/Collision.h"

#include <cmath>

namespace aige {

namespace {

BoxQueryFn& BlockedParts() {
    static BoxQueryFn query;
    return query;
}

PassPointFn& PassPoints() {
    static PassPointFn query;
    return query;
}

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
    if (const BoxQueryFn& parts = BlockedParts()) {
        return parts(pos.x - halfSize.x + kEpsilon, pos.y - halfSize.y + kEpsilon, pos.x + halfSize.x - kEpsilon, pos.y + halfSize.y - kEpsilon);
    }
    return false;
}

} // namespace

void SetBlockedParts(BoxQueryFn query) { BlockedParts() = std::move(query); }

void SetPassPoints(PassPointFn query) { PassPoints() = std::move(query); }

Vec2 WaypointIn(int gridX, int gridY) {
    Vec2 at{gridX + 0.5f, gridY + 0.5f};
    if (const PassPointFn& points = PassPoints()) points(gridX, gridY, at.x, at.y);
    return at;
}

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
