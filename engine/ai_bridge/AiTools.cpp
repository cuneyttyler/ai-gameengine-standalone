#include "ai_bridge/AiTools.h"

#include "assets/AssetDatabase.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneFiles.h"
#include "gameplay/Behaviour.h"
#include "gameplay/Components.h"
#include "gameplay/InputMap.h"
#include "gameplay/Interiors.h"
#include "gameplay/Story.h"
#include "physics2d/Components.h"
#include "reflection/Reflection.h"
#include "scripting/Components.h"
#include "world/WorldBible.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <filesystem>
#include <fstream>
#include <set>
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
    // Who it is in the story: its Actor key (quests, dialogue and scripts name it by this).
    if (const auto* actor = registry.try_get<Actor>(entity); actor && !actor->key.empty()) row["actor"] = actor->key;
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
                                                   "buildings", "check", "look", "playtest", "requirements", "placements"};
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
            nlohmann::json out = {{"entities", rows}, {"total", total}};
            if (total == 0) {
                // Nothing by that: it isn't in this scene (another spelling won't find it either).
                out["note"] = "nothing in the open scene matches; it isn't here. Don't search other spellings: make it, or use what is here";
            }
            // The same story character more than once in the scene (an Actor key): the game keeps one, the others are
            // leftovers. Keep the one with a script (else the most components), delete the rest.
            std::map<std::string, std::vector<Entity>> byKey;
            for (auto [e, actor] : c.registry.view<Actor>().each()) {
                if (!actor.key.empty()) byKey[actor.key].push_back(e);
            }
            nlohmann::json copies = nlohmann::json::object();
            for (auto& [key, list] : byKey) {
                if (list.size() < 2) continue;
                Entity keep = list.front();
                auto weight = [&](Entity e) { return (c.registry.all_of<LuaScript>(e) ? 1000 : 0) + static_cast<int>(Components(c.reflection, c.registry, e).size()); };
                for (Entity e : list) {
                    if (weight(e) > weight(keep)) keep = e;
                }
                nlohmann::json others = nlohmann::json::array();
                for (Entity e : list) {
                    if (e != keep) others.push_back(static_cast<uint32_t>(e));
                }
                copies[key] = {{"keep", static_cast<uint32_t>(keep)}, {"extra", others}};
            }
            if (!copies.empty()) out["duplicateActors"] = copies;
            return out;
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
        if (tool == "requirements") {
            // The AI Planner's requirements (planner/requirements.json, development_plan.md 34): what the story says must be true
            // in the game, how each is built (its items) and how it's checked; filtered by id, scene, status, kind, words.
            std::ifstream in(c.project.root / "planner" / "requirements.json", std::ios::binary);
            const nlohmann::json reqs = in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json();
            if (!reqs.is_object()) return {{"error", "this project has no requirements (AI Planner ▸ Build ▸ Requirements)"}};
            const std::string id = args.value("id", ""), scene = args.value("scene", ""), status = args.value("status", ""),
                              kind = args.value("kind", ""), text = args.value("text", "");
            const int limit = std::clamp(args.value("limit", 30), 1, 120);
            const nlohmann::json items = reqs.value("items", nlohmann::json::array()), impl = reqs.value("implementations", nlohmann::json::array());
            nlohmann::json rows = nlohmann::json::array();
            int total = 0;
            for (const auto& r : items) {
                if (!id.empty() && r.value("id", "") != id) continue;
                if (!scene.empty() && r.value("scene", "") != scene) continue;
                if (!status.empty() && r.value("status", "") != status) continue;
                if (!kind.empty() && r.value("kind", "") != kind) continue;
                if (!text.empty() && !Matches(r.value("text", ""), text)) continue;
                if (++total > limit) continue;
                nlohmann::json row = {{"id", r.value("id", "")}, {"scene", r.value("scene", "")}, {"kind", r.value("kind", "")}, {"text", r.value("text", "")},
                                      {"status", r.value("status", "todo")}, {"when", r.value("when", nlohmann::json::object())},
                                      {"where", r.value("where", nlohmann::json::object())}, {"who", r.value("who", nlohmann::json::array())},
                                      {"accept", r.value("accept", nlohmann::json::array())}};
                if (r.contains("why")) row["why"] = r["why"];
                if (r.contains("checks")) row["checks"] = r["checks"];
                if (r.contains("asset")) row["asset"] = r["asset"];
                if (r.contains("animation")) row["animation"] = r["animation"];
                nlohmann::json its = nlohmann::json::array();
                for (const auto& i : impl) {
                    if (i.value("req", "") != r.value("id", "")) continue;
                    nlohmann::json one = {{"id", i.value("id", "")}, {"how", i.value("how", "")}, {"does", i.value("does", "")}, {"by", i.value("by", "")},
                                          {"status", i.value("status", "")}};
                    if (i.contains("error")) one["error"] = i["error"];
                    its.push_back(one);
                }
                row["items"] = its;
                rows.push_back(row);
            }
            return {{"requirements", rows}, {"total", total}, {"summary", reqs.value("summary", nlohmann::json::object())}};
        }
        if (tool == "placements") {
            // The AI Planner's placements (planner/placements.json, development_plan.md 34.11): every building of the built
            // places (its kind, door, drawn interior, whose home or work it is), and where each character and story prop
            // stands and is over time (its whereabouts, each a requirement); filtered by id, scene, words.
            std::ifstream in(c.project.root / "planner" / "placements.json", std::ios::binary);
            const nlohmann::json p = in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json();
            if (!p.is_object()) return {{"error", "this project has no placements (AI Planner ▸ Build ▸ Placements ▸ Replace Characters)"}};
            const std::string id = args.value("id", ""), scene = args.value("scene", ""), text = args.value("text", "");
            const int limit = std::clamp(args.value("limit", 40), 1, 120);
            auto wanted = [&](const nlohmann::json& e) {
                if (!id.empty() && e.value("id", "") != id) return false;
                if (!scene.empty() && e.value("scene", "") != scene && e.value("spot", nlohmann::json::object()).value("scene", "") != scene) {
                    bool there = false;
                    for (const auto& w : e.value("whereabouts", nlohmann::json::array())) there = there || w.value("scene", "") == scene;
                    if (!there) return false;
                }
                return text.empty() || Matches(e.value("id", "") + " " + e.value("name", "") + " " + e.value("why", ""), text);
            };
            nlohmann::json out = {{"updated", p.value("updated", 0)}, {"decided", p.value("decided", "")}};
            for (const char* list : {"characters", "props", "buildings"}) {
                nlohmann::json rows = nlohmann::json::array();
                int total = 0;
                for (const auto& e : p.value(list, nlohmann::json::array())) {
                    if (!wanted(e) || ++total > limit) continue;
                    rows.push_back(e);
                }
                out[list] = rows;
                out[std::string(list) + "Total"] = total;
            }
            return out;
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
        if (tool == "buildings") {
            // The buildings one can go into (an Interior), for populating a place: where each is, where someone can stand
            // inside it, what's there to use, and who's in it already.
            std::vector<std::pair<float, float>> people;
            for (auto [e, at] : c.registry.view<WorldTransform2D>().each()) {
                if (c.registry.any_of<Talk, Dialogue, Barks, Behaviour, Routine>(e)) people.push_back({at.x, at.y});
            }
            nlohmann::json rows = nlohmann::json::array();
            for (auto [e, interior] : c.registry.view<Interior>().each()) {
                nlohmann::json row = Summary(c.registry, e);
                if (const auto* node = c.registry.try_get<PlaceNode>(e)) row["what"] = node->statement;
                row["kind"] = interior.kind;
                row["door"] = interior.door;
                int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                if (!InteriorFootprint(c.registry, e, interior, x0, y0, x1, y1)) {
                    row["note"] = "no footprint: it can't be gone into yet";
                    rows.push_back(row);
                    continue;
                }
                row["footprint"] = {x0, y0, x1, y1};
                if (interior.kind == "scene") {
                    // Its inside is another scene: people for it are placed there, not here.
                    row["scene"] = interior.scene.empty() ? "(not built yet)" : interior.scene;
                    row["note"] = "its inside is its own scene: people for it go in that scene (open it), not in this one";
                    int dx = 0, dy = 0;
                    if (InteriorDoor(c.registry, e, interior, dx, dy)) row["doorway"] = {dx, dy};
                    rows.push_back(row);
                    continue;
                }
                row["floors"] = std::max(1, InteriorFloors(interior));
                // Floor 1's open cells: inside its walls (the footprint's edge), not under its furniture or on its stairs.
                std::set<std::pair<int, int>> taken;
                for (const auto& [sx, sy] : InteriorSolids(interior, 1)) {
                    taken.insert({x0 + static_cast<int>(std::floor(sx)), y0 + static_cast<int>(std::floor(sy))});
                }
                if (InteriorFloors(interior) > 1) {
                    for (const auto& cell : StairsCells(interior, 1, x0, y0, x1, y1)) taken.insert(cell);
                }
                int already = 0;
                for (const auto& [px, py] : people) {
                    const int cx = static_cast<int>(std::floor(px)), cy = static_cast<int>(std::floor(py));
                    if (cx >= x0 && cx <= x1 && cy >= y0 && cy <= y1) {
                        taken.insert({cx, cy});
                        ++already;
                    }
                }
                nlohmann::json spots = nlohmann::json::array();
                for (int y = y0 + 1; y < y1 && spots.size() < 8; ++y) {
                    for (int x = x0 + 1; x < x1 && spots.size() < 8; ++x) {
                        if (!taken.contains({x, y})) spots.push_back({x + 0.5, y + 0.5});
                    }
                }
                row["standHere"] = spots; // scene.spawn x, y for someone inside, on its ground floor
                row["peopleInside"] = already;
                nlohmann::json things = nlohmann::json::array();
                for (auto [o, smart, at] : c.registry.view<SmartObject, WorldTransform2D>().each()) {
                    if (at.x >= x0 && at.x < x1 + 1 && at.y >= y0 && at.y < y1 + 1) {
                        things.push_back({{"id", static_cast<uint32_t>(o)}, {"type", smart.type}, {"at", {at.x, at.y}}});
                    }
                }
                if (!things.empty()) row["smartObjects"] = things;
                rows.push_back(row);
                if (rows.size() >= kMaxList) break;
            }
            if (rows.empty()) return {{"buildings", rows}, {"note", "no building in this scene can be gone into (none has an Interior)"}};
            return {{"buildings", rows}};
        }
        if (tool == "check") return c.check ? c.check() : nlohmann::json{{"error", "checks aren't available here"}};
        if (tool == "look") return c.look ? c.look(args) : nlohmann::json{{"error", "pictures of the view aren't available here"}};
        if (tool == "playtest") return c.playtest ? c.playtest(args) : nlohmann::json{{"error", "playtests aren't available here"}};
        return {{"error", "no tool " + tool + " (tools: find_entities, read_entity, find_assets, find_prefabs, read_file, list_scenes, "
                          "read_settings, read_input, search_world, what_is_at, buildings, check, look, playtest)"}};
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
