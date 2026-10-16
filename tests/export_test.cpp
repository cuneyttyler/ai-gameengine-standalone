#include <gtest/gtest.h>

#include "assets/Export.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// development_plan.md 5.1: Build & Export (desktop).
namespace {

void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

fs::path TempDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / ("aige_export_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

} // namespace

TEST(ExportTest, OnlyRuntimeContentIsShipped) {
    for (const char* yes : {"settings.aige", "scenes/main.aige", "prefabs/a/b.prefab", "scripts/x.lua",
                            "assets/textures/a.png", "assets/textures/a.png.meta", "dialogue/t.json", "quests/q.json",
                            "schedules/s.json", "world/characters/x.json", "modules/lantern/module.lua"}) {
        EXPECT_TRUE(aige::IsRuntimeContent(yes)) << yes;
    }
    for (const char* no : {".cache/tex.bin", "ai_log.jsonl", "assets/textures/generated/a.png.gen.json",
                           "README.md", "notes.txt", "scenes/.backup.aige", ".git/config", "assets/../x", ""}) {
        EXPECT_FALSE(aige::IsRuntimeContent(no)) << no;
    }
    EXPECT_EQ(aige::SafeGameName("  My Game: Part 2! "), "My Game Part 2");
    EXPECT_EQ(aige::SafeGameName("../.."), "");
}

TEST(ExportTest, TheExportHasEverythingTheGameNeedsAndNothingElse) {
    const fs::path project = TempDir("project");
    Write(project / "settings.aige", "# settings\n");
    Write(project / "scenes/main.aige", "# AIGE Scene v1\n");
    Write(project / "assets/textures/hero.png", "png");
    Write(project / "assets/textures/hero.png.meta", "type=texture\n");
    Write(project / "assets/textures/generated/bat.png.gen.json", "{}");
    Write(project / "scripts/hero.lua", "-- lua");
    Write(project / ".cache/cooked.bin", "x");
    Write(project / "ai_log.jsonl", "{}");
    Write(project / "README.md", "# my game");
    const fs::path player = TempDir("player");
    Write(player / "sandbox.exe", "MZ fake");
    Write(player / "sandbox.pdb", "debug info");
    Write(player / "shaders/sprite.vert.spv", "spv");
    const fs::path out = TempDir("out");

    aige::ExportOptions options{project, player, "sandbox.exe", out, "Hero's Quest", true};
    auto result = aige::ExportGame(options);
    ASSERT_EQ(result.error, "");
    const fs::path game = out / "Heros Quest";
    EXPECT_EQ(result.folder, game);
    for (const char* file : {"Heros Quest.exe", "shaders/sprite.vert.spv", "game/settings.aige", "game/scenes/main.aige",
                             "game/assets/textures/hero.png", "game/assets/textures/hero.png.meta",
                             "game/scripts/hero.lua", "game/export.json"}) {
        EXPECT_TRUE(fs::exists(game / file)) << file;
    }
    for (const char* file : {"sandbox.exe", "sandbox.pdb", "game/.cache", "game/ai_log.jsonl", "game/README.md",
                             "game/assets/textures/generated/bat.png.gen.json"}) {
        EXPECT_FALSE(fs::exists(game / file)) << file;
    }
    EXPECT_NE(Read(game / "game/export.json").find("\"name\": \"Heros Quest\""), std::string::npos);
    EXPECT_EQ(result.files.size(), 8u);

    // The zip lists the same files under the game's folder name.
    ASSERT_TRUE(fs::exists(result.zip));
    const std::string zip = Read(result.zip);
    ASSERT_GE(zip.size(), 22u);
    const std::string end = zip.substr(zip.size() - 22);
    EXPECT_EQ(end.substr(0, 4), std::string("PK\x05\x06", 4));
    const int entries = static_cast<unsigned char>(end[10]) | (static_cast<unsigned char>(end[11]) << 8);
    EXPECT_EQ(entries, 8);
    EXPECT_NE(zip.find("Heros Quest/game/scenes/main.aige"), std::string::npos);
    EXPECT_EQ(zip.substr(0, 4), std::string("PK\x03\x04", 4));

    // Never over an earlier export.
    EXPECT_NE(aige::ExportGame(options).error, "");
    // Nothing to play, or no player: refused before anything is written.
    fs::remove(project / "scenes/main.aige");
    options.destination = TempDir("out2");
    EXPECT_NE(aige::ExportGame(options).error, "");
    EXPECT_TRUE(fs::is_empty(options.destination));
}

#ifdef _WIN32
TEST(ExportTest, TheExportedDemoRunsOnItsOwn) {
    // The real player and the committed demo, exported and run without a
    // window for two seconds of game time.
    const fs::path out = TempDir("smoke");
    aige::ExportOptions options{fs::path(AIGE_SOURCE_DIR) / "projects" / "demo", fs::path(AIGE_PLAYER_DIR),
                                "sandbox.exe", out, "Smoke Test", false};
    auto result = aige::ExportGame(options);
    ASSERT_EQ(result.error, "");

    const std::string command = "\"\"" + (result.folder / "Smoke Test.exe").string() + "\" --headless --exit-after 2 2>&1\"";
    FILE* pipe = _popen(command.c_str(), "r");
    ASSERT_NE(pipe, nullptr);
    std::string output;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) output += buffer;
    const int code = _pclose(pipe);
    EXPECT_EQ(code, 0) << output;
    EXPECT_NE(output.find("headless ok: 120 steps"), std::string::npos) << output;
    fs::remove_all(out);
}
#endif
