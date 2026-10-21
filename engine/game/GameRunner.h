#pragma once

#include "assets/Project.h"
#include "audio/Audio.h"
#include "game/Actors.h"
#include "game/GameFlow.h"
#include "game/PlayWindows.h"
#include "game/SaveGames.h"
#include "gameplay/GameSession.h"
#include "gameplay/Schedule.h"
#include "gameplay/Story.h"
#include "world/WorldBible.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace aige {

class LuaVM;
class PrefabLibrary;

// A whole game around the simulation (development_plan.md 12.4, 12.5): the title
// screen, new game, continue and load, pause, save slots, game over and
// victory, credits, scene transitions with a fade, and the world remembered
// across scenes and saves. The player (sandbox) runs games with it; the
// editor's Play uses it with the title screen off. Owns the GameSession and the
// project's story, schedules and World Bible; the host owns the window, the
// renderer and the audio.
struct GameRunnerOptions {
    bool titleScreen = true;      // and false: straight into play (the editor's Play)
    bool pauseMenu = true;        // the "menu" action opens it (the editor stops Play itself)
    std::filesystem::path saveDir; // the save slots' folder
};

class GameRunner {
public:
    GameRunner(const Project& project, Registry& registry, LuaVM& lua, PrefabLibrary& prefabs,
               GameRunnerOptions options);

    GameSession& Session() { return *session_; }
    GameFlow& Flow() { return flow_; }
    SaveGames& Saves() { return saves_; }

    // The title screen, or a new game when it's off.
    void Start();
    // One frame: menus or play, travel, deaths, quick save and load.
    void Frame(double seconds, const KeyQueryFn& keys, const AxisQueryFn& axes = {});
    // The game's UI on top of the scene: the session's (dialogue, objective),
    // the menus, toasts and the fade.
    void BuildUi(std::vector<UiQuad>& out, float viewportWidth, float viewportHeight);

    void NewGame();
    bool SaveTo(const std::string& slot);
    bool LoadFrom(const std::string& slot);

    // The scene changed under the camera (start, travel, load): snap it, once.
    bool TakeCameraSnap() {
        bool snap = snapCamera_;
        snapCamera_ = false;
        return snap;
    }
    bool QuitRequested() const { return quit_; }
    double PlaySeconds() const { return playSeconds_; }
    // What happened since the last call ("Game saved", "Now in ..."), for a host's console.
    std::vector<std::string> TakeLog();
    // The game's sound (12.6): music, ambience and the listener follow the game
    // each frame. Optional (headless runs have none).
    void SetAudio(AudioEngine* audio) {
        audio_ = audio;
        ApplySettings();
    }
    // The player's settings (12.7): saved beside the saves, applied to the audio
    // and the language, and to the window through `onFullscreen`.
    PlayerSettings& Settings() { return settings_; }
    void ApplySettings();
    void OfferDifficulties(); // the levels for the New game and Settings menus (16)
    void ApplyAssists();      // the player's assists and adaptive choice (16.4)
    std::function<void(bool fullscreen)> onFullscreen;

    // The game's UI as documents (17, game-ui.md): the host turns it on once it can
    // load pictures and register textures; without it the built-in overlays draw.
    void EnableDocuments(DocumentUi::ImageLoader images);
    DocumentUi* Documents() { return docs_.get(); }
    // Textures the documents made since the last call, for the renderer.
    std::vector<DocumentUi::NewTexture> TakeUiTextures();
    // The mouse over the documents (viewport pixels; button -1: a move).
    void UiMouse(int x, int y, int button, bool down);

    // The world's changes per scene, so far (tests, the debug overlay).
    const std::map<std::string, nlohmann::json>& Worlds() const { return worlds_; }

private:
    void HandleCommand(const GameFlow::Command& command);
    void FrameLogic(double seconds, const KeyQueryFn& keys, const AxisQueryFn& axes);
    void CaptureCurrentScene();
    ActorRoster::SpawnFn ActorSpawner();
    void ApplyWorld(const std::string& scene);
    void TravelIfAsked();
    void LoadNotificationRules();
    void NotifyByRules(); // the session's events this frame -> notifications (17.5)
    std::string MenuScreen() const; // the id of the menu or window on screen
    std::string PlaceName(const std::string& scene) const;

    const Project& project_;
    Registry& registry_;
    LuaVM& lua_;
    PrefabLibrary& prefabs_;
    GameRunnerOptions options_;
    std::unique_ptr<GameSession> session_;
    StoryLibrary story_;
    ScheduleLibrary schedules_;
    WorldBible world_;
    GameFlow flow_;
    SaveGames saves_;
    InputState menuInput_;
    std::map<std::string, nlohmann::json> worlds_;
    double playSeconds_ = 0.0;
    double fade_ = 0.0;        // 1 = black, fading out over a moment
    double deathTimer_ = -1.0; // counts down to the game-over screen
    bool snapCamera_ = false;
    bool quit_ = false;
    std::vector<std::string> log_;
    AudioEngine* audio_ = nullptr;
    PlayerSettings settings_;
    PlayWindows windows_; // inventory and shop over play (13.3-13.4)
    ActorRoster actors_;  // persistent characters (14.4)
    std::mt19937 shopRng_{0x5409u};
    std::unique_ptr<DocumentUi> docs_;
    nlohmann::json notificationRules_ = nlohmann::json::array();
};

} // namespace aige
