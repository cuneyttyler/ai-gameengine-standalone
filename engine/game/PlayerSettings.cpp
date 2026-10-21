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
            {"subtitles", subtitles}};
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
