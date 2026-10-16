#include "Launcher.h"

#include "Win32Util.h"

#include "ai_bridge/RecentProjects.h"
#include "assets/ProjectTemplate.h"

namespace aige {

Launcher::Launcher(HWND window, std::filesystem::path demoProject, PostFn post)
    : window_(window), demo_(std::move(demoProject)), post_(std::move(post)), prefs_(UserPrefs::DefaultPath()) {}

nlohmann::json Launcher::InfoJson() {
    nlohmann::json recent = nlohmann::json::array();
    for (const RecentProject& project : RecentProjects(prefs_)) {
        recent.push_back(
            {{"path", project.path}, {"name", project.name}, {"opened", project.opened}, {"exists", project.exists}});
    }
    return {{"type", "welcome:info"},
            {"recent", recent},
            {"openLast", prefs_.Get("openLastProject", false)},
            {"demo", WideToUtf8(std::filesystem::absolute(demo_).wstring())}};
}

void Launcher::Open(const std::filesystem::path& root) {
    if (!LooksLikeProject(root)) {
        post_({{"type", "welcome:error"},
               {"text", WideToUtf8(root.wstring()) + " isn't a project (no scenes, settings or prefabs)."}});
        return;
    }
    if (!StartEditorProcess(L"--project \"" + std::filesystem::absolute(root).wstring() + L"\"")) {
        post_({{"type", "welcome:error"}, {"text", "Couldn't start the editor."}});
        return;
    }
    DestroyWindow(window_);
}

void Launcher::OnWebMessage(const nlohmann::json& message) {
    const std::string type = message.value("type", "");
    if (type == "ui:error") {
        if (message.value("fatal", false)) {
            MessageBoxW(window_, Utf8ToWide("The welcome window failed to start:\n\n" + message.value("text", "")).c_str(),
                        L"Editor", MB_ICONERROR);
        }
    } else if (type == "welcome:request") {
        post_(InfoJson());
    } else if (type == "welcome:open") {
        Open(std::filesystem::path(Utf8ToWide(message.value("path", ""))));
    } else if (type == "welcome:openFolder") {
        if (auto folder = ShowFolderDialog(window_, L"Open project")) {
            Open(*folder);
        }
    } else if (type == "welcome:demo") {
        Open(demo_);
    } else if (type == "welcome:new") {
        const std::string name = message.value("name", "");
        if (name.empty()) {
            return;
        }
        auto parent = ShowFolderDialog(window_, L"Where to create \"" + Utf8ToWide(name) + L"\"");
        if (!parent) {
            return;
        }
        const std::filesystem::path root = *parent / Utf8ToWide(name);
        std::optional<std::filesystem::path> starter;
        if (message.value("starter", false)) {
            starter = demo_;
        }
        if (auto error = CreateProject(root, starter)) {
            post_({{"type", "welcome:error"}, {"text", *error}});
            return;
        }
        Open(root);
    } else if (type == "welcome:forget") {
        ForgetProject(prefs_, message.value("path", ""));
        post_(InfoJson());
    } else if (type == "prefs:openLast") {
        prefs_.Set("openLastProject", message.value("on", false));
        post_(InfoJson());
    }
}

std::optional<std::filesystem::path> LastProjectToReopen(const UserPrefs& prefs) {
    const nlohmann::json openLast = prefs.Get("openLastProject", false);
    if (!openLast.is_boolean() || !openLast.get<bool>()) {
        return std::nullopt;
    }
    for (const RecentProject& project : RecentProjects(prefs)) {
        if (project.exists) {
            return std::filesystem::path(Utf8ToWide(project.path));
        }
        break; // only the last one counts
    }
    return std::nullopt;
}

} // namespace aige
