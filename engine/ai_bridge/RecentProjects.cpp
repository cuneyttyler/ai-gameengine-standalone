#include "ai_bridge/RecentProjects.h"

#include "assets/ProjectTemplate.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace aige {

namespace fs = std::filesystem;

namespace {

fs::path FromUtf8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

std::string Key(const std::string& path) {
    std::string key = FromUtf8(path).lexically_normal().generic_string();
    while (key.size() > 1 && key.back() == '/') key.pop_back();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return key;
}

} // namespace

bool SameProjectPath(const std::string& a, const std::string& b) { return Key(a) == Key(b); }

void RememberProject(UserPrefs& prefs, const std::string& path, const std::string& openedIso, size_t max) {
    nlohmann::json recent = nlohmann::json::array({path});
    for (const auto& entry : prefs.Get("recentProjects", nlohmann::json::array())) {
        if (entry.is_string() && !SameProjectPath(entry.get<std::string>(), path) && recent.size() < max) {
            recent.push_back(entry);
        }
    }
    nlohmann::json opened = prefs.Get("recentOpened", nlohmann::json::object());
    if (!opened.is_object()) opened = nlohmann::json::object();
    nlohmann::json kept = nlohmann::json::object();
    for (const auto& entry : recent) {
        const std::string p = entry.get<std::string>();
        if (p == path) {
            kept[p] = openedIso;
        } else if (opened.contains(p)) {
            kept[p] = opened[p];
        }
    }
    prefs.Set("recentProjects", recent);
    prefs.Set("recentOpened", kept);
}

void ForgetProject(UserPrefs& prefs, const std::string& path) {
    nlohmann::json recent = nlohmann::json::array();
    for (const auto& entry : prefs.Get("recentProjects", nlohmann::json::array())) {
        if (entry.is_string() && !SameProjectPath(entry.get<std::string>(), path)) recent.push_back(entry);
    }
    nlohmann::json opened = prefs.Get("recentOpened", nlohmann::json::object());
    if (opened.is_object()) {
        for (auto it = opened.begin(); it != opened.end();) {
            it = SameProjectPath(it.key(), path) ? opened.erase(it) : std::next(it);
        }
        prefs.Set("recentOpened", opened);
    }
    prefs.Set("recentProjects", recent);
}

std::vector<RecentProject> RecentProjects(const UserPrefs& prefs) {
    std::vector<RecentProject> list;
    const nlohmann::json opened = prefs.Get("recentOpened", nlohmann::json::object());
    for (const auto& entry : prefs.Get("recentProjects", nlohmann::json::array())) {
        if (!entry.is_string()) continue;
        RecentProject project;
        project.path = entry.get<std::string>();
        const fs::path folder = FromUtf8(project.path);
        const std::u8string name = folder.lexically_normal().filename().u8string();
        project.name = std::string(name.begin(), name.end());
        if (opened.is_object() && opened.contains(project.path) && opened[project.path].is_string()) {
            project.opened = opened[project.path].get<std::string>();
        }
        std::error_code ec;
        project.exists = fs::is_directory(folder, ec) && LooksLikeProject(folder);
        list.push_back(std::move(project));
    }
    return list;
}

} // namespace aige
