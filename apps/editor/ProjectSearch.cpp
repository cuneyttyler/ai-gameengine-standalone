// Edit ▸ Search in project (Ctrl+Shift+F) and "Show uses": the project's text files read on the host (scenes,
// prefabs, records and packages, story, scripts, UI, planner documents) and its art by file name. A scene's matches
// say which of its things they are in (the entity's place in the file and its name), so opening one selects it
// (scene:reveal). The open scene is searched as it is now, saved or not.

#include "EditorApp.h"

#include "assets/SceneIO.h"
#include "gameplay/Components.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace aige {

namespace {

namespace fs = std::filesystem;

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

// What a project file is, by where it is: the kinds the search filters by and the UI opens each its own way.
std::string KindOf(const std::string& rel) {
    const std::string top = rel.substr(0, rel.find('/'));
    if (rel == "settings.aige") return "settings";
    if (top == "scenes" && rel.ends_with(".aige")) return "scene";
    if (top == "prefabs") return rel.ends_with(".prefab") ? "prefab" : "other";
    if (top == "records") return rel.rfind("records/package/", 0) == 0 ? "package" : "record";
    if (top == "quests") return "quest";
    if (top == "dialogue") return "dialogue";
    if (top == "schedules") return "schedule";
    if (top == "cutscenes") return "cutscene";
    if (top == "scripts") return "script";
    if (top == "world") return "world";
    if (top == "planner") return "planner";
    if (top == "ui") return "ui";
    if (top == "assets") return "art";
    if (top == "modules") return "module";
    if (top == "loc") return "loc";
    return "other";
}

bool IsText(const std::string& rel) {
    static const std::set<std::string> kExt = {".aige", ".prefab", ".json", ".lua", ".md", ".txt", ".rml", ".rcss", ".csv", ".yaml", ".yml", ".ini"};
    return kExt.contains(Lower(fs::path(rel).extension().string()));
}

struct ProjectFile {
    std::string rel, kind;
};

// The project's own content: its known folders (not .cache, builds or anything else), art's .meta files left out.
std::vector<ProjectFile> ListProjectFiles(const fs::path& root) {
    static const char* kFolders[] = {"scenes", "prefabs", "records", "quests", "dialogue", "schedules", "cutscenes", "scripts",
                                     "world",  "planner", "ui",      "assets", "modules",  "loc"};
    std::vector<ProjectFile> files;
    std::error_code ec;
    if (fs::is_regular_file(root / "settings.aige", ec)) files.push_back({"settings.aige", "settings"});
    for (const char* folder : kFolders) {
        const fs::path dir = root / folder;
        if (!fs::is_directory(dir, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            const std::string name = it->path().filename().string();
            if (it->is_directory(ec)) {
                if (!name.empty() && name[0] == '.') it.disable_recursion_pending();
                continue;
            }
            if (name.ends_with(".meta") || (!name.empty() && name[0] == '.')) continue;
            const std::string rel = it->path().lexically_relative(root).generic_string();
            if (rel.empty()) continue;
            files.push_back({rel, KindOf(rel)});
        }
    }
    std::sort(files.begin(), files.end(), [](const ProjectFile& a, const ProjectFile& b) { return a.rel < b.rel; });
    return files;
}

std::vector<std::string> ReadLines(const fs::path& path, const std::string* override) {
    std::vector<std::string> lines;
    std::error_code ec;
    if (!override && fs::file_size(path, ec) > 8u * 1024u * 1024u) return lines; // not something one searches
    std::stringstream text;
    if (override) {
        text << *override;
    } else {
        std::ifstream in(path, std::ios::binary);
        text << in.rdbuf();
    }
    std::string line;
    while (std::getline(text, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

// `key=value` pairs of a scene or prefab line (a value may be double-quoted, with \" and \\).
std::vector<std::pair<std::string, std::string>> Fields(const std::string& line) {
    std::vector<std::pair<std::string, std::string>> fields;
    size_t i = 0;
    const size_t n = line.size();
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        const size_t keyStart = i;
        while (i < n && line[i] != '=' && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= n || line[i] != '=') continue;
        std::string key = line.substr(keyStart, i - keyStart), value;
        ++i;
        if (i < n && line[i] == '"') {
            ++i;
            while (i < n && line[i] != '"') {
                if (line[i] == '\\' && i + 1 < n) ++i;
                value += line[i++];
            }
            ++i;
        } else {
            while (i < n && !std::isspace(static_cast<unsigned char>(line[i]))) value += line[i++];
        }
        fields.emplace_back(std::move(key), std::move(value));
    }
    return fields;
}

// The string literals of a JSON or Lua line.
std::vector<std::string> Strings(const std::string& line, bool singleQuotes) {
    std::vector<std::string> out;
    for (size_t i = 0; i < line.size(); ++i) {
        const char q = line[i];
        if (q != '"' && !(singleQuotes && q == '\'')) continue;
        std::string value;
        size_t j = i + 1;
        while (j < line.size() && line[j] != q) {
            if (line[j] == '\\' && j + 1 < line.size()) ++j;
            value += line[j++];
        }
        out.push_back(std::move(value));
        i = j;
    }
    return out;
}

// The values a line gives: what a reference could be in.
std::vector<std::string> ValuesOf(const std::string& rel, const std::string& line) {
    const std::string ext = Lower(fs::path(rel).extension().string());
    std::vector<std::string> values;
    if (ext == ".aige" || ext == ".prefab") {
        for (auto& [key, value] : Fields(line)) values.push_back(std::move(value));
    } else if (ext == ".json") {
        values = Strings(line, false);
    } else if (ext == ".lua") {
        values = Strings(line, true);
    }
    return values;
}

// What a reference to one thing looks like: its path anywhere in a value, or a whole value (or one of a list's
// items, "hoe, seeds*2") that is one of its names.
struct Target {
    std::string path;            // project-relative
    std::set<std::string> names; // lower case
};

bool Refers(const std::string& value, const Target& target) {
    if (!target.path.empty() && target.path.find('/') != std::string::npos && value.find(target.path) != std::string::npos) return true;
    if (target.names.empty() || value.size() > 400) return false;
    std::vector<std::string> tokens;
    std::string token;
    std::stringstream byComma(value);
    while (std::getline(byComma, token, ',')) tokens.push_back(Trim(token));
    // A short lower-case phrase (an effect "give bread"), not a line someone says: its words too.
    const size_t words = static_cast<size_t>(std::count(value.begin(), value.end(), ' ')) + 1;
    const bool lower = std::none_of(value.begin(), value.end(), [](char c) { return std::isupper(static_cast<unsigned char>(c)); });
    if (words <= 4 && lower) {
        std::stringstream byWord(value);
        while (byWord >> token) tokens.push_back(token);
    }
    for (std::string t : tokens) {
        if (const size_t star = t.find('*'); star != std::string::npos) t = t.substr(0, star); // seeds*2
        while (!t.empty() && std::string("()[]{}\"'").find(t.back()) != std::string::npos) t.pop_back();
        while (!t.empty() && std::string("()[]{}\"'").find(t.front()) != std::string::npos) t.erase(0, 1);
        if (!t.empty() && target.names.contains(Lower(t))) return true;
    }
    return false;
}

// A scene's things: for each line, the entity it belongs to (-1 before the first), and each entity's name.
struct SceneMap {
    std::vector<int> entityOfLine;
    std::vector<std::string> names;
};

SceneMap MapScene(const std::vector<std::string>& lines) {
    SceneMap map;
    map.entityOfLine.assign(lines.size(), -1);
    std::vector<std::string> prefab, marker;
    int current = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string t = Trim(lines[i]);
        if (t == "entity") {
            ++current;
            map.names.emplace_back();
            prefab.emplace_back();
            marker.emplace_back();
        } else if (current >= 0) {
            const std::string component = t.substr(0, t.find(' '));
            if (component == "Name" || component == "PrefabInstance" || component == "Marker") {
                for (const auto& [key, value] : Fields(t.substr(component.size()))) {
                    if (component == "Name" && key == "value") map.names[current] = value;
                    if (component == "PrefabInstance" && key == "path") prefab[current] = fs::path(value).stem().string();
                    if (component == "Marker" && key == "name") marker[current] = "Marker " + value;
                }
            }
        }
        map.entityOfLine[i] = current;
    }
    for (size_t k = 0; k < map.names.size(); ++k) {
        if (map.names[k].empty()) map.names[k] = !marker[k].empty() ? marker[k] : !prefab[k].empty() ? prefab[k] : "Entity #" + std::to_string(k + 1);
    }
    return map;
}

// The line shown for a match: trimmed, and cut short around the match when long.
std::string Excerpt(const std::string& line, size_t at) {
    std::string t = Trim(line);
    if (t.size() <= 180) return t;
    const size_t start = at > 60 ? std::min(at - 60, t.size() - 180) : 0;
    return (start > 0 ? "..." : "") + t.substr(start, 180) + "...";
}

struct Scan {
    nlohmann::json files = nlohmann::json::array();
    int hits = 0;
    bool truncated = false;
};

constexpr int kMaxFiles = 400, kMaxHits = 4000, kHitsPerFile = 60;

// Each file through `match` (a line's text and the file -> where in it, or npos); a file's name through `byName`.
void ScanProject(const fs::path& root, const std::set<std::string>& kinds, const std::map<std::string, std::string>& overrides,
                 const std::string& skip, const std::function<size_t(const std::string&, const std::string&)>& match,
                 const std::function<bool(const std::string&)>& byName, Scan& scan) {
    for (const ProjectFile& file : ListProjectFiles(root)) {
        if (!kinds.empty() && !kinds.contains(file.kind)) continue;
        if (file.rel == skip) continue;
        if (static_cast<int>(scan.files.size()) >= kMaxFiles || scan.hits >= kMaxHits) {
            scan.truncated = true;
            break;
        }
        nlohmann::json hits = nlohmann::json::array();
        const bool named = byName(file.rel);
        if (file.kind != "art" && IsText(file.rel)) {
            const auto over = overrides.find(file.rel);
            const std::vector<std::string> lines = ReadLines(root / file.rel, over == overrides.end() ? nullptr : &over->second);
            std::optional<SceneMap> scene;
            for (size_t i = 0; i < lines.size(); ++i) {
                const size_t at = match(lines[i], file.rel);
                if (at == std::string::npos) continue;
                if (static_cast<int>(hits.size()) >= kHitsPerFile) {
                    scan.truncated = true;
                    break;
                }
                nlohmann::json hit = {{"line", i + 1}, {"text", Excerpt(lines[i], at)}};
                if (file.kind == "scene") {
                    if (!scene) scene = MapScene(lines);
                    if (const int e = scene->entityOfLine[i]; e >= 0) hit["entity"] = {{"index", e}, {"name", scene->names[e]}};
                }
                hits.push_back(std::move(hit));
            }
        }
        if (hits.empty() && !named) continue;
        scan.hits += static_cast<int>(hits.size());
        const fs::path rel(file.rel);
        nlohmann::json entry = {{"path", file.rel}, {"kind", file.kind}, {"id", rel.stem().string()}, {"byName", named}, {"hits", hits}};
        if (file.kind == "record" || file.kind == "package") {
            const std::string rest = file.rel.substr(std::string("records/").size());
            entry["recordType"] = rest.substr(0, rest.find('/'));
        }
        scan.files.push_back(std::move(entry));
    }
}

} // namespace

bool EditorApp::HandleSearchMessage(const std::string& type, const nlohmann::json& message) {
    const fs::path root = assets_.GetProject().root;
    // The open scene as it is now: its unsaved changes are searched too.
    std::map<std::string, std::string> overrides;
    if (!playing_ && dirty_ && !scenePath_.empty()) overrides[OpenSceneRelative()] = SerializeScene(registry_);
    std::set<std::string> kinds;
    for (const auto& k : message.value("kinds", nlohmann::json::array())) {
        if (k.is_string()) kinds.insert(k.get<std::string>());
    }

    if (type == "project:search") {
        // Text anywhere in the project's files, every word of it on the line (any case); art by its file name.
        const std::string query = Trim(message.value("query", std::string()));
        std::vector<std::string> words;
        {
            std::stringstream in(Lower(query));
            for (std::string w; in >> w;) words.push_back(w);
        }
        const bool exact = message.value("exact", false); // the whole query, as typed (case and all)
        Scan scan;
        if (!words.empty()) {
            ScanProject(
                root, kinds, overrides, "",
                [&](const std::string& line, const std::string&) -> size_t {
                    if (exact) return line.find(query);
                    const std::string low = Lower(line);
                    size_t first = std::string::npos;
                    for (const auto& w : words) {
                        const size_t at = low.find(w);
                        if (at == std::string::npos) return std::string::npos;
                        first = std::min(first, at);
                    }
                    return first;
                },
                [&](const std::string& rel) {
                    const std::string name = exact ? fs::path(rel).filename().string() : Lower(fs::path(rel).filename().string());
                    if (exact) return name.find(query) != std::string::npos;
                    return std::all_of(words.begin(), words.end(), [&](const std::string& w) { return name.find(w) != std::string::npos; });
                },
                scan);
        }
        post_({{"type", "project:searchResults"}, {"query", query}, {"seq", message.value("seq", 0)}, {"files", scan.files},
               {"truncated", scan.truncated}});
        return true;
    }

    if (type == "project:uses") {
        // What refers to one thing: its path, or one of its names (an id) as a whole value.
        const std::string kind = message.value("kind", std::string());
        Target target;
        target.path = message.value("path", std::string());
        const std::string id = message.value("id", std::string());
        if (!id.empty()) target.names.insert(Lower(id));
        if (!target.path.empty()) {
            const fs::path p(target.path);
            target.names.insert(Lower(target.path));
            const std::string noExt = (p.parent_path() / p.stem()).generic_string();
            target.names.insert(Lower(noExt));
            // Art is named by its file next to it (an animation's sheet); a script by its path without scripts/.
            if (kind == "art") target.names.insert(Lower(p.filename().string()));
            if (kind == "script" && target.path.rfind("scripts/", 0) == 0) target.names.insert(Lower(target.path.substr(8)));
            // Things known by their id (quests, dialogue, records, cutscenes, scenes, screens): the file's name.
            if (kind != "art" && kind != "prefab" && kind != "script") target.names.insert(Lower(p.stem().string()));
        }
        Scan scan;
        if (!target.path.empty() || !target.names.empty()) {
            ScanProject(
                root, kinds, overrides, target.path,
                [&](const std::string& line, const std::string& rel) -> size_t {
                    for (const std::string& value : ValuesOf(rel, line)) {
                        if (Refers(value, target)) {
                            const size_t at = line.find(value);
                            return at == std::string::npos ? 0 : at;
                        }
                    }
                    // Prose and markup (planner documents, screens): its path only.
                    const std::string ext = Lower(fs::path(rel).extension().string());
                    if (target.path.find('/') != std::string::npos && ext != ".aige" && ext != ".prefab" && ext != ".json" && ext != ".lua") {
                        return line.find(target.path);
                    }
                    return std::string::npos;
                },
                [](const std::string&) { return false; }, scan);
        }
        post_({{"type", "project:usesResults"}, {"kind", kind}, {"path", target.path}, {"id", id},
               {"label", message.value("label", id.empty() ? target.path : id)}, {"files", scan.files}, {"truncated", scan.truncated}});
        return true;
    }

    if (type == "scene:reveal") {
        // A search result in a scene: that scene opened (asking about unsaved changes first) and the thing selected.
        const std::string scene = message.value("path", std::string());
        if (scene.empty() || playing_) return true;
        if (scene != OpenSceneRelative()) OpenScene(scene);
        if (scene != OpenSceneRelative()) return true; // not opened (cancelled, or missing)
        const int index = message.value("index", -1);
        const std::string name = message.value("name", std::string());
        const std::vector<Entity> order = EntitiesInOrder(registry_);
        auto nameOf = [&](Entity e) {
            const auto* n = registry_.try_get<Name>(e);
            return n ? n->value : std::string();
        };
        std::optional<Entity> found;
        if (index >= 0 && index < static_cast<int>(order.size()) && (name.empty() || nameOf(order[index]) == name || nameOf(order[index]).empty())) {
            found = order[index];
        }
        if (!found && !name.empty()) {
            for (Entity e : order) {
                if (nameOf(e) == name) {
                    found = e;
                    break;
                }
            }
        }
        if (!found && index >= 0 && index < static_cast<int>(order.size())) found = order[index];
        if (found) {
            Select(found);
            sceneTreeStale_ = true;
        } else {
            Notify("That thing isn't in " + scene + " any more");
        }
        return true;
    }
    return false;
}

} // namespace aige
