#include "editing/PlacePaint.h"

#include "assets/TextureCook.h"
#include "editing/PlaceBlockout.h"
#include "editing/PlaceBuild.h"
#include "editing/PlaceInspect.h"
#include "gameplay/Components.h"
#include "gameplay/Isometric.h"
#include "gameplay/TileLayer.h"
#include "physics2d/Components.h"
#include "places/Ontology.h"
#include "renderer2d/Components.h"
#include "renderer2d/ImageData.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>

namespace aige {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

// Where a cell lies in an area, in compass words (north is the diamond's top corner: low x and y).
std::string Compass(double x, double y, int x0, int y0, int x1, int y1) {
    const double u = (x - x0) / std::max(1, x1 - x0), v = (y - y0) / std::max(1, y1 - y0);
    const double right = u - v, down = u + v; // on screen: -1..1 across, 0..2 down
    std::string ns = down < 0.7 ? "north" : down > 1.3 ? "south" : "";
    std::string ew = right < -0.3 ? "west" : right > 0.3 ? "east" : "";
    if (ns.empty() && ew.empty()) return "in the middle";
    return "in the " + ns + (ns.empty() || ew.empty() ? "" : "-") + ew;
}

} // namespace

json BackdropRequests(const PlaceBuild& build, const Ontology* ontology, const json& anchors) {
    const json& solved = build.Solved();
    const json brief = build.Brief();
    const json story = build.Story().is_object() ? build.Story() : json::object();
    const json look = LookFor(brief.value("buildType", ""));
    std::map<std::string, std::string> moduleOf;
    std::function<void(const json&)> walk = [&](const json& n) {
        if (!n.is_object()) return;
        if (n.contains("id")) moduleOf[n.value("id", "")] = n.value("module", "");
        for (const auto& c : n.value("children", json::array())) walk(c);
    };
    if (build.Spec().is_object() && build.Spec().contains("root")) walk(build.Spec()["root"]);
    auto nameOf = [&](const std::string& module) {
        const ModuleDef* m = ontology ? ontology->Module(module) : nullptr;
        return m ? m->name : module;
    };
    auto request = [&](const std::string& level, int x0, int y0, int x1, int y1) {
        // The zones (or rooms) here, where each lies.
        json zones = json::array();
        for (const auto& r : solved.value("regions", json::array())) {
            const std::string id = r.value("id", ""), module = r.value("module", "");
            if (module.empty()) continue;
            double sx = 0, sy = 0;
            int n = 0;
            for (const auto& run : r.value("cells", json::array())) {
                for (int x = run[1].get<int>(); x < run[2].get<int>(); ++x) sx += x, sy += run[0].get<int>(), ++n;
            }
            if (n == 0) continue;
            const double cx = sx / n, cy = sy / n;
            if (cx < x0 || cy < y0 || cx >= x1 || cy >= y1) continue;
            const ModuleDef* m = ontology ? ontology->Module(module) : nullptr;
            const bool big = m && (m->level == PlaceLevel::Zone || m->level == PlaceLevel::Parcel);
            if (!big && zones.size() > 40) continue;
            zones.push_back({{"id", id}, {"is", m ? m->name + ": " + m->description : module}, {"where", Compass(cx, cy, x0, y0, x1, y1)},
                             {"size", n}});
        }
        // The buildings by kind, then every other kind of thing, each with where its copies are (an
        // image model follows a few counted groups; it loses a long list of single things).
        json items = json::array();
        std::map<std::string, std::map<std::string, int>> spread, buildings;
        std::map<std::string, std::string> drawn;
        for (const auto& o : solved.value("objects", json::array())) {
            const int w = o.value("w", 1), h = o.value("h", 1);
            const double cx = o.value("x", 0) + w / 2.0, cy = o.value("y", 0) + h / 2.0;
            if (cx < x0 || cy < y0 || cx >= x1 || cy >= y1) continue;
            const std::string where = Compass(cx, cy, x0, y0, x1, y1);
            if (o.value("layer", "") == "building") {
                const std::string module = moduleOf[o.value("node", "")];
                ++buildings[nameOf(module).empty() ? "building" : nameOf(module)][where];
                continue;
            }
            if (o.value("layer", "") == "decal") continue; // flowers, cracks: the painting's own detail
            std::string kind = o.value("slot", "thing");
            if (!o.value("shows", "").empty()) kind += " (" + o.value("shows", "") + ")";
            ++spread[kind][where];
            if (!drawn.contains(kind) && ontology) {
                if (const Role* r = ontology->FindRole(o.value("slot", ""))) drawn[kind] = r->draw;
            }
        }
        auto spreadOf = [](const std::map<std::string, int>& places, int& total) {
            std::string where;
            for (const auto& [w, n] : places) {
                total += n;
                where += (where.empty() ? "" : ", ") + std::to_string(n) + " " + w;
            }
            return where;
        };
        for (const auto& [kind, places] : buildings) {
            int total = 0;
            const std::string where = spreadOf(places, total);
            items.push_back({{"key", kind}, {"type", kind + " (building)"}, {"count", total}, {"where", where}});
        }
        for (const auto& [kind, places] : spread) {
            int total = 0;
            std::string where;
            for (const auto& [w, n] : places) {
                total += n;
                where += (where.empty() ? "" : ", ") + std::to_string(n) + " " + w;
            }
            json item = {{"key", kind}, {"type", kind}, {"count", total}, {"where", where}};
            if (!drawn[kind].empty()) item["drawn"] = drawn[kind];
            items.push_back(item);
        }
        // The ground's big lines first: the river, the roads, the bridges (where they run).
        json ground = json::array();
        for (const auto& path : solved.value("paths", json::array())) {
            std::vector<std::pair<double, double>> in;
            for (const auto& c : path.value("cells", json::array())) {
                if (c[0].get<int>() >= x0 && c[1].get<int>() >= y0 && c[0].get<int>() < x1 && c[1].get<int>() < y1) in.push_back({c[0].get<double>(), c[1].get<double>()});
            }
            if (in.size() < 3) continue;
            std::string course;
            for (const size_t i : {size_t{0}, in.size() / 4, in.size() / 2, 3 * in.size() / 4, in.size() - 1}) {
                const std::string w = Compass(in[i].first, in[i].second, x0, y0, x1, y1);
                if (course.find(w) == std::string::npos) course += (course.empty() ? "" : " -> ") + w;
            }
            ground.push_back({{"what", path.value("class", "path") + " (" + path.value("terrain", "") + ", " + std::to_string(path.value("width", 1)) + " cells wide)"},
                              {"runs", course}});
        }
        if (solved.contains("terrain")) {
            for (const auto& b : solved["terrain"].value("bridges", json::array())) {
                if (b[0].get<int>() >= x0 && b[1].get<int>() >= y0 && b[0].get<int>() < x1 && b[1].get<int>() < y1) {
                    ground.push_back({{"what", "a bridge where the road crosses the water"}, {"runs", Compass(b[0].get<double>(), b[1].get<double>(), x0, y0, x1, y1)}});
                    break;
                }
            }
        }
        json context = {{"place", {{"name", brief.value("name", "")}, {"type", brief.value("buildType", "")}, {"style", brief.value("style", json::object())}}},
                        {"look", look},
                        {"story", {{"purpose", story.value("purpose", json())}, {"mood", story.value("mood", json())}, {"logline", story.value("logline", json())}}},
                        {"ground", ground},
                        {"zones", zones},
                        {"items", items},
                        {"anchors", anchors.is_array() ? anchors : json::array()},
                        {"light", look.value("lighting", "sun")}};
        if (!level.empty()) context["level"] = level + " (one level of the place: only it is painted)";
        const std::string file = level.empty() ? "backdrop.png" : "backdrop-" + level + ".png";
        return json{{"context", context}, {"out", "places/" + build.Id() + "/" + file}, {"level", level}, {"area", {x0, y0, x1, y1}}};
    };
    json out = json::array();
    const auto levels = ontology ? PlaceLevelAreas(*ontology, build.Spec(), solved) : std::vector<PlaceLevelArea>{};
    if (levels.empty()) {
        const auto [x0, y0, x1, y1] = OccupiedBounds(solved);
        out.push_back(request("", x0, y0, x1, y1));
    } else {
        for (const auto& l : levels) out.push_back(request(l.id, l.x0, l.y0, l.x1, l.y1));
    }
    return out;
}

json BlockoutPaintRequests(const PlaceBuild& build, const Ontology* ontology, int width) {
    const json& solved = build.Solved();
    const json brief = build.Brief();
    const json story = build.Story().is_object() ? build.Story() : json::object();
    const fs::path root = build.Folder().parent_path().parent_path();
    auto request = [&](const std::string& level, int x0, int y0, int x1, int y1) {
        json legend;
        const ImageData image = RenderPlanBlockout(solved, x0, y0, x1, y1, width, ontology, &legend);
        const std::string suffix = level.empty() ? "" : "-" + level;
        const std::string png = "places/" + build.Id() + "/blockout" + suffix + ".png", key = "places/" + build.Id() + "/blockout" + suffix + ".legend.json";
        WritePng(root / png, image);
        std::ofstream(root / key, std::ios::binary) << legend.dump(2);
        const json body = {{"blockout", png},
                           {"legend", legend},
                           {"place", {{"name", brief.value("name", "")}, {"type", brief.value("buildType", "")}}},
                           {"story", {{"purpose", story.value("purpose", json())}, {"mood", story.value("mood", json())}, {"logline", story.value("logline", json())}}},
                           {"look", LookFor(brief.value("buildType", ""))},
                           {"out", "places/" + build.Id() + "/painting" + suffix + ".png"}};
        return json{{"path", "/places/paint-blockout"}, {"body", body}, {"level", level}, {"area", {x0, y0, x1, y1}}, {"blockout", png}, {"legend", key}};
    };
    json out = json::array();
    const auto levels = ontology ? PlaceLevelAreas(*ontology, build.Spec(), solved) : std::vector<PlaceLevelArea>{};
    if (levels.empty()) {
        const auto [x0, y0, x1, y1] = OccupiedBounds(solved);
        out.push_back(request("", x0, y0, x1, y1));
    } else {
        for (const auto& l : levels) out.push_back(request(l.id, l.x0, l.y0, l.x1, l.y1));
    }
    return out;
}

json BuildBackdropScene(Registry& registry, const PlaceBuild& build, const json& backdrops, const PlaceSceneOptions& options) {
    const IsoParams& iso = options.iso;
    // The projection and its inverse (worked out from it: no assumptions on its form).
    const ScreenPose p0 = ProjectIso(0, 0, iso), px = ProjectIso(1, 0, iso), py = ProjectIso(0, 1, iso);
    const double ax = px.x - p0.x, ay = px.y - p0.y, bx = py.x - p0.x, by = py.y - p0.y, det = ax * by - ay * bx;
    auto toWorld = [&](double sx, double sy) {
        const double dx = sx - p0.x, dy = sy - p0.y;
        return std::pair<double, double>{(dx * by - dy * bx) / det, (ax * dy - ay * dx) / det};
    };
    TileLayer& walk = LayersOf(registry).Get("walk");
    TileDef open, blocked;
    open.sprite.a = 0.0f; // invisible: the painting shows the ground
    blocked.sprite.a = 0.0f;
    blocked.solid = true;
    const uint16_t openId = walk.Intern(open), blockedId = walk.Intern(blocked);
    json report = {{"open", 0}, {"blocked", 0}, {"occluders", 0}, {"markers", 0}, {"backdrops", 0}};
    int part = 0;
    for (const auto& b : backdrops) {
        const std::string file = b.value("backdrop", ""), level = b.value("level", "");
        const auto image = DecodeImage(build.Folder().parent_path().parent_path() / file);
        if (!image || image->width == 0) continue;
        const json area = b.value("area", json::array({0, 0, 1, 1}));
        const int x0 = area[0].get<int>() + options.originX, y0 = area[1].get<int>() + options.originY;
        const int x1 = area[2].get<int>() + options.originX, y1 = area[3].get<int>() + options.originY;
        // The painting over the area's diamond: as wide as it, centred on it.
        float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        for (const auto& [cx, cy] : {std::pair{x0, y0}, std::pair{x1, y0}, std::pair{x0, y1}, std::pair{x1, y1}}) {
            const ScreenPose p = ProjectIso(static_cast<float>(cx), static_cast<float>(cy), iso);
            minX = std::min(minX, p.x), minY = std::min(minY, p.y), maxX = std::max(maxX, p.x), maxY = std::max(maxY, p.y);
        }
        const double iw = image->width, ih = image->height, k = (maxX - minX) / iw;
        const double left = minX, top = (minY + maxY) / 2.0 - ih * k / 2.0;
        auto pixelToWorld = [&](double pxl, double pyl) { return toWorld(left + pxl * k, top + pyl * k); };
        auto worldToPixel = [&](double wx, double wy) {
            const ScreenPose s = ProjectIso(static_cast<float>(wx), static_cast<float>(wy), iso);
            return std::pair<double, double>{(s.x - left) / k, (s.y - top) / k};
        };
        // The painting itself, at the back of everything.
        const Entity back = registry.create();
        registry.emplace<ScreenTransform2D>(back, static_cast<float>(left + iw * k / 2), static_cast<float>(top + ih * k / 2), -1.0e7f);
        Sprite s;
        s.texture = file;
        s.width = static_cast<float>(iw * k);
        s.height = static_cast<float>(ih * k);
        registry.emplace<Sprite>(back, s);
        registry.emplace<PlaceNode>(back, options.place, level, "backdrop#" + level);
        registry.emplace<Name>(back, "Backdrop " + (level.empty() ? options.place : level));
        report["backdrops"] = report["backdrops"].get<int>() + 1;
        // What stands where (the map): blocked on its footprint.
        const json map = b.value("map", json::object());
        const json elements = map.value("elements", json::array());
        auto blockedAt = [&](double pxl, double pyl) {
            for (const auto& e : elements) {
                if (e.value("walkable", false)) continue;
                const json f = e.value("footprint", e.value("box", json()));
                if (!f.is_array() || f.size() < 4) continue;
                if (pxl >= f[0].get<double>() && pyl >= f[1].get<double>() && pxl < f[0].get<double>() + f[2].get<double>() &&
                    pyl < f[1].get<double>() + f[3].get<double>()) {
                    return true;
                }
            }
            const int ix = std::clamp(static_cast<int>(pxl), 0, static_cast<int>(iw) - 1), iy = std::clamp(static_cast<int>(pyl), 0, static_cast<int>(ih) - 1);
            const uint8_t* c = &image->rgba[(static_cast<size_t>(iy) * image->width + ix) * 4];
            return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2] < 14.0; // the dark round a dungeon's rooms, the void
        };
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto [pxl, pyl] = worldToPixel(x + 0.5, y + 0.5);
                if (pxl < 0 || pyl < 0 || pxl >= iw || pyl >= ih) continue; // off the painting: nothing (solid)
                const bool no = blockedAt(pxl, pyl);
                walk.Set(x, y, no ? blockedId : openId);
                report[no ? "blocked" : "open"] = report[no ? "blocked" : "open"].get<int>() + 1;
            }
        }
        // Each tall thing cut out of the painting: drawn by depth, so a character goes behind it.
        for (const auto& e : elements) {
            const std::string kind = e.value("kind", "");
            if (kind != "building" && kind != "wall" && kind != "tree" && kind != "wood" && kind != "cliff" && kind != "rock") continue;
            const json bb = e.value("box", json()), f = e.value("footprint", json());
            if (!bb.is_array() || bb.size() < 4 || !f.is_array() || f.size() < 4) continue;
            const int cx0 = std::max(0, static_cast<int>(bb[0].get<double>())), cy0 = std::max(0, static_cast<int>(bb[1].get<double>()));
            const int foot = static_cast<int>(f[1].get<double>() + f[3].get<double>());
            const int cx1 = std::min(static_cast<int>(iw), static_cast<int>(bb[0].get<double>() + bb[2].get<double>()));
            const int cy1 = std::min(static_cast<int>(ih), foot);
            if (cx1 - cx0 < 6 || cy1 - cy0 < 6) continue;
            ImageData cut;
            cut.width = static_cast<uint32_t>(cx1 - cx0);
            cut.height = static_cast<uint32_t>(cy1 - cy0);
            cut.rgba.resize(static_cast<size_t>(cut.width) * cut.height * 4);
            for (int yy = 0; yy < static_cast<int>(cut.height); ++yy) {
                std::copy_n(&image->rgba[(static_cast<size_t>(cy0 + yy) * image->width + cx0) * 4], cut.width * 4, &cut.rgba[static_cast<size_t>(yy) * cut.width * 4]);
            }
            const std::string partFile = "places/" + build.Id() + "/backdrop-parts/" + (level.empty() ? "" : level + "-") + std::to_string(++part) + ".png";
            fs::create_directories((build.Folder().parent_path().parent_path() / partFile).parent_path());
            if (!WritePng(build.Folder().parent_path().parent_path() / partFile, cut)) continue;
            // Standing on its footprint's lower edge: the projection puts a sprite's bottom a quarter
            // tile below its point.
            const auto [wx, wy] = toWorld(left + (cx0 + cx1) / 2.0 * k, top + cy1 * k - iso.tileHeight * 0.25);
            const Entity o = registry.create();
            registry.emplace<WorldTransform2D>(o, static_cast<float>(wx), static_cast<float>(wy));
            registry.emplace<ScreenTransform2D>(o);
            Sprite cs;
            cs.texture = partFile;
            cs.width = static_cast<float>(cut.width * k);
            cs.height = static_cast<float>(cut.height * k);
            registry.emplace<Sprite>(o, cs);
            registry.emplace<PlaceNode>(o, options.place, level, "occluder#" + std::to_string(part));
            report["occluders"] = report["occluders"].get<int>() + 1;
        }
        // Markers: the story's anchors, the doors, the way in; each on the nearest open cell.
        auto nearestOpen = [&](double wx, double wy) -> std::optional<std::pair<int, int>> {
            const int cx = static_cast<int>(std::floor(wx)), cy = static_cast<int>(std::floor(wy));
            for (int r = 0; r < 12; ++r) {
                for (int dy = -r; dy <= r; ++dy) {
                    for (int dx = -r; dx <= r; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                        const int gx = cx + dx, gy = cy + dy;
                        if (gx < x0 || gy < y0 || gx >= x1 || gy >= y1) continue;
                        if (walk.Get(gx, gy) == openId) return std::pair{gx, gy};
                    }
                }
            }
            return std::nullopt;
        };
        auto marker = [&](const std::string& name, const json& at) {
            if (!at.is_array() || at.size() < 2) return;
            const auto [wx, wy] = pixelToWorld(at[0].get<double>(), at[1].get<double>());
            const auto cell = nearestOpen(wx, wy);
            if (!cell) return;
            const Entity m = registry.create();
            registry.emplace<WorldTransform2D>(m, cell->first + 0.5f, cell->second + 0.5f);
            registry.emplace<Marker>(m, name);
            registry.emplace<Name>(m, "Marker " + name);
            registry.emplace<PlaceNode>(m, options.place, level, "marker#" + name);
            report["markers"] = report["markers"].get<int>() + 1;
        };
        const json anchors = map.value("anchors", json::object());
        for (const auto& [id, at] : anchors.items()) marker(id, at);
        int d = 0;
        for (const auto& door : map.value("doors", json::array())) marker("door-" + std::to_string(++d), door.value("at", json()));
        marker(level.empty() ? "arrive" : "arrive-" + level, map.value("entrance", json()));
    }
    return report;
}

} // namespace aige
