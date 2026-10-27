#include "places/Arrange.h"

#include "places/Ontology.h"
#include "places/Solver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace aige {

using json = nlohmann::json;

namespace {

bool Arranged(const json& s) {
    const std::string op = s.value("op", "");
    return (op == "place" || op == "scatter") && s.contains("slot");
}

std::string RoleOf(const json& s) {
    const std::string slot = s.value("slot", "");
    return slot.substr(0, slot.find('|'));
}

// A kind's arrangement with the ontology's defaults under it.
json KindOf(const Ontology& ontology, const std::string& role, const json& given) {
    json kind = given.is_object() ? given : json::object();
    auto fill = [&](const char* key) {
        if (!kind.contains(key)) {
            if (const json v = ontology.RoleLook(role, key); !v.is_null()) kind[key] = v;
        }
    };
    fill("purpose");
    fill("relations");
    fill("faces");
    fill("turns");
    fill("views");
    return kind;
}

} // namespace

json ArrangementKinds(const Ontology& ontology, const json& ir) {
    std::map<std::string, json> kinds;
    for (const auto& s : ir.value("statements", json::array())) {
        if (!Arranged(s)) continue;
        const std::string role = RoleOf(s);
        json& k = kinds[role];
        if (k.is_null()) {
            k = KindOf(ontology, role, json::object());
            k["role"] = role;
            const Role* r = ontology.FindRole(role);
            k["label"] = r ? r->label : role;
            k["footprint"] = s.value("footprint", json{{"w", 1}, {"h", 1}});
            k["in"] = json::array();
            k["statements"] = 0;
        }
        k["statements"] = k["statements"].get<int>() + 1;
        const std::string in = s.value("in", "");
        if (!in.empty() && k["in"].size() < 6 && std::find(k["in"].begin(), k["in"].end(), in) == k["in"].end()) k["in"].push_back(in);
    }
    json out = json::array();
    for (auto& [role, k] : kinds) out.push_back(k);
    return out;
}

json DefaultArrangement(const Ontology& ontology, const json& ir) {
    json kinds = json::object();
    for (const auto& k : ArrangementKinds(ontology, ir)) {
        json kind = k;
        for (const char* drop : {"role", "label", "in", "statements", "footprint"}) kind.erase(drop);
        kinds[k.value("role", "")] = kind;
    }
    return {{"kinds", kinds}};
}

void ApplyArrangement(const Ontology& ontology, json& ir, const json& arrangement) {
    const json given = arrangement.value("kinds", json::object());
    for (auto& s : ir["statements"]) {
        if (!Arranged(s)) continue;
        const std::string role = RoleOf(s);
        const json kind = KindOf(ontology, role, given.value(role, json::object()));
        if (kind.contains("purpose")) s["purpose"] = kind["purpose"];
        if (kind.contains("relations")) s["relations"] = kind["relations"];
        if (kind.contains("faces")) s["faces"] = kind["faces"];
        if (s.value("op", "") == "place" && kind.contains("footprint") && kind["footprint"].is_object()) {
            const int w = std::clamp(kind["footprint"].value("w", 1), 1, 8), h = std::clamp(kind["footprint"].value("h", 1), 1, 8);
            s["footprint"] = {{"w", w}, {"h", h}};
        }
        // Where it stands: the arrangement's, else its first relation that places it (a door set in a wall, stairs
        // leading out of the room: at a wall), over where its module put it.
        std::string position = kind.value("position", "");
        if (position.empty()) {
            for (const auto& r : kind.value("relations", json::array())) {
                const RelationType* type = ontology.Relation(r.value("type", ""));
                if (type && type->use.is_object() && type->use.contains("position")) {
                    position = type->use.value("position", "");
                    break;
                }
            }
        }
        if (position == "wall" || position == "corner" || position == "center" || position == "far-wall" || position == "facing-door") {
            s["position"] = position;
        }
    }
}

int FaceObjects(const Ontology& ontology, json& solved, const json& ir) {
    std::map<std::string, json> statements;
    for (const auto& s : ir.value("statements", json::array())) statements[s.value("id", "")] = s;
    const json bounds = solved.value("grid", json::object()).value("bounds", json::array());
    if (!bounds.is_array() || bounds.size() != 4) return 0;
    const int W = bounds[2].get<int>(), H = bounds[3].get<int>();
    auto in = [&](int x, int y) { return x >= 0 && y >= 0 && x < W && y < H; };
    // The ground's names, and what's solid (rock, walls).
    std::vector<std::string> ground(static_cast<size_t>(W) * H);
    {
        const json t = solved.value("terrain", json::object());
        const json names = t.value("palette", json::array());
        int y = 0;
        for (const auto& row : t.value("rows", json::array())) {
            int x = 0;
            for (const auto& run : row) {
                const int n = run[0].get<int>(), i = run[1].get<int>();
                for (int k = 0; k < n; ++k, ++x) {
                    if (in(x, y) && i >= 0 && i < static_cast<int>(names.size())) ground[static_cast<size_t>(y) * W + x] = names[i].get<std::string>();
                }
            }
            ++y;
        }
    }
    std::vector<uint8_t> solid(static_cast<size_t>(W) * H, 0);
    for (int c : DecodeCells(solved.value("spaces", json::object()).value("solid", json::array()), W)) {
        if (c >= 0 && c < W * H) solid[c] = 1;
    }
    // Each region's cells (rooms, parcels), smallest first: a thing's area is the smallest holding it.
    struct Area {
        std::string id;
        std::vector<int> cells;
        std::set<int> set;
    };
    std::vector<Area> areas;
    for (const auto& r : solved.value("regions", json::array())) {
        Area a{r.value("id", ""), DecodeCells(r.value("cells", json::array()), W), {}};
        a.set.insert(a.cells.begin(), a.cells.end());
        if (!a.cells.empty()) areas.push_back(std::move(a));
    }
    std::sort(areas.begin(), areas.end(), [](const Area& a, const Area& b) { return a.cells.size() < b.cells.size(); });
    const json doors = solved.value("spaces", json::object()).value("doors", json::object());
    // The roads' cells (a city's streets).
    std::vector<uint8_t> road(static_cast<size_t>(W) * H, 0);
    for (const auto& r : solved.value("roads", json::array())) {
        if (r.is_array() && r.size() >= 2 && in(r[0].get<int>(), r[1].get<int>())) road[static_cast<size_t>(r[1].get<int>()) * W + r[0].get<int>()] = 1;
    }
    // A role is `of` or narrower than it (a car is a vehicle).
    auto isKind = [&](std::string id, const std::string& of) {
        for (int i = 0; i < 16 && !id.empty(); ++i) {
            if (id == of) return true;
            const Role* r = ontology.FindRole(id);
            if (r == nullptr) break;
            id = r->broader;
        }
        return false;
    };
    int turned = 0;
    for (auto& o : solved["objects"]) {
        const auto st = statements.find(o.value("statement", ""));
        if (st == statements.end() || (!st->second.contains("faces") && !st->second.contains("relations"))) continue;
        const std::string rule = st->second.value("faces", "");
        if (o.value("layer", "") == "building" || rule == "across" || o.value("faced", "") == "layout") continue; // its layout's facing
        const int x = o.value("x", 0), y = o.value("y", 0), w = std::max(1, o.value("w", 1)), h = std::max(1, o.value("h", 1));
        const int cell = y * W + x;
        const Area* area = nullptr;
        for (const auto& a : areas) {
            if (a.set.contains(cell)) {
                area = &a;
                break;
            }
        }
        auto open = [&](int cx, int cy) {
            if (!in(cx, cy)) return false;
            const int c = cy * W + cx;
            if (solid[c] || ground[c].empty() || ground[c] == "void") return false;
            return area == nullptr || area->set.contains(c);
        };
        // Room in front of each side (up to 8 cells): n, s, w, e.
        auto room = [&](int dx, int dy) {
            int n = 0;
            const int cx = dx > 0 ? x + w - 1 : x, cy = dy > 0 ? y + h - 1 : y;
            while (n < 8 && open(cx + dx * (n + 1), cy + dy * (n + 1))) ++n;
            return n;
        };
        const int dn = room(0, -1), ds = room(0, 1), dw = room(-1, 0), de = room(1, 0);
        std::string facing = o.value("facing", "s");
        const double mx = x + w / 2.0, my = y + h / 2.0;
        auto towards = [&](double tx, double ty) {
            const double dx = tx - mx, dy = ty - my;
            if (std::abs(dx) < 0.5 && std::abs(dy) < 0.5) return facing;
            return std::abs(dx) > std::abs(dy) ? std::string(dx > 0 ? "e" : "w") : std::string(dy > 0 ? "s" : "n");
        };
        // Its relations first (docs/places/ontology-in-use.md): one whose type says how it faces the other (aligned:
        // along it; against, mounted-on: away from it; used-with: towards it), to the nearest thing it names within the
        // type's reach.
        std::string by;
        for (const auto& r : st->second.value("relations", json::array())) {
            const RelationType* type = ontology.Relation(r.value("type", ""));
            if (type == nullptr || !type->use.is_object()) continue;
            const std::string how = type->use.value("facing", "");
            if (how != "along" && how != "away" && how != "front") continue;
            const std::string to = r.value("to", "");
            const int reach = type->use.value("within", 4);
            // Where the plan has things of that role (its look "found"): the roads' cells, the solid cells (walls, rock),
            // ground of that name, else placed things of that kind.
            const json found = ontology.RoleLook(to, "found");
            const bool roads = found == "roads", walls = found == "solid";
            auto target = [&](int cx, int cy) {
                if (!in(cx, cy)) return false;
                const int c = cy * W + cx;
                return (roads && road[c]) || (walls && solid[c]) || (!to.empty() && ground[c] == to);
            };
            // The nearest cell of it, else the nearest thing of its kind: where it is and how far it runs each way.
            double best = 1e9, tx = 0, ty = 0;
            int run[4] = {0, 0, 0, 0}; // west, east, north, south of it
            bool onCells = false;     // a road, a wall or ground, not a thing
            for (int cy = y - reach; cy < y + h + reach; ++cy) {
                for (int cx = x - reach; cx < x + w + reach; ++cx) {
                    if (!target(cx, cy)) continue;
                    if (const double d = std::hypot(cx + 0.5 - mx, cy + 0.5 - my); d < best) best = d, tx = cx + 0.5, ty = cy + 0.5;
                }
            }
            if (best < 1e9) {
                onCells = true;
                const int cx = static_cast<int>(tx), cy = static_cast<int>(ty);
                const int dirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                for (int k = 0; k < 4; ++k) {
                    while (run[k] < 16 && target(cx + dirs[k][0] * (run[k] + 1), cy + dirs[k][1] * (run[k] + 1))) ++run[k];
                }
            } else {
                for (const auto& other : solved["objects"]) {
                    const std::string slot = other.value("slot", "");
                    if (&other == &o || to.empty() || !isKind(slot.substr(0, slot.find('|')), to)) continue;
                    const int ow = std::max(1, other.value("w", 1)), oh = std::max(1, other.value("h", 1));
                    const double ox = other.value("x", 0) + ow / 2.0, oy = other.value("y", 0) + oh / 2.0;
                    const double d = std::hypot(ox - mx, oy - my);
                    if (d - (ow + oh + w + h) / 4.0 > reach || d >= best) continue;
                    best = d, tx = ox, ty = oy;
                    run[0] = run[1] = ow / 2, run[2] = run[3] = oh / 2;
                }
                if (best >= 1e9) continue;
            }
            if (how == "front") {
                facing = towards(tx, ty);
            } else if (how == "away") {
                const std::string t = towards(tx, ty);
                facing = t == "n" ? "s" : t == "s" ? "n" : t == "e" ? "w" : "e";
            } else {
                // Along its long way there; on its right-hand side of it (a car in its lane: the south half drives east,
                // the east half north).
                // Beside it (a car parked at the kerb), the way to it says: a road to the north or south runs along x.
                // On it, the way it runs furthest from there (at a crossing, either).
                const int alongX = run[0] + run[1], alongY = run[2] + run[3];
                const bool beside = onCells && best > 0.75;
                const bool runsX = beside ? std::abs(ty - my) >= std::abs(tx - mx)
                                          : alongX > alongY || (alongX == alongY && ((x * 7 + y * 13) & 1));
                if (runsX) {
                    facing = my > ty + (run[3] - run[2]) / 2.0 ? "e" : "w";
                } else {
                    facing = mx > tx + (run[1] - run[0]) / 2.0 ? "n" : "s";
                }
            }
            by = r.value("type", "");
            break;
        }
        if (!by.empty()) {
        } else if (rule == "away-from-wall") {
            // Back to the nearest edge; on a tie, the way with more room in front.
            const int nearest = std::min({dn, ds, dw, de});
            int best = -1;
            for (const auto& [behind, f, ahead] : {std::tuple{ds, "n", dn}, std::tuple{dn, "s", ds}, std::tuple{de, "w", dw}, std::tuple{dw, "e", de}}) {
                if (behind == nearest && ahead > best) best = ahead, facing = f;
            }
        } else if (rule == "room" && area) {
            double sx = 0, sy = 0;
            for (int c : area->cells) sx += c % W + 0.5, sy += c / W + 0.5;
            facing = towards(sx / area->cells.size(), sy / area->cells.size());
        } else if (rule == "door" && area) {
            double best = 1e9, tx = mx, ty = my;
            for (const auto& d : doors.value(area->id, json::array())) {
                const double dx = d[0].get<int>() + 0.5, dy = d[1].get<int>() + 0.5, dist = std::hypot(dx - mx, dy - my);
                if (dist < best) best = dist, tx = dx, ty = dy;
            }
            facing = towards(tx, ty);
        } else if (rule == "path") {
            // The long way of its ground through it; the side of the road it's on says which way (right-hand).
            const std::string here = ground[cell];
            auto reach = [&](int dx, int dy) {
                int n = 0;
                while (n < 16 && in(x + dx * (n + 1), y + dy * (n + 1)) && ground[(y + dy * (n + 1)) * W + x + dx * (n + 1)] == here) ++n;
                return n;
            };
            const int west = reach(-1, 0), east = reach(1, 0), north = reach(0, -1), south = reach(0, 1);
            const bool alongX = west + east > north + south || (west + east == north + south && ((x * 7 + y * 13) & 1));
            facing = alongX ? (south < north ? "e" : "w") : (east < west ? "n" : "s");
        } else if (rule == "front") {
            // Towards the nearest thing of the kind it's used with.
            std::string with;
            for (const auto& r : st->second.value("relations", json::array())) {
                if (r.value("type", "") == "used-with") with = r.value("to", "");
            }
            double best = 1e9, tx = mx, ty = my;
            for (const auto& other : solved["objects"]) {
                const std::string slot = other.value("slot", "");
                if (with.empty() || slot.substr(0, slot.find('|')) != with) continue;
                const double ox = other.value("x", 0) + other.value("w", 1) / 2.0, oy = other.value("y", 0) + other.value("h", 1) / 2.0;
                if (const double d = std::hypot(ox - mx, oy - my); d < best) best = d, tx = ox, ty = oy;
            }
            facing = towards(tx, ty);
        } else {
            continue;
        }
        o["facing"] = facing;
        o["faced"] = by.empty() ? rule : by;
        if (st->second.contains("purpose")) o["purpose"] = st->second["purpose"];
        if (st->second.contains("relations")) o["relations"] = st->second["relations"];
        ++turned;
    }
    // Purposes and relations on every object (the art and the scene read them), turned or not.
    for (auto& o : solved["objects"]) {
        const auto st = statements.find(o.value("statement", ""));
        if (st == statements.end()) continue;
        if (st->second.contains("purpose") && !o.contains("purpose")) o["purpose"] = st->second["purpose"];
        if (st->second.contains("relations") && !o.contains("relations")) o["relations"] = st->second["relations"];
    }
    return turned;
}

} // namespace aige
