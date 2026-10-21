#include "assets/Export.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <set>

namespace aige {

namespace fs = std::filesystem;

namespace {

bool EndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

uint32_t Crc32(const std::string& data) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char byte : data) crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

void Put16(std::string& out, uint32_t v) {
    out += static_cast<char>(v & 0xFF);
    out += static_cast<char>((v >> 8) & 0xFF);
}
void Put32(std::string& out, uint32_t v) {
    Put16(out, v & 0xFFFF);
    Put16(out, v >> 16);
}

std::string Utf8(const std::u8string& text) { return std::string(text.begin(), text.end()); }

std::string ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

} // namespace

bool IsRuntimeContent(const std::string& relative) {
    static const std::set<std::string> folders = {"scenes", "prefabs", "scripts", "assets", "dialogue",
                                                  "quests", "schedules", "world",   "modules", "records",
                                                  "loc",    "ui",       "cutscenes"};
    if (relative.empty() || relative.find("..") != std::string::npos) return false;
    // Hidden files and folders anywhere (.cache, .git, ...).
    if (relative.front() == '.' || relative.find("/.") != std::string::npos) return false;
    if (EndsWith(relative, ".gen.json")) return false;
    const auto slash = relative.find('/');
    if (slash == std::string::npos) {
        return relative == "settings.aige" || relative == "input.json" || relative == "game.json";
    }
    return folders.contains(relative.substr(0, slash));
}

std::string SafeGameName(const std::string& name) {
    std::string out;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == ' ' || c == '-' || c == '_' || u >= 0x80) out += c;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out.substr(0, 64);
}

ExportResult ExportGame(const ExportOptions& options) {
    ExportResult result;
    const std::string name = SafeGameName(options.name);
    if (name.empty()) {
        result.error = "the game needs a name (letters, digits, spaces, - or _)";
        return result;
    }
    std::error_code ec;
    bool anyScene = false;
    for (const auto& entry : fs::directory_iterator(options.project / "scenes", ec)) {
        anyScene |= entry.path().extension() == ".aige";
    }
    if (!anyScene) {
        result.error = "the project has no scene to play";
        return result;
    }
    const fs::path playerExe = options.playerDir / options.playerExe;
    if (!fs::is_regular_file(playerExe, ec)) {
        result.error = "no built player at " + playerExe.string();
        return result;
    }
    result.folder = options.destination / fs::u8path(name);
    if (fs::exists(result.folder, ec)) {
        result.error = result.folder.string() + " already exists; choose another place or name";
        return result;
    }
    fs::create_directories(result.folder / "game", ec);
    if (ec) {
        result.error = "couldn't create " + result.folder.string() + ": " + ec.message();
        return result;
    }

    auto copy = [&](const fs::path& from, const std::string& relative) {
        const fs::path to = result.folder / fs::u8path(relative);
        fs::create_directories(to.parent_path(), ec);
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
        if (ec && result.error.empty()) result.error = "couldn't copy " + from.string() + ": " + ec.message();
        result.files.push_back(relative);
    };

    // The credits (7.9), where a player sees them.
    if (fs::is_regular_file(options.project / "CREDITS.md", ec)) copy(options.project / "CREDITS.md", "CREDITS.md");
    // The player and what it needs (shaders), renamed after the game.
    copy(playerExe, name + ".exe");
    for (auto it = fs::recursive_directory_iterator(options.playerDir / "shaders", ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file()) {
            copy(it->path(), "shaders/" + fs::relative(it->path(), options.playerDir / "shaders").generic_string());
        }
    }
    // The game.
    for (auto it = fs::recursive_directory_iterator(options.project, ec); it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) break;
        const std::string relative = fs::relative(it->path(), options.project).generic_string();
        if (it->is_directory()) {
            const std::string folder = it->path().filename().string();
            if (!folder.empty() && folder.front() == '.') it.disable_recursion_pending();
            continue;
        }
        if (IsRuntimeContent(relative)) copy(it->path(), "game/" + relative);
    }
    {
        std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char date[32];
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &utc);
        std::ofstream(result.folder / "game" / "export.json")
            << nlohmann::json{{"name", name}, {"exported", date}, {"engine", "AIGE"}}.dump(2);
        result.files.push_back("game/export.json");
    }
    if (!result.error.empty()) return result;

    if (options.zip) {
        result.zip = options.destination / fs::u8path(name + ".zip");
        if (fs::exists(result.zip, ec)) {
            result.error = result.zip.string() + " already exists";
        } else if (!WriteZip(result.folder, result.zip)) {
            result.error = "couldn't write " + result.zip.string();
        }
    }
    return result;
}

bool WriteZip(const fs::path& folder, const fs::path& zipPath) {
    std::ofstream out(zipPath, std::ios::binary);
    if (!out) return false;
    std::string central;
    uint32_t offset = 0;
    uint16_t count = 0;
    std::error_code ec;
    const std::string top = Utf8(folder.filename().generic_u8string());
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(folder, ec); it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) return false;
        if (it->is_regular_file()) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        const std::string data = ReadAll(file);
        const std::string name = top + "/" + Utf8(fs::relative(file, folder).generic_u8string());
        const uint32_t crc = Crc32(data);
        const auto size = static_cast<uint32_t>(data.size());
        std::string local;
        Put32(local, 0x04034b50);
        Put16(local, 20);      // version needed
        Put16(local, 0x0800);  // UTF-8 names
        Put16(local, 0);       // stored
        Put16(local, 0);       // time
        Put16(local, 0x21);    // date: 1980-01-01
        Put32(local, crc);
        Put32(local, size);
        Put32(local, size);
        Put16(local, static_cast<uint32_t>(name.size()));
        Put16(local, 0);
        local += name;
        out.write(local.data(), static_cast<std::streamsize>(local.size()));
        out.write(data.data(), static_cast<std::streamsize>(data.size()));

        Put32(central, 0x02014b50);
        Put16(central, 20);
        Put16(central, 20);
        Put16(central, 0x0800);
        Put16(central, 0);
        Put16(central, 0);
        Put16(central, 0x21);
        Put32(central, crc);
        Put32(central, size);
        Put32(central, size);
        Put16(central, static_cast<uint32_t>(name.size()));
        Put16(central, 0);
        Put16(central, 0);
        Put16(central, 0);
        Put16(central, 0);
        Put32(central, 0);
        Put32(central, offset);
        central += name;
        offset += static_cast<uint32_t>(local.size()) + size;
        ++count;
    }
    std::string end;
    Put32(end, 0x06054b50);
    Put16(end, 0);
    Put16(end, 0);
    Put16(end, count);
    Put16(end, count);
    Put32(end, static_cast<uint32_t>(central.size()));
    Put32(end, offset);
    Put16(end, 0);
    out.write(central.data(), static_cast<std::streamsize>(central.size()));
    out.write(end.data(), static_cast<std::streamsize>(end.size()));
    return static_cast<bool>(out);
}

} // namespace aige
