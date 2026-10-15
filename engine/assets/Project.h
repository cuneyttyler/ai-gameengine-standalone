#pragma once

#include <filesystem>
#include <string>

namespace aige {

// A game project on disk — the unit the editor opens and the runtime plays:
//
//   <root>/scenes/main.aige    the scene loaded on open / play
//   <root>/settings.aige       project settings (renderer, camera, combat, ...)
//   <root>/prefabs/            placeable entity templates (the editor palette)
//   <root>/scripts/            Lua gameplay scripts
//   <root>/assets/             imported art and audio (each with a .meta)
//   <root>/.cache/             cooked asset data (derived; not versioned)
//
// Every path stored inside a project (scene files, metas) is relative to
// the root with '/' separators, so projects are portable and diff cleanly.
struct Project {
    std::filesystem::path root;

    std::filesystem::path MainScenePath() const { return root / "scenes" / "main.aige"; }
    std::filesystem::path SettingsPath() const { return root / "settings.aige"; }
    std::filesystem::path AssetsDir() const { return root / "assets"; }
    std::filesystem::path CacheDir() const { return root / ".cache"; }

    std::filesystem::path Resolve(const std::string& relative) const { return root / relative; }

    // Project-relative, '/'-separated form of a path inside the project.
    std::string Relative(const std::filesystem::path& absolute) const {
        return std::filesystem::relative(absolute, root).generic_string();
    }
};

} // namespace aige
