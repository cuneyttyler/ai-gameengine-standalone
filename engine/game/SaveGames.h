#pragma once

#include "ecs/Registry.h"
#include "game/GameFlow.h"
#include "reflection/Reflection.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aige {

// Saved games v2 (development_plan.md 12.5).
//
// World state: what changed in a scene since it was loaded from its file, so
// a scene the player comes back to (or a loaded game) is as they left it:
// killed enemies stay dead, picked-up things stay gone, moved and changed
// things keep their values, things spawned in play come back. Entities are
// identified by their place in the scene file (SceneOrigin); the player is
// saved on its own.
//
//   {"removed": [3, 17], "changed": {"5": {"Health": {"current": 2}}},
//    "removedComponents": {"9": ["Pickup"]}, "spawned": "<scene text>"}
nlohmann::json CaptureSceneState(Registry& live, Registry& pristine, const ReflectionRegistry& reflection);
void ApplySceneState(Registry& registry, const nlohmann::json& state, const ReflectionRegistry& reflection);

// The save slots: quick, auto, slot1..slot6. A slot is one JSON file,
// <dir>/<slot>.json:
//   {"schema": "aige.save/2", "meta": {slot, time, place, playSeconds, title},
//    "session": GameSession::MakeSave(), "player": "<the player entity as scene text>",
//    "worlds": {"scenes/a.aige": <scene state>, ...}}
// A version-1 quicksave.json (6.9) still loads, as the quick slot.
class SaveGames {
public:
    explicit SaveGames(std::filesystem::path dir) : dir_(std::move(dir)) {}

    static const std::vector<std::string>& SlotNames();
    const std::filesystem::path& Dir() const { return dir_; }

    std::vector<SaveSlotInfo> List() const;
    std::optional<nlohmann::json> Read(const std::string& slot) const;
    // Fills in meta.slot and meta.time; returns false when it can't write.
    bool Write(const std::string& slot, nlohmann::json save) const;
    bool Remove(const std::string& slot) const;

private:
    std::filesystem::path PathOf(const std::string& slot) const;
    std::filesystem::path dir_;
};

// Where a player's saves go: %LOCALAPPDATA%/AIGE/saves/<game>/ (outside the
// game's folder, which may be read-only once installed).
std::filesystem::path PlayerSaveDir(const std::string& gameName);

} // namespace aige
