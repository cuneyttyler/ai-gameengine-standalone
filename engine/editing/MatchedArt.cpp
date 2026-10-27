#include "editing/MatchedArt.h"

#include "assets/AssetDatabase.h"
#include "assets/Catalog.h"
#include "assets/Project.h"
#include "assets/TextureCook.h"
#include "editing/PlaceBlockout.h"
#include "places/Ontology.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace aige {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

// The block-out's view: where cell (x, y) at height `up` lands in its picture (RenderPlanBlockout).
struct View {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0, tw = 32, th = 16, top = 48;
    std::array<double, 2> P(double wx, double wy, double up = 0) const {
        return {(wx - x0 - (wy - y0)) * tw / 2 + (y1 - y0) * tw / 2, (wx - x0 + wy - y0) * th / 2 + top - up};
    }
};

int IdAt(const ImageData& ids, double fx, double fy) {
    const int x = static_cast<int>(std::floor(fx)), y = static_cast<int>(std::floor(fy));
    if (x < 0 || y < 0 || x >= static_cast<int>(ids.width) || y >= static_cast<int>(ids.height)) return 0;
    const uint8_t* p = &ids.rgba[(static_cast<size_t>(y) * ids.width + x) * 4];
    return p[0] | (p[1] << 8) | (p[2] << 16);
}

// A painting's pixel, bilinear, at a point in its own pixels.
std::array<float, 3> Sample(const ImageData& im, double x, double y) {
    x = std::clamp(x - 0.5, 0.0, im.width - 1.0), y = std::clamp(y - 0.5, 0.0, im.height - 1.0);
    const int xa = static_cast<int>(x), ya = static_cast<int>(y);
    const int xb = std::min<int>(xa + 1, im.width - 1), yb = std::min<int>(ya + 1, im.height - 1);
    const double fx = x - xa, fy = y - ya;
    std::array<float, 3> out{};
    for (int c = 0; c < 3; ++c) {
        auto at = [&](int px, int py) { return static_cast<double>(im.rgba[(static_cast<size_t>(py) * im.width + px) * 4 + c]); };
        out[c] = static_cast<float>((at(xa, ya) * (1 - fx) + at(xb, ya) * fx) * (1 - fy) + (at(xa, yb) * (1 - fx) + at(xb, yb) * fx) * fy);
    }
    return out;
}

// The painting's edges at the block-out's size: its greys box-averaged down, then their gradient's strength.
struct Edges {
    int w = 0, h = 0;
    std::vector<float> v;
    float At(double fx, double fy) const {
        const int x = static_cast<int>(fx), y = static_cast<int>(fy);
        return x < 0 || y < 0 || x >= w || y >= h ? 0.0f : v[static_cast<size_t>(y) * w + x];
    }
};

Edges EdgesOf(const ImageData& sketch, int w, int h) {
    std::vector<float> grey(static_cast<size_t>(w) * h, 0.0f);
    const double sx = static_cast<double>(sketch.width) / w, sy = static_cast<double>(sketch.height) / h;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int ax = static_cast<int>(x * sx), bx = std::max(ax + 1, static_cast<int>((x + 1) * sx));
            const int ay = static_cast<int>(y * sy), by = std::max(ay + 1, static_cast<int>((y + 1) * sy));
            double sum = 0;
            int n = 0;
            for (int py = ay; py < by && py < static_cast<int>(sketch.height); ++py) {
                for (int px = ax; px < bx && px < static_cast<int>(sketch.width); ++px) {
                    const uint8_t* p = &sketch.rgba[(static_cast<size_t>(py) * sketch.width + px) * 4];
                    sum += 0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2];
                    ++n;
                }
            }
            grey[static_cast<size_t>(y) * w + x] = n ? static_cast<float>(sum / n) : 0.0f;
        }
    }
    Edges e;
    e.w = w, e.h = h;
    e.v.assign(grey.size(), 0.0f);
    auto g = [&](int x, int y) { return grey[static_cast<size_t>(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)]; };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float gx = g(x + 1, y - 1) + 2 * g(x + 1, y) + g(x + 1, y + 1) - g(x - 1, y - 1) - 2 * g(x - 1, y) - g(x - 1, y + 1);
            const float gy = g(x - 1, y + 1) + 2 * g(x, y + 1) + g(x + 1, y + 1) - g(x - 1, y - 1) - 2 * g(x, y - 1) - g(x + 1, y - 1);
            e.v[static_cast<size_t>(y) * w + x] = std::sqrt(gx * gx + gy * gy);
        }
    }
    return e;
}

// The painting's drift from the block-out, as a whole: a block-out point p is painted at (p - c) scale + c + d.
struct Align {
    double dx = 0, dy = 0, scale = 1, cx = 0, cy = 0, score = 0;
    std::array<double, 2> Map(double x, double y) const { return {(x - cx) * scale + cx + dx, (y - cy) * scale + cy + dy}; }
};

Align GlobalAlign(const Edges& edges, const std::vector<std::array<int, 2>>& outline, int w, int h) {
    Align best;
    best.cx = w / 2.0, best.cy = h / 2.0;
    auto score = [&](const Align& a) {
        double sum = 0;
        for (const auto& p : outline) {
            const auto q = a.Map(p[0] + 0.5, p[1] + 0.5);
            sum += edges.At(q[0], q[1]);
        }
        return outline.empty() ? 0.0 : sum / outline.size();
    };
    best.score = score(best);
    const double base = best.score;
    // Coarse, then fine.
    for (const double s : {0.97, 0.985, 1.0, 1.015, 1.03}) {
        for (int dy = -30; dy <= 30; dy += 3) {
            for (int dx = -30; dx <= 30; dx += 3) {
                Align a = best;
                a.scale = s, a.dx = dx, a.dy = dy;
                if (const double v = score(a); v > best.score) a.score = v, best = a;
            }
        }
    }
    const Align coarse = best;
    for (const double ds : {-0.0075, 0.0, 0.0075}) {
        for (int dy = -3; dy <= 3; ++dy) {
            for (int dx = -3; dx <= 3; ++dx) {
                Align a = coarse;
                a.scale += ds, a.dx += dx, a.dy += dy;
                if (const double v = score(a); v > best.score) a.score = v, best = a;
            }
        }
    }
    // Moved only for a clear gain: an empty painting mustn't wander.
    if (best.score < base * 1.03) {
        Align none;
        none.cx = best.cx, none.cy = best.cy, none.score = base;
        return none;
    }
    return best;
}

std::string Slug(const std::string& text) {
    std::string out;
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "piece" : out.substr(0, 80);
}

std::string Number(double v) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f", v);
    return text;
}

bool IsKey(const uint8_t* p) {
    // The key colour, or near it (a painting's magenta isn't exact): strong red and blue, little green.
    const int r = p[0], g = p[1], b = p[2];
    const int d = std::abs(r - kMatchedKey[0]) + std::abs(g - kMatchedKey[1]) + std::abs(b - kMatchedKey[2]);
    return d < 150 || (r - g > 90 && b - g > 90 && std::abs(r - b) < 90);
}

// A site of `cells` ("sheet" ground everywhere but where `terrainOf` says) as a solved layout, for the block-out.
json SheetLayout(int side, const std::function<std::string(int, int)>& terrainOf, const json& objects) {
    json palette = json::array(), rows = json::array();
    std::map<std::string, int> index;
    for (int y = 0; y < side; ++y) {
        json row = json::array();
        int run = 0, last = -1;
        for (int x = 0; x < side; ++x) {
            const std::string t = terrainOf(x, y);
            auto [it, fresh] = index.try_emplace(t, static_cast<int>(index.size()));
            if (fresh) palette.push_back(t);
            if (it->second != last && run > 0) row.push_back({run, last}), run = 0;
            last = it->second;
            ++run;
        }
        if (run > 0) row.push_back({run, last});
        rows.push_back(row);
    }
    return {{"grid", {{"bounds", {0, 0, side, side}}}}, {"terrain", {{"palette", palette}, {"rows", rows}}}, {"objects", objects},
            {"lines", json::array()}, {"regions", json::array()}};
}

// The box round every pixel the id map gives to an object (or, with `ground`, every pixel the test passes), with a margin.
std::array<int, 4> Crop(const ImageData& ids, int margin, const std::function<bool(int, int)>& keep) {
    int x0 = ids.width, y0 = ids.height, x1 = 0, y1 = 0;
    for (int y = 0; y < static_cast<int>(ids.height); ++y) {
        for (int x = 0; x < static_cast<int>(ids.width); ++x) {
            if (!keep(x, y)) continue;
            x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x + 1), y1 = std::max(y1, y + 1);
        }
    }
    if (x1 <= x0) return {0, 0, static_cast<int>(ids.width), static_cast<int>(ids.height)};
    return {std::max(0, x0 - margin), std::max(0, y0 - margin), std::min<int>(ids.width, x1 + margin), std::min<int>(ids.height, y1 + margin)};
}

ImageData CropImage(const ImageData& im, const std::array<int, 4>& c) {
    ImageData out;
    out.width = static_cast<uint32_t>(c[2] - c[0]), out.height = static_cast<uint32_t>(c[3] - c[1]);
    out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
    for (uint32_t y = 0; y < out.height; ++y) {
        std::copy_n(&im.rgba[((static_cast<size_t>(y) + c[1]) * im.width + c[0]) * 4], out.width * 4, &out.rgba[static_cast<size_t>(y) * out.width * 4]);
    }
    return out;
}

// A colour's distance to its nearest in a palette.
double Nearest(const std::vector<std::array<float, 3>>& palette, const uint8_t* p, size_t* which = nullptr) {
    double best = 1e18;
    for (size_t i = 0; i < palette.size(); ++i) {
        const double d = std::pow(p[0] - palette[i][0], 2) + std::pow(p[1] - palette[i][1], 2) + std::pow(p[2] - palette[i][2], 2);
        if (d < best) {
            best = d;
            if (which) *which = i;
        }
    }
    return std::sqrt(best);
}

// The colours of some pieces, as k means (the style sheet's palette).
std::vector<std::array<float, 3>> PaletteOf(const std::vector<ImageData>& pieces, int k) {
    std::vector<std::array<float, 3>> samples;
    for (const auto& im : pieces) {
        const size_t n = im.rgba.size() / 4, step = std::max<size_t>(1, n / 4000);
        for (size_t i = 0; i < n; i += step) {
            if (im.rgba[i * 4 + 3] > 200) samples.push_back({float(im.rgba[i * 4]), float(im.rgba[i * 4 + 1]), float(im.rgba[i * 4 + 2])});
        }
    }
    std::vector<std::array<float, 3>> centres;
    if (samples.empty()) return centres;
    for (int i = 0; i < k; ++i) centres.push_back(samples[(samples.size() * i) / k]);
    for (int round = 0; round < 10; ++round) {
        std::vector<std::array<double, 4>> sum(centres.size(), {0, 0, 0, 0});
        for (const auto& s : samples) {
            size_t best = 0;
            double d = 1e18;
            for (size_t c = 0; c < centres.size(); ++c) {
                const double e = std::pow(s[0] - centres[c][0], 2) + std::pow(s[1] - centres[c][1], 2) + std::pow(s[2] - centres[c][2], 2);
                if (e < d) d = e, best = c;
            }
            sum[best][0] += s[0], sum[best][1] += s[1], sum[best][2] += s[2], sum[best][3] += 1;
        }
        for (size_t c = 0; c < centres.size(); ++c) {
            if (sum[c][3] > 0) centres[c] = {float(sum[c][0] / sum[c][3]), float(sum[c][1] / sum[c][3]), float(sum[c][2] / sum[c][3])};
        }
    }
    return centres;
}

// A piece's opaque pixels kept to the part joined to its biggest piece (specks of the background dropped), its
// key-coloured fringe taken out.
void Clean(ImageData& im) {
    const int w = static_cast<int>(im.width), h = static_cast<int>(im.height);
    std::vector<int> part(static_cast<size_t>(w) * h, -1);
    std::vector<int> sizes;
    for (int start = 0; start < w * h; ++start) {
        if (im.rgba[static_cast<size_t>(start) * 4 + 3] < 128 || part[start] >= 0) continue;
        const int label = static_cast<int>(sizes.size());
        std::vector<int> todo{start};
        part[start] = label;
        int n = 0;
        while (!todo.empty()) {
            const int at = todo.back();
            todo.pop_back();
            ++n;
            const int x = at % w, y = at / w;
            for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const int nb = ny * w + nx;
                if (part[nb] >= 0 || im.rgba[static_cast<size_t>(nb) * 4 + 3] < 128) continue;
                part[nb] = label;
                todo.push_back(nb);
            }
        }
        sizes.push_back(n);
    }
    if (sizes.empty()) return;
    const int biggest = static_cast<int>(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());
    for (int i = 0; i < w * h; ++i) {
        uint8_t* p = &im.rgba[static_cast<size_t>(i) * 4];
        // Parts at least a fifth of the biggest stay (a lamp's head beside its pole); specks go.
        if (p[3] > 0 && (part[i] < 0 || sizes[part[i]] * 5 < sizes[biggest])) {
            p[3] = 0;
            continue;
        }
        // The key's tint at the edge: red and blue pulled down to the green.
        const int m = std::min(p[0], p[2]);
        if (p[3] > 0 && m > p[1] + 30) {
            p[0] = static_cast<uint8_t>(p[0] - (m - p[1]) * 0.7), p[2] = static_cast<uint8_t>(p[2] - (m - p[1]) * 0.7);
        }
    }
}

// An image's opaque pixels' box, or nothing.
std::optional<std::array<int, 4>> OpaqueBox(const ImageData& im) {
    int x0 = im.width, y0 = im.height, x1 = -1, y1 = -1;
    for (uint32_t y = 0; y < im.height; ++y) {
        for (uint32_t x = 0; x < im.width; ++x) {
            if (im.rgba[(static_cast<size_t>(y) * im.width + x) * 4 + 3] < 128) continue;
            x0 = std::min<int>(x0, x), y0 = std::min<int>(y0, y), x1 = std::max<int>(x1, x + 1), y1 = std::max<int>(y1, y + 1);
        }
    }
    if (x1 < 0) return std::nullopt;
    return std::array<int, 4>{x0, y0, x1, y1};
}

// The ground of a painted patch unwarped to a top-down square, seamless (blended with itself moved by half, the
// moved copy weighted at the edges).
ImageData Seamless(const std::function<std::array<float, 3>(double u, double v)>& sample, int size = 128) {
    std::vector<std::array<float, 3>> base(static_cast<size_t>(size) * size);
    for (int v = 0; v < size; ++v) {
        for (int u = 0; u < size; ++u) base[static_cast<size_t>(v) * size + u] = sample((u + 0.5) / size, (v + 0.5) / size);
    }
    ImageData tex;
    tex.width = tex.height = static_cast<uint32_t>(size);
    tex.rgba.assign(static_cast<size_t>(size) * size * 4, 255);
    for (int v = 0; v < size; ++v) {
        for (int u = 0; u < size; ++u) {
            const double edge = std::min({u + 0.5, size - u - 0.5, v + 0.5, size - v - 0.5}) / (size / 2.0);
            const double k = std::clamp(edge * 1.6, 0.0, 1.0);
            const auto& a = base[static_cast<size_t>(v) * size + u];
            const auto& m = base[static_cast<size_t>((v + size / 2) % size) * size + (u + size / 2) % size];
            uint8_t* px = &tex.rgba[(static_cast<size_t>(v) * size + u) * 4];
            for (int c = 0; c < 3; ++c) px[c] = static_cast<uint8_t>(std::clamp(a[c] * k + m[c] * (1 - k), 0.0, 255.0));
        }
    }
    return tex;
}

// A JSON file's contents, or null.
json ReadJsonFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return json();
    json value = json::parse(in, nullptr, false);
    return value.is_discarded() ? json() : value;
}

// A painting's foot: its left corner, front corner (its lowest point) and right corner, as columns. Empty when too small.
std::optional<std::array<int, 3>> FootCorners(const ImageData& painted) {
    const int W = static_cast<int>(painted.width), H = static_cast<int>(painted.height);
    if (W < 16 || H < 16) return std::nullopt;
    std::vector<int> foot(W, -1);
    for (int x = 0; x < W; ++x) {
        for (int y = H - 1; y >= 0; --y) {
            if (painted.rgba[(static_cast<size_t>(y) * W + x) * 4 + 3] > 128) {
                foot[x] = y;
                break;
            }
        }
    }
    int xl = 0, xr = W - 1;
    while (xl < W && foot[xl] < 0) ++xl;
    while (xr > xl && foot[xr] < 0) --xr;
    if (xr - xl < 16) return std::nullopt;
    int xf = xl, lowest = -1;
    for (int x = xl + (xr - xl) / 20; x <= xr - (xr - xl) / 20; ++x) {
        if (foot[x] > lowest) lowest = foot[x], xf = x;
    }
    return std::array<int, 3>{xl, xf, xr};
}

// A building's painting fitted to its plan's footprint (w x h cells): its foot's corners measured (the lowest painted
// pixel of each column: the left corner, the front corner (lowest), the right corner) and the picture re-proportioned so
// its front corner is w / (w + h) of the way across and both edges of its foot slope 1 in 2, as the plan's box does.
// Columns stretch or squeeze on each side of the front corner; each column moves up or down so its foot lands on the
// box's. A building painted 9 x 6 for a 9 x 3 lot comes out 9 x 3; walls stay upright.
ImageData FitFootprint(const ImageData& painted, int w, int h) {
    const int W = static_cast<int>(painted.width), H = static_cast<int>(painted.height);
    if (W < 16 || H < 16) return painted;
    std::vector<int> foot(W, -1);
    for (int x = 0; x < W; ++x) {
        for (int y = H - 1; y >= 0; --y) {
            if (painted.rgba[(static_cast<size_t>(y) * W + x) * 4 + 3] > 128) {
                foot[x] = y;
                break;
            }
        }
    }
    int xl = 0, xr = W - 1;
    while (xl < W && foot[xl] < 0) ++xl;
    while (xr > xl && foot[xr] < 0) --xr;
    if (xr - xl < 16) return painted;
    // The front corner: the lowest foot in the middle 90%.
    int xf = xl, lowest = -1;
    for (int x = xl + (xr - xl) / 20; x <= xr - (xr - xl) / 20; ++x) {
        if (foot[x] > lowest) lowest = foot[x], xf = x;
    }
    const double want = xl + (xr - xl) * static_cast<double>(w) / (w + h);
    if (std::abs(xf - want) < 0.03 * (xr - xl)) return painted;
    // The source foot, smoothed (a door step, a sign below the line shouldn't steer it): a straight line each side.
    auto footAt = [&](double x) {
        const double a = x < xf ? (x - xl) / std::max(1, xf - xl) : (xr - x) / std::max(1, xr - xf);
        const double corner = x < xf ? foot[xl] : foot[xr];
        return corner + (lowest - corner) * std::clamp(a, 0.0, 1.0);
    };
    ImageData out;
    out.width = painted.width, out.height = painted.height;
    out.rgba.assign(painted.rgba.size(), 0);
    for (int x = 0; x < W; ++x) {
        // Back to the painting's column: each side of the front corner scaled on its own.
        double sx = x;
        if (x >= xl && x <= xr) sx = x < want ? xl + (x - xl) * (xf - xl) / std::max(1.0, want - xl) : xf + (x - want) * (xr - xf) / std::max(1.0, xr - want);
        const int sxi = std::clamp(static_cast<int>(sx), 0, W - 1);
        // Its foot where the box's is: the front corner's height less half the distance from it (1 in 2).
        const double target = lowest - 0.5 * std::abs(x - want);
        const double shift = target - footAt(sx);
        for (int y = 0; y < H; ++y) {
            const int sy = static_cast<int>(std::lround(y - shift));
            if (sy < 0 || sy >= H) continue;
            std::copy_n(&painted.rgba[(static_cast<size_t>(sy) * W + sxi) * 4], 4, &out.rgba[(static_cast<size_t>(y) * W + x) * 4]);
        }
    }
    return out;
}

// A run as painted, set to the grid's angle (docs/dungeon.md §4): the model paints its own "isometric", so the run's
// foot along its front slopes a little more or less than the grid's 1 in 2. The slope is measured (the lowest painted
// pixel of each column along the front face, a straight line through them) and the picture scaled up or down to 1 in 2.
ImageData Level(const ImageData& painted, int w, int h) {
    const double k = static_cast<double>(painted.width) / (w + h);
    const int x0 = static_cast<int>((0.15 + 0) * k), x1 = static_cast<int>((w - 0.15) * k);
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (int x = std::max(0, x0); x < std::min(static_cast<int>(painted.width), x1); ++x) {
        for (int y = static_cast<int>(painted.height) - 1; y >= 0; --y) {
            if (painted.rgba[(static_cast<size_t>(y) * painted.width + x) * 4 + 3] > 128) {
                sx += x, sy += y, sxx += static_cast<double>(x) * x, sxy += static_cast<double>(x) * y, ++n;
                break;
            }
        }
    }
    if (n < 8) return painted;
    const double slope = (n * sxy - sx * sy) / std::max(1e-6, n * sxx - sx * sx);
    if (slope <= 0.05) return painted;
    const double factor = std::clamp(0.5 / slope, 0.75, 1.33);
    if (std::abs(factor - 1) < 0.02) return painted;
    ImageData out;
    out.width = painted.width, out.height = static_cast<uint32_t>(std::max(1.0, std::round(painted.height * factor)));
    out.rgba.assign(static_cast<size_t>(out.width) * out.height * 4, 0);
    for (uint32_t y = 0; y < out.height; ++y) {
        const uint32_t sy0 = std::min(painted.height - 1, static_cast<uint32_t>(y / factor));
        std::copy_n(&painted.rgba[static_cast<size_t>(sy0) * painted.width * 4], painted.width * 4, &out.rgba[static_cast<size_t>(y) * out.width * 4]);
    }
    return out;
}

// Where along a run a thing painted in it stands (a door in its wall, docs/dungeon.md §2): the run with the thing
// against the same wall painted plain (both cropped to the box, the plain one scaled to the other), the columns along the
// front face where they differ most, a cell wide. Returns the cell to cut (0 .. w-1, fractional), or `fallback`.
double FindInRun(const ImageData& with, const ImageData& plain, int w, int h, double fallback) {
    if (plain.width == 0 || with.width == 0) return fallback;
    const double k = static_cast<double>(with.width) / (w + h);
    std::vector<double> diff(with.width, 0.0);
    for (uint32_t x = 0; x < with.width; ++x) {
        const uint32_t px = std::min(plain.width - 1, static_cast<uint32_t>(x * plain.width / with.width));
        double sum = 0;
        int count = 0;
        for (uint32_t y = 0; y < with.height; ++y) {
            const uint32_t py = std::min(plain.height - 1, static_cast<uint32_t>(y * plain.height / with.height));
            const uint8_t* a = &with.rgba[(static_cast<size_t>(y) * with.width + x) * 4];
            const uint8_t* b = &plain.rgba[(static_cast<size_t>(py) * plain.width + px) * 4];
            if (a[3] < 128 || b[3] < 128) continue;
            sum += std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]);
            ++count;
        }
        diff[x] = count ? sum / count : 0;
    }
    // The front face's columns (u from -h to w - h): a window a cell's front wide (k pixels), the most different.
    const int span = std::max(1, static_cast<int>(k));
    double best = -1;
    int at = -1;
    for (int x = 0; x + span <= static_cast<int>(w * k) && x + span <= static_cast<int>(with.width); ++x) {
        double sum = 0;
        for (int i = x; i < x + span; ++i) sum += diff[i];
        if (sum > best) best = sum, at = x;
    }
    if (at < 0) return fallback;
    // The window's middle on the front face (y = h): x = u + h, u = -h + column / k.
    return std::clamp((at + span / 2.0) / k - 0.5, 0.0, static_cast<double>(w - 1));
}

// One cell of a kit (a box `w` x `h` cells painted as one run along x, cropped to what was painted): the pixels whose
// surface lies in cell x = take..take+1, found by which face of the box each pixel shows (top, front at y = h, end at
// x = w), framed as a one-cell thing (centred on the cell, down to its front corner). A run's last cell keeps its
// painted end; every cell's sides are flat cuts, so copies stand side by side with no gap.
ImageData KitCell(const ImageData& painted, int w, int h, double take) {
    const double k = static_cast<double>(painted.width) / (w + h); // pixels per half a cell across (u)
    const double hz = painted.height / (k / 2) - (w + h);          // the box's height, in quarter cells (v)
    const int px0 = static_cast<int>(std::lround(take * k)), px1 = static_cast<int>(std::lround((take + 2) * k));
    const int py1 = std::min(static_cast<int>(painted.height), static_cast<int>(std::lround((take + 1 + h + hz) * k / 2)));
    ImageData out;
    out.width = static_cast<uint32_t>(std::max(1, px1 - px0)), out.height = static_cast<uint32_t>(std::max(1, py1));
    out.rgba.assign(static_cast<size_t>(out.width) * out.height * 4, 0);
    const double e = 0.03;
    for (int py = 0; py < py1; ++py) {
        for (int px = std::max(0, px0); px < std::min(static_cast<int>(painted.width), px1); ++px) {
            const uint8_t* src = &painted.rgba[(static_cast<size_t>(py) * painted.width + px) * 4];
            if (src[3] == 0) continue;
            const double u = -h + (px + 0.5) / k, v = -hz + (py + 0.5) / (k / 2);
            double x = u + h; // the front face's, and for what lies outside the box (paint past its edge)
            const double tx = (u + v + hz) / 2, ty = (v + hz - u) / 2;
            const double fz = u + 2 * h - v, ey = w - u, ez = w + ey - v;
            if (tx >= -e && tx <= w + e && ty >= -e && ty <= h + e) x = tx;
            else if (x >= -e && x <= w + e && fz >= -e && fz <= hz + e) {}
            else if (ey >= -e && ey <= h + e && ez >= -e && ez <= hz + e) x = w;
            if (x < take - e || x > take + 1 + e) continue;
            std::copy_n(src, 4, &out.rgba[(static_cast<size_t>(py) * out.width + (px - px0)) * 4]);
        }
    }
    return out;
}

} // namespace

json MatchedArtSheets(const json& solved, const json& assetPlan, const Ontology* ontology, const fs::path& dir, int perSheet, int styleCount) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    // Each need, with an object that shows it (its footprint and look), and how much of the place it is.
    struct Need {
        json row, object;
        int uses = 0;
    };
    std::vector<Need> needs;
    // The ground under a thing in the place (its footprint's commonest terrain): what its lot is made of.
    const json terrainRows = solved.contains("terrain") ? solved["terrain"].value("rows", json::array()) : json::array();
    const json terrainNames = solved.contains("terrain") ? solved["terrain"].value("palette", json::array()) : json::array();
    auto groundUnder = [&](const json& o) -> std::string {
        std::map<std::string, int> count;
        const int ox = o.value("x", 0), oy = o.value("y", 0), ow = std::max(1, o.value("w", 1)), oh = std::max(1, o.value("h", 1));
        for (int y = oy; y < oy + oh && y < static_cast<int>(terrainRows.size()); ++y) {
            int x = 0;
            for (const auto& run : terrainRows[y]) {
                const int n = run[0].get<int>(), index = run[1].get<int>();
                for (int k = 0; k < n; ++k, ++x) {
                    if (x >= ox && x < ox + ow && index >= 0 && index < static_cast<int>(terrainNames.size())) ++count[terrainNames[index].get<std::string>()];
                }
            }
        }
        const auto best = std::max_element(count.begin(), count.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        return best == count.end() ? std::string() : best->first;
    };
    {
        std::map<std::string, json> objectOf;
        for (const auto& o : solved.value("objects", json::array())) {
            const std::string key = assetPlan.value("byObject", json::object()).value(o.value("id", ""), "");
            if (!key.empty() && !objectOf.contains(key)) objectOf[key] = o;
        }
        for (const auto& n : assetPlan.value("needs", json::array())) {
            const std::string layer = n.value("layer", "");
            // Every need, "dropped" ones too: the plan drops an optional thing it has no art for, and this makes its art.
            if (layer != "building" && layer != "prop" && layer != "decal") continue;
            if (n.value("source", "") == "painted") continue; // in the place's painting
            const auto it = objectOf.find(n.value("key", ""));
            if (it == objectOf.end()) continue;
            json object = it->second;
            // A flat thing (a door in its doorway, a key, debris) drawn on its sheet as a small standing box: a flat
            // dot would be painted as a stain.
            if (layer == "decal") object["layer"] = "prop";
            needs.push_back({n, object, n.value("uses", 1)});
        }
        // The build type's parts (a dungeon's wall block, its wall torch): no object of the layout shows them, so
        // each gets a box of its own, a cell across; a wall block a few storeys high.
        const json parts = assetPlan.value("parts", json::object());
        for (const auto& [partKey, part] : parts.items()) {
            const bool wall = part.value("at", "") == "wall-edge";
            const std::string role = part.value("role", partKey);
            json row = {{"key", "part|" + partKey}, {"role", role}, {"layer", wall ? "building" : "prop"}, {"concepts", json::array({partKey})},
                        {"uses", 1}, {"part", partKey}};
            json object = {{"id", "part/" + partKey}, {"slot", role}, {"layer", wall ? "building" : "prop"}, {"x", 0}, {"y", 0}, {"w", 1}, {"h", 1},
                           {"facing", "s"}};
            if (wall) object["floors"] = 3;
            needs.push_back({row, object, 1});
        }
    }
    // What works together (made on its own sheet, told to match), from the ontology alone: a thing that fills a
    // doorway, or hangs on a wall, with the place's wall part; a thing with what its role depends on (a door on its
    // wall). Not things that merely stand near each other (a car by every building).
    std::map<std::string, std::set<std::string>> with;
    auto isTrue = [&](const std::string& role, const char* key) {
        const json v = ontology ? ontology->RoleLook(role, key) : json();
        return v.is_boolean() && v.get<bool>();
    };
    auto under = [&](const std::string& role, const std::string& root) {
        for (const Role* r = ontology ? ontology->FindRole(role) : nullptr; r; r = r->broader.empty() ? nullptr : ontology->FindRole(r->broader)) {
            if (r->id == root) return true;
        }
        return false;
    };
    // A need's relations: its objects' (the plan's arrangement), else its role's defaults.
    auto relationsOf = [&](const Need& n) {
        const json own = n.object.value("relations", json());
        if (own.is_array()) return own;
        const json def = ontology ? ontology->RoleLook(n.row.value("role", ""), "relations") : json();
        return def.is_array() ? def : json::array();
    };
    std::string wallKey;
    {
        for (const auto& n : needs) {
            if (n.row.contains("part") && n.object.value("layer", "") == "building") wallKey = n.row.value("key", "");
        }
        for (auto& n : needs) {
            const std::string key = n.row.value("key", ""), role = n.row.value("role", "");
            if (wallKey.empty() || key == wallKey || ontology == nullptr) continue;
            const bool doorway = isTrue(role, "doorway");
            if (doorway || n.row.contains("part")) {
                with[key].insert(wallKey), with[wallKey].insert(key);
                if (doorway) n.object["floors"] = 3; // as tall as the wall it stands in
            }
        }
        // Its relations (docs/dungeon.md §0: the plan's, else its role's): a thing set in, mounted on, against or
        // leading out of a wall goes with the wall (the place's architecture); part-of and used-with go with what
        // they name. Each relation's "art" says it.
        for (auto& n : needs) {
            const std::string key = n.row.value("key", ""), role = n.row.value("role", "");
            // Whose colours its art takes (a relation whose art is "colour": a wall coloured by the background), the
            // plan's or its role's: said when it's painted, linking it to nothing.
            json relations = relationsOf(n);
            if (ontology) {
                for (const auto& r : ontology->RoleLook(role, "relations").is_array() ? ontology->RoleLook(role, "relations") : json::array()) relations.push_back(r);
            }
            for (const auto& r : relations) {
                const RelationType* type = ontology ? ontology->Relation(r.value("type", "")) : nullptr;
                if (type && type->use.is_object() && type->use.value("art", "") == "colour") n.row["colourFrom"] = r.value("to", "");
            }
            for (const auto& r : relations) { // the plan's and its role's
                const RelationType* type = ontology ? ontology->Relation(r.value("type", "")) : nullptr;
                const std::string art = type && type->use.is_object() ? type->use.value("art", "") : "";
                if (art.empty() || art == "colour") continue;
                const std::string to = r.value("to", "");
                for (const auto& m : needs) {
                    const std::string mk = m.row.value("key", "");
                    if (mk == key) continue;
                    const bool isWall = mk == wallKey && (to == "wall" || under(m.row.value("role", ""), to) || under(to, "wall-block"));
                    if (isWall || (!to.empty() && under(m.row.value("role", ""), to))) {
                        with[key].insert(mk), with[mk].insert(key);
                        // Made with it (made-with: stairs with the walls): its picture shown when this is painted.
                        if (art == "with") n.row["madeWith"].push_back(mk);
                    }
                }
                if (art == "on-run" && !wallKey.empty() && key != wallKey) n.row["onRun"] = true;
                if (art == "in-run" && !wallKey.empty() && key != wallKey) n.row["inRun"] = true;
            }
        }
        for (const auto& a : needs) {
            const json depends = ontology ? ontology->RoleLook(a.row.value("role", ""), "dependsOn") : json();
            if (!depends.is_array()) continue;
            for (const auto& b : needs) {
                const std::string ak = a.row.value("key", ""), bk = b.row.value("key", "");
                if (ak == bk) continue;
                for (const auto& d : depends) {
                    if (d.is_string() && under(b.row.value("role", ""), d.get<std::string>())) with[ak].insert(bk), with[bk].insert(ak);
                }
            }
        }
    }
    // A run (its role's look "run": a wall, a fence) is made as one piece of a continuous run: a box three cells long,
    // painted as one wall; its middle cell cut (flat on both sides: a cell inside a run) and its last (its end painted:
    // where a run ends). A doorway thing in that wall is painted in the middle of such a run and its middle cell cut:
    // it fits the wall by being part of it.
    for (auto& n : needs) {
        const std::string key = n.row.value("key", ""), role = n.row.value("role", "");
        const bool run = n.row.contains("part") && n.object.value("layer", "") == "building" && isTrue(role, "run");
        const bool inWall = !wallKey.empty() && key != wallKey && (isTrue(role, "doorway") || n.row.value("inRun", false)) && with[key].contains(wallKey);
        const bool onWall = !wallKey.empty() && key != wallKey && !inWall && n.row.value("onRun", false);
        if (!run && !inWall && !onWall) continue;
        n.object["w"] = 3, n.object["h"] = 1, n.object["floors"] = 3, n.object["layer"] = "building";
        n.row["kit"] = {{"cells", 3}, {"take", 1}, {"run", run}};
        if (onWall) n.row["kit"]["on"] = true;
    }
    if (!wallKey.empty()) {
        for (auto& n : needs) {
            if (n.row.contains("kit") && n.row.value("key", "") != wallKey) {
                const auto w = std::find_if(needs.begin(), needs.end(), [&](const Need& m) { return m.row.value("key", "") == wallKey; });
                if (w != needs.end() && w->row.contains("kit")) {
                    n.row["kit"]["wall"] = w->row.value("role", "");
                    n.row["kit"]["plain"] = Slug(wallKey) + "-run"; // the same wall painted plain: where the thing is, by difference
                }
            }
        }
    }
    // One edit for kinds of one family set in or on the wall the same way (a locked door is a door; a light the plan put
    // on the wall is that wall's torch): the kind whose role itself sets it in or on the wall is made, else the broader;
    // the other takes its art, "sameAs" (docs/places/ontology-in-use.md).
    auto setsItself = [&](const std::string& role, const std::string& art) {
        const json relations = ontology ? ontology->RoleLook(role, "relations") : json();
        for (const auto& r : relations.is_array() ? relations : json::array()) {
            const RelationType* type = ontology->Relation(r.value("type", ""));
            if (type && type->use.is_object() && type->use.value("art", "") == art) return true;
        }
        return false;
    };
    auto onTheWall = [](const Need& n) { return n.row.contains("kit") && !n.row["kit"].value("run", false); };
    for (auto& a : needs) {
        if (!onTheWall(a)) continue;
        const bool on = a.row["kit"].value("on", false);
        const std::string art = on ? "on-run" : "in-run", ra = a.row.value("role", "");
        for (const auto& b : needs) {
            if (&a == &b || !onTheWall(b) || b.row["kit"].value("on", false) != on || b.row["kit"].contains("sameAs")) continue;
            const std::string rb = b.row.value("role", "");
            if (!under(ra, rb) && !under(rb, ra)) continue;
            const bool sa = setsItself(ra, art), sb = setsItself(rb, art);
            if ((sb && !sa) || (sa == sb && under(ra, rb))) {
                a.row["kit"]["sameAs"] = b.row.value("key", "");
                break;
            }
        }
    }
    // The style sheet first: the biggest kinds of building and the most used things, half and half.
    std::stable_sort(needs.begin(), needs.end(), [](const Need& a, const Need& b) {
        const bool ab = a.row.value("layer", "") == "building", bb = b.row.value("layer", "") == "building";
        const int area = a.object.value("w", 1) * a.object.value("h", 1), barea = b.object.value("w", 1) * b.object.value("h", 1);
        return ab != bb ? ab : ab ? area * a.uses > barea * b.uses : a.uses > b.uses;
    });
    std::vector<Need> style, rest;
    {
        int buildings = 0, things = 0;
        for (const auto& n : needs) {
            const bool building = n.row.value("layer", "") == "building";
            if (static_cast<int>(style.size()) < styleCount && (building ? buildings < (styleCount + 1) / 2 : things < styleCount / 2)) {
                (building ? buildings : things)++;
                style.push_back(n);
            } else {
                rest.push_back(n);
            }
        }
    }
    // A sheet with buildings holds fewer (they're wide): every sheet near 4:1 at most, so the provider's few
    // megapixels give each piece room.
    std::vector<std::pair<std::string, std::vector<Need>>> groups = {{"style", style}};
    for (size_t i = 0; i < rest.size();) {
        // Buildings a few to a sheet (they're wide); small things at most 4 (each with room).
        const size_t take = rest[i].row.value("layer", "") == "building" ? std::max(1, perSheet - 2) : std::min(perSheet, 4);
        groups.push_back({"things-" + std::to_string(groups.size()), std::vector<Need>(rest.begin() + i, rest.begin() + std::min(rest.size(), i + take))});
        i += take;
    }

    json out = {{"sheets", json::array()}};
    const int kTile = 96; // pixels a cell is across in a sheet: art big enough for a close camera
    for (const auto& [name, group] : groups) {
        if (group.empty()) continue;
        // Its boxes in a row across the picture (along x - y, at one x + y): side by side, none in front of
        // another, each as far from the last as its own footprint needs, a gap between.
        const int margin = 3, count = static_cast<int>(group.size());
        std::vector<int> along(count, 0);
        int run = 0;
        for (int i = 0; i < count; ++i) {
            along[i] = run;
            run += std::max(group[i].object.value("w", 1), group[i].object.value("h", 1)) + 2;
        }
        const int side = 2 * margin + run;
        json objects = json::array();
        for (int i = 0; i < count; ++i) {
            json o = group[i].object;
            const int x = margin + along[i], y = margin + run - along[i] - std::max(o.value("w", 1), o.value("h", 1));
            const double dx = x - o.value("x", 0), dy = y - o.value("y", 0);
            if (o.contains("outline") && o["outline"].is_array()) {
                for (size_t k = 0; k + 1 < o["outline"].size(); k += 2) o["outline"][k] = o["outline"][k].get<double>() + dx, o["outline"][k + 1] = o["outline"][k + 1].get<double>() + dy;
            }
            o["x"] = x, o["y"] = y, o["id"] = name + "/" + std::to_string(i);
            o.erase("cells");
            // A building's placeholder reads as a building: at least two storeys (a one-storey role's box is a flat
            // slab, which a painter takes for a base or a plaza and paints a building beside it).
            if (o.value("layer", "") == "building" && ontology != nullptr) {
                if (const Role* role = ontology->FindRole(o.value("slot", "")); role && role->look.is_object()) {
                    const json f = role->look.value("floors", json());
                    if (f.is_array() && f.size() == 2 && f[1].get<int>() < 2) o["floors"] = 2;
                }
            }
            objects.push_back(o);
        }
        const json layout = SheetLayout(side, [](int, int) { return std::string("sheet"); }, objects);
        json legend;
        ImageData ids;
        const ImageData image = RenderPlanBlockout(layout, 0, 0, side, side, side * kTile, ontology, &legend, &ids);
        // On the key colour, cropped to the boxes.
        const auto crop = Crop(ids, kTile, [&](int x, int y) { return IdAt(ids, x, y) != 0; });
        ImageData sheet = CropImage(image, crop), sheetIds = CropImage(ids, crop);
        for (size_t i = 0; i < sheet.rgba.size(); i += 4) {
            if ((sheetIds.rgba[i] | (sheetIds.rgba[i + 1] << 8) | (sheetIds.rgba[i + 2] << 16)) == 0) {
                sheet.rgba[i] = kMatchedKey[0], sheet.rgba[i + 1] = kMatchedKey[1], sheet.rgba[i + 2] = kMatchedKey[2];
            } else if (IsKey(&sheet.rgba[i])) {
                // A thing drawn in the key colour (the block-out's palette has magenta) would vanish into the
                // background: grey, as shaded.
                const uint8_t shade = static_cast<uint8_t>((sheet.rgba[i] + sheet.rgba[i + 2]) / 4 + 40);
                sheet.rgba[i] = sheet.rgba[i + 1] = shade, sheet.rgba[i + 2] = static_cast<uint8_t>(shade + 8);
            }
        }
        WritePng(dir / (name + ".png"), sheet);
        WritePng(dir / (name + "-ids.png"), sheetIds);
        View view;
        view.x1 = view.y1 = side;
        view.tw = legend["view"].value("tw", 32.0), view.th = legend["view"].value("th", 16.0), view.top = legend["view"].value("top", 48.0);
        auto at = [&](double wx, double wy) {
            const auto p = view.P(wx, wy);
            return json::array({p[0] - crop[0], p[1] - crop[1]});
        };
        json pieces = json::array();
        for (int i = 0; i < count; ++i) {
            const json& o = objects[i];
            const int x = o.value("x", 0), y = o.value("y", 0), w = std::max(1, o.value("w", 1)), h = std::max(1, o.value("h", 1));
            const json& row = group[i].row;
            const std::string role = row.value("role", "");
            std::string words = ontology ? ontology->RoleDraw(role) : std::string();
            if (words.empty()) words = role;
            int bx0 = sheetIds.width, by0 = sheetIds.height, bx1 = 0, by1 = 0;
            for (int py = 0; py < static_cast<int>(sheetIds.height); ++py) {
                for (int px = 0; px < static_cast<int>(sheetIds.width); ++px) {
                    if (IdAt(sheetIds, px, py) != i + 1) continue;
                    bx0 = std::min(bx0, px), by0 = std::min(by0, py), bx1 = std::max(bx1, px + 1), by1 = std::max(by1, py + 1);
                }
            }
            // A kit piece (a run, a door in its wall) has a slug of its own: a piece made before as a lone block is
            // a different picture.
            const std::string slug = Slug(row.value("key", "")) +
                                     (row.contains("kit") ? (row["kit"].value("run", false) ? "-run" : row["kit"].value("on", false) ? "-onwall" : "-inwall") : "");
            json piece = {{"key", row.value("key", "")}, {"role", role}, {"layer", row.value("layer", "")}, {"words", words},
                          {"concepts", row.value("concepts", json::array())}, {"w", w}, {"h", h}, {"id", i + 1},
                          {"slug", slug}, {"box", {bx0, by0, bx1, by1}}, {"left", at(x, y + h)}, {"right", at(x + w, y)},
                          {"front", at(x + w, y + h)}, {"stand", at(x + w - 0.5, y + h - 0.5)}, {"ground", groundUnder(group[i].object)},
                          {"variantKey", row.value("key", "")}, {"variant", 1}};
            if (row.contains("part")) piece["part"] = row["part"];
            if (row.contains("kit")) piece["kit"] = row["kit"];
            if (row.contains("colourFrom")) piece["colourFrom"] = row["colourFrom"];
            if (row.contains("madeWith")) piece["madeWith"] = row["madeWith"];
            // What it's for in this place (the plan's purpose), for what's painted.
            if (const json purpose = group[i].object.value("purpose", json()); purpose.is_string()) piece["purpose"] = purpose;
            if (const auto w = with.find(row.value("key", "")); w != with.end()) piece["with"] = json(std::vector<std::string>(w->second.begin(), w->second.end()));
            // How it turns with its place (its role's look "orient"): a thing with a front is made seen from the front
            // and from behind; the scene shows the one its facing needs, mirrored for the other two ways.
            const json orient = ontology ? ontology->RoleLook(role, "orient") : json();
            const bool views = orient.is_string() && orient != "none" && row.value("layer", "") != "building" && !row.contains("kit");
            if (views) piece["orient"] = orient, piece["view"] = "front";
            // A building one can go into (its role's look has_interior, Phase 27): its interior made with it.
            if (row.value("layer", "") == "building" && ontology && ontology->RoleLook(role, "has_interior") == true) piece["interior"] = true;
            // Several looks for a kind the place uses often, by how much real ones differ (its role's look
            // "variety"): many different cars, a few shops, one street light.
            int variants = 1;
            if (!row.contains("kit") && !row.contains("part")) {
                const json variety = ontology ? ontology->RoleLook(role, "variety") : json();
                const std::string v = variety.is_string() ? variety.get<std::string>() : "medium";
                const double root = std::sqrt(static_cast<double>(std::max(1, group[i].uses)));
                const bool building = row.value("layer", "") == "building";
                if (v == "high") variants = std::min(building ? 4 : 6, static_cast<int>(std::ceil(root / 1.5)));
                else if (v == "medium") variants = std::min(3, static_cast<int>(std::ceil(root / 3.0)));
                // The build's asset variety (its start page): one look each, or twice as many (a role that never varies stays one).
                const std::string wanted = assetPlan.value("variety", "");
                if (wanted == "low") variants = 1;
                else if (wanted == "high" && v != "low" && v != "none") variants = std::min(building ? 6 : 8, std::max(2, variants * 2));
            }
            for (int k = 1; k <= variants; ++k) {
                json p = piece;
                if (k > 1) p["key"] = row.value("key", "") + "#v" + std::to_string(k), p["slug"] = slug + "-v" + std::to_string(k), p["variant"] = k;
                pieces.push_back(p);
                if (views) {
                    json back = p;
                    back["key"] = p.value("key", "") + "#back", back["slug"] = p.value("slug", "") + "-back", back["view"] = "back";
                    pieces.push_back(back);
                }
            }
        }
        out["sheets"].push_back({{"name", name}, {"kind", name == "style" ? "style" : "things"}, {"png", name + ".png"}, {"ids", name + "-ids.png"},
                                 {"size", {sheet.width, sheet.height}}, {"tw", view.tw}, {"th", view.th}, {"legend", legend}, {"pieces", pieces}});
    }

    // No ground sheet: the ground is the place's background, painted whole (docs/places/background.md).
    // The ground plan: the layout's terrain alone (no things, no fences), for the provider to paint as the scene's
    // ground in one picture. Nothing stands in front of anything on the ground, so a painting of it can be used whole.
    if (solved.contains("terrain") && solved.contains("grid")) {
        json bare = solved;
        bare["objects"] = json::array();
        bare["lines"] = json::array();
        const json b = solved["grid"].value("bounds", json::array({0, 0, 0, 0}));
        const int W = b[2].get<int>(), H = b[3].get<int>();
        json legend;
        const ImageData plan = RenderPlanBlockout(bare, 0, 0, W, H, std::min(6000, std::max(W, H) * 96), ontology, &legend);
        WritePng(dir / "background-blockout.png", plan);
        out["sheets"].push_back({{"name", "background-blockout"}, {"kind", "plan"}, {"png", "background-blockout.png"}, {"size", {plan.width, plan.height}},
                                 {"cells", {W, H}}, {"view", legend.value("view", json::object())}, {"legend", legend}});
    }
    std::ofstream(dir / "sheets.json", std::ios::binary) << out.dump(2);
    return out;
}

json CutMatchedArt(AssetDatabase& assets, const Project& project, const json& sheets, const fs::path& dir, const Ontology* ontology, const std::string& place) {
    json report = {{"pieces", json::array()}, {"grounds", json::array()}, {"skipped", json::array()}};
    const std::string folder = "assets/matched/" + Slug(place);
    std::error_code ec;
    fs::create_directories(project.root / folder, ec);
    struct Made {
        json piece;
        std::string sheet;
        ImageData image;
        double cells = 1;
        bool style = false;
        std::optional<ImageData> interior; // its interior (Phase 27), cut and framed exactly as `image`
    };
    std::vector<Made> made;
    for (const auto& s : sheets.value("sheets", json::array())) {
        const std::string name = s.value("name", ""), kind = s.value("kind", "");
        const auto painting = DecodeImage(dir / (name + "-painted.png"));
        if (kind == "plan") {
            if (!painting || painting->width == 0) continue;
            View view;
            const json v = s.value("view", json::object());
            view.x0 = v.value("x0", 0.0), view.y0 = v.value("y0", 0.0), view.x1 = v.value("x1", 0.0), view.y1 = v.value("y1", 0.0);
            view.tw = v.value("tw", 32.0), view.th = v.value("th", 16.0), view.top = v.value("top", 48.0);
            const double sx = static_cast<double>(painting->width) / s["size"][0].get<double>(), sy = static_cast<double>(painting->height) / s["size"][1].get<double>();
            const int W = s["cells"][0].get<int>(), H = s["cells"][1].get<int>();
            // As sharp as the painting is: a cell's diamond is tw painting pixels across (by its scale), its square ~0.75 of that.
            const int cellPx = std::clamp(static_cast<int>(std::lround(view.tw * sx * 0.75)), 32, 256);
            // The painting laid exactly on the plan: its drift measured where the plan's grounds meet (their edges on the
            // painting's), so each cell of the picture is that cell of the layout, and the grid and the ground agree.
            Align align;
            if (const auto block = DecodeImage(dir / s.value("png", "")); block && block->width > 0) {
                const int bw = static_cast<int>(block->width), bh = static_cast<int>(block->height);
                auto differs = [&](int x0, int y0, int x1, int y1) {
                    const uint8_t* a = &block->rgba[(static_cast<size_t>(y0) * bw + x0) * 4];
                    const uint8_t* b = &block->rgba[(static_cast<size_t>(y1) * bw + x1) * 4];
                    return std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]) > 40;
                };
                std::vector<std::array<int, 2>> outline;
                for (int y = 0; y + 1 < bh; y += 2) {
                    for (int x = 0; x + 1 < bw; x += 2) {
                        if (differs(x, y, x + 1, y) || differs(x, y, x, y + 1)) outline.push_back({x, y});
                    }
                }
                align = GlobalAlign(EdgesOf(*painting, bw, bh), outline, bw, bh);
                report["groundAlign"] = {{"dx", align.dx}, {"dy", align.dy}, {"scale", align.scale}};
            }
            ImageData top;
            top.width = static_cast<uint32_t>(W * cellPx), top.height = static_cast<uint32_t>(H * cellPx);
            top.rgba.assign(static_cast<size_t>(top.width) * top.height * 4, 255);
            for (uint32_t py = 0; py < top.height; ++py) {
                for (uint32_t px = 0; px < top.width; ++px) {
                    const auto p = view.P((px + 0.5) / cellPx, (py + 0.5) / cellPx);
                    const auto q = align.Map(p[0], p[1]);
                    const auto c = Sample(*painting, q[0] * sx, q[1] * sy);
                    uint8_t* d = &top.rgba[(static_cast<size_t>(py) * top.width + px) * 4];
                    for (int k = 0; k < 3; ++k) d[k] = static_cast<uint8_t>(std::clamp(c[k], 0.0f, 255.0f));
                }
            }
            const std::string path = folder + "/ground-plan.png";
            WritePng(project.root / path, top);
            report["groundPicture"] = {{"path", path}, {"cellPx", cellPx}, {"cells", {W, H}}};
            continue;
        }
        if (kind == "ground") {
            for (const auto& g : s.value("grounds", json::array())) {
                ImageData tex;
                if (painting && painting->width > 0) {
                    // The middle cell of its patch, unwarped (the painting's scale to the sheet's).
                    const double sx = static_cast<double>(painting->width) / s["size"][0].get<double>(), sy = static_cast<double>(painting->height) / s["size"][1].get<double>();
                    const json c = g["corners"]; // top, right, bottom, left of the cell
                    auto lerp = [&](double u, double v) {
                        // Bilinear over the cell's four corners, its middle 80% (away from its neighbours).
                        u = 0.1 + 0.8 * u, v = 0.1 + 0.8 * v;
                        const double x = (1 - u) * (1 - v) * c[0][0].get<double>() + u * (1 - v) * c[1][0].get<double>() + u * v * c[2][0].get<double>() + (1 - u) * v * c[3][0].get<double>();
                        const double y = (1 - u) * (1 - v) * c[0][1].get<double>() + u * (1 - v) * c[1][1].get<double>() + u * v * c[2][1].get<double>() + (1 - u) * v * c[3][1].get<double>();
                        return Sample(*painting, x * sx, y * sy);
                    };
                    tex = Seamless(lerp);
                } else if (const auto own = DecodeImage(dir / "pieces" / (g.value("slug", "") + ".png")); own && own->width > 0) {
                    // A provider's own tile: made seamless the same way.
                    tex = Seamless([&](double u, double v) { return Sample(*own, u * own->width, v * own->height); });
                } else {
                    report["skipped"].push_back({{"key", "terrain|" + g.value("terrain", "")}, {"why", "no ground painted for it"}});
                    continue;
                }
                const std::string path = folder + "/" + g.value("slug", "ground") + ".png";
                WritePng(project.root / path, tex);
                report["grounds"].push_back({{"terrain", g.value("terrain", "")}, {"path", path}});
            }
            continue;
        }
        const auto ids = DecodeImage(dir / s.value("ids", ""));
        Align align;
        double sx = 1, sy = 1;
        if (painting && ids) {
            sx = static_cast<double>(painting->width) / ids->width, sy = static_cast<double>(painting->height) / ids->height;
            std::vector<std::array<int, 2>> outline;
            for (int y = 0; y < static_cast<int>(ids->height); y += 1) {
                for (int x = (y % 2); x < static_cast<int>(ids->width); x += 2) {
                    const int id = IdAt(*ids, x, y);
                    if (id != IdAt(*ids, x + 1, y) || id != IdAt(*ids, x, y + 1)) outline.push_back({x, y});
                }
            }
            align = GlobalAlign(EdgesOf(*painting, ids->width, ids->height), outline, ids->width, ids->height);
        }
        for (const auto& p : s.value("pieces", json::array())) {
            const bool building = p.value("layer", "") == "building";
            const double tw = s.value("tw", 32.0);
            ImageData cut;
            double cells = 1;
            if (painting && ids) {
                // Its column of the painting (between its footprint's left and right corners, down to its
                // front edges, with room for drift), the key colour keyed out.
                const json L = p["left"], R = p["right"], F = p["front"], S = p["stand"];
                const double lx = L[0], ly = L[1], rx = R[0], ry = R[1], fx = F[0], fy = F[1];
                const double slackX = std::max(4.0, 0.12 * (rx - lx)), slackY = 0.3 * s.value("th", 16.0);
                const auto standQ = align.Map(S[0].get<double>(), S[1].get<double>());
                const double standX = standQ[0] * sx, standY = standQ[1] * sy;
                const int W = static_cast<int>(painting->width), H = static_cast<int>(painting->height);
                std::vector<uint8_t> keep(static_cast<size_t>(W) * H, 0);
                int mx0 = W, my0 = H, mx1 = -1, my1 = -1;
                const auto a = align.Map(lx - slackX, 0), z = align.Map(rx + slackX, std::max(ly, ry) + slackY);
                for (int y = 0; y < std::min(H, static_cast<int>(z[1] * sy) + 1); ++y) {
                    for (int x = std::max(0, static_cast<int>(a[0] * sx)); x < std::min(W, static_cast<int>(z[0] * sx) + 1); ++x) {
                        const double bx = ((x + 0.5) / sx - align.cx - align.dx) / align.scale + align.cx;
                        const double by = ((y + 0.5) / sy - align.cy - align.dy) / align.scale + align.cy;
                        const double floor = bx <= fx ? ly + (std::max(bx, lx) - lx) * (fy - ly) / std::max(1e-6, fx - lx)
                                                      : fy + (std::min(bx, rx) - fx) * (ry - fy) / std::max(1e-6, rx - fx);
                        if (bx < lx - slackX || bx > rx + slackX || by > floor + slackY) continue;
                        if (IsKey(&painting->rgba[(static_cast<size_t>(y) * W + x) * 4])) continue;
                        keep[static_cast<size_t>(y) * W + x] = 1;
                        mx0 = std::min(mx0, x), my0 = std::min(my0, y), mx1 = std::max(mx1, x + 1), my1 = std::max(my1, y + 1);
                    }
                }
                if (mx1 < 0) {
                    report["skipped"].push_back({{"key", p.value("key", "")}, {"why", "nothing painted in its box"}});
                    continue;
                }
                // Framed where the builder stands it: centred on its front cell's middle; a building down to
                // there, a thing down to its foot.
                const double half = std::max(standX - mx0, mx1 - standX) + 2;
                const int x0 = static_cast<int>(std::floor(standX - half)), x1 = static_cast<int>(std::ceil(standX + half));
                const int y0 = my0 - 2, y1 = building ? static_cast<int>(std::lround(standY)) : my1 + 1;
                cut.width = static_cast<uint32_t>(std::max(1, x1 - x0)), cut.height = static_cast<uint32_t>(std::max(1, y1 - y0));
                cut.rgba.assign(static_cast<size_t>(cut.width) * cut.height * 4, 0);
                for (uint32_t j = 0; j < cut.height; ++j) {
                    for (uint32_t i = 0; i < cut.width; ++i) {
                        const int x = x0 + static_cast<int>(i), y = y0 + static_cast<int>(j);
                        if (x < 0 || y < 0 || x >= W || y >= H || !keep[static_cast<size_t>(y) * W + x]) continue;
                        std::copy_n(&painting->rgba[(static_cast<size_t>(y) * W + x) * 4], 3, &cut.rgba[(static_cast<size_t>(j) * cut.width + i) * 4]);
                        cut.rgba[(static_cast<size_t>(j) * cut.width + i) * 4 + 3] = 255;
                    }
                }
                cells = cut.width / (tw * sx * align.scale);
            } else if (const json onGrid = ReadJsonFile(dir / "pieces" / (p.value("slug", "") + ".json")); onGrid.contains("cellPx")) {
                // Already set on the grid (a wall block and what's set in it or on it: matched.py make_blocks): one cell
                // `cellPx` across by its foot, its front corner at its middle and bottom. Used as it is.
                auto own = DecodeImage(dir / "pieces" / (p.value("slug", "") + ".png"));
                if (!own || own->width == 0) {
                    report["skipped"].push_back({{"key", p.value("key", "")}, {"why", "its block is missing"}});
                    continue;
                }
                cut = std::move(*own);
                cells = static_cast<double>(cut.width) / std::max(1, onGrid.value("cellPx", 256));
                made.push_back({p, name, std::move(cut), cells, s.value("kind", "") == "style"});
                continue;
            } else if (auto own = DecodeImage(dir / "pieces" / (p.value("cutFrom", p.value("slug", "")) + ".png")); own && own->width > 0) {
                // One piece made alone (PixelLab, Mistral): clear, or on the key colour; as wide on the grid as
                // its footprint (a building) or its role says (a thing).
                for (size_t i = 0; i < own->rgba.size(); i += 4) {
                    if (IsKey(&own->rgba[i])) own->rgba[i + 3] = 0;
                }
                Clean(*own);
                const auto box = OpaqueBox(*own);
                if (!box) {
                    report["skipped"].push_back({{"key", p.value("key", "")}, {"why", "its image is empty"}});
                    continue;
                }
                cut = CropImage(*own, *box);
                // Its interior (Phase 27): painted on the exterior's own canvas (matched.py make_interiors), so the same
                // crop, and below the same framing, line it up with the exterior exactly.
                std::optional<ImageData> inside;
                if (auto raw = DecodeImage(dir / "pieces" / (p.value("slug", "") + "-interior.png")); raw && raw->width == own->width && raw->height == own->height) {
                    for (size_t i = 0; i < raw->rgba.size(); i += 4) {
                        if (IsKey(&raw->rgba[i])) raw->rgba[i + 3] = 0;
                    }
                    Clean(*raw);
                    inside = CropImage(*raw, *box);
                }
                const double size = ontology ? ontology->RoleSize(p.value("role", "")) : 0.0;
                cells = building ? (p.value("w", 1) + p.value("h", 1)) / 2.0 : std::clamp(size > 0 ? size : 0.8, 0.2, 3.0);
                // A thing as tall as its role says too (its relative size, docs/places/ontology-in-use.md): painted
                // taller for its width than that (a bush drawn as a tall shrub), it's made narrower until its height
                // is its role's, so it stands beside the rest at the size they agree on.
                const double tall = ontology && !building ? ontology->RoleTall(p.value("role", "")) : 0.0;
                if (tall > 0 && cut.width > 0 && p.value("layer", "") != "decal") {
                    const double drawn = static_cast<double>(cut.height) / cut.width;
                    if (drawn > tall * 1.15) cells = std::max(0.15, cells * tall / drawn);
                }
                const int w = std::max(1, p.value("w", 1)), h = std::max(1, p.value("h", 1));
                if (p.contains("kit")) {
                    // One cell of a painted run (KitCell), the run first set to the grid's angle (Level): the run's
                    // middle or last cell, or where the thing painted in it stands (FindInRun, against the plain run).
                    cut = FitFootprint(Level(cut, w, h), w, h);
                    double take = p["kit"].value("take", 1);
                    if (!p["kit"].value("run", false) && p["kit"].contains("plain")) {
                        if (auto plain = DecodeImage(dir / "pieces" / (p["kit"].value("plain", "") + ".png")); plain && plain->width > 0) {
                            for (size_t i = 0; i < plain->rgba.size(); i += 4) {
                                if (IsKey(&plain->rgba[i])) plain->rgba[i + 3] = 0;
                            }
                            Clean(*plain);
                            if (const auto pb = OpaqueBox(*plain)) take = FindInRun(cut, FitFootprint(Level(CropImage(*plain, *pb), w, h), w, h), w, h, take);
                        }
                    }
                    cut = KitCell(cut, w, h, take);
                    cells = 1;
                } else if (building) {
                    // Its painted footprint fitted to the plan's (FitFootprint: a building painted deeper than its box
                    // would stand over its street and past the map's edge), then stood where the scene stands it:
                    // centred on its footprint's front cell (w / (w + h) of the way across from its left corner), its
                    // foot the footprint's front corner (the scene seats it there).
                    // Kept as painted (a warp bends roofs), scaled whole so it fits its footprint: each side of its
                    // painted foot against the footprint's (w cells on the left of its front corner, h on the right),
                    // the tighter one deciding, its front corner on the footprint's.
                    const auto corners = FootCorners(cut);
                    double stand = cut.width * static_cast<double>(w) / (w + h);
                    double unit = static_cast<double>(cut.width) / (w + h); // picture pixels per half a cell across
                    if (corners) {
                        const auto [xl, xf, xr] = *corners;
                        stand = xf;
                        unit = std::max({static_cast<double>(xf - xl) / w, static_cast<double>(xr - xf) / h, unit * 0.6});
                    }
                    const double half = std::max(stand, cut.width - stand);
                    const int offset = static_cast<int>(std::lround(half - stand));
                    const int height = static_cast<int>(cut.height);
                    ImageData framed;
                    framed.width = static_cast<uint32_t>(std::lround(2 * half)), framed.height = static_cast<uint32_t>(height);
                    framed.rgba.assign(static_cast<size_t>(framed.width) * framed.height * 4, 0);
                    for (int y = 0; y < height; ++y) {
                        for (uint32_t x = 0; x < cut.width && x + offset < framed.width; ++x) {
                            std::copy_n(&cut.rgba[(static_cast<size_t>(y) * cut.width + x) * 4], 4, &framed.rgba[(static_cast<size_t>(y) * framed.width + x + offset) * 4]);
                        }
                    }
                    cells = framed.width / (2.0 * unit);
                    cut = std::move(framed);
                    if (inside) {
                        // The interior framed the same way.
                        ImageData room;
                        room.width = cut.width, room.height = cut.height;
                        room.rgba.assign(static_cast<size_t>(room.width) * room.height * 4, 0);
                        for (int y = 0; y < std::min<int>(height, static_cast<int>(inside->height)); ++y) {
                            for (uint32_t x = 0; x < inside->width && x + offset < room.width; ++x) {
                                std::copy_n(&inside->rgba[(static_cast<size_t>(y) * inside->width + x) * 4], 4, &room.rgba[(static_cast<size_t>(y) * room.width + x + offset) * 4]);
                            }
                        }
                        inside = std::move(room);
                    }
                }
                if (inside && building) {
                    Clean(cut);
                    made.push_back({p, name, std::move(cut), cells, s.value("kind", "") == "style", std::move(inside)});
                    continue;
                }
            } else {
                report["skipped"].push_back({{"key", p.value("key", "")}, {"why", "nothing made for it"}});
                continue;
            }
            Clean(cut);
            made.push_back({p, name, std::move(cut), cells, s.value("kind", "") == "style"});
        }
    }

    // The style sheet's palette; every other piece pulled towards it (softly), and measured against it.
    std::vector<ImageData> anchor;
    for (const auto& m : made) {
        if (m.style) anchor.push_back(m.image);
    }
    const std::vector<std::array<float, 3>> palette = PaletteOf(anchor, 24);
    for (const auto& c : palette) report["palette"].push_back(Number(c[0]) + "," + Number(c[1]) + "," + Number(c[2]));
    std::vector<double> distances;
    std::vector<std::pair<std::string, std::map<std::string, std::string>>> catalog;
    for (auto& m : made) {
        double sum = 0;
        int n = 0;
        for (size_t i = 0; i < m.image.rgba.size(); i += 4) {
            uint8_t* px = &m.image.rgba[i];
            if (px[3] < 128 || palette.empty()) continue;
            size_t which = 0;
            sum += Nearest(palette, px, &which);
            ++n;
            if (!m.style) {
                for (int c = 0; c < 3; ++c) px[c] = static_cast<uint8_t>(px[c] + (palette[which][c] - px[c]) * 0.35);
            }
        }
        const double distance = n ? sum / n : 0;
        distances.push_back(distance);
        const json& p = m.piece;
        const std::string path = folder + "/" + p.value("slug", "piece") + ".png";
        WritePng(project.root / path, m.image);
        const bool building = p.value("layer", "") == "building";
        std::string concepts;
        for (const auto& c : p.value("concepts", json::array())) {
            if (c.is_string()) concepts += (concepts.empty() ? "" : ",") + c.get<std::string>();
        }
        std::map<std::string, std::string> fields = {
            {"roles", p.value("role", "")}, {"name", p.value("role", "")}, {"kind", building ? "building" : "prop"}, {"layer", p.value("layer", "")},
            {"description", p.value("words", "")}, {"tags", concepts}, {"perspective", "isometric"}, {"cells", Number(m.cells)},
            {"blocks", building ? "1" : "0"}, {"source", "generated"}, {"author", "matched art"}, {"own", "1"}, {"matched", "1"}};
        if (p.contains("part")) fields["partOf"] = p.value("part", "");
        if (p.contains("kit")) fields["run"] = p["kit"].value("take", 1) == 2 ? "end" : "1";
        // A wall cell with a thing mounted on it (a torch): the scene puts it where that thing hangs.
        if (p.contains("kit") && p["kit"].value("on", false)) fields["onWallOf"] = p.value("role", "");
        if (p.contains("purpose")) fields["purpose"] = p.value("purpose", "");
        // Its kind's looks (variantKey, variant n) and views: a back view isn't found by its role (never chosen for a
        // thing by itself), only through the front it belongs to (backOf).
        fields["variantKey"] = p.value("variantKey", p.value("key", ""));
        fields["variant"] = std::to_string(p.value("variant", 1));
        if (p.contains("orient")) fields["orient"] = p.value("orient", "");
        if (p.value("view", "") == "back") {
            fields.erase("roles");
            std::string front = p.value("slug", "");
            front = front.substr(0, front.size() - std::string("-back").size());
            fields["backOf"] = folder + "/" + front + ".png";
        }
        // It stands up, as drawn (a door in its arch, a lamp): never laid flat on the ground (the catalog guesses flat
        // for a squarish outline).
        fields["flat"] = "0";
        if (building) fields["footprint"] = std::to_string(p.value("w", 1)) + "x" + std::to_string(p.value("h", 1));
        if (m.interior) {
            const std::string inside = folder + "/" + p.value("slug", "piece") + "-interior.png";
            WritePng(project.root / inside, *m.interior);
            fields["interior"] = inside;
            catalog.push_back({inside, {{"name", p.value("role", "") + " interior"}, {"kind", "building"}, {"layer", "building"},
                                        {"description", "the inside of " + p.value("words", p.value("role", ""))}, {"perspective", "isometric"},
                                        {"cells", Number(m.cells)}, {"flat", "0"}, {"exteriorOf", path}, {"source", "generated"},
                                        {"author", "matched art"}, {"own", "1"}, {"matched", "1"}}});
            report["interiors"] = report.value("interiors", 0) + 1;
        }
        catalog.push_back({path, fields});
        int opaque = 0;
        for (size_t i = 3; i < m.image.rgba.size(); i += 4) opaque += m.image.rgba[i] > 128;
        report["pieces"].push_back({{"key", p.value("key", "")}, {"role", p.value("role", "")}, {"path", path}, {"sheet", m.sheet},
                                    {"cells", m.cells}, {"size", {m.image.width, m.image.height}}, {"palette", distance},
                                    {"fill", static_cast<double>(opaque) / std::max<size_t>(1, m.image.rgba.size() / 4)}, {"style", m.style}});
    }
    // An outlier: its colours far from the style sheet's (twice the typical piece's), or barely there.
    if (!distances.empty()) {
        std::vector<double> sorted = distances;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        const double typical = sorted[sorted.size() / 2];
        for (size_t i = 0; i < report["pieces"].size(); ++i) {
            json& r = report["pieces"][i];
            r["flagged"] = !r.value("style", false) && (distances[i] > 2.0 * typical + 8 || r.value("fill", 1.0) < 0.12);
        }
    }
    for (const auto& g : report["grounds"]) {
        catalog.push_back({g.value("path", ""), {{"terrain", g.value("terrain", "")}, {"roles", "ground-tile"}, {"name", g.value("terrain", "") + " ground"},
                                                 {"kind", "ground"}, {"layer", "ground"}, {"tileType", "texture"}, {"perspective", "top-down"},
                                                 {"source", "generated"}, {"author", "matched art"}, {"own", "1"}, {"matched", "1"}, {"fromSketch", "1"}}});
    }
    assets.Refresh();
    for (const auto& [path, fields] : catalog) SetCatalogFields(assets, path, fields);
    assets.Refresh();
    return report;
}

ImageData MatchedArtBoard(const Project& project, const json& report, int width) {
    std::vector<ImageData> images;
    for (const char* list : {"pieces", "grounds"}) {
        for (const auto& p : report.value(list, json::array())) {
            if (auto image = DecodeImage(project.root / p.value("path", "")); image && image->width > 0) images.push_back(std::move(*image));
        }
    }
    const int cell = 260, columns = std::max(1, width / cell);
    const int rows = std::max(1, static_cast<int>((images.size() + columns - 1) / columns));
    ImageData board;
    board.width = static_cast<uint32_t>(columns * cell), board.height = static_cast<uint32_t>(rows * cell);
    board.rgba.assign(static_cast<size_t>(board.width) * board.height * 4, 0);
    for (size_t i = 0; i < board.rgba.size(); i += 4) board.rgba[i] = board.rgba[i + 1] = board.rgba[i + 2] = 92, board.rgba[i + 3] = 255;
    for (size_t n = 0; n < images.size(); ++n) {
        const ImageData& im = images[n];
        const double k = std::min(1.0, (cell - 16.0) / std::max(im.width, im.height));
        const int w = std::max(1, static_cast<int>(im.width * k)), h = std::max(1, static_cast<int>(im.height * k));
        const int ox = static_cast<int>(n % columns) * cell + (cell - w) / 2, oy = static_cast<int>(n / columns) * cell + (cell - h) / 2;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const uint8_t* s = &im.rgba[(static_cast<size_t>(std::min<int>(im.height - 1, static_cast<int>(y / k))) * im.width +
                                             std::min<int>(im.width - 1, static_cast<int>(x / k))) * 4];
                uint8_t* d = &board.rgba[(static_cast<size_t>(oy + y) * board.width + ox + x) * 4];
                const float a = s[3] / 255.0f;
                for (int c = 0; c < 3; ++c) d[c] = static_cast<uint8_t>(d[c] * (1 - a) + s[c] * a);
            }
        }
    }
    return board;
}

} // namespace aige
