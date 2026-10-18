#pragma once

#include "gameplay/Story.h"
#include "world/WorldBible.h"

#include <map>
#include <string>
#include <vector>

namespace aige {

// The checks StoryLibrary::Validate can't make on its own: references from
// dialogue and quests to the rest of the project. Speakers, givers and talk
// targets written as World Bible keys ("character/villager") must exist;
// defeat/collect targets must be prefabs in `prefabPaths` or a team name
// (Enemy, Player, Neutral). Plain speaker names ("A guard") are allowed.
// With `sceneMarkers` (each scene of the project and its Marker names), an
// objective's scene must exist, and a reach objective's marker must be in
// that scene (or, without a scene, in some scene).
using SceneMarkerMap = std::map<std::string, std::vector<std::string>>;
std::vector<StoryLibrary::Problem> ValidateStoryReferences(const StoryLibrary& story, const WorldBible& world,
                                                           const std::vector<std::string>& prefabPaths,
                                                           const SceneMarkerMap* sceneMarkers = nullptr);

} // namespace aige
