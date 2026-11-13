// The built game against its plan (development_plan.md 33): each story scene traced to what was built for it (its quest
// stages and the roles they need, the cast and where each one is, its cutscene, the scripts, packages and dialogue for
// it), checked by rules and by the AI scene by scene, and what's missing or wrong fixed: edits of the plan's documents
// (then ⑪ again: the roles bound and the quests written again) or requests for the AI panel (as ⑫'s gameplay steps).
#include "gamebuild/GameBuild.h"

#include "planner/PlannerDocs.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Its words of three letters or more, lowercase ("A ghost at the gate" -> ghost, the, gate).
std::set<std::string> Words(const std::string& text) {
    std::set<std::string> out;
    std::string w;
    for (const unsigned char c : text + " ") {
        if (std::isalpha(c)) {
            w += static_cast<char>(std::tolower(c));
        } else {
            if (w.size() >= 3) out.insert(w);
            w.clear();
        }
    }
    return out;
}

// Sets `value` at a JSON pointer ("/aliases/3/fill"; "-" appends to an array; missing objects and arrays on the way are
// made). Why not, or "".
std::string SetPointer(json& doc, const std::string& pointer, const json& value) {
    if (pointer.empty() || pointer[0] != '/') return "a pointer starts with /: " + pointer;
    std::vector<std::string> parts;
    std::string part;
    for (size_t i = 1; i <= pointer.size(); ++i) {
        if (i == pointer.size() || pointer[i] == '/') {
            std::string p;
            for (size_t k = 0; k < part.size(); ++k) {
                if (part[k] == '~' && k + 1 < part.size()) p += part[++k] == '1' ? '/' : '~';
                else p += part[k];
            }
            parts.push_back(p);
            part.clear();
        } else {
            part += pointer[i];
        }
    }
    auto index = [](const std::string& p) -> long {
        if (p.empty() || !std::all_of(p.begin(), p.end(), [](unsigned char c) { return std::isdigit(c); })) return -1;
        return std::stol(p);
    };
    json* at = &doc;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string& p = parts[i];
        if (at->is_array()) {
            const long k = index(p);
            if (k < 0 || k >= static_cast<long>(at->size())) return "no item " + p + " in " + pointer;
            at = &(*at)[static_cast<size_t>(k)];
        } else if (at->is_object()) {
            if (!at->contains(p)) (*at)[p] = parts[i + 1] == "-" || index(parts[i + 1]) >= 0 ? json::array() : json::object();
            at = &(*at)[p];
        } else {
            return "nothing to go into at " + p + " in " + pointer;
        }
    }
    const std::string& last = parts.back();
    if (at->is_array()) {
        const long k = index(last);
        if (last == "-" || k == static_cast<long>(at->size())) at->push_back(value);
        else if (k >= 0 && k < static_cast<long>(at->size())) (*at)[static_cast<size_t>(k)] = value;
        else return "no item " + last + " in " + pointer;
    } else if (at->is_object()) {
        (*at)[last] = value;
    } else {
        return "nothing to set at " + pointer;
    }
    return {};
}

// The Lua scripts a prefab or scene file attaches ("scripts/x.lua"), in the order they first appear.
std::vector<std::string> ScriptsIn(const std::string& text) {
    std::vector<std::string> out;
    for (size_t at = text.find("scripts/"); at != std::string::npos; at = text.find("scripts/", at + 8)) {
        if (at > 0 && (std::isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_')) continue;
        size_t end = at + 8;
        while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || std::string("_-./").find(text[end]) != std::string::npos)) ++end;
        const std::string path = text.substr(at, end - at);
        if (path.size() > 12 && path.ends_with(".lua") && std::find(out.begin(), out.end(), path) == out.end()) out.push_back(path);
    }
    return out;
}

// An enemy's script, written by Validate where one is attached but missing: it strikes the player in reach (the enemy
// closes on what it notices by itself: Follow, Perception).
const char* const kEnemyScript = R"(-- Written by Validate: attached to enemies but missing. It strikes the player in reach;
-- the enemy closes on what it notices by itself (Follow, Perception).
function update(dt)
  local player, distance = find_nearest("Player")
  if player and distance and distance <= 1.5 then
    attack_toward(player)
  end
end
)";

// A plan edit's value as the AI meant it: a model's answer shaped after an example whose values are stage effects can
// come back as {"op": "location/x"} for "location/x"; a count as "1".
json CleanValue(const std::string& pointer, json value) {
    if (value.is_object() && value.size() == 1 && value.contains("op") && !value["op"].is_object()) value = json(value["op"]);
    const std::string last = pointer.substr(pointer.rfind('/') + 1);
    static const std::set<std::string> numbers = {"count", "index", "level", "xp", "gold", "stage", "next", "seconds"};
    if (value.is_string() && numbers.contains(last)) {
        const std::string text = value.get<std::string>();
        if (!text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); })) value = std::stoi(text);
    }
    return value;
}

bool Hostile(const json& roles) {
    for (const auto& r : roles) {
        if (r == "enemy" || r == "boss" || r == "elite") return true;
    }
    return false;
}

const char* const kOps =
    "A quest stage's onEnter (what happens as the stage begins), each {op, ...}: "
    "move_actor {who: a character id or the quest's alias, to: a place id (location/...), anchor: a marker there, "
    "\"arrive\" when none}; show_actor / hide_actor {who}; set_hostile {who, hostile: true|false}; "
    "spawn {class: an enemy class, at: a spot alias or anchor, count}; give_to {item}. "
    "A character's alias is bound to that character when the alias has {fill: \"character/<id>\"}.";

} // namespace

json GameBuild::TraceScenes() const {
    const fs::path root = host_.projectRoot;
    auto exists = [&](const std::string& path) { return !path.empty() && fs::exists(root / path); };
    std::map<std::string, std::string> texts; // a file's text, read once
    auto textOf = [&](const std::string& path) -> const std::string& {
        auto [it, fresh] = texts.try_emplace(path);
        if (fresh && !path.empty()) {
            std::ifstream in(root / path, std::ios::binary);
            it->second.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        return it->second;
    };
    auto sceneOfPlace = [&](const std::string& place) -> std::string {
        for (const auto& r : rows_) {
            if (r.place == place || PlanSlug(r.place) == PlanSlug(place)) return r.scene;
        }
        return {};
    };
    std::set<std::string> players;
    for (const auto& c : store_.Read("outline").value_or(json::object()).value("cast", json::array())) {
        if (c.value("player", false)) players.insert(c.value("id", ""));
    }
    std::vector<std::pair<std::string, json>> quests, designs;
    for (const auto& doc : store_.List("quests")) quests.push_back({doc, store_.Read(doc).value_or(json::object())});
    for (const auto& doc : store_.List("systemdesigns")) designs.push_back({doc, store_.Read(doc).value_or(json::object())});
    const json aliasesBound = bindings_.value("aliases", json::array());
    auto bindingOf = [&](const std::string& quest, const std::string& alias) -> json {
        for (const auto& b : aliasesBound) {
            if (b.value("quest", "") == quest && b.value("alias", "") == alias) return b;
        }
        return json();
    };
    json out = json::array();
    for (const auto& doc : store_.List("story/scenes")) {
        const json s = store_.Read(doc).value_or(json::object());
        if (!s.is_object()) continue;
        const json card = s.value("card", json::object());
        const std::string id = s.value("id", doc.substr(doc.rfind('/') + 1)), location = card.value("location", "");
        json t = {{"scene", id}, {"act", s.value("act", "")}, {"chapter", s.value("chapter", "")}, {"order", s.value("order", 0)},
                  {"status", s.value("status", "")}, {"mode", card.value("mode", "")}, {"goal", card.value("goal", "")},
                  {"outcome", card.value("outcome", "")}, {"summary", s.value("summary", "")}, {"location", location},
                  {"locationScene", sceneOfPlace(location)}, {"beats", s.value("beats", json::array())}};
        // Its cast: each one's prefab, home and dialogue.
        json cast = json::array();
        std::vector<std::string> slugs;
        for (const auto& p : card.value("participants", json::array())) {
            const std::string who = p.value("who", ""), slug = PlanSlug(who);
            const json c = store_.Read("characters/" + slug).value_or(json::object());
            const bool player = players.contains(who) || c.value("player", false);
            const std::string prefab = "prefabs/characters/" + slug + ".prefab", dialogue = "dialogue/" + slug + ".json";
            const std::string home = c.value("home", "");
            const json roles = c.value("gameplay", json::object()).value("roles", json::array());
            // There already: a copy of it in the scene's place (its key, or its prefab, in the scene file).
            const std::string& sceneText = textOf(sceneOfPlace(location));
            const bool there = !sceneText.empty() && (sceneText.find("\"" + who + "\"") != std::string::npos ||
                                                      sceneText.find("prefabs/characters/" + slug + ".prefab") != std::string::npos);
            cast.push_back({{"who", who}, {"name", c.value("name", slug)}, {"role", p.value("role", "")}, {"player", player},
                            {"roles", roles}, {"hostile", Hostile(roles)}, {"prefab", exists(prefab) ? prefab : ""},
                            {"home", home}, {"homeScene", sceneOfPlace(home)}, {"inScene", there},
                            {"scripts", exists(prefab) ? json(ScriptsIn(textOf(prefab))) : json::array()},
                            {"dialogue", exists(dialogue) ? dialogue : ""}});
            if (!player) slugs.push_back(slug);
        }
        t["cast"] = cast;
        // The quest stages that advance it: their objectives, each with what its role was bound to; what each does as it begins.
        json stages = json::array();
        for (const auto& [qdoc, q] : quests) {
            const json st = q.value("stages", json::array());
            for (size_t i = 0; i < st.size(); ++i) {
                if (st[i].value("advancesScene", "") != id) continue;
                json objectives = json::array();
                for (const auto& o : st[i].value("objectives", json::array())) {
                    json query;
                    for (const auto& a : q.value("aliases", json::array())) {
                        if (a.value("id", "") == o.value("target", "")) query = a;
                    }
                    objectives.push_back({{"action", o.value("action", "")}, {"target", o.value("target", "")}, {"count", o.value("count", 1)},
                                          {"alias", query}, {"binding", bindingOf(q.value("id", ""), o.value("target", ""))}});
                }
                // The stage as the game's quest file has it (⑫'s steps may have rewritten it): its objectives and effects.
                json game;
                const std::string& questText = textOf("quests/" + q.value("id", "") + ".json");
                const json engine = questText.empty() ? json() : json::parse(questText, nullptr, false);
                if (engine.is_object()) {
                    for (const auto& es : engine.value("stages", json::array())) {
                        if (es.value("index", -1) == st[i].value("index", 0)) {
                            game = {{"journal", es.value("journal", "")}, {"objectives", es.value("objectives", json::array())},
                                    {"onEnter", es.value("onEnter", json::array())}};
                        }
                    }
                }
                stages.push_back({{"doc", qdoc}, {"quest", q.value("id", "")}, {"name", q.value("name", "")}, {"at", i},
                                  {"index", st[i].value("index", 0)}, {"journal", st[i].value("journal", "")},
                                  {"objectives", objectives}, {"onEnter", st[i].value("onEnter", json::array())},
                                  {"game", game}, {"gameFile", engine.is_object() ? "quests/" + q.value("id", "") + ".json" : ""}});
            }
        }
        t["stages"] = stages;
        const std::string cutscene = "cutscenes/" + id + ".json";
        t["cutscene"] = exists(cutscene) ? cutscene : "";
        // Its cast's packages (their days and what quests push), and the gameplay designs that name its cast or place.
        json packages = json::array();
        for (const auto& [path, from] : files_.items()) {
            if (path.rfind("records/package/", 0) != 0) continue;
            for (const auto& slug : slugs) {
                if (path.rfind("records/package/" + slug + "-", 0) == 0) packages.push_back(path);
            }
        }
        t["packages"] = packages;
        json features = json::array(), scripts = json::array();
        for (const auto& [ddoc, d] : designs) {
            const std::string text = d.dump();
            std::vector<std::string> why;
            for (size_t k = 0; k < slugs.size(); ++k) {
                if (text.find("\"" + slugs[k] + "\"") != std::string::npos || text.find("character/" + slugs[k]) != std::string::npos) {
                    why.push_back(slugs[k]);
                }
            }
            if (!location.empty() && text.find(location) != std::string::npos) why.push_back(location);
            if (why.empty()) continue;
            std::string because;
            for (const auto& w : why) because += (because.empty() ? "" : ", ") + w;
            features.push_back({{"id", ddoc.substr(ddoc.rfind('/') + 1)}, {"why", "names " + because}});
            for (const auto& sc : d.value("scripts", json::array())) {
                const std::string path = sc.value("path", "");
                scripts.push_back({{"path", path}, {"feature", ddoc.substr(ddoc.rfind('/') + 1)}, {"purpose", sc.value("purpose", "")},
                                   {"on", sc.value("on", json::object())}, {"written", exists(path)}});
            }
        }
        // The scripts that run for it: its cast's (on their prefabs), its place's (in its scene), besides those its designs ask for.
        auto addScript = [&](const std::string& path, const std::string& on) {
            for (auto& s : scripts) {
                if (s.value("path", "") == path) {
                    if (s.value("attached", "").empty()) s["attached"] = on;
                    return;
                }
            }
            scripts.push_back({{"path", path}, {"attached", on}, {"written", exists(path)}});
        };
        for (const auto& c : cast) {
            for (const auto& path : c.value("scripts", json::array())) addScript(path.get<std::string>(), "on " + c.value("name", ""));
        }
        for (const auto& path : ScriptsIn(textOf(t.value("locationScene", "")))) addScript(path, "in " + t.value("locationScene", ""));
        t["features"] = features;
        t["scripts"] = scripts;
        out.push_back(t);
    }
    std::sort(out.begin(), out.end(), [](const json& a, const json& b) { return a.value("order", 0) < b.value("order", 0); });
    return out;
}

json GameBuild::RuleFindings(const json& trace) const {
    json out = json::array();
    int n = 0;
    auto add = [&](const std::string& scene, const std::string& severity, const std::string& kind, const std::string& what,
                   const std::string& detail, json fix) {
        out.push_back({{"id", "r" + std::to_string(++n)}, {"scene", scene}, {"severity", severity}, {"kind", kind}, {"what", what},
                       {"detail", detail}, {"fix", fix.is_object() ? fix : json{{"how", "none"}}}, {"source", "rules"}, {"status", "open"}});
    };
    std::set<std::string> stuckSeen, missingScripts, reported;
    std::map<std::string, std::set<std::string>> outOfReach; // a scene's markers no one can walk to
    for (const auto& t : trace) {
        const std::string scene = t.value("scene", ""), location = t.value("location", ""), locationScene = t.value("locationScene", "");
        const json stages = t.value("stages", json::array());
        if (t.value("status", "") != "written") continue;
        // Its cast: a prefab each; one who fights in it, there when it happens.
        for (const auto& c : t.value("cast", json::array())) {
            if (c.value("player", false)) continue;
            const std::string who = c.value("who", ""), name = c.value("name", "");
            if (c.value("prefab", "").empty()) {
                add(scene, "error", "missing", name + " isn't in the game",
                    "No prefab (prefabs/characters/" + PlanSlug(who) + ".prefab): " + scene + " has " + name + " as " + c.value("role", "") + ".", {});
                continue;
            }
            if (!c.value("hostile", false) || locationScene.empty() || c.value("homeScene", "") == locationScene || c.value("inScene", false)) continue;
            bool brought = false;
            for (const auto& st : stages) {
                for (const auto& op : st.value("onEnter", json::array())) {
                    if (op.value("op", "") == "move_actor" && (op.value("who", "") == who || Lower(op.value("who", "")) == PlanSlug(who))) brought = true;
                }
            }
            if (brought) continue;
            json fix;
            if (!stages.empty()) {
                const json& st = stages[0];
                fix = {{"how", "plan"},
                       {"edits", json::array({{{"doc", st.value("doc", "")}, {"pointer", "/stages/" + std::to_string(st.value("at", 0)) + "/onEnter/-"},
                                               {"value", {{"op", "move_actor"}, {"who", who}, {"to", location}, {"anchor", "arrive"}}},
                                               {"reason", name + " is brought to " + location + " as " + scene + " begins"}}})}};
            }
            add(scene, "error", "wrong", name + " isn't where " + scene + " happens",
                name + " lives in " + c.value("homeScene", "") + " and nothing brings it to " + locationScene + " for " + scene + " (" +
                    t.value("goal", "") + ")" + (stages.empty() ? "; no quest stage advances the scene, so there's no stage to bring it in" : ""),
                fix);
        }
        // Its quest stages: each role bound, and bound to whom the story names.
        for (const auto& st : stages) {
            for (const auto& o : st.value("objectives", json::array())) {
                const std::string target = o.value("target", "");
                if (target.empty()) continue;
                const json b = o.value("binding", json());
                const std::string where = st.value("quest", "") + " stage " + std::to_string(st.value("index", 0)) + " (" + o.value("action", "") + " " + target + ")";
                if (!b.is_object() || b.value("how", "") == "failed") {
                    add(scene, "error", "missing", target + " isn't in the game", where + ": its role wasn't found" +
                        (b.is_object() ? " (" + b.value("reason", "") + ")" : ""), {});
                    continue;
                }
                // In the game's quest (⑫'s steps may have rewritten it): the objective, by its role or what that's bound to.
                if (const json game = st.value("game", json()); !st.value("gameFile", "").empty()) {
                    // Its names' words (its role, what that's bound to, the character's name) in a game objective's target or
                    // text: "Defeat Odran Vale" is "kill odran-vale".
                    const std::string fill = o.value("alias", json()).value("fill", "");
                    std::string named = target + " " + fill + " " + b.value("entity", "") + " " + b.value("marker", "");
                    if (fill.rfind("character/", 0) == 0) named += " " + store_.Read("characters/" + PlanSlug(fill)).value_or(json::object()).value("name", "");
                    static const std::set<std::string> generic = {"main", "side", "character", "the", "and", "with", "enemy", "arrive", "location"};
                    std::set<std::string> want;
                    for (const auto& w : Words(named)) {
                        if (!generic.contains(w)) want.insert(w);
                    }
                    bool there = false;
                    std::string has;
                    for (const auto& g : game.is_object() ? game.value("objectives", json::array()) : json::array()) {
                        const auto words = Words(g.value("target", "") + " " + g.value("text", ""));
                        there = there || std::any_of(want.begin(), want.end(), [&](const std::string& w) { return words.contains(w); });
                        has += (has.empty() ? "" : "; ") + g.value("text", g.value("type", "") + " " + g.value("target", ""));
                    }
                    if (!there) {
                        add(scene, "warning", "missing", "the game's " + st.value("quest", "") + " stage " + std::to_string(st.value("index", 0)) +
                                                           " has no \"" + o.value("action", "") + " " + target + "\"",
                            "The design's stage has the player " + o.value("action", "") + " " + target + " (" + t.value("goal", "") + "); " +
                                (game.is_object() ? "the game's stage has: " + (has.empty() ? std::string("nothing") : has) : std::string("the game's quest has no such stage")) +
                                ". Fixed by adding it to the game's stage, as its role is bound.",
                            game.is_object() ? json{{"how", "plan"}, {"edits", json::array()},
                                                    {"merge", json::array({{{"doc", st.value("doc", "")}, {"aliases", json::array({target})}}})}}
                                             : json());
                    }
                }
                // Its marker, where one can walk to it from where one arrives in its scene.
                if (const std::string marker = b.value("marker", ""), in = b.value("scene", ""); !marker.empty() && !in.empty()) {
                    auto [it, fresh] = outOfReach.try_emplace(in);
                    if (fresh) it->second = MarkersOutOfReach(in);
                    if (it->second.contains(marker) && reported.insert(in + ">" + marker).second) {
                        const std::string anchor = marker.substr(marker.rfind('/') + 1);
                        add(scene, "error", "wrong", target + " can't be reached",
                            where + ": its marker " + marker + " in " + in + " is where no one can walk to from where one arrives there.",
                            {{"how", "markers"}, {"scene", in}, {"ids", json::array({anchor})}});
                    }
                }
                if (b.value("how", "") != "query" || b.value("entity", "") != "Enemy") continue;
                // Any enemy anywhere: the story names one of its cast, who fights.
                const json alias = o.value("alias", json());
                const auto want = Words(alias.value("query", "") + " " + target);
                std::string pick, pickName;
                int hostiles = 0;
                for (const auto& c : t.value("cast", json::array())) {
                    if (!c.value("hostile", false)) continue;
                    ++hostiles;
                    const json ch = store_.Read("characters/" + PlanSlug(c.value("who", ""))).value_or(json::object());
                    const auto has = Words(c.value("name", "") + " " + ch.value("gameplay", json::object()).value("class", "") + " " +
                                           ch.value("appearance", json::object()).value("description", ""));
                    const bool match = std::any_of(want.begin(), want.end(), [&](const std::string& w) { return has.contains(w); });
                    if (match || pick.empty()) pick = c.value("who", ""), pickName = c.value("name", "");
                }
                if (pick.empty()) continue;
                json fix;
                const json q = store_.Read(st.value("doc", "")).value_or(json::object());
                const json aliases = q.value("aliases", json::array());
                for (size_t k = 0; k < aliases.size(); ++k) {
                    if (aliases[k].value("id", "") != target) continue;
                    fix = {{"how", "plan"},
                           {"edits", json::array({{{"doc", st.value("doc", "")}, {"pointer", "/aliases/" + std::to_string(k) + "/fill"}, {"value", pick},
                                                   {"reason", target + " is " + pickName + ", as " + scene + " has it"}}})}};
                }
                add(scene, "error", "wrong", target + " is any enemy, not " + pickName,
                    where + " counts any enemy anywhere; " + scene + " has " + pickName + " in it" + (hostiles > 1 ? " (of " + std::to_string(hostiles) + " who fight)" : ""),
                    fix);
            }
        }
        // A cutscene scene: its cutscene. A gameplay scene: a quest stage that marks it happening.
        if (t.value("mode", "") == "cutscene" && t.value("cutscene", "").empty()) {
            add(scene, "warning", "missing", scene + "'s cutscene", "It's a cutscene in the story; cutscenes/" + scene + ".json wasn't written.", {});
        }
        if (t.value("mode", "") == "gameplay" && stages.empty()) {
            add(scene, "warning", "missing", "nothing in the game marks " + scene + " happening",
                "It's played (" + t.value("goal", "") + ") but no quest stage advances it.", {});
        }
        // Its scripts, written: one its designs ask for, or one attached (on its cast, in its place) whose file isn't there.
        for (const auto& sc : t.value("scripts", json::array())) {
            const std::string path = sc.value("path", "");
            if (sc.value("written", false) || path.empty() || !missingScripts.insert(path).second) continue;
            const json on = sc.value("on", json::object());
            const std::string attached = sc.value("attached", "");
            if (!sc.value("feature", "").empty()) {
                add(scene, "error", "missing", path + " isn't written", sc.value("feature", "") + " asks for it: " + sc.value("purpose", ""),
                    {{"how", "game"}, {"scene", on.value("place", "").empty() ? std::string() : t.value("locationScene", "")},
                     {"request", "Write the Lua script " + path + " (" + sc.value("purpose", "") + ") and attach it to " +
                                     on.value("target", std::string("the game")) + "."}});
            } else if (const std::string low = Lower(path); low.find("enemy") != std::string::npos || low.find("melee") != std::string::npos ||
                                                            low.find("attack") != std::string::npos || low.find("guard") != std::string::npos) {
                // An enemy's: it strikes the player in reach (it closes on them by itself: Follow).
                add(scene, "error", "missing", path + " is attached but isn't there",
                    "It's attached " + attached + " (and wherever else it's named), but the file doesn't exist: that script does nothing.",
                    {{"how", "file"}, {"path", path},
                     {"content", std::string(kEnemyScript)}});
            } else {
                add(scene, "error", "missing", path + " is attached but isn't there",
                    "It's attached " + attached + " (and wherever else it's named), but the file doesn't exist: that script does nothing.",
                    {{"how", "game"}, {"scene", attached.rfind("in ", 0) == 0 ? attached.substr(3) : std::string()},
                     {"request", "The Lua script " + path + " is attached " + attached + " and to other entities, but the file doesn't exist. Write " + path +
                                     " for what the entities it's on need (read them first: an enemy's script fights the player), or take it off them."}});
            }
        }
        // Quests the playtest found stuck: on the first scene they advance.
        for (const auto& st : stages) {
            const std::string quest = st.value("quest", "");
            if (stuckSeen.contains(quest)) continue;
            for (const auto& pq : playtest_.value("quests", json::array())) {
                if (pq.value("id", "") != quest || pq.value("result", "") == "completed") continue;
                // Stuck at a stage ("stage 20: ..."): on that stage's scene.
                const std::string reason = pq.value("reason", "");
                if (const size_t at = reason.find("stage "); at != std::string::npos) {
                    const int index = std::atoi(reason.c_str() + at + 6);
                    if (index > 0 && st.value("index", 0) != index) continue;
                }
                stuckSeen.insert(quest);
                // Its fix: the marker it can't reach placed again; else a task for the AI panel.
                json fix = {{"how", "game"}, {"scene", t.value("locationScene", "")},
                            {"request", "The quest " + quest + " can't be finished: " + reason + ". Make it completable as the story has it (" +
                                            t.value("goal", "") + "): the target in place, reachable on foot from where the player arrives."}};
                if (const size_t q1 = reason.find('\''), q2 = reason.rfind('\''); q1 != std::string::npos && q2 > q1) {
                    const std::string what = reason.substr(q1 + 1, q2 - q1 - 1);
                    for (const auto& [in, names] : outOfReach) {
                        if (names.contains(what)) fix = {{"how", "markers"}, {"scene", in}, {"ids", json::array({what.substr(what.rfind('/') + 1)})}};
                    }
                }
                add(scene, "error", "wrong", quest + " can't be finished", "The playtest: " + pq.value("result", "") + " — " + reason, fix);
            }
        }
    }
    // The whole game: quests the playtest found stuck that advance no scene (side quests), and its other checks that failed.
    for (const auto& pq : playtest_.value("quests", json::array())) {
        const std::string quest = pq.value("id", "");
        if (pq.value("result", "") == "completed" || stuckSeen.contains(quest)) continue;
        add("", "error", "wrong", quest + " can't be finished", "The playtest: " + pq.value("result", "") + " — " + pq.value("reason", ""),
            {{"how", "game"}, {"scene", ""},
             {"request", "The quest " + quest + " can't be finished: " + pq.value("reason", "") + ". Make it completable: what it needs placed in the "
                         "game (an enemy placed or spawned where the quest says, an item, a marker), reachable on foot from where the player arrives."}});
    }
    for (const auto& c : playtest_.value("checks", json::array())) {
        const std::string what = c.value("check", "");
        if (c.value("ok", true) || what.rfind("quest ", 0) == 0 && what.find("walked to the end") != std::string::npos) continue;
        add("", what.find("key") != std::string::npos ? "warning" : "error", "wrong", what, c.value("detail", ""),
            {{"how", "game"}, {"scene", ""},
             {"request", "The playtest's check \"" + what + "\" fails: " + c.value("detail", "") +
                             ". Fix it in the game's files (the quest or dialogue it names, the keys in input.json), keeping what the story has."}});
    }
    return out;
}

std::string GameBuild::CheckBuild() {
    if (stage_ != "done") return "Validate once the game is built (⑬)";
    if (check_.value("status", "") == "checking") return "it's being validated";
    const json trace = TraceScenes();
    json scenes = json::array();
    for (const auto& t : trace) {
        if (t.value("status", "") == "written") scenes.push_back({{"scene", t.value("scene", "")}, {"status", host_.request ? "queued" : "skipped"}});
    }
    const json tests = check_.value("tests", json::object()); // what the scenes' tests recorded: the requirements' played checks
    check_ = {{"schema", "aige.build-check/1"}, {"status", "checking"}, {"trace", trace}, {"findings", RuleFindings(trace)}, {"scenes", scenes}};
    if (!tests.empty()) check_["tests"] = tests;
    // 34.4: each requirement checked; a failed one is a finding, fixed by building its items again.
    if (!reqs_.value("items", json::array()).empty()) {
        CheckRequirements();
        SaveRequirements();
        for (const auto& r : reqs_["items"]) {
            if (r.value("status", "") != "failed") continue;
            check_["findings"].push_back({{"id", "req-" + r.value("id", "")}, {"scene", r.value("scene", "")}, {"severity", "error"}, {"kind", "missing"},
                                          {"what", r.value("text", "")}, {"detail", r.value("why", "")},
                                          {"fix", {{"how", "requirement"}, {"id", r.value("id", "")}}}, {"source", "requirements"}, {"status", "open"}});
        }
    }
    status_ = "working";
    message_ = "Validating the build: " + std::to_string(scenes.size()) + " scenes against the story…";
    SaveCheck();
    Save();
    return {};
}

bool GameBuild::DriveCheck() {
    if (check_.value("status", "") != "checking") return false;
    bool did = false;
    json& scenes = check_["scenes"];
    json& findings = check_["findings"];
    for (const auto& [index, body] : checkAnswers_) {
        if (index < 0 || index >= static_cast<int>(scenes.size())) continue;
        json& row = scenes[static_cast<size_t>(index)];
        did = true;
        if (!body.value("ok", false)) {
            row["status"] = "failed";
            row["error"] = body.contains("errors") ? body["errors"].dump() : "no answer";
            continue;
        }
        int k = 0;
        for (const auto& f : body.value("findings", json::array())) {
            if (!f.is_object() || f.value("what", "").empty()) continue;
            json fix = f.value("fix", json::object());
            if (!fix.is_object() || (fix.value("how", "") != "plan" && fix.value("how", "") != "game")) fix = {{"how", "none"}};
            findings.push_back({{"id", "ai-" + row.value("scene", "") + "-" + std::to_string(++k)}, {"scene", row.value("scene", "")},
                                {"severity", f.value("severity", "warning")}, {"kind", f.value("kind", "wrong")}, {"what", f.value("what", "")},
                                {"detail", f.value("detail", "")}, {"fix", fix}, {"source", "ai"}, {"status", "open"}});
        }
        row["status"] = "done";
    }
    checkAnswers_.clear();
    int flying = 0;
    for (const auto& r : scenes) flying += r.value("status", "") == "asking";
    for (size_t i = 0; i < scenes.size() && flying < 2; ++i) {
        json& row = scenes[i];
        if (row.value("status", "") != "queued") continue;
        const std::string id = row.value("scene", "");
        json traced;
        for (const auto& t : check_.value("trace", json::array())) {
            if (t.value("scene", "") == id) traced = t;
        }
        const json s = store_.Read("story/scenes/" + id).value_or(json::object());
        json events = json::array();
        for (const auto& e : s.value("events", json::array())) {
            events.push_back({{"id", e.value("id", "")}, {"agent", e.value("agent", "")}, {"class", e.value("class", "")}, {"text", e.value("text", "")}});
        }
        json quests = json::array();
        std::set<std::string> seen;
        for (const auto& st : traced.value("stages", json::array())) {
            if (!seen.insert(st.value("doc", "")).second) continue;
            const json q = store_.Read(st.value("doc", "")).value_or(json::object());
            quests.push_back({{"doc", st.value("doc", "")}, {"aliases", q.value("aliases", json::array())}, {"stages", q.value("stages", json::array())}});
        }
        json places = json::array();
        for (const auto& r : rows_) places.push_back({{"place", r.place}, {"scene", r.scene}, {"buildType", r.buildType}});
        json found = json::array();
        for (const auto& f : findings) {
            if (f.value("scene", "") == id) found.push_back(f.value("what", ""));
        }
        const json body = {{"label", "Validate " + id},
                           {"scene", {{"id", id}, {"card", s.value("card", json::object())}, {"summary", s.value("summary", "")},
                                      {"beats", s.value("beats", json::array())}, {"events", events}, {"end", s.value("end", json::array())},
                                      {"newEntities", s.value("newEntities", json::array())}}},
                           {"built", traced}, {"quests", quests}, {"places", places}, {"alreadyFound", found}, {"stageEffects", kOps}};
        const int request = host_.request ? host_.request("/planner/buildcheck", body, 600) : 0;
        if (request == 0) {
            row["status"] = "failed";
            row["error"] = "the AI service isn't running";
        } else {
            pending_[request] = "check:" + std::to_string(i);
            row["status"] = "asking";
            ++flying;
            message_ = "Validating " + id + " against what was built…";
        }
        did = true;
    }
    const bool left = std::any_of(scenes.begin(), scenes.end(), [](const json& r) {
        return r.value("status", "") == "queued" || r.value("status", "") == "asking";
    });
    if (!left) {
        int errors = 0, warnings = 0;
        for (const auto& f : findings) {
            errors += f.value("severity", "") == "error";
            warnings += f.value("severity", "") != "error";
        }
        check_["status"] = "done";
        message_ = "Validated: " + std::to_string(errors) + " errors, " + std::to_string(warnings) + " warnings";
        if (stage_ == "done" && !TestsPending()) status_ = "done";
        Record("validate", "build", message_);
        did = true;
    }
    if (did) SaveCheck();
    return did;
}

std::string GameBuild::FixFindings(const json& ids) {
    if (stage_ != "done") return "fix once the game is built (⑬)";
    if (!check_.contains("findings")) return "validate the build first";
    std::set<std::string> chosen;
    if (ids.is_array()) {
        for (const auto& i : ids) {
            if (i.is_string()) chosen.insert(i.get<std::string>());
        }
    }
    int plan = 0, game = 0;
    std::map<std::string, QuestMerge> questsEdited;
    // Who the story never moves: the player (they travel themselves; moving them to another scene would remove them),
    // and a vendor out of their shop (they'd sell nowhere after).
    std::set<std::string> players;
    for (const auto& c : store_.Read("outline").value_or(json::object()).value("cast", json::array())) {
        if (c.value("player", false)) players.insert(c.value("id", ""));
    }
    auto refused = [&](const json& value) -> std::string {
        if (!value.is_object() || !value.contains("op") || !value["op"].is_string()) return {};
        const std::string op = value.value("op", "");
        const std::string who = value.contains("who") && value["who"].is_string() ? value["who"].get<std::string>() : std::string();
        if (who.empty() || (op != "move_actor" && op != "hide_actor" && op != "show_actor" && op != "set_hostile")) return {};
        if (players.contains(who)) return op + " on the player (the player travels by themselves)";
        if (op == "move_actor") {
            const json c = store_.Read("characters/" + PlanSlug(who)).value_or(json::object());
            for (const auto& r : c.value("gameplay", json::object()).value("roles", json::array())) {
                // Wherever to: a vendor stands in their shop (its interior may not be their document's home).
                if (r == "vendor" || r == "merchant" || r == "healer") return "moving " + c.value("name", who) + " out of their shop (they'd sell nowhere after)";
            }
        }
        return {};
    };
    for (auto& f : check_["findings"]) {
        if (!chosen.empty() && !chosen.contains(f.value("id", ""))) continue;
        if (f.value("status", "") != "open") continue;
        const json fix = f.value("fix", json::object());
        const std::string how = fix.value("how", "none");
        try {
        if (how == "plan") {
            std::string why;
            json skipped = json::array();
            for (const auto& e : fix.value("edits", json::array())) {
                const std::string doc = e.value("doc", "");
                // The documents the build is made from (not the story itself: changing it would make the plan stale).
                if (doc.rfind("quests/", 0) != 0 && doc.rfind("characters/", 0) != 0 && doc.rfind("behaviours/", 0) != 0 &&
                    doc.rfind("systemdesigns/", 0) != 0) {
                    why = "not a document fixes change: " + doc;
                    break;
                }
                auto content = store_.Read(doc);
                if (!content) {
                    why = "no document " + doc;
                    break;
                }
                const std::string pointer = e.value("pointer", "");
                const json value = CleanValue(pointer, e.value("value", json()));
                if (const std::string no = refused(value); !no.empty()) {
                    skipped.push_back(no);
                    continue;
                }
                // Already so (a fix pressed again): nothing to change.
                bool already = false;
                try {
                    if (pointer.ends_with("/-")) {
                        const json::json_pointer parent(pointer.substr(0, pointer.size() - 2));
                        if (content->contains(parent) && content->at(parent).is_array()) {
                            const json& list = content->at(parent);
                            already = std::find(list.begin(), list.end(), value) != list.end();
                        }
                    } else if (content->contains(json::json_pointer(pointer))) {
                        already = content->at(json::json_pointer(pointer)) == value;
                    }
                } catch (const std::exception&) {
                }
                if (!already) {
                    if (const std::string bad = SetPointer(*content, pointer, value); !bad.empty()) {
                        why = bad;
                        break;
                    }
                    store_.Touch(doc, *content, "validate");
                    Record("validate", doc, pointer + " = " + value.dump(), e.value("reason", f.value("what", "")));
                }
                // A quest's: what the merge into its game file takes (the role an alias edit binds; a stage's effects; the
                // role an objective it adds targets).
                if (doc.rfind("quests/", 0) == 0) {
                    QuestMerge& m = questsEdited[doc];
                    const int k = std::atoi(pointer.c_str() + pointer.find('/', 1) + 1);
                    if (pointer.rfind("/aliases/-", 0) == 0 && value.is_object()) {
                        m.aliases.insert(value.value("id", ""));
                    } else if (pointer.rfind("/aliases/", 0) == 0) {
                        const json aliases = content->value("aliases", json::array());
                        if (k >= 0 && k < static_cast<int>(aliases.size())) m.aliases.insert(aliases[static_cast<size_t>(k)].value("id", ""));
                    } else if (pointer.rfind("/stages/", 0) == 0) {
                        m.stages.insert(k);
                        if (pointer.find("/objectives") != std::string::npos && value.is_object() && value.contains("target") && value["target"].is_string()) {
                            m.aliases.insert(value["target"].get<std::string>());
                        }
                    }
                }
            }
            if (why.empty()) {
                for (const auto& m : fix.value("merge", json::array())) {
                    QuestMerge& qm = questsEdited[m.value("doc", "")];
                    for (const auto& a : m.value("aliases", json::array())) {
                        if (a.is_string()) qm.aliases.insert(a.get<std::string>());
                    }
                }
            }
            f["status"] = why.empty() ? "fixed" : "failed";
            f["error"] = why.empty() && !skipped.empty() ? "not done: " + skipped.dump() : why;
            plan += why.empty();
        } else if (how == "requirement") {
            // 34.4: its implementation items built again (⑫ Implement), told what failed.
            const std::string why = FixRequirements(fix.value("id", ""));
            f["status"] = why.empty() ? "fixed" : "failed";
            f["error"] = why;
            game += why.empty();
        } else if (how == "markers") {
            std::set<std::string> ids;
            for (const auto& i : fix.value("ids", json::array())) ids.insert(i.get<std::string>());
            const std::string why = PlaceAnchorsAgain(fix.value("scene", ""), ids);
            f["status"] = why.empty() ? "fixed" : "failed";
            f["error"] = why;
            plan += why.empty();
        } else if (how == "file") {
            const fs::path path = fs::path(host_.projectRoot) / fix.value("path", "");
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            std::ofstream out(path, std::ios::binary);
            out << fix.value("content", "");
            f["status"] = out ? "fixed" : "failed";
            f["error"] = out ? "" : "can't write " + fix.value("path", "");
            if (out) files_[fix.value("path", "")] = {{"planner", "validate"}};
            plan += static_cast<bool>(out);
        } else if (how == "game") {
            features_.push_back({{"id", "fix-" + f.value("id", "")}, {"name", "Fix: " + f.value("what", "")}, {"place", ""},
                                 {"scene", fix.value("scene", "")},
                                 {"request", fix.value("request", "") + " (Why: " + f.value("detail", "") + ")"},
                                 {"needsScript", false}, {"status", "queued"}, {"error", ""}});
            f["status"] = "fixing";
            ++game;
        }
        } catch (const std::exception& e) {
            // One fix that can't be made doesn't stop the others.
            f["status"] = "failed";
            f["error"] = std::string("couldn't be made: ") + e.what();
        }
    }
    if (plan == 0 && game == 0) {
        SaveCheck();
        return "nothing to fix there (the findings without a fix are for you to decide)";
    }
    // Plan edits: the quests' roles that now name a character bound to it, the quests written again (nothing placed
    // again: what ⑫'s steps did in the scenes and prefabs stays); game fixes: the AI panel, as ⑫'s steps; then the
    // playtest and the result again.
    if (plan > 0) {
        try {
        json& bound = bindings_["aliases"];
        if (!bound.is_array()) bound = json::array();
        for (const auto& doc : store_.List("quests")) {
            const json q = store_.Read(doc).value_or(json::object());
            for (const auto& a : q.value("aliases", json::array())) {
                const std::string fill = a.contains("fill") && a["fill"].is_string() ? a["fill"].get<std::string>() : std::string();
                if (a.value("kind", "") != "character" || fill.empty()) continue;
                json* entry = nullptr;
                for (auto& b : bound) {
                    if (b.value("quest", "") == q.value("id", "") && b.value("alias", "") == a.value("id", "")) entry = &b;
                }
                if (entry && entry->value("how", "") == "fixed" && entry->value("entity", "") == fill) continue;
                json b = {{"quest", q.value("id", "")}, {"alias", a.value("id", "")}, {"how", "failed"}, {"reason", fill + " isn't placed anywhere"}};
                for (const auto& act : bindings_.value("actors", json::array())) {
                    if (act.value("character", "") == fill) b = {{"quest", q.value("id", "")}, {"alias", a.value("id", "")}, {"how", "fixed"}, {"entity", fill}, {"scene", act.value("scene", "")}};
                }
                if (entry) *entry = b;
                else bound.push_back(b);
                Record("bind", q.value("id", "") + "/" + a.value("id", ""), b.value("how", "") + " " + fill, "validate");
            }
        }
            MergeQuests(questsEdited);
        } catch (const std::exception& e) {
            check_["mergeError"] = std::string("the game's quests couldn't take the fixes: ") + e.what();
            Record("validate", "quests", "not merged", e.what());
        }
    }
    stage_ = game > 0 ? "features" : "playtest";
    status_ = "working";
    message_ = "Fixing: " + std::to_string(plan) + " in the plan, " + std::to_string(game) + " in the game…";
    check_["status"] = "fixed";
    SaveCheck();
    Save();
    return {};
}

json GameBuild::CheckView() const {
    if (!check_.is_object() || check_.empty()) return json();
    json view = check_;
    // A game fix: how its step went.
    for (auto& f : view["findings"]) {
        if (f.value("status", "") != "fixing") continue;
        for (const auto& s : features_) {
            if (s.value("id", "") != "fix-" + f.value("id", "")) continue;
            if (s.value("status", "") == "done") f["status"] = "fixed";
            else if (s.value("status", "") == "failed") f["status"] = "failed", f["error"] = s.value("error", "");
        }
    }
    return view;
}

void GameBuild::SaveCheck() const {
    std::ofstream(store_.Dir() / "buildcheck.json", std::ios::binary) << check_.dump(1);
}

void GameBuild::LoadCheck() {
    std::ifstream in(store_.Dir() / "buildcheck.json", std::ios::binary);
    check_ = in ? json::parse(in, nullptr, false) : json::object();
    if (!check_.is_object()) check_ = json::object();
    // Answers lost with the editor: asked again.
    if (check_.contains("scenes")) {
        for (auto& r : check_["scenes"]) {
            if (r.value("status", "") == "asking") r["status"] = "queued";
        }
    }
    if (check_.contains("tests") && check_["tests"].is_object()) {
        for (auto& [scene, t] : check_["tests"].items()) {
            if (t.value("status", "") == "running" || t.value("status", "") == "judging") t["status"] = "queued";
            // Watched in Play when the editor closed: it stopped there.
            if (t.value("status", "") == "watching") t = {{"status", "done"}, {"passed", false}, {"summary", "stopped: the editor closed during the test"}};
        }
    }
    // A trace for the story's scenes to show before any validating (the Story tab).
    if (!check_.contains("trace") && stage_ == "done") check_["trace"] = TraceScenes();
}

} // namespace aige
