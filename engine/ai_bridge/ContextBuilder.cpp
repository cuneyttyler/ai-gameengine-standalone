#include "ai_bridge/ContextBuilder.h"

#include "assets/SceneFiles.h"

#include "assets/AssetDatabase.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "records/Records.h"
#include "reflection/Reflection.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"
#include "gameplay/Cutscene.h"
#include "gameplay/Animation.h"
#include "gameplay/Difficulty.h"
#include "gameplay/Effects.h"
#include "gameplay/Magic.h"
#include "gameplay/Physics.h"
#include "story/StoryGraph.h"
#include "game/Accessibility.h"
#include "gameplay/InputMap.h"
#include "story/StoryRules.h"
#include "ui/Documents.h"
#include "gameplay/Settings.h"
#include "gameplay/Schedule.h"
#include "gameplay/Story.h"
#include "gameplay/TileLayer.h"
#include "world/WorldBible.h"

#include <algorithm>
#include <climits>
#include <filesystem>
#include <fstream>
#include <map>

namespace aige {

namespace fs = std::filesystem;

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

    // Records (12.1), in full while they fit; beyond that, by id only.
    constexpr size_t kMaxRecordChars = 16000;
    size_t recordChars = 0;
    nlohmann::json records = nlohmann::json::array();
    RecordDatabase recordDb;
    recordDb.Open(reflection, project.root);
    for (const auto& type : reflection.RecordTypes()) {
        for (const Record* record : recordDb.OfType(type.name)) {
            nlohmann::json json = {{"type", record->type}, {"id", record->id}};
            const size_t size = record->data.dump().size();
            if (recordChars + size <= kMaxRecordChars) {
                json["record"] = record->data;
                recordChars += size;
            } else if (record->resolved.contains("name")) {
                json["name"] = record->resolved["name"];
            }
            records.push_back(json);
        }
    }

    // Cutscenes (15.5): what play_cutscene can name; each with its name and length.
    nlohmann::json cutscenes = nlohmann::json::array();
    for (const std::string& id : ListCutscenes(project.root)) {
        std::string error;
        if (auto cutscene = LoadCutscene(project.root, id, &error)) {
            cutscenes.push_back({{"id", id}, {"name", cutscene->name}, {"seconds", cutscene->Length()}});
        }
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
                              {"quests", quests},     {"markers", markers},   {"schedules", schedules},
                              {"records", records},   {"cutscenes", cutscenes}};
    // Animation and effects (18): the project's sheets and their clips, the particles and
    // effects there are (presets and records), the built-in animators.
    {
        AnimLibrary anims(project.root);
        anims.Refresh();
        nlohmann::json sheets = nlohmann::json::array();
        for (const auto& sheet : anims.Sheets()) {
            nlohmann::json clips = nlohmann::json::array();
            for (const auto& [name, clip] : sheet.clips) clips.push_back(name);
            sheets.push_back({{"path", sheet.path}, {"texture", sheet.texture}, {"clips", clips}});
        }
        auto ids = [&](std::vector<std::string> presets, const char* type) {
            for (const Record* r : recordDb.OfType(type)) presets.push_back(r->id);
            std::sort(presets.begin(), presets.end());
            presets.erase(std::unique(presets.begin(), presets.end()), presets.end());
            return presets;
        };
        context["fx"] = {{"sheets", sheets},
                         {"particles", ids(ParticlePresets(), "particles")},
                         {"effects", ids(EffectPresets(), "effect")},
                         {"animators", BuiltInAnimatorIds()},
                         {"lighting", ActiveSettings().lighting.enabled}};
    }
    // Abilities and magic (19): the abilities and magic effects there are (presets and records).
    {
        auto ids = [&](std::vector<std::string> presets, const char* type) {
            for (const Record* r : recordDb.OfType(type)) presets.push_back(r->id);
            std::sort(presets.begin(), presets.end());
            presets.erase(std::unique(presets.begin(), presets.end()), presets.end());
            return presets;
        };
        context["magic"] = {{"abilities", ids(AbilityPresets(), "ability")}, {"effects", ids(MagicEffectPresets(), "magic-effect")}};
    }
    // Shipping (24): the achievements, the version, what the accessibility report finds.
    {
        nlohmann::json achievements = nlohmann::json::array();
        for (const Record* r : recordDb.OfType("achievement")) achievements.push_back(r->id);
        const nlohmann::json report = AccessibilityReport(ProjectThemeTokens(project.root), ActiveInputMap());
        nlohmann::json failing = nlohmann::json::array();
        for (const auto& c : report["checks"]) {
            if (!c.value("ok", true)) failing.push_back(c.value("id", "") + ": " + c.value("message", ""));
        }
        context["shipping"] = {{"achievements", achievements},
                               {"version", ActiveSettings().game.version},
                               {"accessibility", {{"passed", report["passed"]}, {"total", report["total"]}, {"failing", failing}}}};
    }
    // The AI Planner's design (22), if the project has one: what game it is and where the plan is.
    {
        std::ifstream briefIn(project.root / "planner" / "brief.json", std::ios::binary);
        std::ifstream stateIn(project.root / "planner" / "state.json", std::ios::binary);
        const nlohmann::json brief = briefIn ? nlohmann::json::parse(briefIn, nullptr, false) : nlohmann::json();
        const nlohmann::json state = stateIn ? nlohmann::json::parse(stateIn, nullptr, false) : nlohmann::json();
        if (brief.is_object() && state.is_object()) {
            context["plan"] = {{"title", brief.value("title", "")}, {"genre", brief.value("genre", "")}, {"logline", brief.value("logline", "")},
                               {"stage", state.value("stage", "")}, {"status", state.value("status", "")}};
            std::ifstream buildIn(project.root / "planner" / "buildstate.json", std::ios::binary);
            const nlohmann::json build = buildIn ? nlohmann::json::parse(buildIn, nullptr, false) : nlohmann::json();
            if (build.is_object()) context["plan"]["build"] = {{"stage", build.value("stage", "")}, {"status", build.value("status", "")}};
        }
    }
    // The story knowledge graph (21): the ontology, the graph's nodes and scenes, its issues.
    {
        const StoryOntology ontology = StoryOntology::ForProject(project.root);
        nlohmann::json graphJson = {{"ontology", ontology.Summary()}};
        if (auto graph = StoryGraph::Load(StoryGraph::PathIn(project.root))) {
            nlohmann::json nodes = nlohmann::json::object(), scenes = nlohmann::json::array(), counts = nlohmann::json::object();
            for (const auto& n : graph->nodes) {
                nodes[n.id] = n.cls;
                counts[n.cls] = counts.value(n.cls, 0) + 1;
            }
            for (const auto& id : graph->SceneOrder()) {
                const StoryNode* n = graph->Find(id);
                nlohmann::json s = {{"id", id}, {"order", n->fields.value("order", 0.0)}, {"mode", n->Text("mode")}};
                for (const char* k : {"location", "branch", "goal"}) {
                    if (!n->Text(k).empty()) s[k] = n->Text(k);
                }
                scenes.push_back(std::move(s));
            }
            auto issues = CheckStory(*graph, ontology);
            if (issues.size() > 30) issues.resize(30);
            graphJson["revision"] = graph->revision;
            graphJson["nodes"] = nodes;
            graphJson["scenes"] = scenes;
            graphJson["counts"] = counts;
            graphJson["issues"] = IssuesJson(issues);
        }
        context["storyGraph"] = graphJson;
    }
    // Physics v2 (20): the layers in use, gravity, how many rigid bodies, the triggers' names.
    {
        std::vector<std::string> layers;
        for (auto [e, collider] : registry.view<Collider2D>().each()) {
            if (!collider.layer.empty()) layers.push_back(collider.layer);
        }
        std::sort(layers.begin(), layers.end());
        layers.erase(std::unique(layers.begin(), layers.end()), layers.end());
        nlohmann::json triggers = nlohmann::json::array();
        for (auto [e, trigger] : registry.view<Trigger>().each()) triggers.push_back(trigger.name);
        const auto& p = ActiveSettings().physics;
        context["physics"] = {{"layers", layers},
                              {"gravity", {p.gravityX, p.gravityY}},
                              {"bodies", registry.view<RigidBody2D>().size()},
                              {"triggers", triggers}};
    }
    // The game's UI (17): the built-in themes, the project's theme and its own UI files.
    {
        nlohmann::json themes = nlohmann::json::array();
        std::error_code ec;
        for (const auto& file : fs::directory_iterator(EngineUiDir() / "themes", ec)) {
            if (file.path().extension() == ".json") themes.push_back(file.path().stem().string());
        }
        nlohmann::json files = nlohmann::json::array();
        if (fs::is_directory(project.root / "ui", ec)) {
            for (const auto& file : fs::recursive_directory_iterator(project.root / "ui", ec)) {
                if (file.is_regular_file()) files.push_back("ui/" + fs::relative(file.path(), project.root / "ui").generic_string());
            }
        }
        nlohmann::json theme = nlohmann::json::object();
        if (std::ifstream in(project.root / "ui" / "theme.json", std::ios::binary); in) {
            theme = nlohmann::json::parse(in, nullptr, false);
            if (!theme.is_object()) theme = nlohmann::json::object();
        }
        context["ui"] = {{"themes", themes}, {"theme", theme}, {"files", files}, {"documents", ActiveSettings().ui.documents}};
    }
    // Difficulty (16): the levels (built-in or the project's), their steps, and how
    // enemies' levels are chosen; scalable fields are marked in the schema.
    {
        const DifficultyModel model = DifficultyModel::FromRecords(recordDb);
        nlohmann::json levels = nlohmann::json::array();
        for (const auto& level : model.Levels()) {
            levels.push_back({{"id", level.id}, {"name", level.name}, {"categories", level.steps}, {"xp", level.xp}});
        }
        static const char* kScaling[] = {"None", "FixedZones", "Scaled", "Hybrid"};
        context["difficulty"] = {{"levels", levels},
                                 {"builtIn", model.BuiltInLevels()},
                                 {"defaultLevel", model.DefaultLevel()},
                                 {"scaling", kScaling[static_cast<int>(ActiveSettings().difficulty.scaling)]}};
    }
    // A built place's ground (tile layers, not entities): which tile prefabs cover how many
    // cells, so a request like "use grass in place of cobble" names what's there (terrain.replace).
    if (const TileLayers* layers = FindLayers(registry); layers && !layers->layers.empty()) {
        nlohmann::json ground = nlohmann::json::array();
        for (const auto& [name, layer] : layers->layers) {
            std::map<std::string, int> cells;
            layer.ForEach([&](int, int, uint16_t id) {
                if (const TileDef* def = layer.Def(id)) ++cells[def->prefab.empty() ? def->sprite.texture : def->prefab];
            });
            nlohmann::json tilesUsed = nlohmann::json::array();
            for (const auto& [prefab, n] : cells) tilesUsed.push_back({{"prefab", prefab}, {"cells", n}});
            ground.push_back({{"layer", name}, {"tiles", tilesUsed}});
        }
        context["ground"] = ground;
    }
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
