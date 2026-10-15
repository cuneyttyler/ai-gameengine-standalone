#pragma once

#include "ecs/Registry.h"
#include "gameplay/Isometric.h"
#include "physics2d/Collision.h"
#include "platform/Input.h"
#include "scripting/ScriptSystem.h"

#include <functional>
#include <optional>

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
// standard isometric-ARPG scheme), resolved against solid grid cells.
void UpdatePlayer(Registry& registry, Entity player, const KeyQueryFn& isKeyDown, const SolidQueryFn& isSolid,
                  float dt);

// Re-derives ScreenTransform2D from WorldTransform2D for every entity that
// has both — the one place world space meets screen space. Static content
// without a WorldTransform2D keeps whatever ScreenTransform2D it was given.
void UpdateWorldToScreen(Registry& registry, const IsoParams& iso);

// Exposes get_position/set_position (bound to the entity's WorldTransform2D)
// to every script.
ScriptBinder MakeWorldTransformBinder(Registry& registry);

// Fixed-timestep simulation (player input, Lua scripts, projection) over a
// registry, shared by the sandbox and the editor's Play mode so the two can't
// drift apart.
class GameSession {
public:
    static constexpr double kFixedDt = 1.0 / 60.0;

    GameSession(Registry& registry, LuaVM& lua, IsoParams iso, SolidQueryFn isSolid);

    // Consumes `frameSeconds` of real time in kFixedDt steps (clamped, to
    // avoid a spiral of death after a long stall). Returns steps run.
    int Advance(double frameSeconds, const KeyQueryFn& isKeyDown);

    // Drops any leftover partial step, e.g. when (re)entering Play mode.
    void ResetClock() { accumulator_ = 0.0; }

private:
    Registry& registry_;
    LuaVM& lua_;
    IsoParams iso_;
    SolidQueryFn isSolid_;
    ScriptBinder binder_;
    double accumulator_ = 0.0;
};

} // namespace aige
