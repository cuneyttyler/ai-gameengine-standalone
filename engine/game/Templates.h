#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aige {

// A template project per supported game type (development_plan.md 24, game-types.md): a new
// project that already plays as its genre — the grid, a starter scene with the player, a
// character to talk to with a dialogue and a first quest, what the genre needs (an enemy and
// a potion for action games, a field and a shopkeeper for a life-sim), records, achievements,
// a theme and game.json.
struct GameTemplate {
    std::string id, name, description;
    bool topDown = false;
    std::string theme;
};
const std::vector<GameTemplate>& GameTemplates(); // arpg, rpg, action-adventure, life-sim
const GameTemplate* FindGameTemplate(const std::string& id);

// Creates the project at `root` (empty or new). Returns why not.
std::optional<std::string> CreateProjectFromTemplate(const std::filesystem::path& root, const std::string& templateId);

} // namespace aige
