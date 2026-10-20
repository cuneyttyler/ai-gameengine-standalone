#pragma once

#include "assets/Catalog.h"
#include "places/Brief.h"
#include "places/Ontology.h"
#include "places/PlaceValidate.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace aige {

// One build, from the request to the solved layout (development_plan.md
// 7.4-7.6; Build and Result follow in 7.7). The editor's Build panel shows
// its State(); the AI panel shows its Card().
//
//   Start(query)  -> interpreting: the AI drafts Briefs from several readings
//                 -> each draft is validated; one with errors is repaired once
//                 -> merged (disputed and guessed fields marked), completed
//   request       ① Request: round 1 of the window; Edit() changes fields
//   assets        ② Assets: round 2 and the asset plan
//   Confirm()     -> the Brief is saved, its answers become the project's
//                    defaults, and planning starts:
//   planning      the root step, then a step per zone (AI), each validated
//                 and a broken node repaired once; then the expansion rules,
//                 the fixes that need no AI, the building count rebalanced,
//                 the whole Spec validated, what's still wrong repaired once
//   plan          ③ Plan: the summary and the sketch; Approve() compiles
//   planned       and solves it: layout.solved.json and layout.png, Verify
//                 (asked, planned, built), and the AI's review of the map
//   building      ④ Build: StartBuilding(); the editor puts the layout into the
//                 scene (editing/PlaceScene.h), verifies and repairs it, and
//   built         ⑤ Result: Built(result) — asked vs built, assets by source,
//                 the checks and the repairs; a revision, then Build again,
//                 replaces the build in the scene
//   revising      Revise(request) or Review(): the AI's changes are applied,
//                 the plan finished again and, if it was laid out, solved
//                 again; Undo() goes back one revision
//
// It handles its own failures: an AI call that fails is retried twice; if
// the AI still can't answer, the step falls back on what needs no AI (the
// request's words for the Brief, the Brief for the zones, the module rules
// for a zone's parcels), says so, and carries on. A plan is never left
// unapprovable: what can't be fixed is kept and reported as a warning.
//
// Everything is kept in places/<id>/ (session.json follows each change), so a
// build can be reopened in a later session (Open). Everything the AI was
// asked and answered, its thinking too, goes to places/<id>/log.jsonl; the
// user never sees the thinking.
class PlaceBuild {
public:
    struct Host {
        std::function<const Ontology&()> ontology;
        std::function<Catalog()> catalog;
        std::function<bool()> isoGrid;
        // Sends a request to the AI service; the answer comes to OnResponse
        // with the id returned. 0: the AI isn't available.
        std::function<int(const std::string& path, const nlohmann::json& body, int timeoutSeconds)> request;
        std::filesystem::path projectRoot;
    };

    explicit PlaceBuild(Host host) : host_(std::move(host)) {}

    // Starts interpreting `query`. Returns why it can't, or "".
    std::string Start(const std::string& query);
    // Reopens a build of the project (places/<id>/) where it was left; a step
    // that was waiting on the AI starts again. Returns why it can't, or "".
    std::string Open(const std::string& id);
    // Whether the AI answer `requestId` is this build's.
    bool Owns(int requestId) const { return pending_.contains(requestId); }
    void OnResponse(int requestId, const nlohmann::json& body);

    // A field changed in the window.
    void Edit(const std::string& path, const nlohmann::json& value);
    // Moves to "request" or "assets". Returns why not (errors to fix), or "".
    std::string Step(const std::string& step);
    // The Assets step's Build: saves the Brief and starts planning. Returns why not, or "".
    std::string Confirm();
    // ③ Plan's Approve: compiles and solves the plan, then has the AI review
    // the map. Returns why not, or "".
    std::string Approve();
    // The user's change to the plan, in words ("houses around the commons").
    std::string Revise(const std::string& request);
    // The AI looks at the laid-out map and fixes what looks wrong.
    std::string Review();
    // Back to the plan before the last revision.
    std::string Undo();
    // The plan laid out again: the fixes that need no AI, then the fitting
    // rounds (a plan from before a solver change gets its benefit). The stage stays.
    std::string Relayout();
    // ④ Build: from a laid-out plan. Returns why not, or "".
    std::string StartBuilding();
    // The editor's scene build is done: {assets, report, verify, repairs, after}.
    void Built(const nlohmann::json& result);
    const nlohmann::json& Result() const { return result_; }
    // Where the site's corner is in the world (kept, so a rebuild lands on the same cells).
    bool HasOrigin() const { return hasOrigin_; }
    std::pair<int, int> Origin() const { return {originX_, originY_}; }
    void SetOrigin(int x, int y);
    // [{by: you|review, text, summary, issues, notes, undo?, undone?}]
    const nlohmann::json& Revisions() const { return revisions_; }
    void Cancel();

    const std::string& Id() const { return id_; }
    const std::string& Stage() const { return stage_; }
    const nlohmann::json& Brief() const { return brief_; }
    const nlohmann::json& Spec() const { return spec_; }
    // The solved layout (aige.solved/1) once the plan is approved (7.6).
    const nlohmann::json& Solved() const { return solved_; }
    const std::vector<PlaceProblem>& PlanProblems() const { return planProblems_; }
    // What the build did on its own when something failed (shown to the user).
    const std::vector<std::string>& Notices() const { return notices_; }
    // The site's side in cells: as laid out, else the Brief's area.
    int SiteSide() const;
    int BriefSide() const;
    const BriefMerge& Merge() const { return merge_; }
    bool Active() const { return stage_ != "cancelled" && stage_ != "failed" && !stage_.empty(); }
    std::filesystem::path Folder() const { return host_.projectRoot / "places" / id_; }

    // For the Build panel: {"type": "build:state", ...}.
    nlohmann::json State();
    // For the AI panel: {"type": "build:card", ...}.
    nlohmann::json Card() const;

private:
    struct Pending {
        std::string kind; // interpret, repair, plan-root, plan-zone, repair-node
        int draft = -1;
        std::string node; // plan-zone, repair-node
        std::string path;
        nlohmann::json body;
        int timeout = 300;
        int attempts = 1;
    };
    // Sends a request, keeping what's needed to send it again. False: the AI isn't available.
    bool Send(Pending what);
    // A failed answer is sent again (twice at most). True: it was.
    bool Retry(const Pending& what, const nlohmann::json& body);
    void Log(nlohmann::json entry) const;
    void Save() const; // session.json
    void Notice(const std::string& text);
    void FinishInterpreting();
    void InterpretWithoutAi(const std::string& why);
    nlohmann::json Defaults() const;
    // Planning, step by step.
    void StartPlanning();
    void OnPlanAnswer(const Pending& what, const nlohmann::json& body);
    bool RepairNodes(const std::string& phase); // false: nothing to repair
    void PlanZones();
    void FinishPlanning();
    void Solve();
    void OnRevision(const Pending& what, const nlohmann::json& body);
    // Whether a request names a zone (its id, or its module's name).
    bool Mentions(const std::string& request, const std::string& zoneId) const;
    // Directions a request gives a zone, as relations: "below the docks" -> {south-of, docks}.
    std::vector<std::pair<std::string, std::string>> DirectionsIn(const std::string& request, const std::string& moving) const;
    nlohmann::json RevisionBody() const;

    Host host_;
    std::string id_, query_, stage_, status_;
    int round_ = 1;
    std::vector<nlohmann::json> drafts_;
    std::vector<std::vector<PlaceProblem>> draftProblems_;
    std::map<int, Pending> pending_;
    nlohmann::json context_;
    nlohmann::json brief_;
    BriefMerge merge_;
    std::vector<std::string> errors_;
    std::vector<std::string> notices_;
    nlohmann::json spec_;
    nlohmann::json solved_;
    int imageVersion_ = 0;                   // bumped on each new layout.png, so the panel reloads it
    std::string planPhase_;                  // root, zones, final
    std::set<std::string> repaired_;         // nodes already given their one repair
    bool finalRepairAsked_ = false;
    std::vector<PlaceProblem> planProblems_; // what the final Spec still breaks (warnings only, in the end)
    std::vector<PlaceProblem> tidied_;       // what was fixed without the AI (warnings)
    int zonesTotal_ = 0, zonesDone_ = 0;
    nlohmann::json revisions_ = nlohmann::json::array();
    std::string beforeRevision_; // the stage a revision returns to
    bool solveAfter_ = false;    // a revision of a laid-out plan: solve again once planned
    // The next layout keeps these zones' seeds (a revision moves only what it's about).
    std::map<std::string, std::pair<int, int>> keepSeeds_;
    // Where the last fitting ended (zone scales, the site's side): the next layout starts there.
    std::map<std::string, double> fitScale_;
    int fitSide_ = 0;
    nlohmann::json result_;      // the last scene build's (⑤ Result)
    bool hasOrigin_ = false;
    int originX_ = 0, originY_ = 0;
};

// "Build a large village by a river" -> "large-village-by-river", unique in places/.
std::string PlaceIdFor(const std::filesystem::path& projectRoot, const std::string& query);

// The project's builds, the latest first: [{id, query, name, stage, updated}].
nlohmann::json ListBuilds(const std::filesystem::path& projectRoot);

} // namespace aige
