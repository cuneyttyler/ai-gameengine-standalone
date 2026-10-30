#include "gameplay/Interiors.h"

#include "gameplay/Components.h"
#include "gameplay/Settings.h"

#include <nlohmann/json.hpp>
#include "gameplay/Archetypes.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

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
        door.door = d.value("door", true);
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
                        {"consumeKey", d.consumeKey}, {"trigger", d.trigger}, {"prompt", d.prompt}, {"door", d.door}});
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
            if (std::sscanf(cell.c_str(), "%d,%d", &x, &y) == 2) cells.push_back({x, y});
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

std::vector<std::pair<float, float>> InteriorSolids(const Interior& interior, int floor) {
    std::vector<std::pair<float, float>> quarters;
    std::stringstream in(interior.solids);
    for (std::string part; std::getline(in, part, ';');) {
        const size_t colon = part.find(':');
        if (colon == std::string::npos || std::atoi(part.substr(0, colon).c_str()) != floor) continue;
        std::stringstream each(part.substr(colon + 1));
        for (std::string q; std::getline(each, q, '|');) {
            int x = 0, y = 0;
            if (std::sscanf(q.c_str(), "%d,%d", &x, &y) == 2) quarters.push_back({x * 0.5f, y * 0.5f});
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

void UpdateInteriors(Registry& registry, std::optional<Entity> player) {
    // The buildings one goes into (cutaways), each its footprint and a storey's height on screen.
    struct Building {
        Entity entity;
        int x0, y0, x1, y1, floors;
        float storey;
    };
    std::vector<Building> buildings;
    for (auto [entity, interior] : registry.view<Interior>().each()) {
        if (interior.texture.empty() || interior.kind == "scene") continue;
        Building b{entity, 0, 0, -1, -1, InteriorFloors(interior), interior.storeyHeight};
        if (!InteriorFootprint(registry, entity, interior, b.x0, b.y0, b.x1, b.y1)) continue;
        if (b.storey <= 0.0f) {
            b.storey = InteriorStorey(registry, entity, interior);
        }
        buildings.push_back(b);
    }
    auto buildingAt = [&](float wx, float wy) -> const Building* {
        const int px = static_cast<int>(std::floor(wx)), py = static_cast<int>(std::floor(wy));
        for (const auto& b : buildings) {
            if (px >= b.x0 && px <= b.x1 && py >= b.y0 && py <= b.y1) return &b;
        }
        return nullptr;
    };
    // Each character's floor (the player's and everyone's): in through a door, the ground floor; a floor change's stairs
    // walked onto from the floor below go up, from the floor above down; off them before they go again.
    for (auto [mover, velocity, world] : registry.view<Velocity2D, WorldTransform2D>().each()) {
        const Building* b = buildingAt(world.x, world.y);
        auto* on = registry.try_get<OnFloor>(mover);
        if (b == nullptr) {
            if (on) on->building = entt::null, on->floor = 1, on->onStairs = false;
            continue;
        }
        if (on == nullptr) on = &registry.emplace<OnFloor>(mover);
        if (on->building != b->entity) on->building = b->entity, on->floor = 1, on->onStairs = false;
        bool onStairs = false;
        if (b->floors > 1) {
            const Interior& interior = registry.get<Interior>(b->entity);
            const std::pair<int, int> cell{static_cast<int>(std::floor(world.x)), static_cast<int>(std::floor(world.y))};
            const auto up = StairsCells(interior, on->floor, b->x0, b->y0, b->x1, b->y1);       // up from its floor
            const auto down = StairsCells(interior, on->floor - 1, b->x0, b->y0, b->x1, b->y1); // down to the one below
            const bool onUp = on->floor < b->floors && std::find(up.begin(), up.end(), cell) != up.end();
            const bool onDown = on->floor > 1 && std::find(down.begin(), down.end(), cell) != down.end();
            onStairs = onUp || onDown;
            if (onStairs && !on->onStairs) on->floor = std::clamp(on->floor + (onUp ? 1 : -1), 1, b->floors);
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
        const float target = b ? (on.floor - 1) * b->storey : 0.0f;
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
