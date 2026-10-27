#include "assets/ProjectTemplate.h"

#include <fstream>

namespace aige {

namespace fs = std::filesystem;

namespace {

const char* kFolders[] = {"scenes", "prefabs", "scripts", "assets", "world", "dialogue", "quests", "schedules"};

std::optional<std::string> MustBeEmptyOrMissing(const fs::path& root) {
    std::error_code ec;
    if (fs::exists(root, ec) && (!fs::is_directory(root, ec) || !fs::is_empty(root, ec))) {
        return root.string() + " already exists and isn't empty";
    }
    return std::nullopt;
}

bool Excluded(const fs::path& relative) {
    const std::string first = relative.begin() != relative.end() ? relative.begin()->string() : std::string();
    return first == ".cache" || relative == "ai_log.jsonl";
}

} // namespace

bool LooksLikeProject(const fs::path& root) {
    std::error_code ec;
    return fs::exists(root / "scenes" / "main.aige", ec) || fs::exists(root / "settings.aige", ec) ||
           fs::is_directory(root / "prefabs", ec);
}

std::optional<std::string> CreateProject(const fs::path& root, const std::optional<fs::path>& contentFrom,
                                         bool topDown, const std::map<std::string, std::string>& ai, const std::string& buildMethod) {
    if (auto problem = MustBeEmptyOrMissing(root)) return problem;
    std::error_code ec;
    for (const char* folder : kFolders) {
        fs::create_directories(root / folder, ec);
        if (ec) return "couldn't create " + (root / folder).string() + ": " + ec.message();
    }
    if (contentFrom) {
        for (const char* folder : {"assets", "prefabs", "scripts"}) {
            if (fs::is_directory(*contentFrom / folder, ec)) {
                fs::copy(*contentFrom / folder, root / folder,
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
                if (ec) return "couldn't copy " + std::string(folder) + ": " + ec.message();
            }
        }
        // Generated art's history belongs to the project it was made in.
        fs::remove_all(root / "assets" / "textures" / "generated", ec);
    }
    std::string aiLine;
    for (const char* field : {"provider", "model", "art", "background", "paint"}) {
        if (const auto it = ai.find(field); it != ai.end() && !it->second.empty()) aiLine += std::string(" ") + field + "=\"" + it->second + "\"";
    }
    std::ofstream(root / "settings.aige") << "# AIGE Settings v1\n" << (topDown ? "Grid style=TopDown\n" : "")
                                          << (aiLine.empty() ? "" : "AI" + aiLine + "\n")
                                          << (buildMethod.empty() ? "" : "Places method=\"" + buildMethod + "\"\n");
    std::ofstream(root / "README.md")
        << "# " << root.filename().string() << "\n\n"
        << "An AIGE project. Open this folder in the editor (File > Open Project).\n\n"
        << "- scenes/main.aige: the level\n- prefabs/: placeable things\n- scripts/: Lua behavior\n"
        << "- assets/: textures and sounds\n- world/: the World Bible (canon)\n"
        << "- dialogue/, quests/, schedules/: story and routines\n";
    return std::nullopt;
}

std::optional<std::string> CopyProject(const fs::path& from, const fs::path& to) {
    if (!LooksLikeProject(from)) return from.string() + " isn't a project";
    if (auto problem = MustBeEmptyOrMissing(to)) return problem;
    std::error_code ec;
    fs::create_directories(to, ec);
    for (auto it = fs::recursive_directory_iterator(from, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path relative = fs::relative(it->path(), from, ec);
        if (Excluded(relative)) {
            if (it->is_directory()) it.disable_recursion_pending();
            continue;
        }
        const fs::path target = to / relative;
        if (it->is_directory()) {
            fs::create_directories(target, ec);
        } else {
            fs::create_directories(target.parent_path(), ec);
            fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
            if (ec) return "couldn't copy " + relative.string() + ": " + ec.message();
        }
    }
    return std::nullopt;
}

} // namespace aige
