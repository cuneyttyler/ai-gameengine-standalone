#include "editing/Commands.h"

#include "assets/Prefab.h"
#include "assets/Project.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "physics2d/Components.h"

#include <cctype>
#include <cmath>
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
}

} // namespace aige
