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
    float deadZone = 0.1f;        // share of the screen around the centre the player moves in freely (12.7)
    float lookAhead = 1.5f;       // cells the camera leads the player by, the way they move
    bool clampToLevel = false;    // never show beyond the level's edges
};

struct CombatSettings {
    float minimumDamage = 1.0f;    // a hit never deals less than this after defense
    float hitFlashSeconds = 0.12f; // how long a damaged sprite flashes red
    float knockbackScale = 1.0f;   // multiplies every attack's knockback
    float blockReduction = 0.7f;   // the share of a blocked hit that's stopped (13.6)
    float dodgeDistance = 2.5f;    // cells a dodge covers
    float dodgeStamina = 20.0f;    // what a dodge costs, with stamina
};

struct PhysicsSettings {
    bool playerCollides = true; // off = the player walks through walls (debugging)
};

struct TimeSettings {
    float startHour = 8.0f;        // the time of day a game starts at
    float minutesPerSecond = 2.0f; // game minutes per real second (2: a day takes 12 minutes)
    bool showClock = false;        // always show the clock (it shows anyway when someone has a routine)
};

// How enemies' levels follow the player (development_plan.md 16.3).
enum class LevelScaling {
    None,       // a spawner's own level
    FixedZones, // the scene's Zone band
    Scaled,     // the player's level plus an offset
    Hybrid,     // the player's level, kept inside the zone's band
};

// Difficulty (development_plan.md 16, difficulty.md).
struct DifficultySettings {
    std::string defaultLevel = "normal"; // a difficulty record's id
    bool changeAnytime = true;           // the player may change it during play
    bool adaptive = false;               // offer "adjust difficulty to how I play"
    LevelScaling scaling = LevelScaling::None;
    int levelOffset = 0;                 // Scaled and Hybrid: the player's level plus this
};

// The story's on-screen help (development_plan.md 15.4).
struct StorySettings {
    bool objectiveMarkers = true; // a pip pointing the way to the current objective
    bool journal = true;          // the journal window (J)
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
    StorySettings story;
    DifficultySettings difficulty;
};

ProjectSettings& ActiveSettings();

} // namespace aige
