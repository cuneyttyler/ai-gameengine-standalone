#include "editor/Occlusion.h"

#include <algorithm>

namespace aige {

std::vector<PixelRect> LocalOcclusion(PixelRect viewport, const std::vector<PixelRect>& overlays) {
    std::vector<PixelRect> local;
    for (const auto& o : overlays) {
        int x0 = std::max(o.x, viewport.x);
        int y0 = std::max(o.y, viewport.y);
        int x1 = std::min(o.x + o.w, viewport.x + viewport.w);
        int y1 = std::min(o.y + o.h, viewport.y + viewport.h);
        if (x1 > x0 && y1 > y0) {
            local.push_back({x0 - viewport.x, y0 - viewport.y, x1 - x0, y1 - y0});
        }
    }
    return local;
}

} // namespace aige
