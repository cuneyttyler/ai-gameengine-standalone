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
//   actor     move (to a marker, or x/y; speed, or seconds: it takes that long; path: round what's in the
//             way), face (a marker, an actor or "player"), show, hide, place (jump to a marker)
//   dialogue  a line: speaker, text, seconds, voice; talk (with: a character): their conversation,
//             the player choosing; the timeline waits for it to end
//   camera    follow (an actor; "player"), move (to a marker or x/y; seconds: a pan that long), shake, zoom
//   audio     music (path; "" for the scene's), sound (path)
//   screen    fade_out, fade_in (seconds), title (text, seconds), letterbox (on)
//   logic     do (story effects, as in dialogue), emit (event), wait_input, wait_until (until: {moves},
//             {talk}, {event}, {flag}; max seconds): the timeline holds until it's so
//
// "world": "waits" (the default: nothing else moves) or "runs" (the world goes on around it; the player
// still only watches).
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
    bool worldRuns = false; // "world": "runs"
    // "ambient": true — a small scene (docs/systems/story.md: small scenes): it plays round the player, who keeps control;
    // its lines are said over the speakers' heads, its camera and screen keys are skipped, and it runs beside a cutscene
    // (one small scene at a time). Its cast is held from their packages while it plays.
    bool ambient = false;
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
    std::string conditions; // JSON, as dialogue's: it plays only while they hold (a quest at its stage)
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
    // A wait_input, wait_until or talk key holds the timeline until Continue.
    bool WaitingForInput() const { return waiting_ && waitKey_ && waitKey_->op == "wait_input"; }
    bool Waiting() const { return waiting_; }
    const CutsceneKey* WaitKey() const { return waiting_ ? waitKey_ : nullptr; }
    void Continue() { waiting_ = false; waitKey_ = nullptr; }
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
    const CutsceneKey* waitKey_ = nullptr;
};

} // namespace aige
