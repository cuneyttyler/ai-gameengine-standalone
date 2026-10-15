#pragma once

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

struct ProjectSettings {
    RendererSettings renderer;
    CameraSettings camera;
    CombatSettings combat;
    PhysicsSettings physics;
};

ProjectSettings& ActiveSettings();

} // namespace aige
