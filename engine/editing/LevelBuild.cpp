#include "editing/LevelBuild.h"

#include "assets/Prefab.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "physics2d/Components.h"

#include <cmath>
#include <sstream>

namespace aige {

std::optional<std::string> BuildLevel(Registry& registry, PrefabLibrary& prefabs, const LevelParams& params,
                                      const LevelLayout& layout, const LevelPrefabs& use, LevelBuildResult* result) {
    auto tilePrefab = [&](const std::string& path, const char* what) -> std::optional<std::string> {
        const PrefabInfo* info = prefabs.Find(path);
        if (info == nullptr || !info->isTile) return path + " isn't a tile prefab (" + what + ")";
        return std::nullopt;
    };
    if (auto e = tilePrefab(use.floor, "floor")) return e;
    if (auto e = tilePrefab(use.wall, "walls")) return e;
    for (const auto& [path, what] : {std::pair{use.enemy, "enemies"}, std::pair{use.pickup, "pickups"}}) {
        if (!path.empty()) {
            const PrefabInfo* info = prefabs.Find(path);
            if (info == nullptr || info->isTile) return path + " isn't a placeable prefab (" + std::string(what) + ")";
        }
    }

    LevelBuildResult built;
    auto inArea = [&](float x, float y) {
        int gx = static_cast<int>(std::floor(x));
        int gy = static_cast<int>(std::floor(y));
        return gx >= params.x0 && gx <= params.x1 && gy >= params.y0 && gy <= params.y1;
    };
    // Clear the area: tiles by cell, everything else by position.
    auto player = FindPlayer(registry);
    std::vector<Entity> doomed;
    for (auto [entity, tile] : registry.view<Tile>().each()) {
        if (tile.gx >= params.x0 && tile.gx <= params.x1 && tile.gy >= params.y0 && tile.gy <= params.y1) {
            doomed.push_back(entity);
        }
    }
    for (auto [entity, world] : registry.view<WorldTransform2D>(entt::exclude<Tile>).each()) {
        if (inArea(world.x, world.y) && (!player || entity != *player)) {
            doomed.push_back(entity);
        }
    }
    for (Entity entity : doomed) {
        if (registry.valid(entity)) {
            registry.destroy(entity);
            built.cleared++;
        }
    }

    for (const Cell& c : layout.floor) {
        if (prefabs.Instantiate(registry, use.floor, c.first + 0.5f, c.second + 0.5f)) built.floorTiles++;
    }
    for (const Cell& c : layout.walls) {
        if (prefabs.Instantiate(registry, use.wall, c.first + 0.5f, c.second + 0.5f)) built.wallTiles++;
    }
    if (!use.enemy.empty()) {
        for (const Cell& c : layout.enemies) {
            if (prefabs.Instantiate(registry, use.enemy, c.first + 0.5f, c.second + 0.5f)) built.enemies++;
        }
    }
    if (!use.pickup.empty()) {
        for (const Cell& c : layout.pickups) {
            if (prefabs.Instantiate(registry, use.pickup, c.first + 0.5f, c.second + 0.5f)) built.pickups++;
        }
    }
    if (use.movePlayer && player && registry.valid(*player)) {
        registry.get<WorldTransform2D>(*player) = {layout.playerStart.first + 0.5f, layout.playerStart.second + 0.5f};
    }
    if (result) *result = built;
    return std::nullopt;
}

LevelParams LevelParamsFromArgs(const std::vector<FieldValue>& args) {
    LevelParams p;
    p.x0 = ToInt(args[0]);
    p.y0 = ToInt(args[1]);
    p.x1 = ToInt(args[2]);
    p.y1 = ToInt(args[3]);
    p.seed = static_cast<uint32_t>(ToInt(args[4]));
    p.rooms = ToInt(args[5]);
    p.roomMin = ToInt(args[6]);
    p.roomMax = ToInt(args[7]);
    p.corridorWidth = ToInt(args[8]);
    p.loops = ToFloat(args[9]);
    p.enemies = ToInt(args[13]);
    p.pickups = ToInt(args[15]);
    return p;
}

LevelParams LevelParamsFromJson(const nlohmann::json& a) {
    LevelParams p;
    auto i = [&](const char* key, int fallback) { return a.contains(key) && a[key].is_number() ? a[key].get<int>() : fallback; };
    p.x0 = i("x0", p.x0);
    p.y0 = i("y0", p.y0);
    p.x1 = i("x1", p.x1);
    p.y1 = i("y1", p.y1);
    p.seed = static_cast<uint32_t>(i("seed", 1));
    p.rooms = i("rooms", p.rooms);
    p.roomMin = i("roomMin", p.roomMin);
    p.roomMax = i("roomMax", p.roomMax);
    p.corridorWidth = i("corridorWidth", p.corridorWidth);
    p.loops = a.contains("loops") && a["loops"].is_number() ? a["loops"].get<float>() : p.loops;
    p.enemies = i("enemies", p.enemies);
    p.pickups = i("pickups", p.pickups);
    return p;
}

std::string DescribeLevel(const LevelParams& params, const LevelLayout& layout) {
    std::ostringstream out;
    out << layout.rooms.size() << " rooms";
    if (static_cast<int>(layout.rooms.size()) < params.rooms) out << " (of " << params.rooms << " asked; no room for more)";
    out << ", " << layout.floor.size() << " floor and " << layout.walls.size() << " wall tiles, "
        << layout.enemies.size() << " enemies, " << layout.pickups.size() << " pickups. The farthest room is "
        << layout.pathLength << " steps from the start.\n"
        << "Map (north up): # wall  . floor  @ start  e enemy  + pickup\n"
        << layout.Ascii(params);
    return out.str();
}

} // namespace aige
