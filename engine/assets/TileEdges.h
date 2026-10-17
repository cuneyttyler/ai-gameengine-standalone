#pragma once

#include "renderer2d/ImageData.h"

#include <array>
#include <string>
#include <vector>

namespace aige {

// Which terrain each part of a ground tile's border shows (development_plan.md
// 6.2), read from its pixels: what autotiling (6.4) matches neighbours by.
// Exact where a vision model would guess.
//
// Eight points, clockwise from the top-left corner:
//   0 NW (corner)  1 N (top edge)     2 NE (corner)  3 E (right edge)
//   4 SE (corner)  5 S (bottom edge)  6 SW (corner)  7 W (left edge)
constexpr int kBorderPoints = 8;

struct BorderSamples {
    std::array<std::array<float, 3>, kBorderPoints> rgb{}; // 0-1, averaged over a small patch
    std::array<float, 3> center{};                         // the middle: the cell's own terrain (6.4)
    bool opaque = true; // false: the border is (partly) transparent, so not a ground tile
};

// Samples a whole image (inset from the very edge, past any outline).
BorderSamples SampleBorder(const ImageData& image);

// A tile's border, as one terrain name per point.
using EdgeSignature = std::array<std::string, kBorderPoints>;

struct EdgeClassification {
    std::vector<std::string> terrains;                // names, e.g. {"grass", "dirt"}
    std::vector<std::array<float, 3>> colors;          // each terrain's average colour
    std::vector<EdgeSignature> signatures;             // one per input, in order
    std::vector<std::string> centers;                  // each tile's middle terrain, in order
};

// Clusters every sample of a set of tiles (one art pack's ground) into
// `terrains` groups, names them by colour, and labels each tile's border.
EdgeClassification ClassifyEdges(const std::vector<BorderSamples>& tiles, int terrains = 2);

// "grass", "dirt", "sand", "water", "stone", … from a colour.
std::string TerrainNameFor(const std::array<float, 3>& rgb);

// "grass,grass,dirt,…" and back (a meta value).
std::string FormatSignature(const EdgeSignature& signature);
bool ParseSignature(const std::string& text, EdgeSignature& signature);

} // namespace aige
