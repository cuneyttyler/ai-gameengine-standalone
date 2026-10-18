#include "ai_bridge/ContextBuilder.h"

#include "assets/SceneFiles.h"

#include "assets/AssetDatabase.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "reflection/Reflection.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"
#include "gameplay/Schedule.h"
#include "gameplay/Story.h"
#include "world/WorldBible.h"

#include <algorithm>
#include <climits>
#include <filesystem>
#include <fstream>
#include <map>

namespace aige {

namespace {

// Components worth telling the model about: visible, and not recomputed
// every frame (ScreenTransform2D would just be noise).
bool Describable(const ComponentInfo& component) {
    return component.inspectorVisible && component.name != "ScreenTransform2D";
}

nlohmann::json ComponentValues(const ReflectionRegistry& reflection, Registry& registry, Entity entity,
                               bool includeInstanceLocal) {
    nlohmann::json components = nlohmann::json::object();
    for (const auto& component : reflection.Components()) {
        if (!Describable(component) || !component.has(registry, entity)) {
            continue;
        }
        nlohmann::json fields = nlohmann::json::object();
        for (const auto& field : component.fields) {
            if (!includeInstanceLocal && IsInstanceLocal(component.name, field)) {
                continue;
            }
            fields[field.name] = ValueToJson(field, field.get(registry, entity));
        }
        components[component.name] = fields;
    }
    return components;
}

nlohmann::json ScriptPropertiesJson(const LuaScript& script) {
    nlohmann::json properties = nlohmann::json::object();
    for (const auto& [name, value] : script.properties) {
        std::visit([&](const auto& v) { properties[name] = v; }, value);
    }
    return properties;
}

} // namespace

nlohmann::json BuildEditorContext(Registry& registry, const ReflectionRegistry& reflection, PrefabLibrary& prefabs,
                                  const AssetDatabase& assets, const Project& project, const ContextOptions& options) {
    nlohmann::json entities = nlohmann::json::array();
    std::map<std::string, int> tilesByPrefab;
    int tileCount = 0;
    int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;

    for (auto entity : EntitiesInOrder(registry)) {
        const auto* link = registry.try_get<PrefabInstance>(entity);
        if (const auto* tile = registry.try_get<Tile>(entity)) {
            ++tileCount;
            ++tilesByPrefab[link ? link->path : (tile->solid ? "(wall)" : "(floor)")];
            minX = std::min(minX, tile->gx);
            minY = std::min(minY, tile->gy);
            maxX = std::max(maxX, tile->gx);
            maxY = std::max(maxY, tile->gy);
            continue;
        }
        if (entities.size() >= options.maxEntities) {
            continue;
        }
        nlohmann::json entry = {{"id", static_cast<uint32_t>(entity)},
                                {"components", ComponentValues(reflection, registry, entity, true)}};
        if (const auto* name = registry.try_get<Name>(entity)) {
            entry["name"] = name->value;
        }
        if (link) {
            entry["prefab"] = link->path;
        }
        if (const auto* script = registry.try_get<LuaScript>(entity); script && !script->properties.empty()) {
            entry["scriptProperties"] = ScriptPropertiesJson(*script);
        }
        entities.push_back(entry);
    }

    nlohmann::json tiles = {{"count", tileCount}, {"byPrefab", tilesByPrefab}};
    if (tileCount > 0) {
        tiles["bounds"] = {minX, minY, maxX, maxY};
    }

    nlohmann::json prefabList = nlohmann::json::array();
    for (const auto& info : prefabs.Prefabs()) {
        if (!info.error.empty()) {
            continue;
        }
        nlohmann::json entry = {{"path", info.path}, {"name", info.name}, {"isTile", info.isTile}};
        prefabs.VisitTemplate(info.path, [&](Registry& templateRegistry, Entity templateEntity) {
            entry["components"] = ComponentValues(reflection, templateRegistry, templateEntity, false);
        });
        prefabList.push_back(entry);
    }

    nlohmann::json assetList = nlohmann::json::array();
    for (const auto& asset : assets.Assets()) {
        if (asset.error.empty()) {
            nlohmann::json entry = {{"path", asset.path}, {"type", AssetTypeName(asset.type)}};
            // What the art is (6.2): its description, kind, tags, direction,
            // size in cells and border terrains, when it has been analyzed.
            for (const auto& [key, value] : asset.analysis) {
                entry[key] = value;
            }
            if (asset.width > 0) {
                entry["pixels"] = {asset.width, asset.height};
            }
            assetList.push_back(entry);
        }
    }

    // Script paths, plus their sources so the model can extend an existing
    // behavior instead of replacing it blind (bounded: scripts are small, but
    // a project could have many).
    constexpr size_t kMaxScriptChars = 6000;
    constexpr size_t kMaxTotalScriptChars = 24000;
    nlohmann::json scriptSources = nlohmann::json::object();
    size_t totalScriptChars = 0;
    nlohmann::json scripts = nlohmann::json::array();
    std::error_code ec;
    std::filesystem::path scriptDir = project.root / "scripts";
    if (std::filesystem::exists(scriptDir, ec)) {
        std::vector<std::string> found;
        for (const auto& file : std::filesystem::recursive_directory_iterator(scriptDir, ec)) {
            if (file.is_regular_file() && file.path().extension() == ".lua") {
                found.push_back(project.Relative(file.path()));
            }
        }
        std::sort(found.begin(), found.end());
        scripts = found;
        for (const auto& path : found) {
            std::ifstream in(project.Resolve(path), std::ios::binary);
            std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (source.size() > kMaxScriptChars) {
                source = source.substr(0, kMaxScriptChars) + "\n-- (truncated)";
            }
            if (totalScriptChars + source.size() > kMaxTotalScriptChars) {
                break;
            }
            totalScriptChars += source.size();
            scriptSources[path] = source;
        }
    }

    nlohmann::json settings = nlohmann::json::object();
    for (const auto& section : reflection.AllSettings()) {
        nlohmann::json values = nlohmann::json::object();
        for (const auto& field : section.fields) {
            values[field.name] = ValueToJson(field, field.get());
        }
        settings[section.name] = values;
    }

    // The World Bible, compact: every entry's key, name and summary. The AI
    // service adds the full text of the entries most related to the request
    // (its search index), so the model is grounded without the whole canon in
    // every prompt.
    nlohmann::json worldList = nlohmann::json::array();
    WorldBible world(project);
    world.Refresh();
    for (const auto& entry : world.Entries()) {
        worldList.push_back({{"key", entry.Key()}, {"name", entry.name}, {"summary", entry.summary}});
    }

    // Dialogue and quests, in full while they fit: changing an existing
    // conversation needs its nodes.
    constexpr size_t kMaxStoryChars = 16000;
    size_t storyChars = 0;
    StoryLibrary story(project.root);
    story.Refresh();
    nlohmann::json quests = nlohmann::json::array();
    for (const auto& quest : story.Quests()) {
        nlohmann::json json = QuestToJson(quest);
        json["id"] = quest.id;
        storyChars += json.dump().size();
        quests.push_back(json);
    }
    nlohmann::json dialogues = nlohmann::json::array();
    for (const auto& dialogue : story.Dialogues()) {
        nlohmann::json json = DialogueToJson(dialogue);
        json["id"] = std::filesystem::path(dialogue.path).stem().string();
        size_t size = json.dump().size();
        if (storyChars + size > kMaxStoryChars) {
            json = {{"id", json["id"]}, {"speaker", dialogue.speaker}, {"truncated", true}};
        }
        storyChars += size;
        dialogues.push_back(json);
    }

    // Routines: the scene's named places and the schedules that use them.
    nlohmann::json markers = nlohmann::json::array();
    for (const auto& [name, pos] : MarkerPositions(registry)) {
        markers.push_back({{"name", name}, {"x", pos.first}, {"y", pos.second}});
    }
    ScheduleLibrary scheduleLibrary(project.root);
    scheduleLibrary.Refresh();
    nlohmann::json schedules = nlohmann::json::array();
    for (const auto& schedule : scheduleLibrary.Schedules()) {
        nlohmann::json json = ScheduleToJson(schedule);
        json["id"] = schedule.id;
        schedules.push_back(json);
    }

    // The project's scenes and their markers (5.3), for gateways.
    nlohmann::json scenes = nlohmann::json::array();
    for (const std::string& scene : ListScenes(project)) {
        nlohmann::json entry = {{"path", scene}, {"start", scene == StartScene()}, {"markers", SceneMarkers(project, scene)}};
        // The World Bible place each scene shows (6.9): canon for building it.
        if (const WorldEntry* place = world.LocationOf(scene)) entry["location"] = place->Key();
        scenes.push_back(entry);
    }

    nlohmann::json context = {{"entities", entities}, {"tiles", tiles},       {"prefabs", prefabList},
                              {"assets", assetList},  {"scripts", scripts},   {"scriptSources", scriptSources},
                              {"settings", settings}, {"world", worldList},   {"dialogues", dialogues},
                              {"quests", quests},     {"markers", markers},   {"schedules", schedules}};
    if (options.selection && registry.valid(*options.selection)) {
        context["selection"] = static_cast<uint32_t>(*options.selection);
    }
    if (!options.focus.empty()) {
        context["focus"] = options.focus;
    }
    context["scenes"] = scenes;
    if (!options.scene.empty()) {
        context["scene"] = options.scene;
        // The open scene's place, in full: what it is, for anything built or changed in it.
        if (const WorldEntry* place = world.LocationOf(options.scene)) {
            context["sceneLocation"] = {{"key", place->Key()}, {"name", place->name}, {"summary", place->summary},
                                        {"body", place->body}};
        }
    }
    return context;
}

} // namespace aige
