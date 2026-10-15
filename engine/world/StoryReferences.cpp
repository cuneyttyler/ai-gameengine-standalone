#include "world/StoryReferences.h"

#include <algorithm>

namespace aige {

std::vector<StoryLibrary::Problem> ValidateStoryReferences(const StoryLibrary& story, const WorldBible& world,
                                                           const std::vector<std::string>& prefabPaths) {
    std::vector<StoryLibrary::Problem> problems;
    // A key is "<kind>/<id>"; plain names have no slash.
    auto checkCharacter = [&](const std::string& file, const std::string& where, const std::string& who) {
        if (who.empty() || who == "player" || who.find('/') == std::string::npos) {
            return;
        }
        if (world.Find(who) == nullptr) {
            problems.push_back({file, where + ": " + who + " isn't in the World Bible"});
        }
    };
    auto isPrefab = [&](const std::string& path) {
        return std::find(prefabPaths.begin(), prefabPaths.end(), path) != prefabPaths.end();
    };

    for (const auto& dialogue : story.Dialogues()) {
        checkCharacter(dialogue.path, "speaker", dialogue.speaker);
        for (const auto& node : dialogue.nodes) {
            checkCharacter(dialogue.path, "node '" + node.id + "' speaker", node.speaker);
        }
    }
    for (const auto& quest : story.Quests()) {
        const std::string file = "quests/" + quest.id + ".json";
        checkCharacter(file, "giver", quest.giver);
        for (const auto& objective : quest.objectives) {
            const std::string where = "objective '" + objective.id + "'";
            if (objective.type == "talk") {
                if (world.Find(objective.target) == nullptr) {
                    problems.push_back({file, where + ": " + objective.target + " isn't in the World Bible"});
                }
            } else if (objective.type == "defeat" || objective.type == "collect") {
                bool team = objective.type == "defeat" &&
                            (objective.target == "Enemy" || objective.target == "Player" || objective.target == "Neutral");
                if (!team && !isPrefab(objective.target)) {
                    problems.push_back({file, where + ": " + objective.target + " isn't a prefab" +
                                                  (objective.type == "defeat" ? " or a team (Enemy)" : "")});
                }
            }
        }
    }
    return problems;
}

} // namespace aige
