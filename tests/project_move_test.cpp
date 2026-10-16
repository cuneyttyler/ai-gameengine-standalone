#include <gtest/gtest.h>

#include "assets/Prefab.h"
#include "assets/ProjectMove.h"
#include "assets/SceneIO.h"
#include "editing/EditHistory.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "renderer2d/Components.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// development_plan.md 4.4: moving and renaming follows every reference.
namespace {

std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

struct TempProject {
    TempProject() {
        project.root = fs::temp_directory_path() /
                       ("aige_move_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(project.root);
        fs::create_directories(project.root);
    }
    ~TempProject() { fs::remove_all(project.root); }
    aige::Project project;
};

} // namespace

TEST(ProjectMoveTest, SceneTextRewritesWholeValuesOnly) {
    const std::string text =
        "entity\n"
        "  PrefabInstance path=prefabs/skeleton.prefab overrides=\"\"\n"
        "  Sprite texture=\"assets/my art/a.png\" width=32\n"
        "  Other path=prefabs/skeleton.prefab2 note=prefabs/skeleton.prefab\n";
    int count = 0;
    std::string out = aige::RewriteSceneText(text, "prefabs/skeleton.prefab", "prefabs/enemies/skeleton.prefab", &count);
    EXPECT_EQ(count, 2);
    EXPECT_NE(out.find("path=prefabs/enemies/skeleton.prefab overrides=\"\""), std::string::npos);
    EXPECT_NE(out.find("path=prefabs/skeleton.prefab2"), std::string::npos); // a longer name is another file
    EXPECT_NE(out.find("note=prefabs/enemies/skeleton.prefab\n"), std::string::npos);

    // Quoted on the way in, quoted on the way out when the new name has a space.
    out = aige::RewriteSceneText(text, "assets/my art/a.png", "assets/art/b c.png", &count);
    EXPECT_NE(out.find("texture=\"assets/art/b c.png\" width=32"), std::string::npos);
    out = aige::RewriteSceneText(out, "assets/art/b c.png", "assets/art/b.png");
    EXPECT_NE(out.find("texture=assets/art/b.png width=32"), std::string::npos);
}

TEST(ProjectMoveTest, QuotedTextRewritesStringLiterals) {
    int count = 0;
    std::string json = aige::RewriteQuotedText(R"({"target": "prefabs/potion.prefab", "x": "prefabs/potion.prefabs"})",
                                               "prefabs/potion.prefab", "prefabs/items/potion.prefab", false, &count);
    EXPECT_EQ(count, 1);
    EXPECT_NE(json.find(R"("target": "prefabs/items/potion.prefab")"), std::string::npos);
    std::string lua = aige::RewriteQuotedText("scene.spawn('prefabs/potion.prefab', 1, 2)\nlocal p = \"prefabs/potion.prefab\"",
                                              "prefabs/potion.prefab", "prefabs/items/potion.prefab", true);
    EXPECT_EQ(lua, "scene.spawn('prefabs/items/potion.prefab', 1, 2)\nlocal p = \"prefabs/items/potion.prefab\"");
}

TEST(ProjectMoveTest, MovingAPrefabUpdatesScenesStoryScriptsAndOtherPrefabs) {
    TempProject t;
    const fs::path& root = t.project.root;
    Write(root / "prefabs/potion.prefab", "# AIGE Scene v1\n\nentity\n  Name value=Potion\n");
    Write(root / "prefabs/chest.prefab", "# AIGE Scene v1\n\nentity\n  Spawner prefab=prefabs/potion.prefab\n");
    Write(root / "scenes/main.aige", "# AIGE Scene v1\n\nentity\n  PrefabInstance path=prefabs/potion.prefab overrides=\"\"\n");
    Write(root / "quests/q.json", R"({"objectives": [{"target": "prefabs/potion.prefab"}]})");
    Write(root / "scripts/drop.lua", "scene.spawn(\"prefabs/potion.prefab\", x, y)\n");
    Write(root / ".cache/ai_conversation.json", R"({"text": "prefabs/potion.prefab"})");

    auto result = aige::MoveProjectFile(t.project, "prefabs/potion.prefab", "prefabs/items/healing.prefab");
    ASSERT_EQ(result.error, "");
    EXPECT_EQ(result.references, 4);
    EXPECT_EQ(result.rewritten.size(), 4u);
    EXPECT_FALSE(fs::exists(root / "prefabs/potion.prefab"));
    EXPECT_TRUE(fs::exists(root / "prefabs/items/healing.prefab"));
    for (const char* file : {"prefabs/chest.prefab", "scenes/main.aige", "quests/q.json", "scripts/drop.lua"}) {
        EXPECT_NE(Read(root / file).find("prefabs/items/healing.prefab"), std::string::npos) << file;
        EXPECT_EQ(Read(root / file).find("prefabs/potion.prefab"), std::string::npos) << file;
    }
    // .cache is derived: not touched.
    EXPECT_NE(Read(root / ".cache/ai_conversation.json").find("prefabs/potion.prefab"), std::string::npos);

    // The library finds it in its folder.
    aige::PrefabLibrary prefabs(t.project);
    prefabs.Refresh();
    EXPECT_NE(prefabs.Find("prefabs/items/healing.prefab"), nullptr);
}

TEST(ProjectMoveTest, MovingAnAssetTakesItsMetaAndGenerationAlong) {
    TempProject t;
    const fs::path& root = t.project.root;
    Write(root / "assets/textures/crate.png", "png");
    Write(root / "assets/textures/crate.png.meta", "# AIGE Asset v1\ntype=texture\n");
    Write(root / "assets/textures/crate.png.gen.json", R"({"description": "a crate"})");
    Write(root / "prefabs/crate.prefab", "# AIGE Scene v1\n\nentity\n  Sprite texture=assets/textures/crate.png width=32\n");

    auto result = aige::MoveProjectFile(t.project, "assets/textures/crate.png", "assets/props/wooden crate.png");
    ASSERT_EQ(result.error, "");
    for (const char* file : {"assets/props/wooden crate.png", "assets/props/wooden crate.png.meta",
                             "assets/props/wooden crate.png.gen.json"}) {
        EXPECT_TRUE(fs::exists(root / file)) << file;
    }
    EXPECT_FALSE(fs::exists(root / "assets/textures/crate.png.meta"));
    EXPECT_NE(Read(root / "prefabs/crate.prefab").find("texture=\"assets/props/wooden crate.png\" width=32"),
              std::string::npos);
}

TEST(ProjectMoveTest, RefusesMovesThatWouldBreakTheProject) {
    TempProject t;
    Write(t.project.root / "prefabs/a.prefab", "x");
    Write(t.project.root / "prefabs/b.prefab", "x");
    Write(t.project.root / "assets/a.png", "x");
    EXPECT_NE(aige::CheckMove(t.project, "prefabs/a.prefab", "prefabs/b.prefab"), "");  // exists
    EXPECT_NE(aige::CheckMove(t.project, "prefabs/a.prefab", "prefabs/a.txt"), "");     // type change
    EXPECT_NE(aige::CheckMove(t.project, "prefabs/a.prefab", "assets/a.prefab"), "");   // out of prefabs/
    EXPECT_NE(aige::CheckMove(t.project, "prefabs/a.prefab", "prefabs/../a.prefab"), "");
    EXPECT_NE(aige::CheckMove(t.project, "scenes/main.aige", "prefabs/main.aige"), ""); // scenes stay in scenes/
    EXPECT_NE(aige::CheckMove(t.project, "prefabs/missing.prefab", "prefabs/x.prefab"), "");
    EXPECT_NE(aige::CheckMove(t.project, "assets/a.png", "assets/.png"), "");           // empty name
    EXPECT_EQ(aige::CheckMove(t.project, "assets/a.png", "assets/sub/a.png"), "");
    EXPECT_TRUE(fs::exists(t.project.root / "prefabs/a.prefab")); // checks touch nothing
}

TEST(ProjectMoveTest, TheOpenSceneAndItsUndoHistoryFollowWithIdsKept) {
    aige::Registry registry;
    aige::Entity crate = aige::CreateProp(registry, 1.5f, 1.5f, "assets/textures/crate.png", 32.0f, 32.0f);
    aige::EditHistory history;
    history.Record(registry);
    registry.get<aige::WorldTransform2D>(crate).x = 4.5f;

    auto rewrite = [](const std::string& text) {
        return aige::RewriteSceneText(text, "assets/textures/crate.png", "assets/props/crate.png");
    };
    aige::RewriteScene(registry, rewrite);
    history.RewriteSnapshots(rewrite);
    ASSERT_TRUE(registry.valid(crate)); // same id: selection and references hold
    EXPECT_EQ(registry.get<aige::Sprite>(crate).texture, "assets/props/crate.png");
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(crate).x, 4.5f);

    ASSERT_TRUE(history.Undo(registry));
    for (auto [entity, sprite] : registry.view<aige::Sprite>().each()) {
        EXPECT_EQ(sprite.texture, "assets/props/crate.png"); // undo doesn't bring the old path back
    }
}
