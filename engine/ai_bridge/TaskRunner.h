#pragma once

#include "ai_bridge/DryRun.h"
#include "ai_bridge/Proposal.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "ecs/Registry.h"
#include "gameplay/Settings.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace aige {

class AssetDatabase;
class ReflectionRegistry;

// Internal safety limits for one multi-step task — not a user setting; they
// only stop a runaway loop (development_plan.md 2.9).
struct TaskLimits {
    int maxSteps = 6;
    int maxInputTokens = 250000;
    double dryRunSeconds = 2.0;
};

struct TaskStep {
    int number = 0;
    std::string summary;
    std::vector<std::string> applyErrors;   // operations that failed on the working copy
    std::vector<ScriptProblem> problems;    // script errors when the working copy was played
};

// Carries out one "bigger task" (a proposal with scope "task"): each step the
// model proposes is applied to a scratch working copy — a clone of the scene
// with the same entity ids, copies of the project's scripts and prefabs, and
// its own project settings — then the working copy is played headless and
// the results go back to the model for the next step. Nothing touches the
// real project until the user accepts the combined result, which Replay
// applies in one go. The editor drives the requests (AiClient is async);
// this class holds the task's state and is testable without a model.
class TaskRunner {
public:
    enum class State { Working, Finished, GaveUp };

    TaskRunner(Registry& scene, const Project& project, const ReflectionRegistry& reflection,
               std::string instruction, TaskLimits limits = {});
    ~TaskRunner(); // removes the working copy
    TaskRunner(const TaskRunner&) = delete;
    TaskRunner& operator=(const TaskRunner&) = delete;

    // Feeds the model's answer for the next step (the first answer included):
    // applies its operations to the working copy, plays it, and decides
    // whether another step is needed.
    State OnAnswer(const nlohmann::json& proposal, int inputTokens = 0);
    State GetState() const { return state_; }
    // The next step couldn't be had (the model's answer never validated):
    // ends the task with what the steps so far did, for review.
    void GiveUp(const std::string& reason) {
        state_ = State::GaveUp;
        stopReason_ = reason;
    }

    // For the next request while Working: the follow-up message (step
    // results, what to do next), the task's own conversation so far, and the
    // working copy described as the AI context.
    const std::string& NextInstruction() const { return nextInstruction_; }
    nlohmann::json History() const { return history_; }
    nlohmann::json Context(AssetDatabase& assets);

    const std::string& Instruction() const { return instruction_; }
    const std::vector<std::string>& Plan() const { return plan_; }
    const std::vector<TaskStep>& Steps() const { return steps_; }
    const std::string& StopReason() const { return stopReason_; } // when GaveUp

    // Everything the task did, as one proposal for review: every operation
    // that applied, in order, with before/after as they were on the working
    // copy.
    nlohmann::json CombinedProposal() const;
    const std::vector<ChangeItem>& CombinedItems() const { return items_; }

    // Applies accepted operations of the combined proposal to the real scene.
    // `accepted` pairs each operation's index in CombinedProposal() with the
    // operation (possibly edited in review). References to entities the task
    // spawned are translated to the entities spawned now.
    ApplyOutcome Replay(const std::vector<std::pair<size_t, nlohmann::json>>& accepted, Registry& registry,
                        PrefabLibrary& prefabs, const Project& project) const;

    const std::filesystem::path& WorkingCopy() const { return scratch_.root; }

private:
    void ApplyStep(const nlohmann::json& operations, TaskStep& step);
    void BuildNextInstruction(const TaskStep& step, bool done);

    const ReflectionRegistry& reflection_;
    std::string instruction_;
    TaskLimits limits_;
    State state_ = State::Working;

    Project scratch_;
    std::unique_ptr<PrefabLibrary> prefabs_;
    Registry registry_;
    ProjectSettings settings_; // the working copy's settings (ActiveSettings is global)

    std::vector<std::string> plan_;
    std::vector<TaskStep> steps_;
    std::vector<nlohmann::json> operations_; // combined, in order
    std::vector<ChangeItem> items_;
    std::vector<Entity> created_;            // per combined operation (entt::null if none)
    std::vector<std::string> writtenScripts_;
    nlohmann::json history_ = nlohmann::json::array();
    std::string pendingUser_;
    std::string nextInstruction_;
    std::string stopReason_;
    int inputTokens_ = 0;
};

} // namespace aige
