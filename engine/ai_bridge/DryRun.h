#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace aige {

struct Project;

struct ScriptProblem {
    std::string path;
    std::string message;
    int count = 0; // how many times it happened during the run
};

// Plays a copy of a scene (SerializeScene text) headless for `seconds` with
// no input and returns each distinct script error, optionally only for the
// given scripts. The editor runs this after applying an AI proposal that
// writes scripts: a schema-valid proposal can still contain Lua that fails
// at runtime, and catching it here turns "it silently doesn't work" into an
// error the AI can be asked to fix. Errors in code paths the run doesn't
// reach (say, only when health is low) aren't found.
std::vector<ScriptProblem> DryRunScripts(const std::string& sceneText, const Project& project, double seconds = 2.0,
                                         const std::vector<std::string>& onlyScripts = {});

// The AI panel's playtest (docs/ai/agent.md): the scene played headless for `seconds`, pressing the input actions in
// `press` one after another (each held a moment, then let go; "move_x+", "move_y-" walk), and what happened: the
// events, script errors, where the player went, what's in its pockets. As JSON for the model.
nlohmann::json PlaytestScene(const std::string& sceneText, const Project& project, double seconds,
                             const std::vector<std::string>& press);

} // namespace aige
