#include <gtest/gtest.h>

#include "audio/Audio.h"
#include "game/GameAudio.h"
#include "game/GameFlow.h"
#include "game/PlayerSettings.h"
#include "gameplay/Archetypes.h"
#include "gameplay/CameraFollow.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/Localization.h"
#include "physics2d/Components.h"
#include "scripting/LuaVM.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// development_plan.md 12.6 (audio) and 12.7 (camera, localization, player settings).

TEST(LocalizationTest, TablesTranslateByKeyAndFallBackToTheSource) {
    const fs::path root = fs::temp_directory_path() / "aige_loc_test";
    fs::remove_all(root);
    fs::create_directories(root / "loc");
    fs::create_directories(root / "dialogue");
    fs::create_directories(root / "quests");
    std::ofstream(root / "loc" / "fr.json") << R"({"menu.new_game": "Nouvelle partie", "greet.name": "Bonjour, {name} !"})";
    std::ofstream(root / "loc" / "de.json") << R"({})";
    std::ofstream(root / "dialogue" / "tamsin.json")
        << R"({"speaker": "x", "start": "hi", "nodes": [{"id": "hi", "text": "Hello.", "choices": [{"text": "Bye", "next": ""}]}]})";
    std::ofstream(root / "quests" / "q.json")
        << R"({"name": "Quiet", "objectives": [{"id": "kill", "text": "Defeat them", "type": "defeat", "target": "Enemy"}]})";

    aige::Localization loc;
    loc.Load(root, "fr");
    EXPECT_EQ(loc.Available(), (std::vector<std::string>{"de", "fr"}));
    EXPECT_EQ(loc.Text("menu.new_game", "New game"), "Nouvelle partie");
    EXPECT_EQ(loc.Text("menu.quit", "Quit"), "Quit");
    EXPECT_EQ(loc.Format("greet.name", "Hello, {name}!", {{"name", "Tamsin"}}), "Bonjour, Tamsin !");
    loc.Load(root, "");
    EXPECT_EQ(loc.Text("menu.new_game", "New game"), "New game");

    auto source = aige::Localization::CollectSource(root);
    EXPECT_EQ(source["dialogue/tamsin/hi"], "Hello.");
    EXPECT_EQ(source["dialogue/tamsin/hi/1"], "Bye");
    EXPECT_EQ(source["quest/q/name"], "Quiet");
    EXPECT_EQ(source["quest/q/kill"], "Defeat them");
    fs::remove_all(root);
}

TEST(PlayerSettingsTest, RoundTripsAndClamps) {
    const fs::path file = fs::temp_directory_path() / "aige_player_settings" / "settings.json";
    fs::remove_all(file.parent_path());
    aige::PlayerSettings s;
    s.music = 0.3f;
    s.language = "fr";
    s.fullscreen = true;
    ASSERT_TRUE(aige::SavePlayerSettings(file, s));
    auto back = aige::LoadPlayerSettings(file);
    EXPECT_FLOAT_EQ(back.music, 0.3f);
    EXPECT_EQ(back.language, "fr");
    EXPECT_TRUE(back.fullscreen);
    auto clamped = aige::PlayerSettings::FromJson(nlohmann::json::parse(R"({"volume": {"master": 7}})"));
    EXPECT_FLOAT_EQ(clamped.master, 1.0f);
    fs::remove_all(file.parent_path());
}

TEST(PlayerSettingsTest, TheSettingsScreenStepsValues) {
    aige::PlayerSettings s;
    s.music = 0.8f;
    aige::GameFlow flow;
    flow.SetPlayerSettings(&s, {"fr"});
    flow.Begin();
    // Title: Continue, New game, Load, Settings, Credits, Quit.
    flow.Choose(3);
    ASSERT_EQ(flow.Current(), aige::GameFlow::State::Settings);
    EXPECT_EQ(flow.Choose(1).kind, aige::GameFlow::Command::Kind::SettingsChanged); // music
    EXPECT_FLOAT_EQ(s.music, 0.9f);
    flow.Choose(1);
    flow.Choose(1);
    EXPECT_FLOAT_EQ(s.music, 0.0f); // past full, back to silence
    flow.Choose(5);                 // language
    EXPECT_EQ(s.language, "fr");
    flow.Choose(5);
    EXPECT_EQ(s.language, "");
}

TEST(AudioTest, BusesPlacementAndMusicFades) {
    aige::AudioEngine audio(true); // no device: mixes silently, volumes readable
    ASSERT_TRUE(audio.IsAvailable());
    EXPECT_EQ(aige::AudioBusFromName("music"), aige::AudioBus::Music);
    audio.SetBusVolume(aige::AudioBus::Music, 1.7f);
    EXPECT_FLOAT_EQ(audio.BusVolume(aige::AudioBus::Music), 1.0f);

    audio.SetListener(10.0f, 10.0f, 16.0f);
    auto [nearVolume, nearPan] = audio.Placement(10.5f, 10.0f);
    auto [farVolume, farPan] = audio.Placement(22.0f, 10.0f);
    auto [goneVolume, gonePan] = audio.Placement(40.0f, 10.0f);
    EXPECT_FLOAT_EQ(nearVolume, 1.0f);
    EXPECT_LT(farVolume, nearVolume);
    EXPECT_GT(farPan, 0.5f); // to the right
    EXPECT_EQ(goneVolume, 0.0f);

    // A track that doesn't exist plays nothing but is remembered; "" fades out.
    audio.PlayMusic("nowhere.ogg", 1.0f);
    EXPECT_EQ(audio.CurrentMusic(), "nowhere.ogg");
    audio.PlayMusic("", 0.5f);
    EXPECT_EQ(audio.CurrentMusic(), "");
    audio.Update(0.1f);
}

TEST(AudioTest, TheGameDirectsMusicFromTheSceneCombatAndScripts) {
    aige::LuaVM lua;
    aige::Registry registry;
    auto player = aige::CreatePlayer(registry, 2.5f, 2.5f);
    auto musicEntity = registry.create();
    registry.emplace<aige::Music>(musicEntity, "assets/music/town.ogg", "assets/amb/wind.ogg", "assets/music/fight.ogg");
    aige::GameSession session(registry, lua, aige::IsoParams{});
    aige::GameManifest manifest;
    manifest.titleMusic = "assets/music/title.ogg";

    EXPECT_EQ(aige::DirectAudio(registry, session, manifest, true).music, "assets/music/title.ogg");
    auto calm = aige::DirectAudio(registry, session, manifest, false);
    EXPECT_EQ(calm.music, "assets/music/town.ogg");
    EXPECT_EQ(calm.ambience, "assets/amb/wind.ogg");
    EXPECT_FALSE(calm.combat);

    aige::CreateEnemy(registry, 4.0f, 2.5f, player, ""); // chasing, in range
    auto fight = aige::DirectAudio(registry, session, manifest, false);
    EXPECT_TRUE(fight.combat);
    EXPECT_EQ(fight.music, "assets/music/fight.ogg");
}

TEST(CameraTest, DeadZoneLookAheadAndLevelEdges) {
    aige::Registry registry;
    auto player = aige::CreatePlayer(registry, 5.5f, 5.5f);
    for (int x = 0; x < 20; ++x) {
        for (int y = 0; y < 20; ++y) aige::CreateTile(registry, x, y, aige::TileKind::Grass);
    }
    aige::IsoParams iso;
    aige::UpdateWorldToScreen(registry, iso);
    aige::Camera2D camera;
    aige::CameraRig rig;
    rig.MeasureLevel(registry, iso);
    ASSERT_TRUE(rig.hasLevel);
    aige::CameraSettings settings;
    settings.deadZone = 0.2f;
    settings.lookAhead = 0.0f;
    aige::SnapCameraTo(camera, registry, player, 800, 600);
    const float x0 = camera.offsetX;
    // A small step inside the dead zone doesn't move the camera.
    registry.get<aige::ScreenTransform2D>(player).x += 20.0f;
    aige::UpdatePlayCamera(camera, rig, registry, player, 800, 600, 1.0f, settings);
    EXPECT_FLOAT_EQ(camera.offsetX, x0);
    // A big one does, by what's past the zone's edge.
    registry.get<aige::ScreenTransform2D>(player).x += 300.0f;
    aige::UpdatePlayCamera(camera, rig, registry, player, 800, 600, 1.0f, settings);
    EXPECT_GT(camera.offsetX, x0);
    // Clamped, the camera never shows past the level's left edge.
    settings.clampToLevel = true;
    registry.get<aige::ScreenTransform2D>(player).x = rig.levelMinX - 2000.0f;
    aige::UpdatePlayCamera(camera, rig, registry, player, 800, 600, 1.0f, settings);
    EXPECT_GE(camera.offsetX, rig.levelMinX - 0.01f);
}
