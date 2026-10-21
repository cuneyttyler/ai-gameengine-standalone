#include "gameplay/Story.h"

#include "gameplay/Localization.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace aige {

namespace fs = std::filesystem;

namespace {

const std::set<std::string> kEffectOps = {"set_flag",   "clear_flag", "start_quest", "complete_quest", "complete_objective",
                                          "fail_quest", "set_stage",  "set_var",     "add_var",        "give_item",
                                          "take_item",  "give_gold",  "give_xp",     "reputation",     "play_cutscene"};
// Effects only the game can carry out (StoryState::TakeWorldEffects).
const std::set<std::string> kWorldOps = {"give_item", "take_item", "give_gold", "give_xp", "reputation", "play_cutscene"};
const std::set<std::string> kObjectiveTypes = {"defeat", "talk", "collect", "reach", "deliver", "use", "examine",
                                               "have", "survive", "defend", "escort"};
const std::set<std::string> kQuestStates = {"inactive", "active", "completed", "failed"};

std::vector<StoryCondition> ConditionsFromJson(const nlohmann::json& list) {
    std::vector<StoryCondition> conditions;
    if (!list.is_array()) {
        return conditions;
    }
    for (const auto& item : list) {
        StoryCondition condition;
        if (!item.is_object()) continue;
        if (item.contains("flag")) {
            condition.flag = item.value("flag", "");
            condition.flagValue = item.contains("is") && item["is"].is_boolean() ? item["is"].get<bool>() : true;
        } else if (item.contains("quest")) {
            condition.quest = item.value("quest", "");
            condition.questState = item.value("is", "active");
            condition.stage = item.value("stage", -1);
        } else {
            condition.other = item; // a variable, or something about the world (15.2)
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
        } else if (!c.quest.empty()) {
            nlohmann::json item = {{"quest", c.quest}, {"is", c.questState}};
            if (c.stage >= 0) item["stage"] = c.stage;
            list.push_back(item);
        } else if (c.other.is_object()) {
            list.push_back(c.other);
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
        if (!item.is_object()) continue;
        StoryEffect effect;
        effect.op = item.value("op", "");
        // The name: whichever of these the op uses.
        for (const char* key : {"flag", "quest", "var", "item", "faction", "cutscene"}) {
            if (item.contains(key) && item[key].is_string()) {
                effect.name = item[key].get<std::string>();
                break;
            }
        }
        effect.objective = item.value("objective", "");
        effect.stage = item.value("stage", 0);
        if (item.contains("value")) effect.value = item["value"];
        else if (item.contains("count")) effect.value = item["count"];
        else if (item.contains("amount")) effect.value = item["amount"];
        effects.push_back(std::move(effect));
    }
    return effects;
}

nlohmann::json EffectsToJson(const std::vector<StoryEffect>& effects) {
    nlohmann::json list = nlohmann::json::array();
    for (const auto& e : effects) {
        const char* key = e.op == "set_flag" || e.op == "clear_flag"     ? "flag"
                          : e.op == "set_var" || e.op == "add_var"       ? "var"
                          : e.op == "give_item" || e.op == "take_item"   ? "item"
                          : e.op == "reputation"                         ? "faction"
                          : e.op == "play_cutscene"                      ? "cutscene"
                          : e.op == "give_gold" || e.op == "give_xp"     ? nullptr
                                                                         : "quest";
        nlohmann::json item = {{"op", e.op}};
        if (key) item[key] = e.name;
        if (!e.objective.empty()) item["objective"] = e.objective;
        if (e.op == "set_stage") item["stage"] = e.stage;
        if (!e.value.is_null()) {
            const char* as = e.op == "give_item" || e.op == "take_item" ? "count"
                             : e.op == "give_gold" || e.op == "give_xp" ? "amount"
                                                                        : "value";
            item[as] = e.value;
        }
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
        node.portrait = item.value("portrait", "");
        node.voice = item.value("voice", "");
        node.effects = EffectsFromJson(item.value("do", nlohmann::json::array()));
        for (const auto& c : item.value("choices", nlohmann::json::array())) {
            DialogueChoice choice;
            choice.text = c.value("text", "");
            choice.next = c.value("next", "");
            choice.conditions = ConditionsFromJson(c.value("if", nlohmann::json::array()));
            choice.effects = EffectsFromJson(c.value("do", nlohmann::json::array()));
            if (c.contains("check") && c["check"].is_object()) choice.check = c["check"];
            choice.fail = c.value("fail", "");
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
        if (!node.portrait.empty()) n["portrait"] = node.portrait;
        if (!node.voice.empty()) n["voice"] = node.voice;
        if (!node.effects.empty()) n["do"] = EffectsToJson(node.effects);
        if (!node.choices.empty()) {
            nlohmann::json choices = nlohmann::json::array();
            for (const auto& c : node.choices) {
                nlohmann::json choice = {{"text", c.text}, {"next", c.next}};
                if (!c.conditions.empty()) choice["if"] = ConditionsToJson(c.conditions);
                if (!c.effects.empty()) choice["do"] = EffectsToJson(c.effects);
                if (c.check.is_object()) choice["check"] = c.check;
                if (!c.fail.empty()) choice["fail"] = c.fail;
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

std::vector<StoryCondition> StoryConditionsFromJson(const nlohmann::json& list) { return ConditionsFromJson(list); }
std::vector<StoryEffect> StoryEffectsFromJson(const nlohmann::json& list) { return EffectsFromJson(list); }

const QuestStage* Quest::StageByIndex(int index) const {
    for (const auto& stage : stages) {
        if (stage.index == index) return &stage;
    }
    return nullptr;
}

std::string Quest::Resolve(const std::string& target) const {
    if (target.size() > 1 && target[0] == '@') {
        for (const auto& alias : aliases) {
            if (alias.id == target.substr(1)) return alias.fill;
        }
    }
    return target;
}

namespace {

QuestObjective ObjectiveFromJson(const nlohmann::json& item) {
    QuestObjective objective;
    objective.id = item.value("id", "");
    objective.text = item.value("text", "");
    objective.type = item.value("type", "");
    objective.target = item.value("target", "");
    objective.count = std::max(1, item.value("count", 1));
    objective.scene = item.value("scene", "");
    objective.optional = item.value("optional", false);
    objective.item = item.value("item", "");
    objective.to = item.value("to", "");
    objective.seconds = item.value("seconds", 0.0f);
    return objective;
}

nlohmann::json ObjectiveToJson(const QuestObjective& o) {
    nlohmann::json item = {{"id", o.id}, {"text", o.text}, {"type", o.type}, {"target", o.target}};
    if (o.count != 1) item["count"] = o.count;
    if (!o.scene.empty()) item["scene"] = o.scene;
    if (o.optional) item["optional"] = true;
    if (!o.item.empty()) item["item"] = o.item;
    if (!o.to.empty()) item["to"] = o.to;
    if (o.seconds > 0.0f) item["seconds"] = o.seconds;
    return item;
}

} // namespace

std::optional<Quest> QuestFromJson(const nlohmann::json& json, const std::string& id, std::string* error) {
    const bool staged = json.is_object() && json.contains("stages") && json["stages"].is_array();
    if (!json.is_object() || (!staged && (!json.contains("objectives") || !json["objectives"].is_array()))) {
        if (error) *error = "needs a \"stages\" or an \"objectives\" list";
        return std::nullopt;
    }
    Quest quest;
    quest.id = id;
    quest.name = json.value("name", "");
    quest.description = json.value("description", "");
    quest.giver = json.value("giver", "");
    quest.staged = staged;
    if (staged) {
        for (const auto& s : json["stages"]) {
            if (!s.is_object()) continue;
            QuestStage stage;
            stage.index = s.value("index", static_cast<int>(quest.stages.size() + 1) * 10);
            stage.journal = s.value("journal", "");
            for (const auto& o : s.value("objectives", nlohmann::json::array())) {
                stage.objectives.push_back(ObjectiveFromJson(o));
                quest.objectives.push_back(stage.objectives.back());
            }
            stage.onEnter = EffectsFromJson(s.value("onEnter", nlohmann::json::array()));
            for (const auto& b : s.value("branches", nlohmann::json::array())) {
                stage.branches.push_back({ConditionsFromJson(b.value("if", nlohmann::json::array())), b.value("stage", 0)});
            }
            stage.next = s.value("next", 0);
            stage.ends = s.value("ends", "");
            quest.stages.push_back(std::move(stage));
        }
    } else {
        // A v1 quest: its objectives one after another, a stage each.
        int index = 10;
        for (const auto& item : json["objectives"]) {
            QuestStage stage;
            stage.index = index;
            index += 10;
            stage.objectives.push_back(ObjectiveFromJson(item));
            stage.journal = stage.objectives.back().text;
            quest.objectives.push_back(stage.objectives.back());
            quest.stages.push_back(std::move(stage));
        }
    }
    for (const auto& a : json.value("aliases", nlohmann::json::array())) {
        if (a.is_object()) quest.aliases.push_back({a.value("id", ""), a.value("kind", "character"), a.value("fill", "")});
    }
    const nlohmann::json rewards = json.value("rewards", nlohmann::json::object());
    quest.rewards.experience = rewards.value("experience", 0.0f);
    quest.rewards.gold = rewards.value("gold", 0);
    quest.rewards.items = rewards.value("items", "");
    quest.rewards.faction = rewards.value("faction", "");
    quest.rewards.reputation = rewards.value("reputation", 0);
    quest.onComplete = EffectsFromJson(json.value("onComplete", nlohmann::json::array()));
    quest.onFail = EffectsFromJson(json.value("onFail", nlohmann::json::array()));
    return quest;
}

nlohmann::json QuestToJson(const Quest& quest) {
    nlohmann::json json = {{"name", quest.name}, {"description", quest.description}, {"giver", quest.giver}};
    if (quest.staged) {
        nlohmann::json stages = nlohmann::json::array();
        for (const auto& s : quest.stages) {
            nlohmann::json stage = {{"index", s.index}};
            if (!s.journal.empty()) stage["journal"] = s.journal;
            nlohmann::json objectives = nlohmann::json::array();
            for (const auto& o : s.objectives) objectives.push_back(ObjectiveToJson(o));
            stage["objectives"] = objectives;
            if (!s.onEnter.empty()) stage["onEnter"] = EffectsToJson(s.onEnter);
            if (!s.branches.empty()) {
                nlohmann::json branches = nlohmann::json::array();
                for (const auto& b : s.branches) branches.push_back({{"if", ConditionsToJson(b.conditions)}, {"stage", b.stage}});
                stage["branches"] = branches;
            }
            if (s.next) stage["next"] = s.next;
            if (!s.ends.empty()) stage["ends"] = s.ends;
            stages.push_back(stage);
        }
        json["stages"] = stages;
    } else {
        nlohmann::json objectives = nlohmann::json::array();
        for (const auto& o : quest.objectives) objectives.push_back(ObjectiveToJson(o));
        json["objectives"] = objectives;
    }
    if (!quest.aliases.empty()) {
        nlohmann::json aliases = nlohmann::json::array();
        for (const auto& a : quest.aliases) aliases.push_back({{"id", a.id}, {"kind", a.kind}, {"fill", a.fill}});
        json["aliases"] = aliases;
    }
    const auto& r = quest.rewards;
    if (r.experience > 0.0f || r.gold > 0 || !r.items.empty() || r.reputation != 0) {
        nlohmann::json rewards = nlohmann::json::object();
        if (r.experience > 0.0f) rewards["experience"] = r.experience;
        if (r.gold > 0) rewards["gold"] = r.gold;
        if (!r.items.empty()) rewards["items"] = r.items;
        if (r.reputation != 0) {
            rewards["faction"] = r.faction;
            rewards["reputation"] = r.reputation;
        }
        json["rewards"] = rewards;
    }
    if (!quest.onComplete.empty()) json["onComplete"] = EffectsToJson(quest.onComplete);
    if (!quest.onFail.empty()) json["onFail"] = EffectsToJson(quest.onFail);
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
        bool questOp = effect.op.find("quest") != std::string::npos || effect.op == "complete_objective" ||
                       effect.op == "set_stage";
        if (questOp) {
            const Quest* quest = findQuest(effect.name);
            if (quest == nullptr) {
                problems.push_back(where + ": no quest '" + effect.name + "'");
            } else if (effect.op == "complete_objective" &&
                       std::none_of(quest->objectives.begin(), quest->objectives.end(),
                                    [&](const QuestObjective& o) { return o.id == effect.objective; })) {
                problems.push_back(where + ": quest '" + effect.name + "' has no objective '" + effect.objective + "'");
            } else if (effect.op == "set_stage" && quest->StageByIndex(effect.stage) == nullptr) {
                problems.push_back(where + ": quest '" + effect.name + "' has no stage " + std::to_string(effect.stage));
            }
        } else if (effect.name.empty() && effect.op != "give_gold" && effect.op != "give_xp") {
            const char* what = effect.op.find("flag") != std::string::npos  ? "a flag name"
                               : effect.op.find("var") != std::string::npos ? "a variable name"
                               : effect.op == "reputation"                  ? "a faction"
                               : effect.op == "play_cutscene"               ? "a cutscene"
                                                                            : "an item";
            problems.push_back(where + ": " + effect.op + " needs " + what);
        }
    }
}

void CheckConditions(std::vector<std::string>& problems, const std::string& where,
                     const std::vector<StoryCondition>& conditions, const QuestLookup& findQuest) {
    for (const auto& c : conditions) {
        if (!c.flag.empty()) continue;
        if (c.quest.empty()) {
            if (!c.other.is_object() || c.other.empty()) problems.push_back(where + ": an empty condition");
            continue;
        }
        const Quest* quest = findQuest(c.quest);
        if (quest == nullptr) {
            problems.push_back(where + ": condition on unknown quest '" + c.quest + "'");
        } else if (!kQuestStates.contains(c.questState)) {
            problems.push_back(where + ": quest state should be inactive, active, completed or failed");
        } else if (c.stage >= 0 && quest->StageByIndex(c.stage) == nullptr) {
            problems.push_back(where + ": quest '" + c.quest + "' has no stage " + std::to_string(c.stage));
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
            if (!choice.fail.empty()) stack.push_back(choice.fail);
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
            if (!choice.fail.empty() && d.Node(choice.fail) == nullptr) {
                problems.push_back(at + " fails to '" + choice.fail + "', which isn't a node");
            }
            if (!choice.fail.empty() && !choice.check.is_object()) {
                problems.push_back(at + " has somewhere to go on failing but no check");
            }
            CheckConditions(problems, at, choice.conditions, findQuest);
            CheckEffects(problems, at, choice.effects, findQuest);
        }
        if (node.choices.size() > 9) {
            problems.push_back(where + " has more than 9 choices (only 1-9 can be picked by key)");
        }
    }
    return problems;
}

std::vector<std::string> ValidateQuest(const Quest& q, const QuestLookup& findQuest) {
    std::vector<std::string> problems;
    if (q.name.empty()) {
        problems.push_back("has no name");
    }
    if (q.stages.empty()) {
        problems.push_back(q.staged ? "has no stages" : "has no objectives");
    }
    std::set<std::string> ids;
    for (const auto& o : q.objectives) {
        const std::string where = "objective '" + o.id + "'";
        if (o.id.empty() || !ids.insert(o.id).second) {
            problems.push_back("objective ids must be present and unique");
        }
        if (!kObjectiveTypes.contains(o.type)) {
            problems.push_back(where + ": type should be defeat, talk, collect, reach, deliver, use, examine, have, "
                                       "survive, defend or escort");
        }
        const bool targetless = (o.type == "reach" && !o.scene.empty()) || o.type == "survive";
        if (o.target.empty() && !targetless) {
            problems.push_back(where + (o.type == "reach" ? " needs a marker or a scene to reach" : " has no target"));
        }
        if (o.type == "deliver" && o.item.empty()) problems.push_back(where + " needs an item to deliver");
        if (o.type == "escort" && o.to.empty()) problems.push_back(where + " needs a marker to bring them to");
        if ((o.type == "survive" || o.type == "defend") && o.seconds <= 0.0f) {
            problems.push_back(where + " needs a number of seconds");
        }
        for (const std::string* ref : {&o.target, &o.item}) {
            if (ref->size() > 1 && (*ref)[0] == '@' &&
                std::none_of(q.aliases.begin(), q.aliases.end(), [&](const QuestAlias& a) { return a.id == ref->substr(1); })) {
                problems.push_back(where + ": no alias '" + ref->substr(1) + "'");
            }
        }
        if (!o.scene.empty() && (o.scene.rfind("scenes/", 0) != 0 || o.scene.size() < 13 ||
                                 o.scene.substr(o.scene.size() - 5) != ".aige")) {
            problems.push_back(where + ": scene should be a scene file, scenes/<name>.aige");
        }
        if (o.text.empty()) {
            problems.push_back(where + " has no text to show");
        }
    }
    std::set<int> indices;
    for (const auto& s : q.stages) {
        if (!indices.insert(s.index).second) problems.push_back("two stages are numbered " + std::to_string(s.index));
    }
    for (const auto& s : q.stages) {
        const std::string where = "stage " + std::to_string(s.index);
        auto known = [&](int index) { return index == 0 || indices.contains(index); };
        if (!known(s.next)) problems.push_back(where + " goes on to stage " + std::to_string(s.next) + ", which isn't one");
        for (const auto& b : s.branches) {
            if (!known(b.stage) || b.stage == 0) {
                problems.push_back(where + " branches to stage " + std::to_string(b.stage) + ", which isn't one");
            }
            CheckConditions(problems, where, b.conditions, findQuest);
        }
        if (!s.ends.empty() && s.ends != "complete" && s.ends != "fail") {
            problems.push_back(where + ": ends should be complete or fail");
        }
        CheckEffects(problems, where, s.onEnter, findQuest);
    }
    std::set<std::string> aliases;
    for (const auto& a : q.aliases) {
        if (a.id.empty() || !aliases.insert(a.id).second) problems.push_back("alias ids must be present and unique");
        if (a.fill.empty()) problems.push_back("alias '" + a.id + "' isn't filled");
    }
    if (q.rewards.reputation != 0 && q.rewards.faction.empty()) problems.push_back("a reputation reward needs a faction");
    CheckEffects(problems, "onComplete", q.onComplete, findQuest);
    CheckEffects(problems, "onFail", q.onFail, findQuest);
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

namespace {

bool Truthy(const nlohmann::json& v) {
    if (v.is_null()) return false;
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) return v.get<double>() != 0.0;
    if (v.is_string()) return !v.get<std::string>().empty();
    return true;
}

bool Compare(const nlohmann::json& a, const std::string& op, const nlohmann::json& b) {
    if (a.is_number() && b.is_number()) {
        const double x = a.get<double>(), y = b.get<double>();
        if (op == "!=") return x != y;
        if (op == "<") return x < y;
        if (op == "<=") return x <= y;
        if (op == ">") return x > y;
        if (op == ">=") return x >= y;
        return x == y;
    }
    if (op == "!=") return a != b;
    return a == b;
}

// "potion*2, iron-sword" -> give_item effects.
std::vector<StoryEffect> ItemEffects(const std::string& list) {
    std::vector<StoryEffect> effects;
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(',', start);
        if (end == std::string::npos) end = list.size();
        std::string part = list.substr(start, end - start);
        part.erase(0, part.find_first_not_of(" \t"));
        part.erase(part.find_last_not_of(" \t") + 1);
        if (!part.empty()) {
            int count = 1;
            if (size_t star = part.find('*'); star != std::string::npos) {
                count = std::max(1, std::atoi(part.c_str() + star + 1));
                part = part.substr(0, star);
            }
            effects.push_back({"give_item", part, "", 0, count});
        }
        start = end + 1;
    }
    return effects;
}

} // namespace

void StoryState::SetFlag(const std::string& name, bool value) {
    if (value) {
        flags_.insert(name);
    } else {
        flags_.erase(name);
    }
}

nlohmann::json StoryState::Var(const std::string& name) const {
    auto it = vars_.find(name);
    return it == vars_.end() ? nlohmann::json() : it->second;
}

void StoryState::SetVar(const std::string& name, const nlohmann::json& value) {
    if (value.is_null()) vars_.erase(name);
    else vars_[name] = value;
}

StoryState::Progress StoryState::Quest(const std::string& id) const {
    auto it = quests_.find(id);
    return it == quests_.end() ? Progress{} : it->second;
}

const char* StoryState::StateName(QuestState state) {
    switch (state) {
        case QuestState::Active: return "active";
        case QuestState::Completed: return "completed";
        case QuestState::Failed: return "failed";
        default: return "inactive";
    }
}

int StoryState::StageOf(const std::string& id, const StoryLibrary& library) const {
    const aige::Quest* quest = library.FindQuest(id);
    Progress progress = Quest(id);
    if (quest == nullptr || progress.state != QuestState::Active || progress.objective >= quest->stages.size()) return 0;
    return quest->stages[progress.objective].index;
}

std::vector<const QuestObjective*> StoryState::OpenObjectives(const std::string& id, const StoryLibrary& library) const {
    std::vector<const QuestObjective*> open;
    const aige::Quest* quest = library.FindQuest(id);
    auto it = quests_.find(id);
    if (quest == nullptr || it == quests_.end() || it->second.state != QuestState::Active ||
        it->second.objective >= quest->stages.size()) {
        return open;
    }
    for (const auto& o : quest->stages[it->second.objective].objectives) {
        if (!it->second.done.contains(o.id)) open.push_back(&o);
    }
    return open;
}

bool StoryState::Holds(const std::vector<StoryCondition>& conditions) const {
    for (const auto& c : conditions) {
        if (!c.flag.empty()) {
            if (Flag(c.flag) != c.flagValue) return false;
        } else if (!c.quest.empty()) {
            Progress progress = Quest(c.quest);
            if (StateName(progress.state) != c.questState) return false;
            if (c.stage >= 0 && progress.stage != c.stage) return false;
        } else if (c.other.is_object() && c.other.contains("var")) {
            const nlohmann::json value = Var(c.other.value("var", ""));
            if (c.other.contains("value")) {
                if (!Compare(value, c.other.value("op", "=="), c.other["value"])) return false;
            } else if (!Truthy(value)) {
                return false;
            }
        } else if (c.other.is_object()) {
            if (!worldConditions_ || !worldConditions_(c.other)) return false; // nobody to ask: no
        }
    }
    return true;
}

bool StoryState::Passes(const nlohmann::json& check) const {
    if (!check.is_object()) return true;
    nlohmann::json condition = check;
    if (condition.contains("difficulty")) {
        condition["atLeast"] = condition["difficulty"];
        condition.erase("difficulty");
    }
    if (condition.contains("var")) return Holds({StoryCondition{"", true, "", "", -1, condition}});
    return worldConditions_ && worldConditions_(condition);
}

void StoryState::Apply(const std::vector<StoryEffect>& effects, const StoryLibrary& library) {
    for (const auto& e : effects) {
        if (e.op == "set_flag") SetFlag(e.name, true);
        else if (e.op == "clear_flag") SetFlag(e.name, false);
        else if (e.op == "start_quest") StartQuest(e.name, library);
        else if (e.op == "complete_quest") CompleteQuest(e.name, library);
        else if (e.op == "complete_objective") CompleteObjective(e.name, e.objective, library);
        else if (e.op == "fail_quest") FailQuest(e.name, library);
        else if (e.op == "set_stage") SetStage(e.name, e.stage, library);
        else if (e.op == "set_var") SetVar(e.name, e.value);
        else if (e.op == "add_var") {
            const nlohmann::json now = Var(e.name);
            const double by = e.value.is_number() ? e.value.get<double>() : 1.0;
            const double sum = (now.is_number() ? now.get<double>() : 0.0) + by;
            if (sum == static_cast<double>(static_cast<long long>(sum))) vars_[e.name] = static_cast<long long>(sum);
            else vars_[e.name] = sum;
        } else if (kWorldOps.contains(e.op)) {
            worldEffects_.push_back(e);
        }
    }
}

void StoryState::StartQuest(const std::string& id, const StoryLibrary& library) {
    const aige::Quest* quest = library.FindQuest(id);
    if (quest == nullptr || quests_[id].state != QuestState::Inactive) {
        return;
    }
    quests_[id] = Progress{};
    quests_[id].state = QuestState::Active;
    startedOrder_.push_back(id);
    notices_.push_back({Notice::Kind::QuestStarted, id, Tr("quest/" + id + "/name", quest->name)});
    EnterStage(id, 0, library);
}

void StoryState::CompleteQuest(const std::string& id, const StoryLibrary& library) {
    const aige::Quest* quest = library.FindQuest(id);
    auto& progress = quests_[id];
    if (quest == nullptr || progress.state == QuestState::Completed || progress.state == QuestState::Failed) {
        return;
    }
    progress.state = QuestState::Completed;
    notices_.push_back({Notice::Kind::QuestCompleted, id, Tr("quest/" + id + "/name", quest->name)});
    const auto& r = quest->rewards;
    if (r.experience > 0.0f) worldEffects_.push_back({"give_xp", "", "", 0, r.experience});
    if (r.gold > 0) worldEffects_.push_back({"give_gold", "", "", 0, r.gold});
    for (auto& effect : ItemEffects(r.items)) worldEffects_.push_back(std::move(effect));
    if (r.reputation != 0 && !r.faction.empty()) worldEffects_.push_back({"reputation", r.faction, "", 0, r.reputation});
    Apply(quest->onComplete, library);
}

void StoryState::FailQuest(const std::string& id, const StoryLibrary& library) {
    const aige::Quest* quest = library.FindQuest(id);
    auto it = quests_.find(id);
    if (quest == nullptr || it == quests_.end() || it->second.state != QuestState::Active) return;
    it->second.state = QuestState::Failed;
    notices_.push_back({Notice::Kind::QuestFailed, id, Tr("quest/" + id + "/name", quest->name)});
    Apply(quest->onFail, library);
}

void StoryState::SetStage(const std::string& id, int stage, const StoryLibrary& library) {
    const aige::Quest* quest = library.FindQuest(id);
    if (quest == nullptr) return;
    size_t position = quest->stages.size();
    for (size_t i = 0; i < quest->stages.size(); ++i) {
        if (quest->stages[i].index == stage) position = i;
    }
    if (position == quest->stages.size()) return; // no such stage
    auto& progress = quests_[id];
    if (progress.state == QuestState::Completed || progress.state == QuestState::Failed) return;
    if (progress.state == QuestState::Inactive) {
        progress = Progress{};
        progress.state = QuestState::Active;
        startedOrder_.push_back(id);
        notices_.push_back({Notice::Kind::QuestStarted, id, Tr("quest/" + id + "/name", quest->name)});
    }
    EnterStage(id, position, library);
}

void StoryState::EnterStage(const std::string& id, size_t position, const StoryLibrary& library) {
    const aige::Quest* quest = library.FindQuest(id);
    auto& progress = quests_[id];
    if (quest == nullptr || progress.state != QuestState::Active) return;
    progress.objective = position;
    progress.count = 0;
    progress.counts.clear();
    progress.done.clear();
    progress.timer = 0.0f;
    if (position >= quest->stages.size()) {
        progress.stage = 0;
        CompleteQuest(id, library);
        return;
    }
    const QuestStage& stage = quest->stages[position];
    progress.stage = stage.index;
    if (!stage.journal.empty()) {
        const std::string text = Tr("quest/" + id + "/stage/" + std::to_string(stage.index), stage.journal);
        journal_.push_back({id, stage.index, text});
        if (quest->staged && position > 0) notices_.push_back({Notice::Kind::StageChanged, id, text});
    }
    Apply(stage.onEnter, library);
    if (progress.state != QuestState::Active || progress.objective != position) return; // onEnter moved it on
    if (stage.ends == "complete") {
        CompleteQuest(id, library);
        return;
    }
    if (stage.ends == "fail") {
        FailQuest(id, library);
        return;
    }
    if (stage.objectives.empty()) {
        Advance(id, library);
        return;
    }
    CheckArrival(library); // "go to the cellar" while already in it
}

void StoryState::Advance(const std::string& id, const StoryLibrary& library) {
    const aige::Quest* quest = library.FindQuest(id);
    auto& progress = quests_[id];
    if (quest == nullptr || progress.state != QuestState::Active || progress.objective >= quest->stages.size()) return;
    const QuestStage& stage = quest->stages[progress.objective];
    int target = 0;
    for (const auto& branch : stage.branches) {
        if (Holds(branch.conditions)) {
            target = branch.stage;
            break;
        }
    }
    if (target == 0) target = stage.next;
    size_t position = progress.objective + 1;
    if (target != 0) {
        for (size_t i = 0; i < quest->stages.size(); ++i) {
            if (quest->stages[i].index == target) position = i;
        }
    }
    EnterStage(id, position, library);
}

void StoryState::MarkDone(const std::string& id, const QuestObjective& objective, const StoryLibrary& library) {
    const aige::Quest* quest = library.FindQuest(id);
    auto& progress = quests_[id];
    if (quest == nullptr || progress.state != QuestState::Active || progress.objective >= quest->stages.size() ||
        !progress.done.insert(objective.id).second) {
        return;
    }
    notices_.push_back({Notice::Kind::ObjectiveDone, id, Tr("quest/" + id + "/" + objective.id, objective.text)});
    const QuestStage& stage = quest->stages[progress.objective];
    bool anyRequired = false, allRequired = true;
    for (const auto& o : stage.objectives) {
        if (o.optional) continue;
        anyRequired = true;
        if (!progress.done.contains(o.id)) allRequired = false;
    }
    // A stage of optional objectives only moves on with the first one done.
    if (allRequired && (anyRequired || !progress.done.empty())) {
        Advance(id, library);
        return;
    }
    progress.count = 0;
    for (const auto& o : stage.objectives) {
        if (!progress.done.contains(o.id)) {
            progress.count = progress.counts[o.id];
            break;
        }
    }
}

void StoryState::CompleteObjective(const std::string& id, const std::string& objective, const StoryLibrary& library) {
    for (const QuestObjective* o : OpenObjectives(id, library)) {
        if (o->id == objective) {
            MarkDone(id, *o, library);
            return;
        }
    }
}

void StoryState::Count(const std::string& type,
                       const std::function<int(const QuestObjective&, const std::string&)>& amount,
                       const StoryLibrary& library, bool absolute) {
    std::vector<std::string> active = startedOrder_; // a stage change may start other quests
    for (const auto& id : active) {
        const aige::Quest* quest = library.FindQuest(id);
        // (A loaded save may point past a quest that was edited since.)
        if (quest == nullptr || quests_[id].state != QuestState::Active || quests_[id].objective >= quest->stages.size()) {
            continue;
        }
        const size_t position = quests_[id].objective;
        for (const auto& objective : quest->stages[position].objectives) {
            auto& progress = quests_[id];
            if (progress.state != QuestState::Active || progress.objective != position) break; // moved on
            if (objective.type != type || progress.done.contains(objective.id)) continue;
            if (!objective.scene.empty() && objective.scene != scene_) continue;
            const int n = amount(objective, quest->Resolve(objective.target));
            if (n <= 0 && !absolute) continue;
            int& counted = progress.counts[objective.id];
            counted = absolute ? n : counted + n;
            if (counted >= objective.count) {
                MarkDone(id, objective, library);
            } else {
                const auto open = OpenObjectives(id, library);
                if (!open.empty() && open.front()->id == objective.id) progress.count = counted;
            }
        }
    }
}

void StoryState::OnReached(const std::string& marker, const StoryLibrary& library) {
    Count("reach",
          [&](const QuestObjective&, const std::string& target) { return !target.empty() && target == marker ? 1 : 0; },
          library);
}

void StoryState::OnDefeated(const std::string& prefab, const std::string& team, const StoryLibrary& library) {
    Count("defeat",
          [&](const QuestObjective&, const std::string& target) {
              return target == prefab || (!team.empty() && target == team) ? 1 : 0;
          },
          library);
}

void StoryState::OnCollected(const std::string& prefab, const StoryLibrary& library) {
    Count("collect", [&](const QuestObjective&, const std::string& target) { return target == prefab ? 1 : 0; }, library);
}

void StoryState::OnTalkedTo(const std::string& character, const StoryLibrary& library) {
    Count("talk", [&](const QuestObjective&, const std::string& target) { return target == character ? 1 : 0; }, library);
}

void StoryState::OnDelivered(const std::string& character, const std::function<bool(const std::string&, int)>& take,
                             const StoryLibrary& library) {
    for (const auto& id : std::vector<std::string>(startedOrder_)) {
        const aige::Quest* quest = library.FindQuest(id);
        if (quest == nullptr) continue;
        for (const QuestObjective* o : OpenObjectives(id, library)) {
            if (o->type == "deliver" && quest->Resolve(o->target) == character && take(quest->Resolve(o->item), o->count)) {
                MarkDone(id, *o, library);
                break; // the stage may have changed
            }
        }
    }
}

void StoryState::OnUsed(const std::string& thing, const StoryLibrary& library) {
    auto match = [&](const QuestObjective&, const std::string& target) { return !target.empty() && target == thing ? 1 : 0; };
    Count("use", match, library);
    Count("examine", match, library);
}

void StoryState::OnHave(const std::function<int(const std::string&)>& count, const StoryLibrary& library) {
    Count("have", [&](const QuestObjective&, const std::string& target) { return count(target); }, library, true);
}

void StoryState::OnEscorted(const std::function<bool(const std::string&, const std::string&)>& arrived,
                            const StoryLibrary& library) {
    Count("escort", [&](const QuestObjective& o, const std::string& target) { return arrived(target, o.to) ? o.count : 0; },
          library);
}

void StoryState::OnLost(const std::string& who, const StoryLibrary& library) {
    for (const auto& id : std::vector<std::string>(startedOrder_)) {
        const aige::Quest* quest = library.FindQuest(id);
        if (quest == nullptr) continue;
        for (const QuestObjective* o : OpenObjectives(id, library)) {
            if ((o->type == "escort" || o->type == "defend") && quest->Resolve(o->target) == who) {
                FailQuest(id, library);
                break;
            }
        }
    }
}

void StoryState::OnTime(float seconds, const StoryLibrary& library) {
    for (const auto& id : std::vector<std::string>(startedOrder_)) {
        auto open = OpenObjectives(id, library);
        bool timed = std::any_of(open.begin(), open.end(),
                                 [](const QuestObjective* o) { return o->type == "survive" || o->type == "defend"; });
        if (!timed) continue;
        const size_t position = quests_[id].objective;
        quests_[id].timer += seconds;
        for (const QuestObjective* o : open) {
            if (quests_[id].state != QuestState::Active || quests_[id].objective != position) break;
            if ((o->type == "survive" || o->type == "defend") && quests_[id].timer >= o->seconds) MarkDone(id, *o, library);
        }
    }
}

void StoryState::SetScene(const std::string& scene, const StoryLibrary& library) {
    scene_ = scene;
    CheckArrival(library);
}

// An open "reach" objective that names this scene and no marker is done on arrival.
void StoryState::CheckArrival(const StoryLibrary& library) {
    if (scene_.empty()) return;
    for (const auto& id : std::vector<std::string>(startedOrder_)) {
        for (const QuestObjective* o : OpenObjectives(id, library)) {
            if (o->type == "reach" && o->target.empty() && o->scene == scene_) {
                MarkDone(id, *o, library); // a stage change checks the next stage in turn
                break;
            }
        }
    }
}

nlohmann::json StoryState::ToJson() const {
    nlohmann::json quests = nlohmann::json::object();
    for (const auto& [id, progress] : quests_) {
        if (progress.state == QuestState::Inactive) continue;
        nlohmann::json entry = {{"state", StateName(progress.state)},
                                {"objective", progress.objective},
                                {"count", progress.count},
                                {"stage", progress.stage}};
        if (!progress.counts.empty()) entry["counts"] = progress.counts;
        if (!progress.done.empty()) entry["done"] = std::vector<std::string>(progress.done.begin(), progress.done.end());
        if (progress.timer > 0.0f) entry["timer"] = progress.timer;
        quests[id] = entry;
    }
    nlohmann::json journal = nlohmann::json::array();
    for (const auto& j : journal_) journal.push_back({{"quest", j.quest}, {"stage", j.stage}, {"text", j.text}});
    nlohmann::json vars = nlohmann::json::object();
    for (const auto& [name, value] : vars_) vars[name] = value;
    return {{"flags", std::vector<std::string>(flags_.begin(), flags_.end())},
            {"vars", vars},
            {"quests", quests},
            {"order", startedOrder_},
            {"journal", journal},
            {"scene", scene_}};
}

StoryState StoryState::FromJson(const nlohmann::json& json) {
    StoryState state;
    if (!json.is_object()) return state;
    for (const auto& flag : json.value("flags", nlohmann::json::array())) {
        if (flag.is_string()) state.flags_.insert(flag.get<std::string>());
    }
    const auto vars = json.value("vars", nlohmann::json::object());
    if (vars.is_object()) {
        for (const auto& [name, value] : vars.items()) state.vars_[name] = value;
    }
    const auto quests = json.value("quests", nlohmann::json::object());
    if (quests.is_object()) {
        for (const auto& [id, entry] : quests.items()) {
            if (!entry.is_object()) continue;
            const std::string name = entry.value("state", "inactive");
            Progress progress;
            progress.state = name == "active"      ? QuestState::Active
                             : name == "completed" ? QuestState::Completed
                             : name == "failed"    ? QuestState::Failed
                                                   : QuestState::Inactive;
            progress.objective = entry.value("objective", static_cast<size_t>(0));
            progress.count = entry.value("count", 0);
            progress.stage = entry.value("stage", 0);
            const auto counts = entry.value("counts", nlohmann::json::object());
            if (counts.is_object()) {
                for (const auto& [objective, n] : counts.items()) {
                    if (n.is_number_integer()) progress.counts[objective] = n.get<int>();
                }
            }
            for (const auto& done : entry.value("done", nlohmann::json::array())) {
                if (done.is_string()) progress.done.insert(done.get<std::string>());
            }
            progress.timer = entry.value("timer", 0.0f);
            state.quests_[id] = progress;
        }
    }
    for (const auto& id : json.value("order", nlohmann::json::array())) {
        if (id.is_string() && state.quests_.contains(id.get<std::string>())) state.startedOrder_.push_back(id.get<std::string>());
    }
    for (const auto& j : json.value("journal", nlohmann::json::array())) {
        if (j.is_object()) state.journal_.push_back({j.value("quest", ""), j.value("stage", 0), j.value("text", "")});
    }
    state.scene_ = json.value("scene", "");
    return state;
}

std::optional<StoryState::Shown> StoryState::CurrentObjective(const StoryLibrary& library) const {
    for (const auto& id : startedOrder_) {
        const aige::Quest* quest = library.FindQuest(id);
        auto open = OpenObjectives(id, library);
        if (quest == nullptr || open.empty()) continue;
        // The first open required objective, else the first open one.
        const QuestObjective* objective = open.front();
        for (const QuestObjective* o : open) {
            if (!o->optional) {
                objective = o;
                break;
            }
        }
        std::string text = Tr("quest/" + id + "/" + objective->id, objective->text); // 12.7
        const auto& progress = quests_.at(id);
        if (objective->count > 1) {
            auto it = progress.counts.find(objective->id);
            const int n = it == progress.counts.end() ? 0 : it->second;
            text += " (" + std::to_string(n) + "/" + std::to_string(objective->count) + ")";
        } else if ((objective->type == "survive" || objective->type == "defend") && objective->seconds > 0.0f) {
            const int left = static_cast<int>(std::max(0.0f, objective->seconds - progress.timer) + 0.99f);
            text += " (" + std::to_string(left) + "s)";
        }
        return Shown{Tr("quest/" + id + "/name", quest->name), text};
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
    lastCheck_.reset();
    if (choice.check.is_object()) {
        lastCheck_ = state.Passes(choice.check);
        if (!*lastCheck_) {
            Enter(choice.fail, state, library); // failing skips the choice's effects
            return;
        }
    }
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
