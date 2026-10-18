#include "assets/ProjectMove.h"

#include "assets/TileLayerIO.h"

#include "reflection/Reflection.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace aige {

namespace fs = std::filesystem;

namespace {

bool StartsWith(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

std::string ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

bool IsSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

} // namespace

std::string RewriteSceneText(const std::string& text, const std::string& from, const std::string& to, int* count) {
    std::string out;
    out.reserve(text.size());
    const size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        // A value starts after '=' that follows a key (no space before it).
        if (text[i] != '=' || i == 0 || IsSpace(text[i - 1])) {
            out += text[i++];
            continue;
        }
        out += text[i++];
        const size_t start = i;
        std::string value;
        if (i < n && text[i] == '"') {
            ++i;
            while (i < n && text[i] != '"' && text[i] != '\n') {
                if (text[i] == '\\' && i + 1 < n) {
                    ++i;
                }
                value += text[i++];
            }
            if (i < n && text[i] == '"') {
                ++i;
            }
        } else {
            while (i < n && !IsSpace(text[i])) {
                value += text[i++];
            }
        }
        if (value == from) {
            out += QuoteValue(to);
            if (count) {
                ++*count;
            }
        } else {
            out.append(text, start, i - start);
        }
    }
    return out;
}

std::string RewriteHeaderValue(const std::string& text, const std::string& key, const std::string& from,
                               const std::string& to, int* count) {
    // "key: value" lines whose value is exactly `from` (World Bible headers).
    std::string out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const bool cr = !line.empty() && line.back() == '\r';
        if (cr) line.pop_back();
        const std::string prefix = key + ":";
        if (line.rfind(prefix, 0) == 0) {
            std::string value = line.substr(prefix.size());
            const size_t first = value.find_first_not_of(" \t");
            value = first == std::string::npos ? std::string() : value.substr(first);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
            if (value == from) {
                line = prefix + " " + to;
                if (count) ++*count;
            }
        }
        out += line;
        if (cr) out += '\r';
        if (end == std::string::npos) break;
        out += '\n';
        start = end + 1;
    }
    return out;
}

std::string RewriteQuotedText(const std::string& text, const std::string& from, const std::string& to,
                              bool singleQuotes, int* count) {
    std::string out = text;
    for (char quote : {'"', '\''}) {
        if (quote == '\'' && !singleQuotes) {
            continue;
        }
        const std::string needle = quote + from + quote;
        const std::string replacement = quote + to + quote;
        size_t at = 0;
        while ((at = out.find(needle, at)) != std::string::npos) {
            out.replace(at, needle.size(), replacement);
            at += replacement.size();
            if (count) {
                ++*count;
            }
        }
    }
    return out;
}

std::string CheckMove(const Project& project, const std::string& from, const std::string& to) {
    if (from == to) {
        return "it's already there";
    }
    for (const std::string& p : {from, to}) {
        if (p.empty() || p.find("..") != std::string::npos || p.find('\\') != std::string::npos ||
            p.front() == '/' || p.find(':') != std::string::npos) {
            return "not a path inside the project: " + p;
        }
    }
    // Prefabs, assets and scenes (5.3) move within their own folder.
    std::string folder;
    for (const char* candidate : {"prefabs/", "assets/", "scenes/"}) {
        if (StartsWith(from, candidate)) folder = candidate;
    }
    if (folder.empty()) {
        return "only prefabs, assets and scenes can be moved";
    }
    if (!StartsWith(to, folder)) {
        return "it stays under " + folder;
    }
    if (fs::path(from).extension() != fs::path(to).extension()) {
        return "the file type can't change (" + fs::path(from).extension().string() + ")";
    }
    if (fs::path(to).stem().string().empty()) {
        return "the name is empty";
    }
    std::error_code ec;
    if (!fs::is_regular_file(project.Resolve(from), ec)) {
        return from + " doesn't exist";
    }
    if (fs::exists(project.Resolve(to), ec)) {
        return to + " already exists";
    }
    return {};
}

std::vector<std::string> FolderContents(const Project& project, const std::string& folder) {
    std::vector<std::string> items;
    const bool prefabs = StartsWith(folder, "prefabs/") || folder == "prefabs";
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(project.Resolve(folder), ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file()) continue;
        const std::string path = project.Relative(it->path());
        const std::string ext = it->path().extension().string();
        if (prefabs ? ext == ".prefab" : (ext != ".meta" && path.find(".gen.json") == std::string::npos)) {
            items.push_back(path);
        }
    }
    std::sort(items.begin(), items.end());
    return items;
}

std::string PathAfterFolderChange(const std::string& path, const std::string& folder, const std::string& newFolder) {
    if (!StartsWith(path, folder + "/")) return path;
    const std::string rest = path.substr(folder.size() + 1);
    std::string target = newFolder;
    if (target.empty()) {
        const auto slash = folder.find_last_of('/');
        target = slash == std::string::npos ? "" : folder.substr(0, slash);
    }
    return target.empty() ? rest : target + "/" + rest;
}

bool RemoveEmptyFolders(const Project& project, const std::string& folder) {
    std::error_code ec;
    const fs::path dir = project.Resolve(folder);
    if (!fs::is_directory(dir, ec)) return true;
    // Deepest first, so a folder whose subfolders were all empty goes too.
    std::vector<fs::path> dirs{dir};
    for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_directory()) dirs.push_back(it->path());
    }
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
        return a.native().size() > b.native().size();
    });
    for (const fs::path& d : dirs) {
        if (fs::is_empty(d, ec)) fs::remove(d, ec);
    }
    return !fs::exists(dir, ec);
}

MoveResult MoveProjectFile(const Project& project, const std::string& from, const std::string& to) {
    MoveResult result;
    result.error = CheckMove(project, from, to);
    if (!result.error.empty()) {
        return result;
    }
    std::error_code ec;
    fs::create_directories(project.Resolve(to).parent_path(), ec);
    fs::rename(project.Resolve(from), project.Resolve(to), ec);
    if (ec) {
        result.error = "couldn't move " + from + ": " + ec.message();
        return result;
    }
    for (const char* sidecar : {".meta", ".gen.json"}) {
        if (fs::exists(project.Resolve(from + sidecar), ec)) {
            fs::rename(project.Resolve(from + sidecar), project.Resolve(to + sidecar), ec);
        }
    }
    // A scene's tile layers (scenes/<name>.tiles/, 7.2).
    if (fs::path(from).extension() == ".aige" && fs::is_directory(TileLayersDir(project.Resolve(from)), ec)) {
        fs::rename(TileLayersDir(project.Resolve(from)), TileLayersDir(project.Resolve(to)), ec);
    }

    // Every text file that can refer to it.
    for (auto it = fs::recursive_directory_iterator(project.root, ec); it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) {
            break;
        }
        const fs::path& path = it->path();
        if (it->is_directory()) {
            const std::string name = path.filename().string();
            if (name == ".cache" || name == ".git") {
                it.disable_recursion_pending();
            }
            continue;
        }
        const std::string ext = path.extension().string();
        const bool scene = ext == ".aige" || ext == ".prefab";
        const bool json = ext == ".json";
        const bool lua = ext == ".lua";
        // World Bible entries: a location's `scene:` header line (6.9).
        const bool world = ext == ".md" && project.Relative(path).rfind("world/", 0) == 0;
        if (!scene && !json && !lua && !world) {
            continue;
        }
        const std::string text = ReadAll(path);
        int count = 0;
        std::string rewritten = scene ? RewriteSceneText(text, from, to, &count)
                                : world ? RewriteHeaderValue(text, "scene", from, to, &count)
                                        : RewriteQuotedText(text, from, to, lua, &count);
        if (count == 0) {
            continue;
        }
        std::ofstream(path, std::ios::binary) << rewritten;
        result.rewritten.push_back(project.Relative(path));
        result.references += count;
    }
    return result;
}

} // namespace aige
