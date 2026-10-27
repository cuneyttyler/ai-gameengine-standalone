#pragma once

#include "planner/PlannerDocs.h"
#include "planner/PlannerStore.h"
#include "story/StoryGraph.h"
#include "story/StoryOntology.h"

#include <nlohmann/json.hpp>

#include <deque>
#include <filesystem>
#include <functional>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aige {

class Ontology;

// The AI Planner, designing a game (development_plan.md 22, docs/ai/ai-planner.md §5.1-5.9):
//
//   Start(request)  ① Concept: three readings of the request, merged into a Game Brief
//                   (each field's confidence, the readings that differ, what was assumed
//                   from the genre profile and the scope table); the confirmation window
//                   opens (always); EditBrief() changes fields; Confirm() continues:
//   outline         ② the story, cast and places drafted together; the knowledge graph
//                   seeded; World Bible stubs
//   story           ③ each act's chapters and scene cards, then scene by scene in order,
//                   against the graph's world state: beats, events, facts (checked by the
//                   story rules; a violation goes back once), new characters and places as
//                   stubs, the main quest's skeleton; a critic reviews each act
//   characters      ④ each character detailed (in parallel): biography, relationships,
//                   class, packages; the graph and the World Bible get them
//   quests          ⑤ the main quest in full and side quests from motivations (R-Q checks)
//   places          ⑥ each place described, with a draft Brief for the Build panel
//   systems         ⑦ records (items, loot, abilities, enemies...), validated
//   visuals         ⑧ theme, screens, HUD, notifications
//   validate        ⑨ the rules and the cross-document checks, an AI critic; with no
//                   errors the design becomes the baseline
//
// Each stage's autonomy (the Brief's plan.autonomy) is confirm (waits for Approve()), review
// (continues; marked to review) or auto. Every AI answer is a decision (planner/
// decisions.jsonl) that documents and graph facts point to (Why?). Work is a queue of
// jobs in planner/state.json, so a plan reopened later (Open) carries on where it was. A
// document's inputs are recorded: editing one marks its dependents stale, and
// RegenerateStale() redoes exactly those (locked documents are kept).
class GamePlan {
public:
    struct Host {
        // Sends a request to the AI service; the answer comes to OnResponse with the id
        // returned. 0: the AI isn't available.
        std::function<int(const std::string& path, const nlohmann::json& body, int timeoutSeconds)> request;
        std::filesystem::path projectRoot;
        std::function<const Ontology&()> places;
        // What the stages may name: {packageTemplates, themes, screens, recordTypes {type: [fields]},
        // abilities, effects}.
        nlohmann::json catalog = nlohmann::json::object();
        // Checks a record the Systems stage writes; empty: not checked.
        std::function<std::vector<std::string>(const std::string& type, const nlohmann::json& record)> validateRecord;
        int parallel = 3; // jobs that may run side by side (characters, places)
    };

    explicit GamePlan(Host host);

    // Starts a new plan from a request (an existing plan in planner/ is replaced), with the user's choices (Brief
    // paths to values, from the home page's Choices): the readings keep them, the merged Brief has them. Returns
    // why it can't, or "".
    std::string Start(const std::string& request, const nlohmann::json& choices = nlohmann::json::object());
    // Reopens planner/: running jobs start again. Returns why it can't, or "".
    std::string Open();
    bool Active() const { return !stage_.empty(); }
    const std::string& Stage() const { return stage_; }
    const std::string& Status() const { return status_; } // working | waiting | paused | failed | done

    bool Owns(int requestId) const { return pending_.contains(requestId); }
    void OnResponse(int requestId, const nlohmann::json& body);

    // ① The confirmation window.
    void EditBrief(const std::string& path, const nlohmann::json& value);
    std::string Confirm();
    // A stage waiting for the user (autonomy "confirm") goes on.
    std::string Approve();
    void Pause(bool paused);

    // --- Items (every document) ----------------------------------------------------------
    // A user's change: a JSON pointer into the document ("/card/goal") gets `value`.
    std::string Edit(const std::string& doc, const std::string& pointer, const nlohmann::json& value, const std::string& reason = {});
    std::string Lock(const std::string& doc, bool locked);
    std::string Regenerate(const std::string& doc);
    std::string RegenerateStale();
    // A step that failed, tried again from its first attempt; or every failed step. The plan
    // goes back to what it was doing (waiting for review, done) once they're through.
    std::string RetryJob(const std::string& id);
    std::string RetryFailed();
    // ⑨'s findings with a known remedy, fixed with as few AI calls as can be: directly where no
    // AI is needed (a window added, a part listed, a companion given a package), else a quick
    // fix of the item (its document back to the AI with only the findings to correct; one call
    // per item, however many findings). Then validated again. `ids`: finding ids, or "errors",
    // "warnings", "*" (both); empty: every fixable error (what ⑨ does on its own, once).
    std::string FixFindings(const std::vector<std::string>& ids = {});
    // Every fix stops: what's still fixing is open again, the fix jobs not yet answered (and the
    // check after them) are cancelled; fixes already made stay.
    std::string StopFixes();
    // Fix now (the user): the findings asked for; then, for errors or both, more rounds within that
    // scope while errors keep turning up. Warnings or one finding: a single pass, errors untouched.
    std::string FixNow(const std::vector<std::string>& ids) {
        fixRounds_ = 1;
        const bool errors = std::find(ids.begin(), ids.end(), "errors") != ids.end();
        const bool both = std::find(ids.begin(), ids.end(), "*") != ids.end();
        fixScope_ = both ? "*" : errors ? "errors" : "none";
        return FixFindings(ids);
    }
    // The AI service became available: what waits for it (re-applied answers) goes now.
    // A plan stopped only because the AI wasn't there goes on by itself.
    void Kick();
    // Steps queued or running while the plan works (0 when paused, waiting or done): closing the
    // editor now would cut them off.
    int Busy() const;
    // The editor closes with steps to do: paused and saved, and carried on when the plan is next
    // opened (a step that was running is asked again; its answer was lost with the session).
    void PauseForClose();
    // A change in words from the AI panel ("make Aldric the mentor's son"): the AI turns it into
    // edits of the plan's documents; what depends on them goes stale.
    std::string Revise(const std::string& instruction);
    // Whether words name something in the plan (a character, place or quest by name).
    bool AboutThePlan(const std::string& text) const;
    std::vector<std::string> Stale() const;
    // Why? The decision behind a document (or a graph node): its rationale, inputs, checks.
    nlohmann::json Why(const std::string& docOrNode) const;
    nlohmann::json Document(const std::string& doc) const;
    // ⑨ A finding kept as it is, with the user's reason (an accepted error doesn't block).
    std::string AcceptFinding(const std::string& finding, const std::string& reason);
    // Back to a history snapshot (the baseline, a stage's commit).
    std::string Restore(int snapshot);

    // --- Views ---------------------------------------------------------------------------
    nlohmann::json State() const; // the Planner panel's model
    nlohmann::json Card() const;  // the AI panel's plan card
    PlannerStore& Store() { return store_; }
    StoryGraph Graph() const;

private:
    struct Job {
        std::string id, kind, subject, status = "queued", error;
        int attempts = 0;
        std::vector<std::string> after; // jobs that must be done first
        nlohmann::json problems = nlohmann::json::array(); // the last answer's, for the retry
        // That answer itself: the retry is a quick fix of it (only the problems), not a rewrite.
        nlohmann::json lastAnswer;
        // Its next send re-applies an answer the AI already gave (from the log: no model call,
        // the service only puts it into shape); once tried, it isn't replayed again.
        bool replay = false, replayed = false;
        std::string decision;
    };

    Host host_;
    PlannerStore store_;
    StoryOntology gso_;
    std::string stage_, status_, message_, request_;
    nlohmann::json choices_ = nlohmann::json::object(); // the user's choices with the request (Start)
    std::deque<Job> jobs_; // a deque: handlers add jobs while holding a reference to theirs
    std::map<int, std::string> pending_; // request id -> job id
    nlohmann::json usage_ = {{"calls", 0}, {"inputTokens", 0}, {"outputTokens", 0}};
    // Generation time: while any call is in flight (overlapping calls counted once), added to usage_["seconds"].
    std::optional<std::chrono::steady_clock::time_point> busySince_;
    nlohmann::json concept_ = nlohmann::json::object(); // drafts, marks, assumptions, decisions
    nlohmann::json toReview_ = nlohmann::json::array();
    nlohmann::json findings_ = nlohmann::json::array(); // what stages couldn't fix
    int nextJob_ = 1;
    // The status a retry or a regenerate interrupted ({status, message}): back to it when the
    // extra work is through.
    nlohmann::json resume_;
    // ⑨ fixed what it could on its own after this report (once per validation).
    bool autoFixed_ = false; // (plans from before rounds; read as rounds = 1)
    // Fix rounds this validation has had: after each, a report with errors fixes again, up to 3.
    int fixRounds_ = 0;
    // What the rounds after a fix may fix: "errors" (the plan's own), "*" (Fix now: both), "none".
    std::string fixScope_ = "errors";
    // Paused because the editor closed (not by the user): Open carries on.
    bool pausedForClose_ = false;
    // A finding's remedy: {"how": what it does, "do": the change}, or null.
    nlohmann::json RemedyOf(const nlohmann::json& finding) const;
    // An item's document back to the AI with only these problems to correct (no rewrite).
    std::string QuickFix(const std::string& doc, const std::vector<std::string>& problems);
    void RepairQuest(nlohmann::json& quest, StoryGraph& graph, std::vector<std::string>& known, std::vector<std::string>& fixes) const;
    void Requeue(Job& job);
    void BeginExtraWork();
    std::string DocOfJob(const Job& job) const;
    // The last answer the AI gave a job of this kind and subject (from the call log), or null.
    nlohmann::json LoggedAnswer(const std::string& kind, const std::string& subject) const;
    // Failed jobs with an answer in the log: queued to re-apply it (no model call).
    int QueueReplays();
    bool paused_ = false;

    // The job engine.
    std::string AddJob(const std::string& kind, const std::string& subject, std::vector<std::string> after = {});
    Job* FindJob(const std::string& id);
    void Pump();
    bool Send(Job& job);
    void Finish(Job& job, bool ok, const std::string& error = {});
    void Retry(Job& job, const std::vector<std::string>& problems);
    void Save();
    void Load();
    bool StageJobsDone(const std::string& stage) const;
    void AfterStage(const std::string& stage);
    void BeginStage(const std::string& stage);
    std::string NextStage(const std::string& stage) const;
    std::string StageOfJob(const std::string& kind) const;
    bool StageOn(const std::string& stage) const;
    void Fail(const std::string& message);
    void Finding(const std::string& stage, const std::string& subject, const std::string& message, const std::string& severity = "warning");

    // Requests and answers, by job kind.
    nlohmann::json RequestBody(const Job& job);
    std::string PathFor(const Job& job) const;
    std::vector<std::string> Handle(Job& job, const nlohmann::json& body); // problems, or none: done
    void LocalJob(Job& job); // jobs that need no AI (report)

    // Stage work.
    void MergeConcept();
    std::vector<std::string> HandleOutline(Job& job, const nlohmann::json& outline, const std::string& decision);
    std::vector<std::string> HandleAct(Job& job, const nlohmann::json& act, const std::string& decision);
    std::vector<std::string> HandleScene(Job& job, const nlohmann::json& scene, const std::string& decision);
    std::vector<std::string> HandleCritic(Job& job, const nlohmann::json& answer, const std::string& decision);
    std::vector<std::string> HandleCharacter(Job& job, const nlohmann::json& character, const std::string& decision);
    std::vector<std::string> HandleQuests(Job& job, const nlohmann::json& quests, const std::string& decision);
    std::vector<std::string> HandlePlace(Job& job, const nlohmann::json& place, const std::string& decision);
    std::vector<std::string> HandleSystems(Job& job, const nlohmann::json& systems, const std::string& decision);
    std::vector<std::string> HandleVisuals(Job& job, const nlohmann::json& ui, const std::string& decision);
    void BuildReport(const nlohmann::json& critic);
    void UndoScene(const std::string& scene); // a scene's facts out of the graph before it's written again

    // Helpers.
    nlohmann::json Brief() const;
    nlohmann::json Outline() const;
    std::vector<std::string> SceneIds() const; // by order
    bool SaveGraph(const StoryGraph& graph);
    void WorldStub(const std::string& key, const std::string& name, const std::string& summary, const std::string& body = {});
    std::string DocOfNode(const std::string& node) const;
};

// The planner's built-in fallbacks when the AI isn't available: a Game Brief from the
// request's words alone (title, genre by keywords, every field a guess).
nlohmann::json FallbackGameBrief(const std::string& request);
// Merges Game Brief drafts: each leaf takes the most common value (a tie goes to the
// "typical" reading); marks[path] = {confidence, differ: [other values], guessed}.
nlohmann::json MergeGameBriefs(const std::vector<nlohmann::json>& drafts, nlohmann::json* marks);

} // namespace aige
