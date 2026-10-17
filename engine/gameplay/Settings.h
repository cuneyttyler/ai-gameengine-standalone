#pragma once

#include <string>

namespace aige {

// Project-wide settings, registered with the reflection registry (so the
// Inspector, scripts and the AI can read and change them) and saved in the
// project's settings.aige. One active instance per process: an app works on
// one project at a time.

struct RendererSettings {
    // Background around the level.
    float clearR = 0.05f;
    float clearG = 0.05f;
    float clearB = 0.08f;
};

struct CameraSettings {
    float followStiffness = 6.0f; // how quickly the play camera catches up with the player
    float playZoom = 1.0f;        // camera zoom while playing (the sandbox, and editor Play)
};

struct CombatSettings {
    float minimumDamage = 1.0f;    // a hit never deals less than this after defense
    float hitFlashSeconds = 0.12f; // how long a damaged sprite flashes red
    float knockbackScale = 1.0f;   // multiplies every attack's knockback
};

struct PhysicsSettings {
    bool playerCollides = true; // off = the player walks through walls (debugging)
};

struct TimeSettings {
    float startHour = 8.0f;        // the time of day a game starts at
    float minutesPerSecond = 2.0f; // game minutes per real second (2: a day takes 12 minutes)
    bool showClock = false;        // always show the clock (it shows anyway when someone has a routine)
};

// How the world is laid out on screen (development_plan.md 6.1): diamond
// cells seen at an angle, or square cells seen from straight above (for art
// drawn top-down, whose fences and bridges run along the screen's axes).
enum class GridStyle {
    Isometric,
    TopDown,
};

struct GridSettings {
    GridStyle style = GridStyle::Isometric;
};

struct GameSettings {
    std::string startScene = "scenes/main.aige"; // where play begins (5.3)
};

struct ProjectSettings {
    GameSettings game;
    GridSettings grid;
    RendererSettings renderer;
    CameraSettings camera;
    CombatSettings combat;
    PhysicsSettings physics;
    TimeSettings time;
};

ProjectSettings& ActiveSettings();

} // namespace aige
