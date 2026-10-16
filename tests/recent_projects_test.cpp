#include <gtest/gtest.h>

#include "ai_bridge/RecentProjects.h"
#include "assets/ProjectTemplate.h"

#include <filesystem>

namespace fs = std::filesystem;

// development_plan.md 5.4: the welcome window's recent projects.
namespace {

struct Temp {
    Temp() {
        dir = fs::temp_directory_path() /
              ("aige_recent_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Temp() { fs::remove_all(dir); }
    std::string Project(const std::string& name) {
        const fs::path root = dir / name;
        aige::CreateProject(root);
        return root.string();
    }
    fs::path dir;
};

} // namespace

TEST(RecentProjectsTest, NewestFirstOnceEachAndCapped) {
    Temp t;
    aige::UserPrefs prefs(t.dir / "prefs.json");
    const std::string a = t.Project("a"), b = t.Project("b"), c = t.Project("c");
    aige::RememberProject(prefs, a, "2026-10-01T10:00:00Z");
    aige::RememberProject(prefs, b, "2026-10-02T10:00:00Z");
    aige::RememberProject(prefs, a, "2026-10-03T10:00:00Z"); // again: moves to the top, not twice
    auto list = aige::RecentProjects(prefs);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].path, a);
    EXPECT_EQ(list[0].opened, "2026-10-03T10:00:00Z");
    EXPECT_EQ(list[0].name, "a");
    EXPECT_EQ(list[1].path, b);
    EXPECT_EQ(list[1].opened, "2026-10-02T10:00:00Z");

    aige::RememberProject(prefs, c, "2026-10-04T10:00:00Z", 2); // at most two
    list = aige::RecentProjects(prefs);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].path, c);
    EXPECT_EQ(list[1].path, a);

    // It's saved: a fresh read sees the same.
    aige::UserPrefs reread(t.dir / "prefs.json");
    EXPECT_EQ(aige::RecentProjects(reread).size(), 2u);
}

TEST(RecentProjectsTest, MissingProjectsAreMarkedAndCanBeForgotten) {
    Temp t;
    aige::UserPrefs prefs(t.dir / "prefs.json");
    const std::string kept = t.Project("kept");
    const std::string gone = t.Project("gone");
    aige::RememberProject(prefs, kept, "x");
    aige::RememberProject(prefs, gone, "y");
    fs::remove_all(gone);
    auto list = aige::RecentProjects(prefs);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_FALSE(list[0].exists);
    EXPECT_TRUE(list[1].exists);

    aige::ForgetProject(prefs, gone);
    list = aige::RecentProjects(prefs);
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].path, kept);
    EXPECT_FALSE(prefs.Get("recentOpened").contains(gone));
}

TEST(RecentProjectsTest, TheSameFolderWrittenDifferentlyIsOneEntry) {
    Temp t;
    aige::UserPrefs prefs(t.dir / "prefs.json");
    const std::string a = t.Project("a");
    aige::RememberProject(prefs, a, "1");
    aige::RememberProject(prefs, a + "/", "2");
    aige::RememberProject(prefs, (fs::path(a) / ".." / "a").string(), "3");
    EXPECT_EQ(aige::RecentProjects(prefs).size(), 1u);
    EXPECT_TRUE(aige::SameProjectPath("C:/Games/My", "C:\\Games\\My\\"));
#ifdef _WIN32
    EXPECT_TRUE(aige::SameProjectPath("C:/Games/My", "c:/games/my"));
#endif
}
