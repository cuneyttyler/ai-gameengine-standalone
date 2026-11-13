// Requirements (development_plan.md 34.1, 34.2): what the plan (① to ⑨: the story's scenes, the quests, the cast, the places,
// the systems) says must be true in the game, each a sentence with when, where, who, where it came from and how to tell
// it's done (its accept checks). Made once the places and the cast are built (so they name real scenes and markers):
// first by rule from the plan's structure, then by the AI from each scene's events and beats. planner/requirements.json.
#include "gamebuild/GameBuild.h"

#include "assets/Project.h"
#include "planner/PlannerDocs.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string Str(const json& j, const char* key) {
    const auto it = j.is_object() ? j.find(key) : j.end();
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// The kinds a requirement may be (34.1), and the checks that tell it's done.
const std::set<std::string> kKinds = {"presence", "arrival", "movement", "combat", "dialogue", "cutscene", "item", "prop",
                                      "gateway", "effect", "quest", "guidance", "system", "scene", "asset", "animation"};

// "a gate", "an east gate".
std::string A(const std::string& name) {
    const char c = name.empty() ? 'x' : static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
    return (std::string("aeiou").find(c) != std::string::npos ? "an " : "a ") + name;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

json GameBuild::SceneFacts(const std::string& sceneFile) const {
    // A built scene's markers and its gateways (to which scene), read from its file.
    json markers = json::array(), gateways = json::array();
    const std::string text = ReadAll(Project{host_.projectRoot}.Resolve(sceneFile));
    std::set<std::string> seen;
    static const std::regex marker(R"(\n  Marker name=("[^"]*"|\S+))"), gateway(R"(\n  Gateway scene=(\S+))");
    for (std::sregex_iterator it(text.begin(), text.end(), marker), end; it != end; ++it) {
        std::string name = (*it)[1].str();
        if (name.size() > 1 && name.front() == '"') name = name.substr(1, name.size() - 2);
        if (seen.insert(name).second && markers.size() < 120) markers.push_back(name);
    }
    std::set<std::string> to;
    for (std::sregex_iterator it(text.begin(), text.end(), gateway), end; it != end; ++it) {
        if (to.insert((*it)[1].str()).second) gateways.push_back((*it)[1].str());
    }
    return {{"markers", markers}, {"gateways", gateways}};
}

json GameBuild::RuleRequirements() const {
    const json trace = check_.value("trace", json::array()).empty() ? TraceScenes() : check_.value("trace", json::array());
    json items = json::array();
    int n = 0;
    // A place's name as the outline has it ("Bellwether"), else its document's, else its id's words.
    const json outlinePlaces = store_.Read("outline").value_or(json::object()).value("places", json::array());
    auto placeName = [&](const std::string& place) {
        for (const auto& p : outlinePlaces) {
            if (Str(p, "id") == place && !Str(p, "name").empty()) return Str(p, "name");
        }
        const std::string name = Str(store_.Read("places/" + PlanSlug(place)).value_or(json::object()), "name");
        if (!name.empty()) return name;
        std::string words = PlanSlug(place);
        std::replace(words.begin(), words.end(), '-', ' ');
        return words;
    };
    // A quest role as the player knows it: the character's name, else what it describes, else its id's words.
    auto roleName = [&](const json& alias, const std::string& id) {
        const std::string fill = alias.is_object() ? Str(alias, "fill") : std::string();
        if (fill.rfind("character/", 0) == 0) {
            const std::string name = Str(store_.Read("characters/" + PlanSlug(fill)).value_or(json::object()), "name");
            if (!name.empty()) return name;
        }
        if (fill.rfind("location/", 0) == 0) return placeName(fill);
        if (alias.is_object() && !Str(alias, "query").empty()) return Str(alias, "query");
        std::string words = id;
        std::replace(words.begin(), words.end(), '-', ' ');
        return words;
    };
    auto add = [&](json r) {
        r["id"] = "r" + std::to_string(++n);
        r["by"] = "rule";
        r["status"] = "todo";
        items.push_back(std::move(r));
    };
    // Visual requirements: each separate piece of art, once (a part, a gateway, an item, a character), with every scene
    // and place it's in; its look from the plan where it has one (a character's appearance), else written by the AI.
    std::map<std::string, size_t> assetAt;
    std::set<std::string> animated; // the characters whose animations are required already
    const json outlineKinds = outlinePlaces;
    auto placeKind = [&](const std::string& place) {
        for (const auto& p : outlineKinds) {
            if (Str(p, "id") == place) return Str(p, "kind");
        }
        return std::string();
    };
    auto addAsset = [&](const std::string& key, const std::string& scene, const std::string& act, const std::string& place,
                        const std::string& name, const std::string& type, const std::string& look, const std::string& text) {
        if (const auto it = assetAt.find(key); it != assetAt.end()) {
            json& r = items[it->second];
            if (std::find(r["scenes"].begin(), r["scenes"].end(), scene) == r["scenes"].end()) r["scenes"].push_back(scene);
            json& places = r["asset"]["places"];
            if (!place.empty() && std::find(places.begin(), places.end(), place) == places.end()) places.push_back(place);
            return;
        }
        assetAt[key] = items.size();
        add({{"scene", scene}, {"act", act}, {"kind", "asset"}, {"text", text}, {"scenes", json::array({scene})},
             {"when", json::object()}, {"where", {{"place", place}}}, {"who", json::array()}, {"source", {{"scene", scene}, {"field", type}}},
             {"asset", {{"id", key}, {"name", name}, {"type", type}, {"look", look}, {"separate", true},
                        {"places", place.empty() ? json::array() : json::array({place})}}},
             {"accept", json::array({{{"check", "asset"}, {"name", name}}})}});
    };
    // In the story's order, each written scene.
    std::vector<const json*> scenes;
    for (const auto& t : trace) {
        if (t.value("status", "") == "written") scenes.push_back(&t);
    }
    std::stable_sort(scenes.begin(), scenes.end(), [](const json* a, const json* b) { return a->value("order", 0) < b->value("order", 0); });
    std::string lastLocation;
    const json plannedCutscenes = TimelineCutscenes(); // the timeline's (⑥b): a scene it plans a cutscene for gets that one
    for (const json* tp : scenes) {
        const json& t = *tp;
        const std::string scene = t.value("scene", ""), act = t.value("act", ""), location = t.value("location", ""),
                          sceneFile = t.value("locationScene", ""), where = placeName(location);
        const json stages = t.value("stages", json::array());
        // When it happens: its first quest stage, else just the scene.
        json when = {{"scene", scene}};
        if (!stages.empty()) when = {{"quest", stages[0].value("quest", "")}, {"stage", stages[0].value("index", 0)}};
        const json placeAt = {{"place", location}, {"scene", sceneFile}};
        const json card = store_.Read("story/scenes/" + scene).value_or(json::object()).value("card", json::object());
        // Its cast, there.
        for (const auto& c : t.value("cast", json::array())) {
            const std::string who = c.value("who", "");
            const json appearance = store_.Read("characters/" + PlanSlug(who)).value_or(json::object()).value("appearance", json::object());
            const std::string look = appearance.is_object() ? Str(appearance, "description") : std::string();
            addAsset(who, scene, act, "", c.value("name", ""), "character", look,
                     c.value("name", "") + " is drawn" + (look.empty() ? "" : ": " + look));
            // Its animations: what ⑪ animates it doing (walking; a fighter's attack, getting hurt, falling dead), once each.
            if (animated.insert(who).second) {
                // From the plan alone (its class, whether it fights): the Characters step draws what these ask.
                json actions = PlanActions(who);
                if (actions.empty()) {
                    actions.push_back({{"action", "walking"}});
                    if (c.value("hostile", false) || c.value("player", false)) {
                        for (const char* x : {"attacking", "getting hurt", "falling down dead"}) actions.push_back({{"action", x}});
                    }
                }
                for (const auto& x : actions) {
                    const std::string action = x.value("action", "");
                    if (action.empty()) continue;
                    add({{"scene", scene}, {"act", act}, {"kind", "animation"}, {"text", c.value("name", "") + " is animated " + action},
                         {"when", json::object()}, {"where", json::object()}, {"who", json::array({who})}, {"source", {{"scene", scene}, {"field", "participants"}}},
                         {"animation", {{"who", who}, {"name", c.value("name", "")}, {"action", action}}},
                         {"accept", json::array({{{"check", "animation"}, {"who", who}, {"action", action}}})}});
                }
            }
            if (c.value("player", false)) continue;
            // Where they're met: the story's place, or (the places say so) their own building in it: a merchant in their shop.
            std::string met = MetAt(who, location);
            if (!stages.empty()) {
                // The cast timeline (⑥b) says where they are as the stage begins.
                const json state = TimelineStateAt(who, stages[0].value("quest", ""), stages[0].value("index", 0));
                if (state.is_object() && !Str(state, "place").empty()) met = Str(state, "place");
            }
            std::string metScene = sceneFile;
            if (met != location) {
                for (const auto& r : rows_) {
                    if (r.place == met) metScene = r.scene;
                }
            }
            const std::string metText = met == location ? where : placeName(met) + " in " + where;
            add({{"scene", scene}, {"act", act}, {"kind", "presence"},
                 {"text", c.value("name", "") + " is in " + metText + (c.value("hostile", false) ? " to fight" : "") + " for " + scene + " (" + t.value("goal", "") + ")" +
                              (met == location ? "" : ": where they work, the player goes to them")},
                 {"when", when}, {"where", {{"place", met}, {"scene", metScene}}}, {"who", json::array({c.value("who", "")})},
                 {"source", {{"scene", scene}, {"field", "participants"}, {"place", met}}},
                 {"accept", json::array({{{"check", "present"}, {"who", c.value("who", "")}, {"scene", metScene}, {"when", when}}})}});
        }
        // The parts it needs.
        for (const auto& p : card.is_object() ? card.value("needsParts", json::array()) : json::array()) {
            if (!p.is_string()) continue;
            addAsset("part:" + Lower(p.get<std::string>()) + "@" + location, scene, act, location, p.get<std::string>(), "prop", "",
                     "The " + p.get<std::string>() + " of " + where + " looks as the story has it");
            add({{"scene", scene}, {"act", act}, {"kind", "prop"}, {"text", "There is " + A(p.get<std::string>()) + " in " + where + " (" + scene + " needs it)"},
                 {"when", when}, {"where", placeAt}, {"who", json::array()}, {"source", {{"scene", scene}, {"field", "needsParts"}}},
                 {"accept", json::array({{{"check", "prop"}, {"name", p.get<std::string>()}, {"scene", sceneFile}}})}});
        }
        // Its quest stages: each objective there in the game, the stage moving on once they're done, and the way there shown.
        for (const auto& st : stages) {
            const std::string quest = st.value("quest", "");
            const int index = st.value("index", 0);
            const json stageWhen = {{"quest", quest}, {"stage", index}};
            std::string asks;
            bool goes = false;
            for (const auto& o : st.value("objectives", json::array())) {
                const json alias = o.value("alias", json());
                const std::string target = roleName(alias, o.value("target", ""));
                const std::string action = o.value("action", "");
                asks += (asks.empty() ? "" : ", ") + action + " " + target;
                goes = goes || action == "reach" || action == "goto" || action == "examine";
                if (action == "collect" || action == "deliver" || action == "use" || action == "take" || action == "give" || action == "have") {
                    addAsset("item:" + Lower(target), scene, act, location, target, "item", "", "The " + target + " (an item the player " + action + "s) is drawn");
                }
                add({{"scene", scene}, {"act", act}, {"kind", "quest"}, {"text", quest + " stage " + std::to_string(index) + ": the player can " + action + " " + target},
                     {"when", stageWhen}, {"where", placeAt}, {"who", json::array()}, {"source", {{"quest", st.value("doc", "")}, {"stage", index}}},
                     {"accept", json::array({{{"check", "objective"}, {"quest", quest}, {"stage", index}, {"action", action}, {"target", o.value("target", "")}}})}});
            }
            add({{"scene", scene}, {"act", act}, {"kind", "quest"}, {"text", quest + " moves on from stage " + std::to_string(index) + " once the player has done it (" + asks + ")"},
                 {"when", stageWhen}, {"where", placeAt}, {"who", json::array()}, {"source", {{"quest", st.value("doc", "")}, {"stage", index}}},
                 {"accept", json::array({{{"check", "stage-advances"}, {"quest", quest}, {"from", index}}})}});
            if (goes || (!lastLocation.empty() && lastLocation != location)) {
                add({{"scene", scene}, {"act", act}, {"kind", "guidance"},
                     {"text", "In " + quest + " stage " + std::to_string(index) + " the player can tell where to go: " + where + ", its way there seen and pointed to"},
                     {"when", stageWhen}, {"where", placeAt}, {"who", json::array()}, {"source", {{"quest", st.value("doc", "")}, {"stage", index}}},
                     {"accept", json::array({{{"check", "journal-names"}, {"quest", quest}, {"stage", index}, {"place", where}},
                                             {{"check", "gateway-visible"}, {"to", sceneFile}},
                                             {{"check", "objective-marker"}, {"quest", quest}, {"stage", index}}})}});
            }
        }
        // A scene no stage plays: a cutscene (it's watched), or a conversation something has to start.
        const std::string mode = t.value("mode", "");
        const bool planned = std::any_of(plannedCutscenes.begin(), plannedCutscenes.end(), [&](const json& c) { return Str(c, "scene") == scene; });
        if (mode == "cutscene" && !planned) {
            add({{"scene", scene}, {"act", act}, {"kind", "cutscene"}, {"text", scene + " is watched as a cutscene in " + where + ": " + t.value("goal", "")},
                 {"when", when}, {"where", placeAt}, {"who", json::array()}, {"source", {{"scene", scene}, {"field", "mode"}}},
                 {"accept", json::array({{{"check", "cutscene"}, {"id", scene}}, {{"check", "cutscene-plays"}, {"id", scene}}})}});
        } else if (stages.empty() && !planned) {
            add({{"scene", scene}, {"act", act}, {"kind", "scene"},
                 {"text", "Something in the game plays " + scene + " (" + mode + ", in " + where + "): no quest stage does — " + t.value("goal", "")},
                 {"when", when}, {"where", placeAt}, {"who", json::array()}, {"source", {{"scene", scene}, {"field", "mode"}}},
                 {"accept", json::array({{{"check", "scene-played"}, {"scene", scene}}})}});
        }
        // The way from the last place into this one, when both are out of doors: a gateway that can be seen (a gate in a
        // wall, a path between posts), not a bare tile.
        if (!lastLocation.empty() && lastLocation != location && placeKind(location) != "interior" && placeKind(lastLocation) != "interior") {
            const std::string from = placeName(lastLocation);
            const std::string key = "gateway:" + lastLocation + ">" + location;
            const bool fresh = !assetAt.contains(key);
            addAsset(key, scene, act, lastLocation, "the way into " + where + " from " + from, "gateway", "",
                     "The way from " + from + " into " + where + " can be seen in " + from + ": a gateway drawn and placed where it leads");
            if (fresh) {
                items.back()["asset"]["to"] = location;
                items.back()["accept"].push_back({{"check", "gateway-visible"}, {"to", sceneFile}});
            }
        }
        lastLocation = location;
    }
    // The systems (⑦): each step of each feature.
    for (const auto& f : store_.Read("gameplay").value_or(json::object()).value("features", json::array())) {
        const json design = store_.Read("systemdesigns/" + PlanSlug(f.value("id", ""))).value_or(json::object());
        int i = 0;
        for (const auto& step : design.value("steps", json::array())) {
            std::string text = step.value("request", "");
            if (text.size() > 240) text = text.substr(0, text.find_last_of(' ', 240)) + "…";
            add({{"scene", ""}, {"act", ""}, {"kind", "system"}, {"text", f.value("name", f.value("id", "")) + ": " + text},
                 {"when", json::object()}, {"where", {{"place", step.value("place", "")}}}, {"who", json::array()},
                 {"source", {{"feature", f.value("id", "")}, {"step", i}}},
                 {"accept", json::array({{{"check", "feature-step"}, {"feature", f.value("id", "")}, {"step", i}}})}});
            ++i;
        }
    }
    // The cast timeline (⑥b): each character's start and each change, a requirement each (the Cast tab).
    for (auto& r : TimelineRequirements()) items.push_back(std::move(r));
    // The placements (34.11): where each character (and story prop) is, span by span.
    for (auto& r : PlacementRequirements()) items.push_back(std::move(r));
    return items;
}

std::string GameBuild::MakeRequirements() {
    // From the plan alone, before anything is built (each place's scene is known from the build plan).
    if (rows_.empty()) return "start the build first (its plan names the places' scenes)";
    if (reqs_.value("status", "") == "making") return {};
    reqsOnly_ = status_ != "working" && stage_ != "requirements"; // made again while stopped: only they go on
    json items = RuleRequirements();
    // The AI, scene by scene: what the scene's events and beats need, besides these.
    json scenes = json::array();
    std::set<std::string> seen;
    for (const auto& r : items) {
        const std::string s = r.value("scene", "");
        if (!s.empty() && seen.insert(s).second) scenes.push_back({{"scene", s}, {"status", "queued"}});
    }
    reqs_ = {{"schema", "aige.requirements/1"}, {"status", "making"}, {"phase", "scenes"}, {"items", items}, {"scenes", scenes},
             {"implementations", json::array()}, {"implScenes", json::array()}};
    status_ = "working";
    message_ = "Requirements: the plan's, by rule (" + std::to_string(items.size()) + "); the AI reads each scene for the rest…";
    if (!host_.request) {
        // No AI service (build_scenes): the rule-made requirements and their rule-made implementations.
        reqs_["phase"] = "impl";
        reqs_["status"] = "done";
        reqs_["implementations"] = RuleImplementations();
    }
    Record("requirements", "requirements", std::to_string(items.size()) + " by rule", "the AI reads the scenes next");
    AnnotateCastPackages(); // where each character's packages run (their bed, their workplace, a marker)
    SaveRequirements();
    Save();
    return {};
}

std::string GameBuild::PlanImplementations() {
    if (reqs_.value("items", json::array()).empty()) return "make the requirements first";
    if (reqs_.value("status", "") == "making") return {};
    reqs_["status"] = "making";
    status_ = "working";
    StartImplementations();
    SaveRequirements();
    Save();
    return {};
}

void GameBuild::AskAssets() {
    // Every asset the requirements name (but the characters: drawn from their appearance in ⑪), merged and given its look.
    reqs_["phase"] = "assets";
    json given = json::array();
    for (const auto& r : reqs_["items"]) {
        if (r.value("kind", "") != "asset") continue;
        const json a = r.value("asset", json::object());
        if (a.value("type", "") == "character") continue;
        given.push_back({{"id", r.value("id", "")}, {"name", a.value("name", "")}, {"type", a.value("type", "")}, {"look", a.value("look", "")},
                         {"scenes", r.value("scenes", json::array({r.value("scene", "")}))}, {"places", a.value("places", json::array())},
                         {"from", r.value("text", "")}});
    }
    if (given.empty() || !host_.request) {
        StartImplementations();
        return;
    }
    const json brief = store_.Read("brief").value_or(json::object());
    json places = json::array();
    for (const auto& p : store_.Read("outline").value_or(json::object()).value("places", json::array())) {
        const json doc = store_.Read("places/" + PlanSlug(Str(p, "id"))).value_or(json::object());
        places.push_back({{"id", Str(p, "id")}, {"name", Str(p, "name")}, {"kind", Str(p, "kind")}, {"line", Str(p, "line")},
                          {"description", Str(doc, "description")}});
    }
    const json body = {{"label", "The game's assets and their looks"},
                       {"art", {{"style", brief.value("artStyle", json::object())}, {"tone", brief.value("tone", json::array())},
                                {"setting", brief.value("setting", json::object())}}},
                       {"places", places},
                       {"given", given}};
    const int id = host_.request("/planner/assets", body, 300);
    if (id == 0) {
        reqs_["assetsError"] = "the AI service isn't running";
        StartImplementations();
        return;
    }
    pending_[id] = "req:assets";
    message_ = "Requirements: the AI merges the " + std::to_string(given.size()) + " assets the plan names and writes how each looks…";
}

void GameBuild::MergeAssets(const json& body) {
    // The merged assets replace the ones they cover: one visual requirement each, its look, size, states and places.
    json& items = reqs_["items"];
    if (!body.value("ok", false)) {
        reqs_["assetsError"] = body.contains("errors") ? body["errors"].dump() : std::string("no answer");
        StartImplementations();
        return;
    }
    std::map<std::string, json> byId;
    for (const auto& r : items) byId[r.value("id", "")] = r;
    std::set<std::string> covered;
    json merged = json::array();
    for (const auto& a : body.value("assets", json::array())) {
        if (!a.is_object() || Str(a, "id").empty()) continue;
        json scenes = json::array(), covers = json::array();
        std::string scene, act;
        for (const auto& c : a.value("covers", json::array())) {
            if (!c.is_string() || !byId.contains(c.get<std::string>())) continue;
            const json& r = byId[c.get<std::string>()];
            covered.insert(c.get<std::string>());
            covers.push_back(c);
            if (scene.empty()) scene = r.value("scene", ""), act = r.value("act", "");
            for (const auto& s : r.value("scenes", json::array({r.value("scene", "")}))) {
                if (std::find(scenes.begin(), scenes.end(), s) == scenes.end()) scenes.push_back(s);
            }
        }
        const bool separate = a.value("separate", true);
        std::string to; // a gateway: the place it leads to (from the rule's)
        for (const auto& c : covers) {
            const json was = byId[c.get<std::string>()].value("asset", json::object());
            if (to.empty() && was.is_object()) to = was.value("to", "");
        }
        std::string where;
        for (const auto& p : a.value("places", json::array())) where += (where.empty() ? "" : ", ") + (p.is_string() ? p.get<std::string>() : std::string());
        json asset = a;
        asset["separate"] = separate;
        if (!to.empty()) asset["to"] = to, asset["type"] = "gateway";
        json accept = json::array();
        if (separate) {
            accept.push_back({{"check", "asset"}, {"name", Str(a, "name")}});
            for (const auto& p : a.value("places", json::array())) accept.push_back({{"check", "placed"}, {"name", Str(a, "name")}, {"place", p}});
        } else {
            for (const auto& p : a.value("places", json::array())) accept.push_back({{"check", "painted"}, {"name", Str(a, "name")}, {"place", p}});
        }
        merged.push_back({{"id", "v-" + Str(a, "id")}, {"scene", scene}, {"act", act}, {"kind", "asset"}, {"by", "ai"}, {"status", "todo"},
                          {"text", separate ? Str(a, "name") + ": " + Str(a, "look") : Str(a, "name") + " is painted with its place (" + where + ")"},
                          {"scenes", scenes}, {"when", json::object()}, {"where", {{"place", a.value("places", json::array()).empty() ? json("") : a["places"][0]}}},
                          {"who", json::array()}, {"source", {{"covers", covers}}}, {"asset", asset}, {"accept", accept}});
        // What it's seen doing: an animation each (a creature drawn as a character is; a prop's or effect's moves).
        int k = 0;
        for (const auto& x : a.value("animations", json::array())) {
            if (!x.is_string() || x.get<std::string>().empty()) continue;
            const std::string action = x.get<std::string>(), who = "asset:" + Str(a, "id");
            merged.push_back({{"id", "v-" + Str(a, "id") + "-anim-" + std::to_string(++k)}, {"scene", scene}, {"act", act}, {"kind", "animation"},
                              {"by", "ai"}, {"status", "todo"}, {"text", Str(a, "name") + " is animated " + action}, {"when", json::object()},
                              {"where", json::object()}, {"who", json::array()}, {"source", {{"asset", Str(a, "id")}}},
                              {"animation", {{"who", who}, {"name", Str(a, "name")}, {"action", action}, {"type", Str(a, "type")}}},
                              {"accept", json::array({{{"check", "animation"}, {"who", who}, {"action", action}}})}});
        }
    }
    // The rest kept as they were (an asset the AI left out stays as the plan named it).
    json out = json::array();
    for (const auto& r : items) {
        if (r.value("kind", "") == "asset" && covered.contains(r.value("id", ""))) continue;
        out.push_back(r);
    }
    for (auto& m : merged) out.push_back(std::move(m));
    items = std::move(out);
    reqs_.erase("assetsError");
    StartImplementations();
}

json GameBuild::RuleImplementations() const {
    // 34.2: what the build makes by rule for each requirement, from its kind (the AI's from the scenes are planned by the
    // AI, but assets: made by rule however they were found).
    json out = json::array();
    int n = 0;
    const json items = reqs_.value("items", json::array());
    // The asset requirement that covers a part or a gateway (its art comes first).
    auto assetOf = [&](const std::string& type, const std::string& name, const std::string& place) -> std::string {
        for (const auto& r : items) {
            if (r.value("kind", "") != "asset") continue;
            const json a = r.value("asset", json::object());
            const json places = a.value("places", json::array());
            if (a.value("type", "") == type && Lower(a.value("name", "")).find(Lower(name)) != std::string::npos &&
                (place.empty() || std::find(places.begin(), places.end(), place) != places.end())) return r.value("id", "");
        }
        return {};
    };
    auto add = [&](const json& r, const std::string& how, const std::string& does, const std::string& by, json after = json::array()) {
        after.erase(std::remove(after.begin(), after.end(), json("")), after.end());
        out.push_back({{"id", "i" + std::to_string(++n)}, {"req", r.value("id", "")}, {"how", how}, {"does", does}, {"by", by},
                       {"after", after}, {"status", "todo"}, {"made", json::array()}});
    };
    for (const auto& r : items) {
        const std::string kind = r.value("kind", "");
        if (r.value("by", "") != "rule" && kind != "asset" && kind != "animation") continue;
        const json accept = r.value("accept", json::array());
        const json first = accept.empty() ? json::object() : accept[0];
        const json when = r.value("when", json::object());
        const std::string stage = when.contains("quest") ? when.value("quest", "") + " stage " + std::to_string(when.value("stage", 0)) : std::string();
        const std::string place = r.value("where", json::object()).value("place", "");
        if (kind == "presence") {
            const std::string who = first.value("who", "");
            add(r, "quest-effect", "⑪ places " + who + " at story/" + r.value("scene", "") + "/" + PlanSlug(who) + " in " + r.value("where", json::object()).value("scene", "") +
                                       (stage.empty() ? "" : "; entering " + stage + " moves them there"), "rule");
        } else if (kind == "prop") {
            const std::string name = first.value("name", "");
            add(r, "prefab", "the " + name + " placed in " + r.value("where", json::object()).value("scene", ""), "rule", json::array({assetOf("prop", name, place)}));
        } else if (kind == "quest" && first.value("check", "") == "objective") {
            add(r, "objective", stage + ": the objective " + first.value("action", "") + " " + first.value("target", "") + ", bound to what ⑪ found for it, with its journal line", "rule");
        } else if (kind == "quest") {
            add(r, "objective", r.value("text", ""), "rule");
        } else if (kind == "guidance") {
            add(r, "guidance", stage + ": the journal and the objective's text name the place in the player's words", "ai-step");
            add(r, "prefab", "the gateway there drawn and placed where it can be seen", "rule", json::array({assetOf("gateway", "", place)}));
            add(r, "guidance", stage + ": the objective marker points to it", "rule");
        } else if (kind == "cutscene") {
            const json planned = r.value("cutscene", json::object());
            const json trig = planned.value("trigger", json::object());
            add(r, "cutscene", planned.empty() ? "the tracks of " + r.value("scene", "") + " written from its beats, its cast at their marks"
                                               : "the tracks of \"" + planned.value("name", "") + "\" written from its beats, each of its cast doing their part",
                "ai-step");
            if (!planned.empty()) {
                // Its start as Life ▸ Cutscenes plans it, made with its tracks.
                add(r, "trigger", "it starts " + CutsceneStartText(planned), "rule");
            } else {
                add(r, "trigger", "a trigger at its place that plays it" + (stage.empty() ? std::string() : " while " + stage + " is on"), "rule");
            }
        } else if (kind == "scene") {
            add(r, "trigger", "something at its place starts " + r.value("scene", "") + ": a trigger or a conversation", "ai-panel");
        } else if (kind == "system") {
            add(r, "script", r.value("text", ""), "ai-panel");
        } else if (kind == "character-state") {
            // Where they are and how they behave: the quests' stage effects (staged from the timeline when the quests are
            // written); their packages and dialogue: there already, else the AI panel decides how (an AI package that
            // starts at the stage, a script, dialogue offered from it).
            for (const auto& a : accept) {
                const std::string c = a.value("check", "");
                const std::string what = c == "located" ? "moved to " + a.value("scene", "") + " by the stage's effects"
                                       : c == "behaviour" ? "made " + a.value("value", "") + " by the stage's effects"
                                       : c == "package" ? "runs its packages from then (an AI package that starts at the stage, or a script)"
                                                        : "its dialogue talks about it from then";
                add(r, c == "package" ? "package" : c == "dialogue" ? "dialogue" : "quest-effect", what, "rule");
                out.back()["check"] = c;
            }
        } else if (kind == "whereabouts") {
            // A stage effect (move_actor) where the rules can, ⑪'s placing for the start, the AI panel for an event's move.
            for (const auto& w : PlacementImplementations(r)) add(r, w.value("how", ""), w.value("does", ""), w.value("by", ""));
        } else if (kind == "animation") {
            const json an = r.value("animation", json::object());
            const std::string who = an.value("who", "");
            // A character or a creature (drawn as one) is animated on its sheet; a prop or an effect (a gate opening, a glow
            // pulsing) is animated from its own picture, once that's drawn.
            const bool sheet = who.rfind("character/", 0) == 0 || an.value("type", "") == "creature" || who.rfind("class:", 0) == 0;
            add(r, "animation", an.value("name", who) + " animated " + an.value("action", "") + (sheet ? " on its sheet" : " from its picture"), "art",
                who.rfind("asset:", 0) == 0 ? json::array({"v-" + who.substr(6)}) : json::array());
        } else if (kind == "asset") {
            const json a = r.value("asset", json::object());
            const std::string name = a.value("name", "");
            if (a.value("type", "") == "character") {
                add(r, "art", "⑪ draws " + name + " from their appearance", "art");
            } else if (!a.value("separate", true)) {
                add(r, "art", name + " painted with its place's ground (⑩), named in its prompt", "art");
            } else {
                add(r, "art", name + " drawn" + (a.value("look", "").empty() ? std::string() : ": " + a.value("look", "")), "art");
                for (const auto& p : a.value("places", json::array())) {
                    if (p.is_string()) add(r, "prefab", name + " made a prefab from its art and placed in " + p.get<std::string>(), "rule");
                }
            }
        }
    }
    return out;
}

void GameBuild::StartImplementations() {
    // The rule-made items, then the AI's plan for each scene's requirements it read from the scenes.
    reqs_["phase"] = "impl";
    reqs_["implementations"] = RuleImplementations();
    json scenes = json::array();
    std::set<std::string> seen;
    for (const auto& r : reqs_["items"]) {
        if (r.value("by", "") != "ai" || r.value("kind", "") == "asset" || r.value("kind", "") == "animation") continue;
        const std::string s = r.value("scene", "");
        if (!s.empty() && seen.insert(s).second) scenes.push_back({{"scene", s}, {"status", "queued"}});
    }
    reqs_["implScenes"] = scenes;
    message_ = "Implementations: " + std::to_string(reqs_["implementations"].size()) + " by rule; the AI plans each scene's next…";
}

bool GameBuild::DriveRequirements() {
    bool did = false;
    json& scenes = reqs_["scenes"];
    json& items = reqs_["items"];
    const json trace = check_.value("trace", json::array()).empty() ? TraceScenes() : check_.value("trace", json::array());
    auto traced = [&](const std::string& scene) {
        for (const auto& t : trace) {
            if (t.value("scene", "") == scene) return t;
        }
        return json::object();
    };
    // Answers: a scene's requirements (each with its own id), the merged assets, a scene's implementations.
    for (const auto& [who, body] : reqAnswers_) {
        did = true;
        if (who == "assets") {
            MergeAssets(body);
            continue;
        }
        const int row = std::atoi(who.c_str() + 1);
        if (who[0] == 'i') {
            json& is = reqs_["implScenes"];
            if (row < 0 || row >= static_cast<int>(is.size())) continue;
            if (!body.value("ok", false)) {
                is[row]["status"] = "failed";
                is[row]["error"] = body.contains("errors") ? body["errors"].dump() : std::string("no answer");
                continue;
            }
            json& impl = reqs_["implementations"];
            int k = 0;
            for (auto it : body.value("items", json::array())) {
                if (!it.is_object() || Str(it, "does").empty()) continue;
                it["id"] = "i-" + is[row].value("scene", "") + "-" + std::to_string(++k);
                it["status"] = "todo";
                it["made"] = json::array();
                if (!it.contains("after") || !it["after"].is_array()) it["after"] = json::array();
                impl.push_back(it);
            }
            is[row]["status"] = "done";
            is[row]["count"] = k;
            continue;
        }
        if (row < 0 || row >= static_cast<int>(scenes.size())) continue;
        json& s = scenes[row];
        if (!body.value("ok", false)) {
            s["status"] = "failed";
            s["error"] = body.contains("errors") ? body["errors"].dump() : std::string("no answer");
            continue;
        }
        int k = 0;
        for (auto r : body.value("requirements", json::array())) {
            if (!r.is_object() || Str(r, "text").empty()) continue;
            if (!kKinds.contains(Str(r, "kind"))) r["kind"] = "effect";
            r["id"] = "a-" + s.value("scene", "") + "-" + std::to_string(++k);
            r["scene"] = s.value("scene", "");
            r["by"] = "ai";
            r["status"] = "todo";
            r["act"] = traced(s.value("scene", "")).value("act", "");
            if (r["kind"] == "asset") {
                json a = r.value("asset", json::object());
                if (!a.is_object()) a = json::object();
                a["places"] = json::array({traced(s.value("scene", "")).value("location", "")});
                r["asset"] = a;
                r["scenes"] = json::array({s.value("scene", "")});
            }
            if (!r.contains("accept") || !r["accept"].is_array()) r["accept"] = json::array();
            items.push_back(r);
        }
        s["status"] = "done";
        s["count"] = k;
    }
    reqAnswers_.clear();
    const std::string phase = reqs_.value("phase", "scenes");
    auto isOpen = [](const json& list) {
        return std::any_of(list.begin(), list.end(), [](const json& s) { return s.value("status", "") == "queued" || s.value("status", "") == "asking"; });
    };
    auto asking = [](const json& list) {
        return std::any_of(list.begin(), list.end(), [](const json& s) { return s.value("status", "") == "asking"; });
    };
    if (phase == "scenes") {
        // The next scene to the AI (one at a time).
        for (size_t i = 0; i < scenes.size() && !asking(scenes); ++i) {
            json& s = scenes[i];
            if (s.value("status", "") != "queued") continue;
            const std::string scene = s.value("scene", "");
            const json doc = store_.Read("story/scenes/" + scene).value_or(json::object());
            const json t = traced(scene);
            json given = json::array();
            for (const auto& r : items) {
                if (r.value("scene", "") == scene || (r.value("kind", "") == "asset" && r.value("asset", json::object()).value("type", "") != "character")) {
                    given.push_back({{"kind", r.value("kind", "")}, {"text", r.value("text", "")}});
                }
            }
            const std::string sceneFile = t.value("locationScene", "");
            json cast = json::array();
            for (const auto& c : t.value("cast", json::array())) {
                const std::string met = MetAt(c.value("who", ""), t.value("location", ""));
                cast.push_back({{"key", c.value("who", "")}, {"name", c.value("name", "")}, {"player", c.value("player", false)},
                                {"fights", c.value("hostile", false)}, {"livesIn", c.value("homeScene", "")},
                                {"metIn", met == t.value("location", "") ? json("the scene's place") : json(met)}});
            }
            json stages = json::array();
            for (const auto& st : t.value("stages", json::array())) {
                stages.push_back({{"quest", st.value("quest", "")}, {"stage", st.value("index", 0)}, {"journal", st.value("journal", "")},
                                  {"objectives", st.value("objectives", json::array())}});
            }
            const json body = {{"label", "Requirements of " + scene},
                               {"scene", {{"id", scene}, {"summary", doc.value("summary", "")}, {"card", doc.value("card", json::object())},
                                          {"beats", doc.value("beats", json::array())}, {"events", doc.value("events", json::array())}}},
                               {"place", {{"id", t.value("location", "")}, {"scene", sceneFile}, {"built", SceneFacts(sceneFile)}}},
                               {"cast", cast},
                               {"stages", stages},
                               {"given", given}};
            const int id = host_.request ? host_.request("/planner/requirements", body, 300) : 0;
            if (id == 0) {
                s["status"] = "failed";
                s["error"] = "the AI service isn't running";
                did = true;
                continue;
            }
            pending_[id] = "req:s" + std::to_string(i);
            s["status"] = "asking";
            int done = 0;
            for (const auto& x : scenes) done += x.value("status", "") == "done" || x.value("status", "") == "failed";
            message_ = "Requirements: the AI reads " + scene + " (" + std::to_string(done + 1) + " of " + std::to_string(scenes.size()) + ")…";
            did = true;
            break;
        }
        if (!isOpen(scenes)) {
            AskAssets();
            did = true;
        }
    } else if (phase == "impl") {
        json& is = reqs_["implScenes"];
        for (size_t i = 0; i < is.size() && !asking(is); ++i) {
            json& s = is[i];
            if (s.value("status", "") != "queued") continue;
            const std::string scene = s.value("scene", "");
            const json t = traced(scene);
            const std::string sceneFile = t.value("locationScene", "");
            json reqs = json::array(), ruled = json::array(), assets = json::array();
            for (const auto& r : items) {
                if (r.value("kind", "") == "asset") {
                    const json a = r.value("asset", json::object());
                    if (a.value("type", "") != "character") {
                        assets.push_back({{"name", a.value("name", "")}, {"type", a.value("type", "")}, {"separate", a.value("separate", true)},
                                          {"places", a.value("places", json::array())}, {"req", r.value("id", "")}});
                    }
                    continue;
                }
                if (r.value("scene", "") != scene || r.value("kind", "") == "animation") continue;
                const json brief = {{"id", r.value("id", "")}, {"kind", r.value("kind", "")}, {"text", r.value("text", "")}, {"when", r.value("when", json::object())},
                                    {"where", r.value("where", json::object())}, {"who", r.value("who", json::array())}, {"accept", r.value("accept", json::array())}};
                (r.value("by", "") == "ai" ? reqs : ruled).push_back(brief);
            }
            json cast = json::array();
            for (const auto& c : t.value("cast", json::array())) cast.push_back({{"key", c.value("who", "")}, {"name", c.value("name", "")}, {"player", c.value("player", false)}});
            json stages = json::array();
            for (const auto& st : t.value("stages", json::array())) {
                stages.push_back({{"quest", st.value("quest", "")}, {"stage", st.value("index", 0)}, {"objectives", st.value("objectives", json::array())}});
            }
            const json body = {{"label", "Implementations of " + scene},
                               {"scene", {{"id", scene}, {"goal", t.value("goal", "")}, {"place", t.value("location", "")}, {"file", sceneFile},
                                          {"built", SceneFacts(sceneFile)}}},
                               {"cast", cast},
                               {"stages", stages},
                               {"assets", assets},
                               {"requirements", reqs},
                               {"builtByRule", ruled}};
            const int id = host_.request ? host_.request("/planner/implement", body, 300) : 0;
            if (id == 0) {
                s["status"] = "failed";
                s["error"] = "the AI service isn't running";
                did = true;
                continue;
            }
            pending_[id] = "req:i" + std::to_string(i);
            s["status"] = "asking";
            int done = 0;
            for (const auto& x : is) done += x.value("status", "") == "done" || x.value("status", "") == "failed";
            message_ = "Implementations: the AI plans " + scene + " (" + std::to_string(done + 1) + " of " + std::to_string(is.size()) + ")…";
            did = true;
            break;
        }
        if (!isOpen(is) && reqs_.value("status", "") == "making") {
            reqs_["status"] = "done";
            int ai = 0;
            for (const auto& r : items) ai += r.value("by", "") == "ai";
            const size_t impl = reqs_["implementations"].size();
            message_ = "Requirements: " + std::to_string(items.size()) + " (" + std::to_string(ai) + " from the scenes and the assets' looks); " +
                       std::to_string(impl) + " implementation items.";
            Record("requirements", "requirements", std::to_string(items.size()) + " in all, " + std::to_string(impl) + " implementation items",
                   std::to_string(ai) + " read from the scenes by the AI");
            did = true;
        }
    }
    if (did) SaveRequirements();
    return did;
}

void GameBuild::SaveRequirements() const {
    std::ofstream(store_.Dir() / "requirements.json", std::ios::binary) << reqs_.dump(1);
}

void GameBuild::LoadRequirements() {
    std::ifstream in(store_.Dir() / "requirements.json", std::ios::binary);
    reqs_ = in ? json::parse(in, nullptr, false) : json::object();
    if (!reqs_.is_object()) reqs_ = json::object();
    // A scene the AI was reading when the editor closed: asked again.
    if (reqs_.contains("scenes") && reqs_["scenes"].is_array()) {
        for (auto& s : reqs_["scenes"]) {
            if (s.value("status", "") == "asking") s["status"] = "queued";
        }
    }
    if (reqs_.contains("implScenes") && reqs_["implScenes"].is_array()) {
        for (auto& s : reqs_["implScenes"]) {
            if (s.value("status", "") == "asking") s["status"] = "queued";
        }
    }
    // The assets being merged: asked again once the scenes are read (they are).
    if (reqs_.value("phase", "") == "assets") reqs_["phase"] = "scenes";
}

} // namespace aige
