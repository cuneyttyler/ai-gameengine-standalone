// The Planner's stages (development_plan.md 22.2-22.9): what each job asks the AI service,
// and what each answer becomes: documents, graph facts, World Bible entries, decisions.
#include "planner/GamePlan.h"

#include "places/Brief.h"
#include "places/Ontology.h"
#include "story/StoryRules.h"
#include "world/WorldBible.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace aige {

using json = nlohmann::json;

namespace {

const char* kPending = "__pending__";

json CompactBrief(json brief) {
    for (const char* k : {"plan", "assumed", "request", "confirmed", "inputs", "revision", "madeBy", "schema", "locked"}) brief.erase(k);
    return brief;
}

// The word nearest to `word` in a closed list, by hints (a word it contains -> the option);
// `fallback` when none matches.
std::string Nearest(std::string word, const std::vector<std::pair<const char*, const char*>>& hints, const std::string& fallback) {
    for (auto& c : word) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& [hint, to] : hints) {
        if (word.find(hint) != std::string::npos) return to;
    }
    return fallback;
}

// A relationship type outside the vocabulary: the nearest that's in it (by its words, else its
// valence). "skeptical-of" -> rival, "guides" -> mentor-of.
std::string NearestRelationship(const std::string& type, const json& valence) {
    static const std::vector<std::pair<const char*, const char*>> hints = {
        {"betray", "betrayed-by"}, {"distrust", "rival"}, {"suspic", "rival"}, {"skeptic", "rival"}, {"rival", "rival"},
        {"trust", "trusts"}, {"fear", "fears"}, {"love", "romantic"}, {"roman", "romantic"}, {"friend", "friend"},
        {"mentor", "mentor-of"}, {"teach", "mentor-of"}, {"student", "student-of"}, {"serv", "serves"},
        {"employ", "employs"}, {"owe", "owes"}, {"debt", "owes"}, {"enem", "enemy"}, {"hate", "enemy"},
        {"member", "member-of"}, {"guide", "mentor-of"}, {"ally", "friend"}, {"protect", "friend"},
        {"parent", "parent"}, {"father", "parent"}, {"mother", "parent"}, {"child", "child"}, {"son", "child"}, {"daughter", "child"},
        {"brother", "sibling"}, {"sister", "sibling"}, {"sibling", "sibling"}, {"wife", "spouse"}, {"husband", "spouse"}, {"spouse", "spouse"}};
    const std::string near = Nearest(type, hints, "");
    if (!near.empty()) return near;
    const double v = valence.is_number() ? valence.get<double>() : 0.0;
    return v <= -0.5 ? "enemy" : v < 0 ? "rival" : v >= 0.5 ? "trusts" : "friend";
}

// The scene's answer with what can't be used taken out, before anything reads it: lists hold
// objects (events, facts, ends, new entities) or ids; a fact's p, s and o are text; its from,
// until and branch are text or left out ("from": true becomes "from this scene").
json CleanSceneAnswer(json scene) {
    if (!scene.is_object()) return json::object();
    auto objects = [&](const char* key) {
        json kept = json::array();
        if (scene.contains(key) && scene[key].is_array()) {
            for (const auto& v : scene[key]) {
                if (v.is_object()) kept.push_back(v);
            }
        }
        scene[key] = kept;
    };
    for (const char* key : {"newEntities", "events", "facts", "end"}) objects(key);
    auto textOnly = [](json& o, const char* key) {
        if (o.contains(key) && !(o[key].is_string() && !o[key].get<std::string>().empty())) o.erase(key);
    };
    for (auto& e : scene["events"]) {
        for (const char* key : {"causes", "enables"}) {
            json ids = json::array();
            if (e.contains(key) && e[key].is_array()) {
                for (const auto& v : e[key]) {
                    if (v.is_string()) ids.push_back(v);
                }
            } else if (e.contains(key) && e[key].is_string()) {
                ids.push_back(e[key]);
            }
            e[key] = ids;
        }
        for (const char* key : {"id", "class", "text", "serves", "resolves", "paysOff"}) {
            if (e.contains(key) && !e[key].is_string()) e.erase(key);
        }
    }
    for (const char* list : {"facts", "end"}) {
        json kept = json::array();
        for (auto f : scene[list]) {
            if (!f.value("p", json()).is_string() || !f.value("s", json()).is_string()) continue;
            if (f.contains("o") && !f["o"].is_string()) f.erase("o");
            for (const char* key : {"from", "until", "branch", "at"}) textOnly(f, key);
            kept.push_back(f);
        }
        scene[list] = kept;
    }
    for (auto& n : scene["newEntities"]) {
        for (const char* key : {"id", "class", "name", "line", "importance", "kind"}) {
            if (n.contains(key) && !n[key].is_string()) n.erase(key);
        }
        // "amulet" (class Item) is item/amulet: the id names its kind.
        std::string nid = n.value("id", std::string());
        if (nid.empty()) nid = n.value("name", std::string());
        if (!nid.empty() && nid.find('/') == std::string::npos) {
            std::string kind = n.value("class", std::string("Item"));
            for (auto& c : kind) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::string slug;
            for (const char c : nid) {
                if (std::isalnum(static_cast<unsigned char>(c))) slug += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                else if (!slug.empty() && slug.back() != '-') slug += '-';
            }
            while (!slug.empty() && slug.back() == '-') slug.pop_back();
            if (!slug.empty()) n["id"] = kind + "/" + slug;
        }
    }
    if (scene.contains("summary") && !scene["summary"].is_string()) scene.erase("summary");
    return scene;
}

json Vocab(const StoryOntology& gso, std::initializer_list<const char*> schemes) {
    json out = json::object();
    for (const char* s : schemes) {
        auto it = gso.vocabularies.find(s);
        if (it != gso.vocabularies.end()) out[s] = it->second;
    }
    return out;
}

std::string Text(const json& j, const char* key) {
    if (!j.is_object()) return {};
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::vector<std::string> Strings(const json& j) {
    std::vector<std::string> out;
    if (j.is_array()) {
        for (const auto& v : j) {
            if (v.is_string()) out.push_back(v.get<std::string>());
        }
    }
    return out;
}

// Participants as [{who, role}], from strings or objects.
json Participants(const json& card) {
    json out = json::array();
    for (const auto& p : card.value("participants", json::array())) {
        if (p.is_string()) out.push_back({{"who", p}, {"role", "agent"}});
        else if (p.is_object() && !Text(p, "who").empty()) out.push_back({{"who", p["who"]}, {"role", p.value("role", "agent")}});
    }
    return out;
}

void RemoveMadeBy(StoryGraph& g, const std::string& decision, const StoryOntology& gso, bool eventsToo) {
    if (decision.empty()) return;
    g.edges.erase(std::remove_if(g.edges.begin(), g.edges.end(), [&](const StoryEdge& e) { return e.madeBy == decision; }), g.edges.end());
    if (!eventsToo) return;
    std::vector<std::string> gone;
    for (const auto& n : g.nodes) {
        if (n.Text("madeBy") == decision && (gso.IsA(n.cls, "Event") || n.cls == "QuestStage" || n.cls == "Alias")) gone.push_back(n.id);
    }
    for (const auto& id : gone) g.RemoveNode(id);
}

void StampMadeBy(StoryGraph& g, const std::string& decision) {
    for (auto& e : g.edges) {
        if (e.madeBy == kPending) e.madeBy = decision;
    }
    for (auto& n : g.nodes) {
        if (n.Text("madeBy") == kPending) n.fields["madeBy"] = decision;
    }
}

// next / hasChoice / branchesTo between the scenes, from their order and branches (§6.3):
// a branch's scenes follow a choice after the main-line scene before them and meet again
// at a convergence before the next main-line scene; the last scene leads to the endings.
void LinkDiscourse(StoryGraph& g) {
    const std::string tag = "planner:discourse";
    g.edges.erase(std::remove_if(g.edges.begin(), g.edges.end(), [&](const StoryEdge& e) { return e.madeBy == tag; }), g.edges.end());
    std::vector<std::string> made;
    for (const auto& n : g.nodes) {
        if (n.Text("madeBy") == tag) made.push_back(n.id);
    }
    for (const auto& id : made) g.RemoveNode(id);
    auto edge = [&](const std::string& p, const std::string& s, const std::string& o, json fields = json::object()) {
        StoryEdge e;
        e.p = p;
        e.s = s;
        e.o = o;
        e.madeBy = tag;
        e.fields = std::move(fields);
        g.edges.push_back(std::move(e));
    };
    auto node = [&](const std::string& id, const std::string& cls, json fields) {
        StoryNode& n = g.Upsert(id, cls);
        n.fields = std::move(fields);
        n.fields["madeBy"] = tag;
    };
    std::vector<std::string> main, all = g.SceneOrder();
    for (const auto& s : all) {
        if (g.BranchOf(s).empty()) main.push_back(s);
    }
    for (size_t i = 0; i < main.size(); ++i) {
        const double from = g.OrderOf(main[i]);
        const double to = i + 1 < main.size() ? g.OrderOf(main[i + 1]) : 1e18;
        std::map<std::string, std::vector<std::string>> branches;
        for (const auto& s : all) {
            const double o = g.OrderOf(s);
            if (!g.BranchOf(s).empty() && o > from && o < to) branches[g.BranchOf(s)].push_back(s);
        }
        if (branches.empty()) {
            if (i + 1 < main.size()) edge("next", main[i], main[i + 1]);
            continue;
        }
        const std::string choice = "choice/" + main[i];
        json options = json::array();
        for (const auto& [b, scenes] : branches) options.push_back(b);
        node(choice, "ChoicePoint", {{"options", options}});
        edge("hasChoice", main[i], choice);
        const std::string meet = i + 1 < main.size() ? "conv/" + main[i + 1] : "";
        if (!meet.empty()) node(meet, "Convergence", {{"carries", json::array()}});
        for (const auto& [b, scenes] : branches) {
            edge("branchesTo", choice, scenes.front(), {{"option", b}});
            for (size_t k = 0; k + 1 < scenes.size(); ++k) edge("next", scenes[k], scenes[k + 1]);
            if (!meet.empty()) edge("next", scenes.back(), meet);
        }
        if (!meet.empty()) edge("next", meet, main[i + 1]);
    }
    // The endings.
    std::vector<std::string> endings;
    for (const auto& n : g.nodes) {
        if (n.cls == "Ending") endings.push_back(n.id);
    }
    if (!main.empty() && !endings.empty()) {
        if (endings.size() == 1) {
            edge("next", main.back(), endings.front());
        } else {
            const std::string choice = "choice/ending";
            json options = json::array();
            for (const auto& e : endings) options.push_back(PlanSlug(e));
            node(choice, "ChoicePoint", {{"options", options}});
            edge("hasChoice", main.back(), choice);
            for (const auto& e : endings) edge("branchesTo", choice, e, {{"option", PlanSlug(e)}});
        }
    }
}

} // namespace

std::string GamePlan::PathFor(const Job& job) const {
    static const std::map<std::string, std::string> paths = {
        {"interpret", "/planner/concept"}, {"outline", "/planner/outline"}, {"act", "/planner/act"},
        {"scene", "/planner/scene"},       {"critic", "/planner/critic"},   {"character", "/planner/character"},
        {"quests", "/planner/quests"},     {"quest", "/planner/quests"},    {"place", "/planner/place"},     {"systems", "/planner/systems"},
        {"visuals", "/planner/visuals"},   {"review", "/planner/review"},   {"revise", "/planner/revise"}};
    auto it = paths.find(job.kind);
    return it == paths.end() ? "" : it->second;
}

json GamePlan::RequestBody(const Job& job) {
    const json brief = CompactBrief(Brief());
    const json outline = Outline();
    const StoryGraph g = Graph();
    json cast = json::array(), places = json::array();
    for (const auto& c : outline.value("cast", json::array())) {
        cast.push_back({{"id", c.value("id", "")}, {"name", c.value("name", "")}, {"importance", c.value("importance", "")}, {"line", c.value("line", "")}});
    }
    for (const auto& p : outline.value("places", json::array())) {
        places.push_back({{"id", p.value("id", "")}, {"name", p.value("name", "")}, {"kind", p.value("kind", "")}, {"line", p.value("line", "")}});
    }
    std::vector<std::string> buildTypes = Strings(host_.catalog.value("buildTypes", json::array()));
    if (buildTypes.empty()) buildTypes = {"settlement", "interior", "dungeon", "wilderness"};

    if (job.kind == "interpret") {
        return {{"request", request_}, {"reading", job.subject}, {"schema", BriefSchemaForAi(gso_)},
                {"scope", PlannerData::Engine().scope.value("classes", json::array())}, {"defaults", store_.Defaults()}};
    }
    if (job.kind == "outline") {
        const std::string structure = brief.value("structure", "three-act");
        json tmpl = json::object();
        if (auto t = gso_.templates.find(structure); t != gso_.templates.end()) {
            json acts = json::array(), beats = json::array();
            for (const auto& [a, share] : t->second.acts) acts.push_back({{"id", a}, {"share", share}});
            for (const auto& [b, at] : t->second.beats) beats.push_back({{"id", b}, {"at", at}});
            tmpl = {{"id", structure}, {"acts", acts}, {"beats", beats}, {"roles", t->second.roles}};
        }
        return {{"brief", brief}, {"template", tmpl}, {"vocab", Vocab(gso_, {"NarrativeRole", "GameplayRole", "Importance"})},
                {"buildTypes", buildTypes}};
    }
    if (job.kind == "act") {
        json act = json::object(), previous = json::array();
        const auto acts = outline.value("story", json::object()).value("acts", json::array());
        for (const auto& a : acts) {
            if (a.value("id", "") == job.subject) {
                act = a;
                break;
            }
            if (auto doc = store_.Read("story/acts/" + a.value("id", ""))) {
                json chapters = json::array();
                for (const auto& ch : doc->value("chapters", json::array())) {
                    chapters.push_back({{"id", ch.value("id", "")}, {"question", ch.value("question", "")}, {"summary", ch.value("summary", "")}});
                }
                previous.push_back({{"act", a.value("id", "")}, {"chapters", chapters}});
            }
        }
        const json scenes = BriefGet(brief, "scope.scenes");
        double total = 12;
        if (scenes.is_array() && scenes.size() == 2) total = (scenes[0].get<double>() + scenes[1].get<double>()) / 2.0;
        else if (scenes.is_number()) total = scenes.get<double>();
        const int target = std::max(1, static_cast<int>(std::lround(total / std::max<size_t>(1, acts.size()))));
        return {{"brief", brief}, {"story", outline.value("story", json::object())}, {"cast", cast}, {"places", places}, {"act", act},
                {"previous", previous}, {"sceneTarget", target}, {"vocab", Vocab(gso_, {"SceneMode", "Value"})}};
    }
    if (job.kind == "scene") {
        const json doc = store_.Read("story/scenes/" + job.subject).value_or(json::object());
        const std::string actId = doc.value("act", "");
        const json actDoc = store_.Read("story/acts/" + actId).value_or(json::object());
        json chapter = json::object();
        for (const auto& ch : actDoc.value("chapters", json::array())) {
            for (const auto& s : ch.value("scenes", json::array())) {
                if (s == job.subject) chapter = {{"id", ch.value("id", "")}, {"question", ch.value("question", "")}, {"summary", ch.value("summary", "")}};
            }
        }
        json previous = json::object(), earlier = json::array();
        const auto order = g.SceneOrder();
        const auto at = std::find(order.begin(), order.end(), job.subject);
        for (auto it = order.begin(); it != at; ++it) {
            const json s = store_.Read("story/scenes/" + *it).value_or(json::object());
            if (s.value("status", "") != "written") continue;
            earlier.push_back({{"id", *it}, {"summary", s.value("summary", "")}});
            previous = {{"id", *it}, {"summary", s.value("summary", "")}, {"beats", s.value("beats", json::array())}};
        }
        if (earlier.size() > 20) earlier.erase(earlier.begin(), earlier.end() - 20);
        json properties = json::array();
        for (const auto& [id, p] : gso_.properties) {
            if (id == "madeBy" || id == "realizedBy") continue;
            json f = json::array();
            for (const auto& [name, type] : p.fields) f.push_back(name);
            properties.push_back({{"id", id}, {"domain", p.domain}, {"range", p.range}, {"temporal", p.temporal}, {"fields", f}});
        }
        json mainQuest = store_.Read("quests/q-main").value_or(json::object());
        json people = json::array();
        for (const auto* n : g.OfClass("Character", gso_)) people.push_back({{"id", n->id}, {"name", n->Text("name")}, {"importance", n->Text("importance")}});
        json sites = json::array();
        for (const auto* n : g.OfClass("Location", gso_)) sites.push_back({{"id", n->id}, {"name", n->Text("name")}, {"kind", n->Text("buildType")}});
        return {{"brief", brief},
                {"story", {{"logline", outline.value("story", json::object()).value("logline", "")},
                           {"synopsis", outline.value("story", json::object()).value("synopsis", "")}}},
                {"act", {{"id", actId}, {"summary", actDoc.value("summary", "")}}},
                {"chapter", chapter},
                {"id", job.subject},
                {"card", doc.value("card", json::object())},
                {"world", g.Subgraph(job.subject)},
                {"previous", previous},
                {"earlier", earlier},
                {"cast", people},
                {"places", sites},
                {"properties", properties},
                {"vocab", Vocab(gso_, {"RelationshipType"})},
                {"mainQuest", {{"id", "q-main"}, {"stages", mainQuest.value("stages", json::array())}}}};
    }
    if (job.kind == "critic") {
        json scenes = json::array();
        for (const auto& id : g.SceneOrder()) {
            const json s = store_.Read("story/scenes/" + id).value_or(json::object());
            if (s.value("act", "") != job.subject) continue;
            scenes.push_back({{"id", id}, {"card", s.value("card", json::object())}, {"summary", s.value("summary", "")}});
        }
        json issues = json::array();
        for (const auto& i : CheckStory(g, gso_)) {
            if (i.rule.rfind("R-S", 0) == 0 || i.rule.rfind("R-C", 0) == 0) issues.push_back({{"rule", i.rule}, {"node", i.node}, {"message", i.message}});
        }
        return {{"brief", brief}, {"act", job.subject}, {"scenes", scenes}, {"issues", issues}};
    }
    if (job.kind == "character") {
        json entry = json::object();
        for (const auto& c : outline.value("cast", json::array())) {
            if (c.value("id", "") == job.subject) entry = c;
        }
        if (entry.empty()) {
            if (const StoryNode* n = g.Find(job.subject)) entry = {{"id", n->id}, {"name", n->Text("name")}, {"importance", n->Text("importance")}, {"line", n->Text("summary")}};
        }
        json scenes = json::array();
        for (const auto& id : g.SceneOrder()) {
            const auto who = g.Participants(id);
            if (std::find(who.begin(), who.end(), job.subject) == who.end()) continue;
            const json s = store_.Read("story/scenes/" + id).value_or(json::object());
            scenes.push_back({{"id", id}, {"summary", s.value("summary", "")}, {"mode", s.value("card", json::object()).value("mode", "")}});
        }
        json others = json::array();
        for (const auto* n : g.OfClass("Character", gso_)) {
            if (n->id != job.subject) others.push_back({{"id", n->id}, {"name", n->Text("name")}});
        }
        return {{"brief", brief}, {"entry", entry}, {"scenes", scenes}, {"facts", g.Timeline(job.subject)}, {"cast", others},
                {"packageTemplates", host_.catalog.value("packageTemplates", json::array())},
                {"vocab", Vocab(gso_, {"Trait", "Motivation", "RelationshipType", "GameplayRole"})}};
    }
    if (job.kind == "quests" || job.kind == "quest") {
        json characters = json::array();
        for (const auto* n : g.OfClass("Character", gso_)) {
            const auto where = g.Out(n->id, "locatedAt");
            characters.push_back({{"id", n->id}, {"name", n->Text("name")}, {"importance", n->Text("importance")},
                                  {"motivations", n->List("motivations")}, {"summary", n->Text("summary")},
                                  {"location", where.empty() ? "" : where.front()->o}});
        }
        json sites = json::array();
        for (const auto* n : g.OfClass("Location", gso_)) sites.push_back({{"id", n->id}, {"name", n->Text("name")}, {"kind", n->Text("buildType")}});
        json scenes = json::array();
        for (const auto& id : g.SceneOrder()) {
            const StoryNode* n = g.Find(id);
            const json s = store_.Read("story/scenes/" + id).value_or(json::object());
            scenes.push_back({{"id", id}, {"mode", n->Text("mode")}, {"kernel", n->Flag("kernel")}, {"branch", n->Text("branch")},
                              {"location", g.LocationOfScene(id)}, {"summary", s.value("summary", "")}});
        }
        const json side = BriefGet(brief, "scope.sideQuests");
        int target = 3;
        if (side.is_array() && side.size() == 2) target = static_cast<int>(std::lround((side[0].get<double>() + side[1].get<double>()) / 2.0));
        const GenreProfile* profile = PlannerData::Engine().Profile(brief.value("genre", ""));
        json keep = json::array();
        for (const auto& doc : store_.List("quests")) {
            if (store_.Locked(doc)) keep.push_back(store_.Read(doc).value_or(json::object()));
        }
        json body = {{"brief", brief}, {"skeleton", store_.Read("quests/q-main").value_or(json::object())}, {"characters", characters},
                     {"places", sites}, {"scenes", scenes}, {"sideQuests", target},
                     {"mode", profile && profile->stages.is_object() && profile->stages.value("quests", json("full")).is_string() ? profile->stages.value("quests", "full") : "full"},
                     {"keep", keep}, {"strategies", gso_.strategies}, {"vocab", Vocab(gso_, {"ObjectiveAction", "Motivation"})}};
        if (job.kind == "quest") {
            // This call designs one quest; the others made so far, so a side quest doesn't repeat them.
            body["only"] = job.subject;
            json designed = json::array();
            for (const auto& doc : store_.List("quests")) {
                const json q = store_.Read(doc).value_or(json::object());
                if (!q.is_object() || Text(q, "slot") == job.subject || Text(q, "kind") == "skeleton") continue;
                designed.push_back({{"id", Text(q, "id")}, {"name", Text(q, "name")}, {"giver", q.value("giver", json())}, {"strategy", Text(q, "strategy")}});
            }
            body["designed"] = designed;
        }
        return body;
    }
    if (job.kind == "place") {
        const StoryNode* n = g.Find(job.subject);
        json entry = json::object();
        for (const auto& p : outline.value("places", json::array())) {
            if (p.value("id", "") == job.subject) entry = p;
        }
        if (entry.empty() && n) entry = {{"id", n->id}, {"name", n->Text("name")}, {"kind", n->Text("buildType")}};
        const std::string kind = entry.value("kind", n ? n->Text("buildType") : "interior");
        json scenes = json::array();
        for (const auto& id : g.SceneOrder()) {
            if (g.LocationOfScene(id) != job.subject) continue;
            const json s = store_.Read("story/scenes/" + id).value_or(json::object());
            scenes.push_back({{"id", id}, {"summary", s.value("summary", "")}, {"needsParts", g.Find(id)->List("needsParts")}});
        }
        json anchors = json::array();
        for (const auto& doc : store_.List("quests")) {
            const json q = store_.Read(doc).value_or(json::object());
            for (const auto& a : q.value("aliases", json::array())) {
                if (a.value("in", "") == job.subject) anchors.push_back({{"quest", q.value("id", "")}, {"alias", a.value("id", "")}, {"kind", a.value("kind", "")}, {"where", a.value("where", json::object())}});
            }
        }
        json residents = json::array(), connections = json::array();
        for (const auto* e : g.In(job.subject, "locatedAt")) residents.push_back(e->s);
        for (const auto& e : g.edges) {
            if (e.p == "connects" && (e.s == job.subject || e.o == job.subject)) connections.push_back({{"to", e.s == job.subject ? e.o : e.s}, {"kind", e.Text("kind")}});
            if (e.p == "inside" && e.s == job.subject) connections.push_back({{"to", e.o}, {"kind", "inside"}});
        }
        json interpret = json::object();
        if (host_.places) interpret = InterpretContext(host_.places(), kind);
        return {{"brief", brief}, {"entry", entry}, {"kind", kind}, {"scenes", scenes}, {"anchors", anchors}, {"residents", residents},
                {"connections", connections}, {"interpret", interpret}};
    }
    if (job.kind == "systems") {
        json characters = json::array();
        for (const auto& doc : store_.List("characters")) {
            const json c = store_.Read(doc).value_or(json::object());
            characters.push_back({{"id", c.value("id", "")}, {"name", c.value("name", "")}, {"gameplay", c.value("gameplay", json::object())}});
        }
        json quests = json::array();
        for (const auto& doc : store_.List("quests")) {
            const json q = store_.Read(doc).value_or(json::object());
            quests.push_back({{"id", q.value("id", "")}, {"level", q.value("level", json())}, {"rewards", q.value("rewards", json::array())}});
        }
        return {{"brief", brief}, {"characters", characters}, {"quests", quests},
                {"recordTypes", host_.catalog.value("recordTypes", json::object())},
                {"abilities", host_.catalog.value("abilities", json::array())}, {"effects", host_.catalog.value("effects", json::array())}};
    }
    if (job.kind == "visuals") {
        return {{"brief", brief}, {"themes", host_.catalog.value("themes", json::array())}, {"windows", PlanWindows()},
                {"widgets", PlanHudWidgets()}, {"systems", store_.Read("systems").value_or(json::object()).value("summary", json())}};
    }
    if (job.kind == "revise") {
        // The documents the words name, in full; an index of the rest.
        std::string words;
        for (char ch : job.subject) words += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        json named = json::object(), index = json::array();
        for (const char* folder : {"characters", "places", "quests", "story/scenes"}) {
            for (const auto& doc : store_.List(folder)) {
                const json d = store_.Read(doc).value_or(json::object());
                std::string name = Text(d, "name").empty() ? Text(d, "summary") : Text(d, "name");
                index.push_back({{"doc", doc}, {"name", name}});
                for (auto& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                const std::string slug = PlanSlug(doc);
                if ((name.size() >= 3 && words.find(name) != std::string::npos) || words.find(slug) != std::string::npos) {
                    json shown = d;
                    for (const char* k : {"inputs", "revision", "madeBy", "schema"}) shown.erase(k);
                    named[doc] = shown;
                }
            }
        }
        return {{"instruction", job.subject}, {"brief", brief}, {"story", outline.value("story", json::object())}, {"cast", cast},
                {"places", places}, {"documents", named}, {"index", index}};
    }
    if (job.kind == "review") {
        json warnings = json::array();
        for (const auto& i : CheckStory(g, gso_)) {
            if (warnings.size() < 40) warnings.push_back(i.rule + ": " + i.message);
        }
        return {{"brief", brief}, {"story", outline.value("story", json::object())}, {"cast", cast}, {"places", places}, {"issues", warnings}};
    }
    return json::object();
}

std::vector<std::string> GamePlan::Handle(Job& job, const json& body) {
    const std::string rationale = body.value("rationale", "");
    auto decide = [&](const std::string& kind, const std::string& subject, const std::string& chosen) {
        PlanDecision d;
        d.stage = StageOfJob(job.kind);
        d.actor = "ai";
        d.kind = kind;
        d.subject = subject;
        d.chosen = chosen;
        d.rationale = rationale;
        d.call = body.value("call", "");
        d.inputs = {{"brief", store_.RevisionOf("brief")}, {"outline", store_.RevisionOf("outline")}, {"kg", store_.RevisionOf("kg")}};
        if (job.attempts > 1) d.checks = {{"repaired", true}};
        return store_.Decide(d);
    };
    if (job.kind == "interpret") {
        concept_["drafts"].push_back({{"reading", job.subject},
                                      {"brief", body.value("brief", json::object())},
                                      {"confidence", body.value("confidence", json::object())},
                                      {"assumptions", body.value("assumptions", json::array())},
                                      {"decisions", body.value("decisions", json::array())}});
        return {};
    }
    if (job.kind == "outline") {
        json outline = body.value("outline", json::object());
        // Roles filed under the wrong kind ("companion" as a narrative role) are put right, not refused.
        const auto repaired = RepairOutline(outline, gso_);
        std::vector<std::string> buildTypes = Strings(host_.catalog.value("buildTypes", json::array()));
        const auto problems = ValidateOutline(outline, Brief(), gso_, buildTypes);
        if (HasErrors(problems)) return ProblemTexts(problems);
        const std::string decision = decide("write-outline", "outline", outline.value("story", json::object()).value("logline", ""));
        if (!repaired.empty()) {
            PlanDecision fix;
            fix.stage = "outline";
            fix.actor = "rule";
            fix.kind = "fix";
            fix.subject = "outline";
            for (const auto& r : repaired) fix.chosen += (fix.chosen.empty() ? "" : "; ") + r;
            fix.rationale = "Cast roles from the wrong vocabulary were moved to the right one, or dropped.";
            store_.Decide(fix);
        }
        return HandleOutline(job, outline, decision);
    }
    if (job.kind == "act") {
        const json act = body.value("act", json::object());
        const auto problems = ValidateActPlan(act, Outline(), gso_);
        if (HasErrors(problems)) return ProblemTexts(problems);
        return HandleAct(job, act, decide("plan-act", "story/acts/" + job.subject, std::to_string(act.value("chapters", json::array()).size()) + " chapters"));
    }
    if (job.kind == "scene") {
        // What can't be used (a fact as plain text, "from": false) is taken out before the checks.
        json scene = CleanSceneAnswer(body.value("scene", json::object()));
        // A relation the story ontology doesn't have ("conflictActive"): left out, noted.
        json fixes = json::array(), facts = json::array();
        for (const auto& f : scene["facts"]) {
            if (gso_.Property(Text(f, "p"))) facts.push_back(f);
            else fixes.push_back("dropped " + Text(f, "p") + " " + Text(f, "s") + " (no such relation)");
        }
        scene["facts"] = facts;
        // An event without an id or with a class that isn't an event's; a new entity whose
        // class doesn't exist (taken from its id: item/... is an Item); a quest stage as text.
        for (size_t i = 0; i < scene["events"].size(); ++i) {
            json& e = scene["events"][i];
            if (Text(e, "id").empty()) e["id"] = "e" + std::to_string(i + 1);
            const std::string cls = Text(e, "class");
            if (!cls.empty() && !gso_.IsA(cls, "Event")) {
                e["class"] = e.contains("agent") ? "Action" : "Event";
                fixes.push_back("event " + Text(e, "id") + ": '" + cls + "' is now " + e["class"].get<std::string>());
            }
        }
        for (auto& n : scene["newEntities"]) {
            if (gso_.Class(Text(n, "class"))) continue;
            static const std::map<std::string, std::string> kinds = {
                {"character", "Character"}, {"location", "Location"}, {"item", "Item"}, {"faction", "Faction"}};
            const std::string nid = Text(n, "id");
            const auto it = kinds.find(nid.substr(0, nid.find('/')));
            const std::string cls = it == kinds.end() ? "Item" : it->second;
            fixes.push_back(nid + ": class '" + Text(n, "class") + "' is now " + cls);
            n["class"] = cls;
        }
        if (scene.contains("questStage") && scene["questStage"].is_object()) {
            json& q = scene["questStage"];
            const json index = q.value("index", json());
            if (index.is_number()) q["index"] = static_cast<int>(std::lround(index.get<double>()));
            else if (index.is_string() && !index.get<std::string>().empty() && std::isdigit(static_cast<unsigned char>(index.get<std::string>()[0])))
                q["index"] = std::stoi(index.get<std::string>());
            else scene["questStage"] = nullptr;
        }
        if (job.attempts >= 2) {
            // The quick fix still left these: settled here, not sent back again.
            json withSubject = json::array();
            for (const auto& f : scene["facts"]) {
                if (!Text(f, "s").empty()) withSubject.push_back(f);
            }
            scene["facts"] = withSubject;
            if (Text(scene, "summary").empty()) {
                const json card = store_.Read("story/scenes/" + job.subject).value_or(json::object()).value("card", json::object());
                scene["summary"] = Text(card, "goal").empty() ? "Scene " + job.subject + "." : Text(card, "goal");
            }
        }
        scene["_fixes"] = fixes;
        const auto problems = ValidateSceneDoc(scene, gso_);
        if (HasErrors(problems)) return ProblemTexts(problems);
        return HandleScene(job, scene, rationale.empty() ? scene.value("rationale", "") : rationale);
    }
    if (job.kind == "critic") return HandleCritic(job, body, decide("critic", "story/acts/" + job.subject, ""));
    if (job.kind == "character") {
        json c = body.value("character", json::object());
        if (!c.is_object()) c = json::object();
        // Put right what a small model gets wrong, instead of sending it back: a package template
        // the engine hasn't ("patrol" -> guard), a motivation or relationship type outside the
        // vocabulary, a relationship with nobody, no name.
        const auto templates = Strings(host_.catalog.value("packageTemplates", json::array()));
        if (Text(c, "name").empty()) {
            const StoryNode* n = Graph().Find(job.subject);
            c["name"] = n && !n->Text("name").empty() ? n->Text("name") : PlanSlug(job.subject);
        }
        if (c.contains("biography") && c["biography"].is_object()) {
            json& bio = c["biography"];
            json kept = json::array();
            for (const auto& m : Strings(bio.value("motivations", json::array()))) {
                if (gso_.InVocabulary("Motivation", m)) kept.push_back(m);
            }
            bio["motivations"] = kept;
        }
        if (c.contains("relationships") && c["relationships"].is_array()) {
            json kept = json::array();
            for (auto r : c["relationships"]) {
                if (!r.is_object() || Text(r, "with").empty()) continue;
                if (!gso_.InVocabulary("RelationshipType", Text(r, "type"))) r["type"] = NearestRelationship(Text(r, "type"), r.value("valence", json()));
                kept.push_back(r);
            }
            c["relationships"] = kept;
        }
        if (!templates.empty() && c.contains("behaviour") && c["behaviour"].is_object() && c["behaviour"].value("packages", json()).is_array()) {
            static const std::vector<std::pair<const char*, const char*>> hints = {
                {"patrol", "guard"}, {"guard", "guard"}, {"watch", "guard"}, {"sentry", "guard"}, {"defend", "guard"}, {"protect", "guard"},
                {"sleep", "sleep"}, {"rest", "sleep"}, {"work", "work"}, {"craft", "work"}, {"sell", "work"}, {"trade", "work"},
                {"shop", "work"}, {"study", "work"}, {"research", "work"}, {"pray", "work"}, {"cook", "work"}, {"farm", "work"}};
            for (auto& pkg : c["behaviour"]["packages"]) {
                if (!pkg.is_object() || std::find(templates.begin(), templates.end(), Text(pkg, "template")) != templates.end()) continue;
                std::string to = Nearest(Text(pkg, "template"), hints, "wander"); // follow, accompany, roam, a script: wander
                if (std::find(templates.begin(), templates.end(), to) == templates.end()) to = templates.front();
                pkg["template"] = to;
            }
        }
        const auto problems = ValidateCharacterDoc(c, gso_, templates);
        if (HasErrors(problems)) return ProblemTexts(problems);
        return HandleCharacter(job, c, rationale.empty() ? c.value("rationale", "") : rationale);
    }
    if (job.kind == "quests" || job.kind == "quest") return HandleQuests(job, body.value("quests", json::array()), rationale);
    if (job.kind == "place") return HandlePlace(job, body.value("place", json::object()), rationale);
    if (job.kind == "systems") return HandleSystems(job, body.value("systems", json::object()), rationale);
    if (job.kind == "visuals") return HandleVisuals(job, body.value("ui", json::object()), rationale);
    if (job.kind == "revise") {
        int made = 0;
        std::vector<std::string> problems;
        for (const auto& e : body.value("edits", json::array())) {
            const std::string doc = e.value("doc", ""), pointer = e.value("pointer", "");
            if (store_.Locked(doc)) {
                problems.push_back(doc + " is locked");
                continue;
            }
            std::string reason = "AI revision: " + job.subject;
            if (e.contains("reason")) reason += " (" + e.value("reason", "") + ")";
            const std::string why = Edit(doc, pointer, e.value("value", json()), reason);
            if (why.empty()) ++made;
            else problems.push_back(why);
        }
        // The AI's suggested fix for ⑨ with nothing it could apply: nothing changes (not a failed
        // step); words from the AI panel say so (sent back once).
        const bool suggestion = job.subject.rfind("Fix these validation findings", 0) == 0;
        if (made == 0 && !problems.empty() && !suggestion) return problems;
        // An edit that couldn't be applied (a small model's edit of its request, not of a
        // document: "no places") is dropped quietly, not a warning.
        message_ = std::to_string(made) + " change(s) made for '" + job.subject + "'; " + std::to_string(Stale().size()) +
                   " document(s) to regenerate.";
        return {};
    }
    if (job.kind == "review") {
        const std::string d = decide("critic", "report", "");
        store_.Write("review", {{"schema", "aige.plan-review/1"}, {"findings", body.value("findings", json::array())}, {"rationale", rationale}}, d);
        return {};
    }
    return {};
}

// --- ② Outline ----------------------------------------------------------------------------------

std::vector<std::string> GamePlan::HandleOutline(Job&, const json& outline, const std::string& decision) {
    json doc = outline;
    doc["schema"] = "aige.outline/1";
    store_.Write("outline", doc, decision, {"brief"});
    const json brief = Brief();
    // The graph seeded: the story, its acts and endings, the cast and their roles, the places
    // and the world graph. A new outline starts the graph again.
    StoryGraph g;
    const json story = outline.value("story", json::object());
    auto node = [&](const std::string& id, const std::string& cls, json fields) {
        StoryNode& n = g.Upsert(id, cls);
        for (auto& [k, v] : fields.items()) {
            if (!v.is_null() && !(v.is_string() && v.get<std::string>().empty())) n.fields[k] = v;
        }
        n.fields["madeBy"] = decision;
    };
    auto edge = [&](const std::string& p, const std::string& s, const std::string& o, json fields = json::object()) {
        StoryEdge e;
        e.p = p;
        e.s = s;
        e.o = o;
        e.fields = std::move(fields);
        e.madeBy = decision;
        g.edges.push_back(std::move(e));
    };
    const auto tones = brief.value("tone", json::array());
    node("story", "Story", {{"logline", story.value("logline", "")}, {"synopsis", story.value("synopsis", "")},
                            {"structure", story.value("structure", brief.value("structure", "three-act"))},
                            {"pattern", brief.value("pattern", "linear")},
                            {"tone", tones.is_array() && !tones.empty() ? tones[0] : json()}});
    int order = 0;
    for (const auto& act : story.value("acts", json::array())) {
        node(act.value("id", ""), "Act", {{"order", ++order}, {"purpose", act.value("purpose", "")}});
    }
    for (const auto& line : story.value("storylines", json::array())) {
        node("storyline/" + PlanSlug(line.value("id", "main")), "Storyline", {{"kind", line.value("kind", "main")}});
    }
    for (const auto& ending : story.value("endings", json::array())) {
        node("ending/" + PlanSlug(ending.value("id", "end")), "Ending", {{"epilogue", ending.value("summary", "")}, {"conditions", ending.value("conditions", "")}});
    }
    for (const auto& f : outline.value("factions", json::array())) {
        node(f.value("id", ""), "Faction", {{"name", f.value("name", "")}});
        WorldStub(f.value("id", ""), f.value("name", ""), f.value("line", ""));
    }
    for (const auto& c : outline.value("cast", json::array())) {
        const std::string id = c.value("id", "");
        node(id, c.value("player", false) ? "PlayerCharacter" : "Character",
             {{"name", c.value("name", "")}, {"importance", c.value("importance", "minor")}, {"summary", c.value("line", "")}});
        for (const auto& r : c.value("narrativeRoles", json::array())) {
            const std::string role = "role/" + r.get<std::string>();
            node(role, "NarrativeRole", {{"archetype", r}});
            edge("fills", id, role);
        }
        for (const auto& r : c.value("gameplayRoles", json::array())) {
            const std::string role = "grole/" + r.get<std::string>();
            node(role, "GameplayRole", {{"role", r}});
            edge("fills", id, role);
        }
        if (!c.value("faction", "").empty() && g.Find(c.value("faction", ""))) edge("memberOf", id, c.value("faction", ""));
        WorldStub(id, c.value("name", ""), c.value("line", ""));
    }
    bool first = true;
    for (const auto& p : outline.value("places", json::array())) {
        const std::string id = p.value("id", "");
        node(id, "Location", {{"name", p.value("name", PlanSlug(id))}, {"buildType", p.value("kind", "")}, {"summary", p.value("line", "")}});
        if (first) g.Find(id)->fields["start"] = true;
        first = false;
        WorldStub(id, p.value("name", ""), p.value("line", ""));
    }
    for (const auto& p : outline.value("places", json::array())) {
        if (!p.value("parent", "").empty()) edge("inside", p.value("id", ""), p.value("parent", ""));
    }
    for (const auto& c : outline.value("connections", json::array())) {
        json fields = {{"kind", c.value("kind", "road")}};
        if (c.contains("gate") && !c["gate"].is_null()) fields["gate"] = c["gate"].is_string() ? c["gate"] : json(c["gate"].dump());
        edge("connects", c.value("from", ""), c.value("to", ""), fields);
    }
    g.revision = 1;
    SaveGraph(g);
    return {};
}

// --- ③ Story -------------------------------------------------------------------------------------

std::vector<std::string> GamePlan::HandleAct(Job& job, const json& act, const std::string& decision) {
    const std::string actId = job.subject;
    const auto acts = Outline().value("story", json::object()).value("acts", json::array());
    int actIndex = 1;
    std::string actSummary;
    for (size_t i = 0; i < acts.size(); ++i) {
        if (acts[i].value("id", "") == actId) {
            actIndex = static_cast<int>(i) + 1;
            actSummary = acts[i].value("summary", "");
        }
    }
    StoryGraph g = Graph();
    // A planned-again act replaces its scenes.
    for (const auto& doc : store_.List("story/scenes")) {
        const json s = store_.Read(doc).value_or(json::object());
        if (s.value("act", "") != actId) continue;
        RemoveMadeBy(g, s.value("madeBy", ""), gso_, true);
        g.RemoveNode(PlanSlug(doc));
        store_.Remove(doc);
    }
    json chapters = json::array();
    struct Card {
        std::string id, chapter;
        json card;
    };
    std::vector<Card> cards;
    int k = 0, c = 0;
    for (const auto& ch : act.value("chapters", json::array())) {
        const std::string chapterId = "ch-" + std::to_string(actIndex) + "-" + std::to_string(++c);
        json ids = json::array();
        for (const auto& card : ch.value("scenes", json::array())) {
            char id[16];
            std::snprintf(id, sizeof(id), "s%d-%02d", actIndex, ++k);
            ids.push_back(id);
            cards.push_back({id, chapterId, card});
        }
        chapters.push_back({{"id", chapterId}, {"question", ch.value("question", "")}, {"summary", ch.value("summary", "")}, {"scenes", ids}});
        StoryNode& n = g.Upsert(chapterId, "Chapter");
        n.fields = {{"question", ch.value("question", "")}, {"order", actIndex * 100 + c}, {"madeBy", decision}};
    }
    store_.Write("story/acts/" + actId,
                 {{"schema", "aige.story/1"}, {"id", actId}, {"summary", actSummary}, {"chapters", chapters}, {"rationale", act.value("rationale", "")}},
                 decision, {"outline"});
    for (size_t i = 0; i < cards.size(); ++i) {
        json card = cards[i].card;
        card["participants"] = Participants(card);
        const double order = actIndex * 100 + static_cast<double>(i) + 1;
        // Who and where the scene needs that the outline lacks: stubs, for later stages to detail.
        for (const auto& p : card["participants"]) {
            const std::string who = p.value("who", "");
            if (!who.empty() && !g.Find(who)) {
                StoryNode& n = g.Upsert(who.find('/') == std::string::npos ? "character/" + who : who, "Character");
                n.fields = {{"name", PlanSlug(who)}, {"importance", "minor"}, {"madeBy", decision}, {"stubFrom", cards[i].id}};
                WorldStub(n.id, PlanSlug(who), "");
            }
        }
        const std::string loc = card.value("location", "");
        if (!loc.empty() && !g.Find(loc)) {
            StoryNode& n = g.Upsert(loc, "Location");
            n.fields = {{"name", PlanSlug(loc)}, {"buildType", "interior"}, {"madeBy", decision}, {"stubFrom", cards[i].id}};
            WorldStub(loc, PlanSlug(loc), "");
        }
        StoryNode& n = g.Upsert(cards[i].id, "StoryScene");
        n.fields = {{"order", order}, {"act", actId}, {"chapter", cards[i].chapter}, {"mode", card.value("mode", "gameplay")}, {"madeBy", decision}};
        for (const char* key : {"kernel", "goal", "conflict", "outcome", "value", "chargeBefore", "chargeAfter", "location", "branch", "needsParts"}) {
            if (card.contains(key) && !card[key].is_null() && !(card[key].is_string() && card[key].get<std::string>().empty())) n.fields[key] = card[key];
        }
        const std::string beat = card.value("beat", "");
        if (beat == "climax") n.fields["climax"] = true;
        if (beat == "inciting-incident" || beat == "catalyst" || beat == "call-to-adventure") n.fields["inciting"] = true;
        store_.Write("story/scenes/" + cards[i].id,
                     {{"schema", "aige.scene/1"}, {"id", cards[i].id}, {"act", actId}, {"chapter", cards[i].chapter}, {"order", order},
                      {"branch", card.value("branch", "")}, {"card", card}, {"status", "card"}},
                     decision, {"story/acts/" + actId});
    }
    // The scenes are written in order, one after another, across acts.
    std::string last;
    for (const auto& j : jobs_) {
        if (j.kind == "scene") last = j.id;
    }
    for (const auto& card : cards) last = AddJob("scene", card.id, last.empty() ? std::vector<std::string>{} : std::vector<std::string>{last});
    if (!cards.empty()) AddJob("critic", actId, {last});
    // Every act planned: the discourse links (next, choices, convergences, endings).
    const bool lastAct = std::all_of(jobs_.begin(), jobs_.end(), [&](const Job& j) { return j.kind != "act" || j.id == job.id || j.status == "done" || j.status == "failed"; });
    if (lastAct) LinkDiscourse(g);
    SaveGraph(g);
    return {};
}

void GamePlan::UndoScene(const std::string& scene) {
    const std::string doc = "story/scenes/" + scene;
    const json s = store_.Read(doc).value_or(json::object());
    if (s.value("status", "") != "written") return;
    StoryGraph g = Graph();
    const std::string decision = s.value("madeBy", "");
    RemoveMadeBy(g, decision, gso_, true);
    // Facts it ended hold again.
    for (const auto& end : s.value("end", json::array())) {
        const std::string at = end.value("at", scene);
        for (auto& e : g.edges) {
            if (e.p == end.value("p", "") && e.s == end.value("s", "") && (end.value("o", "").empty() || e.o == end.value("o", "")) && e.until == at) e.until.clear();
        }
    }
    SaveGraph(g);
    // Its quest stage leaves the main quest's skeleton.
    if (auto q = store_.Read("quests/q-main")) {
        json stages = json::array();
        for (const auto& st : q->value("stages", json::array())) {
            if (st.value("advancesScene", "") != scene) stages.push_back(st);
        }
        if (stages.size() != q->value("stages", json::array()).size()) {
            (*q)["stages"] = stages;
            store_.Touch("quests/q-main", *q, q->value("madeBy", ""));
        }
    }
}


std::vector<std::string> GamePlan::HandleScene(Job& job, const json& answer, const std::string& rationale) {
    const json& scene = answer; // cleaned by Handle (CleanSceneAnswer)
    const std::string id = job.subject;
    const std::string docName = "story/scenes/" + id;
    json doc = store_.Read(docName).value_or(json::object());
    const json card = doc.value("card", json::object());
    const std::string branch = card.value("branch", "");
    StoryGraph g = Graph();
    const auto before = CheckStory(g, gso_);
    json nodes = json::array(), facts = json::array(), ends = json::array();
    // New characters, places and items the scene needs: stubs.
    for (const auto& n : scene.value("newEntities", json::array())) {
        json node = {{"id", n.value("id", "")}, {"class", n.value("class", "Character")}, {"name", n.value("name", PlanSlug(n.value("id", "")))},
                     {"summary", n.value("line", "")}, {"madeBy", kPending}, {"stubFrom", id}};
        if (n.value("class", "") == "Character") node["importance"] = n.value("importance", "minor");
        if (n.value("class", "") == "Location") node["buildType"] = n.value("kind", "interior");
        if (g.Find(n.value("id", ""))) continue; // already there: the scene may name it
        nodes.push_back(node);
    }
    // Its events, presented, with their causes.
    std::map<std::string, std::string> eventIds;
    for (const auto& e : scene.value("events", json::array())) eventIds[e.value("id", "")] = id + "/" + e.value("id", "");
    auto eventRef = [&](const std::string& ref) {
        auto it = eventIds.find(ref);
        return it != eventIds.end() ? it->second : ref;
    };
    for (const auto& e : scene.value("events", json::array())) {
        const std::string eid = eventIds[e.value("id", "")];
        json node = {{"id", eid}, {"class", e.value("class", "Event").empty() ? "Event" : e.value("class", "Event")}, {"text", e.value("text", "")},
                     {"madeBy", kPending}};
        for (const char* k : {"kernel", "inciting", "setup", "openEnded", "agent"}) {
            if (e.contains(k)) node[k] = e[k];
        }
        nodes.push_back(node);
        facts.push_back({{"p", "presents"}, {"s", id}, {"o", eid}, {"madeBy", kPending}});
        for (const auto& c : e.value("causes", json::array())) facts.push_back({{"p", "causes"}, {"s", eid}, {"o", eventRef(c.get<std::string>())}, {"madeBy", kPending}});
        for (const auto& c : e.value("enables", json::array())) facts.push_back({{"p", "enables"}, {"s", eid}, {"o", eventRef(c.get<std::string>())}, {"madeBy", kPending}});
        if (!e.value("serves", "").empty()) {
            const std::string goal = e.value("serves", "").rfind("goal/", 0) == 0 ? e.value("serves", "") : "goal/" + e.value("serves", "");
            if (!g.Find(goal)) nodes.push_back({{"id", goal}, {"class", "Goal"}, {"kind", PlanSlug(goal)}, {"madeBy", kPending}});
            facts.push_back({{"p", "serves"}, {"s", eid}, {"o", goal}, {"madeBy", kPending}});
        }
        if (!e.value("resolves", "").empty()) facts.push_back({{"p", "resolves"}, {"s", eid}, {"o", e.value("resolves", "")}, {"madeBy", kPending}});
        if (!e.value("paysOff", "").empty()) facts.push_back({{"p", "paysOff"}, {"s", eid}, {"o", eventRef(e.value("paysOff", ""))}, {"madeBy", kPending}});
    }
    // Who takes part.
    for (const auto& p : Participants(card)) {
        std::string who = p.value("who", "");
        if (who.find('/') == std::string::npos) who = "character/" + who;
        facts.push_back({{"p", "participant"}, {"s", id}, {"o", who}, {"role", p.value("role", "agent")}, {"madeBy", kPending}});
    }
    // The facts it asserts: temporal ones start here, on its branch, unless it says otherwise.
    for (auto f : scene.value("facts", json::array())) {
        const auto* p = gso_.Property(f.value("p", ""));
        if (p && p->temporal) {
            if (!f.contains("from")) f["from"] = id;
            if (!f.contains("branch") && !branch.empty()) f["branch"] = branch;
        }
        f["madeBy"] = kPending;
        facts.push_back(f);
    }
    for (auto e : scene.value("end", json::array())) {
        if (!e.contains("at")) e["at"] = id;
        ends.push_back(e);
    }
    // What it names that the story doesn't have. A character, place, item or faction is added
    // as a stub (the scene brings it in, like its newEntities); a link to anything else that
    // isn't there (an earlier event by a made-up name, a conflict never set up) is dropped,
    // and so is the end of a fact that was never true. Not a failure: noted as a fix.
    std::vector<std::string> fixes;
    for (const auto& f : scene.value("_fixes", json::array())) {
        if (f.is_string()) fixes.push_back(f.get<std::string>());
    }
    {
        std::map<std::string, std::string> added; // id -> class, of the nodes this scene adds
        for (const auto& n : nodes) added[Text(n, "id")] = Text(n, "class");
        auto known = [&](const std::string& ref) { return ref.empty() || g.Find(ref) || added.contains(ref) || ref == id; };
        auto classOf = [&](const std::string& ref) -> std::string {
            if (ref == id) return "StoryScene";
            if (const StoryNode* n = g.Find(ref)) return n->cls;
            const auto it = added.find(ref);
            return it == added.end() ? std::string() : it->second;
        };
        // A link to something real but of the wrong kind (paysOff → a scene, not an event).
        auto fits = [&](const std::string& ref, const std::vector<std::string>& classes) {
            if (ref.empty() || classes.empty()) return true;
            const std::string cls = classOf(ref);
            return cls.empty() || std::any_of(classes.begin(), classes.end(), [&](const std::string& c) { return gso_.IsA(cls, c); });
        };
        auto stubClass = [](const std::string& ref) -> std::string {
            static const std::map<std::string, std::string> kinds = {
                {"character", "Character"}, {"location", "Location"}, {"item", "Item"}, {"faction", "Faction"}, {"goal", "Goal"}};
            const auto slash = ref.find('/');
            if (slash == std::string::npos) return {};
            const auto it = kinds.find(ref.substr(0, slash));
            return it == kinds.end() ? std::string() : it->second;
        };
        json kept = json::array();
        for (auto f : facts) {
            bool ok = true;
            for (const char* key : {"s", "o"}) {
                std::string ref = Text(f, key);
                // "goal:seek-amulet" is goal/seek-amulet.
                if (const auto colon = ref.find(':'); colon != std::string::npos && ref.find('/') == std::string::npos) {
                    ref[colon] = '/';
                    f[key] = ref;
                }
                if (known(ref)) continue;
                const std::string cls = stubClass(ref);
                if (cls.empty()) {
                    fixes.push_back("dropped " + Text(f, "p") + " → " + ref + " (not in the story)");
                    ok = false;
                    break;
                }
                std::string name = PlanSlug(ref);
                std::replace(name.begin(), name.end(), '-', ' ');
                if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
                json node = {{"id", ref}, {"class", cls}, {"name", name}, {"madeBy", kPending}, {"stubFrom", id}};
                if (cls == "Character") node["importance"] = "minor";
                if (cls == "Location") node["buildType"] = "interior";
                if (cls == "Goal") node = {{"id", ref}, {"class", "Goal"}, {"kind", PlanSlug(ref)}, {"madeBy", kPending}};
                nodes.push_back(node);
                added[ref] = cls;
                fixes.push_back("added " + ref + " (the scene names it)");
            }
            if (ok) {
                if (const auto* property = gso_.Property(Text(f, "p"));
                    property && (!fits(Text(f, "s"), property->domain) || !fits(Text(f, "o"), property->range))) {
                    fixes.push_back("dropped " + Text(f, "p") + " " + Text(f, "s") + " → " + Text(f, "o") + " (the wrong kind of thing)");
                    ok = false;
                }
            }
            if (ok) kept.push_back(f);
        }
        facts = kept;
        // A fact's "from" / "until" (and an end's "at") name a scene: an event id, a place or
        // "earlier" there means this scene (from) or nothing (until).
        auto isScene = [&](const std::string& ref) { return ref == id || g.OrderOf(ref) >= 0; };
        for (auto& f : facts) {
            for (const char* key : {"from", "until"}) {
                const std::string at = Text(f, key);
                if (at.empty() || isScene(at)) continue;
                fixes.push_back(Text(f, "p") + " " + Text(f, "s") + ": " + key + " '" + at + "' isn't a scene");
                if (std::string(key) == "from") f["from"] = id;
                else f.erase("until");
            }
        }
        for (auto& e : ends) {
            if (!isScene(Text(e, "at"))) e["at"] = id;
        }
        // Moving is leaving: a character put somewhere here is no longer where it was (one
        // place at a time, R-T2); of two places given here, the last one counts.
        {
            std::map<std::string, size_t> lastPlace;
            for (size_t i = 0; i < facts.size(); ++i) {
                if (Text(facts[i], "p") == "locatedAt") lastPlace[Text(facts[i], "s")] = i;
            }
            json placedFacts = json::array();
            for (size_t i = 0; i < facts.size(); ++i) {
                const bool place = Text(facts[i], "p") == "locatedAt";
                if (place && lastPlace[Text(facts[i], "s")] != i) continue;
                placedFacts.push_back(facts[i]);
            }
            facts = placedFacts;
            // A scene moves someone now, not in an earlier scene ("from": s1-03 in s1-04).
            for (auto& f : facts) {
                const std::string from = Text(f, "from");
                if (Text(f, "p") == "locatedAt" && !from.empty() && from != id && g.OrderOf(from) < g.OrderOf(id)) f["from"] = id;
            }
            std::set<std::pair<std::string, std::string>> left;
            for (const auto& f : facts) {
                if (Text(f, "p") != "locatedAt") continue;
                const std::string who = Text(f, "s"), where = Text(f, "o");
                for (const auto& x : g.edges) {
                    if (x.p != "locatedAt" || x.s != who || x.o == where || !x.until.empty()) continue;
                    if (!left.insert({who, x.o}).second) continue;
                    ends.push_back({{"p", "locatedAt"}, {"s", who}, {"o", x.o}, {"at", id}});
                    fixes.push_back(who + " leaves " + x.o + " (now at " + where + ")");
                }
            }
        }
        // Speaking of what one doesn't know yet (R-T3): one learns it here. A relationship type
        // that isn't in the vocabulary: the nearest that is (by its words, else its valence).
        {
            json learned = json::array();
            for (auto& f : facts) {
                const std::string p = Text(f, "p");
                if (p == "mentions") {
                    if (!isScene(Text(f, "scene"))) f["scene"] = id;
                    const std::string who = Text(f, "s"), about = Text(f, "o").empty() ? Text(f, "about") : Text(f, "o");
                    if (about.empty() || g.KnowsAt(who, about, Text(f, "scene"))) continue;
                    const bool toldHere = std::any_of(facts.begin(), facts.end(), [&](const json& k) {
                        return Text(k, "p") == "knows" && Text(k, "s") == who && (Text(k, "about") == about || Text(k, "o") == about);
                    });
                    if (toldHere) continue;
                    learned.push_back({{"p", "knows"}, {"s", who}, {"about", about}, {"from", Text(f, "scene")}, {"madeBy", kPending}});
                    fixes.push_back(who + " learns '" + about + "' here (it's mentioned)");
                } else if (p == "hasRelationship" && f.contains("type") && !gso_.InVocabulary("RelationshipType", Text(f, "type"))) {
                    const std::string chosen = NearestRelationship(Text(f, "type"), f.value("valence", json()));
                    fixes.push_back(Text(f, "s") + " → " + Text(f, "o") + ": relationship '" + Text(f, "type") + "' is now '" + chosen + "'");
                    f["type"] = chosen;
                }
            }
            for (const auto& k : learned) facts.push_back(k);
        }
        json keptEnds = json::array();
        for (auto e : ends) {
            const std::string p = Text(e, "p"), s = Text(e, "s"), o = Text(e, "o");
            const bool held = std::any_of(g.edges.begin(), g.edges.end(),
                                          [&](const StoryEdge& x) { return x.p == p && x.s == s && (o.empty() || x.o == o); });
            // The relationship ends whatever type the answer gave it ("rival" for one held as "trusts").
            const std::string type = Text(e, "type");
            if (held && !type.empty() && std::none_of(g.edges.begin(), g.edges.end(), [&](const StoryEdge& x) {
                    return x.p == p && x.s == s && (o.empty() || x.o == o) && x.Text("type") == type;
                })) {
                e.erase("type");
            }
            if (held) keptEnds.push_back(e);
            else fixes.push_back("dropped the end of " + p + " " + s + (o.empty() ? "" : " → " + o) + " (it was never true)");
        }
        ends = keptEnds;
    }
    // What a change breaks: the continuity rules on what it added.
    auto breaks = [&](const StoryGraph& candidate) {
        std::vector<std::string> out;
        for (const auto& i : NewErrors(before, CheckStory(candidate, gso_))) {
            if (i.rule.rfind("R-T", 0) == 0 || i.rule == "R-K3" || i.rule == "shape") out.push_back(i.rule + ": " + i.message);
        }
        return out;
    };
    StoryGraph next = g;
    if (StoryNode* n = next.Find(id)) n->fields["summary"] = scene.value("summary", "");
    std::string problem = next.Apply({{"nodes", nodes}, {"assert", facts}, {"retract", ends}}, gso_);
    std::vector<std::string> problems = problem.empty() ? breaks(next) : std::vector<std::string>{problem};
    if (!problems.empty() && job.attempts < 2) return problems; // first: a quick fix of the answer (the AI)
    if (!problems.empty()) {
        // The fixed answer still doesn't fit: the scene is written with what fits. Each new
        // entity, fact and end is kept only if the story stays sound with it; what isn't is
        // left out, on record. Quietly: the scene is done, not failed.
        next = g;
        if (StoryNode* n = next.Find(id)) n->fields["summary"] = scene.value("summary", "");
        auto fits = [&](const char* list, const json& item) {
            StoryGraph trial = next;
            if (!trial.Apply({{list, json::array({item})}}, gso_).empty() || !breaks(trial).empty()) return false;
            next = std::move(trial);
            return true;
        };
        for (const auto& n : nodes) {
            if (!fits("nodes", n)) fixes.push_back("left out " + Text(n, "id") + " (it didn't fit the story)");
        }
        json keptFacts = json::array();
        for (const auto& f : facts) {
            if (fits("assert", f)) keptFacts.push_back(f);
            else fixes.push_back("left out " + Text(f, "p") + " " + Text(f, "s") + (Text(f, "o").empty() ? "" : " → " + Text(f, "o")) +
                                 " (it contradicts the story)");
        }
        facts = keptFacts;
        json keptEnds = json::array();
        for (const auto& e : ends) {
            if (fits("retract", e)) keptEnds.push_back(e);
            else fixes.push_back("left out the end of " + Text(e, "p") + " " + Text(e, "s"));
        }
        ends = keptEnds;
    }

    if (!fixes.empty()) {
        PlanDecision fix;
        fix.stage = "story";
        fix.actor = "rule";
        fix.kind = "fix";
        fix.subject = docName;
        for (const auto& f : fixes) fix.chosen += (fix.chosen.empty() ? "" : "; ") + f;
        fix.rationale = "What the scene named that the story doesn't have was added as a stub or left out.";
        store_.Decide(fix);
    }
    PlanDecision d;
    d.stage = "story";
    d.actor = "ai";
    d.kind = "write-scene";
    d.subject = docName;
    d.chosen = scene.value("summary", "");
    d.rationale = rationale;
    d.inputs = {{"story/acts/" + doc.value("act", ""), store_.RevisionOf("story/acts/" + doc.value("act", ""))}, {"kg", store_.RevisionOf("kg")}};
    d.checks = {{"facts", facts.size()}, {"repaired", job.attempts > 1}};
    const std::string decision = store_.Decide(d);
    StampMadeBy(next, decision);

    // The main quest's skeleton: a stage for each kernel gameplay scene on the main line.
    json questStage = scene.value("questStage", json());
    const bool kernel = card.value("kernel", json()).is_boolean() && card["kernel"].get<bool>();
    if (!questStage.is_object() && kernel && Text(card, "mode") == "gameplay" && branch.empty()) {
        questStage = {{"quest", "q-main"}, {"journal", card.value("goal", scene.value("summary", ""))}};
    }
    if (questStage.is_object() && branch.empty()) {
        json q = store_.Read("quests/q-main").value_or(json{{"schema", "aige.quest-design/1"}, {"id", "q-main"}, {"type", "main"},
                                                          {"name", Brief().value("title", "The main quest")}, {"stages", json::array()}});
        int last = 0;
        if (!q.contains("stages") || !q["stages"].is_array()) q["stages"] = json::array();
        for (const auto& st : q["stages"]) {
            if (st.is_object() && st.value("index", json()).is_number()) last = std::max(last, static_cast<int>(st["index"].get<double>()));
        }
        const int index = questStage.value("index", json()).is_number_integer() && questStage["index"].get<int>() > last
                              ? questStage["index"].get<int>()
                              : last + 10;
        q["stages"].push_back({{"index", index}, {"journal", questStage.value("journal", "")}, {"advancesScene", id}});
        store_.Write("quests/q-main", q, decision, {});
        StoryNode& quest = next.Upsert("q-main", "Quest");
        quest.fields["type"] = "main";
        if (!quest.fields.contains("madeBy")) quest.fields["madeBy"] = decision;
        const std::string stageId = "q-main/" + std::to_string(index);
        StoryNode& st = next.Upsert(stageId, "QuestStage");
        st.fields = {{"index", index}, {"journal", questStage.value("journal", "")}, {"quest", "q-main"}, {"madeBy", decision}};
        for (const char* p : {"hasStage", "advancesScene"}) {
            StoryEdge e;
            e.p = p;
            e.s = std::string(p) == "hasStage" ? "q-main" : stageId;
            e.o = std::string(p) == "hasStage" ? stageId : id;
            e.madeBy = decision;
            next.edges.push_back(std::move(e));
        }
        questStage["index"] = index;
    }
    for (const auto& n : scene.value("newEntities", json::array())) WorldStub(n.value("id", ""), n.value("name", ""), n.value("line", ""));
    SaveGraph(next);
    doc["beats"] = scene.value("beats", json::array());
    doc["events"] = scene.value("events", json::array());
    doc["facts"] = scene.value("facts", json::array());
    doc["end"] = ends;
    doc["newEntities"] = scene.value("newEntities", json::array());
    doc["questStage"] = questStage;
    doc["summary"] = scene.value("summary", "");
    doc["rationale"] = rationale;
    doc["status"] = "written";
    doc.erase("needsRewrite");
    store_.Write(docName, doc, decision, {"story/acts/" + doc.value("act", "")});
    return {};
}

std::vector<std::string> GamePlan::HandleCritic(Job& job, const json& answer, const std::string& decision) {
    for (const auto& f : answer.value("findings", json::array())) {
        const std::string scene = f.value("scene", "");
        const std::string doc = "story/scenes/" + scene;
        auto s = store_.Read(doc);
        const json fix = f.value("fix", json::object());
        if (!s || store_.Locked(doc) || !fix.is_object() || fix.empty() || s->value("criticRewrites", 0) >= 1) {
            Finding("story", scene.empty() ? job.subject : scene, f.value("problem", ""), "warning");
            continue;
        }
        // The card changed as the critic proposes; the scene is written again (once).
        for (const auto& [k, v] : fix.items()) (*s)["card"][k] = v;
        (*s)["criticRewrites"] = s->value("criticRewrites", 0) + 1;
        (*s)["criticNote"] = f.value("problem", "");
        store_.Touch(doc, *s, decision);
        // Corrected where the review says (its card changed as proposed), not written again.
        QuickFix(doc, {"The act's review: " + f.value("problem", "") + " (the scene's card now reads as proposed: " + fix.dump() + ")"});
    }
    return {};
}

// --- ④ Characters --------------------------------------------------------------------------------

std::vector<std::string> GamePlan::HandleCharacter(Job& job, const json& answer, const std::string& rationale) {
    const std::string id = job.subject;
    const std::string docName = "characters/" + PlanSlug(id);
    StoryGraph g = Graph();
    if (auto old = store_.Read(docName)) RemoveMadeBy(g, old->value("madeBy", ""), gso_, false);
    json c = answer;
    c["schema"] = "aige.character/1";
    c["id"] = id;
    const StoryNode* existing = g.Find(id);
    if (!c.contains("importance") && existing) c["importance"] = existing->Text("importance");
    PlanDecision d;
    d.stage = "characters";
    d.actor = "ai";
    d.kind = "detail-character";
    d.subject = docName;
    d.chosen = c.value("name", "");
    d.rationale = rationale;
    d.inputs = {{"outline", store_.RevisionOf("outline")}, {"kg", store_.RevisionOf("kg")}};
    const std::string decision = store_.Decide(d);
    if (StoryNode* n = g.Find(id)) {
        const json bio = c.value("biography", json::object());
        if (!c.value("name", "").empty()) n->fields["name"] = c["name"];
        for (const char* k : {"need", "desire", "weakness"}) {
            if (!bio.value(k, "").empty()) n->fields[k] = bio[k];
        }
        if (bio.contains("motivations")) n->fields["motivations"] = bio["motivations"];
        if (bio.contains("personality")) n->fields["traits"] = bio["personality"];
        const json arc = bio.value("arc", json());
        if (arc.is_object() && (!arc.value("from", "").empty() || !arc.value("to", "").empty())) n->fields["arc"] = arc.value("from", "") + " → " + arc.value("to", "");
        else if (arc.is_string() && !arc.get<std::string>().empty()) n->fields["arc"] = arc;
        if (bio.value("static", false) || n->cls == "PlayerCharacter") n->fields["static"] = true;
        const json gameplay = c.value("gameplay", json::object());
        if (!gameplay.value("class", "").empty()) n->fields["statClass"] = gameplay["class"];
        if (!gameplay.value("stock", "").empty()) n->fields["stock"] = gameplay["stock"];
        if (!gameplay.value("skills", "").empty()) n->fields["skills"] = gameplay["skills"];
        const json behaviour = c.value("behaviour", json::object());
        std::string packages;
        for (const auto& p : behaviour.value("packages", json::array())) packages += (packages.empty() ? "" : ", ") + p.value("template", "");
        if (!packages.empty()) n->fields["package"] = PlanSlug(id) + ": " + packages;
        if (behaviour.value("idle", false)) n->fields["idle"] = true;
        if (!c.value("line", "").empty()) n->fields["summary"] = c["line"];
        // Where it lives, if the place exists.
        const std::string home = c.value("home", "");
        if (!home.empty() && g.Find(home) && g.Out(id, "locatedAt").empty()) {
            StoryEdge e;
            e.p = "locatedAt";
            e.s = id;
            e.o = home;
            e.madeBy = decision;
            g.edges.push_back(std::move(e));
        }
    }
    // Relationships, both ways for the ones that have an inverse (R-K3).
    for (const auto& r : c.value("relationships", json::array())) {
        const std::string with = r.value("with", ""), type = r.value("type", "");
        if (!g.Find(with)) {
            Finding("characters", id, "a relationship with " + with + ", who isn't in the story, was left out");
            continue;
        }
        auto add = [&](const std::string& s, const std::string& o, const std::string& t) {
            const auto out = g.Out(s, "hasRelationship");
            if (std::any_of(out.begin(), out.end(), [&](const StoryEdge* e) { return e->o == o && e->Text("type") == t; })) return;
            StoryEdge e;
            e.p = "hasRelationship";
            e.s = s;
            e.o = o;
            e.fields = {{"type", t}, {"valence", r.value("valence", 0.0)}};
            if (r.value("secret", false)) e.fields["secret"] = true;
            e.madeBy = decision;
            g.edges.push_back(std::move(e));
        };
        add(id, with, type);
        if (auto inv = gso_.inverse.find(type); inv != gso_.inverse.end()) add(with, id, inv->second);
    }
    SaveGraph(g);
    const json bio = c.value("biography", json::object());
    std::string body = bio.value("backstory", "");
    if (!c.value("voice", json::object()).value("description", "").empty()) body += "\n\nVoice: " + c["voice"].value("description", "");
    WorldStub(id, c.value("name", ""), c.value("line", ""), body);
    std::vector<std::string> inputs = {"outline"};
    for (const auto& a : store_.List("story/acts")) inputs.push_back(a);
    c["rationale"] = rationale;
    store_.Write(docName, c, decision, inputs);
    return {};
}

// --- ⑤ Quests --------------------------------------------------------------------------------------

// A quest as a small model writes it, put right before it's checked: an alias naming what the
// story lacks gets a stub (an item, character or place), its kind from what it names, and a
// query if it names nothing; stage numbers that don't rise are renumbered; objective actions
// outside the vocabulary map to the nearest, targets that aren't aliases go; a giver that isn't
// one of the aliases becomes the first character alias.
void GamePlan::RepairQuest(json& q, StoryGraph& g, std::vector<std::string>& known, std::vector<std::string>& fixes) const {
    const std::string qid = Text(q, "id");
    if (Text(q, "name").empty()) {
        std::string name = PlanSlug(qid.empty() ? std::string("quest") : qid);
        std::replace(name.begin(), name.end(), '-', ' ');
        if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        q["name"] = name;
    }
    if (!Text(q, "motivation").empty() && !gso_.InVocabulary("Motivation", Text(q, "motivation"))) q.erase("motivation");
    static const std::vector<std::string> kinds = {"character", "item", "location", "spot", "container"};
    static const std::map<std::string, std::string> classes = {{"character", "Character"}, {"item", "Item"}, {"location", "Location"}, {"faction", "Faction"}};
    auto stub = [&](const std::string& ref) {
        if (ref.empty() || std::find(known.begin(), known.end(), ref) != known.end()) return true;
        const auto slash = ref.find('/');
        const auto cls = slash == std::string::npos ? classes.end() : classes.find(ref.substr(0, slash));
        if (cls == classes.end()) return false;
        std::string name = PlanSlug(ref);
        std::replace(name.begin(), name.end(), '-', ' ');
        if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        StoryNode& n = g.Upsert(ref, cls->second);
        n.fields = {{"name", name}, {"madeBy", kPending}, {"stubFrom", qid}};
        if (cls->second == "Character") n.fields["importance"] = "minor";
        if (cls->second == "Location") n.fields["buildType"] = "interior";
        known.push_back(ref);
        fixes.push_back("added " + ref + " (" + qid + " names it)");
        return true;
    };
    json aliases = json::array();
    std::set<std::string> ids;
    for (auto a : q.value("aliases", json::array())) {
        if (!a.is_object()) continue;
        std::string id = Text(a, "id");
        if (id.empty() || ids.contains(id)) id = "alias-" + std::to_string(aliases.size() + 1);
        a["id"] = id;
        ids.insert(id);
        for (const char* key : {"fill", "in"}) {
            if (a.contains(key) && !a[key].is_string()) a.erase(key);
            if (!stub(Text(a, key))) {
                fixes.push_back(qid + ": alias " + id + " " + key + " " + Text(a, key) + " isn't in the story");
                a.erase(key);
            }
        }
        if (std::find(kinds.begin(), kinds.end(), Text(a, "kind")) == kinds.end()) {
            const std::string fill = Text(a, "fill");
            const std::string prefix = fill.substr(0, fill.find('/'));
            a["kind"] = std::find(kinds.begin(), kinds.end(), prefix) != kinds.end() ? prefix : !Text(a, "in").empty() ? "spot" : "item";
        }
        if (Text(a, "fill").empty() && Text(a, "query").empty() && Text(a, "in").empty()) {
            std::string query = id;
            std::replace(query.begin(), query.end(), '-', ' ');
            a["query"] = query; // found by what it's called
        }
        aliases.push_back(a);
    }
    q["aliases"] = aliases;
    static const std::vector<std::pair<const char*, const char*>> hints = {
        {"go", "goto"}, {"travel", "goto"}, {"find", "reach"}, {"reach", "reach"}, {"speak", "talk"}, {"talk", "talk"}, {"ask", "talk"},
        {"slay", "kill"}, {"kill", "kill"}, {"defeat", "kill"}, {"fight", "kill"}, {"get", "collect"}, {"take", "collect"},
        {"retrieve", "collect"}, {"steal", "collect"}, {"find", "collect"}, {"bring", "deliver"}, {"give", "deliver"}, {"return", "deliver"},
        {"protect", "defend"}, {"guard", "defend"}, {"inspect", "examine"}, {"search", "examine"}, {"investigate", "examine"},
        {"sneak", "stealth"}, {"rescue", "free"}, {"save", "free"}, {"make", "craft"}, {"decide", "choose"}, {"open", "use"}};
    int last = 0;
    json stages = json::array();
    for (auto st : q.value("stages", json::array())) {
        if (!st.is_object()) continue;
        const json index = st.value("index", json());
        if (!index.is_number_integer() || index.get<int>() <= last) {
            st["index"] = last + 10;
            fixes.push_back(qid + ": a stage numbered " + index.dump() + " is now " + std::to_string(last + 10));
        }
        last = st["index"].get<int>();
        json objectives = json::array();
        for (auto o : st.value("objectives", json::array())) {
            if (!o.is_object()) continue;
            if (!gso_.InVocabulary("ObjectiveAction", Text(o, "action"))) o["action"] = Nearest(Text(o, "action"), hints, "interact");
            if (!Text(o, "target").empty() && !ids.contains(Text(o, "target"))) o.erase("target");
            objectives.push_back(o);
        }
        st["objectives"] = objectives;
        stages.push_back(st);
    }
    q["stages"] = stages;
    if (q.contains("giver") && q["giver"].is_object() && !Text(q["giver"], "alias").empty() && !ids.contains(Text(q["giver"], "alias"))) {
        std::string first;
        for (const auto& a : aliases) {
            if (first.empty() && Text(a, "kind") == "character") first = Text(a, "id");
        }
        if (first.empty()) q["giver"].erase("alias");
        else q["giver"]["alias"] = first;
    }
}

std::vector<std::string> GamePlan::HandleQuests(Job& job, const json& answer, const std::string& rationale) {
    json quests = answer.is_array() ? answer : answer.is_object() ? json::array({answer}) : json::array();
    // One quest per call: the answer's first, as that quest's slot (q-main, or q-side-N).
    const bool one = job.kind == "quest";
    if (quests.empty() && one && job.subject != "q-main") {
        // No side quest fits this slot (the AI found no giver or need left): it stays empty.
        for (const auto& doc : store_.List("quests")) {
            const json old = store_.Read(doc).value_or(json::object());
            if (old.is_object() && Text(old, "slot") == job.subject && !store_.Locked(doc)) store_.Remove(doc);
        }
        return {};
    }
    if (quests.empty()) return {"no quests"};
    if (one) quests = json::array({quests[0]});
    StoryGraph g = Graph();
    std::vector<std::string> known;
    for (const auto& n : g.nodes) known.push_back(n.id);
    std::vector<std::string> problems, fixes;
    std::vector<json> designs;
    const json skeleton = store_.Read("quests/q-main").value_or(json::object());
    std::set<std::string> otherIds; // quests of other slots: a side quest's id mustn't take theirs
    for (const auto& doc : store_.List("quests")) {
        const json d = store_.Read(doc).value_or(json::object());
        if (d.is_object() && (!one || Text(d, "slot") != job.subject) && Text(d, "kind") != "skeleton") otherIds.insert(PlanSlug(doc));
    }
    for (auto q : quests) {
        if (!q.is_object()) continue;
        if (one) {
            q["slot"] = job.subject;
            if (job.subject == "q-main") q["id"] = "q-main", q["type"] = "main";
            else {
                if (Text(q, "type") == "main" || Text(q, "type").empty()) q["type"] = "side";
                std::string id = Text(q, "id");
                if (id.empty() || id == "q-main" || otherIds.contains(id)) id = job.subject;
                q["id"] = id;
            }
        }
        RepairQuest(q, g, known, fixes);
        q["schema"] = "aige.quest-design/1";
        if (q.value("id", "") == "q-main" || q.value("type", "") == "main") {
            q["id"] = "q-main";
            // The skeleton's stages stay: each plays a kernel scene.
            for (const auto& st : skeleton.value("stages", json::array())) {
                auto& stages = q["stages"];
                const bool has = std::any_of(stages.begin(), stages.end(), [&](const json& s) { return s.value("advancesScene", "") == st.value("advancesScene", "-"); });
                if (!has) stages.push_back(st);
            }
            std::sort(q["stages"].begin(), q["stages"].end(), [](const json& a, const json& b) { return a.value("index", 0) < b.value("index", 0); });
        }
        if (store_.Locked("quests/" + Text(q, "id"))) continue;
        const auto found = ProblemTexts(ValidateQuestDesign(q, gso_, known));
        // On its last try a side quest that still doesn't hold is left out (noted), not failed.
        if (!found.empty() && job.attempts >= 2 && Text(q, "type") != "main") {
            fixes.push_back("left out " + Text(q, "id") + " (" + found.front() + ")");
            continue;
        }
        for (const auto& p : found) problems.push_back(Text(q, "id") + ": " + p);
        designs.push_back(q);
    }
    if (!problems.empty()) return problems;
    if (designs.empty() && one) {
        PlanDecision d;
        d.stage = "quests";
        d.actor = "rule";
        d.kind = "fix";
        d.subject = "quests";
        for (const auto& f : fixes) d.chosen += (d.chosen.empty() ? "" : "; ") + f;
        store_.Decide(d);
        return {};
    }
    // Out with what the last design of these quests put in the graph (one call: only its slot's).
    auto ours = [&](const json& old, const std::string& doc) {
        if (!one) return true;
        return Text(old, "slot") == job.subject || PlanSlug(doc) == job.subject ||
               std::any_of(designs.begin(), designs.end(), [&](const json& q) { return Text(q, "id") == PlanSlug(doc); });
    };
    for (const auto& doc : store_.List("quests")) {
        if (store_.Locked(doc)) continue;
        const json old = store_.Read(doc).value_or(json::object());
        if (old.is_object() && Text(old, "kind") != "skeleton" && ours(old, doc)) RemoveMadeBy(g, Text(old, "designedBy"), gso_, true);
    }
    const auto before = CheckStory(g, gso_);
    json nodes = json::array(), facts = json::array();
    for (const auto& q : designs) {
        const std::string qid = q.value("id", "");
        nodes.push_back({{"id", qid}, {"class", "Quest"}, {"type", q.value("type", "side")}, {"motivation", q.value("motivation", "")},
                         {"strategy", q.value("strategy", "")}, {"madeBy", kPending}});
        std::string firstScene;
        for (const auto& st : q.value("stages", json::array())) {
            const std::string sid = qid + "/" + std::to_string(st.value("index", 0));
            nodes.push_back({{"id", sid}, {"class", "QuestStage"}, {"index", st.value("index", 0)}, {"journal", st.value("journal", "")},
                             {"quest", qid}, {"madeBy", kPending}});
            facts.push_back({{"p", "hasStage"}, {"s", qid}, {"o", sid}, {"madeBy", kPending}});
            const std::string scene = st.value("advancesScene", "");
            if (!scene.empty() && g.Find(scene)) {
                facts.push_back({{"p", "advancesScene"}, {"s", sid}, {"o", scene}, {"madeBy", kPending}});
                if (firstScene.empty()) firstScene = scene;
            }
        }
        for (const auto& a : q.value("aliases", json::array())) {
            const std::string aid = qid + "/alias/" + a.value("id", "");
            std::string fill = a.value("fill", "");
            if (fill.empty()) fill = a.value("query", "");
            if (fill.empty()) fill = "in " + a.value("in", "");
            nodes.push_back({{"id", aid}, {"class", "Alias"}, {"kind", a.value("kind", "")}, {"fill", fill}, {"madeBy", kPending}});
            facts.push_back({{"p", "hasAlias"}, {"s", qid}, {"o", aid}, {"madeBy", kPending}});
            const std::string who = a.value("fill", "");
            if (a.value("kind", "") == "character" && g.Find(who) && gso_.IsA(g.Find(who)->cls, "Character")) {
                json need = {{"p", "needsCharacter"}, {"s", qid}, {"o", who}, {"madeBy", kPending}};
                if (!firstScene.empty()) need["at"] = firstScene;
                facts.push_back(need);
                if (q.value("giver", json::object()).value("alias", "") == a.value("id", "")) {
                    facts.push_back({{"p", "gives"}, {"s", who}, {"o", qid}, {"madeBy", kPending}});
                }
            }
        }
    }
    StoryGraph next = g;
    const std::string problem = next.Apply({{"nodes", nodes}, {"assert", facts}}, gso_);
    if (!problem.empty()) return {problem};
    for (const auto& i : NewErrors(before, CheckStory(next, gso_))) {
        if (i.rule.rfind("R-Q", 0) == 0 || i.rule == "shape") problems.push_back(i.rule + ": " + i.message);
    }
    // Its last try: kept, and what the rules still find goes to ⑨ (where it can be fixed).
    if (!problems.empty() && job.attempts < 2) return problems;
    if (!fixes.empty()) {
        PlanDecision fix;
        fix.stage = "quests";
        fix.actor = "rule";
        fix.kind = "fix";
        fix.subject = "quests";
        for (const auto& f : fixes) fix.chosen += (fix.chosen.empty() ? "" : "; ") + f;
        fix.rationale = "What the quest named that the story lacks was added as a stub; kinds, stage numbers and links put right.";
        store_.Decide(fix);
    }
    PlanDecision d;
    d.stage = "quests";
    d.actor = "ai";
    d.kind = "design-quest";
    d.subject = "quests";
    d.chosen = std::to_string(designs.size()) + " quests";
    d.rationale = rationale;
    const std::string decision = store_.Decide(d);
    StampMadeBy(next, decision);
    SaveGraph(next);
    std::set<std::string> written;
    for (auto q : designs) {
        std::vector<std::string> inputs;
        for (const auto& a : store_.List("story/acts")) inputs.push_back(a);
        for (const auto& a : q.value("aliases", json::array())) {
            const std::string fill = a.value("fill", "");
            if (fill.rfind("character/", 0) == 0 && store_.Exists("characters/" + PlanSlug(fill))) inputs.push_back("characters/" + PlanSlug(fill));
        }
        q["designedBy"] = decision;
        if (q.value("rationale", "").empty()) q["rationale"] = rationale;
        store_.Write("quests/" + q.value("id", ""), q, decision, inputs);
        written.insert("quests/" + q.value("id", ""));
    }
    for (const auto& doc : store_.List("quests")) {
        if (written.contains(doc) || store_.Locked(doc)) continue;
        const json old = store_.Read(doc).value_or(json::object());
        if (!one || (old.is_object() && Text(old, "slot") == job.subject)) store_.Remove(doc); // one call: only its slot's old design
    }
    return {};
}

// --- ⑥ Places -------------------------------------------------------------------------------------

std::vector<std::string> GamePlan::HandlePlace(Job& job, const json& answer, const std::string& rationale) {
    const std::string id = job.subject;
    StoryGraph g = Graph();
    const StoryNode* n = g.Find(id);
    json p = answer;
    p["schema"] = "aige.place-desc/1";
    p["id"] = id;
    if (p.value("kind", "").empty()) p["kind"] = n ? n->Text("buildType") : "interior";
    json brief = p.value("brief", json::object());
    if (!brief.is_object()) brief = json::object();
    brief["schema"] = "aige.brief/1";
    brief["buildType"] = p["kind"];
    if (!brief.contains("name")) brief["name"] = n && !n->Text("name").empty() ? n->Text("name") : PlanSlug(id);
    if (host_.places) {
        // What a small model gets wrong about a Brief, put right instead of sent back: a build
        // type the library hasn't, components the build type doesn't have ("rooms" for a
        // dungeon), features that are empty or unknown, or that this build type can't have.
        const Ontology& places = host_.places();
        if (!places.Builds().contains(Text(p, "kind"))) {
            const std::string fromGraph = n ? n->Text("buildType") : "";
            p["kind"] = places.Builds().contains(fromGraph) ? fromGraph : "interior";
            brief["buildType"] = p["kind"];
        }
        const auto build = places.Builds().find(Text(p, "kind"));
        if (build != places.Builds().end() && brief.contains("components") && brief["components"].is_object()) {
            json kept = json::object();
            for (const auto& [cid, c] : brief["components"].items()) {
                const auto& allowed = build->second.components;
                if (std::find(allowed.begin(), allowed.end(), cid) != allowed.end()) kept[cid] = c;
            }
            brief["components"] = kept;
        }
        if (brief.contains("features")) {
            json kept = json::array();
            for (const auto& f : brief["features"].is_array() ? brief["features"] : json::array({brief["features"]})) {
                const std::string fid = f.is_object() ? Text(f, "module") : f.is_string() ? f.get<std::string>() : "";
                const ModuleDef* module = places.Module(fid);
                if (!module) continue;
                if (!module->buildTypes.empty() && build != places.Builds().end() &&
                    std::find(module->buildTypes.begin(), module->buildTypes.end(), build->first) == module->buildTypes.end()) {
                    continue;
                }
                kept.push_back(f);
            }
            brief["features"] = kept;
        }
        brief = CompleteBrief(places, brief);
    }
    p["brief"] = brief;
    if (host_.places) {
        // Anything else the checks name inside the Brief ("brief.x.y"): that part left out, and
        // checked again; only what can't be left out goes back to the AI.
        auto problems = ValidatePlaceDesc(p, host_.places());
        for (int round = 0; round < 8 && HasErrors(problems); ++round) {
            bool removed = false;
            for (const auto& problem : problems) {
                if (problem.warning || problem.where.rfind("brief.", 0) != 0) continue;
                json* parent = &p["brief"];
                std::string rest = problem.where.substr(6);
                for (size_t dot = rest.find('.'); dot != std::string::npos && parent->is_object(); dot = rest.find('.')) {
                    const std::string key = rest.substr(0, dot);
                    if (!parent->contains(key)) break;
                    parent = &(*parent)[key];
                    rest = rest.substr(dot + 1);
                }
                const std::string key = rest.substr(0, rest.find('['));
                if (parent->is_object() && parent->contains(key) && key != "buildType" && key != "name") {
                    parent->erase(key);
                    removed = true;
                }
            }
            if (!removed) break;
            p["brief"] = CompleteBrief(host_.places(), p["brief"]);
            problems = ValidatePlaceDesc(p, host_.places());
        }
        if (HasErrors(problems)) return ProblemTexts(problems);
    }
    PlanDecision d;
    d.stage = "places";
    d.actor = "ai";
    d.kind = "describe-place";
    d.subject = "places/" + PlanSlug(id);
    d.chosen = p.value("description", json::object()).value("purpose", "");
    d.rationale = rationale;
    const std::string decision = store_.Decide(d);
    if (StoryNode* node = g.Find(id)) {
        node->fields["buildType"] = p["kind"];
        std::set<std::string> parts;
        for (const auto& a : p.value("storyAnchors", json::array())) {
            if (!a.value("module", "").empty()) parts.insert(a.value("module", ""));
            if (!a.value("id", "").empty()) parts.insert(a.value("id", ""));
        }
        for (const auto& s : g.SceneOrder()) {
            if (g.LocationOfScene(s) != id) continue;
            for (const auto& part : g.Find(s)->List("needsParts")) parts.insert(part);
        }
        node->fields["requiredParts"] = json(std::vector<std::string>(parts.begin(), parts.end()));
        const json desc = p.value("description", json::object());
        if (!desc.value("mood", "").empty()) node->fields["mood"] = desc["mood"];
    }
    SaveGraph(g);
    std::vector<std::string> inputs = {"outline"};
    for (const auto& doc : store_.List("quests")) {
        const json q = store_.Read(doc).value_or(json::object());
        for (const auto& a : q.value("aliases", json::array())) {
            if (a.value("in", "") == id && std::find(inputs.begin(), inputs.end(), doc) == inputs.end()) inputs.push_back(doc);
        }
    }
    p["rationale"] = rationale;
    WorldStub(id, n ? n->Text("name") : "", "", p.value("description", json::object()).value("purpose", ""));
    store_.Write("places/" + PlanSlug(id), p, decision, inputs);
    return {};
}

// --- ⑦ Systems and ⑧ Visuals -----------------------------------------------------------------------

std::vector<std::string> GamePlan::HandleSystems(Job&, const json& answer, const std::string& rationale) {
    // Each record put right instead of the whole answer sent back: a field the record type
    // refuses ("stack": false) is left out, so its default applies; a record still refused, of
    // a type the engine hasn't, or without a type or id, is left out. Noted, not failed.
    json systems = answer.is_object() ? answer : json::object();
    std::vector<std::string> fixes;
    json kept = json::array();
    for (auto r : systems.value("records", json::array())) {
        if (!r.is_object()) continue;
        const std::string type = Text(r, "type"), id = Text(r, "id");
        if (type.empty() || id.empty()) {
            fixes.push_back("left out a record without a type or id");
            continue;
        }
        json record = r.value("record", json::object());
        if (!record.is_object()) record = json::object();
        std::vector<std::string> refused;
        if (host_.validateRecord) {
            for (int round = 0; round < 12; ++round) {
                refused = host_.validateRecord(type, record);
                if (refused.empty()) break;
                bool removed = false;
                for (const auto& p : refused) {
                    const std::string field = p.substr(0, std::min(p.find(':'), std::min(p.find('.'), p.find('['))));
                    if (record.contains(field)) {
                        record.erase(field);
                        fixes.push_back(type + "/" + id + ": " + p + " (left at its default)");
                        removed = true;
                    }
                }
                if (!removed) break;
            }
        }
        if (!refused.empty()) {
            fixes.push_back("left out " + type + "/" + id + " (" + refused.front() + ")");
            continue;
        }
        r["record"] = record;
        kept.push_back(r);
    }
    systems["records"] = kept;
    if (!fixes.empty()) {
        PlanDecision fix;
        fix.stage = "systems";
        fix.actor = "rule";
        fix.kind = "fix";
        fix.subject = "systems";
        for (const auto& f : fixes) fix.chosen += (fix.chosen.empty() ? "" : "; ") + f;
        fix.rationale = "Record fields the engine refuses were left at their defaults; records it can't take, left out.";
        store_.Decide(fix);
    }
    PlanDecision d;
    d.stage = "systems";
    d.actor = "ai";
    d.kind = "write-records";
    d.subject = "systems";
    d.chosen = std::to_string(systems.value("records", json::array()).size()) + " records";
    d.rationale = rationale;
    json doc = systems;
    doc["schema"] = "aige.systems/1";
    doc["rationale"] = rationale;
    std::vector<std::string> inputs = {"brief"};
    for (const auto& c : store_.List("characters")) inputs.push_back(c);
    for (const auto& q : store_.List("quests")) inputs.push_back(q);
    store_.Write("systems", doc, store_.Decide(d), inputs);
    return {};
}

std::vector<std::string> GamePlan::HandleVisuals(Job&, const json& ui, const std::string& rationale) {
    std::vector<std::string> themes = Strings(host_.catalog.value("themes", json::array()));
    std::vector<std::string> screens = PlanWindows();
    const auto problems = ValidateUiPlan(ui, themes, screens, PlanHudWidgets());
    if (HasErrors(problems)) return ProblemTexts(problems);
    PlanDecision d;
    d.stage = "visuals";
    d.actor = "ai";
    d.kind = "design-ui";
    d.subject = "ui";
    d.chosen = ui.value("theme", "");
    d.rationale = rationale;
    json doc = ui;
    doc["schema"] = "aige.ui-plan/1";
    doc["rationale"] = rationale;
    store_.Write("ui", doc, store_.Decide(d), {"brief", "systems"});
    return {};
}

// --- ⑨ Validate ------------------------------------------------------------------------------------

void GamePlan::LocalJob(Job& job) {
    job.status = "running";
    BuildReport(store_.Read("review").value_or(json::object()));
    job.status = "done";
    Save();
    Pump(); // what the report set going (its fixes, the report after them) goes on
}

void GamePlan::BuildReport(const json& review) {
    const StoryGraph g = Graph();
    const json brief = Brief();
    const json old = store_.Read("report").value_or(json::object());
    json findings = json::array();
    auto add = [&](const std::string& rule, const std::string& severity, const std::string& subject, const std::string& message, json fix = json()) {
        json f = {{"id", "f-" + std::to_string(findings.size() + 1)}, {"rule", rule}, {"severity", severity}, {"subject", subject},
                  {"message", message}, {"status", "open"}};
        if (!fix.is_null()) f["fix"] = fix;
        for (const auto& o : old.value("findings", json::array())) {
            if (o.value("rule", "") == rule && o.value("message", "") == message && o.value("status", "") == "accepted") {
                f["status"] = "accepted";
                f["reason"] = o.value("reason", "");
            }
        }
        findings.push_back(f);
    };
    // 1. The story rules over the graph.
    for (const auto& i : CheckStory(g, gso_)) add(i.rule, i.severity, i.node, i.message);
    // 2. Across documents.
    for (const auto& s : g.SceneOrder()) {
        for (const auto& c : g.Participants(s)) {
            const StoryNode* n = g.Find(c);
            if (!n || !gso_.IsA(n->cls, "Character") || store_.Exists("characters/" + PlanSlug(c))) continue;
            const bool important = n->Text("importance") == "major" || n->Text("importance") == "supporting";
            add("X-character", important ? "error" : "warning", c, (n->Text("name").empty() ? c : n->Text("name")) + " takes part in " + s + " but has no character document (④)",
                {{"make", {{"kind", "character"}, {"subject", c}}}});
        }
    }
    for (const auto* n : g.OfClass("Location", gso_)) {
        if (!store_.Exists("places/" + PlanSlug(n->id))) add("X-place", "error", n->id, n->id + " has no place description (⑥)", {{"make", {{"kind", "place"}, {"subject", n->id}}}});
    }
    for (const auto& doc : store_.List("quests")) {
        const json q = store_.Read(doc).value_or(json::object());
        for (const auto& a : q.value("aliases", json::array())) {
            const std::string in = a.value("in", "");
            if (in.empty()) continue;
            const auto place = store_.Read("places/" + PlanSlug(in));
            if (!place) {
                add("X-anchor", "error", q.value("id", ""), "alias '" + a.value("id", "") + "' is in " + in + ", which has no description");
                continue;
            }
            const std::string anchor = a.value("where", json::object()).value("anchor", a.value("id", ""));
            const auto anchors = place->value("storyAnchors", json::array());
            const bool has = std::any_of(anchors.begin(), anchors.end(), [&](const json& x) { return x.value("id", "") == anchor || x.value("module", "") == anchor; });
            if (a.value("kind", "") == "spot" && !has) {
                add("X-anchor", "error", q.value("id", ""), "no story anchor '" + anchor + "' in " + in + " for alias '" + a.value("id", "") + "'",
                    {{"regenerate", "places/" + PlanSlug(in)}});
            }
        }
    }
    const json systems = store_.Read("systems").value_or(json::object());
    std::set<std::string> items;
    for (const auto& r : systems.value("records", json::array())) {
        if (r.value("type", "") == "item") items.insert(r.value("id", ""));
    }
    for (const auto& doc : store_.List("quests")) {
        const json q = store_.Read(doc).value_or(json::object());
        for (const auto& r : q.value("rewards", json::array())) {
            const std::string item = PlanSlug(r.value("item", ""));
            if (!item.empty() && !items.contains(item)) add("X-item", "warning", q.value("id", ""), "reward item '" + item + "' has no record (⑦)", {{"addItem", item}});
        }
    }
    if (!BriefGet(brief, "systems.abilities.on").is_boolean() || !BriefGet(brief, "systems.abilities.on").get<bool>()) {
        for (const auto& doc : store_.List("characters")) {
            const json c = store_.Read(doc).value_or(json::object());
            if (!c.value("gameplay", json::object()).value("abilities", json::array()).empty()) {
                add("X-system", "error", c.value("id", ""), c.value("name", "") + " has abilities, but abilities are off in the Brief", {{"clearAbilities", doc}});
            }
        }
    }
    const json ui = store_.Read("ui").value_or(json::object());
    const auto screens = Strings(ui.value("screens", json::array()));
    auto needScreen = [&](bool on, const std::string& screen, const std::string& why, const std::string& severity = "error") {
        if (on && !ui.empty() && std::find(screens.begin(), screens.end(), screen) == screens.end()) {
            add("X-screen", severity, "ui", "the " + screen + " window is needed (" + why + ")", {{"addScreen", screen}});
        }
    };
    auto on = [&](const char* path) { return BriefGet(brief, path).is_boolean() && BriefGet(brief, path).get<bool>(); };
    needScreen(on("systems.inventory.on"), "inventory", "inventory is on");
    needScreen(!store_.List("quests").empty(), "journal", "the game has quests");
    needScreen(on("systems.abilities.magic"), "spellbook", "magic is on");
    needScreen(g.OfClass("Location", gso_).size() > 1, "map", "more than one place", "warning");
    // 3. What the stages couldn't fix.
    for (const auto& f : findings_) add("stage", f.value("severity", "warning"), f.value("subject", ""), f.value("message", ""), {{"retry", true}});
    // 4. The critic.
    for (const auto& f : review.value("findings", json::array())) {
        add("critic", f.value("severity", "warning") == "error" ? "error" : "warning", f.value("subject", ""), f.value("message", ""), f.value("fix", json()));
    }
    // (A finding's remedy is worked out when it's shown or fixed, never stored: so it's always the
    // current one.)
    resume_ = json(); // the report decides what the plan does next
    int errors = 0, warnings = 0, accepted = 0;
    for (const auto& f : findings) {
        if (f["status"] == "accepted") ++accepted;
        else if (f["severity"] == "error") ++errors;
        else ++warnings;
    }
    PlanDecision d;
    d.stage = "validate";
    d.actor = "rule";
    d.kind = "validate";
    d.subject = "report";
    d.chosen = std::to_string(errors) + " errors, " + std::to_string(warnings) + " warnings";
    d.checks = {{"errors", errors}, {"warnings", warnings}, {"accepted", accepted}};
    store_.Write("report", {{"schema", "aige.plan-report/1"}, {"kind", "validate"},
                            {"summary", {{"errors", errors}, {"warnings", warnings}, {"accepted", accepted}}}, {"findings", findings}},
                 store_.Decide(d));
    if (errors == 0) {
        autoFixed_ = false;
        fixRounds_ = 0;
        fixScope_ = "errors";
        const int baseline = store_.Commit("baseline");
        json state = store_.State();
        state["baseline"] = baseline;
        store_.SaveState(state);
        stage_ = "done";
        status_ = "done";
        message_ = "The design is validated (baseline " + std::to_string(baseline) + "): " + std::to_string(warnings) + " warnings to look at.";
    } else {
        stage_ = "validate";
        status_ = "waiting";
        message_ = std::to_string(errors) + " errors to fix before building: Fix them, or accept them with a reason.";
        // What has a remedy is fixed on its own and checked again, while errors remain: up to three
        // rounds (a fix can turn up something new); then what's left is for Fix or Accept.
        if (fixRounds_ < 3 && fixScope_ != "none") {
            ++fixRounds_;
            autoFixed_ = true;
            Save();
            if (FixFindings({fixScope_}).empty()) return;
        }
    }
    Save();
}

json GamePlan::RemedyOf(const json& f) const {
    const std::string rule = f.value("rule", ""), subject = f.value("subject", ""), message = f.value("message", "");
    const json fix = f.value("fix", json());
    auto remedy = [](const std::string& how, json what) { return json{{"how", how}, {"do", std::move(what)}}; };
    if (fix.is_object()) {
        if (fix.contains("make")) return remedy("make " + PlanSlug(fix["make"].value("subject", "")) + " (" + fix["make"].value("kind", "") + ")", fix);
        if (fix.contains("addScreen")) return remedy("add the " + fix.value("addScreen", "") + " window", fix);
        if (fix.contains("addItem")) return remedy("add a record for " + fix.value("addItem", ""), fix);
        if (fix.contains("clearAbilities")) return remedy("remove the abilities", fix);
        if (fix.contains("retry") && rule != "stage") return remedy("try it again", fix);
        if (fix.contains("regenerate")) return remedy("correct " + PlanSlug(fix.value("regenerate", "")), {{"quickfix", fix.value("regenerate", "")}});
    }
    // A report from before the findings carried their fix: read from the finding itself.
    if (rule == "X-character" && subject.rfind("character/", 0) == 0)
        return remedy("make " + PlanSlug(subject) + " (character)", {{"make", {{"kind", "character"}, {"subject", subject}}}});
    if (rule == "X-place" && subject.rfind("location/", 0) == 0)
        return remedy("make " + PlanSlug(subject) + " (place)", {{"make", {{"kind", "place"}, {"subject", subject}}}});
    if (rule == "X-screen") {
        const auto a = message.find("the "), b = message.find(" window");
        if (a != std::string::npos && b != std::string::npos && b > a + 4)
            return remedy("add the " + message.substr(a + 4, b - a - 4) + " window", {{"addScreen", message.substr(a + 4, b - a - 4)}});
    }
    if (rule == "X-item") {
        const auto a = message.find('\''), b = message.find('\'', a + 1);
        if (a != std::string::npos && b != std::string::npos) return remedy("add a record for " + message.substr(a + 1, b - a - 1), {{"addItem", message.substr(a + 1, b - a - 1)}});
    }
    if (rule == "stage") {
        // A step that failed (or is being tried again already): tried again.
        const bool failed = std::any_of(jobs_.begin(), jobs_.end(), [&](const Job& j) {
            return (j.status == "failed" || j.status == "queued" || j.status == "running") && j.kind != "revise" && j.kind != "report" &&
                   (j.subject == subject || PlanSlug(j.subject) == PlanSlug(subject));
        });
        if (failed) return remedy("try it again", {{"retry", true}});
        // A character or place that was never made: made.
        if (subject.rfind("character/", 0) == 0 && !store_.Exists("characters/" + PlanSlug(subject)))
            return remedy("make " + PlanSlug(subject) + " (character)", {{"make", {{"kind", "character"}, {"subject", subject}}}});
        if (subject.rfind("location/", 0) == 0 && !store_.Exists("places/" + PlanSlug(subject)))
            return remedy("make " + PlanSlug(subject) + " (place)", {{"make", {{"kind", "place"}, {"subject", subject}}}});
        // A note about what was settled already ("... was left out"), or the AI's own noise: off the list.
        if (subject == "revise" || message.find("was left out") != std::string::npos || message.find("isn't in the story") != std::string::npos)
            return remedy("dismiss the note (already handled)", {{"dismiss", true}});
        // A note about a scene, act, character, place or quest: that item corrected where it says.
        std::string doc;
        for (const std::string& candidate : {"story/scenes/" + subject, "story/scenes/" + subject.substr(0, subject.find('/')), "story/acts/" + subject,
                                            "characters/" + PlanSlug(subject), "places/" + PlanSlug(subject), "quests/" + subject}) {
            if (doc.empty() && store_.Exists(candidate)) doc = candidate;
        }
        if (!doc.empty() && !store_.Locked(doc)) return remedy("correct " + PlanSlug(doc), {{"quickfix", doc}});
        return remedy("ask the AI for the smallest fix", {{"suggest", true}});
    }
    // The story rules: by what they're about.
    const auto docOf = [&](const std::string& node) -> std::string {
        if (store_.Exists("story/scenes/" + node)) return "story/scenes/" + node;
        if (node.rfind("character/", 0) == 0) return "characters/" + PlanSlug(node);
        if (node.rfind("location/", 0) == 0) return "places/" + PlanSlug(node);
        if (node.rfind("q-", 0) == 0 || store_.Exists("quests/" + node)) return "quests";
        return {};
    };
    if (rule == "R-P3") {
        // "Scene s needs a <part> in <location>, which doesn't list one."
        const auto a = message.find(" needs a "), b = message.find(" in ", a == std::string::npos ? 0 : a + 9), c = message.find(", which");
        if (a != std::string::npos && b != std::string::npos && c != std::string::npos && b < c) {
            const std::string part = message.substr(a + 9, b - a - 9), location = message.substr(b + 4, c - b - 4);
            return remedy("list a " + part + " in " + PlanSlug(location), {{"addPart", {{"location", location}, {"part", part}}}});
        }
    }
    if (rule == "R-P1" && message.find("has no build type") != std::string::npos) {
        const std::string loc = Graph().LocationOfScene(subject);
        if (!loc.empty()) return remedy("give " + PlanSlug(loc) + " a build type", {{"buildType", loc}});
    }
    // A kernel event with no cause (R-C1; the reviewer's "kernel-event/<id>" too): enabled by the
    // latest event of an earlier scene, or the inciting incident if it comes first. No AI.
    if (rule == "critic") {
        // The reviewer names things its own way ("location/enforcer-s-office" for
        // location/enforcers-office, "kernel-event/s1-01/e1"): matched to the story's ids by their
        // letters and digits, then fixed as the rule for the same problem would be.
        auto squash = [](const std::string& s) {
            std::string out;
            for (unsigned char c : s) {
                if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
            }
            return out;
        };
        std::string id = subject.rfind("kernel-event/", 0) == 0 ? subject.substr(13) : subject;
        const StoryGraph g = Graph();
        if (!g.Find(id)) {
            for (const auto& n : g.nodes) {
                if (squash(n.id) == squash(id)) id = n.id;
            }
        }
        std::string words = message;
        for (auto& c : words) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const StoryNode* node = g.Find(id);
        if (node && node->cls == "Location" && (words.find("unreachable") != std::string::npos || words.find("can't be reached") != std::string::npos))
            return remedy("connect " + PlanSlug(id) + " to the start", {{"connect", id}});
        if (node && gso_.IsA(node->cls, "Event") && words.find("cause") != std::string::npos)
            return remedy("let an earlier event lead to " + id, {{"cause", id}});
        if (node && gso_.IsA(node->cls, "Character") && words.find("package") != std::string::npos) {
            const auto templates = Strings(host_.catalog.value("packageTemplates", json::array()));
            const std::string value = templates.empty() || std::find(templates.begin(), templates.end(), "wander") != templates.end() ? "wander" : templates.front();
            return remedy("give " + PlanSlug(id) + " the " + value + " package", {{"setRole", {{"character", id}, {"field", "package"}, {"value", value}}}});
        }
    }
    if (rule == "R-C1" || (rule == "critic" && subject.rfind("kernel-event/", 0) == 0)) {
        const std::string event = subject.rfind("kernel-event/", 0) == 0 ? subject.substr(13) : subject;
        return remedy("let an earlier event lead to " + event, {{"cause", event}});
    }
    // A place nobody can reach (R-P2): a door (an interior) or a road from the start. No AI.
    if (rule == "R-P2" && subject.rfind("location/", 0) == 0) return remedy("connect " + PlanSlug(subject) + " to the start", {{"connect", subject}});
    // A gameplay role without what it needs (R-K5), a placed character with nothing to do (R-K6):
    // filled in directly. A companion follows (wander), a guard guards, a vendor works.
    if (rule == "R-K5" || rule == "R-K6") {
        std::string field = "package", role;
        if (rule == "R-K5") {
            const auto a = message.find(" is a "), b = message.find(" but has no ");
            if (a != std::string::npos && b != std::string::npos && b > a) {
                role = message.substr(a + 6, b - a - 6);
                const std::string missing = message.substr(b + 12);
                field = missing.rfind("stock", 0) == 0 ? "stock" : missing.rfind("skills", 0) == 0 ? "skills" : missing.rfind("fight", 0) == 0 ? "statClass" : "package";
            }
        }
        std::string value = field == "stock" ? "general goods" : field == "skills" ? "combat basics" : field == "statClass" ? "boss" : "wander";
        if (field == "package") {
            const auto templates = Strings(host_.catalog.value("packageTemplates", json::array()));
            const std::string want = role == "guard" ? "guard" : (role == "vendor" || role == "innkeeper" || role == "crafter" || role == "healer" || role == "trainer") ? "work" : "wander";
            value = templates.empty() || std::find(templates.begin(), templates.end(), want) != templates.end() ? want : templates.front();
        }
        return remedy("give " + PlanSlug(subject) + " " + (field == "package" ? "the " + value + " package" : field == "statClass" ? "a stat class" : value),
                      {{"setRole", {{"character", subject}, {"field", field}, {"value", value}}}});
    }
    // A giver without the quest's motivation (R-Q4): the quest takes one the giver has (or the
    // giver gets the quest's). No AI.
    if (rule == "R-Q4") return remedy("match " + subject + "'s motivation to its giver", {{"questMotivation", subject}});
    if (rule.rfind("R-Q", 0) == 0) {
        const std::string doc = store_.Exists("quests/" + subject) ? "quests/" + subject : "quests/q-main";
        return remedy("correct " + PlanSlug(doc), {{"quickfix", doc}});
    }
    if (rule.rfind("R-", 0) == 0 || rule == "critic") {
        const std::string doc = docOf(subject);
        if (!doc.empty() && store_.Exists(doc) && !store_.Locked(doc)) return remedy("correct " + PlanSlug(doc), {{"quickfix", doc}});
        if (!doc.empty() && !store_.Exists(doc) && doc.rfind("characters/", 0) == 0)
            return remedy("make " + PlanSlug(doc) + " (character)", {{"make", {{"kind", "character"}, {"subject", subject}}}});
    }
    // Anything else: the AI suggests the smallest edits that resolve it (every such finding in
    // one call, as a revision of the plan's documents).
    return remedy("ask the AI for the smallest fix", {{"suggest", true}});
}

std::string GamePlan::FixFindings(const std::vector<std::string>& ids) {
    auto report = store_.Read("report");
    if (!report) return "there's no report yet";
    const auto has = [&](const char* word) { return std::find(ids.begin(), ids.end(), word) != ids.end(); };
    const bool every = has("*"), errors = has("errors") || ids.empty(), warnings = has("warnings");
    std::vector<std::string> done;
    std::set<std::string> remade;
    std::map<std::string, std::vector<std::string>> quickFixes; // document -> the findings it corrects
    std::vector<std::string> suggest;                           // for the AI to suggest edits for, in one call
    std::map<std::string, std::string> how;                     // finding -> what fixes it ("direct", "doc:<doc>", "job:<kind> <subject>", "suggest", "retry")
    // Fixing is going on: a paused plan resumes (else the fixes would wait unseen).
    if (paused_) {
        paused_ = false;
        if (status_ == "paused") status_ = "working";
    }
    bool retry = false;
    json ui = store_.Read("ui").value_or(json::object()), systems = store_.Read("systems").value_or(json::object());
    bool uiChanged = false, systemsChanged = false;
    StoryGraph g = Graph();
    bool graphChanged = false;
    for (auto f : report->value("findings", json::array())) {
        if (f.value("status", "") != "open") continue;
        f.erase("remedy"); // one stored by an older report: worked out again
        if (const json remedy = RemedyOf(f); remedy.is_object()) f["remedy"] = remedy;
        else continue;
        const bool isError = f.value("severity", "") == "error";
        const bool chosen = every || (errors && isError) || (warnings && !isError) ||
                            std::find(ids.begin(), ids.end(), f.value("id", "")) != ids.end();
        if (!chosen) continue;
        const json what = f["remedy"].value("do", json::object());
        if (what.contains("addScreen") && ui.is_object() && !ui.empty()) {
            if (!ui.contains("screens") || !ui["screens"].is_array()) ui["screens"] = json::array();
            if (std::find(ui["screens"].begin(), ui["screens"].end(), what["addScreen"]) == ui["screens"].end()) ui["screens"].push_back(what["addScreen"]);
            uiChanged = true;
        } else if (what.contains("addItem")) {
            if (!systems.contains("records") || !systems["records"].is_array()) systems["records"] = json::array();
            std::string name = what.value("addItem", "");
            std::replace(name.begin(), name.end(), '-', ' ');
            if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
            systems["records"].push_back({{"type", "item"}, {"id", what.value("addItem", "")}, {"record", {{"name", name}}}});
            systemsChanged = true;
        } else if (what.contains("clearAbilities")) {
            const std::string doc = what.value("clearAbilities", "");
            if (auto c = store_.Read(doc); c && !store_.Locked(doc)) {
                (*c)["gameplay"]["abilities"] = json::array();
                store_.Touch(doc, *c, "");
            }
        } else if (what.contains("addPart")) {
            const std::string location = what["addPart"].value("location", ""), part = what["addPart"].value("part", "");
            if (StoryNode* n = g.Find(location)) {
                json parts = n->fields.value("requiredParts", json::array());
                if (!parts.is_array()) parts = json::array();
                if (std::find(parts.begin(), parts.end(), part) == parts.end()) parts.push_back(part);
                n->fields["requiredParts"] = parts;
                graphChanged = true;
            }
            const std::string doc = "places/" + PlanSlug(location);
            if (auto p = store_.Read(doc); p && !store_.Locked(doc) && host_.places && host_.places().Module(part)) {
                if (!p->contains("storyAnchors") || !(*p)["storyAnchors"].is_array()) (*p)["storyAnchors"] = json::array();
                (*p)["storyAnchors"].push_back({{"id", part}, {"module", part}});
                store_.Touch(doc, *p, "");
            }
        } else if (what.contains("buildType")) {
            if (StoryNode* n = g.Find(what.value("buildType", ""))) {
                const json place = store_.Read("places/" + PlanSlug(n->id)).value_or(json::object());
                n->fields["buildType"] = place.value("kind", json("interior")).is_string() ? place.value("kind", "interior") : "interior";
                graphChanged = true;
            }
        } else if (what.contains("setRole")) {
            const std::string who = what["setRole"].value("character", ""), field = what["setRole"].value("field", ""),
                              value = what["setRole"].value("value", "");
            if (StoryNode* n = g.Find(who)) {
                n->fields[field] = field == "package" ? json(PlanSlug(who) + ": " + value) : json(value);
                graphChanged = true;
            }
            const std::string doc = "characters/" + PlanSlug(who);
            if (auto c = store_.Read(doc); c && !store_.Locked(doc)) {
                if (field == "package") {
                    json& behaviour = (*c)["behaviour"];
                    if (!behaviour.is_object()) behaviour = json::object();
                    if (!behaviour.contains("packages") || !behaviour["packages"].is_array()) behaviour["packages"] = json::array();
                    behaviour["packages"].push_back({{"template", value}});
                } else {
                    if (!(*c)["gameplay"].is_object()) (*c)["gameplay"] = json::object();
                    (*c)["gameplay"][field == "statClass" ? "class" : field] = value;
                }
                store_.Touch(doc, *c, "");
            }
        } else if (what.contains("cause")) {
            const std::string event = what.value("cause", "");
            const std::string scene = event.substr(0, event.find('/'));
            const double at = g.OrderOf(scene);
            // The latest event presented by an earlier scene (a kernel one if that scene has one).
            std::string before;
            double best = -1;
            bool bestKernel = false;
            for (const auto& e : g.edges) {
                if (e.p != "presents" || e.o == event) continue;
                const double order = g.OrderOf(e.s);
                if (order < 0 || order >= at) continue;
                const StoryNode* ev = g.Find(e.o);
                const bool kernel = ev && ev->Flag("kernel");
                if (order > best || (order == best && kernel && !bestKernel)) best = order, before = e.o, bestKernel = kernel;
            }
            if (StoryNode* n = g.Find(event)) {
                if (before.empty()) n->fields["inciting"] = true; // nothing comes before it: it sets the story going
                else if (g.Apply({{"assert", json::array({{{"p", "enables"}, {"s", before}, {"o", event}}})}}, gso_).empty()) {}
                graphChanged = true;
            }
        } else if (what.contains("connect")) {
            const std::string place = what.value("connect", "");
            std::string start;
            for (const auto& n : g.nodes) {
                if (n.cls == "Location" && n.Flag("start")) start = n.id;
            }
            if (const StoryNode* n = g.Find(place); n && !start.empty() && start != place) {
                const std::string kind = n->Text("buildType") == "interior" ? "door" : "road";
                if (g.Apply({{"assert", json::array({{{"p", "connects"}, {"s", start}, {"o", place}, {"kind", kind}}})}}, gso_).empty()) graphChanged = true;
            }
        } else if (what.contains("questMotivation")) {
            const std::string quest = what.value("questMotivation", "");
            std::string giver;
            for (const auto& e : g.edges) {
                if (e.p == "gives" && e.o == quest) giver = e.s;
            }
            StoryNode* qn = g.Find(quest);
            StoryNode* gn = g.Find(giver);
            if (qn && gn) {
                const auto wants = gn->List("motivations");
                const std::string motivation = qn->Text("motivation");
                if (!wants.empty()) {
                    qn->fields["motivation"] = wants.front();
                    if (auto q = store_.Read("quests/" + quest); q && q->is_object() && !store_.Locked("quests/" + quest)) {
                        (*q)["motivation"] = wants.front();
                        store_.Touch("quests/" + quest, *q, "");
                    }
                } else {
                    gn->fields["motivations"] = json::array({motivation});
                    const std::string doc = "characters/" + PlanSlug(giver);
                    if (auto c = store_.Read(doc); c && c->is_object() && !store_.Locked(doc)) {
                        if (!(*c)["biography"].is_object()) (*c)["biography"] = json::object();
                        (*c)["biography"]["motivations"] = json::array({motivation});
                        store_.Touch(doc, *c, "");
                    }
                }
                graphChanged = true;
            }
        } else if (what.contains("quickfix")) {
            quickFixes[what.value("quickfix", "")].push_back(f.value("message", ""));
            how[f.value("id", "")] = "doc:" + what.value("quickfix", "");
        } else if (what.contains("suggest")) {
            suggest.push_back("[" + f.value("rule", "") + "] " + f.value("subject", "") + ": " + f.value("message", ""));
            how[f.value("id", "")] = "suggest";
        } else if (what.contains("make")) {
            const std::string kind = what["make"].value("kind", ""), subject = what["make"].value("subject", "");
            if (remade.insert(kind + " " + subject).second) AddJob(kind, subject);
            how[f.value("id", "")] = "job:" + kind + " " + subject;
        } else if (what.contains("remake")) {
            const std::string doc = what.value("remake", "");
            quickFixes[doc].push_back(f.value("message", "")); // corrected in place, not made again
            how[f.value("id", "")] = "doc:" + doc;
        } else if (what.contains("dismiss")) {
            json kept = json::array();
            for (const auto& n : findings_) {
                if (!(n.value("subject", json()) == f.value("subject", json()) && n.value("message", json()) == f.value("message", json()))) kept.push_back(n);
            }
            findings_ = kept;
        } else if (what.contains("retry")) {
            retry = true;
            how[f.value("id", "")] = "retry";
        } else {
            continue;
        }
        if (!how.contains(f.value("id", ""))) how[f.value("id", "")] = "direct";
        done.push_back(f.value("id", "") + ": " + f["remedy"].value("how", ""));
    }
    if (done.empty()) return ids.empty() ? "nothing has a fix" : "those findings have no fix";
    // One quick fix per document, with every finding it has.
    for (const auto& [doc, problems] : quickFixes) {
        if (remade.insert(doc).second) QuickFix(doc, problems);
    }
    // The rest: one AI call suggests the smallest edits for all of them.
    std::string suggestJob;
    if (!suggest.empty()) {
        std::string instruction = "Fix these validation findings with the smallest edits to the plan's documents; change only what each "
                                  "one names and keep everything else as it is:";
        for (const auto& s : suggest) instruction += "\n- " + s;
        Revise(instruction);
        if (!jobs_.empty() && jobs_.back().kind == "revise") suggestJob = jobs_.back().id;
    }
    // Each finding on the list says how its fix goes: done (a direct fix: off the list now), or
    // fixing by a job (off the list when that job has made its item; back to open if it fails).
    auto jobFor = [&](const std::string& kind, const std::string& subject) -> std::string {
        for (auto it = jobs_.rbegin(); it != jobs_.rend(); ++it) {
            if (it->kind == kind && it->subject == subject && (it->status == "queued" || it->status == "running")) return it->id;
        }
        return {};
    };
    auto jobOfDoc = [&](const std::string& doc) -> std::string {
        if (doc.rfind("characters/", 0) == 0) return jobFor("character", "character/" + PlanSlug(doc));
        if (doc.rfind("places/", 0) == 0) return jobFor("place", "location/" + PlanSlug(doc));
        if (doc.rfind("story/scenes/", 0) == 0) return jobFor("scene", PlanSlug(doc));
        if (doc.rfind("story/acts/", 0) == 0) return jobFor("act", PlanSlug(doc));
        if (doc.rfind("quests/", 0) == 0) {
            const json q = store_.Read(doc).value_or(json::object());
            const std::string slot = q.is_object() && !Text(q, "slot").empty() ? Text(q, "slot") : PlanSlug(doc);
            const std::string id = jobFor("quest", slot);
            return id.empty() ? jobFor("quests", "quests") : id;
        }
        if (doc == "quests") return jobFor("quests", "quests");
        if (doc == "systems") return jobFor("systems", "systems");
        if (doc == "ui") return jobFor("visuals", "ui");
        if (doc == "outline") return jobFor("outline", "outline");
        return {};
    };
    for (auto& f : (*report)["findings"]) {
        const auto h = how.find(f.value("id", ""));
        if (h == how.end()) continue;
        if (h->second == "direct") {
            f["status"] = "fixed";
            continue;
        }
        std::string job;
        if (h->second.rfind("doc:", 0) == 0) job = jobOfDoc(h->second.substr(4));
        else if (h->second.rfind("job:", 0) == 0) {
            const std::string rest = h->second.substr(4);
            job = jobFor(rest.substr(0, rest.find(' ')), rest.substr(rest.find(' ') + 1));
        } else if (h->second == "suggest") job = suggestJob;
        f["status"] = "fixing";
        if (!job.empty()) f["fixJob"] = job;
    }
    store_.Touch("report", *report, "");
    {
        std::set<std::pair<std::string, std::string>> reviewed;
        for (const auto& f : report->value("findings", json::array())) {
            if (f.value("rule", json()) == "critic" && how.contains(f.value("id", ""))) reviewed.insert({f.value("subject", ""), f.value("message", "")});
        }
        if (auto review = store_.Read("review"); !reviewed.empty() && review && review->contains("findings") && (*review)["findings"].is_array()) {
            json kept = json::array();
            for (const auto& f : (*review)["findings"]) {
                const bool fixed = f.is_object() && reviewed.contains({Text(f, "subject"), Text(f, "message")});
                if (!fixed) kept.push_back(f);
            }
            (*review)["findings"] = kept;
            store_.Touch("review", *review, "");
        }
    }
    if (uiChanged) store_.Touch("ui", ui, "");
    if (systemsChanged) store_.Touch("systems", systems, "");
    if (graphChanged) SaveGraph(g);
    if (retry) RetryFailed();
    PlanDecision d;
    d.stage = "validate";
    d.actor = ids.empty() ? "rule" : "user";
    d.kind = "fix";
    d.subject = "report";
    for (const auto& x : done) d.chosen += (d.chosen.empty() ? "" : "; ") + x;
    d.rationale = "Findings with a known remedy, fixed; then validated again.";
    store_.Decide(d);
    // Validated again once the remakes are through (the report waits for them).
    stage_ = "validate";
    status_ = "working";
    message_ = "Fixing what validation found (" + std::to_string(done.size()) + "), then checking again…";
    if (std::none_of(jobs_.begin(), jobs_.end(), [](const Job& j) { return j.kind == "report" && j.status == "queued"; })) AddJob("report", "report");
    Save();
    Pump();
    return {};
}

std::string GamePlan::QuickFix(const std::string& doc, const std::vector<std::string>& problems) {
    if (store_.Locked(doc)) return doc + " is locked";
    std::string kind, subject, key;
    if (doc.rfind("characters/", 0) == 0) kind = "character", subject = "character/" + PlanSlug(doc), key = "character";
    else if (doc.rfind("places/", 0) == 0) kind = "place", subject = "location/" + PlanSlug(doc), key = "place";
    else if (doc.rfind("story/scenes/", 0) == 0) kind = "scene", subject = PlanSlug(doc), key = "scene";
    else if (doc.rfind("quests/", 0) == 0) {
        const json q = store_.Read(doc).value_or(json::object());
        kind = "quest", subject = q.is_object() && !Text(q, "slot").empty() ? Text(q, "slot") : PlanSlug(doc), key = "quests";
    } else if (doc == "quests") kind = "quests", subject = "quests", key = "quests";
    else if (doc == "systems") kind = "systems", subject = "systems", key = "systems";
    else if (doc == "ui") kind = "visuals", subject = "ui", key = "ui";
    else if (doc.rfind("story/acts/", 0) == 0) kind = "act", subject = PlanSlug(doc), key = "act";
    else if (doc == "outline") kind = "outline", subject = "outline", key = "outline";
    else return Regenerate(doc); // nothing to correct in place
    json content;
    if (kind == "quests") {
        content = json::array();
        for (const auto& q : store_.List("quests")) content.push_back(store_.Read(q).value_or(json::object()));
    } else if (kind == "quest") {
        content = json::array({store_.Read(doc).value_or(json::object())}); // the answer's shape: quests: [it]
    } else {
        content = store_.Read(doc).value_or(json());
    }
    if (content.is_null() || content.empty()) return Regenerate(doc);
    auto strip = [](json& d) {
        for (const char* k : {"revision", "madeBy", "inputs", "schema", "rationale", "locked", "designedBy"}) d.erase(k);
    };
    if (content.is_array()) {
        for (auto& d : content) {
            if (d.is_object()) strip(d);
        }
    } else if (content.is_object()) {
        strip(content);
    }
    for (const auto& j : jobs_) {
        if (j.kind == kind && j.subject == subject && (j.status == "queued" || j.status == "running")) return {};
    }
    if (kind == "scene") UndoScene(subject);
    PlanDecision d;
    d.stage = StageOfJob(kind);
    d.actor = "rule";
    d.kind = "quick-fix";
    d.subject = doc;
    for (const auto& p : problems) d.chosen += (d.chosen.empty() ? "" : "; ") + p;
    store_.Decide(d);
    Job* job = FindJob(AddJob(kind, subject));
    job->problems = json(problems);
    job->lastAnswer = {{key, content}};
    return {};
}

std::string GamePlan::AcceptFinding(const std::string& finding, const std::string& reason) {
    auto report = store_.Read("report");
    if (!report) return "there's no report yet";
    bool found = false;
    for (auto& f : (*report)["findings"]) {
        if (f.value("id", "") == finding) {
            f["status"] = "accepted";
            f["reason"] = reason;
            found = true;
        }
    }
    if (!found) return "no finding " + finding;
    PlanDecision d;
    d.stage = "validate";
    d.actor = "user";
    d.kind = "accept";
    d.subject = finding;
    d.reason = reason;
    store_.Touch("report", *report, store_.Decide(d));
    BuildReport(store_.Read("review").value_or(json::object()));
    return {};
}

} // namespace aige
