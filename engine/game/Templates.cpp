#include "game/Templates.h"

#include "assets/Project.h"
#include "assets/ProjectTemplate.h"
#include "assets/SceneIO.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/Items.h"
#include "gameplay/Story.h"
#include "records/Records.h"
#include "world/WorldBible.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

const std::vector<GameTemplate>& GameTemplates() {
    static const std::vector<GameTemplate> templates = {
        {"arpg", "Isometric action RPG", "Diamond tiles, real-time melee, an elder's first quest, a wolf to fight.", false, "stone"},
        {"rpg", "Top-down RPG", "Square tiles, a village elder, a first quest and a foe on the road.", true, "parchment"},
        {"action-adventure", "Action-adventure", "Top-down rooms, a guide, a monster and a potion.", true, "pixel"},
        {"life-sim", "Top-down life-sim", "A field to tend, a shopkeeper, a neighbour's first errand.", true, "parchment"},
    };
    return templates;
}

const GameTemplate* FindGameTemplate(const std::string& id) {
    for (const auto& t : GameTemplates()) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

std::optional<std::string> CreateProjectFromTemplate(const fs::path& root, const std::string& templateId) {
    const GameTemplate* t = FindGameTemplate(templateId);
    if (!t) return "no template '" + templateId + "'";
    if (auto error = CreateProject(root, std::nullopt, t->topDown)) return error;
    const Project project{root};
    const bool action = templateId != "life-sim";

    // The starter scene: a walled field, the player, the elder (or neighbour), the genre's own.
    Registry registry;
    const int w = 24;
    for (int y = 0; y < w; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool edge = x == 0 || y == 0 || x == w - 1 || y == w - 1;
            CreateTile(registry, x, y, edge ? TileKind::Wall : (!action && x >= 14 && x <= 19 && y >= 4 && y <= 9) ? TileKind::Dirt : TileKind::Grass);
        }
    }
    CreatePlayer(registry, 6.5f, 12.5f);
    const std::string guideKey = action ? "character/elder" : "character/neighbour";
    const std::string guideName = action ? "Elder Maren" : "Neighbour Ola";
    const Entity guide = CreateNpc(registry, 9.5f, 12.5f);
    registry.emplace_or_replace<Name>(guide, guideName);
    registry.emplace_or_replace<Talk>(guide, "dialogue/" + fs::path(guideKey).filename().string() + ".json", guideKey, 1.5f);
    const Entity well = registry.create();
    registry.emplace<WorldTransform2D>(well, 18.5f, 18.5f);
    registry.emplace<Marker>(well, "well");
    registry.emplace<Name>(well, "Marker well");
    if (action) {
        Entity wolf = CreateEnemy(registry, 18.5f, 6.5f, entt::null);
        registry.emplace_or_replace<Name>(wolf, "Wolf");
        CreateHealthPickup(registry, 4.5f, 4.5f);
    } else {
        const Entity shop = CreateNpc(registry, 6.5f, 18.5f);
        registry.emplace_or_replace<Name>(shop, "Shopkeeper Ines");
        registry.emplace_or_replace<Talk>(shop, "dialogue/shopkeeper.json", "character/shopkeeper", 1.5f);
        for (int i = 0; i < 3; ++i) {
            const Entity seed = registry.create();
            registry.emplace<WorldTransform2D>(seed, 15.5f + i * 1.5f, 6.5f);
            registry.emplace<ItemPickup>(seed, "turnip", 1, false);
            registry.emplace<Name>(seed, "Turnip");
        }
    }
    std::error_code ec;
    fs::create_directories(root / "scenes", ec);
    SaveScene(registry, project.Resolve("scenes/main.aige").string());

    // The canon, a dialogue and the first quest.
    WorldBible bible{project};
    WorldEntry entry;
    entry.kind = "character";
    entry.id = fs::path(guideKey).filename().string();
    entry.name = guideName;
    entry.summary = action ? "The village elder, who knows the old roads." : "Your neighbour, who gardens by the well.";
    bible.Save(entry);
    if (!action) {
        WorldEntry shop{"character", "shopkeeper", "Shopkeeper Ines"};
        shop.summary = "Buys what you grow.";
        bible.Save(shop);
    }
    const json quest = action
        ? json::parse(R"({"name": "The First Road", "description": "The elder asks you to clear the road to the well.",
            "giver": "character/elder", "aliases": [{"id": "elder", "kind": "character", "fill": "character/elder"}],
            "stages": [{"index": 10, "journal": "Defeat the wolf on the road.", "objectives": [{"id": "wolf", "text": "Defeat the wolf", "type": "defeat", "target": "Enemy"}]},
                       {"index": 20, "journal": "Reach the old well.", "objectives": [{"id": "well", "text": "Reach the well", "type": "reach", "target": "well"}]},
                       {"index": 30, "journal": "Tell the elder.", "objectives": [{"id": "back", "text": "Talk to the elder", "type": "talk", "target": "@elder"}], "ends": "complete"}],
            "rewards": {"experience": 50, "gold": 20}})")
        : json::parse(R"({"name": "A Neighbour's Errand", "description": "Ola needs turnips from the field.",
            "giver": "character/neighbour", "aliases": [{"id": "ola", "kind": "character", "fill": "character/neighbour"}],
            "stages": [{"index": 10, "journal": "Pick three turnips in the field.", "objectives": [{"id": "turnips", "text": "Pick turnips", "type": "collect", "target": "turnip", "count": 3}]},
                       {"index": 20, "journal": "Bring them to Ola.", "objectives": [{"id": "back", "text": "Talk to Ola", "type": "talk", "target": "@ola"}], "ends": "complete"}],
            "rewards": {"gold": 30}})");
    WriteStoryFile(root, "quests", "q-welcome", quest);
    const json dialogue = {{"speaker", guideKey},
                           {"start", "greet"},
                           {"starts", {{{"conditions", {{{"quest", "q-welcome"}, {"is", "active"}}}}, {"node", "busy"}}}},
                           {"nodes",
                            {{{"id", "greet"},
                              {"text", action ? "The road to the well isn't safe. Will you clear it?" : "Could you pick me three turnips?"},
                              {"choices",
                               {{{"text", "I will."}, {"effects", {{{"op", "start_quest"}, {"name", "q-welcome"}}}},
                                 {"conditions", {{{"quest", "q-welcome"}, {"is", "inactive"}}}}},
                                {{"text", "Not now."}}}}},
                             {{"id", "busy"}, {"text", action ? "Be careful out there." : "The field's past the well."}}}}};
    WriteStoryFile(root, "dialogue", fs::path(guideKey).filename().string(), dialogue);
    if (!action) {
        WriteStoryFile(root, "dialogue", "shopkeeper",
                       {{"speaker", "character/shopkeeper"}, {"start", "greet"}, {"nodes", {{{"id", "greet"}, {"text", "Fresh turnips fetch a good price."}}}}});
    }

    // Records: an item or two, and achievements.
    RecordDatabase records;
    records.Open(EngineReflection(), root);
    if (action) {
        records.Write("item", "potion", {{"name", "Potion"}, {"kind", "consumable"}, {"value", 10}});
        records.Write("achievement", "first-blood", {{"name", "First Blood"}, {"description", "Defeat an enemy."}, {"kind", "event"}, {"event", "death"}, {"count", 1}});
    } else {
        records.Write("item", "turnip", {{"name", "Turnip"}, {"kind", "material"}, {"value", 5}});
        records.Write("achievement", "green-thumb", {{"name", "Green Thumb"}, {"description", "Pick three turnips."}, {"kind", "event"}, {"event", "pickup"}, {"target", "turnip"}, {"count", 3}});
    }
    records.Write("achievement", "first-steps", {{"name", "First Steps"}, {"description", "Finish your first quest."}, {"kind", "quest"}, {"quest", "q-welcome"}, {"state", "completed"}});

    // Its look and its own file.
    fs::create_directories(root / "ui", ec);
    std::ofstream(root / "ui" / "theme.json", std::ios::binary) << json{{"base", t->theme}, {"tokens", json::object()}}.dump(2);
    std::ofstream(root / "game.json", std::ios::binary)
        << json{{"schema", "aige.game/1"}, {"title", t->name}, {"genre", t->id}, {"startScene", "scenes/main.aige"}, {"theme", t->theme},
                {"template", t->id}}
               .dump(2);
    return std::nullopt;
}

} // namespace aige
