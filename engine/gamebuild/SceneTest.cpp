// A story scene tested on its own (development_plan.md 33.3, 33.4): the game started where the scene begins (each quest
// at the stage it's at then, the earlier stages' effects done, the player at the scene's place), then either played by
// the user (the editor's Play) or by an autopilot, headless: to each objective of the scene's stage (in another scene:
// through a gateway's travel), doing it (talking, using, defeating, reaching), with the events, quest notices and
// script errors logged; then the AI judges the log against the scene's beats, and what didn't happen joins Validate.
#include "gamebuild/GameBuild.h"
#include "gamebuild/SceneAutopilot.h"
#include "gameplay/Settings.h"

#include "assets/Prefab.h"
#include "game/Actors.h"
#include "assets/Project.h"
#include "assets/SceneFiles.h"
#include "assets/SceneIO.h"
#include "gameplay/Behaviour.h"
#include "gameplay/BootstrapLevel.h"
#include "gameplay/Components.h"
#include "gameplay/GameSession.h"
#include "gameplay/InputMap.h"
#include "gameplay/Items.h"
#include "gameplay/Pathfinding.h"
#include "gameplay/Story.h"
#include "gameplay/TileGrid.h"
#include "physics2d/Components.h"
#include "scripting/Components.h"
#include "planner/PlannerDocs.h"
#include "scripting/LuaVM.h"
#include "renderer2d/Components.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <map>
#include <deque>
#include <regex>
#include <set>
#include <sstream>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

json GameBuild::TestStart(const std::string& storyScene) const {
    const json trace = check_.value("trace", json::array()).empty() ? TraceScenes() : check_.value("trace", json::array());
    const json* at = nullptr;
    for (const auto& t : trace) {
        if (t.value("scene", "") == storyScene) at = &t;
    }
    if (!at) return {{"error", "no story scene " + storyScene}};
    const int order = at->value("order", 0);
    // Each quest the story's scenes advance: at the first of its stages whose scene is this one or after it (the stage
    // under way as this scene begins); completed when every one of them is before it.
    json quests = json::array();
    for (const auto& doc : store_.List("quests")) {
        const json q = store_.Read(doc).value_or(json::object());
        std::vector<std::pair<int, int>> stages; // index, its scene's order
        for (const auto& s : q.value("stages", json::array())) {
            const std::string scene = s.value("advancesScene", "");
            if (scene.empty()) continue;
            for (const auto& t : trace) {
                if (t.value("scene", "") == scene) stages.push_back({s.value("index", 0), t.value("order", 0)});
            }
        }
        if (stages.empty()) continue;
        std::sort(stages.begin(), stages.end(), [](auto a, auto b) { return a.second < b.second || (a.second == b.second && a.first < b.first); });
        if (stages.front().second > order) continue; // it begins later
        int stage = 0;
        for (const auto& [index, when] : stages) {
            if (when >= order) {
                stage = index;
                break;
            }
        }
        quests.push_back(stage > 0 ? json{{"id", q.value("id", "")}, {"stage", stage}} : json{{"id", q.value("id", "")}, {"complete", true}});
    }
    json out = {{"storyScene", storyScene}, {"start", StartScene()}, {"scene", at->value("locationScene", "")}, {"marker", "arrive"},
                {"goal", at->value("goal", "")}, {"quests", quests}};
    // Where the stage begins, as a player gets there: where the last of the stage before it is done (talking to Nima in
    // her shop sets stage 20: it begins there, beside her). The quest this scene plays, else the main quest.
    const Project project{host_.projectRoot};
    std::string questId;
    int stage = 0;
    for (const auto& st : at->value("stages", json::array())) {
        if (questId.empty()) questId = st.value("quest", ""), stage = st.value("index", 0);
    }
    for (const auto& q : quests) {
        if (questId.empty() && !q.value("complete", false)) questId = q.value("id", ""), stage = q.value("stage", 0);
    }
    std::ifstream qin(project.Resolve("quests/" + questId + ".json"), std::ios::binary);
    const json q = qin ? json::parse(qin, nullptr, false) : json();
    if (!q.is_object()) return out;
    const json qStages = q.value("stages", json::array()); // kept: prev points into it
    const json* prev = nullptr;
    for (const auto& s : qStages) {
        const int index = s.value("index", 0);
        if (index == stage) out["journal"] = s.value("journal", "");
        if (index < stage && (!prev || index > prev->value("index", 0))) prev = &s;
    }
    if (!prev) return out; // its first stage: the game's start
    // Its last objective that isn't optional, and who or what it's on.
    std::string type, target;
    for (const auto& o : prev->value("objectives", json::array())) {
        if (o.value("optional", false)) continue;
        type = o.value("type", ""), target = o.value("target", "");
    }
    if (target.rfind("@", 0) == 0) {
        for (const auto& a : q.value("aliases", json::array())) {
            if ("@" + a.value("id", "") == target) target = a.value("fill", target);
        }
    }
    if (target.rfind("character/", 0) != 0) return out;
    // Where they are then: the cast timeline's place at that stage, else where ⑪ placed them.
    std::string scene;
    const json state = TimelineStateAt(target, questId, prev->value("index", 0));
    if (state.is_object() && state.contains("place") && state["place"].is_string()) {
        for (const auto& r : rows_) {
            if (r.place == state["place"].get<std::string>()) scene = r.scene;
        }
    }
    for (const auto& act : bindings_.value("actors", json::array())) {
        if (scene.empty() && act.value("character", "") == target) scene = act.value("scene", "");
    }
    if (scene.empty()) return out;
    const json markers = SceneFacts(scene).value("markers", json::array());
    std::string marker = "arrive";
    for (const std::string m : {"story/" + storyScene + "/" + PlanSlug(target), "start-" + PlanSlug(target)}) {
        if (marker == "arrive" && std::find(markers.begin(), markers.end(), m) != markers.end()) marker = m;
    }
    std::string name = PlanSlug(target);
    for (const auto& act : bindings_.value("actors", json::array())) {
        if (act.value("character", "") == target && !act.value("entity", "").empty()) name = act.value("entity", "");
    }
    out["scene"] = scene;
    out["marker"] = marker;
    out["where"] = "beside " + name + " in " + scene + ", as " + questId + " stage " + std::to_string(stage) + " begins (" + type + " " + name + " set it)";
    return out;
}

void ApplyTestStart(GameSession& session, Registry& registry, const StoryLibrary& library, const json& start) {
    StoryState& story = session.Story();
    const auto player = FindPlayer(registry);
    for (const auto& q : start.value("quests", json::array())) {
        const std::string id = q.value("id", "");
        const Quest* quest = library.FindQuest(id);
        if (!quest) continue;
        story.StartQuest(id, library);
        // Each stage up to it entered in turn: their effects (characters moved, items given) happen as in a played game.
        const int until = q.value("complete", false) ? 1 << 30 : q.value("stage", 0);
        for (const auto& s : quest->stages) {
            if (s.index > until) break;
            story.SetStage(id, s.index, library);
            // What its objectives had the player find (have, collect), in the player's pockets: they were done.
            if (s.index >= until || !player) continue;
            for (const auto& o : s.objectives) {
                if (o.type != "have" && o.type != "collect") continue;
                const std::string item = quest->Resolve(o.target);
                if (!item.empty() && item.find('/') == std::string::npos) AddItem(registry.get_or_emplace<Inventory>(*player), item, std::max(1, o.count));
            }
        }
        if (q.value("complete", false)) story.CompleteQuest(id, library);
    }
}

namespace {

std::string ReadText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// Where a quest objective's target is in a scene: an entity, or a marker's cell.
struct Found {
    Entity entity = entt::null;
    float x = 0, y = 0;
};

std::optional<Found> FindTarget(Registry& reg, const std::string& type, const std::string& target) {
    std::optional<Found> out;
    auto take = [&](Entity e, float x, float y) {
        if (!out) out = Found{e, x, y};
    };
    if (type == "talk" || type == "deliver" || type == "escort") {
        for (auto [e, talk, w] : reg.view<Talk, WorldTransform2D>().each()) {
            if (talk.character == target) take(e, w.x, w.y);
        }
        for (auto [e, actor, w] : reg.view<Actor, WorldTransform2D>().each()) {
            if (actor.key == target) take(e, w.x, w.y);
        }
    } else if (type == "defeat") {
        for (auto [e, h, w] : reg.view<Health, WorldTransform2D>(entt::exclude<Dead>).each()) {
            if (h.current <= 0.0f) continue;
            const auto* name = reg.try_get<Name>(e);
            const auto* actor = reg.try_get<Actor>(e);
            const auto* copy = reg.try_get<PrefabInstance>(e);
            const auto* team = reg.try_get<Faction>(e);
            if ((name && name->value == target) || (actor && (actor->key == target || actor->prefab == target)) || (copy && copy->path == target) ||
                (target == "Enemy" && team && team->team == Team::Enemy)) {
                take(e, w.x, w.y);
            }
        }
    }
    if (!out) {
        for (auto [e, m, w] : reg.view<Marker, WorldTransform2D>().each()) {
            if (m.name == target) take(entt::null, w.x, w.y);
        }
    }
    if (!out) {
        for (auto [e, n, w] : reg.view<Name, WorldTransform2D>().each()) {
            if (n.value == target) take(e, w.x, w.y);
        }
    }
    return out;
}

// An open cell beside (x, y), else its own.
std::optional<std::pair<int, int>> NearestOpenTo(const TileGrid& grid, float x, float y) {
    const int tx = static_cast<int>(std::floor(x)), ty = static_cast<int>(std::floor(y));
    for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}, std::pair{0, 0}}) {
        if (!grid.IsSolid(tx + dx, ty + dy)) return std::make_pair(tx + dx, ty + dy);
    }
    return std::nullopt;
}

std::string NameOf(Registry& reg, Entity e) {
    if (!reg.valid(e)) return {};
    if (const auto* n = reg.try_get<Name>(e)) return n->value;
    return {};
}

} // namespace

// --- The autopilot (a frame at a time) -----------------------------------------------------------------------------

SceneAutopilot::SceneAutopilot(json start, std::vector<std::string> scenes, fs::path projectRoot)
    : start_(std::move(start)), scenes_(std::move(scenes)), root_(std::move(projectRoot)) {
    log_ = {{"start", start_}, {"steps", json::array()}, {"events", json::array()}, {"errors", json::array()}, {"dialogue", json::array()},
            {"record", {{"present", json::object()}, {"entered", json::array()}, {"damage", json::array()}, {"stages", json::array()}, {"stageStates", json::array()},
                        {"cutscenes", json::array()}, {"events", json::array()}, {"spawned", json::array()}, {"said", json::array()}, {"near", json::object()}}}};
    for (const auto& q : start_.value("quests", json::array())) {
        Step(q.value("complete", false) ? q.value("id", "") + " completed (before this scene)" : q.value("id", "") + " at stage " + std::to_string(q.value("stage", 0)));
    }
}

void SceneAutopilot::Step(const std::string& text) {
    if (log_["steps"].size() < 400) log_["steps"].push_back(text);
}

void SceneAutopilot::Stop(const std::string& why) {
    if (phase_ == Phase::Done) return;
    Step("stopped: " + why);
    log_["stopped"] = why;
    log_["seconds"] = std::round(now_ * 10) / 10;
    held_.clear();
    pressing_.clear();
    phase_ = Phase::Done;
    doing_ = "Stopped";
}

void SceneAutopilot::Press(const std::string& action, double seconds) {
    const InputAction* a = ActiveInputMap().Find(action);
    if (a && !a->keys.empty()) pressing_.push_back({a->keys.front().key, seconds});
}

// The movement keys that walk the player toward (wx, wy) in the world: on an isometric grid the screen's axes are turned
// to the world's (which way, learned: switched when walking one way gets nowhere).
void SceneAutopilot::HoldMove(World& w, float wx, float wy) {
    (void)w;
    float mx = wx, my = wy;
    if (isoKeys_ && ActiveSettings().grid.style != GridStyle::TopDown) {
        mx = wx - wy;
        my = wx + wy;
    }
    const float most = std::max(std::abs(mx), std::abs(my));
    if (most < 1e-4f) return;
    auto hold = [&](const char* axis, float v) {
        if (std::abs(v) < 0.38f * most) return;
        const InputAction* a = ActiveInputMap().Find(axis);
        if (!a) return;
        for (const auto& k : a->keys) {
            if ((k.scale > 0) == (v > 0)) {
                held_.insert(k.key);
                return;
            }
        }
    };
    hold("move_x", mx);
    hold("move_y", my);
}

void SceneAutopilot::SnapCast(World& w, const std::string& quest, int stage) {
    // Each character: the scene the roster has them in (or this one, when they're here), hidden, their side, alive; here
    // also their packages, the one running, whether they follow the player.
    json cast = json::object();
    if (w.roster) {
        for (const auto& [key, e] : w.roster->Entries()) {
            cast[key] = {{"scene", e.scene}, {"hidden", e.hidden}, {"team", e.team}, {"dead", !e.alive}};
        }
    }
    const std::string scene = w.scene ? w.scene() : std::string();
    for (auto [e, actor] : w.registry.view<Actor>().each()) {
        if (actor.key.empty()) continue;
        json me = cast.contains(actor.key) ? cast[actor.key] : json::object();
        me["scene"] = scene;
        if (const auto* at = w.registry.try_get<WorldTransform2D>(e)) me["x"] = std::round(at->x * 10) / 10, me["y"] = std::round(at->y * 10) / 10; // where (the placements' checks)
        if (const auto* sprite = w.registry.try_get<Sprite>(e)) me["hidden"] = sprite->a <= 0.01f;
        if (const auto* f = w.registry.try_get<Faction>(e)) me["team"] = f->team == Team::Enemy ? "Enemy" : f->team == Team::Player ? "Player" : "Neutral";
        if (const auto* b = w.registry.try_get<Behaviour>(e)) me["packages"] = b->packages;
        if (const auto* st = w.registry.try_get<BehaviourState>(e)) me["package"] = st->package;
        me["follows"] = w.registry.all_of<Follow>(e) && me.value("team", "") != "Enemy";
        if (const auto* h = w.registry.try_get<Health>(e)) me["dead"] = h->current <= 0.0f;
        cast[actor.key] = me;
    }
    log_["record"]["stageStates"].push_back({{"quest", quest}, {"stage", stage}, {"t", std::round(now_ * 10) / 10}, {"cast", cast}});
}

void SceneAutopilot::Record(World& w) {
    json& rec = log_["record"];
    // The cast as the test begins (the stages it starts at), and a moment after each stage begins.
    if (!startSnapped_ && now_ >= 1.0) {
        startSnapped_ = true;
        for (const auto& q : start_.value("quests", json::array())) {
            if (!q.value("complete", false)) SnapCast(w, q.value("id", ""), q.value("stage", 0));
        }
    }
    for (auto it = snapshots_.begin(); it != snapshots_.end();) {
        if (std::get<0>(*it) > now_) {
            ++it;
            continue;
        }
        SnapCast(w, std::get<1>(*it), std::get<2>(*it));
        it = snapshots_.erase(it);
    }
    const std::string scene = w.scene ? w.scene() : std::string();
    if (scene != recScene_) {
        recScene_ = scene;
        rec["entered"].push_back({{"t", std::round(now_ * 10) / 10}, {"scene", scene}});
        recAt_ = -10.0;
    }
    // Who is here (every second): each character by its name.
    const auto player = FindPlayer(w.registry);
    const double since = now_ - nearAt_;
    nearAt_ = now_;
    if (now_ - recAt_ >= 1.0) {
        recAt_ = now_;
        json& here = rec["present"][scene];
        if (!here.is_array()) here = json::array();
        for (auto [e, actor] : w.registry.view<Actor>().each()) {
            if (const auto* sprite = w.registry.try_get<Sprite>(e); sprite && sprite->a <= 0.01f) continue; // hidden
            std::string n = NameOf(w.registry, e);
            if (n.empty()) n = actor.key;
            if (!n.empty() && std::find(here.begin(), here.end(), n) == here.end() && here.size() < 80) here.push_back(n);
        }
    }
    // Who keeps by the player (follows): seconds within three cells.
    if (player && since > 0.0 && since < 1.0) {
        const auto* at = w.registry.try_get<WorldTransform2D>(*player);
        for (auto [e, actor, wt] : w.registry.view<Actor, WorldTransform2D>().each()) {
            if (!at || e == *player) continue;
            if (std::hypot(wt.x - at->x, wt.y - at->y) <= 3.0f) {
                std::string n = NameOf(w.registry, e);
                if (n.empty()) n = actor.key;
                near_[n] += since;
            }
        }
        if (static_cast<int>(now_) != static_cast<int>(now_ - since)) {
            json nearBy = json::object();
            for (const auto& [n, s] : near_) nearBy[n] = std::round(s * 10) / 10;
            rec["near"] = nearBy;
        }
    }
}

void SceneAutopilot::LogEvents(World& w) {
    Record(w);
    json& rec = log_["record"];
    for (const auto& e : w.session.DeliveredEvents()) {
        // The record: who hurt whom (and how much), the stages entered, the cutscenes played, what was spawned and said.
        const std::string who = NameOf(w.registry, e.entity), by = NameOf(w.registry, e.other);
        if (e.name == "hit" && !by.empty()) {
            bool had = false;
            for (auto& d : rec["damage"]) {
                if (d.value("by", "") == by && d.value("target", "") == who) d["amount"] = d.value("amount", 0.0) + e.amount, had = true;
            }
            if (!had && rec["damage"].size() < 60) rec["damage"].push_back({{"by", by}, {"target", who}, {"amount", e.amount}, {"t", std::round(now_ * 10) / 10}});
        } else if (e.name.rfind("quest:", 0) == 0) {
            const size_t colon = e.name.rfind(':');
            if (colon > 6) {
                rec["stages"].push_back({{"quest", e.name.substr(6, colon - 6)}, {"stage", std::atoi(e.name.c_str() + colon + 1)}, {"t", std::round(now_ * 10) / 10}});
                snapshots_.push_back({now_ + 1.0, e.name.substr(6, colon - 6), std::atoi(e.name.c_str() + colon + 1)});
            }
        } else if (e.name == "cutscene_started") {
            rec["cutscenes"].push_back(e.text);
        } else if (e.name == "spawned" && rec["spawned"].size() < 60) {
            rec["spawned"].push_back({{"what", e.text}, {"who", who}, {"scene", recScene_}});
        } else if (e.name == "say" && rec["said"].size() < 80) {
            rec["said"].push_back({{"who", who}, {"text", e.text}});
        }
        json& names = rec["events"];
        if (std::find(names.begin(), names.end(), e.name) == names.end() && names.size() < 120) names.push_back(e.name);
    }
    for (const auto& e : w.session.DeliveredEvents()) {
        if (log_["events"].size() >= 300) break;
        json row = {{"t", std::round(now_ * 10) / 10}, {"event", e.name}};
        if (!e.text.empty()) row["text"] = e.text;
        if (const std::string n = NameOf(w.registry, e.entity); !n.empty()) row["entity"] = n;
        if (const std::string n = NameOf(w.registry, e.other); !n.empty()) row["by"] = n;
        log_["events"].push_back(row);
    }
}

// A conversation on: each line read for a moment (so it can be watched), then a choice not yet tried at that line
// (the last when all have been), so it ends rather than going round.
bool SceneAutopilot::Converse(World& w, double dt) {
    const auto view = w.session.CurrentDialogueView();
    if (!view) {
        talkWait_ = 0.0;
        return false;
    }
    doing_ = "Talking with " + view->speaker;
    // Long enough to read it (as a player would), its choices too.
    talkWait_ += dt;
    size_t letters = view->text.size();
    for (const auto& c : view->choices) letters += c.size() / 2;
    if (talkWait_ < 1.2 + static_cast<double>(letters) / 30.0) return true;
    talkWait_ = 0.0;
    json line = {{"speaker", view->speaker}, {"text", view->text}};
    if (!view->choices.empty()) line["choices"] = view->choices;
    if (log_["dialogue"].size() < 120) log_["dialogue"].push_back(line);
    ++lines_;
    int pick = 0;
    if (!view->choices.empty()) {
        auto& tried = tried_[view->speaker + "|" + view->text];
        pick = static_cast<int>(view->choices.size()) - 1;
        for (int i = 0; i < static_cast<int>(view->choices.size()); ++i) {
            if (!tried.contains(i)) {
                pick = i;
                break;
            }
        }
        tried.insert(pick);
    }
    w.session.ChooseDialogue(pick);
    if (const auto next = w.session.CurrentDialogueView(); next && next->text == view->text && next->speaker == view->speaker) Press("confirm", 0.1);
    return true;
}

bool SceneAutopilot::StartObjective(World& w) {
    while (next_ < objectives_.size()) {
        current_ = objectives_[next_];
        const std::string scene = w.scene();
        auto found = FindTarget(w.registry, current_.type, current_.target);
        if (!found) {
            // Elsewhere: where the quest moved it (the roster), else a scene that names it; there the way one arrives.
            std::string movedTo;
            if (w.roster) {
                if (const auto* entry = w.roster->Find(current_.target)) movedTo = entry->scene;
                for (const auto& [key, entry] : w.roster->Entries()) {
                    if (movedTo.empty() && entry.prefab == current_.target) movedTo = entry.scene;
                }
            }
            std::string to;
            if (!movedTo.empty() && movedTo != scene && !searched_.contains(movedTo)) to = movedTo;
            for (const auto& s : scenes_) {
                if (!to.empty()) break;
                if (s == scene || searched_.contains(s) || ReadText(root_ / s).find(current_.target) == std::string::npos) continue;
                to = s;
            }
            if (!to.empty()) {
                searched_.insert(to);
                seeking_ = to;
                timer_ = 0.0;
                travelling_ = jumped_ = false;
                doing_ = "Going to " + to + " for " + current_.what;
                Step(current_.what + ": not here; walking to " + to);
                phase_ = Phase::Seek;
                return true;
            }
            Step(current_.what + ": not found in any scene");
            searched_.clear();
            ++next_;
            continue;
        }
        searched_.clear();
        targetEntity_ = found->entity;
        targetX_ = found->x;
        targetY_ = found->y;
        // A way there on foot: to it, or to an open cell beside it.
        path_.clear();
        const auto player = FindPlayer(w.registry);
        if (!player) {
            Step("there's no player");
            return false;
        }
        auto& pw = w.registry.get<WorldTransform2D>(*player);
        const TileGrid grid = TileGrid::Build(w.registry);
        const int tx = static_cast<int>(std::floor(found->x)), ty = static_cast<int>(std::floor(found->y));
        // A doorway's cell takes one through it: walked round, unless it's where the target is.
        std::set<std::pair<int, int>> doors;
        for (auto [e, gw, at] : w.registry.view<Gateway, WorldTransform2D>().each()) {
            const std::pair<int, int> c{static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y))};
            if (c != std::make_pair(tx, ty)) doors.insert(c);
        }
        auto blocked = [&](int gx, int gy) { return grid.IsSolid(gx, gy) || doors.contains({gx, gy}); };
        std::optional<std::pair<int, int>> stand;
        std::optional<std::vector<std::pair<int, int>>> cells;
        for (const auto [dx, dy] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1},
                                    std::pair{1, 1}, std::pair{-1, -1}, std::pair{1, -1}, std::pair{-1, 1}}) {
            if (blocked(tx + dx, ty + dy) && !(dx == 0 && dy == 0 && !grid.IsSolid(tx, ty))) continue;
            if (!stand) stand = std::make_pair(tx + dx, ty + dy);
            cells = FindPath(blocked, {static_cast<int>(std::floor(pw.x)), static_cast<int>(std::floor(pw.y))}, {tx + dx, ty + dy});
            if (cells) {
                stand = std::make_pair(tx + dx, ty + dy);
                break;
            }
        }
        const std::string who = found->entity != entt::null ? " (" + NameOf(w.registry, found->entity) + ")" : std::string();
        if (cells) {
            for (size_t i = 1; i < cells->size(); ++i) path_.push_back({(*cells)[i].first + 0.5f, (*cells)[i].second + 0.5f});
            Step(current_.what + ": found" + who + " in " + scene + ", walking there");
        } else if (stand) {
            pw.x = stand->first + 0.5f, pw.y = stand->second + 0.5f;
            Step(current_.what + ": found" + who + " in " + scene + ", but there's no path to it: put beside it");
        } else {
            Step(current_.what + ": found" + who + " in " + scene + ", but nowhere beside it is open");
        }
        doing_ = "Walking to " + current_.what;
        phase_ = Phase::Walk;
        timer_ = stuck_ = 0.0;
        lastDistance_ = 1e9;
        acts_ = 0;
        corners_ = 0;
        walked_ = 0.0;
        return true;
    }
    return false;
}

bool SceneAutopilot::PlanHop(World& w) {
    const std::string here = w.scene();
    // The scenes and their ways through (each scene's gateways and where they lead), read from their files.
    std::map<std::string, std::vector<std::string>> ways;
    static const std::regex gateway(R"(\n  Gateway scene=(\S+))");
    for (const auto& s : scenes_) {
        const std::string text = ReadText(root_ / s);
        for (std::sregex_iterator it(text.begin(), text.end(), gateway), end; it != end; ++it) ways[s].push_back((*it)[1].str());
    }
    // The first step on the shortest way there.
    std::map<std::string, std::string> firstStep;
    std::deque<std::string> open = {here};
    firstStep[here] = "";
    while (!open.empty() && !firstStep.contains(seeking_)) {
        const std::string at = open.front();
        open.pop_front();
        for (const auto& to : ways[at]) {
            if (firstStep.contains(to)) continue;
            firstStep[to] = at == here ? to : firstStep[at];
            open.push_back(to);
        }
    }
    if (!firstStep.contains(seeking_)) return false;
    const std::string hop = firstStep[seeking_];
    const auto player = FindPlayer(w.registry);
    if (!player) return false;
    std::optional<std::pair<float, float>> door;
    for (auto [e, gw, at] : w.registry.view<Gateway, WorldTransform2D>().each()) {
        if (gw.scene == hop && !door) door = std::make_pair(at.x, at.y);
    }
    if (!door) return false;
    auto& pw = w.registry.get<WorldTransform2D>(*player);
    const TileGrid grid = TileGrid::Build(w.registry);
    const std::pair<int, int> doorCell{static_cast<int>(std::floor(door->first)), static_cast<int>(std::floor(door->second))};
    std::set<std::pair<int, int>> others; // the other ways through: walked round
    for (auto [e, gw, at] : w.registry.view<Gateway, WorldTransform2D>().each()) {
        const std::pair<int, int> c{static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y))};
        if (c != doorCell) others.insert(c);
    }
    auto blocked = [&](int gx, int gy) { return (grid.IsSolid(gx, gy) && std::make_pair(gx, gy) != doorCell) || others.contains({gx, gy}); };
    const auto cells = FindPath(blocked, {static_cast<int>(std::floor(pw.x)), static_cast<int>(std::floor(pw.y))}, doorCell);
    path_.clear();
    if (cells) {
        for (size_t i = 1; i < cells->size(); ++i) path_.push_back({(*cells)[i].first + 0.5f, (*cells)[i].second + 0.5f});
    }
    targetEntity_ = entt::null;
    targetX_ = door->first, targetY_ = door->second;
    travelling_ = true;
    hopFrom_ = here;
    doing_ = "Walking to the way to " + hop + (hop == seeking_ ? "" : " (on the way to " + seeking_ + ")");
    Step("walking to the way to " + hop + (cells ? "" : ": no path, stepping toward it"));
    phase_ = Phase::Walk;
    timer_ = stuck_ = 0.0;
    lastDistance_ = 1e9;
    walked_ = 0.0;
    corners_ = 0;
    return true;
}

bool SceneAutopilot::Tick(World& w, double dt) {
    now_ += dt;
    // The keys held for a while (a press), then this frame's own (walking).
    held_.clear();
    for (auto& [key, left] : pressing_) left -= dt;
    std::erase_if(pressing_, [](const auto& p) { return p.second <= 0.0; });
    for (const auto& [key, left] : pressing_) held_.insert(key);
    LogEvents(w);
    if (phase_ == Phase::Done) return false;
    const auto player = FindPlayer(w.registry);
    if (player) {
        if (auto* h = w.registry.try_get<Health>(*player)) h->current = h->max; // tested, not played: it can't die
    }
    if (phase_ != Phase::Arrive && phase_ != Phase::Seek && Converse(w, dt)) return true;
    switch (phase_) {
    case Phase::Arrive: {
        const std::string to = start_.value("scene", "");
        if (!to.empty() && w.scene() != to) {
            if (seeking_.empty()) {
                seeking_ = to;
                timer_ = 0.0;
                doing_ = "Going to " + to;
                w.travel(to, start_.value("marker", "arrive"));
                return true;
            }
            if ((timer_ += dt) < 6.0) return true;
            Step("couldn't get to " + to);
        } else if (!to.empty()) {
            Step("went to " + to + " (" + start_.value("marker", "arrive") + ")");
        }
        seeking_.clear();
        phase_ = Phase::Settle;
        timer_ = 0.0;
        return true;
    }
    case Phase::Settle: {
        doing_ = "Looking round";
        if ((timer_ += dt) < 1.0) return true;
        // The quest whose stage this scene is (its first that isn't completed), and that stage's objectives.
        for (const auto& q : start_.value("quests", json::array())) {
            if (!q.value("complete", false) && questId_.empty()) questId_ = q.value("id", ""), stageAt_ = q.value("stage", 0);
        }
        const Quest* quest = questId_.empty() ? nullptr : w.story.FindQuest(questId_);
        if (quest) {
            for (const auto& s : quest->stages) {
                if (s.index != stageAt_) continue;
                for (const auto& o : s.objectives) {
                    const std::string target = quest->Resolve(o.target);
                    objectives_.push_back({o.type, target, o.type + " " + target});
                }
            }
        } else {
            Step("no quest stage marks this scene: only its start is tested");
        }
        phase_ = Phase::Next;
        return true;
    }
    case Phase::Next:
        if (!StartObjective(w)) {
            phase_ = Phase::Finish;
            timer_ = 0.0;
            doing_ = "Finishing";
        }
        return true;
    case Phase::Seek:
        if (w.scene() == seeking_) {
            Step("arrived in " + seeking_);
            seeking_.clear();
            travelling_ = jumped_ = false;
            phase_ = Phase::Next;
        } else if (jumped_) {
            if ((timer_ += dt) > 6.0) {
                Step(current_.what + ": couldn't get to " + seeking_);
                seeking_.clear();
                jumped_ = false;
                phase_ = Phase::Next;
            }
        } else if (!PlanHop(w)) {
            // No way through the gateways leads there: taken there.
            Step(current_.what + ": no way on foot to " + seeking_ + ", taken there");
            jumped_ = true;
            timer_ = 0.0;
            w.travel(seeking_, "arrive");
        }
        return true;
    case Phase::Walk: {
        if (!player) {
            phase_ = Phase::Finish;
            return true;
        }
        auto& pw = w.registry.get<WorldTransform2D>(*player);
        if (travelling_) {
            // Through: the next scene (on the way, or there).
            if (w.scene() != hopFrom_) {
                travelling_ = false;
                path_.clear();
                Step("went through to " + w.scene());
                phase_ = Phase::Seek;
                return true;
            }
            if (path_.empty()) {
                // At the gateway: onto it (a step in), else stood on it after a while.
                const float dx = targetX_ - pw.x, dy = targetY_ - pw.y, d = std::hypot(dx, dy);
                if (d > 0.05f) HoldMove(w, dx / std::max(d, 0.001f), dy / std::max(d, 0.001f));
                if ((walked_ += dt) > 3.0 && d > 0.05f) pw.x = targetX_, pw.y = targetY_;
                return true;
            }
        }
        // Toward where the target is now (a person walks on), the last stretch.
        if (targetEntity_ != entt::null && w.registry.valid(targetEntity_) && path_.size() <= 1) {
            const auto& tw = w.registry.get<WorldTransform2D>(targetEntity_);
            targetX_ = tw.x, targetY_ = tw.y;
        }
        const float reachAt = current_.type == "reach" ? 0.4f : 0.9f;
        if (!travelling_ && (std::hypot(targetX_ - pw.x, targetY_ - pw.y) <= reachAt || path_.empty())) {
            phase_ = Phase::Act;
            timer_ = 0.0;
            acts_ = 0;
            doing_ = "Doing: " + current_.what;
            return true;
        }
        // Too long on the way (pushed back off a corner): put where it was going.
        if ((walked_ += dt) > 40.0) {
            pw.x = path_.back().first, pw.y = path_.back().second;
            path_.clear();
            Step(current_.what + ": too long on the way, put there");
            return true;
        }
        const auto [gx, gy] = path_.front();
        const float dx = gx - pw.x, dy = gy - pw.y, d = std::hypot(dx, dy);
        if (d < 0.35f) {
            path_.erase(path_.begin());
            lastDistance_ = 1e9;
            stuck_ = 0.0;
            return true;
        }
        if (stuck_ > 0.0) {
            // Caught on a corner: carried along this stretch at a walk (no keys), then on with the keys.
            const float step = std::min(d, static_cast<float>(3.5 * dt));
            pw.x += dx / d * step, pw.y += dy / d * step;
            w.registry.emplace_or_replace<Facing>(*player, dx / d, dy / d);
            return true;
        }
        HoldMove(w, dx / d, dy / d);
        // Getting nowhere (a corner the path cuts): this stretch carried.
        if ((timer_ += dt) >= 0.6) {
            timer_ = 0.0;
            if (lastDistance_ - d < 0.15) {
                stuck_ = 1.0;
                if (!corners_++) Step(current_.what + ": caught on a corner on the way, carried round it");
            }
            lastDistance_ = d;
        }
        return true;
    }
    case Phase::Act: {
        timer_ += dt;
        bool done = false;
        const std::string& type = current_.type;
        if (type == "talk" || type == "deliver" || type == "escort") {
            if (acts_ == 0) {
                // Beside them, where they are now.
                if (player && targetEntity_ != entt::null && w.registry.valid(targetEntity_)) {
                    auto& pw = w.registry.get<WorldTransform2D>(*player);
                    const auto& tw = w.registry.get<WorldTransform2D>(targetEntity_);
                    const float d = std::hypot(pw.x - tw.x, pw.y - tw.y);
                    if (d > 1.2f) {
                        const float k = 0.9f / std::max(d, 0.001f);
                        pw.x = tw.x + (pw.x - tw.x) * k, pw.y = tw.y + (pw.y - tw.y) * k;
                    }
                }
                if (const auto to = w.session.TalkTarget()) {
                    Step(current_.what + ": talking to " + NameOf(w.registry, *to));
                    Press("interact", 0.2);
                    acts_ = 1;
                    timer_ = 0.0;
                } else if (timer_ > 1.5) {
                    Step(current_.what + ": no one in reach to talk to");
                    done = true;
                }
            } else if (timer_ > 0.8) {
                done = true; // its conversation, played through above
            }
        } else if (type == "defeat") {
            if (acts_ == 0) {
                if (std::fmod(timer_, 0.6) < dt) Press("attack", 0.3);
                const bool alive = targetEntity_ != entt::null && w.registry.valid(targetEntity_) && !w.registry.all_of<Dead>(targetEntity_);
                if (!alive || timer_ > 3.0) {
                    if (alive) {
                        if (auto* h = w.registry.try_get<Health>(targetEntity_)) h->current = 0.0f;
                        Step(current_.what + ": the fight ended by the test");
                    } else {
                        Step(current_.what + ": defeated");
                    }
                    acts_ = 1;
                    timer_ = 0.0;
                }
            } else if (timer_ > 1.0) {
                done = true;
            }
        } else if (type == "use") {
            if (acts_ == 0) {
                Press("interact", 0.25);
                acts_ = 1;
            } else if (timer_ > 1.0) {
                Step(current_.what + ": used");
                done = true;
            }
        } else if (timer_ > 1.0) {
            Step(current_.what + ": stood there");
            done = true;
        }
        if (done) {
            ++next_;
            phase_ = Phase::Next;
        }
        return true;
    }
    case Phase::Finish: {
        doing_ = "Finishing";
        if ((timer_ += dt) < 1.0) return true;
        if (!questId_.empty()) {
            const int stage = w.session.Story().StageOf(questId_, w.story);
            log_["quest"] = {{"id", questId_}, {"from", stageAt_}, {"to", stage}};
            log_["advanced"] = stage != stageAt_;
            Step(questId_ + ": stage " + std::to_string(stageAt_) + (stage != stageAt_ ? " → " + (stage == 0 ? std::string("done") : std::to_string(stage)) : " (still)"));
        }
        std::set<std::string> told;
        for (const auto& e : w.session.ScriptErrors()) {
            if (told.insert(e.path + e.message).second) log_["errors"].push_back({{"script", e.path}, {"message", e.message}});
        }
        log_["seconds"] = std::round(now_ * 10) / 10;
        phase_ = Phase::Done;
        doing_ = "Done";
        return false;
    }
    case Phase::Done:
        return false;
    }
    return false;
}

// --- The headless run --------------------------------------------------------------------------------------------

std::unique_ptr<SceneAutopilot> GameBuild::MakeAutopilot(const std::string& storyScene, std::string* why) const {
    const json start = TestStart(storyScene);
    if (start.contains("error")) {
        if (why) *why = start.value("error", "");
        return nullptr;
    }
    std::vector<std::string> scenes;
    for (const auto& r : rows_) {
        if (!r.scene.empty()) scenes.push_back(r.scene);
    }
    return std::make_unique<SceneAutopilot>(start, scenes, host_.projectRoot);
}

json GameBuild::RunSceneTest(const std::string& storyScene) {
    const Project project{host_.projectRoot};
    std::string why;
    auto pilot = MakeAutopilot(storyScene, &why);
    if (!pilot) return {{"error", why}};
    const json start = pilot->Log().value("start", json::object());
    LuaVM lua; // outlives the registry (scripts hold environments in its state)
    Registry reg;
    std::string scene = start.value("start", "");
    if (ReadText(project.Resolve(scene)).empty()) return {{"error", "the start scene " + scene + " can't be read"}};
    LoadScene(reg, project.Resolve(scene).string()); // with its tile layers (the ground one walks on)
    PrefabLibrary prefabs(project);
    prefabs.SyncAll(reg);
    StoryLibrary story(project.root);
    story.Refresh();
    GameSession session(reg, lua, BootstrapIso(), project.root);
    session.SetStory(&story);
    session.SetClockMinutes(-1.0); // the day begins at its start hour, as Play has it (not midnight: everyone asleep)
    session.SetScene(scene);
    session.OnSceneChanged();
    // The roster, as a played game keeps it (ActorRoster, 14.4): a character a quest moved to another scene is there
    // when the player arrives in it, and gone from where it was.
    ActorRoster roster;
    session.SetActorHook([&](const std::string& op, const std::string& key, const json& args) { roster.Apply(op, key, args); });
    const ActorRoster::SpawnFn spawn = [&](const std::string& prefab, float x, float y) { return prefabs.Instantiate(reg, prefab, x, y); };
    roster.Enter(reg, scene, spawn);
    ApplyTestStart(session, reg, story, start);
    roster.Leave(reg, scene);
    roster.Enter(reg, scene, spawn);
    auto travel = [&](const std::string& to, const std::string& marker) {
        const std::string text = ReadText(project.Resolve(to));
        if (text.empty() || to == scene) return;
        roster.Leave(reg, scene);
        TravelToScene(reg, text, marker, project.Resolve(to));
        prefabs.SyncAll(reg);
        scene = to;
        session.SetScene(scene);
        roster.Enter(reg, scene, spawn);
        session.OnSceneChanged();
    };
    // Where the stage begins (TestStart): the test starts there.
    if (const std::string there = start.value("scene", ""); !there.empty() && there != scene) travel(there, start.value("marker", "arrive"));
    std::optional<std::pair<std::string, std::string>> asked; // where the autopilot asked to go, gone to after the frame
    SceneAutopilot::World world{session, reg, story, &roster, [&]() { return scene; },
                                [&](const std::string& to, const std::string& marker) { asked = std::make_pair(to, marker); }};
    const double dt = GameSession::kFixedDt;
    for (double t = 0.0; t < 900.0; t += dt) {
        session.Advance(dt, [&](Key k) { return pilot->Holds(k); });
        if (auto gone = session.TakeTravel()) travel(gone->scene, gone->marker); // a gateway stepped on
        if (asked) {
            travel(asked->first, asked->second);
            asked.reset();
        }
        if (!pilot->Tick(world, dt)) break;
    }
    if (!pilot->Done()) pilot->Stop("it ran out of time (15 minutes of play)");
    return pilot->Log();
}

void GameBuild::SubmitTestLog(const std::string& scene, const json& result) {
    if (!check_.contains("tests") || !check_["tests"].is_object()) check_["tests"] = json::object();
    json& t = check_["tests"][scene];
    t["log"] = result;
    // 34.4: the requirements' played checks against what it recorded.
    if (!reqs_.value("items", json::array()).empty()) {
        CheckRequirements();
        SaveRequirements();
    }
    if (result.contains("error") || !host_.request) {
        t["status"] = "done";
        t["passed"] = false;
        t["summary"] = result.value("error", std::string("played; the AI isn't there to judge it"));
        SaveCheck();
        return;
    }
    // The AI judges the log against the scene.
    const json s = store_.Read("story/scenes/" + scene).value_or(json::object());
    json traced;
    for (const auto& tr : check_.value("trace", json::array())) {
        if (tr.value("scene", "") == scene) traced = tr;
    }
    json quests = json::array();
    std::set<std::string> seen;
    for (const auto& st : traced.is_object() ? traced.value("stages", json::array()) : json::array()) {
        if (!seen.insert(st.value("doc", "")).second) continue;
        const json q = store_.Read(st.value("doc", "")).value_or(json::object());
        quests.push_back({{"doc", st.value("doc", "")}, {"aliases", q.value("aliases", json::array())}, {"stages", q.value("stages", json::array())}});
    }
    const json body = {{"label", "Judge the test of " + scene},
                       {"scene", {{"id", scene}, {"card", s.value("card", json::object())}, {"beats", s.value("beats", json::array())},
                                  {"events", s.value("events", json::array())}}},
                       {"built", traced}, {"quests", quests}, {"log", result}};
    const int id = host_.request("/planner/judgetest", body, 600);
    if (id == 0) {
        t["status"] = "done";
        t["passed"] = false;
        t["summary"] = "played; the AI service isn't running to judge it";
    } else {
        pending_[id] = "test:" + scene;
        t["status"] = "judging";
        status_ = "working"; // its answer is taken in Pump
        message_ = "Testing " + scene + ": the AI judges what happened…";
    }
    SaveCheck();
    Save();
}

std::vector<std::string> GameBuild::ScenesBefore(const std::string& storyScene) const {
    const json trace = check_.value("trace", json::array()).empty() ? TraceScenes() : check_.value("trace", json::array());
    int order = -1;
    for (const auto& t : trace) {
        if (t.value("scene", "") == storyScene) order = t.value("order", 0);
    }
    std::vector<std::pair<int, std::string>> before;
    for (const auto& t : trace) {
        if (t.value("status", "") == "written" && t.value("order", 0) < order) before.push_back({t.value("order", 0), t.value("scene", "")});
    }
    std::sort(before.begin(), before.end());
    std::vector<std::string> out;
    for (const auto& [o, scene] : before) out.push_back(scene);
    return out;
}

std::string GameBuild::WatchScene(const std::string& storyScene) {
    if (stage_ != "done") return "test once the game is built (⑬)";
    if (!check_.contains("tests") || !check_["tests"].is_object()) check_["tests"] = json::object();
    check_["tests"][storyScene] = {{"status", "watching"}};
    SaveCheck();
    return {};
}

std::string GameBuild::TestScene(const std::string& storyScene) {
    if (stage_ != "done") return "test once the game is built (⑬)";
    if (!check_.contains("tests") || !check_["tests"].is_object()) check_["tests"] = json::object();
    if (storyScene == "*") {
        // Every written scene, in turn.
        for (const auto& t : check_.value("trace", json::array()).empty() ? TraceScenes() : check_.value("trace", json::array())) {
            if (t.value("status", "") == "written") check_["tests"][t.value("scene", "")] = {{"status", "queued"}};
        }
    } else {
        check_["tests"][storyScene] = {{"status", "queued"}};
    }
    status_ = "working";
    message_ = "Testing " + storyScene + "…";
    SaveCheck();
    Save();
    return {};
}

bool GameBuild::TestsPending() const {
    const json tests = check_.value("tests", json::object()); // held: value() gives a copy, items() only refers to it
    for (const auto& [scene, t] : tests.items()) {
        const std::string s = t.value("status", "");
        if (s == "queued" || s == "running" || s == "judging") return true;
    }
    return false;
}

bool GameBuild::DriveTests() {
    if (!check_.contains("tests")) return false;
    bool did = false;
    json& tests = check_["tests"];
    for (const auto& [scene, body] : testAnswers_) {
        if (!tests.contains(scene)) continue;
        json& t = tests[scene];
        did = true;
        if (!body.value("ok", false)) {
            t["status"] = "failed";
            t["error"] = body.contains("errors") ? body["errors"].dump() : "no answer";
            continue;
        }
        const json verdict = body.value("verdict", json::object());
        t["status"] = "done";
        t["passed"] = verdict.value("passed", false);
        t["summary"] = verdict.value("summary", "");
        // What didn't happen: Validate's findings (this scene's earlier ones from a test go).
        json& findings = check_["findings"];
        if (!findings.is_array()) findings = json::array();
        const std::string prefix = "test-" + scene + "-";
        findings.erase(std::remove_if(findings.begin(), findings.end(), [&](const json& f) {
                           return f.value("id", "").rfind(prefix, 0) == 0 && f.value("status", "") == "open";
                       }),
                       findings.end());
        int k = 0;
        for (const auto& f : verdict.value("findings", json::array())) {
            if (!f.is_object() || f.value("what", "").empty()) continue;
            json fix = f.value("fix", json::object());
            if (!fix.is_object() || (fix.value("how", "") != "plan" && fix.value("how", "") != "game")) fix = {{"how", "none"}};
            findings.push_back({{"id", prefix + std::to_string(++k)}, {"scene", scene}, {"severity", f.value("severity", "error")},
                                {"kind", f.value("kind", "wrong")}, {"what", f.value("what", "")}, {"detail", f.value("detail", "")},
                                {"fix", fix}, {"source", "test"}, {"status", "open"}});
        }
        Record("test", scene, t.value("passed", false) ? "passed" : "failed", t.value("summary", ""));
    }
    testAnswers_.clear();
    // One run a Pump (each is a game played headless), then its log to the AI.
    for (auto& [scene, t] : tests.items()) {
        if (t.value("status", "") != "queued") continue;
        t["status"] = "running";
        message_ = "Testing " + scene + ": playing it…";
        json result;
        try {
            result = RunSceneTest(scene);
        } catch (const std::exception& e) {
            result = {{"error", std::string("the run stopped: ") + e.what()}};
        }
        did = true;
        SubmitTestLog(scene, result);
        break;
    }
    if (did) {
        if (!TestsPending() && check_.value("status", "") != "checking" && stage_ == "done") {
            status_ = "done";
            message_ = "Tested.";
        }
        SaveCheck();
    }
    return did;
}

} // namespace aige
