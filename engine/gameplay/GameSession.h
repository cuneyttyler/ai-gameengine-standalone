#pragma once

#include "ecs/Registry.h"
#include "gameplay/Behaviour.h"
#include "gameplay/Combat.h"
#include "gameplay/Cutscene.h"
#include "gameplay/Animation.h"
#include "gameplay/Difficulty.h"
#include "gameplay/Effects.h"
#include "gameplay/Magic.h"
#include "gameplay/InputMap.h"
#include "gameplay/Isometric.h"
#include "gameplay/Schedule.h"
#include "gameplay/Story.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Collision.h"
#include "platform/Input.h"
#include "scripting/ScriptSystem.h"
#include "renderer2d/Text.h"
#include "ui/Documents.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <optional>
#include <string>
#include <vector>

namespace aige {

class LuaVM;

// A JSON value as Lua (records, 12.1): objects become tables by key, arrays
// tables indexed from 1, null nil.
sol::object JsonToLua(lua_State* L, const nlohmann::json& value);

// KeyQueryFn ("is this key held right now?") and AxisQueryFn come from
// gameplay/InputMap.h, so gameplay doesn't care whether input comes from a
// GLFW window (sandbox) or the editor's Win32 viewport.

// No tagging/prefab system yet, so the player is the (first) entity with
// both Velocity2D and Collider2D — the same heuristic the editor's Scene
// Tree uses to label it.
std::optional<Entity> FindPlayer(Registry& registry);

// Screen-relative WASD/arrow movement (W visibly moves up-screen, the
// standard isometric-ARPG scheme), resolved against solid grid cells. Speed
// comes from Stats.moveSpeed (3 cells/s without Stats), scaled by Slow
// effects; a dead player doesn't move.
void UpdatePlayer(Registry& registry, Entity player, const KeyQueryFn& isKeyDown, const SolidQueryFn& isSolid,
                  float dt);
// The same, from a screen direction (right, down) in [-1, 1] each, as the
// move_x / move_y input actions give it; a stick pushed part way walks slower.
void UpdatePlayerMove(Registry& registry, Entity player, float inputRight, float inputDown, const SolidQueryFn& isSolid,
                      float dt);

// Moves every Follow entity toward its target (resolving collisions like the
// player), stopping at stopDistance; ignores targets beyond `range` and dead
// targets. A Follow with no target follows the player.
void UpdateFollowers(Registry& registry, const SolidQueryFn& isSolid, float dt);

// Mirrors sprites horizontally to match their Facing (art is drawn facing
// right), so characters look the way they move.
void UpdateSpriteFacing(Registry& registry);

// Flat (top-down) tiles draw this far below everything else (5.2).
constexpr float kGroundLayerDepth = -100000.0f;

// Re-derives ScreenTransform2D from WorldTransform2D (characters) and from
// Tile (ground cells) — the one place world space meets screen space.
// Content with neither keeps whatever ScreenTransform2D it was given.
void UpdateWorldToScreen(Registry& registry, const IsoParams& iso);

// Fixed-timestep simulation — player input, followers, Lua scripts, combat,
// projection — over a registry, shared by the sandbox and the editor's Play
// mode so the two can't drift apart. Collision comes from the registry's
// Tile entities (TileGrid).
class GameSession {
public:
    static constexpr double kFixedDt = 1.0 / 60.0;

    // `scriptRoot` is what relative LuaScript paths resolve against (the
    // project root).
    GameSession(Registry& registry, LuaVM& lua, IsoParams iso, std::filesystem::path scriptRoot = {});

    // Consumes `frameSeconds` of real time in kFixedDt steps (clamped, to
    // avoid a spiral of death after a long stall). Returns steps run.
    // Space attacks in the player's facing direction.
    int Advance(double frameSeconds, const KeyQueryFn& isKeyDown);

    // Drops any leftover partial step and input state, e.g. when (re)entering
    // Play mode.
    void ResetClock();

    // The projection changed (the Grid setting, 6.1).
    void SetIso(const IsoParams& iso) { iso_ = iso; }

    // The player attacks toward a world point on the next step (mouse click).
    void RequestPlayerAttack(float worldX, float worldY);

    struct ScriptError {
        Entity entity;
        std::string path;
        std::string message;
    };
    // Script load/runtime errors since the last ClearScriptErrors (capped).
    const std::vector<ScriptError>& ScriptErrors() const { return scriptErrors_; }
    void ClearScriptErrors() { scriptErrors_.clear(); }

    // What happened during the last Advance (hits, deaths, pickups).
    const std::vector<GameEvent>& Events() const { return events_; }

    // How scripts' play_sound(path) reaches the audio engine (path is
    // project-relative). Without one, play_sound does nothing.
    void SetSoundPlayer(std::function<void(const std::string&)> playSound) { playSound_ = std::move(playSound); }
    // play_sound_at(path, x, y): a sound placed in the world (12.6). Without one,
    // it plays like play_sound.
    void SetSoundAtPlayer(std::function<void(const std::string&, float, float)> play) { playSoundAt_ = std::move(play); }
    // play_music(path) from a script: what should play instead of the scene's
    // music, until play_music("") (a boss fight, a cutscene).
    const std::string& MusicOverride() const { return musicOverride_; }

    // How scripts' spawn(prefab, x, y) creates entities (the host owns the
    // prefab library). Without one, spawn returns nil.
    using SpawnFn = std::function<std::optional<Entity>(const std::string& prefab, float x, float y)>;
    void SetSpawner(SpawnFn spawner) { spawner_ = std::move(spawner); }

    // Dialogue and quests (development_plan.md 3.2b). With a library, E talks
    // to the nearest Talk entity in range, conversations pause the game, and
    // deaths, pickups and conversations advance quest objectives. The
    // library must outlive the session (or the next SetStory).
    void SetStory(const StoryLibrary* library);
    // World Bible key -> name shown on screen (speakers, prompts). Without
    // one, the talking entity's Name is used.
    void SetNameResolver(std::function<std::string(const std::string&)> nameOf) { nameOf_ = std::move(nameOf); }
    // Daily routines (development_plan.md 3.5); the library must outlive the session.
    void SetSchedules(const ScheduleLibrary* schedules) { schedules_ = schedules; }
    // The game clock, in minutes since day 0 began; starts at Time.startHour.
    double ClockMinutes() const { return clockMinutes_; }
    void SetClockMinutes(double minutes) { clockMinutes_ = minutes; }
    float MinuteOfDay() const;
    int Day() const;

    // Gateways (development_plan.md 5.3): stepping into a Gateway's cell
    // stops the game where it is and asks the host to change scene. The host
    // takes the request, replaces the scene (TravelToScene) and calls
    // OnSceneChanged; story, flags and the clock carry on.
    struct TravelRequest {
        std::string scene;
        std::string marker;
    };
    std::optional<TravelRequest> TakeTravel();
    void OnSceneChanged();

    // The scene being played ("scenes/cellar.aige"), set by the host when
    // play starts and after each travel. Quest objectives and schedule
    // entries tied to a scene use it.
    void SetScene(const std::string& scene);
    const std::string& Scene() const { return scene_; }

    // A saved game (development_plan.md 6.9): the scene, where the player
    // stands, the clock and the story's progress. The host loads the scene,
    // then ApplySave puts back the rest.
    nlohmann::json MakeSave() const;
    void ApplySave(const nlohmann::json& save);
    // A short message over the game ("Game saved").
    void Toast(const std::string& text) { ShowNotice(text, 2.0); }
    // A message in a channel (toast, feed, banner, alert): drawn as the classic toast,
    // or handed to the UI documents' notifications when a sink is set (17.5).
    void ShowNotice(const std::string& text, double seconds, const std::string& channel = "toast",
                    const std::string& key = "");
    using NoticeSink = std::function<void(const std::string& channel, const std::string& text, const std::string& key,
                                          double seconds)>;
    void SetNoticeSink(NoticeSink sink) { noticeSink_ = std::move(sink); }
    // Lua's open_screen / close_screen / is_open (17): the host shows the project's documents.
    using ScreenHook = std::function<bool(const std::string& id, int open /* 1 open, 0 close, -1 ask */)>;
    void SetScreenHook(ScreenHook hook) { screenHook_ = std::move(hook); }
    // The HUD and dialogue as documents (17): BuildUi then draws only cutscenes and
    // debug overlays; the host shows MakeHud() and CurrentDialogueView().
    void SetDocumentHud(bool on) { documentHud_ = on; }
    HudData MakeHud(float viewportWidth, float viewportHeight);
    std::optional<DialogueView> CurrentDialogueView() const;
    // Picks a dialogue choice by its place among the visible ones (a click on the document).
    void ChooseDialogue(int index);

    StoryState& Story() { return story_; }
    const StoryState& Story() const { return story_; }
    const DialogueRunner& Conversation() const { return conversation_; }
    // Who E would talk to right now (in range, not in a conversation).
    std::optional<Entity> TalkTarget();
    // What the interact action would do now (13.5): talk to someone, else use
    // the nearest door, chest, lever, item or merchant.
    struct InteractTarget {
        Entity entity = entt::null;
        bool talk = false;
    };
    std::optional<InteractTarget> CurrentInteraction();
    // A merchant the player asked to trade with (the host opens its shop), once.
    std::optional<Entity> TakeShopRequest() {
        auto request = shopRequest_;
        shopRequest_.reset();
        return request;
    }

    // The game's own UI for the story: the dialogue box, the talk prompt,
    // the current objective and quest notices. Viewport pixels.
    void BuildUi(std::vector<UiQuad>& out, float viewportWidth, float viewportHeight);

    // Cutscenes (15.5): cutscenes/<id>.json under the project. The world waits
    // (no input, no AI, no combat) while one plays; skip jumps to its end state.
    // `quest` fills its "@alias" actors. False if it doesn't load.
    bool PlayCutscene(const std::string& id, const std::string& quest = "");
    bool InCutscene() const { return cutscene_.Active(); }
    void SkipCutscene();
    const CutscenePlayer& CutscenePlaying() const { return cutscene_; }
    // What the camera should follow now: a cutscene's camera, else the player.
    std::optional<Entity> CameraTarget();
    // A cutscene's zoom, while it sets one.
    std::optional<float> CameraZoom() const { return cutscene_.Active() ? cameraZoom_ : std::nullopt; }

    // Difficulty (development_plan.md 16): the level being played, applied to
    // every scalable field when it changes, when a scene starts and after spawns.
    // The model is the project's difficulty records (else the built-in five).
    void SetDifficulty(const std::string& level);
    const std::string& DifficultyLevelId() const { return difficultyLevel_; }
    const DifficultyModel& Difficulty() const { return difficultyModel_; }
    // Re-reads the levels from the records (they were edited).
    void ReloadDifficulty();
    // The player chose "adjust difficulty to how I play" (Project Settings must allow it).
    void SetAdaptive(bool on);
    const AdaptiveDifficulty& Adaptive() const { return adaptive_; }

    // Animation and effects (18): sheets and animators, particles, effects, juice,
    // tweens and lights. The host draws AppendFxSprites with the world, lights the
    // renderer with Lighting() and offsets its camera by CameraShake().
    void ReloadAnimations() { animations_.Refresh(); }
    const AnimLibrary& Animations() const { return animations_; }
    ParticleSystem& Particles() { return particles_; }
    // Plays an effect (a record or preset) at a point, on a target, from a source.
    bool PlayEffect(const std::string& id, float x, float y, Entity target = entt::null, Entity source = entt::null);
    void Shake(float trauma);
    void HitStop(float seconds) { freeze_ = std::max(freeze_, static_cast<double>(seconds)); }
    void SlowMotion(float scale, float seconds) {
        slowScale_ = std::clamp(scale, 0.05f, 1.0f);
        slowSeconds_ = seconds;
    }
    std::pair<float, float> CameraShake() const; // pixels
    float Trauma() const { return trauma_; }
    bool Frozen() const { return freeze_ > 0.0; }
    LightingFrame Lighting() const;
    void AppendFxSprites(std::vector<SpriteItem>& out) const { particles_.AppendSprites(out, iso_); }
    int StartTween(Entity entity, const std::string& componentField, float to, float seconds, const std::string& ease);
    // Outside play (the editor trying an effect): particles, short lights and shake move on.
    void StepPreviewFx(float dt);
    bool TweenDone(int id) const;

    // Abilities and magic (19): the system (also in the registry's context), and casting.
    MagicSystem& Magic() { return *magic_; }
    // The player casts the ability in quick slot `slot` (0-3) the way it faces.
    bool CastSlot(int slot, std::string* why = nullptr);

    // Objective markers (15.4): where the shown objective's target is (world
    // units), when the scene has it: a marker, a character, an item lying about.
    std::optional<std::pair<float, float>> ObjectiveLocation();
    // The quest a story-aware window (the journal) reads from.
    const StoryLibrary* StoryLibraryInUse() const { return storyLibrary_; }

    // Events (development_plan.md 12.2): something that happened, by name,
    // delivered to every Lua handler registered with on(name, fn) at the end of
    // the step. The engine emits hit, death, pickup, attack and victory (from
    // combat), quest_started, quest_completed and objective_completed (from
    // the story), and talk; scripts and hosts emit their own.
    struct ScriptEvent {
        std::string name;
        Entity entity = entt::null; // who it happened to
        Entity other = entt::null;  // who did it (the attacker, the pickup)
        double amount = 0.0;
        std::string text;           // a quest id, a prefab path...
    };
    void Emit(ScriptEvent event) { pendingEvents_.push_back(std::move(event)); }

    // Input actions (12.3): the gamepad's sticks and triggers (the keys and
    // buttons come through Advance's KeyQueryFn), and the actions' state this
    // frame, which scripts read with action_down / action_pressed / axis.
    void SetAxisQuery(AxisQueryFn axes) { axisQuery_ = std::move(axes); }
    const InputState& Input() const { return input_; }
    // Every event delivered so far this Advance (for hosts and tests).
    const std::vector<ScriptEvent>& DeliveredEvents() const { return delivered_; }
    // Lua handlers, timers and coroutines waiting, for the debug overlay and tests.
    size_t HandlerCount() const { return handlers_.size(); }
    size_t TimerCount() const { return timers_.size(); }
    size_t TaskCount() const { return tasks_.size(); }

private:
    // A Lua function registered by a script: an event handler, a timer, or a
    // coroutine started by start() (or by a handler/timer, so they may wait()).
    struct ScriptHandler {
        int id = 0;
        Entity owner = entt::null;
        std::string event;
        sol::protected_function fn;
    };
    struct ScriptTimer {
        int id = 0;
        Entity owner = entt::null;
        double remaining = 0.0;
        double interval = 0.0; // 0: once
        sol::protected_function fn;
    };
    struct ScriptTask {
        Entity owner = entt::null;
        sol::thread thread;
        sol::coroutine co;
        double wait = 0.0;
    };
    void StartTask(Entity owner, const sol::protected_function& fn, const sol::object& argument);
    void ResumeTask(ScriptTask& task, const sol::object& argument);
    void DispatchEvents();
    void UpdateTimersAndTasks(float dt);
    void ForgetScript(Entity owner); // its script was reloaded or the entity is gone
    void ScriptFailed(Entity owner, const std::string& message);

    void HandleStoryInput(const KeyQueryFn& isKeyDown);
    void ShowNotices();
    void OnStoryEvents(size_t firstEvent);
    // Phase 15: the story's questions about the world, its effects on it, the
    // v2 objectives' checks each step, and the cutscene running.
    void WireStory();
    void ApplyWorldEffects();
    void StepStory(float dt);
    std::vector<std::string> NamesOf(Entity entity) const; // Actor key, Talk character, Name
    std::optional<Entity> FindNamed(const std::string& who) const;
    std::optional<std::pair<float, float>> MarkerPosition(const std::string& name) const;
    void FireCutsceneKey(const CutscenePlayer::Due& due);
    void StepCutscene(float dt);
    void StepTriggers();
    std::string NameOf(const std::string& key, Entity fallback) const;

    // TryMeleeAttack plus an Attack event (with the attacker's sound).
    bool Swing(Entity attacker, float dirX, float dirY);

    // The Lua API every script gets (see docs/scripting.md).
    void BindScriptApi(sol::environment& env, Entity self);

    // Phase 13: attributes, block/dodge/fire, archers, spawners, pickups; then
    // experience and loot for the step's deaths.
    void StepGameplay(float dt, const SolidQueryFn& isSolid, bool firstStep);
    void OnDeaths(size_t firstEvent);
    int LevelOf(Entity entity) const;
    void UseInteractable(Entity target);
    // Phase 14: character AI each step; a remark shown under the action; a
    // scripted package's Lua (false when its run() has finished).
    void StepCharacters(float dt, const SolidQueryFn& isSolid);
    void ShowBark(Entity speaker, const std::string& text);
    bool RunScriptedPackage(Entity actor, const std::string& script, float dt);

    Registry& registry_;
    LuaVM& lua_;
    IsoParams iso_;
    std::filesystem::path scriptRoot_;
    ScriptBinder binder_;
    double accumulator_ = 0.0;

    TileGrid grid_; // rebuilt every Advance, so tiles edited mid-play apply
    std::vector<GameEvent> events_;
    std::vector<ScriptError> scriptErrors_;
    std::function<void(const std::string&)> playSound_;
    std::function<void(const std::string&, float, float)> playSoundAt_;
    std::string musicOverride_;
    SpawnFn spawner_;
    std::optional<std::pair<float, float>> requestedAttack_;

    const StoryLibrary* storyLibrary_ = nullptr;
    StoryState story_;
    DialogueRunner conversation_;
    Entity talkingTo_ = entt::null;
    std::function<std::string(const std::string&)> nameOf_;
    std::string notice_;        // "New quest: ...", shown for a moment
    const ScheduleLibrary* schedules_ = nullptr;
    double clockMinutes_ = -1.0; // < 0: start at Time.startHour on the first Advance
    std::string scene_;
    uint64_t routineRng_ = 0x5EED;
    double noticeSeconds_ = 0.0;
    std::optional<TravelRequest> travel_;
    // A gateway fires when the player steps in, not while standing on one
    // (e.g. arriving on the way back): armed once off every gateway.
    bool gatewayArmed_ = false;
    bool CheckGateways();

    InputState input_;
    AxisQueryFn axisQuery_;
    std::mt19937 gameplayRng_{0x5EEDu}; // loot, spawns: seeded, so a playtest replays
    std::optional<Entity> shopRequest_;
    std::vector<AiNoise> noises_;       // heard by characters next step
    std::string barkText_;              // "Bandit: There you are!"
    double barkSeconds_ = 0.0;
    std::map<Entity, std::string> scriptedPackages_; // actor -> the package script running
    std::map<Entity, sol::environment> scriptEnvs_;
    bool showAiDebug_ = false;
    // Cutscenes (15.5).
    struct CutsceneMove {
        Entity entity = entt::null;
        float x = 0.0f, y = 0.0f, speed = 2.0f;
    };
    CutscenePlayer cutscene_;
    std::string cutsceneQuest_;
    std::vector<CutsceneMove> cutsceneMoves_;
    std::string subtitle_;
    double subtitleSeconds_ = 0.0;
    std::string titleCard_;
    double titleSeconds_ = 0.0;
    float fade_ = 0.0f, fadeTarget_ = 0.0f, fadeRate_ = 0.0f;
    float letterbox_ = 0.0f;
    Entity cameraFollow_ = entt::null;
    Entity cameraFocus_ = entt::null; // an invisible entity the camera follows to a point
    std::optional<std::pair<float, float>> cameraGoal_;
    std::optional<float> cameraZoom_;
    const DialogueNode* voicedNode_ = nullptr; // whose voice line has played
    // Animation and effects (18).
    AnimLibrary animations_;
    ParticleSystem particles_;
    std::vector<Tween> tweens_;
    int nextTween_ = 1;
    float trauma_ = 0.0f;
    double shakeTime_ = 0.0;
    double freeze_ = 0.0;
    float slowScale_ = 1.0f;
    double slowSeconds_ = 0.0;
    struct TempLight {
        float x, y, radius, r, g, b, seconds, age;
    };
    std::vector<TempLight> tempLights_;
    void StepFx(float dt); // animators, particles, tweens, lights
    std::unique_ptr<MagicSystem> magic_;
    float magicThink_ = 0.0f;
    void StepMagic(float dt, size_t firstEvent);
    void CueEffects(size_t firstEvent);
    NoticeSink noticeSink_;
    ScreenHook screenHook_;
    bool documentHud_ = false;
    // Difficulty (16).
    DifficultyModel difficultyModel_ = DifficultyModel::BuiltIn();
    std::string difficultyLevel_;
    bool difficultyDirty_ = true; // apply before the next step
    AdaptiveDifficulty adaptive_;
    bool adaptiveOn_ = false;
    bool playerHurtSinceKill_ = false;
    void ApplyDifficultyNow();
    float XpScale() const;
    std::vector<ScriptEvent> pendingEvents_;
    std::vector<ScriptEvent> delivered_;
    std::vector<ScriptHandler> handlers_;
    std::vector<ScriptTimer> timers_;
    std::vector<std::unique_ptr<ScriptTask>> tasks_;
    int nextScriptId_ = 1;
};

} // namespace aige
