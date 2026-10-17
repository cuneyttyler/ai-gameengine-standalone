#include "assets/TileEdges.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>

namespace aige {

namespace {

using Rgb = std::array<float, 3>;

float Distance2(const Rgb& a, const Rgb& b) {
    float d = 0.0f;
    for (int i = 0; i < 3; ++i) d += (a[i] - b[i]) * (a[i] - b[i]);
    return d;
}

} // namespace

BorderSamples SampleBorder(const ImageData& image) {
    BorderSamples samples;
    if (image.width == 0 || image.height == 0 || image.rgba.size() < static_cast<size_t>(image.width) * image.height * 4) {
        samples.opaque = false;
        return samples;
    }
    const float w = static_cast<float>(image.width);
    const float h = static_cast<float>(image.height);
    // Centres of the eight patches, as fractions: 12% in from the border
    // (past outlines), the patches 8% of the tile across.
    constexpr float in = 0.12f;
    const std::array<std::array<float, 2>, kBorderPoints> centres = {{
        {in, in}, {0.5f, in}, {1.0f - in, in}, {1.0f - in, 0.5f},
        {1.0f - in, 1.0f - in}, {0.5f, 1.0f - in}, {in, 1.0f - in}, {in, 0.5f},
    }};
    const int half = std::max(1, static_cast<int>(std::min(w, h) * 0.04f));
    int transparent = 0;
    for (int p = 0; p < kBorderPoints; ++p) {
        const int cx = static_cast<int>(centres[p][0] * w);
        const int cy = static_cast<int>(centres[p][1] * h);
        Rgb sum{0.0f, 0.0f, 0.0f};
        int count = 0;
        for (int y = std::max(0, cy - half); y <= std::min(static_cast<int>(image.height) - 1, cy + half); ++y) {
            for (int x = std::max(0, cx - half); x <= std::min(static_cast<int>(image.width) - 1, cx + half); ++x) {
                const size_t i = (static_cast<size_t>(y) * image.width + x) * 4;
                if (image.rgba[i + 3] < 200) {
                    ++transparent;
                    continue;
                }
                for (int c = 0; c < 3; ++c) sum[c] += image.rgba[i + c] / 255.0f;
                ++count;
            }
        }
        for (int c = 0; c < 3; ++c) samples.rgb[p][c] = count > 0 ? sum[c] / static_cast<float>(count) : 0.0f;
    }
    // The middle, for the cell's own terrain (a narrow path's tile is mostly
    // grass at the border but road through the middle).
    {
        Rgb sum{0.0f, 0.0f, 0.0f};
        int count = 0;
        const int cx = static_cast<int>(w * 0.5f), cy = static_cast<int>(h * 0.5f);
        for (int y = std::max(0, cy - half); y <= std::min(static_cast<int>(image.height) - 1, cy + half); ++y) {
            for (int x = std::max(0, cx - half); x <= std::min(static_cast<int>(image.width) - 1, cx + half); ++x) {
                const size_t i = (static_cast<size_t>(y) * image.width + x) * 4;
                for (int c = 0; c < 3; ++c) sum[c] += image.rgba[i + c] / 255.0f;
                ++count;
            }
        }
        for (int c = 0; c < 3; ++c) samples.center[c] = count > 0 ? sum[c] / static_cast<float>(count) : 0.0f;
    }
    samples.opaque = transparent == 0;
    return samples;
}

std::string TerrainNameFor(const Rgb& rgb) {
    const float r = rgb[0], g = rgb[1], b = rgb[2];
    const float max = std::max({r, g, b}), min = std::min({r, g, b});
    const float v = max;
    const float s = max > 0.0f ? (max - min) / max : 0.0f;
    float hue = 0.0f;
    if (max > min) {
        if (max == r) hue = 60.0f * std::fmod((g - b) / (max - min), 6.0f);
        else if (max == g) hue = 60.0f * ((b - r) / (max - min) + 2.0f);
        else hue = 60.0f * ((r - g) / (max - min) + 4.0f);
        if (hue < 0.0f) hue += 360.0f;
    }
    if (s < 0.15f) return v < 0.3f ? "dark" : v > 0.85f ? "snow" : "stone";
    if (hue >= 70.0f && hue < 170.0f) return "grass";
    if (hue >= 170.0f && hue < 260.0f) return "water";
    if (hue >= 260.0f && hue < 330.0f) return "magic";
    if (hue >= 40.0f && hue < 70.0f) return v > 0.75f ? "sand" : "dirt";
    return v > 0.8f && s < 0.45f ? "sand" : "dirt"; // reds and oranges: earth
}

EdgeClassification ClassifyEdges(const std::vector<BorderSamples>& tiles, int terrains) {
    EdgeClassification out;
    std::vector<Rgb> points;
    for (const auto& tile : tiles) {
        for (const Rgb& rgb : tile.rgb) points.push_back(rgb);
    }
    if (points.empty()) return out;
    const int k = std::max(1, std::min(terrains, static_cast<int>(points.size())));

    // k-means, seeded far apart (the first point, then the farthest from the
    // seeds so far), so the result doesn't depend on chance.
    std::vector<Rgb> centres{points.front()};
    while (static_cast<int>(centres.size()) < k) {
        size_t far = 0;
        float farDistance = -1.0f;
        for (size_t i = 0; i < points.size(); ++i) {
            float nearest = std::numeric_limits<float>::max();
            for (const Rgb& c : centres) nearest = std::min(nearest, Distance2(points[i], c));
            if (nearest > farDistance) {
                farDistance = nearest;
                far = i;
            }
        }
        centres.push_back(points[far]);
    }
    std::vector<int> label(points.size(), 0);
    for (int iteration = 0; iteration < 30; ++iteration) {
        bool moved = false;
        for (size_t i = 0; i < points.size(); ++i) {
            int best = 0;
            for (int c = 1; c < k; ++c) {
                if (Distance2(points[i], centres[c]) < Distance2(points[i], centres[best])) best = c;
            }
            moved |= best != label[i];
            label[i] = best;
        }
        std::vector<Rgb> sums(k, Rgb{0.0f, 0.0f, 0.0f});
        std::vector<int> counts(k, 0);
        for (size_t i = 0; i < points.size(); ++i) {
            for (int c = 0; c < 3; ++c) sums[label[i]][c] += points[i][c];
            ++counts[label[i]];
        }
        for (int c = 0; c < k; ++c) {
            if (counts[c] > 0) {
                for (int ch = 0; ch < 3; ++ch) centres[c][ch] = sums[c][ch] / static_cast<float>(counts[c]);
            }
        }
        if (!moved && iteration > 0) break;
    }

    // Names by colour, made unique ("dirt", "dirt-2").
    std::map<std::string, int> used;
    for (const Rgb& c : centres) {
        std::string name = TerrainNameFor(c);
        const int n = ++used[name];
        out.terrains.push_back(n == 1 ? name : name + "-" + std::to_string(n));
        out.colors.push_back(c);
    }
    for (size_t t = 0; t < tiles.size(); ++t) {
        EdgeSignature signature;
        for (int p = 0; p < kBorderPoints; ++p) signature[p] = out.terrains[label[t * kBorderPoints + p]];
        out.signatures.push_back(signature);
        // The middle joins the nearest terrain (it wasn't part of the clustering).
        size_t best = 0;
        for (size_t c = 1; c < centres.size(); ++c) {
            if (Distance2(tiles[t].center, centres[c]) < Distance2(tiles[t].center, centres[best])) best = c;
        }
        out.centers.push_back(out.terrains[best]);
    }
    return out;
}

std::string FormatSignature(const EdgeSignature& signature) {
    std::string text;
    for (int p = 0; p < kBorderPoints; ++p) text += (p ? "," : "") + signature[p];
    return text;
}

bool ParseSignature(const std::string& text, EdgeSignature& signature) {
    std::stringstream in(text);
    std::string part;
    int p = 0;
    while (std::getline(in, part, ',')) {
        if (p >= kBorderPoints || part.empty()) return false;
        signature[p++] = part;
    }
    return p == kBorderPoints;
}

} // namespace aige
