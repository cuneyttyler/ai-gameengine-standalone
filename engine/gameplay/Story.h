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
    std::string questState; // inactive | active | completed | failed
    int stage = -1;    // with a quest: it's at this stage (15.1); -1: any
    // Anything else (15.2): a story variable ({"var": "paid", "op": ">=", "value": 10})
    // or something about the world the game answers ({"item": "potion", "count": 2},
    // {"level": 3}, {"attribute": "attack", "atLeast": 5}, {"time": {"from", "to"}}).
    nlohmann::json other;
};

// What a choice, node or completed quest does.
struct StoryEffect {
    // set_flag | clear_flag | start_quest | complete_quest | complete_objective |
    // fail_quest | set_stage (15.1) | set_var | add_var (15.2) | give_item | take_item |
    // give_gold | give_xp | reputation | play_cutscene (handed to the game, 15.5)
    std::string op;
    std::string name;      // the flag, quest, variable, item, faction or cutscene
    std::string objective; // complete_objective: which one
    int stage = 0;         // set_stage
    nlohmann::json value;  // set_var / add_var; a count or amount for the world's ops
};

struct DialogueChoice {
    std::string text;
    std::string next; // empty: ends the conversation
    std::vector<StoryCondition> conditions;
    std::vector<StoryEffect> effects;
    // A skill check (15.3): {"attribute": "speech", "difficulty": 5} or any world
    // condition. Passing goes to `next`; failing to `fail` (and skips `effects`).
    nlohmann::json check;
    std::string fail;
};

struct DialogueNode {
    std::string id;
    std::string speaker; // empty: the dialogue's speaker; "player" for the player's own lines
    std::string text;
    std::string portrait; // an image (assets/...); empty: the speaker's from the World Bible
    std::string voice;    // a sound played with the line
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
    // defeat | talk | collect | reach (3.2) | deliver | use | have | survive | escort (15.1)
    std::string type;
    // defeat/collect: a prefab path or a team (Enemy); talk: a World Bible key;
    // reach: a Marker's name (empty with a scene: just arrive there); deliver: whom to;
    // use: what to use (a Name, an Interactable's name, an Actor key); have: an item;
    // escort: who. "@alias" names one of the quest's aliases.
    std::string target;
    int count = 1;
    // Only counts in this scene ("scenes/cellar.aige"); empty: in any scene.
    std::string scene;
    bool optional = false; // doesn't hold its stage back (15.1)
    std::string item;      // deliver: what to hand over
    std::string to;        // escort: the marker to bring them to
    float seconds = 0.0f;  // survive: for how long
};

// A numbered state of a quest (15.1): its objectives, what happens on entering
// it, and where it goes once its objectives are done.
struct QuestStage {
    int index = 0; // 10, 20...
    std::string journal; // the journal's line for it
    std::vector<QuestObjective> objectives;
    std::vector<StoryEffect> onEnter;
    struct Branch {
        std::vector<StoryCondition> conditions;
        int stage = 0;
    };
    std::vector<Branch> branches; // checked in order when done; the first that holds
    int next = 0;                 // else this stage (0: the next in the list)
    std::string ends;             // "complete" or "fail": the quest ends here
};

// A role the quest refers to (15.1, ai-planner.md §9): "@thief" in a target is its fill.
struct QuestAlias {
    std::string id;
    std::string kind; // character | item | location | entity
    std::string fill; // a World Bible key, an item id, a marker, an entity's name
};

struct QuestRewards {
    float experience = 0.0f;
    int gold = 0;
    std::string items;   // "potion*2, iron-sword"
    std::string faction; // reputation with it
    int reputation = 0;
};

struct Quest {
    std::string id; // the file name, quests/<id>.json
    std::string name;
    std::string description;
    std::string giver; // World Bible key
    std::vector<QuestObjective> objectives; // every stage's, in order (a v1 quest: one per stage)
    std::vector<StoryEffect> onComplete;
    // Quests v2 (15.1). A v1 file (objectives only) gets one stage per objective.
    bool staged = false; // written with "stages"
    std::vector<QuestStage> stages;
    std::vector<QuestAlias> aliases;
    QuestRewards rewards;
    std::vector<StoryEffect> onFail;

    const QuestStage* StageByIndex(int index) const;
    // "@thief" -> the alias's fill; anything else as it is.
    std::string Resolve(const std::string& target) const;
};

std::optional<Dialogue> DialogueFromJson(const nlohmann::json& json, const std::string& path, std::string* error);
nlohmann::json DialogueToJson(const Dialogue& dialogue);
std::optional<Quest> QuestFromJson(const nlohmann::json& json, const std::string& id, std::string* error);
nlohmann::json QuestToJson(const Quest& quest);

// Problems with one dialogue or quest on its own terms (links, reachability,
// effects and conditions); `findQuest` answers for the quests it mentions.
using QuestLookup = std::function<const Quest*(const std::string&)>;
std::vector<std::string> ValidateDialogue(const Dialogue& dialogue, const QuestLookup& findQuest);
std::vector<std::string> ValidateQuest(const Quest& quest, const QuestLookup& findQuest);

// Writes dialogue/<id>.json or quests/<id>.json, normalized through the
// reader and writer above (so every file has one shape). Returns why not.
std::optional<std::string> WriteStoryFile(const std::filesystem::path& projectRoot, const std::string& folder,
                                          const std::string& id, const nlohmann::json& content);

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
    enum class QuestState { Inactive, Active, Completed, Failed };
    struct Progress {
        QuestState state = QuestState::Inactive;
        size_t objective = 0; // the current stage's place in the list (a v1 quest: its objective)
        int count = 0;        // progress on the stage's first open objective
        std::map<std::string, int> counts; // per objective id, in the current stage
        std::set<std::string> done;        // objectives done in the current stage
        float timer = 0.0f;                // survive objectives
        int stage = 0;                     // the current stage's index
    };

    // What happened, for the game to show ("Quest started: ...").
    struct Notice {
        enum class Kind { QuestStarted, ObjectiveDone, QuestCompleted, QuestFailed, StageChanged } kind;
        std::string quest;
        std::string text;
    };

    // Effects only the game can carry out (15.1-15.5): items, gold, experience,
    // reputation, cutscenes. Collected here; the game takes and applies them.
    std::vector<StoryEffect> TakeWorldEffects() {
        std::vector<StoryEffect> out;
        out.swap(worldEffects_);
        return out;
    }
    // Answers conditions about the world (items, level, attributes, time).
    void SetWorldConditions(std::function<bool(const nlohmann::json&)> check) { worldConditions_ = std::move(check); }
    // A dialogue skill check (15.3): {"attribute": "speech", "difficulty": 5} asks the
    // world whether the player has at least that; anything else is a world condition.
    bool Passes(const nlohmann::json& check) const;

    // Story variables (15.2): numbers and text the story remembers.
    nlohmann::json Var(const std::string& name) const;
    void SetVar(const std::string& name, const nlohmann::json& value);

    // The journal (15.4): every stage's line so far, newest last.
    struct JournalEntry {
        std::string quest;
        int stage = 0;
        std::string text;
    };
    const std::vector<JournalEntry>& Journal() const { return journal_; }
    // The current stage's index of a quest (0 when not active).
    int StageOf(const std::string& quest, const StoryLibrary& library) const;
    // The objectives open now in a quest (for the journal and markers).
    std::vector<const QuestObjective*> OpenObjectives(const std::string& quest, const StoryLibrary& library) const;
    const std::vector<std::string>& StartedOrder() const { return startedOrder_; }

    void FailQuest(const std::string& id, const StoryLibrary& library);
    void SetStage(const std::string& id, int stage, const StoryLibrary& library);
    // Game events for the v2 objectives.
    void OnDelivered(const std::string& character, const std::function<bool(const std::string& item, int count)>& take,
                     const StoryLibrary& library);
    void OnUsed(const std::string& thing, const StoryLibrary& library);
    void OnHave(const std::function<int(const std::string& item)>& count, const StoryLibrary& library);
    void OnEscorted(const std::function<bool(const std::string& who, const std::string& marker)>& arrived,
                    const StoryLibrary& library);
    void OnTime(float seconds, const StoryLibrary& library);
    // Someone died: an escort or defend objective about them fails its quest.
    void OnLost(const std::string& who, const StoryLibrary& library);

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
    // The player is at a Marker (reach objectives).
    void OnReached(const std::string& marker, const StoryLibrary& library);

    // The scene the player is in ("scenes/cellar.aige"). Objectives tied to a
    // scene count only there; arriving completes a "reach" objective that
    // names the scene but no marker.
    void SetScene(const std::string& scene, const StoryLibrary& library);
    const std::string& Scene() const { return scene_; }

    // Saving and loading a run's progress (development_plan.md 6.9): flags,
    // quest states and the order they started. Notices are not saved.
    nlohmann::json ToJson() const;
    static StoryState FromJson(const nlohmann::json& json);

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
    void Advance(const std::string& id, const StoryLibrary& library); // the current stage is done
    void EnterStage(const std::string& id, size_t position, const StoryLibrary& library);
    void MarkDone(const std::string& id, const QuestObjective& objective, const StoryLibrary& library);
    // Adds what `amount` says (or sets it, `absolute`) to the open objectives of a type.
    void Count(const std::string& type, const std::function<int(const QuestObjective&, const std::string&)>& amount,
               const StoryLibrary& library, bool absolute = false);
    void CheckArrival(const StoryLibrary& library);

    std::set<std::string> flags_;
    std::map<std::string, nlohmann::json> vars_;
    std::map<std::string, Progress> quests_;
    std::vector<std::string> startedOrder_; // which active quest is shown first
    std::vector<Notice> notices_;
    std::vector<StoryEffect> worldEffects_;
    std::vector<JournalEntry> journal_;
    std::function<bool(const nlohmann::json&)> worldConditions_;
    std::string scene_;
};

// Conditions and effects from their JSON (the files' "if" and "do").
std::vector<StoryCondition> StoryConditionsFromJson(const nlohmann::json& list);
std::vector<StoryEffect> StoryEffectsFromJson(const nlohmann::json& list);

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
    // How the last choice's skill check went (none: it had no check).
    std::optional<bool> LastCheck() const { return lastCheck_; }

private:
    void Enter(const std::string& id, StoryState& state, const StoryLibrary& library);

    const Dialogue* dialogue_ = nullptr;
    const DialogueNode* node_ = nullptr;
    std::vector<size_t> visible_;
    int highlighted_ = 0;
    std::optional<bool> lastCheck_;
};

} // namespace aige
