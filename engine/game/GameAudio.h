#pragma once

#include "audio/Audio.h"
#include "ecs/Registry.h"

#include <string>

namespace aige {

class GameSession;
struct GameManifest;
struct Project;

// What should be playing (development_plan.md 12.6), decided each frame from the
// game's state, so the player and the editor's Play sound the same:
// - the title screen's music (title);
// - a script's play_music, while it lasts;
// - combat music while an enemy is chasing the player (the scene's Music.combat,
//   else the game's);
// - otherwise the scene's Music track and ambience;
// music and ambience ducked while someone talks; the listener at the player.
struct AudioDirection {
    std::string music;
    std::string ambience;
    float ducking = 1.0f;
    bool combat = false;
};

AudioDirection DirectAudio(Registry& registry, const GameSession& session, const GameManifest& manifest, bool title);

// Applies a direction to the engine (paths project-relative), and runs its fades.
void ApplyAudio(AudioEngine& audio, const Project& project, const AudioDirection& direction, Registry& registry,
                float seconds);

} // namespace aige
