// The cast timeline (planner ⑥b, development_plan.md 34.7): each character's state before the first quest stage (where
// they are, their AI packages, how they behave, what they talk about) and what each stage changes. The build makes a
// requirement of each state (the Cast tab: characters by stages), stages the moves and behaviours it says through the
// quests (StoryStaging), and checks each one: against the files (the quests' stage effects folded from the character's
// home, their packages, their dialogue) and against what a scene's test recorded as each stage began.
#include "gamebuild/GameBuild.h"

#include "assets/Project.h"
#include "assets/SceneIO.h"
#include "gameplay/Behaviour.h"
#include "gameplay/Components.h"
#include "planner/PlannerDocs.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string Str(const json& j, const char* key) {
    const auto it = j.is_object() ? j.find(key) : j.end();
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

json ReadJson(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    // A file not there (yet: the quests before Quests writes them) or unreadable: an empty object, not null.
    const json j = in ? json::parse(in, nullptr, false) : json::object();
    return j.is_discarded() || j.is_null() ? json::object() : j;
}

std::string ReadText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// The state's parts a change can carry.
const char* const kParts[] = {"place", "spot", "packages", "behaviour", "dialogue"};

std::string PackagesText(const json& packages) {
    std::string out;
    for (const auto& p : packages) {
        const std::string t = p.is_object() ? Str(p, "template") : p.is_string() ? p.get<std::string>() : std::string();
        if (!t.empty()) out += (out.empty() ? "" : ", ") + t;
    }
    return out;
}

// Whether an effect's "who" (a key, a name, a short id) is this character.
bool Names(const std::string& said, const std::string& who, const std::string& name) {
    if (said.empty()) return false;
    const std::string s = Lower(said);
    return s == Lower(who) || s == Lower(name) || Lower(who).rfind(s + "-", 0) == 0;
}

} // namespace

json GameBuild::Timeline() const { return store_.Read("timeline").value_or(json::object()).value("characters", json::array()); }

json GameBuild::TimelineCutscenes() const { return store_.Read("timeline").value_or(json::object()).value("cutscenes", json::array()); }

int GameBuild::TimelineRank(const std::string& quest, const std::string& priority) const {
    const int prio = priority == "story" ? 0 : priority == "ambient" ? 2 : 1;
    int order = 50;
    int k = 0;
    for (const auto& q : store_.Read("timeline").value_or(json::object()).value("quests", json::array())) {
        if (Str(q, "id") == quest) order = Str(q, "id") == "q-main" || Str(q, "type") == "main" ? 0 : 1 + k;
        ++k;
    }
    if (order == 50 && quest == "q-main") order = 0;
    return prio * 100 + order;
}

std::string GameBuild::TimelineTopQuest(const std::string& who, const char* part) const {
    std::string best;
    int bestRank = 1 << 20;
    for (const auto& t : Timeline()) {
        if (Str(t, "who") != who) continue;
        for (const auto& c : t.value("changes", json::array())) {
            if (!c.contains(part)) continue;
            const int r = TimelineRank(Str(c, "quest"), Str(c, "priority"));
            if (r < bestRank) bestRank = r, best = Str(c, "quest");
        }
    }
    return best;
}

json GameBuild::TimelinePackagePlan(const std::string& who) const {
    json out = json::array();
    const json doc = store_.Read("timeline").value_or(json::object());
    for (const auto& t : Timeline()) {
        if (Str(t, "who") != who) continue;
        const json start = t.value("start", json::object());
        // Each quest that changes their packages or place: its stages in order, its changes at each.
        std::map<std::string, std::vector<json>> byQuest;
        for (const auto& c : t.value("changes", json::array())) {
            if (c.contains("packages") || c.contains("place")) byQuest[Str(c, "quest")].push_back(c);
        }
        for (auto& [quest, changes] : byQuest) {
            std::vector<int> order;
            for (const auto& q : doc.value("quests", json::array())) {
                if (Str(q, "id") != quest) continue;
                for (const auto& s : q.value("stages", json::array())) order.push_back(s.value("index", 0));
            }
            std::sort(order.begin(), order.end());
            if (order.empty()) continue;
            // A change on a stage's completion holds from the next stage.
            auto from = [&](const json& c) {
                int at = c.value("stage", 0);
                if (c.value("on", std::string("enter")) == "complete") {
                    const auto next = std::upper_bound(order.begin(), order.end(), at);
                    at = next == order.end() ? 1 << 20 : *next;
                }
                return at;
            };
            std::stable_sort(changes.begin(), changes.end(), [&](const json& a, const json& b) { return from(a) < from(b); });
            json state = start;
            for (size_t i = 0; i < changes.size(); ++i) {
                const json& c = changes[i];
                for (const char* part : kParts) {
                    if (c.contains(part)) state[part] = c[part];
                }
                const int begin = from(c), end = i + 1 < changes.size() ? from(changes[i + 1]) : (1 << 20);
                json stages = json::array();
                for (int s : order) {
                    if (s >= begin && s < end) stages.push_back(s);
                }
                const bool last = i + 1 == changes.size();
                if (stages.empty() && !last) continue;
                json packages = state.value("packages", json::array());
                if (packages.empty()) packages = start.value("packages", json::array());
                for (const auto& p : packages) {
                    const std::string tmpl = p.is_object() ? Str(p, "template") : p.is_string() ? p.get<std::string>() : std::string();
                    if (tmpl.empty()) continue;
                    json plan = {{"template", tmpl}, {"place", p.is_object() && !Str(p, "place").empty() ? Str(p, "place") : Str(state, "place")}};
                    if (p.is_object() && p.contains("schedule")) plan["schedule"] = p["schedule"];
                    out.push_back({{"id", quest + "-" + std::to_string(begin >= (1 << 20) ? order.back() : begin) + "-" + tmpl}, {"plan", plan},
                                   {"quest", quest}, {"stages", stages}, {"last", last}, {"rank", TimelineRank(quest, Str(c, "priority"))},
                                   {"spot", Str(state, "spot")}});
                }
            }
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const json& a, const json& b) { return a.value("rank", 0) < b.value("rank", 0); });
    return out;
}

json GameBuild::TimelineStateAt(const std::string& who, const std::string& quest, int stage) const {
    // Its start, then each change of this quest up to this stage (entering it; one on a stage's completion counts from
    // the next stage on).
    for (const auto& t : Timeline()) {
        if (Str(t, "who") != who) continue;
        json state = t.value("start", json::object());
        std::vector<std::pair<int, json>> changes;
        for (const auto& c : t.value("changes", json::array())) {
            if (Str(c, "quest") != quest) continue;
            const int at = c.value("stage", 0);
            const bool on = c.value("on", std::string("enter")) == "complete" ? at < stage : at <= stage;
            if (on) changes.push_back({at, c});
        }
        std::stable_sort(changes.begin(), changes.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [at, c] : changes) {
            for (const char* part : kParts) {
                if (c.contains(part)) state[part] = c[part];
            }
        }
        return state;
    }
    return json();
}

json GameBuild::TimelineRequirements() const {
    // One requirement for each character's start, and one for each change (only what changes): the Cast tab's cells.
    json out = json::array();
    const json timeline = Timeline();
    if (timeline.empty()) return out;
    const json places = store_.Read("outline").value_or(json::object()).value("places", json::array());
    auto placeName = [&](const std::string& place) {
        for (const auto& p : places) {
            if (Str(p, "id") == place && !Str(p, "name").empty()) return Str(p, "name");
        }
        return PlanSlug(place);
    };
    auto sceneOf = [&](const std::string& place) {
        for (const auto& r : rows_) {
            if (r.place == place) return r.scene;
        }
        return std::string();
    };
    // The first quest stage (the start is checked as the game begins).
    std::string firstQuest = "q-main";
    int firstStage = 0;
    {
        const json q = ReadJson(host_.projectRoot / "quests" / "q-main.json");
        for (const auto& s : q.value("stages", json::array())) {
            if (s.contains("index") && s["index"].is_number() && (firstStage == 0 || s["index"].get<int>() < firstStage)) firstStage = s["index"].get<int>();
        }
    }
    int n = 0;
    for (const auto& t : timeline) {
        const std::string who = Str(t, "who");
        const std::string name = Str(store_.Read("characters/" + PlanSlug(who)).value_or(json::object()), "name").empty()
                                     ? PlanSlug(who)
                                     : Str(store_.Read("characters/" + PlanSlug(who)).value_or(json::object()), "name");
        const std::string topPlace = TimelineTopQuest(who, "place"), topBehaviour = TimelineTopQuest(who, "behaviour");
        auto make = [&](const json& state, const json& when, const std::string& lead, const std::string& why) {
            std::vector<std::string> said;
            json accept = json::array();
            const std::string quest = when.value("quest", firstQuest);
            const int stage = when.value("stage", firstStage);
            // A quest ranked below another that also moves them (or changes how they behave): that one's holds while it's on.
            std::string yields;
            if (!when.value("start", false) && state.contains("place") && !topPlace.empty() && topPlace != quest) yields += "where they are: " + topPlace;
            if (!when.value("start", false) && state.contains("behaviour") && !topBehaviour.empty() && topBehaviour != quest) {
                yields += std::string(yields.empty() ? "" : "; ") + "how they behave: " + topBehaviour;
            }
            if (state.contains("place") && state["place"].is_string() && (when.value("start", false) || topPlace.empty() || topPlace == quest)) {
                const std::string place = state["place"].get<std::string>();
                said.push_back("in " + placeName(place) + (Str(state, "spot").empty() ? "" : " (" + Str(state, "spot") + ")"));
                accept.push_back({{"check", "located"}, {"who", who}, {"place", place}, {"scene", sceneOf(place)}, {"quest", quest}, {"stage", stage}});
            }
            if (state.contains("packages") && state["packages"].is_array() && !state["packages"].empty()) {
                said.push_back("runs " + PackagesText(state["packages"]));
                json templates = json::array();
                for (const auto& p : state["packages"]) {
                    if (!Str(p, "template").empty()) templates.push_back(Str(p, "template"));
                }
                accept.push_back({{"check", "package"}, {"who", who}, {"templates", templates}, {"quest", quest}, {"stage", stage}});
            }
            if (!Str(state, "behaviour").empty() && (when.value("start", false) || topBehaviour.empty() || topBehaviour == quest)) {
                said.push_back(Str(state, "behaviour"));
                accept.push_back({{"check", "behaviour"}, {"who", who}, {"value", Str(state, "behaviour")}, {"quest", quest}, {"stage", stage}});
            }
            if (!Str(state, "dialogue").empty()) {
                said.push_back("talks about " + Str(state, "dialogue"));
                accept.push_back({{"check", "dialogue"}, {"who", who}, {"about", Str(state, "dialogue")}, {"quest", quest}, {"stage", stage}});
            }
            if (accept.empty()) return;
            std::string text = lead + name;
            for (size_t i = 0; i < said.size(); ++i) text += (i == 0 ? " " : i + 1 == said.size() ? " and " : ", ") + said[i];
            if (!yields.empty()) text += " (yields to the higher-ranked quest for " + yields + ")";
            if (!why.empty()) text += " — " + why;
            json r = {{"id", "c" + std::to_string(++n)}, {"scene", ""}, {"act", ""}, {"kind", "character-state"}, {"text", text},
                      {"when", when}, {"where", {{"place", Str(state, "place")}, {"scene", sceneOf(Str(state, "place"))}}},
                      {"who", json::array({who})}, {"by", "rule"}, {"status", "todo"}, {"state", state},
                      {"source", {{"doc", "timeline"}, {"who", who}}}, {"accept", accept}};
            if (!yields.empty()) r["yields"] = yields;
            out.push_back(r);
        };
        make(t.value("start", json::object()), {{"start", true}}, "At the start, ", "");
        for (const auto& c : t.value("changes", json::array())) {
            json state = json::object();
            for (const char* part : kParts) {
                if (c.contains(part)) state[part] = c[part];
            }
            if (c.contains("does")) state["does"] = c["does"]; // shown, not checked
            const std::string on = c.value("on", std::string("enter"));
            make(state, {{"quest", Str(c, "quest")}, {"stage", c.value("stage", 0)}, {"on", on}},
                 (on == "complete" ? "Once " : "Entering ") + Str(c, "quest") + " stage " + std::to_string(c.value("stage", 0)) +
                     (on == "complete" ? " is done, " : ", "),
                 Str(c, "why"));
        }
    }
    // Its cutscenes: each a requirement (under its story scene), made from what the timeline says (its cast and what
    // each does, its beats), set off where and when it says, and checked for its cast.
    for (const auto& c : TimelineCutscenes()) {
        const std::string id = PlanSlug(Str(c, "id"));
        if (id.empty()) continue;
        const json trigger = c.value("trigger", json::object());
        const std::string kind = Str(trigger, "kind"), place = Str(c, "place"), on = c.value("on", std::string("enter"));
        json when = {{"quest", Str(c, "quest")}, {"stage", c.value("stage", 0)}, {"on", on}};
        json where = {{"place", place}, {"scene", sceneOf(place)}};
        const std::string how = CutsceneStartText(c);
        if (kind == "leave") {
            when["leave"] = Str(trigger, "from"), where["from"] = Str(trigger, "from");
        } else if (kind == "reach") {
            where["marker"] = PlanSlug(Str(trigger, "spot"));
        } else if (kind == "talk-end") {
            when["talkEnds"] = Str(trigger, "with");
        } else if (kind == "defeat") {
            when["defeat"] = Str(trigger, "who");
        } else if (kind == "use") {
            when["use"] = Str(trigger, "thing");
        } else if (kind == "event") {
            when["event"] = Str(trigger, "name");
        }
        json who = json::array(), castKeys = json::array();
        std::string castText;
        for (const auto& m : c.value("cast", json::array())) {
            const std::string k = Str(m, "who");
            if (k.empty()) continue;
            castKeys.push_back(k);
            if (k != "player") who.push_back(k);
            castText += (castText.empty() ? "" : ", ") + (k == "player" ? std::string("the player") : PlanSlug(k)) + (Str(m, "does").empty() ? "" : " " + Str(m, "does"));
        }
        out.push_back({{"id", "cs-" + id}, {"scene", Str(c, "scene")}, {"act", ""}, {"kind", "cutscene"}, {"by", "rule"}, {"status", "todo"},
                       {"text", "Cutscene \"" + Str(c, "name") + "\" in " + placeName(place) + ", " + how + ": " + castText},
                       {"when", when}, {"where", where}, {"who", who}, {"cutscene", c}, {"cutsceneId", id},
                       {"source", {{"doc", "timeline"}, {"cutscene", id}}},
                       {"accept", json::array({{{"check", "cutscene"}, {"id", id}},
                                               {{"check", "cutscene-cast"}, {"id", id}, {"cast", castKeys}},
                                               {{"check", "cutscene-plays"}, {"id", id}}})}});
    }
    return out;
}

json GameBuild::TimelineStaging(const json& design) const {
    // What the timeline says each of this quest's stages does to the cast, as stage effects: where they go (their
    // place's scene, at its spot when the scene has it, else where one arrives), and how they behave. A change on a
    // stage's completion is done as the next stage begins. Packages and dialogue aren't effects: their requirements are
    // built by the AI panel (an AI package that starts at the stage, dialogue offered from it).
    json out = json::object();
    const json timeline = Timeline();
    if (timeline.empty()) return out;
    const std::string quest = design.value("id", "");
    std::vector<int> order;
    for (const auto& s : design.value("stages", json::array())) order.push_back(s.value("index", 0));
    std::sort(order.begin(), order.end());
    auto sceneOf = [&](const std::string& place) {
        for (const auto& r : rows_) {
            if (r.place == place) return r.scene;
        }
        return std::string();
    };
    for (const auto& t : timeline) {
        const std::string who = Str(t, "who");
        const std::string topPlace = TimelineTopQuest(who, "place"), topBehaviour = TimelineTopQuest(who, "behaviour");
        std::string was = Str(t.value("start", json::object()), "behaviour");
        std::vector<json> changes;
        for (const auto& c : t.value("changes", json::array())) {
            if (Str(c, "quest") == quest) changes.push_back(c);
        }
        std::stable_sort(changes.begin(), changes.end(), [](const json& a, const json& b) { return a.value("stage", 0) < b.value("stage", 0); });
        for (const auto& c : changes) {
            int at = c.value("stage", 0);
            if (c.value("on", std::string("enter")) == "complete") {
                const auto next = std::upper_bound(order.begin(), order.end(), at);
                if (next == order.end()) continue; // its last stage done: the quest's own completion effects (not staged)
                at = *next;
            }
            json& ops = out[std::to_string(at)];
            if (!ops.is_array()) ops = json::array();
            if (c.contains("place") && c["place"].is_string() && topPlace == quest) {
                const std::string scene = sceneOf(c["place"].get<std::string>());
                if (!scene.empty()) {
                    const json markers = SceneFacts(scene).value("markers", json::array());
                    const std::string spot = PlanSlug(Str(c, "spot"));
                    std::string marker = "arrive";
                    for (const auto& m : markers) {
                        if (m.is_string() && !spot.empty() && Lower(m.get<std::string>()) == Lower(spot)) marker = m.get<std::string>();
                    }
                    // The placements' whereabouts for then (34.11): the marker it decided (made where it has none).
                    const json w = WhereaboutsAt(who, quest, at);
                    if (w.is_object() && !w.value("from", json::object()).value("start", false) && Str(w, "scene") == scene && !Str(w, "marker").empty() &&
                        std::find(markers.begin(), markers.end(), json(Str(w, "marker"))) != markers.end()) {
                        marker = Str(w, "marker");
                    }
                    ops.push_back({{"op", "move_actor"}, {"who", who}, {"scene", scene}, {"marker", marker}});
                }
            }
            if (c.contains("behaviour") && c["behaviour"].is_string() && topBehaviour == quest) {
                const std::string b = c["behaviour"].get<std::string>();
                if (was == "follows-player" && b != "follows-player") ops.push_back({{"op", "stop_following"}, {"who", who}});
                if ((was == "hidden" || was == "absent") && b != "hidden" && b != "absent") ops.push_back({{"op", "show_actor"}, {"who", who}});
                if (b == "hostile") ops.push_back({{"op", "set_hostile"}, {"who", who}});
                else if (b == "friendly" && was == "hostile") ops.push_back({{"op", "set_friendly"}, {"who", who}});
                else if (b == "follows-player") ops.push_back({{"op", "follow_player"}, {"who", who}});
                else if (b == "hidden" || b == "absent") ops.push_back({{"op", "hide_actor"}, {"who", who}});
                was = b;
            }
            if (ops.empty()) out.erase(std::to_string(at));
        }
    }
    return out;
}

json GameBuild::TimelineStatic(const json& a) {
    // A timeline check read from the files: where the quests' stage effects leave them by that stage (from where ⑪
    // placed them), how those effects leave them behaving, whether they have a package of that kind, whether their
    // dialogue talks about it.
    const Project project{host_.projectRoot};
    const std::string check = a.value("check", ""), who = a.value("who", ""), quest = a.value("quest", "");
    const int stage = a.value("stage", 0);
    const std::string name = Str(store_.Read("characters/" + PlanSlug(who)).value_or(json::object()), "name");
    json r = {{"check", check}, {"ok", nullptr}};
    auto fail = [&](const std::string& why) { r["ok"] = false, r["why"] = why; };
    // Where ⑪ placed them, then each effect of the quest's stages up to this one.
    std::string scene;
    for (const auto& act : bindings_.value("actors", json::array())) {
        if (act.value("character", "") == who) scene = act.value("scene", "");
    }
    std::string behaving = "friendly";
    const json q = ReadJson(project.Resolve("quests/" + quest + ".json"));
    std::vector<std::pair<int, json>> stages;
    for (const auto& s : q.value("stages", json::array())) {
        if (s.contains("index") && s["index"].is_number()) stages.push_back({s["index"].get<int>(), s});
    }
    std::sort(stages.begin(), stages.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    for (const auto& [index, s] : stages) {
        if (index > stage) break;
        for (const auto& e : s.value("onEnter", json::array())) {
            if (!Names(Str(e, "who"), who, name)) continue;
            const std::string op = Str(e, "op");
            if (op == "move_actor" && !Str(e, "scene").empty()) scene = Str(e, "scene");
            else if (op == "set_hostile") behaving = "hostile";
            else if (op == "set_friendly" || op == "stop_following" || op == "show_actor") behaving = "friendly";
            else if (op == "follow_player") behaving = "follows-player";
            else if (op == "hide_actor") behaving = "hidden";
        }
    }
    if (check == "located") {
        const std::string want = a.value("scene", "");
        if (want.empty()) fail(a.value("place", "") + " has no built scene");
        else if (scene == want) r["ok"] = true;
        else fail((name.empty() ? who : name) + " is in " + (scene.empty() ? std::string("no scene") : scene) + " by then: nothing brings them to " + want);
    } else if (check == "behaviour") {
        const std::string want = a.value("value", "");
        if (want == behaving || (want == "absent" && behaving == "hidden")) r["ok"] = true;
        else if (want == "dead") r["ok"] = nullptr; // a fight decides: the test tells
        else fail((name.empty() ? who : name) + " is " + behaving + " by then, not " + want + ": no stage effect makes it so");
    } else if (check == "package") {
        // Their packages (on their entity, in the scene they live in): one of each kind asked.
        std::string packages;
        for (const auto& row : rows_) {
            if (row.scene.empty()) continue;
            const std::string text = ReadText(project.Resolve(row.scene));
            const size_t at = text.find("Actor key=" + who + " ");
            if (at == std::string::npos) continue;
            const size_t start = text.rfind("\nentity\n", at);
            const size_t b = text.find("  Behaviour packages=", start == std::string::npos ? 0 : start);
            if (b != std::string::npos && b < at) {
                // packages=one, or packages="one, two" (quoted when there are several)
                const size_t v = b + 21;
                packages = v < text.size() && text[v] == '"' ? text.substr(v + 1, text.find('"', v + 1) - (v + 1)) : text.substr(v, text.find(' ', v) - v);
            }
        }
        std::string missing;
        for (const auto& t : a.value("templates", json::array())) {
            if (t.is_string() && Lower(packages).find(Lower(t.get<std::string>())) == std::string::npos) missing += (missing.empty() ? "" : ", ") + t.get<std::string>();
        }
        if (packages.empty()) fail((name.empty() ? who : name) + " has no packages in any scene");
        else if (missing.empty()) r["ok"] = nullptr, r["has"] = packages; // there: whether it runs from this stage, the test tells
        else fail((name.empty() ? who : name) + " runs " + packages + ": none is a " + missing + " package");
    } else if (check == "dialogue") {
        const std::string text = Lower(ReadText(project.Resolve("dialogue/" + PlanSlug(who) + ".json")));
        int asked = 0, found = 0;
        std::istringstream in(a.value("about", ""));
        for (std::string w; in >> w;) {
            w.erase(std::remove_if(w.begin(), w.end(), [](unsigned char c) { return !std::isalnum(c); }), w.end());
            if (w.size() < 4) continue;
            ++asked;
            found += text.find(Lower(w)) != std::string::npos;
        }
        if (text.empty()) fail("dialogue/" + PlanSlug(who) + ".json isn't written");
        else if (asked == 0 || found * 2 >= asked) r["ok"] = true;
        else fail((name.empty() ? who : name) + "'s dialogue doesn't talk about " + a.value("about", ""));
    }
    return r;
}

void GameBuild::TimelinePlayed(const json& a, const json& record, json& r) {
    // As the stage began in a test (the record's snapshot a moment after): where they were, how, running what.
    const std::string who = a.value("who", ""), quest = a.value("quest", "");
    const int stage = a.value("stage", 0);
    for (const auto& s : record.value("stageStates", json::array())) {
        if (s.value("quest", "") != quest || s.value("stage", -1) != stage) continue;
        const json cast = s.value("cast", json::object());
        if (!cast.contains(who)) return;
        const json me = cast[who];
        auto verdict = [&](bool ok, const std::string& why) {
            r["ok"] = ok;
            r["played"] = true;
            if (ok) r.erase("why");
            else r["why"] = why;
        };
        const std::string check = a.value("check", "");
        if (check == "located") {
            verdict(me.value("scene", "") == a.value("scene", ""), "in the test they were in " + me.value("scene", std::string("no scene")) + " as stage " + std::to_string(stage) + " began");
        } else if (check == "behaviour") {
            const std::string want = a.value("value", "");
            const std::string was = me.value("dead", false) ? "dead" : me.value("hidden", false) ? "hidden" : me.value("team", "") == "Enemy" ? "hostile"
                                    : me.value("follows", false) ? "follows-player" : "friendly";
            verdict(was == want || (want == "absent" && was == "hidden"), "in the test they were " + was);
        } else if (check == "package") {
            const std::string running = Lower(me.value("package", "") + " " + me.value("packages", ""));
            bool all = !running.empty();
            for (const auto& t : a.value("templates", json::array())) all = all && t.is_string() && running.find(Lower(t.get<std::string>())) != std::string::npos;
            if (!me.value("package", "").empty() || !me.value("packages", "").empty()) verdict(all, "in the test they ran " + me.value("package", me.value("packages", "")));
        }
        return;
    }
}

} // namespace aige
