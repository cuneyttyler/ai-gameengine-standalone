#include "gameplay/Story.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace aige {

namespace fs = std::filesystem;

namespace {

const std::set<std::string> kEffectOps = {"set_flag", "clear_flag", "start_quest", "complete_quest",
                                          "complete_objective"};
const std::set<std::string> kObjectiveTypes = {"defeat", "talk", "collect", "reach"};
const std::set<std::string> kQuestStates = {"inactive", "active", "completed"};

std::vector<StoryCondition> ConditionsFromJson(const nlohmann::json& list) {
    std::vector<StoryCondition> conditions;
    if (!list.is_array()) {
        return conditions;
    }
    for (const auto& item : list) {
        StoryCondition condition;
        if (item.contains("flag")) {
            condition.flag = item.value("flag", "");
            condition.flagValue = item.contains("is") && item["is"].is_boolean() ? item["is"].get<bool>() : true;
        } else {
            condition.quest = item.value("quest", "");
            condition.questState = item.value("is", "active");
        }
        conditions.push_back(std::move(condition));
    }
    return conditions;
}

nlohmann::json ConditionsToJson(const std::vector<StoryCondition>& conditions) {
    nlohmann::json list = nlohmann::json::array();
    for (const auto& c : conditions) {
        if (!c.flag.empty()) {
            nlohmann::json item = {{"flag", c.flag}};
            if (!c.flagValue) item["is"] = false;
            list.push_back(item);
        } else {
            list.push_back({{"quest", c.quest}, {"is", c.questState}});
        }
    }
    return list;
}

std::vector<StoryEffect> EffectsFromJson(const nlohmann::json& list) {
    std::vector<StoryEffect> effects;
    if (!list.is_array()) {
        return effects;
    }
    for (const auto& item : list) {
        StoryEffect effect;
        effect.op = item.value("op", "");
        effect.name = item.contains("flag") ? item.value("flag", "") : item.value("quest", "");
        effect.objective = item.value("objective", "");
        effects.push_back(std::move(effect));
    }
    return effects;
}

nlohmann::json EffectsToJson(const std::vector<StoryEffect>& effects) {
    nlohmann::json list = nlohmann::json::array();
    for (const auto& e : effects) {
        bool isFlag = e.op == "set_flag" || e.op == "clear_flag";
        nlohmann::json item = {{"op", e.op}, {isFlag ? "flag" : "quest", e.name}};
        if (!e.objective.empty()) item["objective"] = e.objective;
        list.push_back(item);
    }
    return list;
}

std::optional<nlohmann::json> ReadJson(const fs::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        if (error) *error = "isn't a JSON object";
        return std::nullopt;
    }
    return json;
}

} // namespace

// --- Data --------------------------------------------------------------------

const DialogueNode* Dialogue::Node(const std::string& id) const {
    for (const auto& node : nodes) {
        if (node.id == id) {
            return &node;
        }
    }
    return nullptr;
}

std::optional<Dialogue> DialogueFromJson(const nlohmann::json& json, const std::string& path, std::string* error) {
    if (!json.is_object() || !json.contains("nodes") || !json["nodes"].is_array()) {
        if (error) *error = "needs a \"nodes\" list";
        return std::nullopt;
    }
    Dialogue dialogue;
    dialogue.path = path;
    dialogue.speaker = json.value("speaker", "");
    dialogue.start = json.value("start", "");
    for (const auto& start : json.value("starts", nlohmann::json::array())) {
        dialogue.starts.push_back({ConditionsFromJson(start.value("if", nlohmann::json::array())), start.value("node", "")});
    }
    for (const auto& item : json["nodes"]) {
        DialogueNode node;
        node.id = item.value("id", "");
        node.speaker = item.value("speaker", "");
        node.text = item.value("text", "");
        node.next = item.value("next", "");
        node.effects = EffectsFromJson(item.value("do", nlohmann::json::array()));
        for (const auto& c : item.value("choices", nlohmann::json::array())) {
            DialogueChoice choice;
            choice.text = c.value("text", "");
            choice.next = c.value("next", "");
            choice.conditions = ConditionsFromJson(c.value("if", nlohmann::json::array()));
            choice.effects = EffectsFromJson(c.value("do", nlohmann::json::array()));
            node.choices.push_back(std::move(choice));
        }
        dialogue.nodes.push_back(std::move(node));
    }
    if (dialogue.start.empty() && !dialogue.nodes.empty()) {
        dialogue.start = dialogue.nodes.front().id;
    }
    return dialogue;
}

nlohmann::json DialogueToJson(const Dialogue& dialogue) {
    nlohmann::json json = {{"speaker", dialogue.speaker}, {"start", dialogue.start}};
    if (!dialogue.starts.empty()) {
        nlohmann::json starts = nlohmann::json::array();
        for (const auto& s : dialogue.starts) {
            starts.push_back({{"if", ConditionsToJson(s.conditions)}, {"node", s.node}});
        }
        json["starts"] = starts;
    }
    nlohmann::json nodes = nlohmann::json::array();
    for (const auto& node : dialogue.nodes) {
        nlohmann::json n = {{"id", node.id}};
        if (!node.speaker.empty()) n["speaker"] = node.speaker;
        n["text"] = node.text;
        if (!node.effects.empty()) n["do"] = EffectsToJson(node.effects);
        if (!node.choices.empty()) {
            nlohmann::json choices = nlohmann::json::array();
            for (const auto& c : node.choices) {
                nlohmann::json choice = {{"text", c.text}, {"next", c.next}};
                if (!c.conditions.empty()) choice["if"] = ConditionsToJson(c.conditions);
                if (!c.effects.empty()) choice["do"] = EffectsToJson(c.effects);
                choices.push_back(choice);
            }
            n["choices"] = choices;
        } else {
            n["next"] = node.next;
        }
        nodes.push_back(n);
    }
    json["nodes"] = nodes;
    return json;
}

std::optional<Quest> QuestFromJson(const nlohmann::json& json, const std::string& id, std::string* error) {
    if (!json.is_object() || !json.contains("objectives") || !json["objectives"].is_array()) {
        if (error) *error = "needs an \"objectives\" list";
        return std::nullopt;
    }
    Quest quest;
    quest.id = id;
    quest.name = json.value("name", "");
    quest.description = json.value("description", "");
    quest.giver = json.value("giver", "");
    for (const auto& item : json["objectives"]) {
        QuestObjective objective;
        objective.id = item.value("id", "");
        objective.text = item.value("text", "");
        objective.type = item.value("type", "");
        objective.target = item.value("target", "");
        objective.count = std::max(1, item.value("count", 1));
        objective.scene = item.value("scene", "");
        quest.objectives.push_back(std::move(objective));
    }
    quest.onComplete = EffectsFromJson(json.value("onComplete", nlohmann::json::array()));
    return quest;
}

nlohmann::json QuestToJson(const Quest& quest) {
    nlohmann::json objectives = nlohmann::json::array();
    for (const auto& o : quest.objectives) {
        nlohmann::json item = {{"id", o.id}, {"text", o.text}, {"type", o.type}, {"target", o.target}};
        if (o.count != 1) item["count"] = o.count;
        if (!o.scene.empty()) item["scene"] = o.scene;
        objectives.push_back(item);
    }
    nlohmann::json json = {{"name", quest.name},
                           {"description", quest.description},
                           {"giver", quest.giver},
                           {"objectives", objectives}};
    if (!quest.onComplete.empty()) json["onComplete"] = EffectsToJson(quest.onComplete);
    return json;
}

// --- Library -----------------------------------------------------------------

StoryLibrary::StoryLibrary(fs::path projectRoot) : root_(std::move(projectRoot)) {}

void StoryLibrary::Refresh() {
    dialogues_.clear();
    quests_.clear();
    loadProblems_.clear();
    std::error_code ec;
    auto files = [&](const char* folder) {
        std::vector<fs::path> paths;
        if (fs::is_directory(root_ / folder, ec)) {
            for (const auto& file : fs::directory_iterator(root_ / folder, ec)) {
                if (file.is_regular_file() && file.path().extension() == ".json") {
                    paths.push_back(file.path());
                }
            }
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    };
    for (const auto& path : files("dialogue")) {
        std::string relative = "dialogue/" + path.filename().string();
        std::string error;
        auto json = ReadJson(path, &error);
        auto dialogue = json ? DialogueFromJson(*json, relative, &error) : std::nullopt;
        if (dialogue) {
            dialogues_.push_back(std::move(*dialogue));
        } else {
            loadProblems_.push_back({relative, error});
        }
    }
    for (const auto& path : files("quests")) {
        std::string relative = "quests/" + path.filename().string();
        std::string error;
        auto json = ReadJson(path, &error);
        auto quest = json ? QuestFromJson(*json, path.stem().string(), &error) : std::nullopt;
        if (quest) {
            quests_.push_back(std::move(*quest));
        } else {
            loadProblems_.push_back({relative, error});
        }
    }
}

const Dialogue* StoryLibrary::FindDialogue(const std::string& path) const {
    for (const auto& dialogue : dialogues_) {
        if (dialogue.path == path) {
            return &dialogue;
        }
    }
    return nullptr;
}

const Quest* StoryLibrary::FindQuest(const std::string& id) const {
    for (const auto& quest : quests_) {
        if (quest.id == id) {
            return &quest;
        }
    }
    return nullptr;
}

namespace {

void CheckEffects(std::vector<std::string>& problems, const std::string& where, const std::vector<StoryEffect>& effects,
                  const QuestLookup& findQuest) {
    for (const auto& effect : effects) {
        if (!kEffectOps.contains(effect.op)) {
            problems.push_back(where + ": unknown effect '" + effect.op + "'");
            continue;
        }
        bool questOp = effect.op.find("quest") != std::string::npos || effect.op == "complete_objective";
        if (questOp) {
            const Quest* quest = findQuest(effect.name);
            if (quest == nullptr) {
                problems.push_back(where + ": no quest '" + effect.name + "'");
            } else if (effect.op == "complete_objective" &&
                       std::none_of(quest->objectives.begin(), quest->objectives.end(),
                                    [&](const QuestObjective& o) { return o.id == effect.objective; })) {
                problems.push_back(where + ": quest '" + effect.name + "' has no objective '" + effect.objective + "'");
            }
        } else if (effect.name.empty()) {
            problems.push_back(where + ": " + effect.op + " needs a flag name");
        }
    }
}

void CheckConditions(std::vector<std::string>& problems, const std::string& where,
                     const std::vector<StoryCondition>& conditions, const QuestLookup& findQuest) {
    for (const auto& c : conditions) {
        if (c.flag.empty() && findQuest(c.quest) == nullptr) {
            problems.push_back(where + ": condition on unknown quest '" + c.quest + "'");
        } else if (c.flag.empty() && !kQuestStates.contains(c.questState)) {
            problems.push_back(where + ": quest state should be inactive, active or completed");
        }
    }
}

} // namespace

std::vector<std::string> ValidateDialogue(const Dialogue& d, const QuestLookup& findQuest) {
    std::vector<std::string> problems;
    std::set<std::string> ids;
    for (const auto& node : d.nodes) {
        if (node.id.empty()) {
            problems.push_back("a node has no id");
        } else if (!ids.insert(node.id).second) {
            problems.push_back("two nodes are called '" + node.id + "'");
        }
    }
    if (d.Node(d.start) == nullptr) {
        problems.push_back("starts at '" + d.start + "', which isn't a node");
    }
    // Reachability from every start.
    std::set<std::string> reached;
    std::vector<std::string> stack = {d.start};
    for (const auto& s : d.starts) {
        if (d.Node(s.node) == nullptr) {
            problems.push_back("a start goes to '" + s.node + "', which isn't a node");
        }
        stack.push_back(s.node);
        CheckConditions(problems, "start '" + s.node + "'", s.conditions, findQuest);
    }
    while (!stack.empty()) {
        std::string id = stack.back();
        stack.pop_back();
        const DialogueNode* node = d.Node(id);
        if (node == nullptr || !reached.insert(id).second) {
            continue;
        }
        if (node->choices.empty() && !node->next.empty()) stack.push_back(node->next);
        for (const auto& choice : node->choices) {
            if (!choice.next.empty()) stack.push_back(choice.next);
        }
    }
    for (const auto& node : d.nodes) {
        const std::string where = "node '" + node.id + "'";
        if (node.text.empty()) {
            problems.push_back(where + " has no text");
        }
        if (!node.id.empty() && !reached.contains(node.id)) {
            problems.push_back(where + " can't be reached");
        }
        if (node.choices.empty() && !node.next.empty() && d.Node(node.next) == nullptr) {
            problems.push_back(where + " continues to '" + node.next + "', which isn't a node");
        }
        CheckEffects(problems, where, node.effects, findQuest);
        for (size_t i = 0; i < node.choices.size(); ++i) {
            const auto& choice = node.choices[i];
            const std::string at = where + " choice " + std::to_string(i + 1);
            if (choice.text.empty()) {
                problems.push_back(at + " has no text");
            }
            if (!choice.next.empty() && d.Node(choice.next) == nullptr) {
                problems.push_back(at + " goes to '" + choice.next + "', which isn't a node");
            }
            CheckConditions(problems, at, choice.conditions, findQuest);
            CheckEffects(problems, at, choice.effects, findQuest);
        }
        if (node.choices.size() > 4) {
            problems.push_back(where + " has more than 4 choices (only 1-4 can be picked by key)");
        }
    }
    return problems;
}

std::vector<std::string> ValidateQuest(const Quest& q, const QuestLookup& findQuest) {
    std::vector<std::string> problems;
    if (q.name.empty()) {
        problems.push_back("has no name");
    }
    if (q.objectives.empty()) {
        problems.push_back("has no objectives");
    }
    std::set<std::string> ids;
    for (const auto& o : q.objectives) {
        const std::string where = "objective '" + o.id + "'";
        if (o.id.empty() || !ids.insert(o.id).second) {
            problems.push_back("objective ids must be present and unique");
        }
        if (!kObjectiveTypes.contains(o.type)) {
            problems.push_back(where + ": type should be defeat, talk, collect or reach");
        }
        if (o.target.empty() && !(o.type == "reach" && !o.scene.empty())) {
            problems.push_back(where + (o.type == "reach" ? " needs a marker or a scene to reach" : " has no target"));
        }
        if (!o.scene.empty() && (o.scene.rfind("scenes/", 0) != 0 || o.scene.size() < 13 ||
                                 o.scene.substr(o.scene.size() - 5) != ".aige")) {
            problems.push_back(where + ": scene should be a scene file, scenes/<name>.aige");
        }
        if (o.text.empty()) {
            problems.push_back(where + " has no text to show");
        }
    }
    CheckEffects(problems, "onComplete", q.onComplete, findQuest);
    return problems;
}

std::vector<StoryLibrary::Problem> StoryLibrary::Validate() const {
    std::vector<Problem> problems = loadProblems_;
    QuestLookup find = [this](const std::string& id) { return FindQuest(id); };
    for (const auto& d : dialogues_) {
        for (auto& message : ValidateDialogue(d, find)) {
            problems.push_back({d.path, std::move(message)});
        }
    }
    for (const auto& q : quests_) {
        for (auto& message : ValidateQuest(q, find)) {
            problems.push_back({"quests/" + q.id + ".json", std::move(message)});
        }
    }
    return problems;
}

std::optional<std::string> WriteStoryFile(const fs::path& projectRoot, const std::string& folder, const std::string& id,
                                          const nlohmann::json& content) {
    if (folder != "dialogue" && folder != "quests") {
        return "stories live in dialogue/ or quests/";
    }
    bool safe = !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (std::isalnum(c) && !std::isupper(c)) || c == '-' || c == '_';
    });
    if (!safe) {
        return "file names are lowercase letters, digits, - and _";
    }
    const std::string relative = folder + "/" + id + ".json";
    std::string error;
    nlohmann::json normalized;
    if (folder == "quests") {
        auto quest = QuestFromJson(content, id, &error);
        if (!quest) return error;
        normalized = QuestToJson(*quest);
    } else {
        auto dialogue = DialogueFromJson(content, relative, &error);
        if (!dialogue) return error;
        normalized = DialogueToJson(*dialogue);
    }
    std::error_code ec;
    fs::create_directories(projectRoot / folder, ec);
    std::ofstream out(projectRoot / relative, std::ios::binary | std::ios::trunc);
    if (!out) {
        return "couldn't write " + relative;
    }
    out << normalized.dump(2) << "\n";
    return std::nullopt;
}

// --- State -------------------------------------------------------------------

void StoryState::SetFlag(const std::string& name, bool value) {
    if (value) {
        flags_.insert(name);
    } else {
        flags_.erase(name);
    }
}

StoryState::Progress StoryState::Quest(const std::string& id) const {
    auto it = quests_.find(id);
    return it == quests_.end() ? Progress{} : it->second;
}

const char* StoryState::StateName(QuestState state) {
    switch (state) {
        case QuestState::Active: return "active";
        case QuestState::Completed: return "completed";
        default: return "inactive";
    }
}

bool StoryState::Holds(const std::vector<StoryCondition>& conditions) const {
    for (const auto& c : conditions) {
        if (!c.flag.empty()) {
            if (Flag(c.flag) != c.flagValue) return false;
        } else if (StateName(Quest(c.quest).state) != c.questState) {
            return false;
        }
    }
    return true;
}

void StoryState::Apply(const std::vector<StoryEffect>& effects, const StoryLibrary& library) {
    for (const auto& e : effects) {
        if (e.op == "set_flag") SetFlag(e.name, true);
        else if (e.op == "clear_flag") SetFlag(e.name, false);
        else if (e.op == "start_quest") StartQuest(e.name, library);
        else if (e.op == "complete_quest") CompleteQuest(e.name, library);
        else if (e.op == "complete_objective") CompleteObjective(e.name, e.objective, library);
    }
}

void StoryState::StartQuest(const std::string& id, const StoryLibrary& library) {
    const aige::Quest* quest = library.FindQuest(id);
    if (quest == nullptr || quests_[id].state != QuestState::Inactive) {
        return;
    }
    quests_[id] = {QuestState::Active, 0, 0};
    startedOrder_.push_back(id);
    notices_.push_back({Notice::Kind::QuestStarted, id, quest->name});
    if (quest->objectives.empty()) {
        CompleteQuest(id, library);
        return;
    }
    CheckArrival(library); // "go to the cellar" while already in it
}

void StoryState::CompleteQuest(const std::string& id, const StoryLibrary& library) {
    const aige::Quest* quest = library.FindQuest(id);
    auto& progress = quests_[id];
    if (quest == nullptr || progress.state == QuestState::Completed) {
        return;
    }
    progress.state = QuestState::Completed;
    notices_.push_back({Notice::Kind::QuestCompleted, id, quest->name});
    Apply(quest->onComplete, library);
}

void StoryState::Advance(const std::string& id, const StoryLibrary& library) {
    const aige::Quest* quest = library.FindQuest(id);
    auto& progress = quests_[id];
    if (quest == nullptr || progress.objective >= quest->objectives.size()) return;
    notices_.push_back({Notice::Kind::ObjectiveDone, id, quest->objectives[progress.objective].text});
    progress.objective += 1;
    progress.count = 0;
    if (progress.objective >= quest->objectives.size()) {
        CompleteQuest(id, library);
        return;
    }
    CheckArrival(library);
}

void StoryState::CompleteObjective(const std::string& id, const std::string& objective, const StoryLibrary& library) {
    const aige::Quest* quest = library.FindQuest(id);
    auto it = quests_.find(id);
    if (quest == nullptr || it == quests_.end() || it->second.state != QuestState::Active ||
        it->second.objective >= quest->objectives.size()) {
        return;
    }
    // Only the current objective (they're done in order).
    if (quest->objectives[it->second.objective].id == objective) {
        Advance(id, library);
    }
}

void StoryState::Count(const std::string& type, const std::function<bool(const std::string&)>& matches,
                       const StoryLibrary& library) {
    std::vector<std::string> active = startedOrder_; // Advance may start other quests
    for (const auto& id : active) {
        const aige::Quest* quest = library.FindQuest(id);
        auto& progress = quests_[id];
        // (A loaded save may point past a quest that was edited since.)
        if (quest == nullptr || progress.state != QuestState::Active || progress.objective >= quest->objectives.size()) {
            continue;
        }
        const QuestObjective& objective = quest->objectives[progress.objective];
        const bool here = objective.scene.empty() || objective.scene == scene_;
        if (objective.type == type && here && matches(objective.target)) {
            if (++progress.count >= objective.count) {
                Advance(id, library);
            }
        }
    }
}

void StoryState::OnReached(const std::string& marker, const StoryLibrary& library) {
    Count("reach", [&](const std::string& target) { return !target.empty() && target == marker; }, library);
}

void StoryState::SetScene(const std::string& scene, const StoryLibrary& library) {
    scene_ = scene;
    CheckArrival(library);
}

// A current "reach" objective that names this scene and no marker is done on arrival.
void StoryState::CheckArrival(const StoryLibrary& library) {
    if (scene_.empty()) return;
    std::vector<std::string> active = startedOrder_;
    for (const auto& id : active) {
        const aige::Quest* quest = library.FindQuest(id);
        auto it = quests_.find(id);
        if (quest == nullptr || it == quests_.end() || it->second.state != QuestState::Active ||
            it->second.objective >= quest->objectives.size()) {
            continue;
        }
        const QuestObjective& objective = quest->objectives[it->second.objective];
        if (objective.type == "reach" && objective.target.empty() && objective.scene == scene_) {
            Advance(id, library); // checks the next objective in turn
        }
    }
}

nlohmann::json StoryState::ToJson() const {
    nlohmann::json quests = nlohmann::json::object();
    for (const auto& [id, progress] : quests_) {
        if (progress.state == QuestState::Inactive) continue;
        quests[id] = {{"state", StateName(progress.state)}, {"objective", progress.objective}, {"count", progress.count}};
    }
    return {{"flags", std::vector<std::string>(flags_.begin(), flags_.end())},
            {"quests", quests},
            {"order", startedOrder_},
            {"scene", scene_}};
}

StoryState StoryState::FromJson(const nlohmann::json& json) {
    StoryState state;
    if (!json.is_object()) return state;
    for (const auto& flag : json.value("flags", nlohmann::json::array())) {
        if (flag.is_string()) state.flags_.insert(flag.get<std::string>());
    }
    const auto quests = json.value("quests", nlohmann::json::object());
    if (quests.is_object()) {
        for (const auto& [id, entry] : quests.items()) {
            if (!entry.is_object()) continue;
            const std::string name = entry.value("state", "inactive");
            Progress progress;
            progress.state = name == "active" ? QuestState::Active : name == "completed" ? QuestState::Completed : QuestState::Inactive;
            progress.objective = entry.value("objective", static_cast<size_t>(0));
            progress.count = entry.value("count", 0);
            state.quests_[id] = progress;
        }
    }
    for (const auto& id : json.value("order", nlohmann::json::array())) {
        if (id.is_string() && state.quests_.contains(id.get<std::string>())) state.startedOrder_.push_back(id.get<std::string>());
    }
    state.scene_ = json.value("scene", "");
    return state;
}

void StoryState::OnDefeated(const std::string& prefab, const std::string& team, const StoryLibrary& library) {
    Count("defeat", [&](const std::string& target) { return target == prefab || (!team.empty() && target == team); },
          library);
}

void StoryState::OnCollected(const std::string& prefab, const StoryLibrary& library) {
    Count("collect", [&](const std::string& target) { return target == prefab; }, library);
}

void StoryState::OnTalkedTo(const std::string& character, const StoryLibrary& library) {
    Count("talk", [&](const std::string& target) { return target == character; }, library);
}

std::optional<StoryState::Shown> StoryState::CurrentObjective(const StoryLibrary& library) const {
    for (const auto& id : startedOrder_) {
        const aige::Quest* quest = library.FindQuest(id);
        Progress progress = Quest(id);
        if (quest == nullptr || progress.state != QuestState::Active || progress.objective >= quest->objectives.size()) {
            continue;
        }
        const QuestObjective& objective = quest->objectives[progress.objective];
        std::string text = objective.text;
        if (objective.count > 1) {
            text += " (" + std::to_string(progress.count) + "/" + std::to_string(objective.count) + ")";
        }
        return Shown{quest->name, text};
    }
    return std::nullopt;
}

bool StoryState::AnyQuestActive() const {
    return std::any_of(quests_.begin(), quests_.end(),
                       [](const auto& entry) { return entry.second.state == QuestState::Active; });
}

std::vector<StoryState::Notice> StoryState::TakeNotices() {
    std::vector<Notice> notices;
    notices.swap(notices_);
    return notices;
}

// --- Conversation ------------------------------------------------------------

bool DialogueRunner::Start(const Dialogue& dialogue, StoryState& state, const StoryLibrary& library) {
    std::string start = dialogue.start;
    for (const auto& s : dialogue.starts) {
        if (state.Holds(s.conditions)) {
            start = s.node;
            break;
        }
    }
    dialogue_ = &dialogue;
    Enter(start, state, library);
    return Active();
}

void DialogueRunner::Enter(const std::string& id, StoryState& state, const StoryLibrary& library) {
    node_ = id.empty() || dialogue_ == nullptr ? nullptr : dialogue_->Node(id);
    visible_.clear();
    highlighted_ = 0;
    if (node_ == nullptr) {
        dialogue_ = nullptr;
        return;
    }
    state.Apply(node_->effects, library);
    for (size_t i = 0; i < node_->choices.size(); ++i) {
        if (state.Holds(node_->choices[i].conditions)) {
            visible_.push_back(i);
        }
    }
}

void DialogueRunner::MoveHighlight(int delta) {
    if (visible_.empty()) {
        return;
    }
    int count = static_cast<int>(visible_.size());
    highlighted_ = ((highlighted_ + delta) % count + count) % count;
}

void DialogueRunner::Choose(size_t index, StoryState& state, const StoryLibrary& library) {
    if (node_ == nullptr || index >= visible_.size()) {
        return;
    }
    const DialogueChoice& choice = node_->choices[visible_[index]];
    std::string next = choice.next;
    state.Apply(choice.effects, library);
    Enter(next, state, library);
}

void DialogueRunner::Continue(StoryState& state, const StoryLibrary& library) {
    if (node_ == nullptr) {
        return;
    }
    if (!visible_.empty()) {
        Choose(static_cast<size_t>(highlighted_), state, library);
    } else {
        Enter(node_->next, state, library);
    }
}

} // namespace aige
