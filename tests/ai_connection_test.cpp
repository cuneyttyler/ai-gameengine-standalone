#include <gtest/gtest.h>

#include "ai_bridge/AiConnection.h"
#include "ai_bridge/UserPrefs.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

// Counts launches instead of spawning the service. The client it returns is
// never started, so nothing runs.
struct CountingLauncher {
    int launches = 0;
    aige::AiConnection::LaunchFn Fn() {
        return [this]() {
            ++launches;
            return std::make_unique<aige::AiClient>(aige::AiClient::Options{});
        };
    }
};

fs::path TempPrefs(const std::string& name) {
    fs::path path = fs::temp_directory_path() / ("aige_prefs_" + name) / "editor.json";
    fs::remove_all(path.parent_path());
    return path;
}

} // namespace

// development_plan.md 2.7: nothing AI-related runs until the user says yes.
TEST(AiConnectionTest, NothingLaunchesBeforeTheUserChooses) {
    aige::UserPrefs prefs({});
    CountingLauncher launcher;
    aige::AiConnection connection(prefs, launcher.Fn());
    connection.Start();
    EXPECT_EQ(connection.Choice(), aige::AiChoice::Undecided);
    EXPECT_EQ(launcher.launches, 0);
    EXPECT_EQ(connection.Client(), nullptr);
}

TEST(AiConnectionTest, OffNeverLaunchesTheService) {
    aige::UserPrefs prefs({});
    prefs.Set("aiEnabled", false);
    CountingLauncher launcher;
    aige::AiConnection connection(prefs, launcher.Fn());
    connection.Start();
    connection.Start();
    EXPECT_EQ(connection.Choice(), aige::AiChoice::Off);
    EXPECT_EQ(launcher.launches, 0);
}

TEST(AiConnectionTest, TurningOnLaunchesOnceAndTurningOffStops) {
    aige::UserPrefs prefs({});
    CountingLauncher launcher;
    aige::AiConnection connection(prefs, launcher.Fn());
    connection.SetEnabled(true);
    connection.Start(); // already running: no second launch
    EXPECT_EQ(launcher.launches, 1);
    EXPECT_NE(connection.Client(), nullptr);

    connection.SetEnabled(false);
    EXPECT_EQ(connection.Client(), nullptr);
    EXPECT_EQ(connection.Choice(), aige::AiChoice::Off);
}

TEST(AiConnectionTest, ChoiceSurvivesARestart) {
    fs::path file = TempPrefs("restart");
    {
        aige::UserPrefs prefs(file);
        CountingLauncher launcher;
        aige::AiConnection connection(prefs, launcher.Fn());
        connection.SetEnabled(false);
    }
    aige::UserPrefs prefs(file);
    CountingLauncher launcher;
    aige::AiConnection connection(prefs, launcher.Fn());
    connection.Start();
    EXPECT_EQ(connection.Choice(), aige::AiChoice::Off);
    EXPECT_EQ(launcher.launches, 0);
    fs::remove_all(file.parent_path());
}

TEST(UserPrefsTest, CorruptFileFallsBackToDefaults) {
    fs::path file = TempPrefs("corrupt");
    fs::create_directories(file.parent_path());
    std::ofstream(file) << "{ not json";
    aige::UserPrefs prefs(file);
    EXPECT_FALSE(prefs.Has("aiEnabled"));
    EXPECT_EQ(prefs.Get("missing", 3), 3);
    prefs.Set("aiEnabled", true); // and it can be written again
    EXPECT_TRUE(aige::UserPrefs(file).Get("aiEnabled").get<bool>());
    fs::remove_all(file.parent_path());
}
