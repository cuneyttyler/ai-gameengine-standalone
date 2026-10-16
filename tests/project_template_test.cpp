#include <gtest/gtest.h>

#include "assets/ProjectTemplate.h"

#include <filesystem>
#include <fstream>

#ifndef AIGE_SOURCE_DIR
#define AIGE_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;

namespace {

fs::path Scratch(const std::string& name) {
    fs::path path = fs::temp_directory_path() / ("aige_template_" + name);
    fs::remove_all(path);
    return path;
}

} // namespace

// development_plan.md 4.2.
TEST(ProjectTemplateTest, ANewProjectHasEveryFolderAndOptionallyAStarterPalette) {
    fs::path empty = Scratch("empty");
    ASSERT_FALSE(aige::CreateProject(empty));
    for (const char* folder : {"scenes", "prefabs", "scripts", "assets", "world", "dialogue", "quests", "schedules"}) {
        EXPECT_TRUE(fs::is_directory(empty / folder)) << folder;
    }
    EXPECT_TRUE(fs::exists(empty / "settings.aige"));
    EXPECT_TRUE(fs::exists(empty / "README.md"));
    EXPECT_TRUE(aige::LooksLikeProject(empty));
    EXPECT_TRUE(fs::is_empty(empty / "prefabs"));

    fs::path starter = Scratch("starter");
    const fs::path demo = fs::path(AIGE_SOURCE_DIR) / "projects" / "demo";
    ASSERT_FALSE(aige::CreateProject(starter, demo));
    EXPECT_TRUE(fs::exists(starter / "prefabs" / "skeleton.prefab"));
    EXPECT_TRUE(fs::exists(starter / "assets" / "textures" / "skeleton.png"));
    EXPECT_TRUE(fs::exists(starter / "scripts" / "enemy_melee.lua"));
    EXPECT_FALSE(fs::exists(starter / "scenes" / "main.aige")); // its own scene, built on first open
    EXPECT_TRUE(fs::is_empty(starter / "world"));               // and its own canon
    EXPECT_FALSE(fs::exists(starter / "assets" / "textures" / "generated"));

    // Never over an existing project.
    EXPECT_TRUE(aige::CreateProject(starter));
    fs::remove_all(empty);
    fs::remove_all(starter);
}

TEST(ProjectTemplateTest, SaveAsCopiesTheProjectButNotItsCacheOrLog) {
    fs::path from = Scratch("from");
    ASSERT_FALSE(aige::CreateProject(from));
    std::ofstream(from / "scenes" / "main.aige") << "# AIGE Scene v1\n";
    std::ofstream(from / "world" / "lore.md") << "x";
    fs::create_directories(from / ".cache");
    std::ofstream(from / ".cache" / "world_index.sqlite") << "x";
    std::ofstream(from / "ai_log.jsonl") << "{}";

    fs::path to = Scratch("to");
    ASSERT_FALSE(aige::CopyProject(from, to));
    EXPECT_TRUE(fs::exists(to / "scenes" / "main.aige"));
    EXPECT_TRUE(fs::exists(to / "world" / "lore.md"));
    EXPECT_TRUE(fs::exists(to / "settings.aige"));
    EXPECT_FALSE(fs::exists(to / ".cache"));
    EXPECT_FALSE(fs::exists(to / "ai_log.jsonl"));

    EXPECT_TRUE(aige::CopyProject(from, to));                 // not over an existing one
    EXPECT_TRUE(aige::CopyProject(Scratch("nothing"), to / "x")); // not from a non-project
    fs::remove_all(from);
    fs::remove_all(to);
}
