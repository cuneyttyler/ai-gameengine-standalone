#pragma once

#include <vector>

namespace aige {

struct PixelRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// The native viewport window sits on top of the web UI, so a menu or dialog
// that overlaps it would be hidden. Each open overlay (page coordinates) is
// cut out of the viewport's window region instead (development_plan.md 4.1):
// this gives the parts of `overlays` that fall on `viewport`, in viewport-local
// coordinates. Rects that miss it are dropped; the rest are clipped.
std::vector<PixelRect> LocalOcclusion(PixelRect viewport, const std::vector<PixelRect>& overlays);

} // namespace aige
