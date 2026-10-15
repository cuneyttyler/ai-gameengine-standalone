#pragma once

#include <cstdint>
#include <vector>

namespace aige {

// Decoded image: tightly packed RGBA8 rows, top row first. The hand-off
// format between the asset pipeline (which decodes/cooks) and the renderer
// (which uploads), so neither depends on the other.
struct ImageData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

} // namespace aige
