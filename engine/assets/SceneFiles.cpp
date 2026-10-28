#include "assets/SceneFiles.h"

#include "assets/ProjectMove.h"
#include "assets/SceneIO.h"
#include "assets/TileLayerIO.h"
#include "gameplay/TileLayer.h"
#include "gameplay/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/GameSession.h"
#include "gameplay/Settings.h"
#include "physics2d/Components.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

namespace aige {

namespace fs = std::filesystem;

namespace {

std::string ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

bool Exists(const Project& project, const std::string& scene) {
    std::error_code ec;
    return fs::is_regular_file(project.Resolve(scene), ec);
}

} // namespace

std::vector<std::string> ListScenes(const Project& project) {
    std::vector<std::string> scenes;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(project.root / "scenes", ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".aige") {
            scenes.push_back("scenes/" + entry.path().filename().string());
        }
    }
    std::sort(scenes.begin(), scenes.end());
    return scenes;
}

std::string StartScene() {
    const std::string& start = ActiveSettings().game.startScene;
    return start.empty() ? "scenes/main.aige" : start;
}

fs::path StartScenePath(const Project& project) { return project.Resolve(StartScene()); }

std::string ScenePathFor(const std::string& name) {
    std::string slug;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) {
            slug += static_cast<char>(std::tolower(u));
        } else if ((c == ' ' || c == '-' || c == '_') && !slug.empty() && slug.back() != '-') {
            slug += '-';
        }
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    return slug.empty() ? "" : "scenes/" + slug.substr(0, 48) + ".aige";
}

SceneResult CreateScene(const Project& project, const std::string& name, const std::string& type) {
    const std::string path = ScenePathFor(name);
    if (path.empty()) return {"", "a scene needs a name with letters or digits"};
    if (Exists(project, path)) return {"", path + " already exists"};
    std::error_code ec;
    fs::create_directories(project.root / "scenes", ec);
    std::ofstream(project.Resolve(path), std::ios::binary) << "# AIGE Scene v1\n" << (type.empty() ? "" : "# type " + type + "\n");
    return {path, ""};
}

std::string SceneType(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::string line;
    for (int i = 0; i < 8 && std::getline(in, line); ++i) { // the header's lines, before the first entity
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("# type ", 0) == 0) return line.substr(7);
        if (line == "entity") break;
    }
    return {};
}

bool SetSceneType(const fs::path& file, const std::string& type) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const std::string line = type.empty() ? std::string() : "# type " + type + "\n";
    if (const size_t at = text.find("# type "); at != std::string::npos && at < 64) {
        size_t stop = text.find('\n', at);
        stop = stop == std::string::npos ? text.size() : stop + 1;
        text.replace(at, stop - at, line);
    } else {
        const size_t first = text.find('\n');
        text.insert(first == std::string::npos ? text.size() : first + 1, line);
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

SceneResult DuplicateScene(const Project& project, const std::string& scene, const std::string& name) {
    if (!Exists(project, scene)) return {"", scene + " doesn't exist"};
    const std::string path = ScenePathFor(name);
    if (path.empty()) return {"", "a scene needs a name with letters or digits"};
    if (Exists(project, path)) return {"", path + " already exists"};
    std::error_code ec;
    fs::copy_file(project.Resolve(scene), project.Resolve(path), ec);
    if (ec) return {"", "couldn't copy " + scene + ": " + ec.message()};
    // Its tile layers (7.2) go with it.
    if (fs::is_directory(TileLayersDir(project.Resolve(scene)), ec)) {
        fs::copy(TileLayersDir(project.Resolve(scene)), TileLayersDir(project.Resolve(path)), fs::copy_options::recursive, ec);
    }
    return {path, ""};
}

std::vector<std::string> SceneReferences(const Project& project, const std::string& scene) {
    std::vector<std::string> files;
    std::vector<std::string> candidates = ListScenes(project);
    candidates.push_back("settings.aige");
    for (const std::string& file : candidates) {
        if (file == scene) continue;
        int count = 0;
        RewriteSceneText(ReadAll(project.Resolve(file)), scene, scene, &count); // counts, changes nothing
        if (count > 0) files.push_back(file);
    }
    return files;
}

std::string DeleteScene(const Project& project, const std::string& scene) {
    if (!Project::IsInside(scene) || scene.rfind("scenes/", 0) != 0) return scene + " isn't a scene of the project";
    if (!Exists(project, scene)) return scene + " doesn't exist";
    if (scene == StartScene()) return "it's the start scene; make another scene the start first";
    auto references = SceneReferences(project, scene);
    references.erase(std::remove(references.begin(), references.end(), std::string("settings.aige")),
                     references.end());
    if (!references.empty()) {
        std::string list;
        for (const auto& r : references) list += (list.empty() ? "" : ", ") + r;
        return "gateways lead to it from " + list + "; change or remove them first";
    }
    std::error_code ec;
    fs::remove_all(TileLayersDir(project.Resolve(scene)), ec);
    fs::remove(project.Resolve(scene), ec);
    return ec ? "couldn't delete " + scene + ": " + ec.message() : "";
}

std::vector<std::string> SceneMarkers(const Project& project, const std::string& scene) {
    Registry registry;
    std::vector<std::string> names;
    if (!LoadScene(registry, project.Resolve(scene).string())) return names;
    for (auto [entity, marker] : registry.view<Marker>().each()) names.push_back(marker.name);
    return names;
}

std::vector<std::string> GatewayProblems(const Project& project) {
    std::vector<std::string> problems;
    std::map<std::string, std::vector<std::string>> markers; // by scene, loaded once
    for (const std::string& scene : ListScenes(project)) {
        Registry registry;
        if (!LoadScene(registry, project.Resolve(scene).string())) continue;
        // Gateways, and the doorways of buildings whose interior is a scene of its own once linked (an unbuilt one, no
        // scene yet, is no problem: it says so when stepped into).
        std::vector<std::pair<std::string, Gateway>> ways;
        for (auto [entity, gateway] : registry.view<Gateway>().each()) {
            const auto* name = registry.try_get<Name>(entity);
            ways.push_back({"Gateway" + (name ? " \"" + name->value + "\"" : std::string()), gateway});
        }
        for (auto [entity, interior] : registry.view<Interior>().each()) {
            if (interior.kind != "scene" || interior.scene.empty()) continue;
            const auto* name = registry.try_get<Name>(entity);
            ways.push_back({"The doorway of" + (name ? " \"" + name->value + "\"" : std::string(" a building")), Gateway{interior.scene, interior.marker}});
        }
        for (const auto& [label, gateway] : ways) {
            if (gateway.scene.empty()) {
                problems.push_back(scene + ": " + label + " has no scene to lead to");
                continue;
            }
            if (!Exists(project, gateway.scene)) {
                problems.push_back(scene + ": " + label + " leads to " + gateway.scene + ", which doesn't exist");
                continue;
            }
            if (!markers.contains(gateway.scene)) markers[gateway.scene] = SceneMarkers(project, gateway.scene);
            const auto& names = markers[gateway.scene];
            if (std::find(names.begin(), names.end(), gateway.marker) == names.end()) {
                problems.push_back(scene + ": " + label + " arrives at marker \"" + gateway.marker + "\", which " +
                                   gateway.scene + " doesn't have");
            }
        }
    }
    return problems;
}

std::string TravelToScene(Registry& registry, const std::string& sceneText, const std::string& marker,
                          const std::filesystem::path& scenePath) {
    const auto traveller = FindPlayer(registry);
    const std::string carried = traveller ? SerializeEntities(registry, {*traveller}) : std::string();
    DeserializeScene(registry, sceneText);
    if (scenePath.empty()) ResetLayers(registry);
    else LoadTileLayers(registry, scenePath);

    // Where to stand.
    std::optional<std::pair<float, float>> at;
    std::optional<std::pair<float, float>> firstMarker;
    for (auto [entity, m, world] : registry.view<Marker, WorldTransform2D>().each()) {
        if (!firstMarker) firstMarker = std::pair{world.x, world.y};
        if (m.name == marker) at = std::pair{world.x, world.y};
    }
    const auto local = FindPlayer(registry);
    std::string warning;
    if (!at) {
        warning = marker.empty() ? "the gateway names no marker" : "no marker \"" + marker + "\" in that scene";
        if (local) {
            const auto& world = registry.get<WorldTransform2D>(*local);
            at = std::pair{world.x, world.y};
        } else {
            at = firstMarker.value_or(std::pair{0.5f, 0.5f});
        }
    }
    if (carried.empty()) {
        if (local) registry.get<WorldTransform2D>(*local) = {at->first, at->second};
        return warning;
    }

    // The traveller replaces the scene's own player; whatever pointed at that
    // one now points at the arrival.
    const Entity player = ReplacePlayer(registry, carried);
    if (player == entt::null) return "the player couldn't be carried over";
    if (auto* world = registry.try_get<WorldTransform2D>(player)) *world = {at->first, at->second};
    if (auto* velocity = registry.try_get<Velocity2D>(player)) *velocity = {};
    return warning;
}

Entity ReplacePlayer(Registry& registry, const std::string& playerText) {
    const auto local = FindPlayer(registry);
    const std::vector<Entity> arrived = AppendEntities(registry, playerText);
    if (arrived.empty()) return entt::null;
    const Entity player = arrived.front();
    if (local && *local != player) {
        for (const ComponentInfo& component : EngineReflection().Components()) {
            for (const FieldInfo& field : component.fields) {
                if (field.type != FieldType::EntityRef) continue;
                for (auto entity : registry.view<entt::entity>()) {
                    if (entity == *local || !component.has(registry, entity)) continue;
                    if (ToEntity(field.get(registry, entity)) == *local) field.set(registry, entity, player);
                }
            }
        }
        registry.destroy(*local);
    }
    return player;
}

} // namespace aige
