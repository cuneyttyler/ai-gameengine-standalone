#pragma once

#include "ai_bridge/UserPrefs.h"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <filesystem>
#include <functional>
#include <optional>

namespace aige {

// The welcome window (development_plan.md 5.4): what the editor shows when
// it's started without a project. Recent projects, New Project, Open
// Folder and the demo; choosing one starts the editor on it (a fresh
// process, as switching projects does) and closes this window.
class Launcher {
public:
    using PostFn = std::function<void(const nlohmann::json&)>;
    Launcher(HWND window, std::filesystem::path demoProject, PostFn post);

    void OnWebMessage(const nlohmann::json& message);

private:
    nlohmann::json InfoJson();
    void Open(const std::filesystem::path& root);

    HWND window_;
    std::filesystem::path demo_;
    PostFn post_;
    UserPrefs prefs_;
};

// Whether to skip the welcome window: the user asked to reopen the last
// project directly, and it's still there. Returns that project.
std::optional<std::filesystem::path> LastProjectToReopen(const UserPrefs& prefs);

} // namespace aige
