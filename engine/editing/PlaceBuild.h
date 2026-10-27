#pragma once

#include "assets/Catalog.h"
#include "assets/Sourcing.h"
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
        // ③ Layout (the zones and where they go, revised before anything inside them is
        // planned): on in the editor; off, confirming plans everything at once.
        bool layoutStep = false;
    };

    explicit PlaceBuild(Host host) : host_(std::move(host)) {}

    // Starts interpreting `query`. Returns why it can't, or "".
    // `choices` (the Build panel's form, {Brief path: value}, BuildChoices): sent
    // with the words and applied over every reading of them: what was chosen stays.
    std::string Start(const std::string& query, const nlohmann::json& choices = {});
    // A Brief already written (the AI Planner's place description, development_plan.md
    // 22.10): straight to ① Confirm with it, no reading of words. `words` names the build.
    std::string StartFromBrief(const std::string& words, const nlohmann::json& brief);
    const nlohmann::json& Choices() const { return choices_; }

    // ② Assets' confirming questions (the AI's, first where each zone goes; or the
    // zones' own without it): [{text, about}]. The user's answers and directions, in
    // their words, go with the plan (the layout follows them).
    const nlohmann::json& Questions() const { return questions_; }
    void SetDirections(const std::string& text);
    // An answer to a decision of ① Confirm's "To decide" that sets no field (in words: it goes to the plan).
    void AnswerDecision(const std::string& question, const std::string& answer);
    const std::string& Directions() const { return directions_; }

    // ③ Layout: confirming plans the zones and lays them out alone (stage "layout");
    // Revise works on them there; PlanFromLayout plans what's inside each zone, the
    // zones kept where the layout put them. Returns why not, or "".
    std::string PlanFromLayout();
    // ① Confirm's Layout tab: the zones planned again from the Brief and the answers
    // (after the Contents tab changed them); a zone's own field (its street pattern,
    // its area, its count); a zone drawn where the user wants it ({x, y, w, h}, or
    // null to let it be placed). Each lays the zones out again. Returns why not, or "".
    std::string ReplanLayout();
    std::string SetZoneKind(const std::string& module, const std::string& kind, const nlohmann::json& value); // ① Confirm: how many of one kind of building
    std::string SetZoneField(const std::string& zone, const std::string& field, const nlohmann::json& value);
    // A field of the layout form (the root's: zone-shape, street-grid, lot-layout, …; docs/places/plan-generation.md
    // §3.1): set in the plan and the Brief, and the place laid out again (the zones in the layout; all of it after).
    std::string SetRootField(const std::string& field, const nlohmann::json& value);
    std::string DrawZone(const std::string& zone, const nlohmann::json& rect);
    // The drawing tool's brush: cells [[x, y]...] painted onto a zone (taken from any other
    // zone painted there), or erased from it. The zone takes exactly its painted cells.
    std::string PaintZone(const std::string& zone, const nlohmann::json& cells, bool erase);
    bool LayoutStale() const { return layoutStale_; }
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
    const nlohmann::json& Story() const { return story_; }
    bool FromPlanner() const { return fromPlanner_; }
    // What the story needs of this place (from the AI Planner: the synopsis, the scenes set here,
    // who is here, what the quests need in it): given to every AI request, so the place is
    // designed for its story. Kept with the build.
    void SetStory(nlohmann::json story);
    // The Assets step's Build: saves the Brief and starts planning. Returns why not, or "".
    std::string Confirm();
    // ③ Plan's Approve: compiles and solves the plan, then has the AI review
    // the map. Returns why not, or "".
    std::string Approve();
    // The user's change to the plan, in words ("houses around the commons").
    std::string Revise(const std::string& request);
    // The AI looks at the laid-out map and fixes what looks wrong.
    std::string Review();
    // ② The layout looked at by the vision model before the plan is made (docs/build-fixes.md): its
    // zones named and judged in the whole and against each other; what it finds wrong goes back as a
    // revision, and the layout is looked at again (two revisions at most). "" when asked.
    std::string CheckLayout();
    bool LayoutChecked() const { return layoutChecked_; }
    const nlohmann::json& LayoutCheck() const { return layoutCheck_; }
    // Back to the plan before the last revision.
    std::string Undo();
    std::string Redo(); // the last action undone, again (8.2)
    nlohmann::json Advanced(const std::string& what) const; // the build's documents: brief, spec, pll, solved, assets, log
    // The plan laid out again: the fixes that need no AI, then the fitting
    // rounds (a plan from before a solver change gets its benefit). The stage stays.
    std::string Relayout();
    // --- Editing after the build (7.8) ---------------------------------------
    // A part of the plan (a zone, a parcel, one instance "ward/cottages#2"): its
    // module, where it is in the tree, its fields with their values, its parts,
    // whether it's locked. Null if there's no such part.
    nlohmann::json NodeInfo(const std::string& node) const;
    // Changes one of a part's fields and lays that part out again, the rest as it was.
    std::string EditNode(const std::string& node, const std::string& field, const nlohmann::json& value);
    // Lays a part out again (its subtree), everything else kept.
    std::string Regenerate(const std::string& node);
    // A locked part stays as it is when what holds it is laid out again (a hand edit locks it).
    void Lock(const std::string& node, bool locked);
    const std::set<std::string>& Locks() const { return locks_; }
    // The module brush: a new part of `module` over the cells [x0, x1) x [y0, y1)
    // (site cells), in the zone there. Returns why not, or "".
    std::string Paint(const std::string& module, int x0, int y0, int x1, int y1);
    // The modules the brush can paint (features and parcels the zones can hold).
    nlohmann::json BrushModules() const;
    // The parts the last edit laid out again (for the scene to follow), then cleared.
    std::set<std::string> TakeChanged();
    // Whether a request (in the AI panel) is about this place: it names a part of it.
    bool AboutThisPlace(const std::string& request) const;
    // --- Sourcing the art (7.9) -------------------------------------------------
    // The asset plan's pending needs ({key, action: generate | web, ...}) go to the
    // Art tool or the web tool; each result is described and checked by the vision
    // model (a generated one is tried again, up to 3 times, with the reasons).
    // Returns false if nothing could be sent (no AI): the build goes on with placeholders.
    bool StartSourcing(const nlohmann::json& pending, const SourcingSettings& settings);
    // What was tried per need (the asset plan's next pass moves on from it).
    const std::map<std::string, Tried>& TriedSources() const { return tried_; }
    // The user builds again: what's still missing is tried again (once more per build).
    void ForgetTried() { tried_.clear(); }
    // Sourcing is over: the build can go on (the editor refreshes its assets first).
    bool SourcingDone() const { return sourcingDone_; }
    void ClearSourcingDone() { sourcingDone_ = false; }
    // Stops sourcing (the user won't wait for the art): what's missing gets placeholders, the build goes on.
    void SkipSourcing();
    // Refine one piece of the build's art: made again with the user's words, checked again
    // (3 tries). While sourcing, in its place; after the build, then the place is built again
    // with it (the editor goes on as after sourcing). Returns why not, or "".
    std::string RefineArt(const std::string& key, const std::string& text);
    // The user's check of the art made for a need: "kept" (it's right) or "rejected" (it isn't:
    // the need goes to its other sources). Returns the paths it was about, or why not.
    std::string SetArtVerdict(const std::string& key, const std::string& verdict, std::vector<std::string>* paths = nullptr);
    // The new assets' descriptions and provenance, for their .meta: [{path, fields}], then cleared.
    nlohmann::json TakeNewAssets();
    // A region's sites (7.10), each built later as a place of its own:
    // [{id, node, name, buildType, scale, bounds: [x, y, w, h]}] from the layout.
    nlohmann::json Sites() const;
    // ④ Build: from a laid-out plan. Returns why not, or "".
    void SetAssetNeeds(const nlohmann::json& needs); // ③ Assets: what each part's art will be (decided, not yet fetched)
    bool HasAssetNeeds() const { return !assetNeeds_.empty(); }
    void MarkSourced(const nlohmann::json& needs); // ③ Assets: the art is all there (or found at once); Build waits for the user
    void SetBuildProgress(int done, int total, const std::string& text); // ④ Build, as it goes
    std::string StartBuilding();
    // The editor's scene build is done: {assets, report, verify, repairs, after}.
    void Built(const nlohmann::json& result);
    // ⑤ The built scene looked at by the vision model (docs/build-fixes.md), in the windows the host
    // drew (InspectBuildRequest): every kind of thing named, its placement, fit and quality judged.
    // Art it finds doesn't fit or looks wrong is refused for its need (made art is made again, its
    // prompt rewritten with why): one more round of sourcing and building, which ArtToRedo names
    // until the host starts it (ArtRedone). "" when asked.
    std::string InspectBuilt(const nlohmann::json& body);
    // The place sketched before its art is made (PlaceSketch, docs/build-fixes.md): one request per
    // window of its block-out; each need's crop of the painting kept, the first reference its own art
    // is drawn from. "" when asked.
    std::string Sketch(const nlohmann::json& requests);
    bool Sketching() const { return sketching_ > 0; }
    bool Sketched() const { return sketched_; }
    const std::map<std::string, std::string>& SketchCrops() const { return sketchCrops_; }
    const nlohmann::json& Sketches() const { return sketches_; }
    // ③ Matched art (MatchedArt, docs/places/matched-art.md): the asset plan's needs drawn as sheets of boxes in
    // matched/, made by the art provider (the style sheet first, every other piece in its style). "" when asked.
    // Once made (MatchedArtMade), the editor cuts the pieces and says what came of it (MatchedArtDone).
    std::string MakeMatchedArt(const nlohmann::json& assetPlan, const Ontology* ontology);
    bool MatchedArtMade() const { return matched_.value("state", "") == "made"; }
    const nlohmann::json& MatchedArtInfo() const { return matched_; }
    void MatchedArtDone(const nlohmann::json& report);
    // The place's background: its ground painted whole (Matched art's ground plan), or a picture the user picked: top-down,
    // the site's cell (0, 0) at its corner. {path, cellPx}, or null. The Build step lays it as the ground, cell by cell.
    nlohmann::json Background() const { return matched_.value("groundPicture", nlohmann::json()); }
    std::string SetBackground(const std::string& path);
    // The background's description (the planner's, editable in Build ▸ Assets: docs/places/background.md §2): the
    // place and its style, then a line per kind of ground ("asphalt: road with white lane markings"). The Brief's
    // background.description, else one made from the Brief.
    std::string BackgroundDescription() const;
    // The background painted from the plan by the background provider (POST /places/background: the whole, then
    // tiles where it needs detail), from its description. "" when asked.
    std::string MakeBackground();
    bool MakingBackground() const { return makingBackground_; }
    bool Inspecting() const { return inspecting_; }
    const std::vector<std::string>& ArtToRedo() const { return redo_; }
    void ArtRedone() { redo_.clear(); }
    const nlohmann::json& BuildCheck() const { return buildCheck_; }
    void BuildFailed(const std::string& why); // ④ Build stopped: back to ③ Assets, said why
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
    // The plan's arrangement asked of the AI (docs/dungeon.md §0): each kind's purpose, size, relations, place and
    // facing; once answered, kept in the spec and the layout solved again with it. False: not asked (no AI, or kept).
    bool Arrange();
    void OnRevision(const Pending& what, const nlohmann::json& body);
    // After a revision is laid out: the spatial facts it asked for, checked; a
    // miss strengthens its relation and lays out again; what's still missed is said.
    void MeetExpectations();
    // The parts a revision only takes out (removes, or a zone re-planned with
    // its parcels but some): empty if it does anything more.
    std::set<std::string> LocalRemovals(const nlohmann::json& changes) const;
    void RenderMap(); // layout.png from solved_, and the status line
    // Verified examples (7.7): a built place that passed its checks is kept in
    // places/examples.json; planning a place like it shows the planner how it went.
    void KeepAsExample() const;
    nlohmann::json ExamplesLikeThis() const;
    // Whether a request names a zone (its id, or its module's name).
    bool Mentions(const std::string& request, const std::string& zoneId) const;
    // The revision language's exact ops (place-layout.md §10): swap, occupy, shift, run on the
    // zones' land as it is now. Adds to `moving` a zone that has to be placed again; returns
    // the zones it placed exactly. `notes` says what it did.
    std::set<std::string> RunExactOps(const nlohmann::json& ops, std::set<std::string>& moving, nlohmann::json& notes);
    // "Swap A and B", "move A to where B is (and B to where A is)": the two zones the request
    // has change places. Empty when it isn't that.
    std::pair<std::string, std::string> SwapAsked(const std::string& request) const;
    bool IsSubject(const std::string& request, const std::string& zoneId) const; // named as what moves, not as where
    void RelayoutMoving(const std::set<std::string>& moving);
    // A moved zone's place on the axes its relations don't move along, from its references
    // (place-layout.md §8.3): {add: [rel], drop: [rel], why: [text]}.
    nlohmann::json ComposePlace(const std::string& zone, const nlohmann::json& rel, const std::string& request,
                                const std::set<std::string>& moving) const; // at the layout: the moved zones anew, the rest exactly as they are
    // Directions a request gives a zone, as relations: "below the docks" -> {south-of, docks}.
    std::vector<std::pair<std::string, std::string>> DirectionsIn(const std::string& request, const std::string& moving) const;
    nlohmann::json RevisionBody() const;

    Host host_;
    std::string id_, query_, stage_, status_;
    nlohmann::json story_; // SetStory
    bool fromPlanner_ = false; // started by the AI Planner's game build (its art strategy is the Brief's)
    int round_ = 1;
    std::vector<nlohmann::json> drafts_;
    std::vector<std::vector<PlaceProblem>> draftProblems_;
    std::map<int, Pending> pending_;
    nlohmann::json context_;
    nlohmann::json brief_ = nlohmann::json::object();
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
    int sketching_ = 0;
    bool sketched_ = false;
    std::map<std::string, std::string> sketchCrops_; // need key -> its crop of the sketch
    std::map<std::string, std::string> sketchSeen_;  // need key -> how it looks in the sketch (the vision model's words)
    nlohmann::json sketches_ = nlohmann::json::array(); // each window's painting and prompt
    nlohmann::json matched_ = nlohmann::json::object(); // {state: making | made | cut | failed, dir, sheets, provider, report}
    bool inspecting_ = false;
    bool makingBackground_ = false;
    int buildRounds_ = 0;
    nlohmann::json buildCheck_;            // the last check of the built scene
    std::vector<std::string> redo_;        // needs whose art the check refused, for the next round
    std::map<std::string, std::string> redoReasons_; // why, for the next prompt
    std::map<std::string, std::string> redoneFrom_;  // the art each redone need had before (put back if the redo is worse)
    double firstBuildWorth_ = 0;                    // the first check's worth, to judge the redo by
    bool layoutChecked_ = false;
    int layoutRounds_ = 0;
    bool layoutCheckRevising_ = false; // the layout check's revision: zones may move, even at ③ Plan
    nlohmann::json layoutCheck_; // the last check of the layout (/places/inspect)
    double bestLayoutWorth_ = -1e9; // the best layout checked (its scores' mean, less a point an error)
    nlohmann::json bestLayout_, bestLayoutCheck_;
    std::string beforeRevision_; // the stage a revision returns to
    bool solveAfter_ = false;    // a revision of a laid-out plan: solve again once planned
    // The next layout keeps these zones' seeds (a revision moves only what it's about).
    std::map<std::string, std::pair<int, int>> keepSeeds_;
    nlohmann::json expect_;                                       // the revision's spatial facts, to check
    std::map<std::string, std::pair<int, int>> revisionKeep_;     // its kept seeds, for the retries
    // Where the last fitting ended (zone scales, the site's side): the next layout starts there.
    std::set<std::string> locks_;   // parts kept as they are (7.8)
    struct Sourcing {
        nlohmann::json need;      // the pending need
        int attempts = 0;
        std::string reasons;      // why the last try didn't pass
        std::string lastPrompt;   // the prompt the last try was drawn from (rewritten with the reasons)
        bool done = false;
        bool passed = false;
        nlohmann::json made = nlohmann::json::array(); // what came, try by try: {path, attempt, seen?, ok?}
        std::string request;      // the user's words for it (Refine)
        std::string verdict;      // the user's check of what was made: "kept", "rejected", or "" (not looked at)
        nlohmann::json superseded = nlohmann::json::array(); // what a refined piece replaces
    };
    // The sourcing as it goes, for the panel: [{key, role, description, action, attempts, done, passed, made}].
    static nlohmann::json SourcingProgress(const std::map<std::string, Sourcing>& pieces);
    // Kept in session.json: the art made, try by try, and what's still being made.
    static nlohmann::json SourcingJson(const std::map<std::string, Sourcing>& pieces);
    static std::map<std::string, Sourcing> SourcingFrom(const nlohmann::json& saved);
    std::map<std::string, Sourcing> sourced_; // what this session's sourcing made, by need (Refine after the build)
    std::map<std::string, Sourcing> sourcing_;
    std::map<std::string, Tried> tried_;
    nlohmann::json newAssets_ = nlohmann::json::array();
    bool sourcingDone_ = false;
    bool SendArt(const std::string& key);
    void HandleResponse(int requestId, const nlohmann::json& body);
    // What a need is designed against (its role's look "dependsOn": a door's wall), and that art.
    std::vector<std::string> DependsOn(const nlohmann::json& need) const;
    nlohmann::json GoesWith(const nlohmann::json& need) const;
    void SendWaiting();
    std::set<std::string> waitingOn_; // made after the pieces they're designed against
    nlohmann::json WantedFor(const Sourcing& s) const;
    void SourcingStep(const Pending& what, const nlohmann::json& body);
    void FinishSourcing();
    std::set<std::string> changed_; // parts laid out again by the last edit
    // Lays these parts out again from the plan, everything else as it was (a partial solve).
    void SolveParts(const std::set<std::string>& parts, const std::string& why);
    std::map<std::string, double> fitScale_;
    int fitSide_ = 0;
    nlohmann::json result_;      // the last scene build's (⑤ Result)
    nlohmann::json choices_ = nlohmann::json::object(); // the form's choices with the request
    nlohmann::json questions_ = nlohmann::json::array();  // ② Assets' confirming questions
    std::string directions_;                              // the user's answers, in their words
    nlohmann::json decisionAnswers_ = nlohmann::json::object(); // {question: answer}: decisions answered in words
    std::map<std::string, std::pair<int, int>> layoutSeeds_; // the zones as the layout placed them (for the plan)
    int layoutSide_ = 0;
    bool layoutStale_ = false; // the Contents tab changed what the layout was made from
    void AskQuestions();
    bool SendReadings(); // the three readings, a request each
    static nlohmann::json InterpretContextFor(const Ontology& ontology, const std::string& query, const nlohmann::json& choices);
    int PlanWaiting() const; // the plan's own requests still out
    // From the request to the layout, step by step, for the panel:
    // {steps: [{id, label, state: done|active|waiting, detail: [..]}], fraction, since}.
    nlohmann::json ConfirmProgress();
    std::string progressStep_;   // the step that was active last time (its start is `progressSince_`)
    std::string lastRetry_;      // why the last request was asked again
    long long progressSince_ = 0;
    void LayoutZones(); // the zones alone, solved and drawn (stage "layout")
    // The zones' kept cells go (the user asked for them laid out again); `keepPlaces`: the
    // zones not in `moving` keep their seeds.
    void UnfreezeLayout(bool keepPlaces, const std::set<std::string>& moving = {}, bool keepOthersCells = false);
    bool LayoutFrozen() const;
    void FitToRoom();
    // A revision asked a zone for more than it holds: it grows into the open land beside it (no
    // other zone's, not water or a road) by what the missing parts need. True if any grew.
    bool GrowShortZones();
    bool growing_ = false;
    std::string ArtDirection() const; // the look every piece of the place is asked for in
    // History (8.2): each action's state before it, restored exactly by Undo; Redo's after.
    nlohmann::json Snapshot() const;
    void Checkpoint(const std::string& what);
    void Restore(const nlohmann::json& saved);
    void ForgetRedo();
    static long long Now();
    nlohmann::json assetNeeds_ = nlohmann::json::array(); // ③ Assets: what each need got
    nlohmann::json buildProgress_ = nlohmann::json::object(); // ④ Build: {done, total, text}             // a zone asked for more than its land holds keeps what fitted, and says so
    bool roomToRevision_ = false; // FitToRoom's notes go to the revision being solved
    std::set<std::string> anchored_; // a revision's landmarks and the zones it says stay: never moved by it
    nlohmann::json layoutQuality_ = nlohmann::json::object(); // the layout's quality checks (§11.3)
    nlohmann::json LayoutQuality(const nlohmann::json& spec, const nlohmann::json& solved) const;
    void ApplySketch(nlohmann::json& zones, int side) const; // the AI's grid sketch, onto the zones
    nlohmann::json NetworkGrid(); // the river and roads on a 16x16 grid, for the AI to sketch on
    static std::string WhereText(const nlohmann::json& zone, const std::map<std::string, std::string>& names, int side);
    bool hasOrigin_ = false;
    int originX_ = 0, originY_ = 0;
};

// "Build a large village by a river" -> "large-village-by-river", unique in places/.
std::string PlaceIdFor(const std::filesystem::path& projectRoot, const std::string& query);

// The project's builds, the latest first: [{id, query, name, stage, updated}].
nlohmann::json ListBuilds(const std::filesystem::path& projectRoot);
// Removes every build under places/ but `keep` (8.2: "Remove older builds"); how many went.
int RemoveBuilds(const std::filesystem::path& projectRoot, const std::string& keep);

} // namespace aige
