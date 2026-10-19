#include "assets/LayoutImage.h"

#include "renderer2d/Text.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <set>

namespace aige {

using json = nlohmann::json;

namespace {

struct Rgb {
    float r, g, b;
};

Rgb Hex(uint32_t v) { return {((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f}; }
Rgb Mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Rgb Shade(Rgb a, float f) { return {std::clamp(a.r * f, 0.0f, 1.0f), std::clamp(a.g * f, 0.0f, 1.0f), std::clamp(a.b * f, 0.0f, 1.0f)}; }

uint32_t Hash(int x, int y, uint32_t salt = 0) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + salt * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float Noise(int x, int y, uint32_t salt = 0) { return (Hash(x, y, salt) & 0xFFFF) / 65535.0f; }

Rgb TerrainColor(const std::string& t) {
    if (t == "grass" || t.empty()) return Hex(0x86b25f);
    if (t == "dirt") return Hex(0xc9a874);
    if (t == "gravel") return Hex(0xbfb8a8);
    if (t == "cobble") return Hex(0xa9a39b);
    if (t == "paved") return Hex(0xb6b6b0);
    if (t == "plank") return Hex(0xa67c52);
    if (t == "water") return Hex(0x4f8fc4);
    if (t == "sand") return Hex(0xe4d29a);
    if (t == "snow") return Hex(0xeef2f5);
    if (t == "stone") return Hex(0x9a9a96);
    if (t == "mud") return Hex(0x8a6f4e);
    if (t == "tilled") return Hex(0x8c6a47);
    const uint32_t h = Hash(static_cast<int>(t.size()), static_cast<int>(t[0]));
    return Mix(Hex(0x86b25f), Hex(h & 0xFFFFFF), 0.3f);
}

Rgb RoofColor(const std::string& module) {
    if (module == "house-plot" || module == "terraced-row") return Hex(0xb2553d);
    if (module == "townhouse-block") return Hex(0x9c4a3a);
    if (module == "shack-cluster") return Hex(0x8d7b63);
    if (module == "shop-plot") return Hex(0xc58b36);
    if (module == "inn-plot") return Hex(0x8b5a3a);
    if (module == "workshop-plot") return Hex(0x6f6a62);
    if (module == "warehouse-plot") return Hex(0x7d6a52);
    if (module == "manor-plot") return Hex(0x5f4b7a);
    if (module == "temple-plot") return Hex(0x8c929c);
    if (module == "mill-plot") return Hex(0xa58552);
    if (module == "guard-post") return Hex(0x6b6f76);
    if (module == "farmstead") return Hex(0xa65a32);
    return Hex(0xa45c44);
}

Rgb ZoneColor(int i) {
    static const uint32_t colours[] = {0xf5c26b, 0x9ed0e6, 0xd8a7e8, 0xf29e8e, 0xa8dba8, 0xf7e08a, 0x8fc1b5, 0xe6b8a2};
    return Hex(colours[i % 8]);
}

Rgb FlowerColor(const std::string& concept_, int x, int y) {
    if (concept_ == "rose") return Hex(0xd7263d);
    if (concept_ == "tulip") return Hex(0xf4b400);
    if (concept_ == "daisy") return Hex(0xf6f6f2);
    if (concept_ == "sunflower") return Hex(0xf6c90e);
    if (concept_ == "lavender") return Hex(0x9b7ed9);
    if (concept_ == "poppy") return Hex(0xe8513a);
    static const uint32_t wild[] = {0xf2f2f2, 0xf4d35e, 0xe86a92, 0xb39ddb};
    return Hex(wild[Hash(x, y, 7) % 4]);
}

bool IsCrop(const std::string& concept_) {
    static const std::set<std::string> crops = {"grain", "vegetable", "wheat", "barley", "cabbage", "carrot", "pumpkin", "flax", "vines"};
    return crops.contains(concept_);
}

class Canvas {
public:
    Canvas(int w, int h) : w_(w), h_(h), px_(static_cast<size_t>(w) * h) {}
    int W() const { return w_; }
    int H() const { return h_; }
    void Blend(int x, int y, Rgb c, float a = 1.0f) {
        if (x < 0 || y < 0 || x >= w_ || y >= h_ || a <= 0) return;
        Rgb& p = px_[static_cast<size_t>(y) * w_ + x];
        p = Mix(p, c, std::clamp(a, 0.0f, 1.0f));
    }
    void Rect(float x0, float y0, float x1, float y1, Rgb c, float a = 1.0f) {
        for (int y = static_cast<int>(std::floor(y0)); y < static_cast<int>(std::ceil(y1)); ++y) {
            for (int x = static_cast<int>(std::floor(x0)); x < static_cast<int>(std::ceil(x1)); ++x) Blend(x, y, c, a);
        }
    }
    void Frame(float x0, float y0, float x1, float y1, Rgb c, float a, float t) {
        Rect(x0, y0, x1, y0 + t, c, a);
        Rect(x0, y1 - t, x1, y1, c, a);
        Rect(x0, y0 + t, x0 + t, y1 - t, c, a);
        Rect(x1 - t, y0 + t, x1, y1 - t, c, a);
    }
    // A soft-edged disc.
    void Disc(float cx, float cy, float r, Rgb c, float a = 1.0f) {
        for (int y = static_cast<int>(cy - r - 1); y <= static_cast<int>(cy + r + 1); ++y) {
            for (int x = static_cast<int>(cx - r - 1); x <= static_cast<int>(cx + r + 1); ++x) {
                const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
                Blend(x, y, c, a * std::clamp(r - d + 0.5f, 0.0f, 1.0f));
            }
        }
    }
    Rgb Get(int x, int y) const { return px_[static_cast<size_t>(y) * w_ + x]; }
    void Set(int x, int y, Rgb c) { px_[static_cast<size_t>(y) * w_ + x] = c; }
    ImageData ToImage() const {
        ImageData out;
        out.width = static_cast<uint32_t>(w_);
        out.height = static_cast<uint32_t>(h_);
        out.rgba.resize(px_.size() * 4);
        for (size_t i = 0; i < px_.size(); ++i) {
            out.rgba[i * 4 + 0] = static_cast<uint8_t>(std::lround(std::clamp(px_[i].r, 0.0f, 1.0f) * 255));
            out.rgba[i * 4 + 1] = static_cast<uint8_t>(std::lround(std::clamp(px_[i].g, 0.0f, 1.0f) * 255));
            out.rgba[i * 4 + 2] = static_cast<uint8_t>(std::lround(std::clamp(px_[i].b, 0.0f, 1.0f) * 255));
            out.rgba[i * 4 + 3] = 255;
        }
        return out;
    }

private:
    int w_, h_;
    std::vector<Rgb> px_;
};

// Text with a dark halo, from the built-in font's atlas.
void Label(Canvas& canvas, const std::string& text, float cx, float cy, float size, Rgb colour, bool centred = true) {
    Font& font = Font::Builtin(size);
    std::vector<UiQuad> quads;
    const float width = font.Measure(text);
    font.Draw(quads, text, centred ? cx - width / 2 : cx, cy - font.LineHeight() / 2, {1, 1, 1, 1});
    const ImageData& atlas = font.Atlas();
    auto coverage = [&](const UiQuad& q, int x, int y) {
        const float u = q.u0 + (x + 0.5f - q.x0) / (q.x1 - q.x0) * (q.u1 - q.u0);
        const float v = q.v0 + (y + 0.5f - q.y0) / (q.y1 - q.y0) * (q.v1 - q.v0);
        const int ax = std::clamp(static_cast<int>(u * atlas.width), 0, static_cast<int>(atlas.width) - 1);
        const int ay = std::clamp(static_cast<int>(v * atlas.height), 0, static_cast<int>(atlas.height) - 1);
        return atlas.rgba[(static_cast<size_t>(ay) * atlas.width + ax) * 4 + 3] / 255.0f;
    };
    for (int pass = 0; pass < 2; ++pass) {
        for (const UiQuad& q : quads) {
            for (int y = static_cast<int>(q.y0); y < static_cast<int>(std::ceil(q.y1)); ++y) {
                for (int x = static_cast<int>(q.x0); x < static_cast<int>(std::ceil(q.x1)); ++x) {
                    const float a = coverage(q, x, y);
                    if (a <= 0) continue;
                    if (pass == 0) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) canvas.Blend(x + dx, y + dy, Hex(0x20242c), a * 0.55f);
                        }
                    } else {
                        canvas.Blend(x, y, colour, a);
                    }
                }
            }
        }
    }
}

} // namespace

ImageData RenderLayoutImage(const json& solved, const LayoutImageOptions& options) {
    const json bounds = solved.value("grid", json::object()).value("bounds", json::array({0, 0, 64, 64}));
    const int W = std::max(1, bounds[2].get<int>()), H = std::max(1, bounds[3].get<int>());
    const int cell = options.cellPixels > 0 ? options.cellPixels : std::clamp(options.targetSize / std::max(W, H), 3, 24);
    const int footer = options.title.empty() ? 0 : std::max(28, cell * 3);
    // The legend: what this map shows, in the order it's drawn.
    enum class Swatch { Fill, Outline, Roof, Door, Tree, Flowers, Crops, Well, Stall, Grave, Prop, Fence, Wall, Hedge, Bridge, Rows };
    struct Entry {
        Swatch swatch;
        Rgb colour;
        std::string label;
        bool heading = false;
    };
    std::vector<Entry> legend;
    if (options.legend) {
        auto add = [&](Swatch s, Rgb c, const std::string& label) {
            for (const auto& e : legend) {
                if (!e.heading && e.label == label) return;
            }
            legend.push_back({s, c, label});
        };
        auto heading = [&](const std::string& text) { legend.push_back({Swatch::Fill, {}, text, true}); };
        auto pretty = [&](const std::string& module) {
            if (auto it = options.moduleNames.find(module); it != options.moduleNames.end()) return it->second;
            std::string out = module;
            for (char& c : out) {
                if (c == '-') c = ' ';
            }
            if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
            return out;
        };
        // Zones, by name.
        int z = 0;
        std::vector<Entry> zones;
        for (const auto& r : solved.value("regions", json::array())) {
            if (r.value("statement", "") != "site/zones") continue;
            const std::string id = r.value("id", "");
            const auto name = options.names.find(id);
            zones.push_back({Swatch::Outline, ZoneColor(z++), name != options.names.end() ? name->second : id});
        }
        if (!zones.empty()) {
            heading("Zones");
            legend.insert(legend.end(), zones.begin(), zones.end());
        }
        // Buildings, by kind.
        std::set<std::string> kinds;
        bool doors = false;
        for (const auto& o : solved.value("objects", json::array())) {
            if (o.value("layer", "") != "building") continue;
            const auto module = options.modules.find(o.value("node", ""));
            kinds.insert(module != options.modules.end() ? module->second : "");
            doors = true;
        }
        if (!kinds.empty()) {
            heading("Buildings (roofs)");
            for (const auto& k : kinds) add(Swatch::Roof, RoofColor(k), k.empty() ? "Building" : pretty(k));
            if (doors) add(Swatch::Door, Hex(0x3a2a1c), "Door (on the street side)");
        }
        // Ground.
        heading("Ground");
        std::set<std::string> grounds;
        for (const auto& row : solved.value("terrain", json::object()).value("rows", json::array())) {
            for (const auto& run : row) {
                const int t = run[1].get<int>();
                const json& pal = solved["terrain"]["palette"];
                if (t >= 0 && t < static_cast<int>(pal.size()) && pal[t].is_string()) grounds.insert(pal[t].get<std::string>());
            }
        }
        for (const auto& g : grounds) {
            const std::string label = g.empty() ? "Grass" : g == "dirt" ? "Dirt road or ground" : g == "water" ? "Water" : pretty(g);
            add(Swatch::Fill, TerrainColor(g.empty() ? "grass" : g), label);
        }
        if (!solved.value("terrain", json::object()).value("bridges", json::array()).empty()) add(Swatch::Bridge, Hex(0x9c7a4f), "Bridge");
        // Parcels and features.
        std::set<std::string> areas;
        bool plots = false;
        for (const auto& r : solved.value("regions", json::array())) {
            const std::string statement = r.value("statement", "");
            if (statement.size() > 6 && statement.substr(statement.size() - 6) == "/plots") plots = true;
            areas.insert(r.value("module", ""));
        }
        if (plots || areas.contains("garden") || areas.contains("vegetable-bed")) heading("Plots and gardens");
        if (plots) add(Swatch::Outline, Hex(0x4e5a32), "Plot boundary");
        if (areas.contains("garden") || areas.contains("formal-garden")) add(Swatch::Fill, Mix(Hex(0x86b25f), Hex(0x6f9e48), 0.55f), "Garden");
        if (areas.contains("vegetable-bed") || areas.contains("field")) add(Swatch::Rows, Hex(0x7a5634), "Bed or field (tilled rows)");
        for (const char* yard : {"front-yard", "yard", "farmyard", "work-yard", "courtyard"}) {
            if (areas.contains(yard)) add(Swatch::Fill, Mix(Hex(0x86b25f), Hex(0xd2bf8e), 0.35f), "Yard");
        }
        // Things.
        std::set<std::string> slots, flowers, animals;
        for (const auto& o : solved.value("objects", json::array())) {
            if (o.value("layer", "") == "building") continue;
            const std::string slot = o.value("slot", ""), shows = o.value("shows", "");
            slots.insert(slot);
            if (shows.empty()) continue;
            if (slot == "animal") animals.insert(shows);
            else if (slot == "plant" || slot == "flower") flowers.insert(shows);
        }
        std::set<std::string> lines;
        for (const auto& l : solved.value("lines", json::array())) lines.insert(l.value("slot", ""));
        if (!slots.empty() || !lines.empty()) heading("Things");
        for (const auto& slot : slots) {
            if (slot == "tree") add(Swatch::Tree, Hex(0x3d6b2a), "Tree");
            else if (slot == "bush") add(Swatch::Tree, Hex(0x3d6b2a), "Bush");
            else if (slot == "plant" || slot == "flower") {
                // Each species shown, in its colour; crops as crops.
                bool any = false;
                for (const auto& f : flowers) {
                    if (IsCrop(f)) {
                        add(Swatch::Crops, Hex(0x6fae3a), "Crops (" + f + ")");
                    } else {
                        add(Swatch::Flowers, FlowerColor(f, 0, 0), pretty(f));
                    }
                    any = true;
                }
                if (!any) add(Swatch::Flowers, Hex(0xf2f2f2), "Flowers (mixed)");
            } else if (slot == "crop") {
                add(Swatch::Crops, Hex(0x6fae3a), "Crops");
            }
            else if (slot == "well" || slot == "centrepiece" || slot == "fountain") add(Swatch::Well, Hex(0x3f7fb7), "Well or fountain");
            else if (slot == "stall") add(Swatch::Stall, Hex(0xc0392b), "Market stall");
            else if (slot == "grave") add(Swatch::Grave, Hex(0x9b9b96), "Grave");
            else if (slot == "item") add(Swatch::Prop, Hex(0x8b6b45), "Doorstep things (pots, barrels, a bench)");
            else if (slot == "sign") add(Swatch::Prop, Hex(0x8b6b45), "Sign");
            else if (slot == "animal") {
                std::string kinds;
                for (const auto& a : animals) kinds += (kinds.empty() ? "" : ", ") + a;
                add(Swatch::Prop, Hex(0x8b6b45), kinds.empty() ? "Animal" : "Animals (" + kinds + ")");
            }
            else add(Swatch::Prop, Hex(0x8b6b45), "Other prop (" + (slot.empty() ? std::string("object") : slot) + ")");
        }
        for (const auto& slot : lines) {
            if (slot == "wall") add(Swatch::Wall, Hex(0x8d8a84), "Wall (gaps are gates)");
            else if (slot == "hedge") add(Swatch::Hedge, Hex(0x3f6b2d), "Hedge");
            else add(Swatch::Fence, Hex(0x6b4a2b), "Fence (gap is the gate)");
        }
    }
    const int rowHeight = 22;
    const int legendWidth = legend.empty() ? 0 : 260;
    const int legendHeight = static_cast<int>(legend.size()) * rowHeight + 24;
    Canvas canvas(W * cell + legendWidth, std::max(H * cell + footer, legendHeight));
    canvas.Rect(0, 0, static_cast<float>(canvas.W()), static_cast<float>(canvas.H()), Hex(0x1e1e2e));

    // Terrain, with a little texture.
    std::vector<std::string> palette;
    for (const auto& t : solved.value("terrain", json::object()).value("palette", json::array())) palette.push_back(t.is_string() ? t.get<std::string>() : "");
    std::vector<int> terrain(static_cast<size_t>(W) * H, 0);
    const json rows = solved.value("terrain", json::object()).value("rows", json::array());
    for (int y = 0; y < H && y < static_cast<int>(rows.size()); ++y) {
        int x = 0;
        for (const auto& run : rows[y]) {
            for (int k = 0; k < run[0].get<int>() && x < W; ++k) terrain[static_cast<size_t>(y) * W + x++] = run[1].get<int>();
        }
    }
    auto terrainAt = [&](int x, int y) -> const std::string& {
        static const std::string none;
        const int t = terrain[static_cast<size_t>(std::clamp(y, 0, H - 1)) * W + std::clamp(x, 0, W - 1)];
        return t >= 0 && t < static_cast<int>(palette.size()) ? palette[t] : none;
    };
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const std::string& t = terrainAt(x, y);
            const Rgb base = TerrainColor(t.empty() ? "grass" : t);
            for (int py = 0; py < cell; ++py) {
                for (int px = 0; px < cell; ++px) {
                    const int gx = x * cell + px, gy = y * cell + py;
                    float f = 0.94f + 0.1f * Noise(gx / 2, gy / 2, 1);
                    if (t == "water") f = 0.92f + 0.14f * std::sin((gx + gy * 0.6f) * 0.35f + Noise(x, y, 3) * 3.0f) * 0.5f + 0.07f;
                    if (t == "cobble") f = ((gx / 3 + gy / 3) % 2) ? 0.95f : 1.03f;
                    canvas.Set(gx, gy, Shade(base, f));
                }
            }
        }
    }
    // Shore and road edges: darker where a different ground begins.
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const std::string& t = terrainAt(x, y);
            if (t != "water" && t != "dirt" && t != "cobble" && t != "gravel" && t != "paved") continue;
            for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                if (x + dx < 0 || y + dy < 0 || x + dx >= W || y + dy >= H || terrainAt(x + dx, y + dy) == t) continue;
                const float edge = std::max(1.0f, cell / 6.0f);
                const float x0 = x * cell, y0 = y * cell;
                if (dx == 1) canvas.Rect(x0 + cell - edge, y0, x0 + cell, y0 + cell, Hex(0x3b3a33), 0.25f);
                if (dx == -1) canvas.Rect(x0, y0, x0 + edge, y0 + cell, Hex(0x3b3a33), 0.25f);
                if (dy == 1) canvas.Rect(x0, y0 + cell - edge, x0 + cell, y0 + cell, Hex(0x3b3a33), 0.25f);
                if (dy == -1) canvas.Rect(x0, y0, x0 + cell, y0 + edge, Hex(0x3b3a33), 0.25f);
            }
        }
    }
    // Bridges: planks over the water.
    for (const auto& b : solved.value("terrain", json::object()).value("bridges", json::array())) {
        const float x0 = b[0].get<int>() * cell, y0 = b[1].get<int>() * cell;
        canvas.Rect(x0, y0, x0 + cell, y0 + cell, Hex(0x9c7a4f));
        for (int k = 0; k < cell; k += std::max(2, cell / 3)) canvas.Rect(x0, y0 + k, x0 + cell, y0 + k + 1, Hex(0x6e5537), 0.7f);
    }

    // Regions: zones tinted and outlined; parcels outlined; gardens and beds coloured.
    auto cellsOf = [&](const json& r) {
        std::vector<std::pair<int, int>> out;
        for (const auto& run : r.value("cells", json::array())) {
            for (int x = run[1].get<int>(); x < run[2].get<int>(); ++x) out.push_back({x, run[0].get<int>()});
        }
        return out;
    };
    auto outline = [&](const std::vector<std::pair<int, int>>& cells, Rgb c, float a, float t) {
        std::set<std::pair<int, int>> in(cells.begin(), cells.end());
        for (const auto& [x, y] : cells) {
            const float x0 = x * cell, y0 = y * cell;
            if (!in.contains({x, y - 1})) canvas.Rect(x0, y0, x0 + cell, y0 + t, c, a);
            if (!in.contains({x, y + 1})) canvas.Rect(x0, y0 + cell - t, x0 + cell, y0 + cell, c, a);
            if (!in.contains({x - 1, y})) canvas.Rect(x0, y0, x0 + t, y0 + cell, c, a);
            if (!in.contains({x + 1, y})) canvas.Rect(x0 + cell - t, y0, x0 + cell, y0 + cell, c, a);
        }
    };
    std::vector<std::pair<std::string, std::array<float, 3>>> zoneLabels;
    int zoneIndex = 0;
    for (const auto& r : solved.value("regions", json::array())) {
        const std::string statement = r.value("statement", "");
        const std::string module = r.value("module", "");
        const auto cells = cellsOf(r);
        if (statement == "site/zones") {
            const Rgb c = ZoneColor(zoneIndex++);
            for (const auto& [x, y] : cells) {
                const std::string& t = terrainAt(x, y);
                if (t == "water" || t == "dirt" || t == "cobble") continue;
                canvas.Rect(x * cell, y * cell, (x + 1) * cell, (y + 1) * cell, c, 0.24f);
            }
            outline(cells, Shade(c, 0.5f), 0.95f, std::max(1.5f, cell / 4.0f));
            double sx = 0, sy = 0;
            for (const auto& [x, y] : cells) {
                sx += x + 0.5;
                sy += y + 0.5;
            }
            if (!cells.empty()) {
                const std::string id = r.value("id", "");
                const auto name = options.names.find(id);
                zoneLabels.push_back({name != options.names.end() ? name->second : id,
                                      {static_cast<float>(sx / cells.size() * cell), static_cast<float>(sy / cells.size() * cell), 0}});
            }
        } else if (statement.size() > 6 && statement.substr(statement.size() - 6) == "/plots") {
            outline(cells, Hex(0x4e5a32), 0.45f, 1.0f);
        } else if (module == "garden" || module == "formal-garden") {
            for (const auto& [x, y] : cells) canvas.Rect(x * cell, y * cell, (x + 1) * cell, (y + 1) * cell, Hex(0x6f9e48), 0.55f);
        } else if (module == "vegetable-bed" || module == "field") {
            for (const auto& [x, y] : cells) {
                canvas.Rect(x * cell, y * cell, (x + 1) * cell, (y + 1) * cell, Hex(0x7a5634), 0.85f);
                for (int k = 1; k < cell; k += std::max(2, cell / 3)) canvas.Rect(x * cell, y * cell + k, (x + 1) * cell, y * cell + k + 1, Hex(0x5a3e24), 0.6f);
            }
        } else if (module == "front-yard" || module == "yard" || module == "farmyard" || module == "work-yard" || module == "courtyard") {
            for (const auto& [x, y] : cells) canvas.Rect(x * cell, y * cell, (x + 1) * cell, (y + 1) * cell, Hex(0xd2bf8e), 0.35f);
        } else if (module == "orchard" || module == "pasture" || module == "graveyard") {
            for (const auto& [x, y] : cells) canvas.Rect(x * cell, y * cell, (x + 1) * cell, (y + 1) * cell, Hex(0x739a4a), 0.35f);
        }
    }

    // Fences, hedges and walls, on cell edges.
    for (const auto& l : solved.value("lines", json::array())) {
        const std::string slot = l.value("slot", "");
        const bool wall = slot == "wall";
        const bool hedge = slot == "hedge";
        const Rgb c = wall ? Hex(0x8d8a84) : hedge ? Hex(0x3f6b2d) : Hex(0x6b4a2b);
        const float t = wall ? std::max(2.0f, cell / 2.5f) : std::max(1.0f, cell / 6.0f);
        for (const auto& e : l.value("edges", json::array())) {
            const float x = e[0].get<int>() * static_cast<float>(cell), y = e[1].get<int>() * static_cast<float>(cell);
            if (e[2] == "n") {
                if (wall) canvas.Rect(x - 1, y - t / 2 - 1, x + cell + 1, y + t / 2 + 1, Hex(0x45433f), 0.9f);
                canvas.Rect(x, y - t / 2, x + cell, y + t / 2, c);
            } else {
                if (wall) canvas.Rect(x - t / 2 - 1, y - 1, x + t / 2 + 1, y + cell + 1, Hex(0x45433f), 0.9f);
                canvas.Rect(x - t / 2, y, x + t / 2, y + cell, c);
            }
        }
    }

    // Objects: small things first, buildings on top.
    std::vector<const json*> objects;
    if (solved.contains("objects") && solved["objects"].is_array()) {
        for (const auto& o : solved["objects"]) objects.push_back(&o); // pointers into the document, not a copy
    }
    std::stable_sort(objects.begin(), objects.end(), [](const json* a, const json* b) {
        return (a->value("layer", "") == "building") < (b->value("layer", "") == "building");
    });
    for (const json* op : objects) {
        const json& o = *op;
        const float x0 = o["x"].get<int>() * static_cast<float>(cell), y0 = o["y"].get<int>() * static_cast<float>(cell);
        const float x1 = x0 + o["w"].get<int>() * cell, y1 = y0 + o["h"].get<int>() * cell;
        const std::string slot = o.value("slot", ""), layer = o.value("layer", ""), shows = o.value("shows", "");
        const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
        if (layer == "building") {
            const auto module = options.modules.find(o.value("node", ""));
            const Rgb roof = RoofColor(module != options.modules.end() ? module->second : "");
            const float shadow = std::max(2.0f, cell / 3.0f);
            canvas.Rect(x0 + shadow, y0 + shadow, x1 + shadow, y1 + shadow, Hex(0x1c1f17), 0.35f);
            canvas.Rect(x0, y0, x1, y1, roof);
            // The roof's two slopes: light on one side of the ridge, dark on the other.
            const bool across = (x1 - x0) >= (y1 - y0);
            if (across) {
                canvas.Rect(x0, y0, x1, cy, Shade(roof, 1.12f), 0.9f);
                canvas.Rect(x0, cy - 0.5f, x1, cy + 0.5f, Shade(roof, 0.6f));
            } else {
                canvas.Rect(x0, y0, cx, y1, Shade(roof, 1.12f), 0.9f);
                canvas.Rect(cx - 0.5f, y0, cx + 0.5f, y1, Shade(roof, 0.6f));
            }
            canvas.Frame(x0, y0, x1, y1, Shade(roof, 0.45f), 1.0f, std::max(1.0f, cell / 8.0f));
            // The door, on the side it faces.
            const std::string f = o.value("facing", "s");
            const float d = std::max(2.0f, cell * 0.5f), t = std::max(1.5f, cell / 4.0f);
            if (f == "s") canvas.Rect(cx - d / 2, y1 - t, cx + d / 2, y1, Hex(0x3a2a1c));
            if (f == "n") canvas.Rect(cx - d / 2, y0, cx + d / 2, y0 + t, Hex(0x3a2a1c));
            if (f == "e") canvas.Rect(x1 - t, cy - d / 2, x1, cy + d / 2, Hex(0x3a2a1c));
            if (f == "w") canvas.Rect(x0, cy - d / 2, x0 + t, cy + d / 2, Hex(0x3a2a1c));
        } else if (slot == "tree" || slot == "bush") {
            const float r = cell * (slot == "tree" ? 0.62f : 0.42f);
            canvas.Disc(cx + cell * 0.18f, cy + cell * 0.22f, r, Hex(0x1c2614), 0.35f);
            canvas.Disc(cx, cy, r, Hex(0x3d6b2a));
            canvas.Disc(cx - r * 0.25f, cy - r * 0.25f, r * 0.55f, Hex(0x5a8f3c), 0.8f);
        } else if ((slot == "plant" || slot == "flower") && !IsCrop(shows)) {
            for (int k = 0; k < 3; ++k) {
                const float fx = x0 + (0.2f + 0.6f * Noise(o["x"].get<int>(), o["y"].get<int>(), k)) * cell;
                const float fy = y0 + (0.2f + 0.6f * Noise(o["y"].get<int>(), o["x"].get<int>(), k + 9)) * cell;
                canvas.Disc(fx, fy, std::max(1.0f, cell * 0.16f), FlowerColor(shows, o["x"].get<int>() + k, o["y"].get<int>()));
            }
        } else if (slot == "crop" || IsCrop(shows)) {
            for (int k = 0; k < 2; ++k) canvas.Disc(x0 + cell * (0.3f + 0.4f * k), cy, std::max(1.0f, cell * 0.2f), Hex(0x6fae3a));
        } else if (slot == "well" || slot == "centrepiece" || slot == "fountain") {
            canvas.Disc(cx, cy, std::min(x1 - x0, y1 - y0) * 0.45f, Hex(0x77746c));
            canvas.Disc(cx, cy, std::min(x1 - x0, y1 - y0) * 0.28f, Hex(0x3f7fb7));
        } else if (slot == "stall") {
            canvas.Rect(x0 + 1, y0 + 1, x1 - 1, y1 - 1, Hex(0xe8dcc0));
            for (float x = x0 + 1; x < x1 - 1; x += std::max(2.0f, cell / 2.0f)) canvas.Rect(x, y0 + 1, x + std::max(1.0f, cell / 4.0f), y1 - 1, Hex(0xc0392b), 0.85f);
        } else if (slot == "grave") {
            canvas.Rect(cx - cell * 0.2f, cy - cell * 0.3f, cx + cell * 0.2f, cy + cell * 0.3f, Hex(0x9b9b96));
        } else {
            canvas.Rect(x0 + cell * 0.25f, y0 + cell * 0.25f, x1 - cell * 0.25f, y1 - cell * 0.25f, Hex(0x8b6b45));
            canvas.Frame(x0 + cell * 0.25f, y0 + cell * 0.25f, x1 - cell * 0.25f, y1 - cell * 0.25f, Hex(0x4f3b25), 1.0f, 1.0f);
        }
    }

    // Zone names, and the title along the bottom.
    const float labelSize = std::clamp(cell * 2.2f, 12.0f, 26.0f);
    for (const auto& [text, at] : zoneLabels) Label(canvas, text, at[0], at[1], labelSize, Hex(0xfdfaf2));
    if (footer > 0) {
        canvas.Rect(0, static_cast<float>(H * cell), static_cast<float>(W * cell), static_cast<float>(H * cell + footer), Hex(0x1e1e2e));
        Label(canvas, options.title, W * cell / 2.0f, H * cell + footer / 2.0f, std::clamp(footer * 0.55f, 12.0f, 24.0f), Hex(0xcdd6f4));
    }

    // The legend, each entry with a swatch drawn the way the map draws it.
    const float lx = static_cast<float>(W * cell) + 14;
    float ly = 14;
    for (const auto& e : legend) {
        if (e.heading) {
            Label(canvas, e.label, lx, ly + rowHeight / 2.0f, 15, Hex(0x89b4fa), false);
            ly += rowHeight;
            continue;
        }
        const float sx = lx + 4, sy = ly + 3, sw = 26, sh = rowHeight - 7;
        const float cx = sx + sw / 2, cy = sy + sh / 2;
        canvas.Rect(sx, sy, sx + sw, sy + sh, TerrainColor("grass")); // the ground things stand on
        switch (e.swatch) {
            case Swatch::Fill: canvas.Rect(sx, sy, sx + sw, sy + sh, e.colour); break;
            case Swatch::Outline:
                canvas.Rect(sx, sy, sx + sw, sy + sh, e.colour, 0.55f);
                canvas.Frame(sx, sy, sx + sw, sy + sh, Shade(e.colour, 0.55f), 1.0f, 2.0f);
                break;
            case Swatch::Roof:
                canvas.Rect(sx + 3, sy + 3, sx + sw + 2, sy + sh + 2, Hex(0x1c1f17), 0.35f);
                canvas.Rect(sx, sy, sx + sw, sy + sh, e.colour);
                canvas.Rect(sx, sy, sx + sw, cy, Shade(e.colour, 1.12f), 0.9f);
                canvas.Rect(sx, cy - 0.5f, sx + sw, cy + 0.5f, Shade(e.colour, 0.6f));
                canvas.Frame(sx, sy, sx + sw, sy + sh, Shade(e.colour, 0.45f), 1.0f, 1.0f);
                break;
            case Swatch::Door:
                canvas.Rect(sx, sy, sx + sw, sy + sh, RoofColor("house-plot"));
                canvas.Rect(cx - 4, sy + sh - 4, cx + 4, sy + sh, e.colour);
                break;
            case Swatch::Tree:
                canvas.Disc(cx + 2, cy + 2, 7, Hex(0x1c2614), 0.35f);
                canvas.Disc(cx, cy, 7, e.colour);
                canvas.Disc(cx - 2, cy - 2, 4, Hex(0x5a8f3c), 0.8f);
                break;
            case Swatch::Flowers:
                for (int k = 0; k < 5; ++k) canvas.Disc(sx + 4 + k * 4.5f, cy + ((k % 2) ? -3.0f : 3.0f), 2, e.colour);
                break;
            case Swatch::Crops:
                for (int k = 0; k < 4; ++k) canvas.Disc(sx + 5 + k * 6, cy, 2.5f, e.colour);
                break;
            case Swatch::Well:
                canvas.Disc(cx, cy, 7, Hex(0x77746c));
                canvas.Disc(cx, cy, 4.5f, e.colour);
                break;
            case Swatch::Stall:
                canvas.Rect(sx + 3, sy + 2, sx + sw - 3, sy + sh - 2, Hex(0xe8dcc0));
                for (float x = sx + 3; x < sx + sw - 3; x += 5) canvas.Rect(x, sy + 2, x + 2.5f, sy + sh - 2, e.colour, 0.85f);
                break;
            case Swatch::Grave: canvas.Rect(cx - 3, cy - 5, cx + 3, cy + 5, e.colour); break;
            case Swatch::Prop:
                canvas.Rect(cx - 5, cy - 5, cx + 5, cy + 5, e.colour);
                canvas.Frame(cx - 5, cy - 5, cx + 5, cy + 5, Hex(0x4f3b25), 1.0f, 1.0f);
                break;
            case Swatch::Fence: canvas.Rect(sx, cy - 1, sx + sw, cy + 1, e.colour); break;
            case Swatch::Hedge: canvas.Rect(sx, cy - 2, sx + sw, cy + 2, e.colour); break;
            case Swatch::Wall:
                canvas.Rect(sx, cy - 4, sx + sw, cy + 4, Hex(0x45433f));
                canvas.Rect(sx, cy - 3, sx + sw, cy + 3, e.colour);
                break;
            case Swatch::Bridge:
                canvas.Rect(sx, sy, sx + sw, sy + sh, TerrainColor("water"));
                canvas.Rect(sx + 6, sy, sx + sw - 6, sy + sh, e.colour);
                break;
            case Swatch::Rows:
                canvas.Rect(sx, sy, sx + sw, sy + sh, e.colour);
                for (float y = sy + 2; y < sy + sh; y += 4) canvas.Rect(sx, y, sx + sw, y + 1, Hex(0x5a3e24), 0.6f);
                break;
        }
        canvas.Frame(sx, sy, sx + sw, sy + sh, Hex(0x11111b), 0.8f, 1.0f);
        Label(canvas, e.label, sx + sw + 10, ly + rowHeight / 2.0f, 13, Hex(0xcdd6f4), false);
        ly += rowHeight;
    }
    return canvas.ToImage();
}

} // namespace aige
