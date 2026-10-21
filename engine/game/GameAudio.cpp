#include "game/GameAudio.h"

#include "assets/Project.h"
#include "game/GameFlow.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "physics2d/Components.h"

#include <cmath>

namespace aige {

AudioDirection DirectAudio(Registry& registry, const GameSession& session, const GameManifest& manifest, bool title) {
    AudioDirection direction;
    if (title) {
        direction.music = manifest.titleMusic;
        return direction;
    }
    const Music* scene = nullptr;
    for (auto [entity, music] : registry.view<Music>().each()) {
        scene = &music;
        break;
    }
    direction.ambience = scene ? scene->ambience : std::string();

    // An enemy chasing the player: one that follows it and has it in range.
    const auto player = FindPlayer(registry);
    if (player && registry.all_of<WorldTransform2D>(*player)) {
        const auto& at = registry.get<WorldTransform2D>(*player);
        for (auto [entity, follow, faction, world] : registry.view<Follow, Faction, WorldTransform2D>().each()) {
            if (faction.team != Team::Enemy || registry.all_of<Dead>(entity)) continue;
            if (follow.target != entt::null && follow.target != *player) continue;
            const float dx = world.x - at.x, dy = world.y - at.y;
            if (follow.range <= 0.0f || std::sqrt(dx * dx + dy * dy) <= follow.range) {
                direction.combat = true;
                break;
            }
        }
    }
    if (!session.MusicOverride().empty()) {
        direction.music = session.MusicOverride();
    } else if (direction.combat && ((scene && !scene->combat.empty()) || !manifest.combatMusic.empty())) {
        direction.music = scene && !scene->combat.empty() ? scene->combat : manifest.combatMusic;
    } else {
        direction.music = scene ? scene->track : std::string();
    }
    if (session.Conversation().Active()) direction.ducking = 0.35f;
    return direction;
}

void ApplyAudio(AudioEngine& audio, const Project& project, const AudioDirection& direction, Registry& registry,
                float seconds) {
    audio.PlayMusic(direction.music.empty() ? std::filesystem::path() : project.Resolve(direction.music));
    audio.PlayAmbience(direction.ambience.empty() ? std::filesystem::path() : project.Resolve(direction.ambience));
    audio.SetDucking(direction.ducking);
    if (auto player = FindPlayer(registry); player && registry.all_of<WorldTransform2D>(*player)) {
        const auto& at = registry.get<WorldTransform2D>(*player);
        audio.SetListener(at.x, at.y);
    }
    audio.Update(seconds);
}

} // namespace aige
