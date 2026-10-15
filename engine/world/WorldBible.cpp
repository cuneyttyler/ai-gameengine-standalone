#include "world/WorldBible.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

namespace aige {

namespace fs = std::filesystem;

namespace {

std::string Trim(const std::string& s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// Header values are single lines.
std::string OneLine(std::string s) {
    std::replace(s.begin(), s.end(), '\n', ' ');
    std::replace(s.begin(), s.end(), '\r', ' ');
    return Trim(s);
}

std::string RelationType(std::string type) {
    type = Trim(type);
    std::replace(type.begin(), type.end(), ' ', '_');
    return type;
}

bool IsKnownKind(const std::string& kind) {
    const auto& kinds = WorldKinds();
    return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
}

std::optional<std::pair<std::string, std::string>> SplitKey(const std::string& key) {
    auto slash = key.find('/');
    if (slash == std::string::npos) {
        return std::nullopt;
    }
    return std::make_pair(key.substr(0, slash), key.substr(slash + 1));
}

std::string ReadText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

const std::vector<std::string>& WorldKinds() {
    static const std::vector<std::string> kinds = {"character", "faction", "location", "item",
                                                   "event",     "lore",    "style"};
    return kinds;
}

std::vector<std::string> ParseTagList(const std::string& text) {
    std::vector<std::string> tags;
    std::istringstream in(text);
    for (std::string tag; std::getline(in, tag, ',');) {
        if (!Trim(tag).empty()) {
            tags.push_back(Trim(tag));
        }
    }
    return tags;
}

std::vector<WorldRelation> ParseRelationList(const std::string& text) {
    std::vector<WorldRelation> relations;
    std::istringstream in(text);
    for (std::string part; std::getline(in, part, ';');) {
        part = Trim(part);
        auto space = part.find_last_of(" \t");
        if (part.empty()) {
            continue;
        }
        if (space == std::string::npos) {
            relations.push_back({"related_to", part});
        } else {
            relations.push_back({RelationType(part.substr(0, space)), Trim(part.substr(space + 1))});
        }
    }
    return relations;
}

std::string WorldIdFromName(const std::string& name) {
    std::string id;
    bool dash = false;
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            if (dash && !id.empty()) {
                id += '-';
            }
            id += static_cast<char>(std::tolower(c));
            dash = false;
        } else if (c == '_' && !id.empty()) {
            id += '_';
            dash = false;
        } else {
            dash = true;
        }
    }
    return id;
}

bool IsValidWorldId(const std::string& id) {
    if (id.empty() || id.size() > 80) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (std::isalnum(c) && !std::isupper(c)) || c == '-' || c == '_';
    });
}

std::string FormatWorldEntry(const WorldEntry& entry) {
    std::ostringstream out;
    out << "---\n";
    out << "name: " << OneLine(entry.name) << "\n";
    if (!entry.summary.empty()) {
        out << "summary: " << OneLine(entry.summary) << "\n";
    }
    if (!entry.tags.empty()) {
        out << "tags: ";
        for (size_t i = 0; i < entry.tags.size(); ++i) {
            out << (i ? ", " : "") << OneLine(entry.tags[i]);
        }
        out << "\n";
    }
    for (const auto& relation : entry.relations) {
        out << "rel: " << RelationType(relation.type) << " " << OneLine(relation.target) << "\n";
    }
    for (const auto& [key, value] : entry.extra) {
        out << key << ": " << OneLine(value) << "\n";
    }
    out << "---\n";
    std::string body = entry.body;
    while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) {
        body.pop_back();
    }
    if (!body.empty()) {
        out << body << "\n";
    }
    return out.str();
}

std::optional<WorldEntry> ParseWorldEntry(const std::string& text, const std::string& kind, const std::string& id,
                                          std::string* error) {
    std::istringstream in(text);
    std::string line;
    // Leading blank lines are fine; then the header must open with ---.
    while (std::getline(in, line) && Trim(line).empty()) {
    }
    if (Trim(line) != "---") {
        if (error) *error = "doesn't start with a --- header";
        return std::nullopt;
    }
    WorldEntry entry;
    entry.kind = kind;
    entry.id = id;
    bool closed = false;
    while (std::getline(in, line)) {
        std::string trimmed = Trim(line);
        if (trimmed == "---") {
            closed = true;
            break;
        }
        if (trimmed.empty()) {
            continue;
        }
        auto colon = trimmed.find(':');
        if (colon == std::string::npos) {
            if (error) *error = "header line without a key: " + trimmed;
            return std::nullopt;
        }
        std::string key = Trim(trimmed.substr(0, colon));
        std::string value = Trim(trimmed.substr(colon + 1));
        if (key == "name") {
            entry.name = value;
        } else if (key == "summary") {
            entry.summary = value;
        } else if (key == "tags") {
            entry.tags = ParseTagList(value);
        } else if (key == "rel") {
            auto relations = ParseRelationList(value);
            entry.relations.insert(entry.relations.end(), relations.begin(), relations.end());
        } else if (key != "kind") { // the folder decides the kind
            entry.extra.emplace_back(key, value);
        }
    }
    if (!closed) {
        if (error) *error = "the --- header is never closed";
        return std::nullopt;
    }
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t start = body.find_first_not_of("\r\n");
    entry.body = start == std::string::npos ? std::string() : body.substr(start);
    while (!entry.body.empty() && (entry.body.back() == '\n' || entry.body.back() == '\r')) {
        entry.body.pop_back();
    }
    return entry;
}

WorldBible::WorldBible(Project project) : project_(std::move(project)) {}

fs::path WorldBible::PathOf(const std::string& kind, const std::string& id) const {
    return project_.root / "world" / kind / (id + ".md");
}

void WorldBible::Refresh() {
    entries_.clear();
    parseProblems_.clear();
    std::error_code ec;
    for (const auto& kind : WorldKinds()) {
        fs::path dir = project_.root / "world" / kind;
        if (!fs::is_directory(dir, ec)) {
            continue;
        }
        std::vector<fs::path> files;
        for (const auto& file : fs::directory_iterator(dir, ec)) {
            if (file.is_regular_file() && file.path().extension() == ".md") {
                files.push_back(file.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const auto& path : files) {
            std::string id = path.stem().string();
            std::string error;
            auto entry = ParseWorldEntry(ReadText(path), kind, id, &error);
            if (entry) {
                entries_.push_back(std::move(*entry));
            } else {
                parseProblems_.push_back({kind + "/" + id, error});
            }
        }
    }
}

const WorldEntry* WorldBible::Find(const std::string& key) const {
    for (const auto& entry : entries_) {
        if (entry.Key() == key) {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<WorldBible::Problem> WorldBible::Validate() const {
    std::vector<Problem> problems = parseProblems_;
    for (const auto& entry : entries_) {
        if (Trim(entry.name).empty()) {
            problems.push_back({entry.Key(), "has no name"});
        }
        if (!IsValidWorldId(entry.id)) {
            problems.push_back({entry.Key(), "file name should be lowercase letters, digits, - and _"});
        }
        for (const auto& relation : entry.relations) {
            if (Find(relation.target) == nullptr) {
                problems.push_back({entry.Key(), relation.type + " points at " + relation.target +
                                                     ", which doesn't exist"});
            }
        }
    }
    return problems;
}

std::optional<std::string> WorldBible::Save(const WorldEntry& entry) {
    if (!IsKnownKind(entry.kind)) {
        return "unknown kind '" + entry.kind + "'";
    }
    if (Trim(entry.name).empty()) {
        return "an entry needs a name";
    }
    if (!IsValidWorldId(entry.id)) {
        return "ids are lowercase letters, digits, - and _";
    }
    fs::path path = PathOf(entry.kind, entry.id);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return "couldn't write " + project_.Relative(path);
    }
    out << FormatWorldEntry(entry);
    out.close();
    Refresh();
    return std::nullopt;
}

std::optional<std::string> WorldBible::Remove(const std::string& key) {
    const WorldEntry* entry = Find(key);
    if (entry == nullptr) {
        return "no entry " + key;
    }
    std::error_code ec;
    fs::remove(PathOf(entry->kind, entry->id), ec);
    if (ec) {
        return "couldn't delete " + key + ": " + ec.message();
    }
    Refresh();
    return std::nullopt;
}

std::optional<std::string> WorldBible::Rename(const std::string& key, const std::string& newKey) {
    if (key == newKey) {
        return std::nullopt;
    }
    const WorldEntry* existing = Find(key);
    auto parts = SplitKey(newKey);
    if (existing == nullptr) {
        return "no entry " + key;
    }
    if (!parts || !IsKnownKind(parts->first) || !IsValidWorldId(parts->second)) {
        return "'" + newKey + "' isn't a valid <kind>/<id>";
    }
    if (Find(newKey) != nullptr) {
        return newKey + " already exists";
    }
    WorldEntry moved = *existing;
    moved.kind = parts->first;
    moved.id = parts->second;
    std::vector<WorldEntry> updated;
    for (const auto& entry : entries_) {
        if (entry.Key() == key) {
            continue;
        }
        WorldEntry copy = entry;
        bool changed = false;
        for (auto& relation : copy.relations) {
            if (relation.target == key) {
                relation.target = newKey;
                changed = true;
            }
        }
        if (changed) {
            updated.push_back(std::move(copy));
        }
    }
    for (auto& relation : moved.relations) { // a self-reference moves too
        if (relation.target == key) {
            relation.target = newKey;
        }
    }
    const fs::path oldPath = PathOf(existing->kind, existing->id); // Save() refreshes entries_
    if (auto error = Save(moved)) {
        return error;
    }
    std::error_code ec;
    fs::remove(oldPath, ec);
    for (const auto& entry : updated) {
        if (auto error = Save(entry)) {
            return error;
        }
    }
    Refresh();
    return std::nullopt;
}

} // namespace aige
