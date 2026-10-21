#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace aige {

// Cutscenes (development_plan.md 15.5, effects-animation.md §8): cutscenes/<id>.json,
// a timeline of tracks. Each key happens at its time ("at", seconds from the start);
// some last a while ("seconds"). The game carries keys out (GameSession); the player
// here only keeps time and says which keys are due, so it's the same in a test.
//
//   actor     move (to a marker, or x/y; speed), face (a marker, an actor or "player"),
//             show, hide, place (jump to a marker)
//   dialogue  a line: speaker, text, seconds, voice
//   camera    follow (an actor; "player"), move (to a marker or x/y), shake, zoom
//   audio     music (path; "" for the scene's), sound (path)
//   screen    fade_out, fade_in (seconds), title (text, seconds), letterbox (on)
//   logic     do (story effects, as in dialogue), emit (event), wait_input
//   effect    (Phase 18) accepted and skipped
//
// Actors are named the way quests name things: an Actor key, a World Bible key, an
// entity's Name, or "@alias" filled from the quest that started the cutscene.

struct CutsceneKey {
    float at = 0.0f;
    float seconds = 0.0f;
    std::string op;      // the track's verb; dialogue keys: "line"
    nlohmann::json data; // the rest of the key as written
};

struct CutsceneTrack {
    std::string type;  // actor | dialogue | camera | audio | screen | logic | effect
    std::string actor; // actor tracks: who
    std::vector<CutsceneKey> keys;
};

struct Cutscene {
    std::string id; // the file name, cutscenes/<id>.json
    std::string name;
    bool letterbox = true;
    bool skippable = true;
    float length = 0.0f; // 0: until the last key ends
    std::vector<CutsceneTrack> tracks;

    float Length() const;
};

std::optional<Cutscene> CutsceneFromJson(const nlohmann::json& json, const std::string& id, std::string* error);
nlohmann::json CutsceneToJson(const Cutscene& cutscene);
std::vector<std::string> ValidateCutscene(const Cutscene& cutscene);
// cutscenes/<id>.json under a project.
std::optional<Cutscene> LoadCutscene(const std::filesystem::path& projectRoot, const std::string& id, std::string* error);
std::vector<std::string> ListCutscenes(const std::filesystem::path& projectRoot);
// Writes cutscenes/<id>.json, normalized through the reader and writer. Returns why not.
std::optional<std::string> WriteCutsceneFile(const std::filesystem::path& projectRoot, const std::string& id,
                                             const nlohmann::json& content);

// A trigger area: the player coming within `radius` plays the cutscene (once,
// remembered with the story flag "cutscene:<id>" so a save keeps it).
struct CutsceneTrigger {
    std::string cutscene; // its id
    float radius = 1.5f;
    bool once = true;
};

// Plays one cutscene: Update hands over the keys that came due, in time order.
class CutscenePlayer {
public:
    struct Due {
        const CutsceneTrack* track;
        const CutsceneKey* key;
        bool skipping; // jumped to the end: put things in their end state, don't play them
    };
    using FireFn = std::function<void(const Due&)>;

    void Start(Cutscene cutscene);
    bool Active() const { return active_; }
    const Cutscene& Current() const { return cutscene_; }
    float Time() const { return time_; }
    // Advances time (not while waiting for input) and fires what's due.
    void Update(float dt, const FireFn& fire);
    // A wait_input key holds the timeline until Continue.
    bool WaitingForInput() const { return waiting_; }
    void Continue() { waiting_ = false; }
    // Fires every key not yet fired (skipping) and ends.
    void Skip(const FireFn& fire);
    void Stop() { active_ = false; }

private:
    struct Entry {
        size_t track, key;
    };
    Cutscene cutscene_;
    std::vector<Entry> order_; // every key, by time
    size_t next_ = 0;
    float time_ = 0.0f;
    bool active_ = false;
    bool waiting_ = false;
};

} // namespace aige
