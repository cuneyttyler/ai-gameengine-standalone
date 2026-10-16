#pragma once

#include "assets/Project.h"

#include <string>
#include <vector>

namespace aige {

// Moving and renaming prefabs and assets (development_plan.md 4.4). A file
// is only ever referred to by its project-relative path, so a move rewrites
// every exact reference to it in the project's text files: scenes, prefabs
// and settings (scene format), story, world and other JSON, and quoted
// strings in Lua scripts. `.cache` and the AI log are left alone.

struct MoveResult {
    std::string error;                      // empty on success
    std::vector<std::string> rewritten;     // project-relative files whose references changed
    int references = 0;                     // how many references were rewritten
};

// Moves `from` to `to` (both project-relative): a prefab stays under
// prefabs/ with its .prefab extension; an asset stays under assets/ with
// its extension, and its .meta and .gen.json move along. Refuses an
// existing destination.
MoveResult MoveProjectFile(const Project& project, const std::string& from, const std::string& to);

// The checks MoveProjectFile makes before touching anything: why `from`
// can't move to `to`, or empty.
std::string CheckMove(const Project& project, const std::string& from, const std::string& to);

// Rewrites `key=value` values exactly equal to `from` in scene-format text
// (quoted or not); the new value is quoted when it needs to be.
std::string RewriteSceneText(const std::string& text, const std::string& from, const std::string& to,
                             int* count = nullptr);

// Rewrites "from" (and 'from' when `singleQuotes`) string literals.
std::string RewriteQuotedText(const std::string& text, const std::string& from, const std::string& to,
                              bool singleQuotes, int* count = nullptr);

} // namespace aige
