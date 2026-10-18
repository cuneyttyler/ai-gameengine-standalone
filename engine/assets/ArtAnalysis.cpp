#include "assets/ArtAnalysis.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <functional>
#include <numeric>

namespace aige {

namespace {

struct Px {
    float r, g, b, a;
};

Px At(const ImageData& image, int x, int y) {
    const size_t i = (static_cast<size_t>(y) * image.width + static_cast<size_t>(x)) * 4;
    return {image.rgba[i] / 255.0f, image.rgba[i + 1] / 255.0f, image.rgba[i + 2] / 255.0f, image.rgba[i + 3] / 255.0f};
}

float Luma(const Px& p) { return 0.299f * p.r + 0.587f * p.g + 0.114f * p.b; }
// A soft shadow: translucent and dark.
bool IsShadow(const Px& p) { return p.a >= 0.08f && p.a < 0.8f && Luma(p) < 0.3f; }
bool IsSolid(const Px& p) { return p.a >= 0.5f && !IsShadow(p); }

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::array<float, 3> ToLab(const std::array<float, 3>& rgb) {
    auto lin = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
    const float r = lin(rgb[0]), g = lin(rgb[1]), b = lin(rgb[2]);
    float x = (0.4124f * r + 0.3576f * g + 0.1805f * b) / 0.95047f;
    float y = (0.2126f * r + 0.7152f * g + 0.0722f * b);
    float z = (0.0193f * r + 0.1192f * g + 0.9505f * b) / 1.08883f;
    auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.0f / 116.0f; };
    x = f(x);
    y = f(y);
    z = f(z);
    return {116.0f * y - 16.0f, 500.0f * (x - y), 200.0f * (y - z)};
}

std::string Hex(const std::array<float, 3>& rgb) {
    char text[8];
    std::snprintf(text, sizeof(text), "#%02x%02x%02x", static_cast<int>(std::lround(rgb[0] * 255)),
                  static_cast<int>(std::lround(rgb[1] * 255)), static_cast<int>(std::lround(rgb[2] * 255)));
    return text;
}

bool Contains(const std::string& text, const char* word) { return text.find(word) != std::string::npos; }

} // namespace

std::array<float, 3> ParseHexColor(const std::string& hex) {
    unsigned r = 0, g = 0, b = 0;
    if (hex.size() == 7 && hex[0] == '#' && std::sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
        return {r / 255.0f, g / 255.0f, b / 255.0f};
    }
    return {0, 0, 0};
}

float ColorDistance(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    const auto la = ToLab(a), lb = ToLab(b);
    return std::sqrt((la[0] - lb[0]) * (la[0] - lb[0]) + (la[1] - lb[1]) * (la[1] - lb[1]) + (la[2] - lb[2]) * (la[2] - lb[2]));
}

ArtShape AnalyzeShape(const ImageData& image, ImageRect region) {
    ArtShape s;
    if (region.Empty()) region = {0, 0, static_cast<int>(image.width), static_cast<int>(image.height)};
    region.w = std::min(region.w, static_cast<int>(image.width) - region.x);
    region.h = std::min(region.h, static_cast<int>(image.height) - region.y);
    s.region = region;
    if (region.Empty() || image.rgba.size() < static_cast<size_t>(image.width) * image.height * 4) return s;
    const int x0 = region.x, y0 = region.y, x1 = region.x + region.w, y1 = region.y + region.h;

    // Solid bounds, shadow, counts, moments.
    int minX = x1, minY = y1, maxX = x0 - 1, maxY = y0 - 1;
    size_t solid = 0, shadow = 0;
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
    std::vector<int> rowCount(region.h, 0);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const Px p = At(image, x, y);
            if (IsShadow(p)) ++shadow;
            if (!IsSolid(p)) continue;
            ++solid;
            ++rowCount[y - y0];
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
            sx += x;
            sy += y;
        }
    }
    const double area = static_cast<double>(region.w) * region.h;
    s.opaqueShare = static_cast<float>(solid / area);
    if (solid == 0) {
        s.tileType = "partial";
        s.silhouette = "blob";
        return s;
    }
    s.opaque = {minX, minY, maxX - minX + 1, maxY - minY + 1};
    s.shadow = shadow > solid / 50;

    // Moments: the main axis.
    const double mx = sx / solid, my = sy / solid;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if (!IsSolid(At(image, x, y))) continue;
            sxx += (x - mx) * (x - mx);
            syy += (y - my) * (y - my);
            sxy += (x - mx) * (y - my);
        }
    }
    sxx /= solid;
    syy /= solid;
    sxy /= solid;
    s.axisDegrees = static_cast<float>(0.5 * std::atan2(2.0 * sxy, sxx - syy) * 180.0 / 3.14159265358979);
    const double common = (sxx + syy) / 2.0, diff = std::sqrt(((sxx - syy) / 2.0) * ((sxx - syy) / 2.0) + sxy * sxy);
    const double major = common + diff, minor = std::max(common - diff, 1e-6);
    s.elongation = static_cast<float>(std::sqrt(major / minor));

    // The base: the lowest real row (ignoring stray pixels), and the solid
    // columns across a thin band above it.
    const int stray = std::max(1, s.opaque.w / 50);
    int baseRow = maxY;
    while (baseRow > minY && rowCount[baseRow - y0] < stray) --baseRow;
    const int band = std::max(2, static_cast<int>(std::lround(s.opaque.h * 0.06)));
    int bMin = maxX, bMax = minX;
    for (int y = std::max(minY, baseRow - band + 1); y <= baseRow; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if (IsSolid(At(image, x, y))) {
                bMin = std::min(bMin, x);
                bMax = std::max(bMax, x);
            }
        }
    }
    s.base = {bMin, std::max(minY, baseRow - band + 1), bMax - bMin + 1, std::min(band, baseRow - minY + 1)};
    s.anchorX = (bMin + bMax + 1) / 2.0f;
    s.anchorY = static_cast<float>(baseRow + 1);

    // Borders.
    size_t borderSolid = 0, borderTotal = 0;
    auto border = [&](int x, int y) {
        ++borderTotal;
        if (At(image, x, y).a >= 0.5f) ++borderSolid;
    };
    for (int x = x0; x < x1; ++x) {
        border(x, y0);
        border(x, y1 - 1);
    }
    for (int y = y0; y < y1; ++y) {
        border(x0, y);
        border(x1 - 1, y);
    }
    s.edgesOpaque = borderSolid >= borderTotal * 97 / 100;

    // Seamless: opposite borders differ no more than neighbouring columns
    // and rows inside do.
    if (s.edgesOpaque && region.w >= 4 && region.h >= 4) {
        auto diff = [&](int ax, int ay, int bx, int by) {
            const Px a = At(image, ax, ay), b = At(image, bx, by);
            return (std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b)) / 3.0f;
        };
        double edge = 0, inner = 0;
        int ne = 0, ni = 0;
        for (int y = y0; y < y1; ++y) {
            edge += diff(x0, y, x1 - 1, y);
            ++ne;
            for (int x = x0; x + 1 < x1; x += std::max(1, region.w / 8)) {
                inner += diff(x, y, x + 1, y);
                ++ni;
            }
        }
        for (int x = x0; x < x1; ++x) {
            edge += diff(x, y0, x, y1 - 1);
            ++ne;
            for (int y = y0; y + 1 < y1; y += std::max(1, region.h / 8)) {
                inner += diff(x, y, x, y + 1);
                ++ni;
            }
        }
        s.seamless = edge / ne <= 1.5 * (inner / ni) + 0.02;
    }
    const bool complete = s.opaqueShare > 0.97f && s.edgesOpaque;
    s.tileType = complete ? (s.seamless ? "texture" : "complete") : "partial";

    // Silhouette.
    auto patchSolid = [&](float fx, float fy, float fw, float fh) {
        const int px0 = x0 + static_cast<int>(fx * region.w), py0 = y0 + static_cast<int>(fy * region.h);
        const int pw = std::max(1, static_cast<int>(fw * region.w)), ph = std::max(1, static_cast<int>(fh * region.h));
        int n = 0, k = 0;
        for (int y = py0; y < std::min(y1, py0 + ph); ++y) {
            for (int x = px0; x < std::min(x1, px0 + pw); ++x) {
                ++k;
                if (At(image, x, y).a >= 0.5f) ++n;
            }
        }
        return k ? static_cast<float>(n) / k : 0.0f;
    };
    const float aspect = static_cast<float>(region.w) / static_cast<float>(region.h);
    const bool cornersClear = patchSolid(0, 0, 0.12f, 0.12f) < 0.2f && patchSolid(0.88f, 0, 0.12f, 0.12f) < 0.2f &&
                              patchSolid(0, 0.88f, 0.12f, 0.12f) < 0.2f && patchSolid(0.88f, 0.88f, 0.12f, 0.12f) < 0.2f;
    const bool middlesSolid = patchSolid(0.45f, 0.02f, 0.1f, 0.08f) > 0.5f && patchSolid(0.45f, 0.9f, 0.1f, 0.08f) > 0.5f &&
                              patchSolid(0.02f, 0.45f, 0.08f, 0.1f) > 0.5f && patchSolid(0.9f, 0.45f, 0.08f, 0.1f) > 0.5f;
    if (s.opaqueShare > 0.95f) {
        s.silhouette = "rect";
    } else if (cornersClear && middlesSolid && s.opaqueShare > 0.38f && s.opaqueShare < 0.68f &&
               ((aspect > 1.7f && aspect < 2.3f) || (aspect > 0.85f && aspect < 1.15f))) {
        s.silhouette = "diamond";
    } else if (s.opaque.h >= 0.5f * s.opaque.w || s.base.w <= 0.6f * s.opaque.w) {
        s.silhouette = "upright";
    } else {
        s.silhouette = "blob";
    }

    // Symmetry: the solid mask against its mirror, within the bounds.
    size_t mismatch = 0;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const int mxr = maxX - (x - minX);
            const Px a = At(image, x, y), b = At(image, mxr, y);
            if (IsSolid(a) != IsSolid(b)) ++mismatch;
            else if (IsSolid(a) && std::abs(Luma(a) - Luma(b)) > 0.2f) ++mismatch;
        }
    }
    s.symmetric = mismatch < solid / 12;

    // Where it touches each border's middle: N, E, S, W.
    const float depth = 0.06f;
    s.touches = {patchSolid(0.3f, 0, 0.4f, depth) > 0.1f, patchSolid(1 - depth, 0.3f, depth, 0.4f) > 0.1f,
                 patchSolid(0.3f, 1 - depth, 0.4f, depth) > 0.1f, patchSolid(0, 0.3f, depth, 0.4f) > 0.1f};

    // Palette: 4 bits per channel, the most common colours, near-duplicates merged.
    std::map<int, std::pair<size_t, std::array<double, 3>>> bins;
    const int step = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(solid) / 20000.0)));
    for (int y = minY; y <= maxY; y += step) {
        for (int x = minX; x <= maxX; x += step) {
            const Px p = At(image, x, y);
            if (!IsSolid(p)) continue;
            const int key = (static_cast<int>(p.r * 15.99f) << 8) | (static_cast<int>(p.g * 15.99f) << 4) | static_cast<int>(p.b * 15.99f);
            auto& bin = bins[key];
            ++bin.first;
            bin.second[0] += p.r;
            bin.second[1] += p.g;
            bin.second[2] += p.b;
        }
    }
    std::vector<std::pair<size_t, std::array<float, 3>>> colors;
    for (const auto& [key, bin] : bins) {
        colors.push_back({bin.first, {static_cast<float>(bin.second[0] / bin.first), static_cast<float>(bin.second[1] / bin.first),
                                      static_cast<float>(bin.second[2] / bin.first)}});
    }
    std::sort(colors.begin(), colors.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::array<float, 3>> kept;
    for (const auto& [count, rgb] : colors) {
        if (kept.size() >= 6) break;
        if (std::any_of(kept.begin(), kept.end(), [&](const auto& k) { return ColorDistance(k, rgb) < 12.0f; })) continue;
        kept.push_back(rgb);
    }
    for (const auto& rgb : kept) s.palette.push_back(Hex(rgb));
    return s;
}

std::string PerspectiveCue(const ArtShape& shape) {
    if (shape.silhouette == "rect") return "top-down";
    if (shape.silhouette == "diamond") return "isometric";
    if (shape.silhouette == "upright") return "upright";
    return "";
}

Footprint FootprintOf(const ArtShape& shape, bool flat, float pixelsPerCell) {
    Footprint f;
    const float ppc = pixelsPerCell > 0 ? pixelsPerCell : 32.0f;
    if (flat) {
        f.w = shape.opaque.w / ppc;
        f.h = shape.silhouette == "diamond" ? f.w : shape.opaque.h / ppc; // a diamond's width is its cells' diagonal sum
    } else {
        f.w = shape.base.w / ppc;
        f.h = std::max(1.0f, f.w * 0.5f);
    }
    f.cellsW = std::max(1, static_cast<int>(std::lround(f.w)));
    f.cellsH = std::max(1, static_cast<int>(std::lround(f.h)));
    return f;
}

std::vector<ImageRect> SegmentObjects(const ImageData& image, int gap, int minPixels) {
    const int w = static_cast<int>(image.width), h = static_cast<int>(image.height);
    std::vector<int> label(static_cast<size_t>(w) * h, -1);
    struct Box {
        int x0, y0, x1, y1;
        size_t pixels;
    };
    std::vector<Box> boxes;
    std::vector<int> stack;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * w + x;
            if (label[i] >= 0 || At(image, x, y).a < 0.1f) continue;
            const int id = static_cast<int>(boxes.size());
            boxes.push_back({x, y, x, y, 0});
            stack.assign(1, static_cast<int>(i));
            label[i] = id;
            while (!stack.empty()) {
                const int p = stack.back();
                stack.pop_back();
                const int px = p % w, py = p / w;
                Box& b = boxes[id];
                b.x0 = std::min(b.x0, px);
                b.x1 = std::max(b.x1, px);
                b.y0 = std::min(b.y0, py);
                b.y1 = std::max(b.y1, py);
                ++b.pixels;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = px + dx, ny = py + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        const size_t n = static_cast<size_t>(ny) * w + nx;
                        if (label[n] >= 0 || At(image, nx, ny).a < 0.1f) continue;
                        label[n] = id;
                        stack.push_back(static_cast<int>(n));
                    }
                }
            }
        }
    }
    // Join pieces closer than `gap` (a tree's crown and its trunk shadow).
    bool merged = true;
    while (merged) {
        merged = false;
        for (size_t i = 0; i < boxes.size() && !merged; ++i) {
            for (size_t j = i + 1; j < boxes.size(); ++j) {
                const Box& a = boxes[i];
                const Box& b = boxes[j];
                if (a.x0 - gap > b.x1 || b.x0 - gap > a.x1 || a.y0 - gap > b.y1 || b.y0 - gap > a.y1) continue;
                boxes[i] = {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1), a.pixels + b.pixels};
                boxes.erase(boxes.begin() + static_cast<long>(j));
                merged = true;
                break;
            }
        }
    }
    std::vector<ImageRect> out;
    for (const Box& b : boxes) {
        if (static_cast<int>(b.pixels) >= minPixels) out.push_back({b.x0, b.y0, b.x1 - b.x0 + 1, b.y1 - b.y0 + 1});
    }
    // Reading order: rows (boxes overlapping a row's top half), then left to right.
    std::sort(out.begin(), out.end(), [](const ImageRect& a, const ImageRect& b) { return a.y < b.y; });
    std::vector<ImageRect> ordered;
    for (size_t i = 0; i < out.size();) {
        const int rowTop = out[i].y, rowBottom = out[i].y + out[i].h / 2;
        size_t j = i;
        while (j < out.size() && out[j].y <= rowBottom) ++j;
        std::sort(out.begin() + static_cast<long>(i), out.begin() + static_cast<long>(j),
                  [](const ImageRect& a, const ImageRect& b) { return a.x < b.x; });
        ordered.insert(ordered.end(), out.begin() + static_cast<long>(i), out.begin() + static_cast<long>(j));
        (void)rowTop;
        i = j;
    }
    return ordered;
}

std::vector<ImageRect> SegmentTiles(const ImageData& image, int tw, int th) {
    if (tw <= 0 || th <= 0) return {};
    const int cols = static_cast<int>(image.width) / tw, rows = static_cast<int>(image.height) / th;
    std::vector<int> parent(static_cast<size_t>(cols) * rows);
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> find = [&](int i) { return parent[i] == i ? i : parent[i] = find(parent[i]); };
    std::vector<bool> used(parent.size(), false);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            for (int y = r * th; y < (r + 1) * th && !used[r * cols + c]; ++y) {
                for (int x = c * tw; x < (c + 1) * tw; ++x) {
                    if (At(image, x, y).a >= 0.1f) {
                        used[r * cols + c] = true;
                        break;
                    }
                }
            }
        }
    }
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (!used[r * cols + c]) continue;
            // Content running across the right border, and the bottom one.
            if (c + 1 < cols && used[r * cols + c + 1]) {
                const int x = (c + 1) * tw;
                for (int y = r * th; y < (r + 1) * th; ++y) {
                    if (IsSolid(At(image, x - 1, y)) && IsSolid(At(image, x, y))) {
                        parent[find(r * cols + c + 1)] = find(r * cols + c);
                        break;
                    }
                }
            }
            if (r + 1 < rows && used[(r + 1) * cols + c]) {
                const int y = (r + 1) * th;
                for (int x = c * tw; x < (c + 1) * tw; ++x) {
                    if (IsSolid(At(image, x, y - 1)) && IsSolid(At(image, x, y))) {
                        parent[find((r + 1) * cols + c)] = find(r * cols + c);
                        break;
                    }
                }
            }
        }
    }
    std::map<int, ImageRect> groups;
    std::vector<int> order;
    for (int i = 0; i < cols * rows; ++i) {
        if (!used[i]) continue;
        const int root = find(i);
        const ImageRect tile{(i % cols) * tw, (i / cols) * th, tw, th};
        auto [it, added] = groups.try_emplace(root, tile);
        if (added) {
            order.push_back(root);
            continue;
        }
        ImageRect& g = it->second;
        const int gx1 = std::max(g.x + g.w, tile.x + tile.w), gy1 = std::max(g.y + g.h, tile.y + tile.h);
        g.x = std::min(g.x, tile.x);
        g.y = std::min(g.y, tile.y);
        g.w = gx1 - g.x;
        g.h = gy1 - g.y;
    }
    std::vector<ImageRect> out;
    for (int root : order) out.push_back(groups[root]);
    return out;
}

std::string PieceKind(const ArtShape& shape, bool isoGrid, const std::string& name) {
    const std::string n = Lower(name);
    if (Contains(n, "gate") || Contains(n, "door")) return "gate";
    if (Contains(n, "post") || Contains(n, "pillar")) return "post";
    if (isoGrid) {
        if (shape.elongation >= 2.0f) {
            const float a = shape.axisDegrees;
            if (std::abs(a - 26.57f) < 12.0f) return "straight-x";
            if (std::abs(a + 26.57f) < 12.0f) return "straight-y";
            if (std::abs(a) < 8.0f) return "straight-h"; // drawn for a square grid
            if (std::abs(std::abs(a) - 90.0f) < 8.0f) return "straight-v";
        }
        if (Contains(n, "corner")) return "corner";
        if (Contains(n, "end")) return "end";
        return "post";
    }
    const bool N = shape.touches[0], E = shape.touches[1], S = shape.touches[2], W = shape.touches[3];
    const int count = N + E + S + W;
    if (count == 4) return "cross";
    if (count == 3) return !N ? "t-s" : !E ? "t-w" : !S ? "t-n" : "t-e";
    if (count == 2) {
        if (E && W) return "straight-h";
        if (N && S) return "straight-v";
        if (N && E) return "corner-ne";
        if (E && S) return "corner-se";
        if (S && W) return "corner-sw";
        return "corner-nw";
    }
    if (count == 1) return N ? "end-n" : E ? "end-e" : S ? "end-s" : "end-w";
    // Touches nothing: a lone piece, unless its long axis says which way it runs.
    if (shape.elongation >= 2.5f) {
        if (std::abs(shape.axisDegrees) < 15.0f) return "straight-h";
        if (std::abs(std::abs(shape.axisDegrees) - 90.0f) < 15.0f) return "straight-v";
        if (std::abs(shape.axisDegrees - 26.57f) < 12.0f) return "straight-x"; // drawn for an isometric grid
        if (std::abs(shape.axisDegrees + 26.57f) < 12.0f) return "straight-y";
    }
    return "post";
}

} // namespace aige
