#pragma once

#include "ecs/Registry.h"
#include "gameplay/Combat.h"
#include "gameplay/Isometric.h"
#include "gameplay/Schedule.h"
#include "gameplay/Story.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Collision.h"
#include "platform/Input.h"
#include "scripting/ScriptSystem.h"
#include "renderer2d/Text.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace aige {

class LuaVM;

// "Is this key held right now?" — a callback so gameplay code doesn't care
// whether input comes from a GLFW window (sandbox) or the editor's Win32
// viewport.
using KeyQueryFn = std::function<bool(Key)>;

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

    StoryState& Story() { return story_; }
    const StoryState& Story() const { return story_; }
    const DialogueRunner& Conversation() const { return conversation_; }
    // Who E would talk to right now (in range, not in a conversation).
    std::optional<Entity> TalkTarget();

    // The game's own UI for the story: the dialogue box, the talk prompt,
    // the current objective and quest notices. Viewport pixels.
    void BuildUi(std::vector<UiQuad>& out, float viewportWidth, float viewportHeight);

private:
    void HandleStoryInput(const KeyQueryFn& isKeyDown);
    void ShowNotices();
    void OnStoryEvents(size_t firstEvent);
    std::string NameOf(const std::string& key, Entity fallback) const;
    bool Pressed(Key key, const KeyQueryFn& isKeyDown); // went down since the last Advance

    // TryMeleeAttack plus an Attack event (with the attacker's sound).
    bool Swing(Entity attacker, float dirX, float dirY);

    // The Lua API every script gets (see docs/scripting.md).
    void BindScriptApi(sol::environment& env, Entity self);

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
    SpawnFn spawner_;
    bool attackKeyWasDown_ = false;
    std::optional<std::pair<float, float>> requestedAttack_;

    const StoryLibrary* storyLibrary_ = nullptr;
    StoryState story_;
    DialogueRunner conversation_;
    Entity talkingTo_ = entt::null;
    std::function<std::string(const std::string&)> nameOf_;
    std::vector<Key> keysDown_; // for edge detection of the story keys
    std::string notice_;        // "New quest: ...", shown for a moment
    const ScheduleLibrary* schedules_ = nullptr;
    double clockMinutes_ = -1.0; // < 0: start at Time.startHour on the first Advance
    uint64_t routineRng_ = 0x5EED;
    double noticeSeconds_ = 0.0;
};

} // namespace aige
