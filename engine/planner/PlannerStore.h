#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aige {

// The AI Planner's documents (docs/ai/planner-schema.md §1, development_plan.md 22.1): every
// document of planner/ by its name without .json ("brief", "outline", "story/acts/act-1",
// "story/scenes/s-04", "characters/tamsin", "quests/q-main", "places/riverford", "systems",
// "ui", "report"), each with a schema, a revision, the decision that made it (madeBy) and
// the revisions of the documents it was made from (inputs) — so a document is stale when
// one of its inputs has moved on. Also the decision record (decisions.jsonl, append-only),
// the AI call log (log.jsonl), the plan state (state.json), history snapshots and the
// baseline (history/<n>/), and the project defaults (defaults.json).

struct PlanDecision {
    std::string id;     // d-<n>
    std::string time;   // ISO
    std::string stage;
    std::string actor;  // ai | user | rule | planner
    std::string kind;   // interpret, write-outline, write-scene, detail-character, edit, lock, ...
    std::string subject;
    std::string chosen;
    std::string rationale; // shown as Why?
    std::string reason;    // a user's reason
    std::string call;      // the log entry of the AI call
    nlohmann::json inputs = nlohmann::json::object();
    nlohmann::json outputs = nlohmann::json::array();
    nlohmann::json alternatives = nlohmann::json::array();
    nlohmann::json checks = nlohmann::json::object();

    nlohmann::json ToJson() const;
    static PlanDecision FromJson(const nlohmann::json& j);
};

class PlannerStore {
public:
    explicit PlannerStore(std::filesystem::path projectRoot);
    const std::filesystem::path& Root() const { return root_; }
    std::filesystem::path Dir() const { return root_ / "planner"; }
    std::filesystem::path PathOf(const std::string& doc) const;

    // --- Documents ------------------------------------------------------------------------
    std::optional<nlohmann::json> Read(const std::string& doc) const;
    // Writes `content` as the document's next revision, made by `madeBy` from `inputs`
    // (their current revisions are recorded). Returns the new revision.
    int Write(const std::string& doc, nlohmann::json content, const std::string& madeBy,
              const std::vector<std::string>& inputs = {});
    // Changes a document in place (a user's edit): its revision moves on, inputs kept.
    int Touch(const std::string& doc, const nlohmann::json& content, const std::string& madeBy);
    int RevisionOf(const std::string& doc) const; // 0: none. "kg" is the story graph's revision.
    bool Exists(const std::string& doc) const { return RevisionOf(doc) > 0; }
    std::vector<std::string> List(const std::string& folder) const; // "characters" -> characters/*
    void Remove(const std::string& doc);
    bool Locked(const std::string& doc) const;

    // Documents whose recorded inputs no longer match (sorted).
    std::vector<std::string> Stale() const;
    // Documents that list `doc` among their inputs.
    std::vector<std::string> Dependents(const std::string& doc) const;

    // --- Decisions and the call log ---------------------------------------------------------
    std::string Decide(PlanDecision decision); // gives it its id and time, appends it
    std::vector<PlanDecision> Decisions() const;
    std::optional<PlanDecision> Decision(const std::string& id) const;
    std::string Log(const nlohmann::json& entry); // returns the entry's id (c-<n>)

    // --- State, history, defaults --------------------------------------------------------------
    nlohmann::json State() const; // {} when there's no plan
    void SaveState(const nlohmann::json& state);
    bool HasPlan() const;
    // Snapshots every document (and the graph) as history/<n>/; returns n.
    int Commit(const std::string& label);
    nlohmann::json History() const; // [{n, label, time}]
    bool Restore(int n);            // documents back as they were at n (the graph too)
    nlohmann::json Defaults() const;
    void SaveDefaults(const nlohmann::json& defaults);

    static std::string Now();

private:
    std::filesystem::path root_;
    std::vector<std::string> AllDocuments() const;
};

} // namespace aige
