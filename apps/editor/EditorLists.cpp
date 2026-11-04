// What the editor's pickers choose from (searchable selects in the Records panel and the Inspector): the project's
// scripts, dialogues and packages, and the open scene's markers, paths and named things. The script editor's reading
// and saving, and the markers' toolbar (place, show, go to).

#include "EditorApp.h"

#include "gameplay/Behaviour.h"
#include "gameplay/Components.h"
#include "gameplay/Paths.h"
#include "assets/Prefab.h"
#include "physics2d/Components.h"
#include "records/Records.h"
#include "renderer2d/Components.h"
#include "scripting/Components.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace aige {

nlohmann::json EditorApp::LookupsJson() {
    std::set<std::string> markers, paths, names, characters;
    for (auto [e, marker] : registry_.view<Marker>().each()) markers.insert(marker.name);
    for (auto [e, path] : registry_.view<Path>().each()) paths.insert(path.name);
    for (auto [e, name] : registry_.view<Name>(entt::exclude<Tile, Marker, Path, Transient>).each()) {
        if (name.value.empty()) continue;
        names.insert(name.value);
        if (registry_.any_of<Behaviour, Talk>(e)) characters.insert(name.value);
    }
    nlohmann::json dialogues = nlohmann::json::array();
    std::error_code ec;
    const auto folder = assets_.GetProject().Resolve("dialogue");
    if (std::filesystem::is_directory(folder, ec)) {
        for (const auto& f : std::filesystem::directory_iterator(folder, ec)) {
            if (f.path().extension() == ".json") dialogues.push_back("dialogue/" + f.path().filename().string());
        }
    }
    nlohmann::json packages = nlohmann::json::array();
    for (const Record* r : ActiveRecords().OfType("package")) {
        packages.push_back({{"id", r->id}, {"template", r->resolved.value("template", std::string())},
                            {"description", r->resolved.value("description", std::string())}});
    }
    // Who uses each script: things in this scene (LuaScript), and packages (template script).
    nlohmann::json scriptUsers = nlohmann::json::object();
    for (auto [e, script] : registry_.view<LuaScript>().each()) {
        if (script.path.empty()) continue;
        const auto* name = registry_.try_get<Name>(e);
        scriptUsers[script.path].push_back(name && !name->value.empty() ? name->value : "#" + std::to_string(static_cast<uint32_t>(e)));
    }
    for (const Record* r : ActiveRecords().OfType("package")) {
        const auto& data = r->resolved.value("data", nlohmann::json::object());
        if (data.is_object() && data.value("script", nlohmann::json()).is_string() && !data["script"].get<std::string>().empty()) {
            scriptUsers[data["script"].get<std::string>()].push_back("package " + r->id);
        }
    }
    return {{"type", "lookups"},
            {"scripts", ListScripts()},
            {"scriptUsers", scriptUsers},
            {"markers", std::vector<std::string>(markers.begin(), markers.end())},
            {"paths", std::vector<std::string>(paths.begin(), paths.end())},
            {"entities", std::vector<std::string>(names.begin(), names.end())},
            {"characters", std::vector<std::string>(characters.begin(), characters.end())},
            {"dialogues", dialogues},
            {"packages", packages}};
}

bool EditorApp::HandleListsMessage(const std::string& type, const nlohmann::json& message) {
    if (type == "lookups:request") {
        post_(LookupsJson());
        return true;
    }
    // The script editor: a project script read and saved (scripts/*.lua only).
    auto scriptPath = [&](std::filesystem::path* out) {
        const std::string path = message.value("path", std::string());
        if (path.rfind("scripts/", 0) != 0 || path.find("..") != std::string::npos || std::filesystem::path(path).extension() != ".lua") {
            return false;
        }
        *out = assets_.GetProject().Resolve(path);
        return true;
    };
    if (type == "script:read") {
        std::filesystem::path file;
        if (!scriptPath(&file)) {
            post_({{"type", "script:text"}, {"path", message.value("path", "")}, {"error", "Only .lua files under scripts/ open here"}});
            return true;
        }
        std::ifstream in(file, std::ios::binary);
        std::stringstream text;
        if (in) text << in.rdbuf();
        post_({{"type", "script:text"}, {"path", message.value("path", "")}, {"text", text.str()}, {"exists", static_cast<bool>(in)}});
        return true;
    }
    if (type == "script:save") {
        std::filesystem::path file;
        if (!scriptPath(&file)) {
            post_({{"type", "notify"}, {"text", "Scripts are saved under scripts/ as .lua files"}});
            return true;
        }
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        std::ofstream out(file, std::ios::binary);
        out << message.value("text", std::string());
        const bool ok = static_cast<bool>(out);
        out.close();
        post_({{"type", "script:saved"}, {"path", message.value("path", "")}, {"ok", ok}});
        if (!ok) post_({{"type", "notify"}, {"text", "Couldn't write " + message.value("path", std::string())}});
        post_(LookupsJson()); // a new script shows in the pickers
        return true;
    }
    // Dialogues nobody can start: each dialogue file given to its character (by name: dialogue/vara-bell.json and its
    // speaker character/vara_bell go to "Vara Bell"), a Talk on its prefab (every copy) or on the thing itself.
    if (type == "dialogue:connect") {
        auto slug = [](std::string text) {
            if (const size_t slash = text.rfind('/'); slash != std::string::npos) text = text.substr(slash + 1);
            if (text.size() > 5 && text.substr(text.size() - 5) == ".json") text.resize(text.size() - 5);
            std::string out;
            for (char c : text) {
                const unsigned char u = static_cast<unsigned char>(c);
                if (std::isalnum(u)) out += static_cast<char>(std::tolower(u));
                else if (!out.empty() && out.back() != '-') out += '-';
            }
            while (!out.empty() && out.back() == '-') out.pop_back();
            return out;
        };
        // The dialogues already started by a Talk somewhere (the scene, a prefab).
        std::set<std::string> talked;
        for (auto [e, talk] : registry_.view<Talk>().each()) talked.insert(talk.dialogue);
        for (const auto& info : prefabs_.Prefabs()) {
            prefabs_.VisitTemplate(info.path, [&](Registry& r, Entity t) {
                if (const auto* talk = r.try_get<Talk>(t)) talked.insert(talk->dialogue);
            });
        }
        int connected = 0;
        std::vector<std::string> unmatched;
        std::error_code ec;
        const auto folder = assets_.GetProject().Resolve("dialogue");
        for (const auto& f : std::filesystem::directory_iterator(folder, ec)) {
            if (f.path().extension() != ".json") continue;
            const std::string dialogue = "dialogue/" + f.path().filename().string();
            if (talked.contains(dialogue)) continue;
            std::string speaker;
            {
                std::ifstream in(f.path(), std::ios::binary);
                const nlohmann::json d = nlohmann::json::parse(in, nullptr, false);
                if (d.is_object()) speaker = d.value("speaker", std::string());
            }
            const std::string bySpeaker = slug(speaker), byFile = slug(dialogue);
            // Its character: a thing in the scene named so (not already talking).
            std::optional<Entity> who;
            for (auto [e, name] : registry_.view<Name>(entt::exclude<Tile, Marker, Path, Transient>).each()) {
                const std::string n = slug(name.value);
                if (!n.empty() && (n == byFile || n == bySpeaker) && !registry_.all_of<Talk>(e)) {
                    who = e;
                    break;
                }
            }
            if (!who) {
                unmatched.push_back(dialogue);
                continue;
            }
            const Talk talk{dialogue, speaker, 1.5f};
            const auto* link = registry_.try_get<PrefabInstance>(*who);
            if (link && prefabs_.Find(link->path)) {
                const auto failed = prefabs_.EditTemplate(link->path, [&](Registry& r, Entity t) { r.emplace_or_replace<Talk>(t, talk); });
                if (failed) {
                    unmatched.push_back(dialogue + " (" + *failed + ")");
                    continue;
                }
                prefabs_.SyncAll(registry_, link->path);
            } else {
                ApplyOneShotEdit([&] {
                    registry_.emplace_or_replace<Talk>(*who, talk);
                    return true;
                });
            }
            ++connected;
        }
        RefreshPrefabs();
        dirty_ = dirty_ || connected > 0;
        inspectorStale_ = sceneTreeStale_ = true;
        std::string text = connected ? "Connected " + std::to_string(connected) + " dialogue(s) to their characters: walk up and press E"
                                     : "Every dialogue with a character in this scene can already be started";
        if (!unmatched.empty()) {
            text += ". No character here for: ";
            for (size_t i = 0; i < unmatched.size() && i < 6; ++i) text += (i ? ", " : "") + unmatched[i];
            if (unmatched.size() > 6) text += " and " + std::to_string(unmatched.size() - 6) + " more";
        }
        post_({{"type", "notify"}, {"text", text}});
        return true;
    }
    // Markers: shown or hidden, and one found (selected, the view centred on it).
    if (type == "marker:show") {
        showMarkers_ = message.value("shown", !showMarkers_);
        editorStateStale_ = true;
        return true;
    }
    if (type == "marker:goto") {
        const std::string name = message.value("name", std::string());
        for (auto [e, marker, w] : registry_.view<Marker, WorldTransform2D>().each()) {
            if (marker.name != name) continue;
            Select(e);
            const ScreenPose at = ProjectIso(w.x, w.y, iso_);
            int width = 0, height = 0;
            viewport_.GetFramebufferSize(width, height);
            camera_.offsetX = at.x - static_cast<float>(width) * 0.5f / camera_.zoom;
            camera_.offsetY = at.y - static_cast<float>(height) * 0.5f / camera_.zoom;
            return true;
        }
        post_({{"type", "notify"}, {"text", "No marker called " + name + " in this scene"}});
        return true;
    }
    return false;
}

std::optional<Entity> EditorApp::MarkerAt(float sceneX, float sceneY) {
    if (!showMarkers_) return std::nullopt;
    float wx = 0.0f, wy = 0.0f;
    UnprojectIso(sceneX, sceneY, iso_, wx, wy);
    const int gx = static_cast<int>(std::floor(wx)), gy = static_cast<int>(std::floor(wy));
    for (auto [e, marker, w] : registry_.view<Marker, WorldTransform2D>().each()) {
        if (static_cast<int>(std::floor(w.x)) == gx && static_cast<int>(std::floor(w.y)) == gy) return e;
    }
    return std::nullopt;
}

} // namespace aige
