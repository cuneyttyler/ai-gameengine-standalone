#include <gtest/gtest.h>

#include "ai_bridge/Proposal.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "game/GameRunner.h"
#include "gameplay/Archetypes.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/InputMap.h"
#include "gameplay/Settings.h"
#include "scripting/LuaVM.h"
#include "ui/Documents.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// development_plan.md Phase 17: the game's UI as documents (RmlUi) — our font and
// renderer under it, the menus' and HUD's data bound as models, clicks routed to
// the game's own logic, themes by tokens, customised screens, notifications.
namespace {

struct UiProject {
    UiProject() {
        root = fs::temp_directory_path() /
               ("aige_gameui_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root);
        fs::create_directories(root / "ui" / "screens");
    }
    ~UiProject() { fs::remove_all(root); }
    void Write(const std::string& relative, const std::string& text) {
        fs::create_directories((root / relative).parent_path());
        std::ofstream(root / relative, std::ios::binary) << text;
    }
    fs::path root;
};

aige::MenuView PauseMenu() {
    aige::MenuView view;
    view.title = "Paused";
    view.items = {{"Resume", true}, {"Save game", true}, {"Load game", false}, {"Quit", true}};
    view.highlighted = 1;
    return view;
}

bool HasColour(const std::vector<aige::UiQuad>& quads, float r, float g, float b) {
    for (const auto& q : quads) {
        if (std::abs(q.r - r) < 0.02f && std::abs(q.g - g) < 0.02f && std::abs(q.b - b) < 0.02f && q.a > 0.5f) return true;
    }
    return false;
}

} // namespace

TEST(GameUiTest, ThemeTokensGoIntoStyles) {
    const auto tokens = nlohmann::json::parse(R"({"panel": "#112233", "size": 14})");
    EXPECT_EQ(aige::ApplyThemeTokens("a { color: var(--panel); font-size: var(--size)px; b: var(--missing); }", tokens),
              "a { color: #112233; font-size: 14px; b: var(--missing); }");
    EXPECT_EQ(aige::ApplyThemeTokens("no tokens", tokens), "no tokens");
}

TEST(GameUiTest, MeshesBecomeClippedQuads) {
    // A textured quad as RmlUi makes it: four corners, two triangles.
    std::vector<aige::UiMeshVertex> v = {{0, 0, 0, 0, 1, 1, 1, 1}, {10, 0, 1, 0, 1, 1, 1, 1},
                                         {10, 20, 1, 1, 1, 1, 1, 1}, {0, 20, 0, 1, 1, 1, 1, 1}};
    std::vector<aige::UiQuad> out;
    aige::MeshToQuads(v, {0, 1, 2, 0, 2, 3}, 5, 5, "atlas", nullptr, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_FLOAT_EQ(out[0].x0, 5);
    EXPECT_FLOAT_EQ(out[0].y1, 25);
    EXPECT_FLOAT_EQ(out[0].u1, 1);
    EXPECT_EQ(out[0].texture, "atlas");
    // Clipped to the right half: the texture coordinates follow.
    out.clear();
    const float clip[4] = {10, 0, 100, 100};
    aige::MeshToQuads(v, {0, 1, 2, 0, 2, 3}, 5, 5, "atlas", clip, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_FLOAT_EQ(out[0].x0, 10);
    EXPECT_FLOAT_EQ(out[0].u0, 0.5f);
    // Untextured boxes use the untextured coordinates; outside the clip nothing.
    out.clear();
    const float away[4] = {200, 200, 300, 300};
    aige::MeshToQuads(v, {0, 1, 2, 0, 2, 3}, 0, 0, "", away, out);
    EXPECT_TRUE(out.empty());
    aige::MeshToQuads(v, {0, 1, 2, 0, 2, 3}, 0, 0, "", nullptr, out);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_FLOAT_EQ(out[0].u0, -1.0f);
}

TEST(GameUiTest, AMenuDocumentShowsTheMenuAndRoutesClicks) {
    UiProject p;
    aige::DocumentUi ui(p.root);
    ASSERT_TRUE(ui.Ready());
    ui.SetViewport(1280, 720);
    ui.SetMenu(PauseMenu(), "pause");
    ui.Update(0.016);
    std::vector<aige::UiQuad> quads;
    ui.Render(quads);
    EXPECT_GT(quads.size(), 20u); // the panel and its text
    const auto items = ui.TextOf("item");
    ASSERT_EQ(items.size(), 4u);
    EXPECT_EQ(items[0], "Resume");
    EXPECT_EQ(items[3], "Quit");
    // The font atlas comes back as a texture to register.
    const auto textures = ui.TakeNewTextures();
    EXPECT_FALSE(textures.empty());
    EXPECT_GT(textures[0].image.width, 0u);
    // A click is handed to the game, which decides.
    int picked = -1;
    ui.onMenuChoose = [&](int index) { picked = index; };
    EXPECT_TRUE(ui.ClickMenuItem(3));
    EXPECT_EQ(picked, 3);
    // Hidden when nothing is on screen.
    ui.SetMenu(std::nullopt, "");
    EXPECT_TRUE(ui.TextOf("item").empty());
    for (const auto& problem : ui.TakeProblems()) ADD_FAILURE() << problem;
}

TEST(GameUiTest, TheThemeRestylesEverything) {
    UiProject p;
    {
        aige::DocumentUi ui(p.root);
        ui.SetViewport(1280, 720);
        EXPECT_FALSE(ui.ThemeTokens().empty()); // the default theme
        EXPECT_GE(ui.Themes().size(), 7u);
    }
    p.Write("ui/theme.json", R"({"base": "parchment", "tokens": {"panel": "#ff0000"}})");
    aige::DocumentUi ui(p.root);
    ui.SetViewport(1280, 720);
    EXPECT_EQ(ui.ThemeTokens().value("panel", ""), "#ff0000");
    EXPECT_EQ(ui.ThemeTokens().value("text", ""), "#2b1d0e"); // parchment's own
    ui.SetMenu(PauseMenu(), "pause");
    ui.Update(0.016);
    std::vector<aige::UiQuad> quads;
    ui.Render(quads);
    EXPECT_TRUE(HasColour(quads, 1.0f, 0.0f, 0.0f)); // the panel in the project's colour
}

TEST(GameUiTest, AProjectCustomisesAScreen) {
    UiProject p;
    p.Write("ui/screens.json", R"({"screens": [{"id": "pause", "document": "screens/pause.rml"}]})");
    p.Write("ui/screens/pause.rml", R"rml(<rml><head><link type="text/rcss" href="../styles/base.rcss"/></head>
        <body><div data-model="menu"><h1 class="custom">{{title}}!</h1>
        <div class="item" data-for="item, i : items" data-event-click="choose(i)">{{item.label}}</div></div></body></rml>)rml");
    aige::DocumentUi ui(p.root);
    ui.SetViewport(1280, 720);
    EXPECT_EQ(ui.SourceOf("pause"), "project");
    EXPECT_EQ(ui.SourceOf("hud"), "engine");
    ui.SetMenu(PauseMenu(), "pause");
    const auto title = ui.TextOf("custom");
    ASSERT_EQ(title.size(), 1u);
    EXPECT_EQ(title[0], "Paused!");
    // Other screens still use the engine's.
    ui.SetMenu(PauseMenu(), "settings");
    EXPECT_TRUE(ui.TextOf("custom").empty());
    EXPECT_EQ(ui.TextOf("item").size(), 4u);
}

TEST(GameUiTest, TheHudAndDialogueAreDocuments) {
    UiProject p;
    aige::DocumentUi ui(p.root);
    ui.SetViewport(1280, 720);
    aige::HudData hud;
    hud.health = 5;
    hud.healthMax = 10;
    hud.quest = "The hunt";
    hud.objective = "Defeat wolves (1/2)";
    hud.promptKey = "E";
    hud.promptText = "Talk to Tamsin";
    ui.SetHud(hud);
    ui.Update(0.016);
    std::vector<aige::UiQuad> quads;
    ui.Render(quads);
    EXPECT_FALSE(quads.empty());
    const auto prompt = ui.TextOf("prompt");
    ASSERT_EQ(prompt.size(), 1u);
    EXPECT_NE(prompt[0].find("Talk to Tamsin"), std::string::npos);

    aige::DialogueView talk;
    talk.speaker = "Tamsin";
    talk.text = "Wolves.";
    talk.choices = {"On it.", "Later."};
    ui.SetDialogue(talk);
    int chose = -1;
    ui.onDialogueChoose = [&](int index) { chose = index; };
    EXPECT_TRUE(ui.ClickDialogueChoice(1));
    EXPECT_EQ(chose, 1);
    ui.SetDialogue(std::nullopt);
    EXPECT_TRUE(ui.TextOf("choice").empty());
}

TEST(GameUiTest, NotificationsQueueCoalesceAndExpire) {
    UiProject p;
    aige::DocumentUi ui(p.root);
    ui.SetViewport(1280, 720);
    ui.Notify({"toast", "New quest: The hunt", "", 3.0});
    ui.Notify({"toast", "Journal updated", "", 3.0});
    ui.Notify({"feed", "+3 gold", "", 4.0, 3, "gold"});
    ui.Notify({"feed", "+4 gold", "", 4.0, 4, "gold"}); // merges
    EXPECT_EQ(ui.Live().size(), 3u);
    EXPECT_EQ(ui.Live()[2].count, 7);
    EXPECT_EQ(ui.History().size(), 3u);
    ui.SetHud({});
    // One toast at a time: the second waits for the first.
    auto toasts = ui.TextOf("toast");
    ASSERT_EQ(toasts.size(), 1u);
    EXPECT_NE(toasts[0].find("New quest"), std::string::npos);
    ui.Update(3.1);
    toasts = ui.TextOf("toast");
    ASSERT_EQ(toasts.size(), 1u);
    EXPECT_NE(toasts[0].find("Journal"), std::string::npos);
    ui.Update(5.0);
    EXPECT_TRUE(ui.Live().empty());
}

TEST(GameUiTest, ProjectScreensAndActions) {
    UiProject p;
    p.Write("ui/screens/map.rml", R"rml(<rml><head><link type="text/rcss" href="../styles/base.rcss"/></head>
        <body><div data-model="ui"><div class="close" data-event-click="action('close-map')">Close</div></div></body></rml>)rml");
    aige::DocumentUi ui(p.root);
    ui.SetViewport(1280, 720);
    std::string action;
    ui.onAction = [&](const std::string& a) { action = a; };
    ui.ShowScreen("map", true);
    EXPECT_TRUE(ui.IsShown("map"));
    EXPECT_EQ(ui.TextOf("close").size(), 1u);
    ui.ShowScreen("map", false);
    EXPECT_FALSE(ui.IsShown("map"));
    EXPECT_TRUE(ui.TextOf("close").empty());
}

TEST(GameUiTest, UiFilesAreChecked) {
    EXPECT_TRUE(aige::CheckUiFile("ui/theme.json", R"({"base": "iron", "tokens": {"panel": "#000"}})").empty());
    EXPECT_FALSE(aige::CheckUiFile("ui/theme.json", R"({"base": "neon"})").empty());
    EXPECT_FALSE(aige::CheckUiFile("scripts/x.rml", "<rml></rml>").empty());
    EXPECT_FALSE(aige::CheckUiFile("ui/screens.json", "{not json").empty());
    EXPECT_FALSE(aige::CheckUiFile("ui/notifications.json", R"({"rules": [{"event": "x", "channel": "popup"}]})").empty());
    EXPECT_TRUE(aige::CheckUiFile("ui/screens/a.rml", R"(<rml><body><div data-model="hud"></div></body></rml>)").empty());
    const auto onBody = aige::CheckUiFile("ui/screens/a.rml", R"(<rml><body data-model="menu"></body></rml>)");
    ASSERT_EQ(onBody.size(), 1u);
    EXPECT_NE(onBody[0].find("not on <body>"), std::string::npos);
    EXPECT_FALSE(aige::CheckUiFile("ui/screens/a.rml", R"(<rml><body><div data-model="player"></div></body></rml>)").empty());
}

TEST(GameUiTest, PromptsShowTheBoundKeyOrButton) {
    const auto map = aige::InputMap::Defaults();
    EXPECT_EQ(aige::ActionGlyph(map, "interact", false), "E");
    EXPECT_EQ(aige::ActionGlyph(map, "interact", true), "A");
    EXPECT_EQ(aige::ActionGlyph(map, "no-such-action", false), "");
    aige::InputState input;
    input.Update(map, [](aige::Key k) { return k == aige::Key::PadA; });
    EXPECT_TRUE(input.UsingPad());
    input.Update(map, [](aige::Key k) { return k == aige::Key::E; });
    EXPECT_FALSE(input.UsingPad());
}

TEST(GameUiTest, TheAiWritesUiFiles) {
    UiProject p;
    aige::Registry registry;
    aige::Project project{p.root};
    aige::PrefabLibrary prefabs(project);
    nlohmann::json ops = nlohmann::json::parse(R"([
        {"op": "write_ui", "path": "ui/theme.json", "content": "{\"base\": \"arcane\"}"},
        {"op": "write_ui", "path": "ui/screens/map.rml", "content": "<rml><body><div data-model=\"ui\">Map</div></body></rml>"}
    ])");
    auto resolved = aige::ResolveProposal({{"summary", "x"}, {"operations", ops}}, registry, aige::EngineReflection(), prefabs,
                                          project);
    ASSERT_EQ(resolved.items.size(), 2u);
    EXPECT_TRUE(resolved.items[0].error.empty()) << resolved.items[0].error;
    EXPECT_EQ(resolved.items[1].target, "UI · ui/screens/map.rml");
    auto outcome = aige::ApplyOperations(ops.get<std::vector<nlohmann::json>>(), registry, aige::EngineReflection(), prefabs,
                                         project);
    ASSERT_TRUE(outcome.errors.empty()) << outcome.errors[0];
    EXPECT_TRUE(fs::exists(p.root / "ui" / "screens" / "map.rml"));
    aige::DocumentUi ui(p.root);
    EXPECT_EQ(ui.ThemeTokens().value("accent", ""), "#a06cff"); // arcane's
    nlohmann::json bad = nlohmann::json::parse(R"([{"op": "write_ui", "path": "ui/theme.json", "content": "{\"base\": \"neon\"}"}])");
    resolved = aige::ResolveProposal({{"summary", "x"}, {"operations", bad}}, registry, aige::EngineReflection(), prefabs, project);
    EXPECT_NE(resolved.items[0].error.find("no built-in theme 'neon'"), std::string::npos);
}

// The game with documents: the title screen is a document; a click on "New game"
// goes through GameFlow like the keys; the HUD shows the player's health.
TEST(GameUiTest, TheGamePlaysWithDocuments) {
    UiProject p;
    {
        aige::Registry scene;
        aige::CreatePlayer(scene, 1.5f, 1.5f);
        fs::create_directories(p.root / "scenes");
        aige::SaveScene(scene, (p.root / "scenes/main.aige").string());
    }
    const auto saved = aige::ActiveSettings();
    aige::ActiveSettings().game.startScene = "scenes/main.aige";
    std::ofstream(p.root / "game.json") << R"({"title": "Doc Test", "titleScreen": true, "autosave": false})";
    aige::Project project{p.root};
    aige::PrefabLibrary prefabs(project);
    aige::LuaVM lua;
    aige::Registry registry;
    aige::GameRunnerOptions options;
    options.saveDir = p.root / "saves";
    aige::GameRunner game(project, registry, lua, prefabs, options);
    game.EnableDocuments({});
    ASSERT_NE(game.Documents(), nullptr);
    game.Start();
    ASSERT_EQ(game.Flow().Current(), aige::GameFlow::State::Title);
    std::vector<aige::UiQuad> quads;
    game.BuildUi(quads, 1280, 720);
    EXPECT_FALSE(quads.empty());
    const auto items = game.Documents()->TextOf("item");
    ASSERT_GE(items.size(), 2u);
    EXPECT_EQ(items[1], "New game");
    EXPECT_FALSE(game.TakeUiTextures().empty()); // the font for the renderer
    // Clicking "New game" asks for the difficulty (the built-in five), then starts it.
    EXPECT_TRUE(game.Documents()->ClickMenuItem(1));
    ASSERT_EQ(game.Flow().Current(), aige::GameFlow::State::Difficulty);
    game.BuildUi(quads, 1280, 720);
    EXPECT_EQ(game.Documents()->TextOf("item")[2], "Normal");
    EXPECT_TRUE(game.Documents()->ClickMenuItem(2));
    EXPECT_TRUE(game.Flow().InPlay());
    EXPECT_EQ(game.Session().DifficultyLevelId(), "normal");
    quads.clear();
    game.BuildUi(quads, 1280, 720);
    EXPECT_TRUE(game.Documents()->TextOf("item").empty()); // the menu is gone
    // Notices reach the documents' notifications.
    game.Session().Toast("Game saved");
    ASSERT_FALSE(game.Documents()->Live().empty());
    EXPECT_EQ(game.Documents()->Live().back().text, "Game saved");
    aige::ActiveSettings() = saved;
}
