#pragma once

#include "ai_bridge/UserPrefs.h"

#include <string>
#include <vector>

namespace aige {

// Recently opened projects, per user (development_plan.md 4.2, 5.4): in the
// user's prefs as "recentProjects" (paths, newest first) and "recentOpened"
// (path -> when it was last opened, ISO 8601).

struct RecentProject {
    std::string path;
    std::string name;   // the folder's name
    std::string opened; // ISO 8601, or empty if unknown
    bool exists = true; // the folder is still there and still a project
};

// Puts `path` first (once), stamped `openedIso`; keeps at most `max`.
void RememberProject(UserPrefs& prefs, const std::string& path, const std::string& openedIso, size_t max = 10);

// Takes `path` off the list.
void ForgetProject(UserPrefs& prefs, const std::string& path);

// The list, newest first, each checked on disk.
std::vector<RecentProject> RecentProjects(const UserPrefs& prefs);

// The same path, whatever the separators, ".." or (on Windows) letter case.
bool SameProjectPath(const std::string& a, const std::string& b);

} // namespace aige
