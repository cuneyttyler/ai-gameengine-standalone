#pragma once

#include "ecs/Registry.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aige {

// Sprite animation (development_plan.md 18.1-18.2, effects-animation.md §3).
//
// A sheet (`<name>.anim.json` beside its picture, aige.anim/1) names a texture, a
// frame grid and clips; a clip is frames (grid indices or regions) with durations,
// a loop mode, frame events and, for characters, one frame list per direction
// (4 or 8; "mirror:e" draws east flipped for west). An animator (a record) is a
// state machine choosing the clip from parameters bound to components (speed from
// Velocity2D, the facing direction, attacking, hurt, dead), so most characters need
// no script. An entity's Animator component names both.

struct AnimFrame {
    float x = 0, y = 0, w = 0, h = 0; // the region, in pixels of the texture
    float duration = 0.1f;
};

struct AnimEvent {
    int frame = 0;
    std::string event; // footstep, hit, effect:<id>, sound:<path>, or a custom name
};

struct AnimClip {
    std::string name;
    std::string loop = "loop"; // loop | once | pingpong | hold
    // Frames per direction ("" for a clip without directions; s, se, e, ne, n, nw, w, sw).
    std::map<std::string, std::vector<AnimFrame>> directions;
    std::map<std::string, std::string> mirrors; // "w" -> "e": draw east's frames flipped
    std::vector<AnimEvent> events;

    // The frames for a facing (the nearest direction it has); flipped when mirrored.
    const std::vector<AnimFrame>* FramesFor(const std::string& direction, bool* flip) const;
    float Length(const std::string& direction) const;
};

struct AnimSheet {
    std::string path;    // project-relative, "assets/hero.anim.json"
    std::string texture; // project-relative
    int textureWidth = 0, textureHeight = 0; // for texture coordinates; 0: frames' extent
    std::map<std::string, AnimClip> clips;
    const AnimClip* Clip(const std::string& name) const;
};

std::optional<AnimSheet> AnimSheetFromJson(const nlohmann::json& json, const std::string& path, std::string* error);
std::vector<std::string> ValidateAnimSheet(const AnimSheet& sheet);
// An Aseprite JSON export (Export Sprite Sheet ▸ JSON, hash or array) as an aige.anim/1
// sheet: its tags become clips (forward, reverse, ping-pong), durations kept.
nlohmann::json AnimSheetFromAseprite(const nlohmann::json& aseprite, const std::string& texture);
// A sheet from a grid: `rows` names each row's clip ("walk_s", or "walk" for all
// directions in order s, e, n, w when `directional`).
nlohmann::json AnimSheetFromGrid(const std::string& texture, int frameWidth, int frameHeight, int columns,
                                 const std::vector<std::string>& rows, float fps);

// The project's sheets (every *.anim.json under assets/).
class AnimLibrary {
public:
    explicit AnimLibrary(std::filesystem::path projectRoot = {});
    void Refresh();
    const AnimSheet* Find(const std::string& path) const;
    const std::vector<AnimSheet>& Sheets() const { return sheets_; }
    const std::vector<std::string>& Problems() const { return problems_; }

private:
    std::filesystem::path root_;
    std::vector<AnimSheet> sheets_;
    std::vector<std::string> problems_;
};

// --- Animators -------------------------------------------------------------------------

struct AnimatorDef {
    std::string id;
    struct State {
        std::string id;
        std::string clip;   // the clip's name in the entity's sheet
        bool exitAtEnd = false; // back to the default state when the clip ends
        float speed = 1.0f;
    };
    struct Transition {
        std::string from; // a state, or "*"
        std::string to;
        std::string condition; // "speed > 0.1", "attacking", "!dead", "a && b"
        int priority = 0;
    };
    std::vector<State> states;
    std::vector<Transition> transitions;
    std::string defaultState;
    const State* Find(const std::string& id) const;
};

// The built-in animators: humanoid (idle, walk, attack, cast, hurt, die, use, sit,
// sleep), quadruped, flyer, prop (idle, open, close, break).
const AnimatorDef* BuiltInAnimator(const std::string& id);
std::vector<std::string> BuiltInAnimatorIds();
// A record (records/animator/<id>) as a definition.
AnimatorDef AnimatorFromJson(const std::string& id, const nlohmann::json& json);

// An entity animated by a sheet and an animator.
struct Animator {
    std::string sheet;               // assets/<name>.anim.json
    std::string animator = "humanoid"; // a built-in or a record id
    float speed = 1.0f;              // playback rate
    float corpseSeconds = 1.5f;      // a dead one stays this long, for its death clip
};

// Runtime: a dead animated entity kept until its death clip has played.
struct Corpse {
    float remaining = 0.0f;
};

// Runtime state.
struct AnimatorState {
    std::string state;
    std::string clip;
    std::string direction = "s";
    float time = 0.0f;
    int frame = -1;
    bool attacking = false, hurt = false, casting = false, using_ = false; // triggers, cleared when read
};

// Sets each animated entity's sprite to its current frame. `parameters` adds
// values a game sets (Lua anim_set). Returns the frame events reached, as
// (entity, event) for the session to turn into effects, sounds and script events.
struct AnimEventFired {
    Entity entity;
    std::string event;
};
std::vector<AnimEventFired> UpdateAnimators(Registry& registry, const AnimLibrary& library, float dt);
// Triggers from gameplay: "attacking", "hurt", "casting", "using".
void TriggerAnimation(Registry& registry, Entity entity, const std::string& trigger);
// The 8-way direction name of a facing vector (screen space for isometric is the caller's).
std::string DirectionName(float dx, float dy);
// Whether a condition holds for these parameters (exposed for tests).
bool AnimConditionHolds(const std::string& condition, const std::map<std::string, float>& parameters);

} // namespace aige
