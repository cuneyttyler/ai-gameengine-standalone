#pragma once

#include "ecs/Registry.h"
#include "gameplay/Combat.h"
#include "gameplay/Isometric.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Collision.h"
#include "platform/Input.h"
#include "scripting/ScriptSystem.h"

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
// targets.
void UpdateFollowers(Registry& registry, const SolidQueryFn& isSolid, float dt);

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

    // What happened during the last Advance (hits, deaths, pickups).
    const std::vector<GameEvent>& Events() const { return events_; }

    // How scripts' play_sound(path) reaches the audio engine (path is
    // project-relative). Without one, play_sound does nothing.
    void SetSoundPlayer(std::function<void(const std::string&)> playSound) { playSound_ = std::move(playSound); }

private:
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
    std::function<void(const std::string&)> playSound_;
    bool attackKeyWasDown_ = false;
    std::optional<std::pair<float, float>> requestedAttack_;
};

} // namespace aige
