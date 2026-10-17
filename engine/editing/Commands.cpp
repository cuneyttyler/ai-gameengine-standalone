#include "editing/Commands.h"

#include "assets/PackImport.h"
#include "assets/Prefab.h"
#include "assets/TextureCook.h"
#include "gameplay/BootstrapLevel.h"
#include "assets/Project.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "physics2d/Components.h"
#include "editing/Autotile.h"
#include "editing/LevelBuild.h"
#include "editing/VillageBuild.h"
#include "assets/AssetDatabase.h"
#include "renderer2d/Components.h"
#include "world/WorldBible.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace aige {

namespace {

constexpr int kMaxPaintCells = 64 * 64;

CommandResult Fail(std::string message) {
    return {false, std::move(message)};
}

bool IsSafeScriptPath(const std::string& path) {
    // scripts/<name>.lua, possibly in subfolders; never outside the folder.
    return path.rfind("scripts/", 0) == 0 && path.size() > 12 && path.substr(path.size() - 4) == ".lua" &&
           path.find("..") == std::string::npos && path.find(':') == std::string::npos &&
           path.find('\\') == std::string::npos;
}

bool IsSafeName(const std::string& name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

} // namespace

void RegisterEditingCommands(ReflectionRegistry& r) {
    if (r.FindCommand("scene.spawn") != nullptr) {
        return;
    }

    r.Command("scene.spawn", "Places a copy of a prefab at a world position. Placing the player prefab when a "
                             "player exists moves that player instead.")
        .Param("prefab", FieldType::Prefab, "The prefab, e.g. prefabs/skeleton.prefab.")
        .Param("x", FieldType::Float, "World x (cell centers are n + 0.5).")
        .Param("y", FieldType::Float, "World y.")
        .Run([](CommandContext& context, const std::vector<FieldValue>& args) -> CommandResult {
            if (context.prefabs == nullptr) {
                return Fail("prefabs aren't available here");
            }
            const std::string prefab = ToString(args[0]);
            const PrefabInfo* info = context.prefabs->Find(prefab);
            if (info == nullptr) {
                return Fail("no prefab " + prefab);
            }
            float x = ToFloat(args[1]);
            float y = ToFloat(args[2]);
            if (info->isPlayer) {
                if (auto player = FindPlayer(context.registry)) {
                    context.registry.get<WorldTransform2D>(*player) = {x, y};
                    return {true, {}, *player};
                }
            }
            auto entity = context.prefabs->Instantiate(context.registry, prefab, x, y);
            if (!entity) {
                return Fail("couldn't place " + prefab);
            }
            return {true, {}, *entity};
        });

    r.Command("scene.paintRect", "Fills a rectangle of grid cells (inclusive) with a tile prefab, replacing "
                                 "whatever tiles were there.")
        .Param("prefab", FieldType::Prefab, "A tile prefab, e.g. prefabs/grass.prefab.")
        .Param("x0", FieldType::Int, "First column.").Range(-1000, 1000)
        .Param("y0", FieldType::Int, "First row.").Range(-1000, 1000)
        .Param("x1", FieldType::Int, "Last column.").Range(-1000, 1000)
        .Param("y1", FieldType::Int, "Last row.").Range(-1000, 1000)
        .Run([](CommandContext& context, const std::vector<FieldValue>& args) -> CommandResult {
            if (context.prefabs == nullptr) {
                return Fail("prefabs aren't available here");
            }
            const std::string prefab = ToString(args[0]);
            const PrefabInfo* info = context.prefabs->Find(prefab);
            if (info == nullptr || !info->isTile) {
                return Fail(prefab + " isn't a tile prefab");
            }
            int x0 = std::min(ToInt(args[1]), ToInt(args[3]));
            int x1 = std::max(ToInt(args[1]), ToInt(args[3]));
            int y0 = std::min(ToInt(args[2]), ToInt(args[4]));
            int y1 = std::max(ToInt(args[2]), ToInt(args[4]));
            if ((x1 - x0 + 1) * (y1 - y0 + 1) > kMaxPaintCells) {
                return Fail("that's more than " + std::to_string(kMaxPaintCells) + " cells");
            }
            for (int gx = x0; gx <= x1; ++gx) {
                for (int gy = y0; gy <= y1; ++gy) {
                    context.prefabs->Instantiate(context.registry, prefab, gx + 0.5f, gy + 0.5f);
                }
            }
            return {};
        });

    r.Command("entity.delete", "Removes an entity from the scene.")
        .Param("entity", FieldType::EntityRef, "The entity to remove.")
        .Run([](CommandContext& context, const std::vector<FieldValue>& args) -> CommandResult {
            Entity entity = ToEntity(args[0]);
            if (!context.registry.valid(entity)) {
                return Fail("no such entity");
            }
            context.registry.destroy(entity);
            return {};
        });

    r.Command("prefab.createFromEntity", "Saves an entity as a new prefab (prefabs/<name>.prefab); the entity "
                                         "becomes its first copy.")
        .Param("entity", FieldType::EntityRef, "The entity to save.")
        .Param("name", FieldType::String, "File name: letters, digits, - and _.")
        .Run([](CommandContext& context, const std::vector<FieldValue>& args) -> CommandResult {
            if (context.prefabs == nullptr) {
                return Fail("prefabs aren't available here");
            }
            Entity entity = ToEntity(args[0]);
            std::string name = ToString(args[1]);
            if (!context.registry.valid(entity)) {
                return Fail("no such entity");
            }
            if (!IsSafeName(name)) {
                return Fail("prefab names can use letters, digits, - and _");
            }
            std::string path = "prefabs/" + name + ".prefab";
            if (context.prefabs->Find(path) != nullptr) {
                return Fail(path + " already exists");
            }
            std::string error;
            if (!context.prefabs->SaveAsPrefab(context.registry, entity, path, &error)) {
                return Fail(error);
            }
            return {};
        });

    r.Command("prefab.create",
              "Makes a prefab from an image asset (development_plan.md 6.3): a ground tile (painted onto cells, "
              "lying flat) or a prop (placed, upright). Size props by the asset's analysis: \"cells\" is how many "
              "grid cells wide it should be; 0 sizes it by its pack's scale. Saved as prefabs/<name>.prefab.")
        .Param("asset", FieldType::Texture, "The image, e.g. assets/pack/well.png.")
        .Param("name", FieldType::String, "Prefab path under prefabs/, without .prefab: letters, digits, - _ and /.")
        .Param("tile", FieldType::Bool, "true: a ground tile; false: a prop.")
        .Param("solid", FieldType::Bool, "Tiles: blocks movement. Props: blocks its cell.")
        .Param("cells", FieldType::Float, "Props: width in grid cells (0 = by the pack's scale).").Range(0, 20)
        .Run([](CommandContext& context, const std::vector<FieldValue>& args) -> CommandResult {
            if (context.prefabs == nullptr || context.project == nullptr) {
                return Fail("prefabs aren't available here");
            }
            const std::string asset = ToString(args[0]);
            const std::string name = ToString(args[1]);
            if (asset.rfind("assets/", 0) != 0 || asset.find("..") != std::string::npos) {
                return Fail("not an asset path: " + asset);
            }
            for (char c : name) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '/') {
                    return Fail("prefab names can use letters, digits, -, _ and /");
                }
            }
            if (name.empty() || name.find("..") != std::string::npos || name.front() == '/') {
                return Fail("a prefab needs a name");
            }
            auto image = DecodeImage(context.project->Resolve(asset));
            if (!image || image->width == 0) {
                return Fail("can't read the image " + asset);
            }
            const IsoParams iso = BootstrapIso();
            PrefabPlan plan;
            plan.asset = asset;
            plan.name = name.substr(name.find_last_of('/') + 1);
            plan.path = "prefabs/" + name + ".prefab";
            plan.tile = ToBool(args[2]);
            plan.solid = ToBool(args[3]);
            float cells = ToFloat(args[4]);
            if (!plan.tile) {
                if (cells <= 0.0f) {
                    const std::string folder = std::filesystem::path(asset).parent_path().generic_string();
                    const float ppc = ReadPackScale(*context.project, folder).value_or(static_cast<float>(image->width));
                    cells = static_cast<float>(image->width) / ppc;
                }
                plan.width = cells * iso.tileWidth;
                plan.height = plan.width * static_cast<float>(image->height) / static_cast<float>(image->width);
            } else {
                plan.width = iso.tileWidth;
                plan.height = iso.tileHeight;
            }
            if (std::string error = CreatePrefabFromPlan(*context.prefabs, plan, iso); !error.empty()) {
                return Fail(error);
            }
            return {};
        });

    // Autotiling (6.4): say where the terrain goes; the engine picks the pieces.
    auto terrainCommand = [](CommandContext& context, const std::string& terrain, std::string background,
                             const std::vector<std::pair<int, int>>& cells) -> CommandResult {
        if (context.prefabs == nullptr || context.project == nullptr) {
            return Fail("prefabs aren't available here");
        }
        const Project& project = *context.project;
        const TerrainSet set = BuildTerrainSet(*context.prefabs, [&](const std::string& asset) {
            return ReadAssetAnalysis(project, asset);
        });
        if (set.tiles.empty()) {
            return Fail("no analyzed ground tile prefabs: analyze the pack's tiles (Assets ▸ Analyze) and import them first");
        }
        if (std::find(set.terrains.begin(), set.terrains.end(), terrain) == set.terrains.end()) {
            std::string known;
            for (const auto& t : set.terrains) known += (known.empty() ? "" : ", ") + t;
            return Fail("no terrain " + terrain + " (the tiles have: " + known + ")");
        }
        if (background.empty()) background = DefaultBackground(set, terrain);
        if (cells.empty()) {
            return Fail("no cells to paint");
        }
        if (cells.size() > 20000) {
            return Fail("too many cells at once (at most 20000)");
        }
        PaintTerrain(context.registry, *context.prefabs, set, cells, terrain, background);
        return {};
    };

    r.Command("terrain.paintRect",
              "Paints a rectangle of cells with a terrain (e.g. dirt for a village square) and lets the engine pick "
              "every tile, edges and corners included, from the analyzed ground tiles. Use the terrain names in the "
              "assets' \"edges\".")
        .Param("terrain", FieldType::String, "The terrain to paint, e.g. dirt.")
        .Param("background", FieldType::String, "What surrounds it, e.g. grass; empty for the usual.")
        .Param("x0", FieldType::Int, "First column.").Range(-1000, 1000)
        .Param("y0", FieldType::Int, "First row.").Range(-1000, 1000)
        .Param("x1", FieldType::Int, "Last column.").Range(-1000, 1000)
        .Param("y1", FieldType::Int, "Last row.").Range(-1000, 1000)
        .Run([terrainCommand](CommandContext& context, const std::vector<FieldValue>& args) -> CommandResult {
            const int x0 = std::min(ToInt(args[2]), ToInt(args[4])), x1 = std::max(ToInt(args[2]), ToInt(args[4]));
            const int y0 = std::min(ToInt(args[3]), ToInt(args[5])), y1 = std::max(ToInt(args[3]), ToInt(args[5]));
            std::vector<std::pair<int, int>> cells;
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) cells.push_back({x, y});
            return terrainCommand(context, ToString(args[0]), ToString(args[1]), cells);
        });

    r.Command("terrain.paintPath",
              "Lays a road (or a river) through points, along the grid, and lets the engine pick every tile: "
              "straights, corners, ends and junctions match their neighbours. Use the terrain names in the assets' "
              "\"edges\".")
        .Param("terrain", FieldType::String, "The terrain to lay, e.g. dirt.")
        .Param("background", FieldType::String, "What surrounds it, e.g. grass; empty for the usual.")
        .Param("points", FieldType::String, "The cells it passes through, in order: \"x,y;x,y;...\".")
        .Param("width", FieldType::Int, "Cells wide.").Range(1, 9)
        .Run([terrainCommand](CommandContext& context, const std::vector<FieldValue>& args) -> CommandResult {
            const auto points = ParseCells(ToString(args[2]));
            if (points.empty()) {
                return Fail("points are \"x,y;x,y;...\"");
            }
            return terrainCommand(context, ToString(args[0]), ToString(args[1]), PathCells(points, ToInt(args[3])));
        });

    r.Command("script.write", "Creates or replaces a Lua script (see docs/scripting.md for the API scripts can "
                              "use). Attach it with a LuaScript component.")
        .Param("path", FieldType::Script, "Where to write it: scripts/<name>.lua.")
        .Param("source", FieldType::String, "The complete Lua source.")
        .Run([](CommandContext& context, const std::vector<FieldValue>& args) -> CommandResult {
            if (context.project == nullptr) {
                return Fail("no project is open");
            }
            std::string path = ToString(args[0]);
            if (!IsSafeScriptPath(path)) {
                return Fail("scripts must be written to scripts/<name>.lua");
            }
            std::filesystem::path file = context.project->Resolve(path);
            std::error_code ec;
            std::filesystem::create_directories(file.parent_path(), ec);
            std::ofstream out(file, std::ios::trunc | std::ios::binary);
            if (!out) {
                return Fail("can't write " + path);
            }
            out << ToString(args[1]);
            return {};
        });

    // World Bible (development_plan.md 3.1): canon entries in world/<kind>/<id>.md.
    r.Command("world.write", "Creates or replaces a World Bible entry (canon: characters, factions, locations, items, "
                             "events, lore, style).")
        .Param("kind", FieldType::String, "character, faction, location, item, event, lore or style.")
        .Param("id", FieldType::String, "File name: lowercase letters, digits, - and _ (empty: made from the name).")
        .Param("name", FieldType::String, "Display name.")
        .Param("summary", FieldType::String, "One sentence.")
        .Param("tags", FieldType::String, "Comma-separated tags (may be empty).")
        .Param("relations", FieldType::String,
               "Links to other entries, '; '-separated, each '<type> <kind>/<id>', e.g. "
               "'lives_in location/brookvale; member_of faction/iron-guild' (may be empty).")
        .Param("body", FieldType::String, "The full description, Markdown.")
        .Run([](CommandContext& context, const std::vector<FieldValue>& args) -> CommandResult {
            if (context.project == nullptr) {
                return Fail("no project is open");
            }
            WorldEntry entry;
            entry.kind = ToString(args[0]);
            entry.name = ToString(args[2]);
            entry.id = ToString(args[1]).empty() ? WorldIdFromName(entry.name) : ToString(args[1]);
            entry.summary = ToString(args[3]);
            entry.tags = ParseTagList(ToString(args[4]));
            entry.relations = ParseRelationList(ToString(args[5]));
            entry.body = ToString(args[6]);
            WorldBible world(*context.project);
            world.Refresh();
            if (const WorldEntry* existing = world.Find(entry.Key())) {
                entry.extra = existing->extra; // hand-written header keys survive
            }
            if (auto error = world.Save(entry)) {
                return Fail(*error);
            }
            return {};
        });

    // Named places for routines (development_plan.md 3.5).
    r.Command("scene.addMarker", "Adds a named place (a Marker) that schedules can send people to, e.g. "
                                 "herb_garden. Invisible in play. Use a free, walkable cell.")
        .Param("name", FieldType::String, "Lowercase letters, digits and _, e.g. village_well.")
        .Param("x", FieldType::Float, "World x (cell centers are n + 0.5).")
        .Param("y", FieldType::Float, "World y.")
        .Run([](CommandContext& context, const std::vector<FieldValue>& args) -> CommandResult {
            const std::string name = ToString(args[0]);
            bool safe = !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) {
                return (std::isalnum(c) && !std::isupper(c)) || c == '_' || c == '-';
            });
            if (!safe) {
                return Fail("marker names are lowercase letters, digits and _");
            }
            for (auto [entity, marker] : context.registry.view<Marker>().each()) {
                if (marker.name == name) {
                    return Fail("there's already a marker called " + name);
                }
            }
            Entity entity = context.registry.create();
            context.registry.emplace<Name>(entity, name);
            context.registry.emplace<WorldTransform2D>(entity, ToFloat(args[1]), ToFloat(args[2]));
            context.registry.emplace<ScreenTransform2D>(entity);
            context.registry.emplace<Marker>(entity, name);
            return {true, {}, entity};
        });

    // Level generation (development_plan.md 3.4).
    r.Command("level.generate",
              "Builds a dungeon in a rectangle of grid cells: rooms joined by corridors, walled in, every room "
              "reachable, with enemies (more of them further from the start) and pickups. Everything already in "
              "the area except the player is replaced. Deterministic: the same seed gives the same level. Pick "
              "an area away from what should stay (see the tile bounds).")
        .Param("x0", FieldType::Int, "Area: first column.").Range(-1000, 1000)
        .Param("y0", FieldType::Int, "Area: first row.").Range(-1000, 1000)
        .Param("x1", FieldType::Int, "Area: last column.").Range(-1000, 1000)
        .Param("y1", FieldType::Int, "Area: last row.").Range(-1000, 1000)
        .Param("seed", FieldType::Int, "Any number; change it for a different layout.").Range(0, 1000000000)
        .Param("rooms", FieldType::Int, "How many rooms to try to fit.").Range(2, 40)
        .Param("roomMin", FieldType::Int, "Smallest room side, in cells.").Range(2, 30)
        .Param("roomMax", FieldType::Int, "Largest room side, in cells.").Range(2, 30)
        .Param("corridorWidth", FieldType::Int, "Corridor width, cells.").Range(1, 3)
        .Param("loops", FieldType::Float, "0-1: extra corridors so not every path is a dead end.").Range(0, 1)
        .Param("floor", FieldType::Prefab, "Tile prefab for the floor, e.g. prefabs/stone_floor.prefab.")
        .Param("wall", FieldType::Prefab, "Solid tile prefab for the walls, e.g. prefabs/wall.prefab.")
        .Param("enemy", FieldType::Prefab, "Enemy prefab; empty for none.")
        .Param("enemies", FieldType::Int, "How many enemies.").Range(0, 200)
        .Param("pickup", FieldType::Prefab, "Pickup prefab (e.g. a potion); empty for none.")
        .Param("pickups", FieldType::Int, "How many pickups.").Range(0, 100)
        .Param("movePlayer", FieldType::Bool, "Move the player to the start room.")
        .Run([](CommandContext& context, const std::vector<FieldValue>& args) -> CommandResult {
            if (context.prefabs == nullptr) {
                return Fail("prefabs aren't available here");
            }
            LevelParams params = LevelParamsFromArgs(args);
            std::string error;
            auto layout = GenerateLevel(params, &error);
            if (!layout) {
                return Fail(error);
            }
            LevelPrefabs use{ToString(args[10]), ToString(args[11]), ToString(args[12]), ToString(args[14]),
                             ToBool(args[16])};
            if (auto problem = BuildLevel(context.registry, *context.prefabs, params, *layout, use)) {
                return Fail(*problem);
            }
            return {};
        });

    // Settlements (development_plan.md 6.5).
    r.Command("level.village",
              "Builds a whole village from the pack's analyzed prefabs: streets laid with fitted road tiles, a square "
              "with a well, houses on plots facing the streets (sized from the house art), fenced gardens, "
              "decorations by the doors, banners at the entrances, a landmark (tower, windmill), and trees, bushes "
              "and rocks around the edge. It grows right and down from (x0,y0); its size follows from the number "
              "of houses. Everything already in the area except the player is replaced. Deterministic: the same "
              "seed gives the same village. Use this for any village or town instead of placing pieces one by one.")
        .Param("x0", FieldType::Int, "Top-left column.").Range(-1000, 1000)
        .Param("y0", FieldType::Int, "Top-left row.").Range(-1000, 1000)
        .Param("houses", FieldType::Int, "How many houses: 4-8 a hamlet, 10-20 a village, 25+ a town.").Range(1, 60)
        .Param("seed", FieldType::Int, "Any number; change it for a different layout.").Range(0, 1000000000)
        .Param("roadWidth", FieldType::Int, "Street width, cells.").Range(1, 3)
        .Param("houseCells", FieldType::Int, "A house's footprint in cells; 0 = from the house art.").Range(0, 6)
        .Param("nature", FieldType::Float, "0-1: how many trees, bushes and rocks.").Range(0, 1)
        .Param("fences", FieldType::Bool, "Fence the gardens behind the houses.")
        .Param("square", FieldType::Bool, "A square with a well where the main streets cross.")
        .Param("ground", FieldType::String, "Ground terrain (from the tiles' edges), e.g. grass; empty = found.")
        .Param("road", FieldType::String, "Road terrain, e.g. dirt; empty = found.")
        .Param("movePlayer", FieldType::Bool, "Move the player onto the main street.")
        .Run([](CommandContext& context, const std::vector<FieldValue>& args) -> CommandResult {
            if (context.prefabs == nullptr || context.project == nullptr) {
                return Fail("prefabs aren't available here");
            }
            const VillageRequest request = VillageRequestFromArgs(args);
            std::string error;
            auto plan = PlanVillage(*context.prefabs, *context.project, request, &error);
            if (!plan) {
                return Fail(error);
            }
            if (auto problem = BuildVillage(context.registry, *context.prefabs, *plan, request.movePlayer)) {
                return Fail(*problem);
            }
            return {};
        });

    r.Command("world.delete", "Deletes a World Bible entry.")
        .Param("key", FieldType::String, "The entry, '<kind>/<id>'.")
        .Run([](CommandContext& context, const std::vector<FieldValue>& args) -> CommandResult {
            if (context.project == nullptr) {
                return Fail("no project is open");
            }
            WorldBible world(*context.project);
            world.Refresh();
            if (auto error = world.Remove(ToString(args[0]))) {
                return Fail(*error);
            }
            return {};
        });
}

} // namespace aige
