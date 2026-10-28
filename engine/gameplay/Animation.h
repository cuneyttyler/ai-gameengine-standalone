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

// A named point on a frame (docs/assets/animations.md §3.1): where a hand, a head or a saddle is, in pixels of the
// frame, its angle (degrees, 0 = right, clockwise) and whether what's held there is in front of the body or behind it.
struct AnimPoint {
    float x = 0, y = 0, angle = 0;
    bool front = true;
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
    // The states it plays for besides its name's ("Plays on": a clip "punch" with plays ["attack"]).
    std::vector<std::string> plays;
    // Named points per direction ("" for all), a point a frame (one: every frame).
    std::map<std::string, std::map<std::string, std::vector<AnimPoint>>> points;

    // The frames for a facing (the nearest direction it has); flipped when mirrored.
    // `used`: the direction whose frames they are (east's for a west mirrored from it).
    const std::vector<AnimFrame>* FramesFor(const std::string& direction, bool* flip, std::string* used = nullptr) const;
    float Length(const std::string& direction) const;
};

struct AnimSheet {
    std::string path;    // project-relative, "assets/hero.anim.json"
    std::string texture; // project-relative
    int textureWidth = 0, textureHeight = 0; // for texture coordinates; 0: frames' extent
    std::map<std::string, AnimClip> clips;
    std::map<std::string, AnimPoint> points; // where a clip has none (a head, a saddle)
    const AnimClip* Clip(const std::string& name) const;
    // The point `name` on a frame of `clip` facing `direction` (its frames' own direction; mirrored when `flip`), in
    // pixels of a frame `frameWidth` wide; false when neither the clip nor the sheet has it.
    bool PointOn(const AnimClip& clip, const std::string& direction, int frame, bool flip, float frameWidth,
                 const std::string& name, AnimPoint* out) const;
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
    // A project picture's size in pixels (read from its PNG header, kept); false when it can't be read.
    bool TextureSize(const std::string& path, int* width, int* height) const;

private:
    std::filesystem::path root_;
    mutable std::map<std::string, std::pair<int, int>> sizes_;
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
    // Per entity, the clips a state plays: "attack=attack_punch|attack_kick; open=lid_up" (docs/assets/animations.md §2.1).
    std::string clips;
    std::string pick = "weapon";     // among several: weapon (its choice, else random), random, order, first
    std::string attackClass;         // stands in for the weapon's class ("" = the weapon's)
};

// On another entity's point (docs/assets/animations.md §3.2): a sword in a hand, a hat on a head, a rider on a saddle.
struct Attachment {
    std::string to;            // the other entity's Name
    std::string point;         // "hand"; "" = the other's bottom middle
    float offsetX = 0.0f, offsetY = 0.0f; // added, in pixels of the other's frame
    float angle = 0.0f;        // added, degrees
    float pivotX = 0.5f, pivotY = 0.5f;   // where this picture is held, 0-1 of it
    float pointing = 0.0f;     // which way this picture's art points (degrees: 0 right, -90 up)
    float size = 0.0f;         // its longest side in the other's frame pixels; 0 = its own at the other's scale
    std::string layer = "auto"; // auto (the point's front/behind), front, behind
    bool turn = true;          // turned with the point's angle (off: kept upright)
};

// Runtime: what an Attachment (or a piece of Equipment shown) is on, found each frame.
struct AttachmentLink {
    Entity parent{entt::null};
    std::string slot; // the Equipment slot it shows ("" for an Attachment of the scene's)
    std::string item;
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
    std::string play;     // a state or clip asked for by name (play(id, "dig")), read once
    std::string chosenFor; // the state `clip` was chosen for
    std::string oneShot;  // a clip played by name, outside the animator's states, until it ends
    int order = 0;        // pick = order: the next variant
    std::map<std::string, float> parameters; // set by scripts (anim_set)
    // This frame's points, its size and whether it's drawn mirrored (for attachments).
    std::map<std::string, AnimPoint> points;
    float frameWidth = 0.0f, frameHeight = 0.0f;
};

// Sets each animated entity's sprite to its current frame. `parameters` adds
// values a game sets (Lua anim_set). Returns the frame events reached, as
// (entity, event) for the session to turn into effects, sounds and script events.
struct AnimEventFired {
    Entity entity;
    std::string event;
};
std::vector<AnimEventFired> UpdateAnimators(Registry& registry, const AnimLibrary& library, float dt);
// Triggers from gameplay: "attacking", "hurt", "casting", "using"; any other name plays the state of that name, a
// state's variant ("attack_kick"), or the clip of that name once ("stop": back to the default state).
void TriggerAnimation(Registry& registry, Entity entity, const std::string& trigger);
// A parameter for an animator record's conditions (Lua anim_set).
void SetAnimationParameter(Registry& registry, Entity entity, const std::string& name, float value);
// How long `name` (a clip, a state's clip or variant) plays on this entity's sheet; 0 when it has none.
float AnimationLength(Registry& registry, const AnimLibrary& library, Entity entity, const std::string& name);
// Attachments and shown Equipment: their parents found, pictures and sizes set (after UpdateAnimators) ...
void UpdateAttachments(Registry& registry, const AnimLibrary& library);
// ... and placed on their parents' points once those are on screen (the end of UpdateWorldToScreen).
void PlaceAttachments(Registry& registry);
// The 8-way direction name of a facing vector (screen space for isometric is the caller's).
std::string DirectionName(float dx, float dy);
// Whether a condition holds for these parameters (exposed for tests).
bool AnimConditionHolds(const std::string& condition, const std::map<std::string, float>& parameters);

} // namespace aige
