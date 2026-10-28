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
    float playZoom = 2.0f;        // camera zoom while playing (the sandbox, and editor Play)
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
    float gravityX = 0.0f;      // rigid bodies' gravity, cells per second squared (20.3);
    float gravityY = 0.0f;      // top-down games have none
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

// 2D lighting and day and night (development_plan.md 18.6).
struct LightingSettings {
    bool enabled = false;                // light the scene (ambient plus lights)
    bool dayNight = true;                // the ambient follows the game clock
    std::string dayAmbient = "#ffffff";
    std::string nightAmbient = "#36426e";
    float dawnHour = 6.0f;
    float duskHour = 19.0f;
    bool vignette = false;               // darken the screen's edges
};

// The game's UI (development_plan.md 17, game-ui.md).
struct UiSettings {
    bool documents = true; // menus, windows and the HUD as themed documents (off: the built-in overlays)
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

// The project's own AI choices (Project Settings ▸ AI): each empty uses the app's (Preferences ▸ AI), so
// projects can differ (one drawn by PixelLab, another by BFL) without changing the editor's defaults.
struct AiSettings {
    std::string provider;   // the language model's provider: mistral, claude
    std::string model;      // its model (empty: the provider's default)
    std::string art;        // who draws the prefabs' art: mistral, pixellab, bfl:<model>
    std::string background; // who paints the places' backgrounds: bfl:flux-3-image
    std::string paint;      // who paints a place whole (the Paint method): flux-2-klein-4b, flux-3-image, flux-3-to-pro, flux-3-to-klein
    // The art's look, for every art prompt (prefabs, matched art, backgrounds, paint, characters): pixel-16, pixel-32,
    // painted, flat-vector; "" from the project (each pipeline's own look). The planner's art style fills it.
    std::string artStyle;
};

// An art style in words for a prompt (ai_service/app/style.py ART_STYLES has the same); "" for none.
inline std::string ArtStyleWords(const std::string& style) {
    if (style == "pixel-16") return "16-bit 1990s pixel art: crisp square pixels, a limited palette, clean dark outlines";
    if (style == "pixel-32") return "detailed 32-bit era pixel art: crisp square pixels, rich shading, clean outlines";
    if (style == "painted") return "hand-painted digital game art: soft painterly shading and brush texture, not pixel art";
    if (style == "flat-vector") return "flat vector game art: clean geometric shapes, flat colours, simple shading, not pixel art";
    return {};
}
inline bool PixelArtStyle(const std::string& style) { return style.empty() || style.rfind("pixel", 0) == 0; }

// How the AI builds the project's places (docs/places/painted-places.md): "Assembled" (each piece's art made and put
// together on the grid over the painted background: Confirm, Layout, Plan, Assets, Build) or "Paint" (the place painted
// whole, then marked by hand: Confirm, Layout, Plan, Paint, Mark, Assets, Build). A build and a plan start with it and
// can choose the other.
struct PlacesSettings {
    std::string method = "Assembled";
};

struct GameSettings {
    std::string startScene = "scenes/main.aige"; // where play begins (5.3)
    std::string version = "1.0";                  // shown in the window's title and the crash log (24)
    bool debugConsole = true;                     // ` opens the debug console in play (exports: only when asked)
    std::string splash;                           // a picture shown when the game starts (assets/...)
};

// Streaming big scenes (24): only the chunks around the player are simulated.
struct StreamingSettings {
    bool enabled = false;
    int chunk = 32;  // cells a side
    int radius = 2;  // chunks around the player's that stay awake
};

struct ProjectSettings {
    GameSettings game;
    GridSettings grid;
    AiSettings ai;
    PlacesSettings places;
    RendererSettings renderer;
    CameraSettings camera;
    CombatSettings combat;
    PhysicsSettings physics;
    TimeSettings time;
    StorySettings story;
    DifficultySettings difficulty;
    UiSettings ui;
    LightingSettings lighting;
    StreamingSettings streaming;
};

ProjectSettings& ActiveSettings();

} // namespace aige
