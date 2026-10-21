#pragma once

#include "assets/Project.h"
#include "ecs/Registry.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aige {

// A project's scenes (development_plan.md 5.3): every scenes/*.aige, one of
// them the start scene (the Game.startScene setting), connected by Gateway
// components. Paths are project-relative ("scenes/cave.aige").

// Every scene, sorted.
std::vector<std::string> ListScenes(const Project& project);

// The start scene (Game.startScene), or scenes/main.aige when unset.
std::string StartScene();
std::filesystem::path StartScenePath(const Project& project);

// "Cave Level" -> "scenes/cave-level.aige"; empty if nothing usable is left.
std::string ScenePathFor(const std::string& name);

// New empty scene / a copy of one. Return the new path, or why not (as the
// error string with an empty path): never over an existing scene.
struct SceneResult {
    std::string path;
    std::string error;
};
SceneResult CreateScene(const Project& project, const std::string& name);
SceneResult DuplicateScene(const Project& project, const std::string& scene, const std::string& name);

// Which files refer to `scene` (gateways in other scenes, the settings).
std::vector<std::string> SceneReferences(const Project& project, const std::string& scene);

// Deletes a scene; refuses the start scene and a scene something refers to.
std::string DeleteScene(const Project& project, const std::string& scene);

// Gateways that lead nowhere: a missing scene, or a marker the target
// scene doesn't have. "scenes/a.aige: Gateway "To the cave" -> ...".
std::vector<std::string> GatewayProblems(const Project& project);

// The marker names in a scene file.
std::vector<std::string> SceneMarkers(const Project& project, const std::string& scene);

// Travel during play: replaces the registry's scene with `sceneText` and
// carries the player over, every component as it is (health, scripts, ...),
// standing at `marker`. The new scene's own player, if it has one, gives
// way (references to it point at the arrival). Returns a warning when the
// marker isn't there (the player then stands where that scene's player
// was, or at its first marker, or at 0.5, 0.5); empty when all went well.
// `scenePath` (optional): where the scene lives, for its tile layers (7.2).
std::string TravelToScene(Registry& registry, const std::string& sceneText, const std::string& marker,
                          const std::filesystem::path& scenePath = {});

// Puts the player entity in `playerText` (scene text, as SerializeEntities
// writes it) in place of the scene's own player, re-pointing references to it.
// The new player, or null if the text held no entity.
Entity ReplacePlayer(Registry& registry, const std::string& playerText);

} // namespace aige
