#include "game/GameFlow.h"

#include "gameplay/Localization.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace aige {

namespace fs = std::filesystem;

nlohmann::json GameManifest::ToJson() const {
    return {{"schema", "aige.game/1"},     {"title", title},
            {"subtitle", subtitle},        {"titleScreen", titleScreen},
            {"titleBackground", titleBackground}, {"titleMusic", titleMusic},
            {"combatMusic", combatMusic},
            {"autosave", autosave},        {"credits", credits},
            {"genre", genre},              {"systems", systems}};
}

GameManifest GameManifest::FromJson(const nlohmann::json& json) {
    GameManifest m;
    if (!json.is_object()) return m;
    m.title = json.value("title", "");
    m.subtitle = json.value("subtitle", "");
    m.titleScreen = json.value("titleScreen", true);
    m.titleBackground = json.value("titleBackground", "");
    m.titleMusic = json.value("titleMusic", "");
    m.combatMusic = json.value("combatMusic", "");
    m.autosave = json.value("autosave", true);
    m.credits = json.value("credits", "");
    m.genre = json.value("genre", "");
    if (json.contains("systems") && json["systems"].is_object()) m.systems = json["systems"];
    return m;
}

GameManifest LoadGameManifest(const fs::path& projectRoot) {
    GameManifest manifest;
    std::ifstream in(projectRoot / "game.json");
    if (in) manifest = GameManifest::FromJson(nlohmann::json::parse(in, nullptr, false));
    if (manifest.title.empty()) manifest.title = projectRoot.filename().string();
    return manifest;
}

bool SaveGameManifest(const fs::path& projectRoot, const GameManifest& manifest) {
    std::ofstream out(projectRoot / "game.json", std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << manifest.ToJson().dump(2) << "\n";
    return static_cast<bool>(out);
}

GameFlow::Command GameFlow::Begin() {
    if (manifest_.titleScreen) {
        Go(State::Title);
        return {};
    }
    Go(State::Playing);
    return {Command::Kind::NewGame};
}

void GameFlow::Go(State state) {
    auto sub = [](State s) {
        return s == State::Load || s == State::Save || s == State::Credits || s == State::Settings || s == State::Difficulty;
    };
    if (sub(state) && !sub(state_)) back_ = state_;
    state_ = state;
    highlighted_ = 0;
    // Start on the first item that can be picked.
    const auto items = Items();
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].enabled) {
            highlighted_ = static_cast<int>(i);
            break;
        }
    }
}

std::string GameFlow::ContinueSlot() const {
    const SaveSlotInfo* best = nullptr;
    for (const auto& slot : slots_) {
        if (slot.used && (!best || slot.time > best->time)) best = &slot;
    }
    return best ? best->slot : std::string();
}

std::string GameFlow::SlotLabel(const SaveSlotInfo& slot) const {
    std::string name = slot.slot == "quick" ? "Quick save" : slot.slot == "auto" ? "Autosave" : "Slot " + slot.slot.substr(4);
    if (!slot.used) return name + "  (empty)";
    const int minutes = static_cast<int>(slot.playSeconds / 60.0);
    std::string played = std::to_string(minutes / 60) + "h " + std::to_string(minutes % 60) + "m";
    return name + "  " + slot.place + "  " + slot.time + "  " + played;
}

std::vector<GameFlow::Item> GameFlow::Items() const {
    const bool anySave = !ContinueSlot().empty();
    switch (state_) {
        case State::Title:
            return {{"continue", Tr("menu.continue", "Continue"), anySave},
                    {"new", Tr("menu.new_game", "New game")},
                    {"load", Tr("menu.load", "Load game"), anySave},
                    {"settings", Tr("menu.settings", "Settings"), settings_ != nullptr},
                    {"credits", Tr("menu.credits", "Credits")},
                    {"quit", Tr("menu.quit", "Quit")}};
        case State::Paused:
            return {{"resume", Tr("menu.resume", "Resume")},
                    {"save", Tr("menu.save", "Save game")},
                    {"load", Tr("menu.load", "Load game"), anySave},
                    {"settings", Tr("menu.settings", "Settings"), settings_ != nullptr},
                    {"title", Tr("menu.quit_to_title", "Quit to title")},
                    {"quit", Tr("menu.quit_game", "Quit game")}};
        case State::Load: {
            std::vector<Item> items;
            for (const auto& slot : slots_) {
                if (slot.used) items.push_back({"slot:" + slot.slot, SlotLabel(slot)});
            }
            items.push_back({"back", Tr("menu.back", "Back")});
            return items;
        }
        case State::Save: {
            std::vector<Item> items;
            for (const auto& slot : slots_) {
                if (slot.slot == "auto" || slot.slot == "quick") continue; // written by the game itself
                items.push_back({"slot:" + slot.slot, SlotLabel(slot)});
            }
            items.push_back({"back", Tr("menu.back", "Back")});
            return items;
        }
        case State::GameOver:
            return {{"continue", Tr("menu.load_last", "Load last save"), anySave},
                    {"new", Tr("menu.start_again", "Start again")},
                    {"title", Tr("menu.quit_to_title", "Quit to title")}};
        case State::Victory:
            return {{"new", Tr("menu.play_again", "Play again")}, {"title", Tr("menu.quit_to_title", "Quit to title")}};
        case State::Credits:
            return {{"back", Tr("menu.back", "Back")}};
        case State::Difficulty: {
            std::vector<Item> items;
            for (const auto& level : levels_) {
                items.push_back({"level:" + level.id, Tr("difficulty." + level.id + ".name", level.name)});
            }
            items.push_back({"back", Tr("menu.back", "Back")});
            return items;
        }
        case State::Settings: {
            std::vector<Item> items;
            if (!settings_) return {{"back", Tr("menu.back", "Back")}};
            auto percent = [](float v) { return std::to_string(static_cast<int>(std::lround(v * 100.0f))) + "%"; };
            auto onOff = [](bool v) { return v ? Tr("menu.on", "On") : Tr("menu.off", "Off"); };
            items.push_back({"set:master", Tr("settings.master", "Master volume") + "  " + percent(settings_->master)});
            items.push_back({"set:music", Tr("settings.music", "Music volume") + "  " + percent(settings_->music)});
            items.push_back({"set:effects", Tr("settings.effects", "Effects volume") + "  " + percent(settings_->effects)});
            items.push_back({"set:voice", Tr("settings.voice", "Voice volume") + "  " + percent(settings_->voice)});
            items.push_back({"set:ambience", Tr("settings.ambience", "Ambience volume") + "  " + percent(settings_->ambience)});
            if (!languages_.empty()) {
                const std::string language = settings_->language.empty() ? Tr("settings.default_language", "Game's own")
                                                                          : settings_->language;
                items.push_back({"set:language", Tr("settings.language", "Language") + "  " + language});
            }
            items.push_back({"set:fullscreen", Tr("settings.fullscreen", "Fullscreen") + "  " + onOff(settings_->fullscreen)});
            items.push_back({"set:subtitles", Tr("settings.subtitles", "Subtitles") + "  " + onOff(settings_->subtitles)});
            // Difficulty and assists (16.4): offered plainly, without judgement.
            if (levels_.size() > 1 && (changeAnytime_ || settingsFrom_ == State::Title)) {
                std::string name = currentLevel_;
                for (const auto& level : levels_) {
                    if (level.id == currentLevel_) name = Tr("difficulty." + level.id + ".name", level.name);
                }
                items.push_back({"set:difficulty", Tr("settings.difficulty", "Difficulty") + "  " + name});
            }
            if (offerAdaptive_) {
                items.push_back({"set:adaptive", Tr("settings.adaptive", "Adjust difficulty to how I play") + "  " +
                                                     onOff(settings_->adaptive)});
            }
            items.push_back({"set:reduction", Tr("settings.damage_reduction", "Damage reduction") + "  " +
                                                  percent(settings_->damageReduction)});
            items.push_back({"set:growing", Tr("settings.growing_reduction", "More protection after each death") + "  " +
                                                onOff(settings_->growingReduction)});
            items.push_back({"set:speed", Tr("settings.game_speed", "Game speed") + "  " + percent(settings_->gameSpeed)});
            items.push_back({"set:invincible", Tr("settings.invincible", "Invincible") + "  " + onOff(settings_->invincible)});
            items.push_back({"back", Tr("menu.back", "Back")});
            return items;
        }
        case State::Playing:
            return {};
    }
    return {};
}

GameFlow::Command GameFlow::Choose(int index) {
    const auto items = Items();
    if (index < 0 || index >= static_cast<int>(items.size()) || !items[index].enabled) return {};
    const std::string& id = items[index].id;
    if (id == "continue") {
        const std::string slot = ContinueSlot();
        Go(State::Playing);
        return {Command::Kind::Load, slot};
    }
    if (id == "new") {
        if (levels_.size() > 1) {
            Go(State::Difficulty); // choose the level first
            return {};
        }
        Go(State::Playing);
        return {Command::Kind::NewGame};
    }
    if (id.rfind("level:", 0) == 0) {
        currentLevel_ = id.substr(6);
        Go(State::Playing);
        return {Command::Kind::NewGame, "", currentLevel_};
    }
    if (id == "load") {
        Go(State::Load);
        return {};
    }
    if (id == "save") {
        Go(State::Save);
        return {};
    }
    if (id == "credits") {
        Go(State::Credits);
        return {};
    }
    if (id == "settings") {
        settingsFrom_ = state_;
        Go(State::Settings);
        return {};
    }
    if (id.rfind("set:", 0) == 0 && settings_) {
        // Each pick steps the value: volumes by 10% (wrapping to 0), switches over, languages round.
        const std::string what = id.substr(4);
        auto step = [](float& v) { v = v >= 0.999f ? 0.0f : std::min(1.0f, std::round(v * 10.0f + 1.0f) / 10.0f); };
        if (what == "master") step(settings_->master);
        else if (what == "music") step(settings_->music);
        else if (what == "effects") step(settings_->effects);
        else if (what == "voice") step(settings_->voice);
        else if (what == "ambience") step(settings_->ambience);
        else if (what == "fullscreen") settings_->fullscreen = !settings_->fullscreen;
        else if (what == "subtitles") settings_->subtitles = !settings_->subtitles;
        else if (what == "invincible") settings_->invincible = !settings_->invincible;
        else if (what == "growing") settings_->growingReduction = !settings_->growingReduction;
        else if (what == "adaptive") settings_->adaptive = !settings_->adaptive;
        else if (what == "reduction") {
            settings_->damageReduction = settings_->damageReduction >= 0.74f ? 0.0f : settings_->damageReduction + 0.25f;
        } else if (what == "speed") {
            settings_->gameSpeed = settings_->gameSpeed <= 0.51f ? 1.0f : settings_->gameSpeed - 0.25f;
        } else if (what == "difficulty" && !levels_.empty()) {
            auto it = std::find_if(levels_.begin(), levels_.end(), [&](const DifficultyChoice& l) { return l.id == currentLevel_; });
            currentLevel_ = it == levels_.end() || it + 1 == levels_.end() ? levels_.front().id : (it + 1)->id;
            return {Command::Kind::DifficultyChanged, "", currentLevel_};
        }
        else if (what == "language") {
            std::vector<std::string> options = {""};
            options.insert(options.end(), languages_.begin(), languages_.end());
            auto it = std::find(options.begin(), options.end(), settings_->language);
            settings_->language = it == options.end() || it + 1 == options.end() ? options.front() : *(it + 1);
        }
        return {Command::Kind::SettingsChanged};
    }
    if (id == "resume") {
        Go(State::Playing);
        return {Command::Kind::Resume};
    }
    if (id == "title") {
        Go(State::Title);
        return {Command::Kind::QuitToTitle};
    }
    if (id == "quit") return {Command::Kind::Quit};
    if (id == "back") {
        Go(back_);
        return {};
    }
    if (id.rfind("slot:", 0) == 0) {
        const std::string slot = id.substr(5);
        if (state_ == State::Save) {
            Go(back_); // back to the pause menu, the game saved
            return {Command::Kind::Save, slot};
        }
        Go(State::Playing);
        return {Command::Kind::Load, slot};
    }
    return {};
}

GameFlow::Command GameFlow::HandleInput(const InputState& input) {
    if (state_ == State::Playing) return {};
    const auto items = Items();
    const int count = static_cast<int>(items.size());
    if (count == 0) return {};
    auto step = [&](int direction) {
        for (int i = 0; i < count; ++i) {
            highlighted_ = (highlighted_ + direction + count) % count;
            if (items[highlighted_].enabled) break;
        }
    };
    if (input.Pressed("ui_up")) step(-1);
    if (input.Pressed("ui_down")) step(1);
    if (input.Pressed("ui_select")) return Choose(highlighted_);
    const bool back = input.Pressed("ui_back") || input.Pressed("menu");
    if (back) {
        if (state_ == State::Paused) {
            Go(State::Playing);
            return {Command::Kind::Resume};
        }
        if (state_ == State::Load || state_ == State::Save || state_ == State::Credits || state_ == State::Settings ||
            state_ == State::Difficulty) {
            Go(back_);
        }
    }
    return {};
}

std::optional<MenuView> GameFlow::View() const {
    if (state_ == State::Playing) return std::nullopt;
    MenuView view;
    switch (state_) {
        case State::Title:
            view.title = manifest_.title;
            view.subtitle = manifest_.subtitle;
            view.dimBackground = false;
            break;
        case State::Paused: view.title = Tr("menu.paused", "Paused"); break;
        case State::Load: view.title = Tr("menu.load", "Load game"); break;
        case State::Save: view.title = Tr("menu.save", "Save game"); break;
        case State::GameOver:
            view.title = Tr("menu.you_died", "You died");
            break;
        case State::Victory:
            view.title = Tr("menu.victory", "Victory!");
            view.subtitle = Tr("menu.victory_line", "All enemies defeated");
            break;
        case State::Credits:
            view.title = Tr("menu.credits", "Credits");
            view.body = manifest_.credits;
            break;
        case State::Settings: view.title = Tr("menu.settings", "Settings"); break;
        case State::Difficulty: {
            view.title = Tr("menu.difficulty", "Choose a difficulty");
            const int index = highlighted_;
            if (index >= 0 && index < static_cast<int>(levels_.size())) {
                view.body = Tr("difficulty." + levels_[index].id + ".description", levels_[index].description);
            }
            break;
        }
        case State::Playing: break;
    }
    for (const auto& item : Items()) view.items.push_back({item.label, item.enabled});
    view.highlighted = highlighted_;
    view.footer = Tr("menu.footer", "Up/Down to choose, Enter to pick, Esc to go back");
    return view;
}

} // namespace aige
