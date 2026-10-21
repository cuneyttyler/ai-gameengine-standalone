#pragma once

#include "game/PlayerSettings.h"
#include "gameplay/InputMap.h"
#include "ui/GameUi.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aige {

// The game as a whole (development_plan.md 12.4): what the project's game.json
// says about it, beyond the scenes. The AI Planner writes it too.
struct GameManifest {
    std::string title;            // the window and title screen
    std::string subtitle;         // a line under the title
    bool titleScreen = true;      // start at the title screen (else straight into play)
    std::string titleBackground;  // a texture behind the title screen ("" = the start scene)
    std::string titleMusic;       // audio path (12.6)
    std::string combatMusic;      // while an enemy chases the player, unless the scene's Music says (12.6)
    bool autosave = true;         // save to the autosave slot on each scene change
    std::string credits;          // shown by Credits (CREDITS.md is added after it)
    std::string genre;            // game-types.md id, for the AI
    nlohmann::json systems = nlohmann::json::object(); // which systems are on (engine-gaps.md §4.6)

    nlohmann::json ToJson() const;
    static GameManifest FromJson(const nlohmann::json& json);
};

// game.json; defaults (titled after the folder) when there's none.
GameManifest LoadGameManifest(const std::filesystem::path& projectRoot);
bool SaveGameManifest(const std::filesystem::path& projectRoot, const GameManifest& manifest);

// One save slot's summary, for the Load and Save menus.
struct SaveSlotInfo {
    std::string slot;  // quick | auto | slot1..slot6
    bool used = false;
    std::string time;  // when it was saved, local time "2026-10-21 18:40"
    std::string place; // where (the location's or scene's name)
    double playSeconds = 0.0;
};

// The screens around play and how the player moves between them: title, pause,
// load and save, game over, victory, credits. Pure logic: the host feeds it
// input and draws its menus; it says what to do (start a new game, load a slot,
// quit) through commands.
class GameFlow {
public:
    enum class State { Title, Playing, Paused, Load, Save, GameOver, Victory, Credits, Settings, Difficulty };

    struct Command {
        enum class Kind { None, NewGame, Load, Save, Resume, QuitToTitle, Quit, SettingsChanged, DifficultyChanged } kind =
            Kind::None;
        std::string slot;  // Load / Save
        std::string level; // NewGame (when chosen) / DifficultyChanged: a difficulty level's id
    };

    void SetManifest(GameManifest manifest) { manifest_ = std::move(manifest); }
    const GameManifest& Manifest() const { return manifest_; }
    // The slots as they are now (the host refreshes them after each save).
    void SetSlots(std::vector<SaveSlotInfo> slots) { slots_ = std::move(slots); }
    const std::vector<SaveSlotInfo>& Slots() const { return slots_; }
    // The player's settings the Settings screen changes (12.7), and the languages to offer.
    void SetPlayerSettings(PlayerSettings* settings, std::vector<std::string> languages) {
        settings_ = settings;
        languages_ = std::move(languages);
    }

    // Difficulty (16): the levels to offer at New game (with more than one) and in
    // Settings (in play only when `changeAnytime`), the current one, and whether the
    // game offers adaptive difficulty.
    struct DifficultyChoice {
        std::string id;
        std::string name;
        std::string description;
    };
    void SetDifficulties(std::vector<DifficultyChoice> levels, std::string current, bool changeAnytime, bool adaptive) {
        levels_ = std::move(levels);
        currentLevel_ = std::move(current);
        changeAnytime_ = changeAnytime;
        offerAdaptive_ = adaptive;
    }
    const std::string& CurrentDifficulty() const { return currentLevel_; }

    // Title screen, or straight into play (the host then starts a new game).
    Command Begin();
    State Current() const { return state_; }
    bool InPlay() const { return state_ == State::Playing; }

    // What happened in play.
    void OnPlayerDied() { Go(State::GameOver); }
    void OnVictory() { Go(State::Victory); }
    // The "menu" action in play: the pause menu.
    void Pause() { Go(State::Paused); }

    // Menu input for this frame (the "menu" context's ui_up/ui_down/ui_select/
    // ui_back, and "menu" itself to close the pause menu).
    Command HandleInput(const InputState& input);
    // The same, by item index (a mouse click, tests).
    Command Choose(int index);

    // The current screen as a menu to draw; nullopt while playing.
    std::optional<MenuView> View() const;

private:
    struct Item {
        std::string id;
        std::string label;
        bool enabled = true;
    };
    std::vector<Item> Items() const;
    void Go(State state);
    std::string ContinueSlot() const; // the most recent save, if any
    std::string SlotLabel(const SaveSlotInfo& slot) const;

    GameManifest manifest_;
    std::vector<SaveSlotInfo> slots_;
    PlayerSettings* settings_ = nullptr;
    std::vector<std::string> languages_;
    std::vector<DifficultyChoice> levels_;
    std::string currentLevel_;
    bool changeAnytime_ = true;
    bool offerAdaptive_ = false;
    State settingsFrom_ = State::Title;
    State state_ = State::Title;
    State back_ = State::Title; // where Load, Save and Credits go back to
    int highlighted_ = 0;
};

} // namespace aige
