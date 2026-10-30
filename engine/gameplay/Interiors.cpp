#include "gameplay/Interiors.h"

#include "gameplay/Components.h"
#include "gameplay/Settings.h"

#include <nlohmann/json.hpp>
#include "gameplay/Archetypes.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <tuple>
#include <utility>
#include <vector>

namespace aige {

bool InteriorFootprint(Registry& registry, Entity entity, const Interior& interior, int& x0, int& y0, int& x1, int& y1) {
    if (!interior.cells.empty()) {
        return std::sscanf(interior.cells.c_str(), "%d,%d,%d,%d", &x0, &y0, &x1, &y1) == 4;
    }
    const auto* obstacle = registry.try_get<Obstacle>(entity);
    const auto* world = registry.try_get<WorldTransform2D>(entity);
    if (obstacle == nullptr || world == nullptr) return false;
    const int sx = static_cast<int>(std::floor(world->x)), sy = static_cast<int>(std::floor(world->y));
    x0 = sx - obstacle->left, x1 = sx + obstacle->right, y0 = sy - obstacle->back, y1 = sy;
    return true;
}

bool InteriorDoor(Registry& registry, Entity entity, const Interior& interior, int& x, int& y) {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!InteriorFootprint(registry, entity, interior, x0, y0, x1, y1)) return false;
    std::tie(x, y) = DoorwayOf(x0, y0, x1, y1, interior.door);
    return true;
}

std::vector<std::pair<int, int>> InteriorEntries(Registry& registry, Entity entity, const Interior& interior) {
    std::vector<std::pair<int, int>> out;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (interior.entries.empty() || !InteriorFootprint(registry, entity, interior, x0, y0, x1, y1)) return out;
    std::stringstream in(interior.entries);
    for (std::string part; std::getline(in, part, ';');) {
        int dx = 0, dy = 0;
        if (std::sscanf(part.c_str(), "q%d,%d", &dx, &dy) == 2) {
            // A quarter cell (half cells from the top-left): its cell is a way in.
            const std::pair<int, int> cell{x0 + TileLayer::FloorDiv(dx, 2), y0 + TileLayer::FloorDiv(dy, 2)};
            if (std::find(out.begin(), out.end(), cell) == out.end()) out.push_back(cell);
        } else if (std::sscanf(part.c_str(), "%d,%d", &dx, &dy) == 2) {
            out.push_back({x0 + dx, y0 + dy});
        }
    }
    return out;
}

std::map<std::pair<int, int>, std::vector<std::pair<float, float>>> InteriorWayQuarters(Registry& registry, Entity entity,
                                                                                       const Interior& interior) {
    std::map<std::pair<int, int>, std::vector<std::pair<float, float>>> out;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (interior.entries.empty() || !InteriorFootprint(registry, entity, interior, x0, y0, x1, y1)) return out;
    std::stringstream in(interior.entries);
    for (std::string part; std::getline(in, part, ';');) {
        int qx = 0, qy = 0;
        if (std::sscanf(part.c_str(), "q%d,%d", &qx, &qy) != 2) continue;
        const float wx = x0 + qx * 0.5f, wy = y0 + qy * 0.5f;
        out[{static_cast<int>(std::floor(wx)), static_cast<int>(std::floor(wy))}].push_back({wx, wy});
    }
    return out;
}

std::string DoorOpens(const AnimatedPart& part) {
    if (part.opens == "auto" || part.opens == "use" || part.opens == "open" || part.opens == "always") return part.opens;
    if (part.opens == "action" || part.opens == "event") return part.trigger.empty() ? "use" : part.opens; // nothing named: by use
    if (!part.opensNear) return "use";
    return ActiveSettings().game.doorsOpen == "auto" ? "auto" : "use";
}

std::vector<AnimatedPart> DoorsFromJson(const std::string& text) {
    std::vector<AnimatedPart> doors;
    const nlohmann::json list = nlohmann::json::parse(text.empty() ? std::string("[]") : text, nullptr, false);
    for (const auto& d : list.is_array() ? list : nlohmann::json::array()) {
        if (!d.is_object()) continue;
        AnimatedPart door;
        door.sheet = d.value("sheet", std::string());
        door.x = d.value("x", 0.0f), door.y = d.value("y", 0.0f);
        door.width = d.value("width", 0.0f), door.height = d.value("height", 0.0f);
        door.clip = d.value("clip", std::string("open"));
        door.closeClip = d.value("closeClip", std::string("close"));
        door.range = d.value("range", 1.8f);
        door.opens = d.value("opens", std::string("project"));
        door.trigger = d.value("trigger", std::string());
        door.prompt = d.value("prompt", std::string());
        door.fires = d.value("fires", std::string());
        door.duration = d.value("duration", 0.0f);
        door.playerClip = d.value("playerClip", std::string());
        door.playerLoop = d.value("playerLoop", true);
        door.holdsPlayer = d.value("holdsPlayer", false);
        door.door = d.value("door", true);
        door.floor = d.value("floor", 0);
        door.locked = d.value("locked", false);
        door.key = d.value("key", std::string());
        door.consumeKey = d.value("consumeKey", false);
        if (!door.sheet.empty() || door.opens == "open") doors.push_back(door); // a door, or an open doorway (§4.5)
    }
    return doors;
}

std::string DoorsJson(const std::vector<AnimatedPart>& doors) {
    nlohmann::json list = nlohmann::json::array();
    for (const AnimatedPart& d : doors) {
        list.push_back({{"sheet", d.sheet}, {"x", d.x}, {"y", d.y}, {"width", d.width}, {"height", d.height}, {"clip", d.clip},
                        {"closeClip", d.closeClip}, {"range", d.range}, {"opens", d.opens}, {"locked", d.locked}, {"key", d.key},
                        {"consumeKey", d.consumeKey}, {"trigger", d.trigger}, {"prompt", d.prompt}, {"door", d.door},
                        {"floor", d.floor}, {"fires", d.fires},
                        {"duration", d.duration}, {"playerClip", d.playerClip}, {"playerLoop", d.playerLoop}, {"holdsPlayer", d.holdsPlayer}});
    }
    return list.empty() ? std::string() : list.dump();
}

std::vector<AnimatedPart> DoorsOf(const AnimatedPart& part) {
    AnimatedPart first = part;
    first.more.clear();
    std::vector<AnimatedPart> doors{first};
    for (auto& d : DoorsFromJson(part.more)) doors.push_back(d);
    return doors;
}

void SetDoors(AnimatedPart& part, const std::vector<AnimatedPart>& doors) {
    if (doors.empty()) {
        part = AnimatedPart{};
        return;
    }
    part = doors.front();
    part.more = DoorsJson(std::vector<AnimatedPart>(doors.begin() + 1, doors.end()));
}

bool DoorwayShut(Registry& registry, Entity entity) {
    const auto* part = registry.try_get<AnimatedPart>(entity);
    if (const auto* interior = registry.try_get<Interior>(entity); (part == nullptr || part->sheet.empty()) && interior && !interior->entries.empty()) {
        // No animated doors, ways in set by hand: its own doorway only if it's one of them.
        int dx = 0, dy = 0;
        if (!InteriorDoor(registry, entity, *interior, dx, dy)) return false;
        const auto ways = InteriorEntries(registry, entity, *interior);
        return std::find(ways.begin(), ways.end(), std::make_pair(dx, dy)) == ways.end();
    }
    if (part == nullptr || (part->sheet.empty() && part->opens != "open" && part->more.empty())) return false;
    if (const auto all = DoorsOf(*part); std::none_of(all.begin(), all.end(), [](const AnimatedPart& d) { return d.door; })) {
        return false; // parts only, no doors
    }
    // In play, each door's doorway is known (PartOpen): the interior's own doorway is shut when a door at it is.
    if (const auto* interior = registry.try_get<Interior>(entity); interior && !interior->entries.empty()) {
        int dx = 0, dy = 0;
        if (!InteriorDoor(registry, entity, *interior, dx, dy)) return false;
        const auto ways = InteriorEntries(registry, entity, *interior);
        if (std::find(ways.begin(), ways.end(), std::make_pair(dx, dy)) == ways.end()) return true; // not a way in
    }
    if (const auto* open = registry.try_get<PartOpen>(entity); open && !open->cells.empty()) {
        const auto* interior = registry.try_get<Interior>(entity);
        int dx = 0, dy = 0;
        if (!interior || !InteriorDoor(registry, entity, *interior, dx, dy)) return false;
        for (size_t i = 0; i < open->cells.size(); ++i) {
            if (open->cells[i] == std::make_pair(dx, dy)) return !(i < open->passable.size() && open->passable[i]);
        }
        // No door at it: the way in is where its doors are drawn (each its own doorway), not this side's middle.
        return true;
    }
    if (!part->door) return false;
    const std::string way = DoorOpens(*part);
    return part->locked || (way != "auto" && way != "open" && way != "always");
}

namespace {

void PutMarker(Registry& registry, const std::string& name, float x, float y) {
    for (auto [e, m] : registry.view<Marker>().each()) {
        if (m.name == name) {
            registry.get_or_emplace<WorldTransform2D>(e) = {x, y};
            return;
        }
    }
    const Entity e = registry.create();
    registry.emplace<WorldTransform2D>(e, x, y);
    registry.emplace<Marker>(e, name);
    registry.emplace<Name>(e, "Marker " + name);
}

} // namespace

bool LinkInteriorOutside(Registry& outside, Entity building, const std::string& scene, const std::string& marker, const std::string& back) {
    auto& interior = outside.get_or_emplace<Interior>(building);
    interior.kind = "scene";
    interior.scene = scene;
    interior.marker = marker;
    int dx = 0, dy = 0;
    if (!InteriorDoor(outside, building, interior, dx, dy)) return false;
    const int ox = interior.door == "e" ? 1 : interior.door == "w" ? -1 : 0;
    const int oy = interior.door == "s" ? 1 : interior.door == "n" ? -1 : 0;
    PutMarker(outside, back, dx + ox + 0.5f, dy + oy + 0.5f);
    return true;
}

bool LinkInteriorInside(Registry& inside, int x0, int y0, int x1, int y1, const std::string& exteriorScene, const std::string& back,
                        const std::string& marker) {
    const TileGrid grid = TileGrid::Build(inside);
    const int mx = (x0 + x1) / 2;
    // Its entrance: the frontmost row with an open cell, the one nearest the middle.
    for (int y = y1; y >= y0; --y) {
        int best = -1;
        for (int x = x0; x <= x1; ++x) {
            if (!grid.IsSolid(x, y) && (best < 0 || std::abs(x - mx) < std::abs(best - mx))) best = x;
        }
        if (best < 0) continue;
        for (auto [e, gateway] : inside.view<Gateway>().each()) {
            if (gateway.scene == exteriorScene && gateway.marker == back) inside.destroy(e); // linked before: once
        }
        CreateGateway(inside, best + 0.5f, y + 0.5f, exteriorScene, back);
        int iy = y;
        for (int k = 0; k < 2 && iy - 1 >= y0 && !grid.IsSolid(best, iy - 1); ++k) --iy;
        PutMarker(inside, marker, best + 0.5f, iy + 0.5f);
        return true;
    }
    return false;
}

namespace {

// Its upper floors' pictures, in order.
std::vector<std::string> UpperFloors(const Interior& interior) {
    std::vector<std::string> out;
    for (size_t at = 0; at < interior.floors.size();) {
        const size_t end = std::min(interior.floors.find(';', at), interior.floors.size());
        if (end > at) out.push_back(interior.floors.substr(at, end - at));
        at = end + 1;
    }
    return out;
}

} // namespace

std::vector<std::pair<int, int>> StairsCells(const Interior& interior, int change, int x0, int y0, int x1, int y1) {
    std::vector<std::string> parts;
    std::stringstream in(interior.stairs);
    for (std::string part; std::getline(in, part, ';');) parts.push_back(part);
    std::vector<std::pair<int, int>> cells;
    if (change >= 1 && change <= static_cast<int>(parts.size())) {
        std::stringstream each(parts[static_cast<size_t>(change - 1)]);
        for (std::string cell; std::getline(each, cell, '|');) {
            int x = 0, y = 0;
            // Only cells of its footprint (one listed outside it is from an older layout: the default instead).
            if (std::sscanf(cell.c_str(), "%d,%d", &x, &y) == 2 && x >= x0 && x <= x1 && y >= y0 && y <= y1) cells.push_back({x, y});
        }
    }
    if (!cells.empty()) return cells;
    // None listed: where the interior pictures draw it (matched.py stairs_ramp), from the back corner three fifths of the
    // way along the back-left wall (column x0) for floor changes 1, 3…, the back-right (row y0) for 2, 4…; the corner
    // itself left out, so a floor's way up and its way down never share a cell.
    const bool odd = std::max(change, 1) % 2 == 1;
    const int length = odd ? y1 - y0 : x1 - x0;
    const int run = std::max(1, static_cast<int>(std::lround(length * 0.6)));
    for (int i = 1; i <= std::min(run, length); ++i) cells.push_back(odd ? std::make_pair(x0, y0 + i) : std::make_pair(x0 + i, y0));
    if (cells.empty()) cells.push_back({x0, y0}); // a footprint one cell deep
    return cells;
}

std::pair<int, int> StairsCell(const Interior& interior, int change, int x0, int y0, int x1, int y1) {
    // The listed cell of floor change `change` (1: floor 1 to 2), else where the interior pictures draw it: along a back
    // wall by the back (top) corner, the back-left wall (column x0) for changes 1, 3…, the back-right (row y0) for 2, 4…
    std::vector<std::pair<int, int>> listed;
    std::stringstream in(interior.stairs);
    std::string part;
    while (std::getline(in, part, ';')) {
        int x = 0, y = 0;
        if (std::sscanf(part.c_str(), "%d,%d", &x, &y) == 2) listed.push_back({x, y});
    }
    if (change >= 1 && change <= static_cast<int>(listed.size())) return listed[static_cast<size_t>(change - 1)];
    if (std::max(change, 1) % 2 == 1) return {x0, std::min(y0 + 1, y1)}; // the back-left wall, by the back corner
    return {std::min(x0 + 1, x1), y0};                                     // the back-right wall
}

float InteriorStorey(Registry& registry, Entity entity, const Interior& interior) {
    if (interior.storeyHeight > 0.0f) return interior.storeyHeight;
    // As its floors are painted (matched.py _floor_frame): what stands above its footprint's diamond (about half its
    // width), seventeen twentieths of it walls, shared by its storeys.
    const auto* sprite = registry.try_get<Sprite>(entity);
    return sprite ? std::max(0.0f, sprite->height - sprite->width * 0.5f) * 0.85f / std::max(1, InteriorFloors(interior)) : 0.0f;
}

std::vector<std::pair<int, int>> InteriorRoomStairs(const Interior& interior, int change) {
    std::vector<std::pair<int, int>> cells;
    std::vector<std::string> parts;
    std::stringstream in(interior.stairs);
    for (std::string part; std::getline(in, part, ';');) parts.push_back(part);
    if (change < 1 || change > static_cast<int>(parts.size())) return cells;
    std::stringstream each(parts[static_cast<size_t>(change - 1)]);
    for (std::string c; std::getline(each, c, '|');) {
        int i = 0, j = 0;
        if (std::sscanf(c.c_str(), "r%d,%d", &i, &j) == 2) cells.push_back({i, j});
    }
    return cells;
}

std::vector<std::pair<int, int>> InteriorRoomSolids(const Interior& interior, int floor) {
    std::vector<std::pair<int, int>> cells;
    std::stringstream in(interior.solids);
    for (std::string part; std::getline(in, part, ';');) {
        const size_t colon = part.find(':');
        if (colon == std::string::npos || std::atoi(part.substr(0, colon).c_str()) != floor) continue;
        std::stringstream each(part.substr(colon + 1));
        for (std::string q; std::getline(each, q, '|');) {
            int i = 0, j = 0;
            if (std::sscanf(q.c_str(), "r%d,%d", &i, &j) == 2) cells.push_back({i, j});
        }
    }
    return cells;
}

std::vector<std::pair<float, float>> InteriorSolids(const Interior& interior, int floor) {
    std::vector<std::pair<float, float>> quarters;
    std::stringstream in(interior.solids);
    for (std::string part; std::getline(in, part, ';');) {
        const size_t colon = part.find(':');
        if (colon == std::string::npos || std::atoi(part.substr(0, colon).c_str()) != floor) continue;
        std::stringstream each(part.substr(colon + 1));
        for (std::string q; std::getline(each, q, '|');) {
            int x = 0, y = 0;
            if (q.rfind('r', 0) != 0 && std::sscanf(q.c_str(), "%d,%d", &x, &y) == 2) quarters.push_back({x * 0.5f, y * 0.5f});
        }
    }
    return quarters;
}

int InteriorFloors(const Interior& interior) {
    return interior.texture.empty() ? 0 : 1 + static_cast<int>(UpperFloors(interior).size());
}

std::string InteriorPicture(const Interior& interior, int floor) {
    const auto upper = UpperFloors(interior);
    return floor >= 2 && floor - 2 < static_cast<int>(upper.size()) ? upper[floor - 2] : interior.texture;
}

std::vector<std::pair<int, int>> InteriorRoomWays(const Interior& interior) {
    std::vector<std::pair<int, int>> ways;
    std::stringstream in(interior.entries);
    for (std::string part; std::getline(in, part, ';');) {
        int side = 0, index = 0;
        if (std::sscanf(part.c_str(), "r%d,%d", &side, &index) == 2) ways.push_back({side, index});
    }
    return ways;
}

int RoomSideCells(const RoomFrame& room, int side) {
    const float length = side == 0 || side == 2 ? std::hypot(room.exx, room.exy) : std::hypot(room.eyx, room.eyy);
    return std::max(1, static_cast<int>(std::lround(length * 4.0f))); // a quarter of a scene cell each (4 x 4 a scene cell)
}

bool InteriorRoom(Registry& registry, Entity entity, const Interior& interior, const IsoParams& iso, RoomFrame& room) {
    float bx, by, rx, ry, lx, ly, storey, w, h, along, deep;
    if (interior.layout.empty() || std::sscanf(interior.layout.c_str(), "%f,%f;%f,%f;%f,%f;%f;%f,%f;%f,%f", &bx, &by, &rx, &ry, &lx, &ly,
                                               &storey, &w, &h, &along, &deep) != 11) {
        return false;
    }
    const auto* sprite = registry.try_get<Sprite>(entity);
    const auto* screen = registry.try_get<ScreenTransform2D>(entity);
    if (!sprite || !screen || w <= 0.0f || h <= 0.0f || sprite->width <= 0.0f || sprite->height <= 0.0f) return false;
    // A point of its picture on the ground (as interior:art places its cells), in the world.
    const bool mirrored = sprite->u0 > sprite->u1;
    const float left = screen->x - sprite->width / 2, top = screen->y - sprite->height / 2;
    auto world = [&](float px, float py) {
        const float sx = left + (mirrored ? w - px : px) * sprite->width / w, sy = top + sprite->lift + py * sprite->height / h;
        float wx = 0.0f, wy = 0.0f;
        UnprojectIso(sx, sy, iso, wx, wy);
        return std::pair<float, float>{wx, wy};
    };
    // Its picture's own corners (mirrored, its right one is drawn on the left: its stairs, painted along its picture's
    // walls, stay along the same ones).
    const auto back = world(bx, by), xEnd = world(rx, ry), yEnd = world(lx, ly);
    room.bx = back.first, room.by = back.second;
    room.exx = xEnd.first - back.first, room.exy = xEnd.second - back.second;
    room.eyx = yEnd.first - back.first, room.eyy = yEnd.second - back.second;
    // Squared to the world's axes (a picture painted a little steeper than 2:1 makes them slightly skewed), each wall its
    // length along the axis it mostly runs: walls along the grid, so one slides along them as along any other.
    auto square = [](float& x, float& y) {
        const float length = std::hypot(x, y);
        if (std::abs(x) >= std::abs(y)) x = x < 0.0f ? -length : length, y = 0.0f;
        else y = y < 0.0f ? -length : length, x = 0.0f;
    };
    square(room.exx, room.exy);
    square(room.eyx, room.eyy);
    room.along = along, room.deep = deep;
    room.storey = storey * sprite->height / h;
    return std::hypot(room.exx, room.exy) > 0.1f && std::hypot(room.eyx, room.eyy) > 0.1f;
}

void UpdateInteriors(Registry& registry, std::optional<Entity> player, const IsoParams* iso) {
    // The buildings one goes into (cutaways), each its footprint and a storey's height on screen; and, painted from a
    // layout (Interior.layout), its painted room in the world: its stairs there exactly.
    struct Building {
        Entity entity;
        int x0, y0, x1, y1, floors;
        float storey;
        bool hasRoom = false;
        RoomFrame room;
    };
    std::vector<Building> buildings;
    for (auto [entity, interior] : registry.view<Interior>().each()) {
        if (interior.texture.empty() || interior.kind == "scene") continue;
        Building b{entity, 0, 0, -1, -1, InteriorFloors(interior), interior.storeyHeight};
        if (!InteriorFootprint(registry, entity, interior, b.x0, b.y0, b.x1, b.y1)) continue;
        b.hasRoom = iso && InteriorRoom(registry, entity, interior, *iso, b.room);
        if (b.storey <= 0.0f) {
            b.storey = b.hasRoom ? b.room.storey : InteriorStorey(registry, entity, interior);
        }
        buildings.push_back(b);
    }
    auto buildingAt = [&](float wx, float wy) -> const Building* {
        const int px = static_cast<int>(std::floor(wx)), py = static_cast<int>(std::floor(wy));
        for (const auto& b : buildings) {
            if (b.hasRoom) {
                // In its room: u, v within it (a tenth of a cell's leeway: one in its doorway is in).
                const RoomFrame& r = b.room;
                const float det = r.exx * r.eyy - r.exy * r.eyx;
                if (std::abs(det) < 1e-6f) continue;
                const float dx = wx - r.bx, dy = wy - r.by;
                const float u = (dx * r.eyy - dy * r.eyx) / det, v = (r.exx * dy - r.exy * dx) / det;
                const float eu = 0.1f / std::max(0.1f, std::hypot(r.exx, r.exy)), ev = 0.1f / std::max(0.1f, std::hypot(r.eyx, r.eyy));
                if (u >= -eu && u <= 1.0f + eu && v >= -ev && v <= 1.0f + ev) return &b;
                continue;
            }
            if (px >= b.x0 && px <= b.x1 && py >= b.y0 && py <= b.y1) return &b;
        }
        return nullptr;
    };
    // Each character's floor (the player's and everyone's): in through a door, the ground floor. A floor's stairs up
    // (from their top, by the back corner, to their foot) are climbed: got on at their foot, it rises with them and stands
    // on the floor above on their top step; on from the side, they're only floor. Down: onto their top from the floor
    // above, then a step down: on the floor below, on its stairs (that high), and down them. A building painted from a
    // layout: its stairs where its room's are (odd floors' along its back-left wall, even floors' its back-right; a step
    // a room cell, a quarter of a scene cell); else its stairs cells (a step a cell; stairs of one cell walked onto, up
    // or down).
    for (auto [mover, velocity, world] : registry.view<Velocity2D, WorldTransform2D>().each()) {
        const Building* b = buildingAt(world.x, world.y);
        auto* on = registry.try_get<OnFloor>(mover);
        if (b == nullptr) {
            if (on) on->building = entt::null, on->floor = 1, on->onStairs = false, on->climbing = false, on->justSwitched = false;
            continue;
        }
        if (on == nullptr) on = &registry.emplace<OnFloor>(mover);
        if (on->building != b->entity) on->building = b->entity, on->floor = 1, on->onStairs = false, on->climbing = false, on->justSwitched = false;
        bool onStairs = false;
        on->climb = 0.0f;
        if (b->floors > 1) {
            const Interior& interior = registry.get<Interior>(b->entity);
            // Where it stands on a floor change's stairs: on them, how far down from their top (0 the top, 1 the foot), and
            // a step's share of them; and stairs of a single step.
            struct OnFlight {
                bool on = false;
                float down = 0.0f, step = 1.0f;
                float top = 1.0f; // its top: a scene cell along it (from the floor above, stepping on it takes one down)
                bool single = false;
            };
            auto flight = [&](int change) {
                OnFlight f;
                if (change < 1 || change >= b->floors) return f;
                if (b->hasRoom) {
                    // Its room's frame: u along its back-right wall (to its right corner), v along its back-left (to its left).
                    const RoomFrame& r = b->room;
                    const float det = r.exx * r.eyy - r.exy * r.eyx;
                    if (std::abs(det) < 1e-6f) return f;
                    const float dx = world.x - r.bx, dy = world.y - r.by;
                    const float u = (dx * r.eyy - dy * r.eyx) / det, v = (r.exx * dy - r.exy * dx) / det;
                    // Drawn on its room's grid (as wide as one likes): on them while in one of their cells; how far down them
                    // measured along their wall (odd floors' the back-left, even floors' the back-right), from their cell
                    // nearest its back corner (their top) to their farthest (their foot); a step half a scene cell (two
                    // room cells: one can't stand nearer a wall than that).
                    if (const auto drawn = InteriorRoomStairs(interior, change); !drawn.empty()) {
                        const int cols = RoomSideCells(r, 0), rows = RoomSideCells(r, 1);
                        const bool leftWall = change % 2 == 1;
                        const std::pair<int, int> here{static_cast<int>(std::floor(u * cols)), static_cast<int>(std::floor(v * rows))};
                        int top = INT_MAX, foot = INT_MIN;
                        for (const auto& [i, j] : drawn) {
                            const int at = leftWall ? j : i;
                            top = std::min(top, at), foot = std::max(foot, at + 1);
                        }
                        const float length = static_cast<float>(foot - top); // room cells
                        const float at = (leftWall ? v * rows : u * cols) - top;
                        f.on = std::find(drawn.begin(), drawn.end(), here) != drawn.end();
                        f.single = length <= 2.0f;
                        f.down = std::clamp(at / std::max(1.0f, length), 0.0f, 1.0f);
                        f.step = std::min(1.0f, 2.0f / std::max(1.0f, length));
                        f.top = std::min(1.0f, 4.0f / std::max(1.0f, length)); // four room cells: a scene cell
                        return f;
                    }
                    const bool leftWall = change % 2 == 1; // odd floors' stairs along the back-left wall
                    const float along = leftWall ? v : u, across = leftWall ? u : v;
                    const float wallLength = leftWall ? std::hypot(r.eyx, r.eyy) : std::hypot(r.exx, r.exy);
                    const float acrossLength = leftWall ? std::hypot(r.exx, r.exy) : std::hypot(r.eyx, r.eyy);
                    const float run = r.along * wallLength; // the stairs' length, in cells
                    // A little leeway past their side (half a cell): one walks beside the steps as much as on them.
                    f.on = along >= -0.1f && along * wallLength <= run + 0.1f && across * acrossLength >= -0.25f &&
                           across * acrossLength <= r.deep * acrossLength + 0.5f;
                    f.down = std::clamp(along * wallLength / std::max(0.5f, run), 0.0f, 1.0f);
                    // A step half a scene cell (getting on, the top, a step down): one can't stand nearer a wall than that.
                    f.step = std::min(1.0f, 0.5f / std::max(0.5f, run));
                    f.top = std::min(1.0f, 1.0f / std::max(1.0f, run));
                    return f;
                }
                auto cells = StairsCells(interior, change, b->x0, b->y0, b->x1, b->y1);
                std::stable_sort(cells.begin(), cells.end(), [&](const auto& a, const auto& c) {
                    return std::hypot(a.first - b->x0, a.second - b->y0) < std::hypot(c.first - b->x0, c.second - b->y0);
                });
                const std::pair<int, int> cell{static_cast<int>(std::floor(world.x)), static_cast<int>(std::floor(world.y))};
                const auto it = std::find(cells.begin(), cells.end(), cell);
                f.on = it != cells.end();
                f.single = cells.size() == 1;
                f.step = 1.0f / std::max<size_t>(1, cells.size());
                f.top = f.step;
                f.down = f.on ? (static_cast<float>(it - cells.begin()) + 0.5f) * f.step : 0.0f;
                return f;
            };
            const OnFlight up = flight(on->floor), down = flight(on->floor - 1);
            // Up: got on at the foot of its floor's stairs and climbed to their top. Down: onto the top of the stairs
            // from the floor below (a scene cell of them), from this floor. Having just switched at a top, it switches
            // again only once it has left it (so arriving at the top doesn't send it straight back). Where a floor's
            // stairs up lie over the top of the stairs from below, down wins there.
            bool wentDown = false;
            if (down.on && down.single) {
                onStairs = true;
                if (!on->onStairs) --on->floor, wentDown = true;
            } else if (down.on && down.down < down.top) {
                if (!on->justSwitched) {
                    --on->floor; // onto the top from this floor: on the floor below, high on its stairs
                    on->justSwitched = true, on->climbing = true, wentDown = true;
                }
            }
            if (!wentDown) {
                if (up.on && up.single) {
                    onStairs = true;
                    if (!on->onStairs) ++on->floor;
                } else if (up.on) {
                    if (up.down > 1.0f - up.step) on->climbing = true; // got on at the foot
                    if (on->climbing && up.down < up.step && !on->justSwitched) {
                        ++on->floor; // climbed to the top: on the floor above, at the top of its stairs down
                        on->climbing = false, on->justSwitched = true;
                    }
                }
            }
            // Left the top it switched at: armed again; off the stairs: neither climbing.
            {
                const OnFlight upNow = flight(on->floor), downNow = flight(on->floor - 1);
                const bool atAnyTop = (downNow.on && downNow.down < downNow.top) || (upNow.on && upNow.down < upNow.top);
                if (!atAnyTop) on->justSwitched = false;
                if (!upNow.on && !downNow.on) on->climbing = false;
            }
            // Climbing its floor's stairs: as high as it is up them.
            if (on->climbing) {
                const OnFlight now = flight(on->floor);
                if (now.on) on->climb = 1.0f - now.down;
            }
        }
        on->onStairs = onStairs;
    }
    // The player's building shows its interior, at the player's floor.
    const OnFloor* mine = player && registry.valid(*player) ? registry.try_get<OnFloor>(*player) : nullptr;
    for (const auto& b : buildings) {
        auto& interior = registry.get<Interior>(b.entity);
        const bool inside = mine && mine->building == b.entity;
        if (inside && !interior.shown) interior.climbing = true;
        interior.shown = inside;
        interior.floor = inside ? mine->floor : 1;
        interior.onStairs = inside && mine->onStairs;
    }
    // Each one stands its floor's storeys higher (eased: it climbs), and is hidden on a floor not shown (inside a building
    // the player isn't in: above the ground floor).
    for (auto [mover, on] : registry.view<OnFloor>().each()) {
        const Building* b = nullptr;
        for (const auto& each : buildings) {
            if (each.entity == on.building) b = &each;
        }
        const float target = b ? (on.floor - 1 + on.climb) * b->storey : 0.0f;
        on.lift += (target - on.lift) * 0.25f;
        if (std::abs(target - on.lift) < 0.5f) on.lift = target;
        bool hide = false;
        if (b && !(player && mover == *player)) {
            const auto& interior = registry.get<Interior>(b->entity);
            hide = interior.shown ? on.floor != interior.floor : on.floor > 1;
        }
        if (auto* sprite = registry.try_get<Sprite>(mover); sprite && hide != on.hidden) {
            if (hide) on.alpha = sprite->a, sprite->a = 0.0f;
            else sprite->a = on.alpha;
            on.hidden = hide;
        }
    }
    ApplyInteriors(registry);
}

void ApplyInteriors(Registry& registry) {
    std::vector<std::pair<std::string, bool>> hides; // a painted building's occluders, and whether its interior shows
    for (auto [entity, interior, sprite] : registry.view<Interior, Sprite>().each()) {
        if (interior.texture.empty() || interior.kind == "scene") continue;
        // The picture of the floor shown; any of its floors' is an interior picture (not the outside to put back).
        const std::string picture = InteriorPicture(interior, interior.floor);
        bool showingInside = sprite.texture == interior.texture;
        for (int k = 2; k <= InteriorFloors(interior) && !showingInside; ++k) showingInside = sprite.texture == InteriorPicture(interior, k);
        if (!interior.cells.empty()) {
            // Painted: the painting is its outside; the cutaway over it only while inside.
            sprite.texture = picture;
            sprite.a = interior.shown ? 1.0f : 0.0f;
        } else if (interior.shown && sprite.texture != picture) {
            if (!showingInside) interior.exteriorTexture = sprite.texture; // put back when it's left
            sprite.texture = picture;
        } else if (!interior.shown && showingInside && !interior.exteriorTexture.empty()) {
            sprite.texture = interior.exteriorTexture;
        }
        if (!interior.hides.empty()) hides.emplace_back(interior.hides, interior.shown);
    }
    if (hides.empty()) return;
    for (auto [entity, node, sprite] : registry.view<PlaceNode, Sprite>().each()) {
        for (const auto& [prefix, shown] : hides) {
            if (node.statement.rfind(prefix, 0) == 0) sprite.a = shown ? 0.0f : 1.0f;
        }
    }
}

} // namespace aige
