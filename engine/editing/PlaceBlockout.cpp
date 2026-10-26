#include "editing/PlaceBlockout.h"

#include "places/Ontology.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace aige {

using json = nlohmann::json;

namespace {

using Rgb = std::array<int, 3>;

struct Pt {
    double x, y;
};

// A plain colour per terrain: enough for an image model to tell grass from road from water.
Rgb TerrainColour(const std::string& t) {
    if (t == "water") return {52, 96, 150};
    if (t == "dirt" || t == "road" || t == "path" || t == "mud") return {150, 118, 78};
    if (t == "forest-floor") return {58, 84, 48};
    if (t == "grass" || t == "meadow") return {96, 130, 70};
    if (t == "sand" || t == "beach") return {196, 176, 128};
    if (t == "cobble" || t == "cobblestone" || t == "paving" || t == "stone" || t == "flagstone") return {138, 134, 126};
    if (t == "floor" || t == "stone-floor" || t == "wood-floor" || t == "planks") return {128, 112, 92};
    if (t == "rock-wall" || t == "rock" || t == "cliff") return {48, 46, 50};
    if (t == "void") return {16, 16, 20};
    if (t == "farmland" || t == "field" || t == "tilled") return {118, 92, 60};
    if (t == "snow") return {226, 230, 236};
    if (t == "asphalt" || t == "tarmac") return {66, 66, 72};
    if (t == "sidewalk" || t == "pavement" || t == "plaza") return {172, 170, 162};
    if (t == "concrete") return {148, 148, 144};
    if (t == "crosswalk") return {214, 214, 208};
    // Anything else: a muted colour of its own, the same every time.
    const size_t h = std::hash<std::string>{}(t);
    return {90 + static_cast<int>(h % 60), 90 + static_cast<int>((h >> 8) % 60), 80 + static_cast<int>((h >> 16) % 50)};
}

// A thing's colour (props, buildings' walls): by what it is.
Rgb ThingColour(const std::string& slot) {
    if (slot == "well" || slot == "statue" || slot == "pillar" || slot == "sarcophagus" || slot == "altar") return {150, 150, 150};
    if (slot == "stall" || slot == "table" || slot == "bench" || slot == "chair" || slot == "crate" || slot == "barrel" || slot == "pier" ||
        slot == "cart" || slot == "chest" || slot == "bookshelf" || slot == "desk" || slot == "bed") {
        return {128, 88, 52};
    }
    if (slot == "light" || slot == "torch" || slot == "lamp" || slot == "candle" || slot == "brazier") return {240, 190, 80};
    const size_t h = std::hash<std::string>{}(slot);
    return {110 + static_cast<int>(h % 90), 90 + static_cast<int>((h >> 8) % 80), 70 + static_cast<int>((h >> 16) % 70)};
}

// Strong colours an image model can tell apart and name, the most distinct first: one per kind of
// building or thing in the plan.
struct Named {
    const char* name;
    Rgb rgb;
};
constexpr Named kPalette[] = {
    {"bright red", {220, 32, 32}},    {"royal blue", {36, 72, 224}},  {"yellow", {240, 208, 32}},     {"magenta", {224, 32, 192}},
    {"cyan", {32, 208, 224}},         {"orange", {240, 128, 32}},     {"purple", {128, 48, 208}},     {"white", {244, 244, 244}},
    {"pink", {240, 128, 176}},        {"teal", {16, 144, 128}},       {"lime green", {144, 224, 32}}, {"navy blue", {20, 32, 128}},
    {"maroon", {128, 16, 32}},        {"lavender", {176, 160, 240}},  {"coral", {240, 96, 80}},       {"olive", {128, 128, 16}},
    {"mint", {128, 240, 176}},        {"gold", {212, 168, 40}},       {"sky blue", {120, 180, 250}},  {"black", {8, 8, 8}},
};

// Plants keep their own colours: green blobs read as trees without being told.
bool Plant(const std::string& slot) {
    return slot == "tree" || slot == "bush" || slot == "hedge" || slot == "shrub" || slot == "grass" || slot == "flower" || slot == "crop" ||
           slot == "plant" || slot == "reed" || slot == "fern" || slot == "herb" || slot == "weed";
}

std::string Hex(Rgb c) {
    char s[8];
    std::snprintf(s, sizeof(s), "#%02X%02X%02X", c[0], c[1], c[2]);
    return s;
}

Rgb Shade(Rgb c, double k) {
    return {std::clamp(static_cast<int>(c[0] * k), 0, 255), std::clamp(static_cast<int>(c[1] * k), 0, 255), std::clamp(static_cast<int>(c[2] * k), 0, 255)};
}

class Canvas {
public:
    Canvas(int w, int h, Rgb background) {
        image.width = static_cast<uint32_t>(w);
        image.height = static_cast<uint32_t>(h);
        image.rgba.resize(static_cast<size_t>(w) * h * 4);
        for (size_t i = 0; i < image.rgba.size(); i += 4) {
            image.rgba[i] = static_cast<uint8_t>(background[0]), image.rgba[i + 1] = static_cast<uint8_t>(background[1]);
            image.rgba[i + 2] = static_cast<uint8_t>(background[2]), image.rgba[i + 3] = 255;
        }
    }
    void Put(int x, int y, Rgb c) {
        if (x < 0 || y < 0 || x >= static_cast<int>(image.width) || y >= static_cast<int>(image.height)) return;
        uint8_t* p = &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
        p[0] = static_cast<uint8_t>(c[0]), p[1] = static_cast<uint8_t>(c[1]), p[2] = static_cast<uint8_t>(c[2]);
    }
    // A polygon, by scanlines (convex or not: even-odd).
    void Fill(const std::vector<Pt>& poly, Rgb c) {
        if (poly.size() < 3) return;
        double minY = 1e30, maxY = -1e30;
        for (const auto& p : poly) minY = std::min(minY, p.y), maxY = std::max(maxY, p.y);
        const int ya = std::max(0, static_cast<int>(std::floor(minY))), yb = std::min(static_cast<int>(image.height) - 1, static_cast<int>(std::ceil(maxY)));
        std::vector<double> xs;
        for (int y = ya; y <= yb; ++y) {
            const double sy = y + 0.5;
            xs.clear();
            for (size_t i = 0; i < poly.size(); ++i) {
                const Pt& a = poly[i];
                const Pt& b = poly[(i + 1) % poly.size()];
                if ((a.y <= sy && b.y > sy) || (b.y <= sy && a.y > sy)) xs.push_back(a.x + (sy - a.y) / (b.y - a.y) * (b.x - a.x));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t i = 0; i + 1 < xs.size(); i += 2) {
                for (int x = std::max(0, static_cast<int>(std::ceil(xs[i] - 0.5))); x <= std::min(static_cast<int>(image.width) - 1, static_cast<int>(std::floor(xs[i + 1] - 0.5))); ++x) {
                    Put(x, y, c);
                }
            }
        }
    }
    void Ellipse(double cx, double cy, double rx, double ry, Rgb c) {
        for (int y = static_cast<int>(cy - ry); y <= static_cast<int>(cy + ry); ++y) {
            const double dy = (y + 0.5 - cy) / ry;
            if (std::abs(dy) > 1) continue;
            const double half = rx * std::sqrt(1 - dy * dy);
            for (int x = static_cast<int>(std::ceil(cx - half)); x <= static_cast<int>(cx + half); ++x) Put(x, y, c);
        }
    }
    ImageData image;
};

} // namespace

ImageData RenderPlanBlockout(const json& solved, int x0, int y0, int x1, int y1, int width, const Ontology* ontology, json* legend) {
    // The terrain, cell by cell.
    const json bounds = solved.contains("grid") ? solved["grid"].value("bounds", json::array({0, 0, 0, 0})) : json::array({0, 0, 0, 0});
    const int W = bounds[2].get<int>(), H = bounds[3].get<int>();
    std::vector<std::string> cells(static_cast<size_t>(std::max(0, W * H)), "grass");
    bool indoor = false;
    if (solved.contains("terrain")) {
        const json palette = solved["terrain"].value("palette", json::array());
        const json rows = solved["terrain"].value("rows", json::array());
        for (int y = 0; y < static_cast<int>(rows.size()) && y < H; ++y) {
            int x = 0;
            for (const auto& run : rows[y]) {
                const int count = run[0].get<int>(), index = run[1].get<int>();
                const std::string name = index >= 0 && index < static_cast<int>(palette.size()) ? palette[index].get<std::string>() : "";
                for (int k = 0; k < count && x < W; ++k, ++x) cells[static_cast<size_t>(y) * W + x] = name.empty() ? "grass" : name;
                indoor = indoor || name == "void" || name == "rock-wall";
            }
        }
    }
    auto terrainAt = [&](int x, int y) { return x >= 0 && y >= 0 && x < W && y < H ? cells[static_cast<size_t>(y) * W + x] : std::string("void"); };
    // The view: a cell is a diamond twice as wide as tall; the area's diamond fills the width.
    const int cw = std::max(1, x1 - x0), ch = std::max(1, y1 - y0);
    const double tw = 2.0 * width / (cw + ch), th = tw / 2;
    const double top = th * 3.0; // room for the tallest things at the back
    const int height = static_cast<int>(std::ceil((cw + ch) * th / 2 + top + th / 2));
    auto P = [&](double wx, double wy, double up = 0) {
        return Pt{(wx - x0 - (wy - y0)) * tw / 2 + ch * tw / 2, (wx - x0 + (wy - y0)) * th / 2 + top - up};
    };
    Canvas canvas(width, height, indoor ? Rgb{12, 12, 16} : Rgb{24, 24, 30});
    // Each kind of building and thing its colour: the most common the most distinct.
    std::map<std::string, int> counts;
    std::map<std::string, std::string> layerOf, showsOf;
    for (const auto& o : solved.value("objects", json::array())) {
        const std::string slot = o.value("slot", "");
        const double cx = o.value("x", 0) + o.value("w", 1) / 2.0, cy = o.value("y", 0) + o.value("h", 1) / 2.0;
        if (slot.empty() || cx < x0 || cy < y0 || cx >= x1 || cy >= y1) continue;
        ++counts[slot];
        layerOf.emplace(slot, o.value("layer", "prop"));
        if (!o.value("shows", "").empty()) showsOf.emplace(slot, o.value("shows", ""));
    }
    std::vector<std::pair<std::string, int>> kinds(counts.begin(), counts.end());
    std::stable_sort(kinds.begin(), kinds.end(), [&](const auto& a, const auto& b) {
        const bool ab = layerOf[a.first] == "building", bb = layerOf[b.first] == "building";
        return ab != bb ? ab : a.second > b.second; // buildings first: they matter most
    });
    std::map<std::string, Rgb> colourOf;
    size_t next = 0;
    json entries = json::array();
    json placed = json::array(); // each building and prop: its kind and box in the image (the per-thing check)
    auto shapeOf = [&](const std::string& slot) -> std::string {
        if (layerOf[slot] == "building") return "box with a roof (its door a dark rectangle)";
        if (slot == "tree") return "round blob on a trunk";
        if (slot == "rock" || slot == "boulder" || slot == "mound") return "low round lump";
        if (layerOf[slot] == "decal") return "dot flat on the ground";
        if (slot == "bush" || slot == "hedge" || slot == "shrub") return "small round blob";
        return "small box";
    };
    for (const auto& [slot, n] : kinds) {
        if (slot == "grass") continue;
        std::string name;
        if (Plant(slot)) {
            colourOf[slot] = slot == "tree" ? Rgb{44, 74, 40} : slot == "flower" ? Rgb{200, 170, 90} : slot == "crop" ? Rgb{100, 140, 50} : Rgb{58, 96, 50};
            name = slot == "tree" ? "dark green" : slot == "flower" ? "pale yellow" : slot == "crop" ? "leaf green (in rows)" : "mid green";
        } else if (next < std::size(kPalette)) {
            colourOf[slot] = kPalette[next].rgb;
            name = kPalette[next++].name;
        } else {
            colourOf[slot] = ThingColour(slot);
            name = "muted " + Hex(colourOf[slot]);
        }
        std::string what = slot;
        if (ontology != nullptr) {
            if (const std::string draw = ontology->RoleDraw(slot); !draw.empty()) what += ": " + draw;
            if (const auto s = showsOf.find(slot); s != showsOf.end()) {
                if (const ModuleDef* m = ontology->Module(s->second)) what += " (" + m->name + (m->description.empty() ? "" : ": " + m->description) + ")";
            }
        }
        entries.push_back({{"colour", name}, {"hex", Hex(colourOf[slot])}, {"shape", shapeOf(slot)}, {"kind", slot}, {"what", what}, {"count", n}});
    }
    auto colour = [&](const std::string& slot) { const auto it = colourOf.find(slot); return it == colourOf.end() ? ThingColour(slot) : it->second; };
    // The ground.
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const std::string t = terrainAt(x, y);
            Rgb c = TerrainColour(t);
            if (t == "water" && solved.contains("terrain")) {
                for (const auto& b : solved["terrain"].value("bridges", json::array())) {
                    if (b[0].get<int>() == x && b[1].get<int>() == y) c = {120, 84, 50}; // a bridge's planks
                }
            }
            // A little light and dark from the upper left, so the ground isn't one flat sheet.
            const double k = 1.0 + 0.03 * std::sin(x * 0.7 + y * 0.3) + 0.03 * std::cos(x * 0.31 - y * 0.83);
            canvas.Fill({P(x, y), P(x + 1, y), P(x + 1, y + 1), P(x, y + 1)}, Shade(c, k));
        }
    }
    // Everything standing, back to front.
    struct Draw {
        double depth;
        std::function<void()> paint;
    };
    std::vector<Draw> draws;
    auto inside = [&](double x, double y) { return x >= x0 && y >= y0 && x < x1 && y < y1; };
    // Fences and walls along their cells' edges: a strip with posts.
    for (const auto& line : solved.value("lines", json::array())) {
        const std::string slot = line.value("slot", "fence");
        const bool wall = slot.find("wall") != std::string::npos;
        const double up = wall ? (indoor ? th * 0.7 : th * 1.6) : th * 0.45;
        const Rgb c = wall ? Rgb{120, 116, 108} : slot == "hedge" ? Rgb{50, 90, 44} : Rgb{110, 76, 44};
        for (const auto& e : line.value("edges", json::array())) {
            const int ex = e[0].get<int>(), ey = e[1].get<int>();
            const std::string side = e[2].get<std::string>();
            if (!inside(ex, ey)) continue;
            const Pt a = side == "n" ? P(ex, ey) : P(ex, ey), b = side == "n" ? P(ex + 1, ey) : P(ex, ey + 1);
            const double depth = side == "n" ? ex + ey + 0.5 : ex + ey + 0.5;
            draws.push_back({depth, [&canvas, a, b, up, c, wall, side, th] {
                                 const Rgb face = Shade(c, side == "n" ? 0.85 : 1.0);
                                 if (wall) {
                                     canvas.Fill({a, b, {b.x, b.y - up}, {a.x, a.y - up}}, face);
                                     canvas.Fill({{a.x, a.y - up}, {b.x, b.y - up}, {b.x, b.y - up - th * 0.12}, {a.x, a.y - up - th * 0.12}}, Shade(c, 1.2));
                                 } else {
                                     // Two rails and posts at the ends.
                                     for (const double r : {0.35, 0.8}) {
                                         canvas.Fill({{a.x, a.y - up * r}, {b.x, b.y - up * r}, {b.x, b.y - up * r - th * 0.08}, {a.x, a.y - up * r - th * 0.08}}, face);
                                     }
                                     for (const Pt& p : {a, b}) canvas.Fill({{p.x - th * 0.05, p.y}, {p.x + th * 0.05, p.y}, {p.x + th * 0.05, p.y - up}, {p.x - th * 0.05, p.y - up}}, Shade(c, 0.7));
                                 }
                             }});
        }
    }
    for (const auto& o : solved.value("objects", json::array())) {
        const int ox = o.value("x", 0), oy = o.value("y", 0), w = std::max(1, o.value("w", 1)), h = std::max(1, o.value("h", 1));
        if (!inside(ox + w / 2.0, oy + h / 2.0)) continue;
        const std::string layer = o.value("layer", "prop"), slot = o.value("slot", "");
        const double depth = ox + w + oy + h - 1;
        if (layer == "building") {
            // A box as tall as it's big, a hipped roof, its door on the side it faces.
            int floors = o.value("floors", 0);
            std::string roofKind = o.value("roof", "");
            if (const Role* r = ontology ? ontology->FindRole(slot) : nullptr; r && r->look.is_object()) {
                // Its role's storeys (a range: each building its own, the same every time) and roof.
                if (const json f = r->look.value("floors", json()); floors == 0 && f.is_array() && f.size() == 2) {
                    const int lo = f[0].get<int>(), hi = f[1].get<int>();
                    floors = lo + static_cast<int>(std::hash<std::string>{}(o.value("id", "")) % static_cast<size_t>(std::max(1, hi - lo + 1)));
                }
                if (roofKind.empty()) roofKind = r->look.value("roof", "");
            }
            const double wallUp = floors > 0 ? th * 0.6 * floors : th * (1.4 + 0.25 * std::min(w, h));
            const double roofUp = floors > 0 ? (roofKind == "flat" ? th * 0.12 : th * 0.5) : th * (0.9 + 0.3 * std::min(w, h));
            // The whole box in its kind's colour (the roof a little darker): the colour says what it is.
            const Rgb walls = colour(slot), roof = Shade(colour(slot), 0.72);
            {
                const Pt l = P(ox, oy + h), r = P(ox + w, oy), t = P(ox, oy, wallUp + roofUp), b = P(ox + w, oy + h);
                placed.push_back({{"id", o.value("id", "")}, {"kind", slot}, {"layer", "building"},
                                  {"box", {std::floor(l.x), std::floor(t.y), std::ceil(r.x - l.x), std::ceil(b.y - t.y)}}});
            }
            const std::string facing = o.value("facing", "s");
            const bool flatRoof = roofKind == "flat";
            draws.push_back({depth, [=, &canvas, &P] {
                                 const Pt b = P(ox + w, oy), c = P(ox + w, oy + h), d = P(ox, oy + h);
                                 canvas.Fill({d, c, {c.x, c.y - wallUp}, {d.x, d.y - wallUp}}, Shade(walls, 1.0));  // south-west face
                                 canvas.Fill({c, b, {b.x, b.y - wallUp}, {c.x, c.y - wallUp}}, Shade(walls, 0.78)); // south-east face
                                 if (flatRoof) {
                                     // A flat roof: its top, with a low parapet's edge.
                                     const Pt fa = P(ox, oy, wallUp), fb = P(ox + w, oy, wallUp), fc = P(ox + w, oy + h, wallUp), fd = P(ox, oy + h, wallUp);
                                     canvas.Fill({fa, fb, fc, fd}, Shade(roof, 1.25));
                                     const double inset = 0.12;
                                     canvas.Fill({P(ox + inset, oy + inset, wallUp), P(ox + w - inset, oy + inset, wallUp), P(ox + w - inset, oy + h - inset, wallUp),
                                                  P(ox + inset, oy + h - inset, wallUp)}, Shade(roof, 1.05));
                                 }
                                 // The roof: a ridge along the longer side.
                                 const bool alongX = w >= h;
                                 if (!flatRoof) {
                                 const Pt r1 = alongX ? P(ox + 0.5 * h, oy + h / 2.0, wallUp + roofUp) : P(ox + w / 2.0, oy + 0.5 * w, wallUp + roofUp);
                                 const Pt r2 = alongX ? P(ox + w - 0.5 * h, oy + h / 2.0, wallUp + roofUp) : P(ox + w / 2.0, oy + h - 0.5 * w, wallUp + roofUp);
                                 const Pt ta = P(ox, oy, wallUp), tb = P(ox + w, oy, wallUp), tc = P(ox + w, oy + h, wallUp), td = P(ox, oy + h, wallUp);
                                 canvas.Fill({ta, tb, r2, r1}, Shade(roof, 1.15)); // back slopes
                                 canvas.Fill({ta, r1, td}, Shade(roof, 1.05));
                                 canvas.Fill({td, tc, r2, r1}, Shade(roof, alongX ? 1.0 : 0.82));
                                 canvas.Fill({tb, r2, tc}, Shade(roof, alongX ? 0.8 : 1.0));
                                 }
                                 // The door, on a face the view shows.
                                 auto door = [&](Pt a, Pt e) {
                                     const Pt m{(a.x + e.x) / 2, (a.y + e.y) / 2};
                                     const double dw = th * 0.35, dh = wallUp * 0.55;
                                     const double sx = (e.x - a.x) / std::hypot(e.x - a.x, e.y - a.y), sy = (e.y - a.y) / std::hypot(e.x - a.x, e.y - a.y);
                                     canvas.Fill({{m.x - sx * dw, m.y - sy * dw}, {m.x + sx * dw, m.y + sy * dw}, {m.x + sx * dw, m.y + sy * dw - dh}, {m.x - sx * dw, m.y - sy * dw - dh}}, {52, 36, 26});
                                 };
                                 if (facing == "s") door(d, c);
                                 if (facing == "e") door(c, b);
                             }});
            continue;
        }
        const Pt base = P(ox + w / 2.0, oy + h / 2.0);
        if (slot == "tree") {
            const double size = 0.8 + 0.4 * (std::hash<int>{}(ox * 131 + oy) % 100) / 100.0;
            const Rgb crown = colour(slot);
            draws.push_back({depth, [=, &canvas] {
                                 canvas.Fill({{base.x - th * 0.08, base.y}, {base.x + th * 0.08, base.y}, {base.x + th * 0.08, base.y - th * 0.9}, {base.x - th * 0.08, base.y - th * 0.9}}, {84, 60, 40});
                                 canvas.Ellipse(base.x, base.y - th * 1.4 * size, th * 0.75 * size, th * 0.8 * size, crown);
                                 canvas.Ellipse(base.x - th * 0.18, base.y - th * 1.6 * size, th * 0.4 * size, th * 0.4 * size, Shade(crown, 1.3));
                             }});
        } else if (slot == "bush" || slot == "hedge" || slot == "shrub") {
            const Rgb c = colour(slot);
            draws.push_back({depth, [=, &canvas] { canvas.Ellipse(base.x, base.y - th * 0.3, th * 0.45, th * 0.35, c); }});
        } else if (slot == "rock" || slot == "boulder" || slot == "mound") {
            const Rgb c = colour(slot);
            draws.push_back({depth, [=, &canvas] { canvas.Ellipse(base.x, base.y - th * 0.2, th * 0.4, th * 0.28, c); }});
        } else if (layer == "decal") {
            // Flat on the ground: crops in rows, flowers and grass as dots.
            if (slot == "grass") continue; // the ground's own colour says it
            const Rgb c = colour(slot);
            draws.push_back({-1e9 + depth, [=, &canvas] { canvas.Ellipse(base.x, base.y, th * 0.3, th * 0.15, c); }});
        } else {
            // A prop: a small box of its colour (a well, a stall, a pier, a barrel).
            const Rgb c = colour(slot);
            const double up = slot == "pier" ? th * 0.15 : slot == "stall" ? th * 0.9 : slot == "well" ? th * 0.6 : th * 0.45;
            {
                const Pt l = P(ox, oy + h), r = P(ox + w, oy), t = P(ox, oy, up), b = P(ox + w, oy + h);
                placed.push_back({{"id", o.value("id", "")}, {"kind", slot}, {"layer", layer},
                                  {"box", {std::floor(l.x), std::floor(t.y), std::ceil(r.x - l.x), std::ceil(b.y - t.y)}}});
            }
            draws.push_back({depth, [=, &canvas, &P] {
                                 const Pt b = P(ox + w - 0.15, oy + 0.15), c2 = P(ox + w - 0.15, oy + h - 0.15), d = P(ox + 0.15, oy + h - 0.15), a = P(ox + 0.15, oy + 0.15);
                                 canvas.Fill({d, c2, {c2.x, c2.y - up}, {d.x, d.y - up}}, c);
                                 canvas.Fill({c2, b, {b.x, b.y - up}, {c2.x, c2.y - up}}, Shade(c, 0.78));
                                 canvas.Fill({{a.x, a.y - up}, {b.x, b.y - up}, {c2.x, c2.y - up}, {d.x, d.y - up}}, Shade(c, 1.2));
                             }});
        }
    }
    std::stable_sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) { return a.depth < b.depth; });
    for (const auto& d : draws) d.paint();
    if (legend != nullptr) {
        std::map<std::string, int> grounds;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) ++grounds[terrainAt(x, y)];
        }
        json ground = json::array();
        for (const auto& [t, n] : grounds) ground.push_back({{"terrain", t}, {"hex", Hex(TerrainColour(t))}, {"cells", n}});
        std::map<std::string, int> lines;
        for (const auto& l : solved.value("lines", json::array())) ++lines[l.value("slot", "fence")];
        json strips = json::array();
        for (const auto& [slot, n] : lines) {
            strips.push_back({{"kind", slot}, {"shape", slot.find("wall") != std::string::npos ? "tall grey strip along cell edges" : "thin brown strip with posts along cell edges"},
                              {"count", n}});
        }
        *legend = {{"things", entries}, {"ground", ground}, {"lines", strips}, {"placed", placed}, {"size", {width, height}}};
    }
    return canvas.image;
}

} // namespace aige
