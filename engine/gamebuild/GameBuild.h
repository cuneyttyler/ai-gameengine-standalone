#pragma once

#include "ecs/Registry.h"
#include "editing/PlaceBuild.h"
#include "planner/PlannerStore.h"
#include "story/StoryGraph.h"

#include <nlohmann/json.hpp>

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aige {

class Ontology;

// The AI Planner, building the game it designed (development_plan.md 23, docs/ai/ai-planner.md
// §5.10-5.13, §8-§11). It starts from the plan's baseline:
//
//   build      ⑩ the build plan (planner/buildplan.json): one row per place, parents first;
//              each place built through a PlaceBuild started from its draft Brief, driven
//              automatically (Confirm, Plan, Approve: each continue recorded as the
//              planner's) or by the user in the Build panel (manual rows); the solved
//              layout becomes the place's scene with its story anchors as markers; a
//              build that fails gets a placeholder place; then the world graph's
//              connections become gateways with arrival markers on both sides
//   populate   ⑪ characters placed in their homes (name, talk, behaviour), quest aliases
//              bound — fixed, by story anchor, by query among the scene's markers, or made
//              — the boss and quest items placed, reach objectives given markers
//              (planner/bindings.json)
//   write      ⑫ the game's files: quests (quests v2), dialogue trees (the AI, against what
//              each speaker knows; validated), cutscenes from cutscene scenes, records,
//              packages, the theme, game.json; a manifest with each file's provenance
//   playtest   ⑫ every scene loads, gateways lead somewhere, quests and dialogues
//              validate, every quest walked: each objective's target exists in a scene
//              reachable through the gateways and is reachable on foot there
//   result     ⑬ planner/result.json: the headline, asked vs made, places, quests,
//              checks, placeholders, what next
//
// The heavy steps (planning, solving, building scenes) run in Pump(), which the editor
// calls off the UI thread. State is in planner/buildstate.json, so it resumes.
class GameBuild {
public:
    struct Host {
        std::function<int(const std::string& path, const nlohmann::json& body, int timeoutSeconds)> request;
        std::filesystem::path projectRoot;
        std::function<const Ontology&()> places;
        // The editor builds a place as the Build panel does (its art found and made, its scene built,
        // checked and saved to `scene`): it starts and returns; editorBuildStatus says how it goes
        // ("working", "built", "failed: why"). Without them the scene is built here, with the art
        // the project has.
        std::function<std::string(const std::string& buildId, const std::string& scene)> buildInEditor;
        std::function<std::string(const std::string& buildId)> editorBuildStatus;
    };

    explicit GameBuild(Host host);
    ~GameBuild();

    // Starts building from the plan's baseline. `mode`: automatic | manual (every row) |
    // the Brief's plan.building. Returns why not, or "".
    std::string Start(const std::string& mode = {});
    std::string Open(); // resumes planner/buildstate.json
    // Stops building: nothing more is started; a place being built goes back to waiting. Start
    // (Restart builds) builds every place again; Resume (Continue building) goes on from the next
    // place that isn't built, keeping those that are.
    void Stop();
    std::string Resume();
    bool Active() const { return !stage_.empty(); }
    const std::string& Stage() const { return stage_; }
    const std::string& Status() const { return status_; }

    bool Owns(int requestId) const;
    void OnResponse(int requestId, const nlohmann::json& body);
    // Moves everything on as far as it can without waiting: drives builds, builds scenes,
    // binds, writes, plays. Heavy: the editor runs it off the UI thread. True when it did
    // something.
    bool Pump();

    // ⑩ rows: switch a row to manual or automatic; try a failed row again; give it a
    // placeholder place; a manual row's build (for the Build panel), and its completion.
    std::string SetMode(const std::string& place, const std::string& mode);
    std::string Retry(const std::string& place);
    // Builds the place again from the start (a new build of it, built or not); the game build goes
    // on with it, then what follows (binding, files, playtest) again.
    std::string Rebuild(const std::string& place);
    std::string UsePlaceholder(const std::string& place);
    // A place built outside the game build (the build test, docs/build-fixes.md), its scene already
    // saved at the row's scene path: taken in as the Build panel's are (story markers added, the row
    // built, its file recorded). The rest of the game build goes on from there.
    std::string InstallBuilt(const std::string& place, const std::string& buildId);
    PlaceBuild* BuildOf(const std::string& place);
    nlohmann::json StoryFor(const std::string& place) const;
    // The place's story anchors, for a painting to show and find ([{id, about}]: BackdropRequests).
    nlohmann::json StoryAnchors(const std::string& place) const;
    std::string ManualDone(const std::string& place); // the user built it in the Build panel

    nlohmann::json State() const;
    nlohmann::json Card() const;

private:
    struct Row {
        std::string place, scene, buildType, prompt, mode = "automatic", status = "waiting", build, error;
        std::vector<std::string> dependsOn;
        nlohmann::json brief = nlohmann::json::object();
        nlohmann::json checks = nlohmann::json::object();
        int steps = 0;      // drive steps taken (a stuck build is stopped)
        std::string lastStage;
        bool inEditor = false; // handed to the editor's Build panel: waiting for it to finish
        bool resumeInEditor = false; // reopened past its plan: handed back to the Build panel to finish
    };
    // The story's markers (its anchors, where one arrives, the player in the first scene) on a built
    // place's scene; the number of anchors that had no part showing them.
    int AddStoryMarkers(Registry& registry, const Row& row, const nlohmann::json& solved, int originX, int originY);
    // The editor finished a place: its scene gets its markers, and the row is built.
    void FinishInEditor(Row& row);

    struct Job {
        std::string id, kind, subject, status = "queued", error;
        int attempts = 0;
        nlohmann::json problems = nlohmann::json::array();
    };

    Host host_;
    PlannerStore store_;
    std::string stage_, status_, message_, mode_;
    std::vector<Row> rows_;
    std::deque<Job> jobs_;
    std::map<int, std::string> pending_;            // request id -> "job:<id>" | "build:<place>"
    std::map<std::string, std::unique_ptr<PlaceBuild>> builds_; // by place
    std::map<std::string, std::vector<std::pair<int, nlohmann::json>>> buildAnswers_; // answers waiting for Pump
    std::vector<std::pair<std::string, nlohmann::json>> jobAnswers_;                    // job id, answer
    nlohmann::json bindings_ = nlohmann::json::object();
    nlohmann::json files_ = nlohmann::json::object();   // path -> provenance
    nlohmann::json playtest_ = nlohmann::json::object();
    int nextJob_ = 1;

    void Save();
    void Load();
    Row* FindRow(const std::string& place);
    std::string StartScene() const;
    PlaceBuild::Host BuildHost(const std::string& place);

    // ⑩
    void MakeBuildPlan();
    bool DriveBuilds();
    bool DriveRow(Row& row);
    std::string BuildScene(Row& row, PlaceBuild& build);
    std::string PlaceholderScene(Row& row);
    void ConnectScenes();
    // ⑪
    void Populate();
    // ⑫
    void QueueWriting();
    bool DriveJobs();
    std::vector<std::string> HandleJob(Job& job, const nlohmann::json& answer);
    void WriteFiles();
    void WriteDialogue(const std::string& character, const nlohmann::json& dialogue, const std::string& madeBy);
    nlohmann::json FallbackDialogue(const std::string& character) const;
    void Playtest();
    // ⑬
    void WriteResult();

    void Next(const std::string& stage);
    void Record(const std::string& kind, const std::string& subject, const std::string& chosen, const std::string& reason = {});
};

// The planner's quest design as the engine's quest file (quests v2): stages with objectives
// whose targets are the quest's aliases, the aliases filled from the bindings.
nlohmann::json EngineQuestFromDesign(const nlohmann::json& design, const nlohmann::json& bindings);

} // namespace aige
