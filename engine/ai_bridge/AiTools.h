#pragma once

#include "ecs/Registry.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace aige {

class AssetDatabase;
class PrefabLibrary;
class ReflectionRegistry;
class WorldBible;
struct Project;

// The AI panel's tools (docs/ai/agent.md): what the model reads or looks at before it answers. Its answer of scope
// "tools" names them; the editor runs each here and asks again with the results. Reading only: nothing in the project
// changes.
struct AiToolContext {
    Registry& registry;
    const ReflectionRegistry& reflection;
    PrefabLibrary& prefabs;
    const AssetDatabase& assets;
    const Project& project;
    const WorldBible* world = nullptr;
    // The editor's own: a picture of the view, a short playtest, the project's problems (empty: not available).
    std::function<nlohmann::json(const nlohmann::json& args)> look;
    std::function<nlohmann::json(const nlohmann::json& args)> playtest;
    std::function<nlohmann::json()> check;
};

// The tool's result ({error} for an unknown tool or bad arguments), kept to a size a prompt can carry.
nlohmann::json RunAiTool(const std::string& tool, const nlohmann::json& args, AiToolContext& context);
// The tools the editor knows, for the prompt and checks.
const std::vector<std::string>& AiToolNames();
// The results as the next message to the model: each tool, its arguments and what came back.
std::string AiToolResultsMessage(const nlohmann::json& calls, const std::vector<nlohmann::json>& results);

} // namespace aige
