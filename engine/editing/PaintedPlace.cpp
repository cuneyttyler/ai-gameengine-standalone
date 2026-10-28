#include "editing/PaintedPlace.h"

#include "assets/TextureCook.h"
#include "editing/PlaceBlockout.h"
#include "editing/PlaceScene.h"
#include "gameplay/Components.h"
#include "gameplay/Interiors.h"
#include "physics2d/Components.h"
#include "places/Ontology.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <map>
#include <optional>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

json ReadJson(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return json();
    try {
        return json::parse(in);
    } catch (const std::exception&) {
        return json();
    }
}

void WriteJsonFile(const fs::path& path, const json& value) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary) << value.dump(2);
}

std::string Now() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &now);
    char stamp[32] = {};
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &local);
    return stamp;
}

// A role's value, or the nearest broader role's that has one.
template <typename Get>
auto Inherited(const Ontology* ontology, std::string role, Get get) -> decltype(get(*static_cast<const Role*>(nullptr))) {
    using T = decltype(get(*static_cast<const Role*>(nullptr)));
    for (int i = 0; i < 8 && ontology != nullptr && !role.empty(); ++i) {
        const Role* r = ontology->FindRole(role);
        if (r == nullptr) break;
        if (T v = get(*r); v != T{}) return v;
        role = r->broader;
    }
    return T{};
}

// How tall a thing of this role stands, in cell widths (its size times its height for its width).
double HeightOf(const Ontology* ontology, const std::string& slot) {
    const double size = Inherited(ontology, slot, [](const Role& r) { return r.size; });
    const double tall = Inherited(ontology, slot, [](const Role& r) { return r.tall; });
    return (size > 0 ? size : 0.6) * (tall > 0 ? tall : 0.8);
}

// The plan's ground, a name per cell.
struct Ground {
    int w = 0, h = 0;
    std::vector<std::string> cells;
    std::set<std::pair<int, int>> bridges;
    const std::string& At(int x, int y) const {
        static const std::string kVoid = "void";
        return x >= 0 && y >= 0 && x < w && y < h ? cells[static_cast<size_t>(y) * w + x] : kVoid;
    }
};

Ground ReadGround(const json& solved) {
    Ground g;
    const json bounds = solved.contains("grid") ? solved["grid"].value("bounds", json::array({0, 0, 0, 0})) : json::array({0, 0, 0, 0});
    g.w = bounds[2].get<int>(), g.h = bounds[3].get<int>();
    g.cells.assign(static_cast<size_t>(std::max(0, g.w * g.h)), "grass");
    if (!solved.contains("terrain")) return g;
    const json palette = solved["terrain"].value("palette", json::array());
    const json rows = solved["terrain"].value("rows", json::array());
    for (int y = 0; y < static_cast<int>(rows.size()) && y < g.h; ++y) {
        int x = 0;
        for (const auto& run : rows[y]) {
            const int count = run[0].get<int>(), index = run[1].get<int>();
            const std::string name = index >= 0 && index < static_cast<int>(palette.size()) ? palette[index].get<std::string>() : "grass";
            for (int k = 0; k < count && x < g.w; ++k, ++x) g.cells[static_cast<size_t>(y) * g.w + x] = name;
        }
    }
    for (const auto& b : solved["terrain"].value("bridges", json::array())) {
        if (b.is_array() && b.size() >= 2) g.bridges.insert({b[0].get<int>(), b[1].get<int>()});
    }
    return g;
}

bool SolidGround(const std::string& t) {
    return t == "void" || t == "water" || t == "deep-water" || t == "lava" || t.find("wall") != std::string::npos ||
           t.find("cliff") != std::string::npos || t == "rock";
}

// The base line's y at column x: along the polyline, flat past its ends.
double BaseY(const json& base, double x) {
    if (!base.is_array() || base.empty()) return 1e9;
    if (base.size() == 1 || x <= base[0][0].get<double>()) return base[0][1].get<double>();
    for (size_t i = 0; i + 1 < base.size(); ++i) {
        const double ax = base[i][0].get<double>(), ay = base[i][1].get<double>();
        const double bx = base[i + 1][0].get<double>(), by = base[i + 1][1].get<double>();
        if (x <= bx) return bx - ax < 1e-6 ? std::max(ay, by) : ay + (by - ay) * (x - ax) / (bx - ax);
    }
    return base.back()[1].get<double>();
}

uint32_t IdAt(const ImageData& ids, int x, int y) {
    const uint8_t* p = &ids.rgba[(static_cast<size_t>(y) * ids.width + x) * 4];
    return p[0] + 256u * p[1] + 65536u * p[2];
}

// A mask of the pixels `inside` says, grown by `grow` px, cut to its box. False when there are none.
template <typename Inside>
bool MaskOf(int width, int height, int x0, int y0, int x1, int y1, int grow, Inside inside, ImageData& mask, std::array<int, 4>& box) {
    x0 = std::max(0, x0), y0 = std::max(0, y0), x1 = std::min(width, x1), y1 = std::min(height, y1);
    int bx0 = x1, by0 = y1, bx1 = x0 - 1, by1 = y0 - 1;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (!inside(x, y)) continue;
            bx0 = std::min(bx0, x), by0 = std::min(by0, y), bx1 = std::max(bx1, x), by1 = std::max(by1, y);
        }
    }
    if (bx1 < bx0) return false;
    bx0 = std::max(0, bx0 - grow), by0 = std::max(0, by0 - grow), bx1 = std::min(width - 1, bx1 + grow), by1 = std::min(height - 1, by1 + grow);
    const int w = bx1 - bx0 + 1, h = by1 - by0 + 1;
    std::vector<uint8_t> on(static_cast<size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) on[static_cast<size_t>(y) * w + x] = inside(bx0 + x, by0 + y) ? 1 : 0;
    }
    mask.width = static_cast<uint32_t>(w), mask.height = static_cast<uint32_t>(h);
    mask.rgba.assign(static_cast<size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool any = false;
            for (int dy = -grow; dy <= grow && !any; ++dy) {
                for (int dx = -grow; dx <= grow && !any; ++dx) {
                    const int sx = x + dx, sy = y + dy;
                    any = sx >= 0 && sy >= 0 && sx < w && sy < h && on[static_cast<size_t>(sy) * w + sx];
                }
            }
            if (!any) continue;
            uint8_t* p = &mask.rgba[(static_cast<size_t>(y) * w + x) * 4];
            p[0] = p[1] = p[2] = p[3] = 255;
        }
    }
    box = {bx0, by0, w, h};
    return true;
}

bool SafeId(const std::string& id) {
    if (id.empty() || id.size() > 40) return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; });
}

} // namespace

PaintView PaintView::From(const json& v) {
    PaintView view;
    if (!v.is_object()) return view;
    view.x0 = v.value("x0", 0), view.y0 = v.value("y0", 0), view.x1 = v.value("x1", 1), view.y1 = v.value("y1", 1);
    view.tw = v.value("tw", 64.0), view.th = v.value("th", 32.0), view.top = v.value("top", 0.0);
    return view;
}

std::pair<double, double> PaintView::Pixel(double x, double y, double up) const {
    const int ch = y1 - y0;
    return {(x - x0 - (y - y0)) * tw / 2 + ch * tw / 2, (x - x0 + (y - y0)) * th / 2 + top - up};
}

std::pair<double, double> PaintView::Cell(double px, double py) const {
    const int ch = y1 - y0;
    const double a = (px - ch * tw / 2) / (tw / 2), b = (py - top) / (th / 2); // x - y, x + y (from the corner)
    return {x0 + (a + b) / 2, y0 + (b - a) / 2};
}

bool PaintedObject(const Ontology* ontology, const json& object) {
    const std::string layer = object.value("layer", "prop");
    if (layer == "building" || layer == "decal") return true;
    const std::string affordance = Inherited(ontology, object.value("slot", ""), [](const Role& r) { return r.affordance; });
    return affordance.empty() || affordance == "environmental-object" || affordance == "terrain";
}

std::set<std::string> PaintedObjectIds(const Ontology* ontology, const json& solved) {
    std::set<std::string> out;
    for (const auto& o : solved.value("objects", json::array())) {
        if (PaintedObject(ontology, o)) out.insert(o.value("id", ""));
    }
    return out;
}

json PrepareBlockout(const json& solved, const Ontology* ontology, const fs::path& folder, int width) {
    // Only what's painted: the things the player uses are sprites over the painting, so they aren't in it.
    json painted = solved;
    painted["objects"] = json::array();
    json ids = json::array();
    for (const auto& o : solved.value("objects", json::array())) {
        if (!PaintedObject(ontology, o)) continue;
        painted["objects"].push_back(o);
        ids.push_back(o.value("id", ""));
    }
    const auto b = OccupiedBounds(solved);
    json legend;
    ImageData idMap;
    const ImageData image = RenderPlanBlockout(painted, b[0], b[1], b[2], b[3], width, ontology, &legend, &idMap);
    std::error_code ec;
    fs::create_directories(folder, ec);
    WritePng(folder / "blockout.png", image);
    WritePng(folder / "blockout-ids.png", idMap);
    WriteJsonFile(folder / "blockout.legend.json", legend);
    json painting = LoadPainting(folder);
    if (!painting.is_object()) painting = json::object();
    painting["size"] = {image.width, image.height};
    painting["view"] = legend.value("view", json::object());
    painting["blockout"] = "blockout.png";
    painting["ids"] = "blockout-ids.png";
    painting["legend"] = "blockout.legend.json";
    painting["objects"] = ids;
    painting["preparedAt"] = Now();
    WriteJsonFile(folder / "painting.json", painting);
    return painting;
}

json LoadPainting(const fs::path& folder) { return ReadJson(folder / "painting.json"); }

void PaintingMade(const fs::path& folder, const json& answer) {
    json painting = LoadPainting(folder);
    if (!painting.is_object()) painting = json::object();
    painting["painting"] = "painting.png";
    painting["provider"] = answer.value("provider", "");
    painting["prompt"] = answer.value("prompt", "");
    painting["seconds"] = answer.value("seconds", 0.0);
    painting["paintedAt"] = Now();
    WriteJsonFile(folder / "painting.json", painting);
}

json PrefillMarks(const json& solved, const Ontology* ontology, const fs::path& folder, bool replaceEdited) {
    const json painting = LoadPainting(folder);
    if (!painting.is_object() || !painting.contains("view")) return json();
    const PaintView view = PaintView::From(painting["view"]);
    const auto idMap = DecodeImage(folder / painting.value("ids", "blockout-ids.png"));
    const int W = painting["size"][0].get<int>(), H = painting["size"][1].get<int>();
    const Ground ground = ReadGround(solved);

    // Walkable: the ground, less what's solid and what stands in the way.
    std::vector<std::string> rows(static_cast<size_t>(ground.h), std::string(static_cast<size_t>(ground.w), '.'));
    auto block = [&](int x, int y) {
        if (x >= 0 && y >= 0 && x < ground.w && y < ground.h) rows[static_cast<size_t>(y)][static_cast<size_t>(x)] = '#';
    };
    for (int y = 0; y < ground.h; ++y) {
        for (int x = 0; x < ground.w; ++x) {
            if (SolidGround(ground.At(x, y)) && !ground.bridges.contains({x, y})) block(x, y);
        }
    }
    const json objects = solved.value("objects", json::array());
    std::map<std::string, const json*> byId;
    for (const auto& o : objects) byId[o.value("id", "")] = &o;
    for (const auto& o : objects) {
        if (!PaintedObject(ontology, o) || o.value("layer", "") == "decal") continue;
        const int ox = o.value("x", 0), oy = o.value("y", 0), w = std::max(1, o.value("w", 1)), h = std::max(1, o.value("h", 1));
        const bool building = o.value("layer", "") == "building";
        if (!building && HeightOf(ontology, o.value("slot", "")) < 0.5) continue; // a bush, flowers: walked past
        for (int y = oy; y < oy + h; ++y) {
            for (int x = ox; x < ox + w; ++x) block(x, y);
        }
    }
    for (const auto& line : solved.value("lines", json::array())) {
        for (const auto& e : line.value("edges", json::array())) block(e[0].get<int>(), e[1].get<int>());
    }

    // What the user already edited stays (unless asked).
    json previous = LoadMarks(folder);
    std::map<std::string, json> kept; // by object id, or by the occluder's own id when drawn by hand
    if (previous.is_object() && !replaceEdited) {
        for (const auto& o : previous.value("occluders", json::array())) {
            if (o.value("edited", false) || !o.value("prefilled", false)) kept[o.contains("object") ? o.value("object", "") : "#" + o.value("id", "")] = o;
        }
    }
    std::error_code ec;
    fs::create_directories(folder / "marks", ec);
    json occluders = json::array();
    int next = 0;
    std::set<std::string> usedIds;
    for (const auto& [key, o] : kept) usedIds.insert(o.value("id", ""));
    auto newId = [&] {
        std::string id;
        do id = "o" + std::to_string(++next);
        while (usedIds.contains(id));
        usedIds.insert(id);
        return id;
    };
    auto add = [&](json occluder, const ImageData& mask) {
        const std::string id = newId();
        occluder["id"] = id;
        occluder["mask"] = "marks/" + id + ".png";
        occluder["prefilled"] = true;
        occluder["edited"] = false;
        WritePng(folder / "marks" / (id + ".png"), mask);
        occluders.push_back(occluder);
    };
    std::map<std::string, int> numbered;
    const json order = painting.value("objects", json::array());
    for (size_t i = 0; i < order.size() && idMap && idMap->width == static_cast<uint32_t>(W); ++i) {
        const std::string oid = order[i].get<std::string>();
        if (auto k = kept.find(oid); k != kept.end()) {
            occluders.push_back(k->second);
            continue;
        }
        const auto it = byId.find(oid);
        if (it == byId.end()) continue;
        const json& o = *it->second;
        const std::string slot = o.value("slot", ""), layer = o.value("layer", "prop");
        const bool building = layer == "building";
        if (layer == "decal" || (!building && HeightOf(ontology, slot) < 0.9)) continue; // only what a character can go behind
        const uint32_t id = static_cast<uint32_t>(i + 1);
        ImageData mask;
        std::array<int, 4> box{};
        if (!MaskOf(W, H, 0, 0, W, H, 2, [&](int x, int y) { return IdAt(*idMap, x, y) == id; }, mask, box)) continue;
        const double ox = o.value("x", 0), oy = o.value("y", 0), w = std::max(1, o.value("w", 1)), h = std::max(1, o.value("h", 1));
        json base;
        if (building) {
            // Its footprint's two front edges: a "V" on screen.
            const auto l = view.Pixel(ox, oy + h), m = view.Pixel(ox + w, oy + h), r = view.Pixel(ox + w, oy);
            base = {{l.first, l.second}, {m.first, m.second}, {r.first, r.second}};
        } else {
            // Through its foot: a tree's trunk, a pole.
            const auto c = view.Pixel(ox + w / 2, oy + h / 2);
            base = {{c.first - view.tw * 0.35, c.second}, {c.first + view.tw * 0.35, c.second}};
        }
        add({{"name", slot + " " + std::to_string(++numbered[slot])}, {"kind", slot}, {"object", oid}, {"box", box}, {"base", base}}, mask);
    }
    // Walls and fences: one occluder per straight run, its base along the run.
    for (const auto& line : solved.value("lines", json::array())) {
        if (!idMap || idMap->width != static_cast<uint32_t>(W)) break;
        const std::string slot = line.value("slot", "fence");
        const bool wall = slot.find("wall") != std::string::npos;
        const double up = (wall ? 1.6 : 0.45) * view.th + view.th * 0.3;
        std::map<std::pair<std::string, int>, std::vector<int>> runs; // (side, the fixed coordinate): the other, sorted
        for (const auto& e : line.value("edges", json::array())) {
            const std::string side = e[2].get<std::string>();
            runs[{side, side == "n" ? e[1].get<int>() : e[0].get<int>()}].push_back(side == "n" ? e[0].get<int>() : e[1].get<int>());
        }
        for (auto& [key, along] : runs) {
            std::sort(along.begin(), along.end());
            for (size_t s = 0; s < along.size();) {
                size_t t = s;
                while (t + 1 < along.size() && along[t + 1] == along[t] + 1) ++t;
                const bool n = key.first == "n";
                const int fixed = key.second, a0 = along[s], a1 = along[t] + 1;
                s = t + 1;
                auto pa = n ? view.Pixel(a0, fixed) : view.Pixel(fixed, a0);
                auto pb = n ? view.Pixel(a1, fixed) : view.Pixel(fixed, a1);
                if (pa.first > pb.first) std::swap(pa, pb);
                const json base = {{pa.first, pa.second}, {pb.first, pb.second}};
                ImageData mask;
                std::array<int, 4> box{};
                const int x0 = static_cast<int>(pa.first) - 2, x1 = static_cast<int>(pb.first) + 3;
                const int y0 = static_cast<int>(std::min(pa.second, pb.second) - up), y1 = static_cast<int>(std::max(pa.second, pb.second)) + 3;
                const bool any = MaskOf(W, H, x0, y0, x1, y1, 1,
                                        [&](int x, int y) {
                                            if (IdAt(*idMap, x, y) != static_cast<uint32_t>(kBlockoutLine)) return false;
                                            const double by = BaseY(base, x);
                                            return y <= by + 2 && y >= by - up;
                                        },
                                        mask, box);
                if (!any) continue;
                add({{"name", slot + " " + std::to_string(++numbered[slot])}, {"kind", slot}, {"line", line.value("id", "")}, {"box", box}, {"base", base}}, mask);
            }
        }
    }
    // Drawn by hand: kept as they are.
    for (const auto& [key, o] : kept) {
        if (key.rfind('#', 0) == 0) occluders.push_back(o);
    }
    json marks = {{"version", 1},
                  {"painting", painting.value("painting", "painting.png")},
                  {"paintedAt", painting.value("paintedAt", "")},
                  {"walk", {{"x0", 0}, {"y0", 0}, {"w", ground.w}, {"h", ground.h}, {"rows", rows}}},
                  {"occluders", occluders}};
    // Masks of the occluders no longer there: gone.
    if (previous.is_object()) {
        std::set<std::string> live;
        for (const auto& o : occluders) live.insert(o.value("mask", ""));
        for (const auto& o : previous.value("occluders", json::array())) {
            if (!live.contains(o.value("mask", "")) && !o.value("mask", "").empty()) fs::remove(folder / o.value("mask", ""), ec);
        }
    }
    WriteJsonFile(folder / "marks.json", marks);
    return marks;
}

json LoadMarks(const fs::path& folder) { return ReadJson(folder / "marks.json"); }

std::string SaveMarks(const fs::path& folder, const json& body) {
    json marks = LoadMarks(folder);
    if (!marks.is_object()) {
        const json painting = LoadPainting(folder);
        if (!painting.is_object()) return "the place isn't painted";
        marks = {{"version", 1}, {"painting", painting.value("painting", "painting.png")}, {"paintedAt", painting.value("paintedAt", "")}};
    }
    std::error_code ec;
    fs::create_directories(folder / "marks", ec);
    if (body.contains("walk") && body["walk"].contains("rows")) marks["walk"] = body["walk"];
    if (body.contains("occluders")) {
        json occluders = json::array();
        for (json o : body["occluders"]) {
            const std::string id = o.value("id", "");
            if (!SafeId(id)) return "an occluder's id can't be a file name: " + id;
            o["mask"] = "marks/" + id + ".png";
            if (const std::string png = o.value("png", ""); !png.empty()) {
                const std::vector<unsigned char> bytes = DecodeBase64(png.substr(png.find(',') == std::string::npos ? 0 : png.find(',') + 1));
                if (bytes.size() < 8) return "the mask of " + id + " couldn't be read";
                std::ofstream(folder / "marks" / (id + ".png"), std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }
            o.erase("png");
            occluders.push_back(o);
        }
        marks["occluders"] = occluders;
    }
    for (const auto& id : body.value("removed", json::array())) {
        if (id.is_string() && SafeId(id.get<std::string>())) fs::remove(folder / "marks" / (id.get<std::string>() + ".png"), ec);
    }
    marks["savedAt"] = Now();
    WriteJsonFile(folder / "marks.json", marks);
    return {};
}

std::string ConfirmMarks(const fs::path& folder) {
    json marks = LoadMarks(folder);
    if (!marks.is_object()) return "nothing is marked yet";
    marks["confirmed"] = true;
    marks["confirmedAt"] = Now();
    WriteJsonFile(folder / "marks.json", marks);
    return {};
}

json MarksSummary(const fs::path& folder) {
    const json painting = LoadPainting(folder);
    const json marks = LoadMarks(folder);
    json out = {{"painted", painting.is_object() && painting.contains("paintedAt")}, {"marked", marks.is_object()}};
    if (painting.is_object()) out["paintedAt"] = painting.value("paintedAt", "");
    if (!marks.is_object()) return out;
    int edited = 0, blocked = 0, walkable = 0;
    for (const auto& o : marks.value("occluders", json::array())) edited += o.value("edited", false) || !o.value("prefilled", false);
    for (const auto& row : marks.value("walk", json::object()).value("rows", json::array())) {
        for (const char c : row.get<std::string>()) (c == '#' ? blocked : walkable) += 1;
    }
    out["occluders"] = marks.value("occluders", json::array()).size();
    out["edited"] = edited;
    out["blocked"] = blocked;
    out["walkable"] = walkable;
    out["stale"] = painting.is_object() && marks.value("paintedAt", "") != painting.value("paintedAt", "");
    out["confirmed"] = marks.value("confirmed", false) && !out["stale"].get<bool>();
    return out;
}

std::vector<std::string> CheckMarks(const json& solved, const Ontology* ontology, const fs::path& folder) {
    std::vector<std::string> out;
    const json marks = LoadMarks(folder);
    if (!marks.is_object()) {
        out.push_back("nothing is marked yet: ⑤ Mark");
        return out;
    }
    std::set<std::string> covered, rooms; // rooms: buildings with an interior, walked into on purpose
    for (const auto& o : marks.value("occluders", json::array())) {
        if (o.contains("object")) covered.insert(o.value("object", ""));
        if (o.contains("object") && o.contains("interior")) rooms.insert(o.value("object", ""));
        const json box = o.value("box", json());
        if (o.value("base", json::array()).size() < 2) out.push_back(o.value("name", o.value("id", "")) + " has no base line");
        if (box.is_array() && box.size() >= 4 && (box[2].get<int>() <= 1 || box[3].get<int>() <= 1)) out.push_back(o.value("name", o.value("id", "")) + "'s region is empty");
    }
    const auto blocked = MarkedBlocked(marks, marks["walk"].value("w", 0), marks["walk"].value("h", 0));
    int uncovered = 0, walkable = 0;
    std::string examples, walkExamples;
    for (const auto& o : solved.value("objects", json::array())) {
        if (!PaintedObject(ontology, o)) continue;
        const bool building = o.value("layer", "") == "building";
        if (!building && HeightOf(ontology, o.value("slot", "")) < 0.9) continue;
        const std::string id = o.value("id", "");
        if (!covered.contains(id) && ++uncovered <= 3) examples += (examples.empty() ? "" : ", ") + o.value("slot", "") + " at " + std::to_string(o.value("x", 0)) + "," + std::to_string(o.value("y", 0));
        if (!building || blocked.empty() || rooms.contains(id)) continue;
        int open = 0;
        for (int y = o.value("y", 0); y < o.value("y", 0) + o.value("h", 1); ++y) {
            for (int x = o.value("x", 0); x < o.value("x", 0) + o.value("w", 1); ++x) {
                if (x >= 0 && y >= 0 && x < static_cast<int>(blocked.size()) && y < static_cast<int>(blocked[0].size()) && !blocked[x][y]) ++open;
            }
        }
        if (open > 0 && ++walkable <= 3) walkExamples += (walkExamples.empty() ? "" : ", ") + o.value("slot", "") + " at " + std::to_string(o.value("x", 0)) + "," + std::to_string(o.value("y", 0));
    }
    if (uncovered) out.push_back(std::to_string(uncovered) + " tall thing(s) with no occluder: " + examples + (uncovered > 3 ? "…" : ""));
    if (walkable) out.push_back(std::to_string(walkable) + " building(s) that can be walked through: " + walkExamples + (walkable > 3 ? "…" : ""));
    return out;
}

std::vector<std::vector<bool>> MarkedBlocked(const json& marks, int w, int h) {
    std::vector<std::vector<bool>> out;
    if (!marks.is_object() || !marks.contains("walk")) return out;
    out.assign(static_cast<size_t>(std::max(0, w)), std::vector<bool>(static_cast<size_t>(std::max(0, h)), false));
    const json rows = marks["walk"].value("rows", json::array());
    for (int y = 0; y < h && y < static_cast<int>(rows.size()); ++y) {
        const std::string row = rows[y].get<std::string>();
        for (int x = 0; x < w && x < static_cast<int>(row.size()); ++x) out[static_cast<size_t>(x)][static_cast<size_t>(y)] = row[static_cast<size_t>(x)] == '#';
    }
    // Buildings with an interior: walked into through their door (docs/places/interiors.md §7); a scene interior's
    // building: solid but its doorway, which leads to its scene.
    for (const auto& o : marks.value("occluders", json::array())) {
        const json cells = o.value("cells", json());
        const bool scene = o.value("interiorKind", "") == "scene";
        if ((!o.contains("interior") && !scene) || !cells.is_array() || cells.size() < 4) continue;
        const int x0 = cells[0].get<int>(), y0 = cells[1].get<int>(), x1 = cells[2].get<int>(), y1 = cells[3].get<int>();
        auto set = [&](int x, int y, bool blocked) {
            if (x >= 0 && y >= 0 && x < w && y < h) out[static_cast<size_t>(x)][static_cast<size_t>(y)] = blocked;
        };
        if (scene) {
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) set(x, y, true);
            }
            const auto [dx, dy] = DoorwayOf(x0, y0, x1, y1, o.value("door", std::string("s")));
            set(dx, dy, false);
            continue;
        }
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) set(x, y, false);
        }
        ForEachInteriorWall(x1, y1, x1 - x0, 0, y1 - y0, o.value("door", std::string("s")), [&](int x, int y) { set(x, y, true); });
    }
    return out;
}

json LoadInteriorChoices(const fs::path& folder) {
    const json choices = ReadJson(folder / "interiors.json");
    return choices.is_object() ? choices : json::object();
}

std::string SetInteriorChoice(const fs::path& folder, const std::string& role, const std::string& kind) {
    if (role.empty()) return "no building type";
    if (kind != "cutaway" && kind != "scene" && kind != "none" && !kind.empty()) return "an interior is cutaway, scene or none";
    json choices = LoadInteriorChoices(folder);
    if (kind.empty()) choices.erase(role); // back to its kind's default
    else choices[role] = kind;
    WriteJsonFile(folder / "interiors.json", choices);
    return {};
}

std::string InteriorKindOf(const Ontology* ontology, const json& choices, const std::string& role) {
    if (ontology == nullptr || ontology->RoleLook(role, "has_interior") != true) return {};
    if (const std::string chosen = choices.value(role, std::string()); chosen == "cutaway" || chosen == "scene" || chosen == "none") return chosen;
    const json kind = ontology->RoleLook(role, "interior_kind");
    return kind.is_string() ? kind.get<std::string>() : "cutaway";
}

json InteriorChoices(const json& solved, const Ontology* ontology, const fs::path& folder) {
    json out = json::array();
    if (ontology == nullptr) return out;
    const json choices = LoadInteriorChoices(folder);
    std::map<std::string, int> counts;
    for (const auto& o : solved.value("objects", json::array())) {
        if (o.value("layer", "") == "building") ++counts[o.value("role", o.value("slot", ""))];
    }
    for (const auto& [role, count] : counts) {
        if (ontology->RoleLook(role, "has_interior") != true) continue;
        const json kind = ontology->RoleLook(role, "interior_kind");
        const Role* r = ontology->FindRole(role);
        out.push_back({{"role", role}, {"label", r ? r->label : role}, {"count", count}, {"kind", InteriorKindOf(ontology, choices, role)},
                       {"default", kind.is_string() ? kind.get<std::string>() : "cutaway"}});
    }
    return out;
}

json InteriorRequests(const json& solved, const Ontology* ontology, const fs::path& folder) {
    json out = json::array();
    const json marks = LoadMarks(folder);
    if (!marks.is_object() || ontology == nullptr) return out;
    const json choices = LoadInteriorChoices(folder);
    const json objects = solved.value("objects", json::array());
    std::map<std::string, const json*> byId;
    for (const auto& o : objects) byId[o.value("id", "")] = &o;
    std::error_code ec;
    for (const auto& o : marks.value("occluders", json::array())) {
        const std::string id = o.value("id", "");
        const auto it = byId.find(o.value("object", ""));
        if (o.value("hidden", false) || !SafeId(id) || it == byId.end() || it->second->value("layer", "") != "building") continue;
        const json& object = *it->second;
        const std::string role = object.value("role", object.value("slot", ""));
        if (InteriorKindOf(ontology, choices, role) != "cutaway") continue;
        if (fs::exists(folder / "marks" / (id + "-interior.png"), ec)) continue;
        out.push_back({{"id", id}, {"box", o.value("box", json::array())}, {"mask", o.value("mask", "marks/" + id + ".png")}, {"role", role},
                       {"purpose", object.value("purpose", json())}});
    }
    return out;
}

int PairInteriorMarks(const json& solved, const fs::path& folder, const Ontology* ontology) {
    json marks = LoadMarks(folder);
    if (!marks.is_object()) return 0;
    const json choices = LoadInteriorChoices(folder);
    const json objects = solved.value("objects", json::array());
    std::map<std::string, const json*> byId;
    for (const auto& o : objects) byId[o.value("id", "")] = &o;
    std::error_code ec;
    int count = 0;
    for (auto& o : marks["occluders"]) {
        const std::string id = o.value("id", "");
        const auto it = byId.find(o.value("object", ""));
        o.erase("interiorKind");
        const bool scene = it != byId.end() && it->second->value("layer", "") == "building" &&
                           InteriorKindOf(ontology, choices, it->second->value("role", it->second->value("slot", ""))) == "scene";
        if (!SafeId(id) || it == byId.end() || (!scene && !fs::exists(folder / "marks" / (id + "-interior.png"), ec))) {
            o.erase("interior"), o.erase("cells"), o.erase("door");
            continue;
        }
        const json& object = *it->second;
        const int x = object.value("x", 0), y = object.value("y", 0), w = std::max(1, object.value("w", 1)), h = std::max(1, object.value("h", 1));
        if (scene) {
            o.erase("interior");
            o["interiorKind"] = "scene";
            o["cells"] = {x, y, x + w - 1, y + h - 1};
            o["door"] = object.value("facing", std::string("s"));
            ++count;
            continue;
        }
        o["interior"] = "marks/" + id + "-interior.png";
        std::string upper; // its upper floors' pictures, while each is there
        for (int k = 2; k <= 4 && fs::exists(folder / "marks" / (id + "-interior-" + std::to_string(k) + ".png"), ec); ++k) {
            upper += (upper.empty() ? "" : ";") + std::string("marks/") + id + "-interior-" + std::to_string(k) + ".png";
        }
        if (upper.empty()) o.erase("floors");
        else o["floors"] = upper;
        o["cells"] = {x, y, x + w - 1, y + h - 1};
        o["door"] = object.value("facing", std::string("s"));
        ++count;
    }
    WriteJsonFile(folder / "marks.json", marks);
    return count;
}

json AddPaintingEntities(Registry& registry, const fs::path& folder, const std::string& relative, const std::string& place, int originX,
                         int originY, const IsoParams& iso) {
    const json painting = LoadPainting(folder);
    if (!painting.is_object() || !painting.contains("paintedAt")) return {{"error", "the place isn't painted"}};
    const auto image = DecodeImage(folder / painting.value("painting", "painting.png"));
    if (!image || image->width == 0) return {{"error", "the painting can't be read"}};
    const PaintView view = PaintView::From(painting["view"]);
    // A painting pixel in the scene: the view's cell under it, at the project's tile size.
    const double kx = iso.tileWidth / view.tw, ky = iso.tileHeight / view.th;
    const auto corner = view.Cell(0, 0);
    const ScreenPose a = ProjectIso(static_cast<float>(corner.first + originX), static_cast<float>(corner.second + originY), iso);
    auto scene = [&](double px, double py) { return std::pair<double, double>{a.x + px * kx, a.y + py * ky}; };
    json out = {{"backdrop", false}, {"occluders", 0}, {"strips", 0}};

    {
        const Entity back = registry.create();
        const auto c = scene(image->width / 2.0, image->height / 2.0);
        registry.emplace<ScreenTransform2D>(back, static_cast<float>(c.first), static_cast<float>(c.second), -1.0e7f);
        Sprite s;
        s.texture = relative + "/" + painting.value("painting", "painting.png");
        s.width = static_cast<float>(image->width * kx);
        s.height = static_cast<float>(image->height * ky);
        registry.emplace<Sprite>(back, s);
        registry.emplace<PlaceNode>(back, place, "", "painting#backdrop");
        registry.emplace<Name>(back, "Painting");
        out["backdrop"] = true;
    }

    const json marks = LoadMarks(folder);
    if (!marks.is_object()) return out;
    std::error_code ec;
    fs::create_directories(folder / "occluders", ec);
    const int strip = std::max(4, static_cast<int>(std::lround(view.tw / 2)));
    for (const auto& o : marks.value("occluders", json::array())) {
        if (o.value("hidden", false)) continue;
        const std::string id = o.value("id", "");
        const json box = o.value("box", json());
        const json base = o.value("base", json());
        if (!SafeId(id) || !box.is_array() || box.size() < 4 || !base.is_array() || base.empty()) continue;
        const auto mask = DecodeImage(folder / o.value("mask", "marks/" + id + ".png"));
        if (!mask || mask->width == 0) continue;
        const int bx = box[0].get<int>(), by = box[1].get<int>();
        const int bw = static_cast<int>(mask->width), bh = static_cast<int>(mask->height);
        // The painting's pixels under the mask, above the base line (below it is ground in front of the thing).
        ImageData cut;
        cut.width = mask->width, cut.height = mask->height;
        cut.rgba.assign(static_cast<size_t>(bw) * bh * 4, 0);
        std::vector<int> top(static_cast<size_t>(bw), bh), bottom(static_cast<size_t>(bw), -1);
        for (int x = 0; x < bw; ++x) {
            const int px = bx + x;
            if (px < 0 || px >= static_cast<int>(image->width)) continue;
            const double baseY = BaseY(base, px + 0.5);
            for (int y = 0; y < bh; ++y) {
                const int py = by + y;
                if (py < 0 || py >= static_cast<int>(image->height) || py > baseY) continue;
                if (mask->rgba[(static_cast<size_t>(y) * bw + x) * 4 + 3] < 128) continue;
                const uint8_t* src = &image->rgba[(static_cast<size_t>(py) * image->width + px) * 4];
                uint8_t* dst = &cut.rgba[(static_cast<size_t>(y) * bw + x) * 4];
                std::copy_n(src, 3, dst);
                dst[3] = 255;
                top[static_cast<size_t>(x)] = std::min(top[static_cast<size_t>(x)], y);
                bottom[static_cast<size_t>(x)] = std::max(bottom[static_cast<size_t>(x)], y);
            }
        }
        const std::string file = "occluders/" + id + ".png";
        if (!WritePng(folder / file, cut)) continue;
        int strips = 0;
        for (int c0 = 0; c0 < bw; c0 += strip) {
            const int c1 = std::min(bw, c0 + strip);
            int t = bh, b = -1;
            for (int x = c0; x < c1; ++x) t = std::min(t, top[static_cast<size_t>(x)]), b = std::max(b, bottom[static_cast<size_t>(x)]);
            if (b < t) continue;
            // Its depth: the base line under the strip's middle, on the ground, as a sprite standing there would have.
            const double mid = bx + (c0 + c1) / 2.0;
            const auto cell = view.Cell(mid, BaseY(base, mid));
            const float depth = static_cast<float>(cell.first + cell.second + originX + originY + 0.49);
            const auto c = scene(mid, by + (t + b + 1) / 2.0);
            const Entity e = registry.create();
            registry.emplace<ScreenTransform2D>(e, static_cast<float>(c.first), static_cast<float>(c.second), depth);
            Sprite s;
            s.texture = relative + "/" + file;
            s.width = static_cast<float>((c1 - c0) * kx);
            s.height = static_cast<float>((b + 1 - t) * ky);
            s.u0 = static_cast<float>(c0) / bw, s.u1 = static_cast<float>(c1) / bw;
            s.v0 = static_cast<float>(t) / bh, s.v1 = static_cast<float>(b + 1) / bh;
            registry.emplace<Sprite>(e, s);
            registry.emplace<PlaceNode>(e, place, "", "painting#" + id + "/" + std::to_string(c0 / strip));
            ++strips;
        }
        if (strips == 0) continue;
        out["occluders"] = out["occluders"].get<int>() + 1;
        // Its interior (docs/places/interiors.md §7): the cutaway over the painted building, seen while the player is
        // inside, its occluder's strips hidden meanwhile; or, a scene interior, its doorway (a scene's way in).
        const json cells = o.value("cells", json());
        if (o.value("interiorKind", "") == "scene" && cells.is_array() && cells.size() >= 4) {
            const int x0 = cells[0].get<int>() + originX, y0 = cells[1].get<int>() + originY;
            const int x1 = cells[2].get<int>() + originX, y1 = cells[3].get<int>() + originY;
            const Entity room = registry.create();
            registry.emplace<WorldTransform2D>(room, (x0 + x1 + 1) / 2.0f, (y0 + y1 + 1) / 2.0f);
            Interior interior;
            interior.kind = "scene";
            interior.door = o.value("door", std::string("s"));
            interior.cells = std::to_string(x0) + "," + std::to_string(y0) + "," + std::to_string(x1) + "," + std::to_string(y1);
            registry.emplace<Interior>(room, interior);
            registry.emplace<PlaceNode>(room, place, "", "painting#" + id + "-interior");
            registry.emplace<Name>(room, o.value("name", id) + " (its interior scene)");
            out["interiors"] = out.value("interiors", 0) + 1;
        }
        if (o.contains("interior") && cells.is_array() && cells.size() >= 4) {
            const int x0 = cells[0].get<int>() + originX, y0 = cells[1].get<int>() + originY;
            const int x1 = cells[2].get<int>() + originX, y1 = cells[3].get<int>() + originY;
            const auto c = scene(bx + bw / 2.0, by + bh / 2.0);
            const Entity room = registry.create();
            // Behind whoever stands in it: its back corner's depth.
            registry.emplace<ScreenTransform2D>(room, static_cast<float>(c.first), static_cast<float>(c.second), static_cast<float>(x0 + y0) - 0.5f);
            Sprite s;
            s.texture = relative + "/" + o.value("interior", "");
            s.width = static_cast<float>(bw * kx);
            s.height = static_cast<float>(bh * ky);
            s.a = 0.0f;
            registry.emplace<Sprite>(room, s);
            Interior interior;
            interior.texture = s.texture;
            interior.door = o.value("door", std::string("s"));
            interior.cells = std::to_string(x0) + "," + std::to_string(y0) + "," + std::to_string(x1) + "," + std::to_string(y1);
            interior.hides = "painting#" + id + "/";
            for (size_t at = 0; at < o.value("floors", std::string()).size();) { // the upper floors, as the scene names them
                const std::string all = o.value("floors", std::string());
                const size_t end = all.find(';', at);
                interior.floors += (interior.floors.empty() ? "" : ";") + relative + "/" + all.substr(at, end == std::string::npos ? std::string::npos : end - at);
                at = end == std::string::npos ? all.size() : end + 1;
            }
            registry.emplace<Interior>(room, interior);
            registry.emplace<PlaceNode>(room, place, "", "painting#" + id + "-interior");
            registry.emplace<Name>(room, o.value("name", id) + " interior");
            out["interiors"] = out.value("interiors", 0) + 1;
        }
        out["strips"] = out["strips"].get<int>() + strips;
    }
    return out;
}

std::vector<unsigned char> DecodeBase64(const std::string& text) {
    static const std::string kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<unsigned char> out;
    out.reserve(text.size() * 3 / 4);
    unsigned value = 0;
    int bits = -8;
    for (const char c : text) {
        if (c == '=') break;
        const size_t k = kAlphabet.find(c);
        if (k == std::string::npos) continue; // line breaks, spaces
        value = (value << 6) | static_cast<unsigned>(k);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<unsigned char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

} // namespace aige
