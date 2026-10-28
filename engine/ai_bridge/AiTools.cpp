#include "ai_bridge/AiTools.h"

#include "assets/AssetDatabase.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneFiles.h"
#include "gameplay/Components.h"
#include "gameplay/InputMap.h"
#include "physics2d/Components.h"
#include "reflection/Reflection.h"
#include "scripting/Components.h"
#include "world/WorldBible.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace aige {

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxText = 20000; // a file read, cut past this
constexpr size_t kMaxList = 60;    // a list's rows

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Every word of `query` in `text` (lowercase): a loose match for names, descriptions and paths.
bool Matches(const std::string& text, const std::string& query) {
    const std::string hay = Lower(text);
    std::stringstream words(Lower(query));
    std::string word;
    bool any = false;
    while (words >> word) {
        any = true;
        if (hay.find(word) == std::string::npos) return false;
    }
    return any || query.empty();
}

nlohmann::json Components(const ReflectionRegistry& reflection, Registry& registry, Entity entity) {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& component : reflection.Components()) {
        if (!component.inspectorVisible || component.name == "ScreenTransform2D" || !component.has(registry, entity)) continue;
        nlohmann::json fields = nlohmann::json::object();
        for (const auto& field : component.fields) fields[field.name] = ValueToJson(field, field.get(registry, entity));
        out[component.name] = fields;
    }
    return out;
}

nlohmann::json Summary(Registry& registry, Entity entity) {
    nlohmann::json row = {{"id", static_cast<uint32_t>(entity)}};
    if (const auto* name = registry.try_get<Name>(entity)) row["name"] = name->value;
    if (const auto* link = registry.try_get<PrefabInstance>(entity)) row["prefab"] = link->path;
    if (const auto* at = registry.try_get<WorldTransform2D>(entity)) row["at"] = {std::round(at->x * 10) / 10, std::round(at->y * 10) / 10};
    return row;
}

int Limit(const nlohmann::json& args, int fallback) {
    return std::clamp(args.value("limit", fallback), 1, static_cast<int>(kMaxList));
}

// The files a tool may read: the project's data, never outside it or the engine's build.
bool Readable(const std::string& path) {
    if (path.empty() || path.find("..") != std::string::npos || path[0] == '/' || path.find(':') != std::string::npos) return false;
    static const char* roots[] = {"records/", "scripts/", "dialogue/", "quests/", "cutscenes/", "schedules/", "ui/", "world/",
                                  "scenes/", "prefabs/", "places/", "planner/", "assets/"};
    if (path == "input.json" || path == "project.json") return true;
    for (const char* root : roots) {
        if (path.rfind(root, 0) == 0) {
            const std::string ext = fs::path(path).extension().string();
            return ext == ".json" || ext == ".lua" || ext == ".md" || ext == ".rml" || ext == ".rcss" || ext == ".prefab" ||
                   ext == ".aige" || ext == ".txt";
        }
    }
    return false;
}

} // namespace

const std::vector<std::string>& AiToolNames() {
    static const std::vector<std::string> names = {"find_entities", "read_entity", "find_assets", "find_prefabs", "read_file",
                                                   "list_scenes", "read_settings", "read_input", "search_world", "what_is_at",
                                                   "check", "look", "playtest"};
    return names;
}

nlohmann::json RunAiTool(const std::string& tool, const nlohmann::json& args, AiToolContext& c) {
    try {
        if (tool == "find_entities") {
            const std::string name = args.value("name", ""), prefab = args.value("prefab", ""), component = args.value("component", "");
            const bool nearby = args.contains("near") && args["near"].is_array() && args["near"].size() == 2;
            const float nx = nearby ? args["near"][0].get<float>() : 0.0f, ny = nearby ? args["near"][1].get<float>() : 0.0f;
            const float radius = args.value("radius", 6.0f);
            nlohmann::json rows = nlohmann::json::array();
            int total = 0;
            for (Entity e : EntitiesInOrder(c.registry)) {
                if (c.registry.all_of<Tile>(e)) continue;
                const auto* n = c.registry.try_get<Name>(e);
                const auto* link = c.registry.try_get<PrefabInstance>(e);
                if (!name.empty() && !(n && Matches(n->value, name))) continue;
                if (!prefab.empty() && !(link && Matches(link->path, prefab))) continue;
                if (!component.empty()) {
                    const ComponentInfo* info = c.reflection.FindComponent(component);
                    if (!info || !info->has(c.registry, e)) continue;
                }
                if (nearby) {
                    const auto* at = c.registry.try_get<WorldTransform2D>(e);
                    if (!at || std::hypot(at->x - nx, at->y - ny) > radius) continue;
                }
                ++total;
                if (static_cast<int>(rows.size()) < Limit(args, 30)) rows.push_back(Summary(c.registry, e));
            }
            return {{"entities", rows}, {"total", total}};
        }
        if (tool == "read_entity") {
            const Entity e = static_cast<Entity>(args.value("id", 0u));
            if (!c.registry.valid(e)) return {{"error", "no entity " + std::to_string(args.value("id", 0u))}};
            nlohmann::json row = Summary(c.registry, e);
            row["components"] = Components(c.reflection, c.registry, e);
            if (const auto* script = c.registry.try_get<LuaScript>(e)) {
                nlohmann::json props = nlohmann::json::object();
                for (const auto& [k, v] : script->properties) std::visit([&](const auto& x) { props[k] = x; }, v);
                row["scriptProperties"] = props;
            }
            return row;
        }
        if (tool == "find_assets") {
            const std::string query = args.value("query", ""), kind = args.value("kind", "");
            nlohmann::json rows = nlohmann::json::array();
            for (const auto& a : c.assets.Assets()) {
                if (!a.error.empty()) continue;
                std::string text = a.path;
                for (const auto& [k, v] : a.analysis) text += " " + v;
                if (!kind.empty() && !Matches(std::string(AssetTypeName(a.type)) + " " + (a.analysis.contains("kind") ? a.analysis.at("kind") : ""), kind)) continue;
                if (!Matches(text, query)) continue;
                nlohmann::json row = {{"path", a.path}, {"type", AssetTypeName(a.type)}, {"size", {a.width, a.height}}};
                if (a.analysis.contains("description")) row["description"] = a.analysis.at("description");
                if (a.analysis.contains("kind")) row["kind"] = a.analysis.at("kind");
                rows.push_back(row);
                if (static_cast<int>(rows.size()) >= Limit(args, 25)) break;
            }
            return {{"assets", rows}};
        }
        if (tool == "find_prefabs") {
            const std::string query = args.value("query", "");
            nlohmann::json rows = nlohmann::json::array();
            for (const auto& p : c.prefabs.Prefabs()) {
                if (!p.error.empty() || !Matches(p.path + " " + p.name + " " + p.kind, query)) continue;
                rows.push_back({{"path", p.path}, {"name", p.name}, {"kind", p.kind}, {"tile", p.isTile}});
                if (static_cast<int>(rows.size()) >= Limit(args, 25)) break;
            }
            return {{"prefabs", rows}};
        }
        if (tool == "read_file") {
            const std::string path = args.value("path", "");
            if (!Readable(path)) return {{"error", "can't read " + path + " (records, scripts, dialogue, quests, cutscenes, schedules, ui, world, prefabs, input.json)"}};
            std::ifstream in(c.project.Resolve(path), std::ios::binary);
            if (!in) return {{"error", "no file " + path}};
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const bool cut = text.size() > kMaxText;
            if (cut) text.resize(kMaxText);
            return {{"path", path}, {"text", text}, {"cut", cut}};
        }
        if (tool == "list_scenes") {
            nlohmann::json rows = nlohmann::json::array();
            for (const std::string& scene : ListScenes(c.project)) {
                const std::string type = SceneType(c.project.Resolve(scene));
                rows.push_back({{"path", scene}, {"type", type.empty() ? "(the project's method)" : type}, {"start", scene == StartScene()}});
            }
            return {{"scenes", rows}};
        }
        if (tool == "read_settings") {
            const std::string want = args.value("section", "");
            nlohmann::json out = nlohmann::json::object();
            for (const auto& section : c.reflection.AllSettings()) {
                if (!want.empty() && Lower(section.name) != Lower(want)) continue;
                nlohmann::json values = nlohmann::json::object();
                for (const auto& field : section.fields) values[field.name] = ValueToJson(field, field.get());
                out[section.name] = values;
            }
            return out;
        }
        if (tool == "read_input") return ActiveInputMap().ToJson();
        if (tool == "search_world") {
            if (!c.world) return {{"error", "the World Bible isn't loaded"}};
            const std::string query = args.value("query", "");
            nlohmann::json rows = nlohmann::json::array();
            for (const auto& entry : c.world->Entries()) {
                std::string text = entry.kind + " " + entry.id + " " + entry.name + " " + entry.summary;
                for (const auto& t : entry.tags) text += " " + t;
                bool hit = Matches(text, query);
                if (!hit) { // any word, for a looser second pass
                    std::stringstream words(Lower(query));
                    std::string word;
                    while (!hit && words >> word) hit = word.size() > 3 && Lower(text + " " + entry.body).find(word) != std::string::npos;
                }
                if (!hit) continue;
                rows.push_back({{"key", entry.kind + "/" + entry.id}, {"name", entry.name}, {"summary", entry.summary},
                                {"body", entry.body.substr(0, 1200)}});
                if (rows.size() >= 12) break;
            }
            return {{"entries", rows}};
        }
        if (tool == "what_is_at") {
            const float x = args.value("x", 0.0f), y = args.value("y", 0.0f), radius = args.value("radius", 0.75f);
            nlohmann::json rows = nlohmann::json::array();
            for (auto [e, at] : c.registry.view<WorldTransform2D>().each()) {
                if (std::hypot(at.x - x, at.y - y) > radius) continue;
                nlohmann::json row = Summary(c.registry, e);
                if (const auto* tile = c.registry.try_get<Tile>(e)) row["tile"] = tile->solid ? "wall" : "ground";
                if (c.registry.all_of<Obstacle>(e)) row["solid"] = true;
                rows.push_back(row);
                if (rows.size() >= kMaxList) break;
            }
            return {{"at", {x, y}}, {"things", rows}};
        }
        if (tool == "check") return c.check ? c.check() : nlohmann::json{{"error", "checks aren't available here"}};
        if (tool == "look") return c.look ? c.look(args) : nlohmann::json{{"error", "pictures of the view aren't available here"}};
        if (tool == "playtest") return c.playtest ? c.playtest(args) : nlohmann::json{{"error", "playtests aren't available here"}};
        return {{"error", "no tool " + tool + " (tools: find_entities, read_entity, find_assets, find_prefabs, read_file, list_scenes, "
                          "read_settings, read_input, search_world, what_is_at, check, look, playtest)"}};
    } catch (const std::exception& error) {
        return {{"error", std::string("bad arguments: ") + error.what()}};
    }
}

std::string AiToolResultsMessage(const nlohmann::json& calls, const std::vector<nlohmann::json>& results) {
    std::string text = "TOOL RESULTS (what you asked to read or look at). Answer the request now, or ask for more tools:\n";
    for (size_t i = 0; i < results.size() && i < calls.size(); ++i) {
        std::string body = results[i].dump();
        if (body.size() > 12000) body = body.substr(0, 12000) + "…(cut)";
        text += "- " + calls[i].value("tool", "") + " " + calls[i].value("args", nlohmann::json::object()).dump() + " → " + body + "\n";
    }
    return text;
}

} // namespace aige
