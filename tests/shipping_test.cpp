#include <gtest/gtest.h>

#include "assets/Export.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "ecs/Dormant.h"
#include "game/Accessibility.h"
#include "game/DebugConsole.h"
#include "game/Templates.h"
#include "gameplay/Achievements.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Components.h"
#include "physics2d/Components.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/GameSession.h"
#include "gameplay/InputMap.h"
#include "gameplay/Items.h"
#include "gameplay/Profiler.h"
#include "gameplay/Story.h"
#include "gameplay/Streaming.h"
#include "records/Records.h"
#include "scripting/Components.h"
#include "scripting/LuaVM.h"
#include "ui/Documents.h"

#include <filesystem>
#include <fstream>

// development_plan.md Phase 24: accessibility, the debug console, the profiler, achievements,
// export polish, streaming big scenes, template projects.
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path Temp(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("aige_shipping_" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

} // namespace

TEST(ShippingTest, TheProfilerTimesSectionsAndFrames) {
    auto& p = aige::Profiler::Get();
    p.Reset();
    for (int i = 0; i < 5; ++i) {
        p.BeginFrame();
        p.Add("physics", 2.0);
        p.Add("scripts", 1.0);
        { aige::Profiler::Scope s("ai"); }
    }
    p.BeginFrame();
    EXPECT_EQ(p.FrameHistory().size(), 5u);
    EXPECT_DOUBLE_EQ(p.LastFrame().at("physics"), 2.0);
    EXPECT_NEAR(p.Averages().at("scripts"), 1.0, 1e-9);
    const auto lines = p.Lines();
    ASSERT_GE(lines.size(), 4u);
    EXPECT_NE(lines[0].find("fps"), std::string::npos);
    EXPECT_NE(lines[1].find("physics"), std::string::npos); // the costliest first
    p.Reset();
}

TEST(ShippingTest, AchievementsUnlockByEventsQuestsAndFlags) {
    const fs::path dir = Temp("achievements");
    aige::AchievementSystem a;
    a.SetPlatform(std::make_shared<aige::LocalAchievements>(dir / "achievements.json"));
    // Defined here rather than from records.
    a.SetDefs({
        aige::AchievementFromJson("wolf-slayer", {{"name", "Wolf Slayer"}, {"kind", "event"}, {"event", "death"}, {"target", "Wolf"}, {"count", 3}}),
        aige::AchievementFromJson("first-steps", {{"name", "First Steps"}, {"kind", "quest"}, {"quest", "q-welcome"}}),
        aige::AchievementFromJson("secret", {{"name", "Secret"}, {"kind", "flag"}, {"flag", "found-it"}, {"hidden", true}})});
    a.OnEvent("death", {"Bat"});
    a.OnEvent("death", {"Wolf"});
    a.OnEvent("death", {"Wolf"});
    EXPECT_FALSE(a.Unlocked("wolf-slayer"));
    EXPECT_EQ(a.Progress("wolf-slayer"), 2);
    a.OnEvent("death", {"Wolf"});
    EXPECT_TRUE(a.Unlocked("wolf-slayer"));
    a.Check([](const std::string& q) { return q == "q-welcome" ? std::string("completed") : std::string("inactive"); },
            [](const std::string&) { return false; });
    EXPECT_TRUE(a.Unlocked("first-steps"));
    const auto fresh = a.TakeUnlocked();
    ASSERT_EQ(fresh.size(), 2u);
    EXPECT_TRUE(a.TakeUnlocked().empty());
    // A secret stays one until it's found.
    json view = a.View();
    EXPECT_EQ(view["achievements"][2]["name"], "???");
    EXPECT_EQ(view["earned"], 20);
    // Kept on disk: a new session remembers.
    aige::LocalAchievements again(dir / "achievements.json");
    EXPECT_TRUE(again.Unlocked("wolf-slayer"));
    EXPECT_FALSE(again.Unlocked("secret"));
    EXPECT_FALSE(aige::ValidateAchievement(aige::AchievementFromJson("x", {{"name", "X"}, {"kind", "event"}})).empty());
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ShippingTest, AccessibilityColoursAndTheReport) {
    EXPECT_NEAR(aige::ContrastRatio("#ffffff", "#000000"), 21.0, 0.01);
    EXPECT_NEAR(aige::ContrastRatio("#777777", "#777777"), 1.0, 0.01);
    // Red and green look alike to a deuteranope; the safe palette stays apart.
    const double normal = aige::ColorDistance("#d64545", "#4caf6a");
    const double deutan = aige::ColorDistance(aige::SimulateColorBlindness("#d64545", "deuteranopia"),
                                              aige::SimulateColorBlindness("#4caf6a", "deuteranopia"));
    EXPECT_LT(deutan, normal);
    aige::PlayerSettings player;
    player.highContrast = true;
    player.colorblind = "deuteranopia";
    player.textScale = 1.5f;
    const json tokens = aige::AccessibilityTokens(player);
    EXPECT_EQ(tokens["text"], "#ffffff");
    EXPECT_EQ(tokens["mana"], "#0072b2");
    EXPECT_EQ(tokens["font-size"], "26px");
    // The report on the engine's themes and default controls.
    const json report = aige::AccessibilityReport(aige::ProjectThemeTokens(Temp("theme")), aige::InputMap::Defaults());
    EXPECT_EQ(report["schema"], "aige.accessibility/1");
    for (const auto& c : report["checks"]) {
        if (c["id"] == "text-contrast" || c["id"] == "keyboard" || c["id"] == "text-size") EXPECT_TRUE(c["ok"].get<bool>()) << c.dump();
    }
    // A theme with grey text on grey fails.
    const json bad = aige::AccessibilityReport({{"text", "#777777"}, {"panel", "#666666"}, {"font-size", "11px"}}, aige::InputMap::Defaults());
    int failed = 0;
    for (const auto& c : bad["checks"]) failed += !c["ok"].get<bool>();
    EXPECT_GE(failed, 2);
}

TEST(ShippingTest, HoldToToggleLatchesHeldActions) {
    aige::InputMap map = aige::InputMap::Defaults();
    aige::InputAction block;
    block.name = "block";
    block.keys = {{aige::Key::Q}};
    map.actions.push_back(block);
    aige::InputState input;
    input.SetHoldToToggle(true);
    bool q = false;
    auto keys = [&](aige::Key k) { return k == aige::Key::Q && q; };
    q = true;
    input.Update(map, keys);
    EXPECT_TRUE(input.Down("block"));
    q = false;
    input.Update(map, keys);
    EXPECT_TRUE(input.Down("block")); // let go: still on
    q = true;
    input.Update(map, keys);
    q = false;
    input.Update(map, keys);
    EXPECT_FALSE(input.Down("block")); // a second press: off
    input.SetHoldToToggle(false);
    q = true;
    input.Update(map, keys);
    q = false;
    input.Update(map, keys);
    EXPECT_FALSE(input.Down("block"));
}

TEST(ShippingTest, TheDebugConsoleRunsCommandsAndLua) {
    aige::Registry registry;
    aige::LuaVM lua;
    const auto player = aige::CreatePlayer(registry, 2.5f, 2.5f);
    aige::GameSession session(registry, lua, aige::IsoParams{});
    aige::DebugConsole console(session, registry, lua);
    EXPECT_EQ(console.Execute("give potion 3"), "gave 3 potion");
    EXPECT_EQ(aige::CountItem(registry.get<aige::Inventory>(player), "potion"), 3);
    console.Execute("tp 10 4");
    EXPECT_FLOAT_EQ(registry.get<aige::WorldTransform2D>(player).x, 10.5f);
    console.Execute("flag met-elder");
    EXPECT_TRUE(session.Story().Flag("met-elder"));
    EXPECT_EQ(console.Execute("= 2 + 3"), "5.000000");
    EXPECT_NE(console.Execute("lua this is not lua").find("error"), std::string::npos);
    console.Execute("god");
    EXPECT_TRUE(console.God());
    console.Execute("speed 2");
    EXPECT_FLOAT_EQ(console.Speed(), 2.0f);
    EXPECT_NE(console.Execute("dance").find("unknown command"), std::string::npos);
    console.Execute("scene scenes/cellar.aige door");
    const auto travel = session.TakeTravel();
    ASSERT_TRUE(travel);
    EXPECT_EQ(travel->scene, "scenes/cellar.aige");
    // Typing: characters, backspace, history.
    for (char c : std::string("helq")) console.Type(static_cast<unsigned>(c));
    console.Backspace();
    console.Type('p');
    EXPECT_EQ(console.Input(), "help");
    console.Submit();
    console.HistoryUp();
    EXPECT_EQ(console.Input(), "help");
    EXPECT_EQ(console.Lines().back(), "> help_");
}

TEST(ShippingTest, StreamingPutsFarCharactersToSleep) {
    aige::Registry registry;
    auto nearby = registry.create();
    registry.emplace<aige::WorldTransform2D>(nearby, 10.0f, 10.0f);
    registry.emplace<aige::LuaScript>(nearby);
    auto distant = registry.create();
    registry.emplace<aige::WorldTransform2D>(distant, 400.0f, 10.0f);
    registry.emplace<aige::LuaScript>(distant);
    auto stats = aige::UpdateStreaming(registry, 12.0f, 12.0f, 32, 2);
    EXPECT_EQ(stats.awake, 1);
    EXPECT_EQ(stats.dormant, 1);
    EXPECT_TRUE(registry.all_of<aige::Dormant>(distant));
    EXPECT_FALSE(registry.all_of<aige::Dormant>(nearby));
    // The player walks over: it wakes.
    aige::UpdateStreaming(registry, 395.0f, 12.0f, 32, 2);
    EXPECT_FALSE(registry.all_of<aige::Dormant>(distant));
    EXPECT_TRUE(registry.all_of<aige::Dormant>(nearby));
    aige::WakeAll(registry);
    EXPECT_FALSE(registry.all_of<aige::Dormant>(nearby));
}

TEST(ShippingTest, EveryTemplateMakesAPlayableProject) {
    for (const auto& t : aige::GameTemplates()) {
        SCOPED_TRACE(t.id);
        const fs::path root = Temp("template_" + t.id);
        fs::remove_all(root);
        ASSERT_FALSE(aige::CreateProjectFromTemplate(root, t.id).has_value());
        const aige::Project project{root};
        aige::Registry registry;
        ASSERT_TRUE(aige::LoadScene(registry, project.Resolve("scenes/main.aige").string()));
        int talkers = 0;
        for (auto [e, talk] : registry.view<aige::Talk>().each()) ++talkers;
        EXPECT_GE(talkers, 1);
        aige::StoryLibrary story(root);
        story.Refresh();
        ASSERT_NE(story.FindQuest("q-welcome"), nullptr);
        EXPECT_TRUE(aige::ValidateQuest(*story.FindQuest("q-welcome"), [&](const std::string& q) { return story.FindQuest(q); }).empty());
        EXPECT_FALSE(story.Dialogues().empty());
        EXPECT_TRUE(fs::exists(root / "records" / "achievement" / "first-steps.json"));
        std::ifstream in(root / "game.json");
        EXPECT_EQ(json::parse(in)["genre"], t.id);
        // Its theme passes the text checks.
        const json report = aige::AccessibilityReport(aige::ProjectThemeTokens(root), aige::InputMap::Defaults());
        EXPECT_TRUE(report["checks"][0]["ok"].get<bool>()) << report["checks"][0].dump();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    EXPECT_TRUE(aige::CreateProjectFromTemplate(Temp("none"), "space-opera").has_value());
}

TEST(ShippingTest, TheExportCarriesItsVersionConsoleAndAccessibility) {
    const fs::path project = Temp("export_project");
    fs::create_directories(project / "scenes");
    std::ofstream(project / "scenes" / "riverford.aige") << "# AIGE Scene v1\n";
    std::ofstream(project / "settings.aige") << "# settings\n";
    const fs::path player = Temp("export_player");
    std::ofstream(player / "sandbox.exe") << "MZ fake";
    const fs::path out = Temp("export_out");
    aige::ExportOptions options{project, player, "sandbox.exe", out, "Moon Amulet", false};
    options.version = "1.2.0";
    options.debugConsole = false;
    options.startScene = "scenes/riverford.aige";
    options.accessibility = {{"schema", "aige.accessibility/1"}, {"passed", 9}, {"total", 10}};
    const auto result = aige::ExportGame(options);
    ASSERT_EQ(result.error, "");
    std::ifstream in(out / "Moon Amulet" / "game" / "export.json");
    const json info = json::parse(in);
    EXPECT_EQ(info["version"], "1.2.0");
    EXPECT_EQ(info["debugConsole"], false);
    EXPECT_EQ(info["startScene"], "scenes/riverford.aige");
    EXPECT_TRUE(fs::exists(out / "Moon Amulet" / "game" / "accessibility.json"));
}
