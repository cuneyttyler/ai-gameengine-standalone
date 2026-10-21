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

    nlohmann::json ToJson() const;
    static PlayerSettings FromJson(const nlohmann::json& json);
};

PlayerSettings LoadPlayerSettings(const std::filesystem::path& file);
bool SavePlayerSettings(const std::filesystem::path& file, const PlayerSettings& settings);

} // namespace aige
