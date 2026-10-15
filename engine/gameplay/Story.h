#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace aige {

// Dialogue and quests (development_plan.md 3.2b), as project files:
//
//   dialogue/<id>.json   a graph of nodes; choices lead to other nodes and
//                        can have conditions and effects
//   quests/<id>.json     a quest's objectives and what completing it does
//
// Speakers, quest givers and talk targets are World Bible keys
// ("character/villager"); the game shows the entry's name. Formats are
// documented in docs/story.md.

// A condition on a choice or a dialogue start.
struct StoryCondition {
    std::string flag;  // set: the flag must have `flagValue`
    bool flagValue = true;
    std::string quest; // set: the quest must be in `questState`
    std::string questState; // inactive | active | completed
};

// What a choice, node or completed quest does.
struct StoryEffect {
    std::string op;    // set_flag | clear_flag | start_quest | complete_quest | complete_objective
    std::string name;  // the flag or quest
    std::string objective; // complete_objective: which one
};

struct DialogueChoice {
    std::string text;
    std::string next; // empty: ends the conversation
    std::vector<StoryCondition> conditions;
    std::vector<StoryEffect> effects;
};

struct DialogueNode {
    std::string id;
    std::string speaker; // empty: the dialogue's speaker; "player" for the player's own lines
    std::string text;
    std::vector<DialogueChoice> choices; // none: Space continues to `next` (or ends)
    std::string next;
    std::vector<StoryEffect> effects; // when the node is shown
};

struct Dialogue {
    std::string path;    // project-relative, "dialogue/tamsin.json"
    std::string speaker; // World Bible key or a plain name
    std::string start;
    // Checked in order before `start`: the first whose conditions hold wins.
    struct Start {
        std::vector<StoryCondition> conditions;
        std::string node;
    };
    std::vector<Start> starts;
    std::vector<DialogueNode> nodes; // file order

    const DialogueNode* Node(const std::string& id) const;
};

struct QuestObjective {
    std::string id;
    std::string text;   // shown on screen: "Defeat the skeletons in the courtyard"
    std::string type;   // defeat | talk | collect
    std::string target; // defeat/collect: a prefab path or a team (Enemy); talk: a World Bible key
    int count = 1;
};

struct Quest {
    std::string id; // the file name, quests/<id>.json
    std::string name;
    std::string description;
    std::string giver; // World Bible key
    std::vector<QuestObjective> objectives; // done in order
    std::vector<StoryEffect> onComplete;
};

std::optional<Dialogue> DialogueFromJson(const nlohmann::json& json, const std::string& path, std::string* error);
nlohmann::json DialogueToJson(const Dialogue& dialogue);
std::optional<Quest> QuestFromJson(const nlohmann::json& json, const std::string& id, std::string* error);
nlohmann::json QuestToJson(const Quest& quest);

// Every dialogue and quest of a project.
class StoryLibrary {
public:
    explicit StoryLibrary(std::filesystem::path projectRoot = {});
    void Refresh();

    const std::vector<Dialogue>& Dialogues() const { return dialogues_; }
    const std::vector<Quest>& Quests() const { return quests_; }
    const Dialogue* FindDialogue(const std::string& path) const;
    const Quest* FindQuest(const std::string& id) const;

    struct Problem {
        std::string file; // project-relative
        std::string message;
    };
    // Files that don't parse, plus what can be checked without the rest of
    // the project: links between nodes, unreachable nodes, unknown quests and
    // effect/condition/objective kinds. (World Bible and prefab references are
    // checked by ValidateStoryReferences, engine/world.)
    std::vector<Problem> Validate() const;

private:
    std::filesystem::path root_;
    std::vector<Dialogue> dialogues_;
    std::vector<Quest> quests_;
    std::vector<Problem> loadProblems_;
};

// A run's story progress: flags and quest states.
class StoryState {
public:
    enum class QuestState { Inactive, Active, Completed };
    struct Progress {
        QuestState state = QuestState::Inactive;
        size_t objective = 0; // index of the current objective while active
        int count = 0;        // progress on it
    };

    // What happened, for the game to show ("Quest started: ...").
    struct Notice {
        enum class Kind { QuestStarted, ObjectiveDone, QuestCompleted } kind;
        std::string quest;
        std::string text;
    };

    bool Flag(const std::string& name) const { return flags_.contains(name); }
    void SetFlag(const std::string& name, bool value);
    Progress Quest(const std::string& id) const;
    static const char* StateName(QuestState state);

    bool Holds(const std::vector<StoryCondition>& conditions) const;
    void Apply(const std::vector<StoryEffect>& effects, const StoryLibrary& library);

    void StartQuest(const std::string& id, const StoryLibrary& library);
    void CompleteQuest(const std::string& id, const StoryLibrary& library);
    void CompleteObjective(const std::string& quest, const std::string& objective, const StoryLibrary& library);

    // Game events that can advance an active quest's current objective.
    void OnDefeated(const std::string& prefab, const std::string& team, const StoryLibrary& library);
    void OnCollected(const std::string& prefab, const StoryLibrary& library);
    void OnTalkedTo(const std::string& character, const StoryLibrary& library);

    // The objective to show: the first active quest's current one.
    struct Shown {
        std::string quest;
        std::string text;
    };
    std::optional<Shown> CurrentObjective(const StoryLibrary& library) const;
    bool AnyQuestActive() const;

    // Notices since the last call.
    std::vector<Notice> TakeNotices();

private:
    void Advance(const std::string& id, const StoryLibrary& library);
    void Count(const std::string& type, const std::function<bool(const std::string&)>& matches,
               const StoryLibrary& library);

    std::set<std::string> flags_;
    std::map<std::string, Progress> quests_;
    std::vector<std::string> startedOrder_; // which active quest is shown first
    std::vector<Notice> notices_;
};

// One conversation in progress.
class DialogueRunner {
public:
    // Starts `dialogue` (from its first start whose conditions hold). Returns
    // false if it has nowhere to start.
    bool Start(const Dialogue& dialogue, StoryState& state, const StoryLibrary& library);
    bool Active() const { return node_ != nullptr; }
    void End() { node_ = nullptr; dialogue_ = nullptr; }

    const DialogueNode* Node() const { return node_; }
    const Dialogue* Current() const { return dialogue_; }
    // The node's choices whose conditions hold, in order (indices into Node()->choices).
    const std::vector<size_t>& VisibleChoices() const { return visible_; }
    int Highlighted() const { return highlighted_; }
    void MoveHighlight(int delta);

    // Picks visible choice `index` (0-based): applies its effects and moves on.
    void Choose(size_t index, StoryState& state, const StoryLibrary& library);
    // Continues past a node without choices (or picks the highlighted choice).
    void Continue(StoryState& state, const StoryLibrary& library);

private:
    void Enter(const std::string& id, StoryState& state, const StoryLibrary& library);

    const Dialogue* dialogue_ = nullptr;
    const DialogueNode* node_ = nullptr;
    std::vector<size_t> visible_;
    int highlighted_ = 0;
};

} // namespace aige
