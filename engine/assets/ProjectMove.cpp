#include "assets/ProjectMove.h"

#include "reflection/Reflection.h"

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
        if (!scene && !json && !lua) {
            continue;
        }
        const std::string text = ReadAll(path);
        int count = 0;
        std::string rewritten = scene ? RewriteSceneText(text, from, to, &count)
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
