#include "ai_bridge/DryRun.h"

#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/GameSession.h"
#include "scripting/LuaVM.h"

#include <algorithm>

namespace aige {

std::vector<ScriptProblem> DryRunScripts(const std::string& sceneText, const Project& project, double seconds,
                                         const std::vector<std::string>& onlyScripts) {
    // lua must outlive registry (scripts hold environments in its state).
    LuaVM lua;
    Registry registry;
    DeserializeScene(registry, sceneText);
    GameSession session(registry, lua, BootstrapIso(), project.root);

    auto noInput = [](Key) { return false; };
    for (double elapsed = 0.0; elapsed < seconds; elapsed += GameSession::kFixedDt) {
        session.Advance(GameSession::kFixedDt, noInput);
    }

    std::vector<ScriptProblem> problems;
    for (const auto& error : session.ScriptErrors()) {
        if (!onlyScripts.empty() &&
            std::find(onlyScripts.begin(), onlyScripts.end(), error.path) == onlyScripts.end()) {
            continue;
        }
        auto same = std::find_if(problems.begin(), problems.end(), [&](const ScriptProblem& p) {
            return p.path == error.path && p.message == error.message;
        });
        if (same != problems.end()) {
            ++same->count;
        } else {
            problems.push_back({error.path, error.message, 1});
        }
    }
    return problems;
}

} // namespace aige
