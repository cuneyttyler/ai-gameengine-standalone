#include "gameplay/Interiors.h"

#include "gameplay/Components.h"
#include "gameplay/Settings.h"
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

std::string DoorOpens(const AnimatedPart& part) {
    if (part.opens == "auto" || part.opens == "use") return part.opens;
    if (!part.opensNear) return "use";
    return ActiveSettings().game.doorsOpen == "auto" ? "auto" : "use";
}

bool DoorwayShut(Registry& registry, Entity entity) {
    const auto* part = registry.try_get<AnimatedPart>(entity);
    if (part == nullptr || part->sheet.empty()) return false;
    if (const auto* open = registry.try_get<PartOpen>(entity); open && open->open) return false;
    return part->locked || DoorOpens(*part) == "use";
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

std::pair<int, int> StairsCell(const Interior& interior, int change, int x0, int y0, int x1, int y1) {
    // The listed cell of floor change `change` (1: floor 1 to 2), else the footprint's corners in turn: top, right, left.
    std::vector<std::pair<int, int>> listed;
    std::stringstream in(interior.stairs);
    std::string part;
    while (std::getline(in, part, ';')) {
        int x = 0, y = 0;
        if (std::sscanf(part.c_str(), "%d,%d", &x, &y) == 2) listed.push_back({x, y});
    }
    if (change >= 1 && change <= static_cast<int>(listed.size())) return listed[static_cast<size_t>(change - 1)];
    switch ((std::max(change, 1) - 1) % 3) {
        case 1: return {x1, y0}; // the right corner
        case 2: return {x0, y1}; // the left corner
        default: return {x0, y0}; // the back (top) corner
    }
}

int InteriorFloors(const Interior& interior) {
    return interior.texture.empty() ? 0 : 1 + static_cast<int>(UpperFloors(interior).size());
}

std::string InteriorPicture(const Interior& interior, int floor) {
    const auto upper = UpperFloors(interior);
    return floor >= 2 && floor - 2 < static_cast<int>(upper.size()) ? upper[floor - 2] : interior.texture;
}

void UpdateInteriors(Registry& registry, std::optional<Entity> player) {
    const WorldTransform2D* at = player && registry.valid(*player) ? registry.try_get<WorldTransform2D>(*player) : nullptr;
    for (auto [entity, interior] : registry.view<Interior>().each()) {
        if (interior.texture.empty() || interior.kind == "scene") continue;
        int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
        bool inside = false;
        int px = 0, py = 0;
        if (at != nullptr && InteriorFootprint(registry, entity, interior, x0, y0, x1, y1)) {
            px = static_cast<int>(std::floor(at->x)), py = static_cast<int>(std::floor(at->y));
            inside = px >= x0 && px <= x1 && py >= y0 && py <= y1;
        }
        if (inside && !interior.shown) interior.floor = 1, interior.climbing = true; // in through the door: the ground floor
        interior.shown = inside;
        if (!inside) {
            interior.floor = 1;
            interior.onStairs = false;
            continue;
        }
        // Its stairs: each floor change has its cell; walked onto from the floor below it goes up, from the floor above
        // down; off it before it goes again.
        const int floors = InteriorFloors(interior);
        bool onStairs = false;
        if (floors > 1) {
            const auto up = StairsCell(interior, interior.floor, x0, y0, x1, y1);   // up from this floor
            const auto down = StairsCell(interior, interior.floor - 1, x0, y0, x1, y1); // down to the one below
            const bool onUp = interior.floor < floors && px == up.first && py == up.second;
            const bool onDown = interior.floor > 1 && px == down.first && py == down.second;
            onStairs = onUp || onDown;
            if (onStairs && !interior.onStairs) interior.floor = std::clamp(interior.floor + (onUp ? 1 : -1), 1, floors);
        }
        interior.onStairs = onStairs;
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
