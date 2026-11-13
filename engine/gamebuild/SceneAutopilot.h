#pragma once

// ⑬ Test a story scene (development_plan.md 33.4): the autopilot that plays it, a frame at a time, so the same run is
// played headless (GameBuild::RunSceneTest) or watched in the editor's Play. From where the scene begins (the quests at
// their stages, the player at its place) it goes to each objective of the scene's quest stage (in another scene: through
// the game's travel), walking there with the movement keys, and does it as the player would: talks (each conversation
// played through, a choice not yet tried each time), uses, fights (the test ends a fight that goes on), stands there.
// What it did, the game's events, the conversations' lines and the script errors are its log, for the AI to judge.

#include "game/Actors.h"
#include "gameplay/GameSession.h"
#include "platform/Input.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <tuple>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aige {

class StoryLibrary;

class SceneAutopilot {
public:
    // What it plays in: the game, its world and story, who's where (the roster), the scene it's in, and how it goes to
    // another (the editor's travel, or the headless run's).
    struct World {
        GameSession& session;
        Registry& registry;
        const StoryLibrary& story;
        const ActorRoster* roster = nullptr;
        std::function<std::string()> scene;
        std::function<void(const std::string& scene, const std::string& marker)> travel;
    };

    // `start`: GameBuild::TestStart's; `scenes`: the game's scenes (where to look for a target that isn't here).
    SceneAutopilot(nlohmann::json start, std::vector<std::string> scenes, std::filesystem::path projectRoot);

    // A frame, after the game advanced: what it sees and does next. False once it's done.
    bool Tick(World& w, double dt);
    // The keys it holds now (the game reads them in place of the keyboard's).
    bool Holds(Key key) const { return held_.contains(key); }
    const nlohmann::json& Log() const { return log_; }
    // What it's doing, a line for the screen.
    const std::string& Doing() const { return doing_; }
    bool Done() const { return phase_ == Phase::Done; }
    // The quest stage it plays and where the scene is (from its start): Play from here checks the game got there.
    const nlohmann::json& Start() const { return start_; }
    // Stopped before the end (the user stopped Play): said in the log.
    void Stop(const std::string& why);

private:
    enum class Phase { Arrive, Settle, Next, Seek, Walk, Act, Finish, Done };
    struct Objective {
        std::string type, target, what;
    };

    void Step(const std::string& text);
    void Press(const std::string& action, double seconds);
    void HoldMove(World& w, float wx, float wy);
    bool Converse(World& w, double dt); // a conversation on: played through; true while one is
    bool StartObjective(World& w);      // the next one found, a way to it; false: none left
    void LogEvents(World& w);

    nlohmann::json start_;
    std::vector<std::string> scenes_;
    std::filesystem::path root_;
    nlohmann::json log_;
    std::string doing_;
    Phase phase_ = Phase::Arrive;
    double now_ = 0.0, timer_ = 0.0, stuck_ = 0.0, lastDistance_ = 0.0;
    bool isoKeys_ = true; // the movement keys turned into the world's as UpdatePlayerMove does (isometric)
    int corners_ = 0;     // corners it was carried round on the way to this objective
    double walked_ = 0.0; // how long it's been walking to this objective
    std::set<Key> held_;
    std::vector<std::pair<Key, double>> pressing_; // keys held for a while
    // The scene's quest stage and its objectives.
    std::string questId_;
    int stageAt_ = 0;
    std::vector<Objective> objectives_;
    size_t next_ = 0;
    Objective current_;
    Entity targetEntity_ = entt::null;
    float targetX_ = 0.0f, targetY_ = 0.0f;
    std::vector<std::pair<float, float>> path_;
    std::string seeking_; // the scene it travels to for the current objective
    std::set<std::string> searched_;
    int acts_ = 0;
    // Conversations: the choices tried at each line, the lines seen.
    std::map<std::string, std::set<int>> tried_;
    double talkWait_ = 0.0;
    int lines_ = 0;
    // The record (34.4): what the requirements' played checks read.
    std::string recScene_;
    double recAt_ = -10.0, nearAt_ = 0.0;
    std::map<std::string, double> near_; // seconds each one kept within a few cells of the player
    // A snapshot of the cast a moment after each quest stage begins (the cast timeline's played checks): due times.
    std::vector<std::tuple<double, std::string, int>> snapshots_;
    bool startSnapped_ = false;
    void SnapCast(World& w, const std::string& quest, int stage);
    // To another scene on foot: the next gateway on the way (the scenes' gateways, nearest first), walked to and stepped
    // on; false when no way leads there (then it's taken there).
    bool PlanHop(World& w);
    bool travelling_ = false; // walking to a gateway
    bool jumped_ = false;     // taken there (no way on foot)
    std::string hopFrom_;
    void Record(World& w);
};

} // namespace aige
