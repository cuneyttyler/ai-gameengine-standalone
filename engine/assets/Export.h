#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace aige {

// Build & Export, desktop (development_plan.md 5.1). An exported game is a
// folder that runs on its own:
//
//   <destination>/<Name>/
//     <Name>.exe        the player, renamed
//     shaders/          what the player needs next to it
//     game/             the project's runtime content, and export.json {name, ...}
//
// and optionally <destination>/<Name>.zip of that folder. The player finds
// game/ next to itself (apps/sandbox).

// Whether a project-relative file belongs in an export: the scene, settings,
// prefabs, scripts, assets (with their .meta), story, world and modules. Not
// .cache, the AI log, generation sidecars (.gen.json), hidden files, or
// anything else at the top level.
bool IsRuntimeContent(const std::string& relative);

// "My Game!" -> "My Game": safe as a folder and file name (empty if nothing is left).
std::string SafeGameName(const std::string& name);

struct ExportOptions {
    std::filesystem::path project;     // the project root
    std::filesystem::path playerDir;   // a built player: player.exe (any name given) plus shaders/
    std::string playerExe = "sandbox.exe";
    std::filesystem::path destination; // the folder to export into
    std::string name;                  // the game's name (window title, file names)
    bool zip = false;
    // Shipping (24).
    std::string version = "1.0";       // export.json, the window's title, the crash log
    bool debugConsole = false;         // the debug console stays available in the export
    nlohmann::json accessibility;      // the accessibility report, as game/accessibility.json
    std::string startScene;            // export.json (the player starts there)
};

struct ExportResult {
    std::string error; // empty on success
    std::filesystem::path folder;
    std::filesystem::path zip;
    std::vector<std::string> files; // relative to `folder`
};

// Refuses an existing <destination>/<Name> folder (never overwrites).
ExportResult ExportGame(const ExportOptions& options);

// The game's icon on the exported player (Windows): a PNG embedded as the exe's icon
// resource. Returns why not, or "".
std::string SetExecutableIcon(const std::filesystem::path& exe, const std::filesystem::path& png);

// A .zip (stored, no compression) of every file under `folder`, with paths
// starting at the folder's own name. Returns false on an I/O error.
bool WriteZip(const std::filesystem::path& folder, const std::filesystem::path& zipPath);

} // namespace aige
