#pragma once

#include "ecs/Registry.h"
#include "gameplay/Isometric.h"
#include "renderer2d/ImageData.h"
#include "renderer2d/SpriteCulling.h"

#include <nlohmann/json.hpp>

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace aige {

// Particles, effects, juice, lights, day and night, weather (development_plan.md
// 18.3-18.6, effects-animation.md §4-7).
//
//   records/particles/<id>   an emitter: rate or burst, lifetime, shape, speed and
//                            direction, gravity, drag, size, colour and alpha over life
//   records/effect/<id>      what something looks and sounds like: particles, a sound,
//                            a light, a flash, shake, hit-stop, slow motion
// The engine has presets of both (data/fx); a project's record of the same id wins.

// --- Particles -------------------------------------------------------------------------

struct ParticleDef {
    std::string id;
    float rate = 20.0f;      // per second while playing
    int burst = 0;           // at once when it starts
    float duration = 0.0f;   // seconds it emits (0: until stopped)
    float lifeMin = 0.6f, lifeMax = 1.0f;
    std::string shape = "point"; // point | circle | box
    float radius = 0.2f;     // circle radius / box half-size, cells
    float speedMin = 0.5f, speedMax = 1.5f; // cells per second
    float direction = -90.0f; // degrees on screen: 0 right, -90 up
    float spread = 360.0f;   // degrees around the direction
    float gravity = 0.0f;    // screen pixels per second squared, + down
    float drag = 0.0f;       // per second
    float rise = 0.0f;       // pixels per second upward, added to the start velocity
    float sizeStart = 6.0f, sizeEnd = 2.0f; // pixels
    float color0[4] = {1, 0.8f, 0.4f, 1}, color1[4] = {1, 0.2f, 0.0f, 0};
    std::string texture;     // empty: a soft dot
    bool above = false;      // drawn above everything (sparks in the air) instead of y-sorted
    int max = 300;
};
ParticleDef ParticleDefFromJson(const std::string& id, const nlohmann::json& json);
// A project's record, else the engine's preset (fire, embers, smoke, sparks, blood, dust,
// heal, magic-trail, explosion, rain, snow, leaves, fireflies).
std::optional<ParticleDef> FindParticles(const std::string& id);
std::vector<std::string> ParticlePresets();
// The engine's preset file (folder: particles or effects).
std::filesystem::path FxPresetPath(const std::string& folder, const std::string& id);

// An entity that emits particles while playing.
struct ParticleEmitter {
    std::string particles;
    bool playing = true;
    float scale = 1.0f; // rate and size
};

class ParticleSystem {
public:
    // Keeps emitters going, spawns and moves particles (world space, cells; height in pixels).
    void Update(Registry& registry, float dt);
    // A one-off emission at a point (an effect, a hit): the def's burst plus `seconds` of its rate.
    void Emit(const ParticleDef& def, float x, float y, float seconds = 0.0f, Entity follow = entt::null);
    // A steady emission at a moving point (weather around the player), for one step.
    void Stream(const ParticleDef& def, float x, float y, float dt, float scale = 1.0f);
    // The live particles as sprites for the renderer, projected to the screen.
    void AppendSprites(std::vector<SpriteItem>& out, const IsoParams& iso) const;
    size_t Count() const { return particles_.size(); }
    void Clear();
    int budget = 4000; // most particles alive at once

private:
    struct Particle {
        float x, y, z, vx, vy, vz; // cells, cells, pixels; per second
        float age, life, size0, size1, gravity, drag;
        float c0[4], c1[4];
        std::string texture;
        bool above;
    };
    struct Source { // a running emission: an entity's emitter or a one-off
        ParticleDef def;
        Entity follow = entt::null;
        float x = 0, y = 0, elapsed = 0, owed = 0, scale = 1;
        bool once = false;
        float seconds = 0; // one-off: how long it keeps emitting
    };
    void Spawn(const Source& source, int count);
    std::vector<Particle> particles_;
    std::map<Entity, Source> emitters_;
    std::vector<Source> oneOffs_;
    std::map<std::string, float> streams_; // owed particles per stream
    std::mt19937 rng_{0xF1u};
};

// --- Effects ------------------------------------------------------------------------------

struct EffectPart {
    std::string kind; // particles | sound | light | flash | shake | hitstop | slowmo
    nlohmann::json data;
};
struct EffectDef {
    std::string id;
    std::vector<EffectPart> parts;
};
EffectDef EffectDefFromJson(const std::string& id, const nlohmann::json& json);
std::optional<EffectDef> FindEffect(const std::string& id);
std::vector<std::string> EffectPresets();
std::vector<std::string> ValidateEffect(const EffectDef& effect);

// Effects played by gameplay events on this entity (Gameplay Cues).
struct EffectCues {
    std::string onHit;
    std::string onDeath;
    std::string onAttack;
};

// --- Lights, day and night, weather ---------------------------------------------------------

struct Light2D {
    float r = 1.0f, g = 0.8f, b = 0.55f;
    float intensity = 1.0f;
    float radius = 4.0f;  // cells
    float flicker = 0.0f; // 0-1
    bool nightOnly = false; // on only when it's dark (street lamps, windows)
};

// A scene's weather (18.6): rain, snow, fog or sandstorm, with its strength.
struct Weather {
    int kind = 0; // 0 none, 1 rain, 2 snow, 3 fog, 4 sandstorm
    float intensity = 0.5f;
};

// What the renderer needs this frame: ambient light and lights in scene pixels.
struct LightingFrame {
    bool enabled = false;
    float ambient[3] = {1, 1, 1};
    struct Light {
        float x, y, radius, r, g, b, intensity;
    };
    std::vector<Light> lights;
    float darkness = 0.0f; // 0 day .. 1 deepest night (for emissive and night-only lights)
};
// The ambient light at a minute of the day, from Project Settings ▸ Lighting.
void AmbientAt(float minuteOfDay, float out[3], float* darkness);

// Accessibility (the player's settings): less shake, no flashes.
struct FxAccessibility {
    float shakeScale = 1.0f;
    bool flashes = true;
};
FxAccessibility& ActiveFxAccessibility();

// The engine's own textures effects draw with ("fx:dot", "fx:vignette"), to register once.
std::vector<std::pair<std::string, ImageData>> FxTextures();

// --- Tweens -----------------------------------------------------------------------------------

// A reflected number eased from its value to a target over time (18.3).
struct Tween {
    int id = 0;
    Entity entity = entt::null;
    std::string component, field;
    float from = 0, to = 0, seconds = 1, elapsed = 0;
    std::string ease = "linear"; // linear | ease_in | ease_out | ease_in_out | bounce
    bool started = false;
};
float Ease(const std::string& ease, float t);

} // namespace aige
