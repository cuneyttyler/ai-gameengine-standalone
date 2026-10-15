#pragma once

#include "physics2d/Collision.h"

#include <optional>
#include <utility>
#include <vector>

namespace aige {

// A* over grid cells (development_plan.md 3.5): 8-directional, diagonals only
// where both sides are open (no cutting wall corners), so a character
// following the path never snags. Returns the cells from `from` to `to`
// inclusive, or nullopt if `to` can't be reached within `maxExpanded` cells.
std::optional<std::vector<std::pair<int, int>>> FindPath(const SolidQueryFn& isSolid, std::pair<int, int> from,
                                                         std::pair<int, int> to, int maxExpanded = 20000);

} // namespace aige
