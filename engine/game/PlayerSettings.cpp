#include "game/PlayerSettings.h"

#include <algorithm>
#include <fstream>

namespace aige {

nlohmann::json PlayerSettings::ToJson() const {
    return {{"schema", "aige.player-settings/1"},
            {"volume", {{"master", master}, {"music", music}, {"effects", effects}, {"voice", voice}, {"ambience", ambience}, {"ui", ui}}},
            {"language", language},
            {"fullscreen", fullscreen},
            {"vsync", vsync},
            {"uiScale", uiScale},
            {"subtitles", subtitles},
            {"reduceMotion", reduceMotion},
            {"reduceFlashes", reduceFlashes},
            {"highContrast", highContrast},
            {"colorblind", colorblind},
            {"textScale", textScale},
            {"holdToToggle", holdToToggle},
            {"assists", {{"invincible", invincible}, {"damageReduction", damageReduction},
                         {"growingReduction", growingReduction}, {"gameSpeed", gameSpeed}, {"adaptive", adaptive}}}};
}

PlayerSettings PlayerSettings::FromJson(const nlohmann::json& json) {
    PlayerSettings s;
    if (!json.is_object()) return s;
    const nlohmann::json volume = json.value("volume", nlohmann::json::object());
    auto level = [&](const char* name, float fallback) { return std::clamp(volume.value(name, fallback), 0.0f, 1.0f); };
    s.master = level("master", s.master);
    s.music = level("music", s.music);
    s.effects = level("effects", s.effects);
    s.voice = level("voice", s.voice);
    s.ambience = level("ambience", s.ambience);
    s.ui = level("ui", s.ui);
    s.language = json.value("language", "");
    s.fullscreen = json.value("fullscreen", false);
    s.vsync = json.value("vsync", true);
    s.uiScale = std::clamp(json.value("uiScale", 1.0f), 0.5f, 2.0f);
    s.subtitles = json.value("subtitles", true);
    s.reduceMotion = json.value("reduceMotion", false);
    s.reduceFlashes = json.value("reduceFlashes", false);
    s.highContrast = json.value("highContrast", false);
    s.colorblind = json.value("colorblind", "none");
    s.textScale = std::clamp(json.value("textScale", 1.0f), 1.0f, 2.0f);
    s.holdToToggle = json.value("holdToToggle", false);
    const nlohmann::json assists = json.value("assists", nlohmann::json::object());
    if (assists.is_object()) {
        s.invincible = assists.value("invincible", false);
        s.damageReduction = std::clamp(assists.value("damageReduction", 0.0f), 0.0f, 0.9f);
        s.growingReduction = assists.value("growingReduction", false);
        s.gameSpeed = std::clamp(assists.value("gameSpeed", 1.0f), 0.25f, 1.0f);
        s.adaptive = assists.value("adaptive", false);
    }
    return s;
}

PlayerSettings LoadPlayerSettings(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    return PlayerSettings::FromJson(nlohmann::json::parse(in, nullptr, false));
}

bool SavePlayerSettings(const std::filesystem::path& file, const PlayerSettings& settings) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << settings.ToJson().dump(2) << "\n";
    return static_cast<bool>(out);
}

} // namespace aige
