// Where the program's data is (the Beta package, docs/beta-release.md): a folder next to the running
// program when there is one, else the source tree's, as compiled in. A build run from the build tree
// (and the tests) uses the source tree; a packaged one, what was copied beside it.
#pragma once

#include <filesystem>
#include <string>

namespace aige {

// The folder of the running program.
std::filesystem::path ProgramDir();

// `relative` (e.g. "data/places") next to the program when it's there, else `built`.
std::filesystem::path InstalledOr(const std::filesystem::path& built, const std::string& relative);

} // namespace aige
