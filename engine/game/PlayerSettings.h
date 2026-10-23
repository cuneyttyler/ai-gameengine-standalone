#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace aige {

// A player's own settings (development_plan.md 12.7), kept per user and per game
// beside their saves (settings.json), not in the project: volumes, language,
// window mode, text and subtitles. Rebound controls go in an input.json beside it.
struct PlayerSettings {
    float master = 1.0f;
    float music = 0.8f;
    float effects = 1.0f;
    float voice = 1.0f;
    float ambience = 0.8f;
    float ui = 1.0f;
    std::string language; // "" = the game's own
    bool fullscreen = false;
    bool vsync = true;
    float uiScale = 1.0f;
    bool subtitles = true;
    // Assists (development_plan.md 16.4): help apart from the difficulty level.
    bool invincible = false;
    float damageReduction = 0.0f; // 0, 0.25, 0.5, 0.75
    bool growingReduction = false; // more after each death
    float gameSpeed = 1.0f;       // 1, 0.75, 0.5
    bool adaptive = false;        // "adjust difficulty to how I play" (when the game offers it)
    // Accessibility (development_plan.md 24).
    bool reduceMotion = false;    // no screen shake, hit-stop or slow motion
    bool reduceFlashes = false;   // no flashes
    bool highContrast = false;    // white on black, a yellow accent
    std::string colorblind = "none"; // none | protanopia | deuteranopia | tritanopia: safe bar colours
    float textScale = 1.0f;       // the UI's text, apart from the UI's scale: 1, 1.25, 1.5, 2
    bool holdToToggle = false;    // actions held down (block, sprint...) toggle instead

    nlohmann::json ToJson() const;
    static PlayerSettings FromJson(const nlohmann::json& json);
};

PlayerSettings LoadPlayerSettings(const std::filesystem::path& file);
bool SavePlayerSettings(const std::filesystem::path& file, const PlayerSettings& settings);

} // namespace aige
