// The Planner's stages (development_plan.md 22.2-22.9): what each job asks the AI service,
// and what each answer becomes: documents, graph facts, World Bible entries, decisions.
#include "planner/GamePlan.h"

#include "places/Brief.h"
#include "places/Ontology.h"
#include "story/StoryRules.h"
#include "world/WorldBible.h"

#include <algorithm>
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

json Vocab(const StoryOntology& gso, std::initializer_list<const char*> schemes) {
    json out = json::object();
    for (const char* s : schemes) {
        auto it = gso.vocabularies.find(s);
        if (it != gso.vocabularies.end()) out[s] = it->second;
    }
    return out;
}

std::string Text(const json& j, const char* key) {
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
        {"quests", "/planner/quests"},     {"place", "/planner/place"},     {"systems", "/planner/systems"},
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
    if (job.kind == "quests") {
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
        return {{"brief", brief}, {"skeleton", store_.Read("quests/q-main").value_or(json::object())}, {"characters", characters},
                {"places", sites}, {"scenes", scenes}, {"sideQuests", target}, {"mode", profile ? profile->stages.value("quests", "full") : "full"},
                {"keep", keep}, {"strategies", gso_.strategies}, {"vocab", Vocab(gso_, {"ObjectiveAction", "Motivation"})}};
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
                std::string name = d.value("name", d.value("summary", ""));
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
        const json scene = body.value("scene", json::object());
        const auto problems = ValidateSceneDoc(scene, gso_);
        if (HasErrors(problems)) return ProblemTexts(problems);
        return HandleScene(job, scene, rationale.empty() ? scene.value("rationale", "") : rationale);
    }
    if (job.kind == "critic") return HandleCritic(job, body, decide("critic", "story/acts/" + job.subject, ""));
    if (job.kind == "character") {
        json c = body.value("character", json::object());
        const auto problems = ValidateCharacterDoc(c, gso_, Strings(host_.catalog.value("packageTemplates", json::array())));
        if (HasErrors(problems)) return ProblemTexts(problems);
        return HandleCharacter(job, c, rationale.empty() ? c.value("rationale", "") : rationale);
    }
    if (job.kind == "quests") return HandleQuests(job, body.value("quests", json::array()), rationale);
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
        if (made == 0 && !problems.empty()) return problems;
        for (const auto& p : problems) Finding(stage_, "revise", p);
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

std::vector<std::string> GamePlan::HandleScene(Job& job, const json& scene, const std::string& rationale) {
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
    StoryGraph next = g;
    if (StoryNode* n = next.Find(id)) n->fields["summary"] = scene.value("summary", "");
    const std::string problem = next.Apply({{"nodes", nodes}, {"assert", facts}, {"retract", ends}}, gso_);
    if (!problem.empty()) return {problem};
    // The continuity rules on what it added (a violation goes back once).
    std::vector<std::string> problems;
    for (const auto& i : NewErrors(before, CheckStory(next, gso_))) {
        if (i.rule.rfind("R-T", 0) == 0 || i.rule == "R-K3" || i.rule == "shape") problems.push_back(i.rule + ": " + i.message);
    }
    if (!problems.empty()) return problems;

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
    if (!questStage.is_object() && card.value("kernel", false) && card.value("mode", "") == "gameplay" && branch.empty()) {
        questStage = {{"quest", "q-main"}, {"journal", card.value("goal", scene.value("summary", ""))}};
    }
    if (questStage.is_object() && branch.empty()) {
        json q = store_.Read("quests/q-main").value_or(json{{"schema", "aige.quest-design/1"}, {"id", "q-main"}, {"type", "main"},
                                                          {"name", Brief().value("title", "The main quest")}, {"stages", json::array()}});
        int last = 0;
        for (const auto& st : q["stages"]) last = std::max(last, st.value("index", 0));
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
        UndoScene(scene);
        (*s)["status"] = "card";
        store_.Touch(doc, *s, decision);
        AddJob("scene", scene);
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

std::vector<std::string> GamePlan::HandleQuests(Job&, const json& quests, const std::string& rationale) {
    if (!quests.is_array() || quests.empty()) return {"no quests"};
    StoryGraph g = Graph();
    std::vector<std::string> known;
    for (const auto& n : g.nodes) known.push_back(n.id);
    std::vector<std::string> problems;
    std::vector<json> designs;
    const json skeleton = store_.Read("quests/q-main").value_or(json::object());
    for (auto q : quests) {
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
        if (store_.Locked("quests/" + q.value("id", ""))) continue;
        for (const auto& p : ProblemTexts(ValidateQuestDesign(q, gso_, known))) problems.push_back(q.value("id", "?") + ": " + p);
        designs.push_back(q);
    }
    if (!problems.empty()) return problems;
    // Out with what the last quest design put in the graph.
    for (const auto& doc : store_.List("quests")) {
        if (store_.Locked(doc)) continue;
        const json old = store_.Read(doc).value_or(json::object());
        if (old.value("kind", "") != "skeleton") RemoveMadeBy(g, old.value("designedBy", ""), gso_, true);
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
    if (!problems.empty()) return problems;
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
        if (!written.contains(doc) && !store_.Locked(doc)) store_.Remove(doc);
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
    if (host_.places) brief = CompleteBrief(host_.places(), brief);
    p["brief"] = brief;
    if (host_.places) {
        const auto problems = ValidatePlaceDesc(p, host_.places());
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

std::vector<std::string> GamePlan::HandleSystems(Job&, const json& systems, const std::string& rationale) {
    std::vector<std::string> problems;
    for (const auto& r : systems.value("records", json::array())) {
        const std::string type = r.value("type", ""), id = r.value("id", "");
        if (type.empty() || id.empty()) {
            problems.push_back("a record needs a type and an id");
            continue;
        }
        if (host_.validateRecord) {
            for (const auto& p : host_.validateRecord(type, r.value("record", json::object()))) problems.push_back(type + "/" + id + ": " + p);
        }
    }
    if (!problems.empty()) return problems;
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
            add("X-character", important ? "error" : "warning", c, (n->Text("name").empty() ? c : n->Text("name")) + " takes part in " + s + " but has no character document (④)");
        }
    }
    for (const auto* n : g.OfClass("Location", gso_)) {
        if (!store_.Exists("places/" + PlanSlug(n->id))) add("X-place", "error", n->id, n->id + " has no place description (⑥)");
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
            if (!item.empty() && !items.contains(item)) add("X-item", "warning", q.value("id", ""), "reward item '" + item + "' has no record (⑦)");
        }
    }
    if (!BriefGet(brief, "systems.abilities.on").is_boolean() || !BriefGet(brief, "systems.abilities.on").get<bool>()) {
        for (const auto& doc : store_.List("characters")) {
            const json c = store_.Read(doc).value_or(json::object());
            if (!c.value("gameplay", json::object()).value("abilities", json::array()).empty()) {
                add("X-system", "error", c.value("id", ""), c.value("name", "") + " has abilities, but abilities are off in the Brief");
            }
        }
    }
    const json ui = store_.Read("ui").value_or(json::object());
    const auto screens = Strings(ui.value("screens", json::array()));
    auto needScreen = [&](bool on, const std::string& screen, const std::string& why, const std::string& severity = "error") {
        if (on && !ui.empty() && std::find(screens.begin(), screens.end(), screen) == screens.end()) {
            add("X-screen", severity, "ui", "the " + screen + " window is needed (" + why + ")");
        }
    };
    auto on = [&](const char* path) { return BriefGet(brief, path).is_boolean() && BriefGet(brief, path).get<bool>(); };
    needScreen(on("systems.inventory.on"), "inventory", "inventory is on");
    needScreen(!store_.List("quests").empty(), "journal", "the game has quests");
    needScreen(on("systems.abilities.magic"), "spellbook", "magic is on");
    needScreen(g.OfClass("Location", gso_).size() > 1, "map", "more than one place", "warning");
    // 3. What the stages couldn't fix.
    for (const auto& f : findings_) add("stage", f.value("severity", "warning"), f.value("subject", ""), f.value("message", ""));
    // 4. The critic.
    for (const auto& f : review.value("findings", json::array())) {
        add("critic", f.value("severity", "warning") == "error" ? "error" : "warning", f.value("subject", ""), f.value("message", ""), f.value("fix", json()));
    }
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
        message_ = std::to_string(errors) + " errors to fix before building: regenerate what they name, or accept them with a reason.";
    }
    Save();
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
