#include "ai_bridge/ContextBuilder.h"

#include "assets/AssetDatabase.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "reflection/Reflection.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <algorithm>
#include <climits>
#include <filesystem>
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
            assetList.push_back({{"path", asset.path}, {"type", AssetTypeName(asset.type)}});
        }
    }

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
    }

    nlohmann::json settings = nlohmann::json::object();
    for (const auto& section : reflection.AllSettings()) {
        nlohmann::json values = nlohmann::json::object();
        for (const auto& field : section.fields) {
            values[field.name] = ValueToJson(field, field.get());
        }
        settings[section.name] = values;
    }

    nlohmann::json context = {{"entities", entities}, {"tiles", tiles},       {"prefabs", prefabList},
                              {"assets", assetList},  {"scripts", scripts},   {"settings", settings}};
    if (options.selection && registry.valid(*options.selection)) {
        context["selection"] = static_cast<uint32_t>(*options.selection);
    }
    if (!options.focus.empty()) {
        context["focus"] = options.focus;
    }
    return context;
}

} // namespace aige
