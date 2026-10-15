#include "ai_bridge/TaskRunner.h"

#include "ai_bridge/ContextBuilder.h"
#include "assets/SceneIO.h"
#include "reflection/Reflection.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <sstream>

namespace aige {

namespace {

namespace fs = std::filesystem;

// The working copy's settings are in effect only while the working copy is
// being changed, played or described; the editor's stay global otherwise.
class SettingsSwap {
public:
    explicit SettingsSwap(ProjectSettings& other) : other_(other) { std::swap(ActiveSettings(), other_); }
    ~SettingsSwap() { std::swap(ActiveSettings(), other_); }

private:
    ProjectSettings& other_;
};

fs::path NewScratchFolder() {
    static std::atomic<int> counter{0};
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("aige_task_" + std::to_string(stamp) + "_" + std::to_string(counter++));
}

std::string ProblemsText(const std::vector<ScriptProblem>& problems) {
    std::ostringstream out;
    for (const auto& problem : problems) {
        out << "\n- " << problem.path << ": " << problem.message;
        if (problem.count > 1) {
            out << " (" << problem.count << " times)";
        }
    }
    return out.str();
}

// Rewrites references to entities the task spawned (scratch ids) into the
// entities spawned during replay. Covers every place an operation can name
// an entity: its target, entity-reference values and command parameters.
void TranslateEntities(nlohmann::json& op, const std::map<uint32_t, uint32_t>& map,
                       const ReflectionRegistry& reflection) {
    auto translate = [&](nlohmann::json& value) {
        if (value.is_number_integer()) {
            auto it = map.find(value.get<uint32_t>());
            if (it != map.end()) {
                value = it->second;
            }
        }
    };
    if (map.empty()) {
        return;
    }
    if (op.contains("entity")) {
        translate(op["entity"]);
    }
    const std::string kind = op.value("op", "");
    const ComponentInfo* component = reflection.FindComponent(op.value("component", ""));
    if (kind == "set_field" && component != nullptr) {
        const FieldInfo* field = component->Field(op.value("field", ""));
        if (field != nullptr && field->type == FieldType::EntityRef && op.contains("value")) {
            translate(op["value"]);
        }
    } else if (kind == "add_component" && component != nullptr && op.contains("fields")) {
        for (auto it = op["fields"].begin(); it != op["fields"].end(); ++it) {
            const FieldInfo* field = component->Field(it.key());
            if (field != nullptr && field->type == FieldType::EntityRef) {
                translate(it.value());
            }
        }
    } else if (kind == "command" && op.contains("args")) {
        if (const CommandInfo* command = reflection.FindCommand(op.value("name", ""))) {
            for (const auto& param : command->params) {
                if (param.type == FieldType::EntityRef && op["args"].contains(param.name)) {
                    translate(op["args"][param.name]);
                }
            }
        }
    }
}

} // namespace

TaskRunner::TaskRunner(Registry& scene, const Project& project, const ReflectionRegistry& reflection,
                       std::string instruction, TaskLimits limits)
    : reflection_(reflection), instruction_(std::move(instruction)), limits_(limits) {
    scratch_.root = NewScratchFolder();
    std::error_code ec;
    fs::create_directories(scratch_.root, ec);
    for (const char* dir : {"scripts", "prefabs"}) {
        if (fs::exists(project.root / dir, ec)) {
            fs::copy(project.root / dir, scratch_.root / dir, fs::copy_options::recursive, ec);
        } else {
            fs::create_directories(scratch_.root / dir, ec);
        }
    }
    prefabs_ = std::make_unique<PrefabLibrary>(scratch_);
    prefabs_->Refresh();
    CloneScene(scene, registry_);
    settings_ = ActiveSettings();
    history_.push_back({{"role", "user"}, {"content", instruction_}});
}

TaskRunner::~TaskRunner() {
    std::error_code ec;
    fs::remove_all(scratch_.root, ec);
}

TaskRunner::State TaskRunner::OnAnswer(const nlohmann::json& proposal, int inputTokens) {
    if (state_ != State::Working) {
        return state_;
    }
    if (!pendingUser_.empty()) {
        history_.push_back({{"role", "user"}, {"content", pendingUser_}});
        pendingUser_.clear();
    }
    history_.push_back({{"role", "assistant"}, {"content", proposal.dump()}});
    inputTokens_ += inputTokens;

    if (plan_.empty()) {
        for (const auto& line : proposal.value("plan", nlohmann::json::array())) {
            if (line.is_string()) {
                plan_.push_back(line.get<std::string>());
            }
        }
    }

    TaskStep step;
    step.number = static_cast<int>(steps_.size()) + 1;
    step.summary = proposal.value("summary", "");
    const nlohmann::json operations = proposal.value("operations", nlohmann::json::array());
    ApplyStep(operations, step);

    // An answer with nothing left to do ends the task even if the model
    // forgot to say "done".
    const bool done = proposal.value("done", false) || operations.empty();
    const bool clean = step.applyErrors.empty() && step.problems.empty();
    steps_.push_back(step);

    if (done && clean) {
        state_ = State::Finished;
    } else if (static_cast<int>(steps_.size()) >= limits_.maxSteps) {
        state_ = State::GaveUp;
        stopReason_ = "Stopped after " + std::to_string(limits_.maxSteps) + " steps" +
                      (clean ? " without finishing." : " with problems left to fix.");
    } else if (inputTokens_ >= limits_.maxInputTokens) {
        state_ = State::GaveUp;
        stopReason_ = "Stopped: this task used its token budget (" + std::to_string(inputTokens_) + " input tokens).";
    } else {
        BuildNextInstruction(steps_.back(), done);
        pendingUser_ = nextInstruction_;
    }
    return state_;
}

void TaskRunner::ApplyStep(const nlohmann::json& operations, TaskStep& step) {
    SettingsSwap swap(settings_);
    for (const auto& op : operations) {
        // One at a time, so an operation can use an entity an earlier one in
        // the same step created, and each before/after is accurate.
        nlohmann::json single = {{"operations", nlohmann::json::array({op})}};
        ChangeItem item = ResolveProposal(single, registry_, reflection_, *prefabs_, scratch_).items.front();
        if (!item.error.empty()) {
            step.applyErrors.push_back(item.target + ": " + item.error);
            continue;
        }
        ApplyOutcome outcome = ApplyOperations({op}, registry_, reflection_, *prefabs_, scratch_);
        if (!outcome.errors.empty()) {
            step.applyErrors.push_back(outcome.errors.front());
            continue;
        }
        operations_.push_back(op);
        items_.push_back(item);
        created_.push_back(outcome.created.front());
        if (op.value("op", "") == "command" && op.value("name", "") == "script.write") {
            std::string path = op["args"].value("path", "");
            if (std::find(writtenScripts_.begin(), writtenScripts_.end(), path) == writtenScripts_.end()) {
                writtenScripts_.push_back(path);
            }
        }
    }
    if (!writtenScripts_.empty()) {
        step.problems = DryRunScripts(SerializeScene(registry_), scratch_, limits_.dryRunSeconds, writtenScripts_);
    }
}

void TaskRunner::BuildNextInstruction(const TaskStep& step, bool done) {
    std::ostringstream out;
    out << "TASK IN PROGRESS. Original request: \"" << instruction_ << "\"\n";
    if (!plan_.empty()) {
        out << "Plan:";
        for (size_t i = 0; i < plan_.size(); ++i) {
            out << "\n" << (i + 1) << ". " << plan_[i];
        }
        out << "\n";
    }
    out << "Step " << step.number << " (" << step.summary << ") was applied to a working copy of the project.";
    if (!step.applyErrors.empty()) {
        out << "\nThese operations could not be applied and were skipped:";
        for (const auto& error : step.applyErrors) {
            out << "\n- " << error;
        }
    }
    if (!step.problems.empty()) {
        out << "\nPlaying the working copy for " << limits_.dryRunSeconds
            << " seconds produced script errors:" << ProblemsText(step.problems);
    } else if (!writtenScripts_.empty()) {
        out << "\nPlaying the working copy for " << limits_.dryRunSeconds << " seconds produced no script errors.";
    }
    out << "\nThe PROJECT CONTEXT is the working copy after step " << step.number
        << ", including everything done so far (ids of entities created by the task are as shown there).\n";
    if (done) {
        out << "You marked the task done, but the problems above must be fixed first. ";
    } else if (!step.applyErrors.empty() || !step.problems.empty()) {
        out << "Fix the problems above first, then continue. ";
    }
    out << "Answer with the operations for the next step, with \"scope\": \"task\". Set \"done\": true when the whole "
           "request is finished and nothing is left to fix (operations may then be empty). Don't repeat operations "
           "that were already applied.";
    nextInstruction_ = out.str();
}

nlohmann::json TaskRunner::Context(AssetDatabase& assets) {
    SettingsSwap swap(settings_);
    return BuildEditorContext(registry_, reflection_, *prefabs_, assets, scratch_);
}

nlohmann::json TaskRunner::CombinedProposal() const {
    nlohmann::json steps = nlohmann::json::array();
    for (const auto& step : steps_) {
        steps.push_back(step.summary);
    }
    return {{"summary", steps_.empty() ? instruction_ : steps_.front().summary},
            {"operations", operations_},
            {"plan", plan_},
            {"steps", steps},
            {"notes", stopReason_}};
}

ApplyOutcome TaskRunner::Replay(const std::vector<std::pair<size_t, nlohmann::json>>& accepted, Registry& registry,
                                PrefabLibrary& prefabs, const Project& project) const {
    ApplyOutcome total;
    std::map<uint32_t, uint32_t> spawned; // working-copy id -> real id
    for (const auto& [index, original] : accepted) {
        nlohmann::json op = original;
        TranslateEntities(op, spawned, reflection_);
        ApplyOutcome outcome = ApplyOperations({op}, registry, reflection_, prefabs, project);
        total.applied += outcome.applied;
        total.errors.insert(total.errors.end(), outcome.errors.begin(), outcome.errors.end());
        total.settingsChanged = total.settingsChanged || outcome.settingsChanged;
        total.prefabsChanged = total.prefabsChanged || outcome.prefabsChanged;
        total.created.push_back(outcome.created.front());
        if (index < created_.size() && created_[index] != entt::null && outcome.created.front() != entt::null) {
            spawned[static_cast<uint32_t>(created_[index])] = static_cast<uint32_t>(outcome.created.front());
        }
    }
    return total;
}

} // namespace aige
