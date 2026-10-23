#include "story/StoryRules.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <set>

namespace aige {

namespace {

class Checker {
public:
    Checker(const StoryGraph& g, const StoryOntology& o, std::vector<StoryIssue>& out) : g_(g), o_(o), out_(out) {}

    void Add(const std::string& rule, const std::string& node, const std::map<std::string, std::string>& vars) {
        const StoryRuleDef* r = o_.Rule(rule);
        std::string message = r ? r->message : rule;
        for (const auto& [k, v] : vars) {
            const std::string key = "{" + k + "}";
            for (size_t at = message.find(key); at != std::string::npos; at = message.find(key, at + v.size())) message.replace(at, key.size(), v);
        }
        out_.push_back({rule, r ? r->severity : "warning", message, node});
    }
    void Shape(const std::string& node, const std::string& message, const char* severity = "error") {
        out_.push_back({"shape", severity, message, node});
    }

    void Run() {
        Shapes();
        Structure();
        Causality();
        Continuity();
        Characters();
        Quests();
        Places();
    }

private:
    const StoryGraph& g_;
    const StoryOntology& o_;
    std::vector<StoryIssue>& out_;

    std::string Name(const std::string& id) const {
        const StoryNode* n = g_.Find(id);
        return n && !n->Text("name").empty() ? n->Text("name") : id;
    }
    bool IsMainLine(const StoryNode& scene) const {
        const std::string mode = scene.Text("mode");
        return scene.Text("branch").empty() && mode != "discoverable" && mode != "ambient";
    }
    std::vector<std::string> MainLine() const {
        std::vector<std::string> out;
        for (const auto& id : g_.SceneOrder()) {
            if (IsMainLine(*g_.Find(id))) out.push_back(id);
        }
        return out;
    }

    // --- Shapes -------------------------------------------------------------------------------
    void Shapes() {
        for (const auto& n : g_.nodes) {
            const StoryClassDef* cls = o_.Class(n.cls);
            if (!cls) {
                Shape(n.id, n.id + ": no class '" + n.cls + "'");
                continue;
            }
            for (const auto& field : cls->required) {
                if (!n.fields.contains(field)) Shape(n.id, n.id + " (" + n.cls + ") needs '" + field + "'");
            }
            for (const auto& [field, value] : n.fields.items()) {
                const std::string type = o_.FieldType(n.cls, field);
                if (type.empty() || !o_.vocabularies.contains(type) || !value.is_string()) continue;
                if (!o_.InVocabulary(type, value.get<std::string>())) {
                    Shape(n.id, n.id + ": " + field + " '" + value.get<std::string>() + "' isn't a " + type);
                }
            }
            if (o_.IsA(n.cls, "Character")) {
                for (const auto& m : n.List("motivations")) {
                    if (!o_.InVocabulary("Motivation", m)) Shape(n.id, n.id + ": motivation '" + m + "' isn't a Motivation");
                }
                for (const auto& t : n.List("traits")) {
                    if (!o_.InVocabulary("Trait", t)) Shape(n.id, n.id + ": trait '" + t + "' isn't a Trait", "warning");
                }
            }
        }
        for (const auto& e : g_.edges) {
            const StoryPropertyDef* p = o_.Property(e.p);
            if (!p) {
                Shape(e.s, e.s + ": no property '" + e.p + "'");
                continue;
            }
            const StoryNode* s = g_.Find(e.s);
            if (!s) {
                Shape(e.s, e.p + ": no node " + e.s);
                continue;
            }
            if (!p->domain.empty() && std::none_of(p->domain.begin(), p->domain.end(), [&](const std::string& c) { return o_.IsA(s->cls, c); })) {
                Shape(e.s, e.p + ": " + e.s + " is a " + s->cls + ", not one of its subjects");
            }
            if (!p->range.empty()) {
                const StoryNode* obj = g_.Find(e.o);
                if (!obj && !(e.o.empty() && !e.Text("about").empty())) Shape(e.s, e.p + " from " + e.s + ": no node " + e.o);
                else if (obj && std::none_of(p->range.begin(), p->range.end(), [&](const std::string& c) { return o_.IsA(obj->cls, c); })) {
                    Shape(e.s, e.p + ": " + e.o + " is a " + obj->cls + ", not one of its objects");
                }
            }
            if (e.p == "hasRelationship" && !o_.InVocabulary("RelationshipType", e.Text("type"))) {
                Shape(e.s, e.s + " → " + e.o + ": relationship type '" + e.Text("type") + "' isn't a RelationshipType");
            }
        }
    }

    // --- Structure ------------------------------------------------------------------------------
    std::vector<std::string> Successors(const std::string& id, bool implicit, const std::vector<std::string>& mainLine) const {
        std::vector<std::string> out;
        for (const auto* e : g_.Out(id)) {
            if (e->p == "next" || e->p == "branchesTo" || e->p == "hasChoice") out.push_back(e->o);
        }
        if (implicit) {
            auto it = std::find(mainLine.begin(), mainLine.end(), id);
            if (it != mainLine.end() && it + 1 != mainLine.end()) out.push_back(*(it + 1));
        }
        return out;
    }

    void Structure() {
        const auto mainLine = MainLine();
        const auto scenes = g_.SceneOrder();
        // R-S1: acts with scenes, in the template's order.
        std::vector<std::pair<double, std::string>> acts;
        for (const auto& n : g_.nodes) {
            if (n.cls == "Act") {
                const auto order = n.fields.find("order");
                acts.push_back({order != n.fields.end() && order->is_number() ? order->get<double>() : 0.0, n.id});
            }
        }
        std::sort(acts.begin(), acts.end());
        double lastFirst = -1e9;
        for (const auto& [order, act] : acts) {
            double first = 1e18;
            for (const auto& s : scenes) {
                const StoryNode* n = g_.Find(s);
                bool in = n->Text("act") == act;
                for (const auto* e : g_.Out(s, "partOf")) in = in || e->o == act;
                if (in) first = std::min(first, g_.OrderOf(s));
            }
            if (first == 1e18) Add("R-S1", act, {{"act", act}});
            else if (first < lastFirst) Add("R-S1", act, {{"act", act + " (its scenes come before the previous act's)"}});
            else lastFirst = first;
        }
        if (scenes.empty()) return;
        // Explicit discourse edges, or (none at all) the main line by order.
        const bool implicit = std::none_of(g_.edges.begin(), g_.edges.end(), [](const StoryEdge& e) {
            return e.p == "next" || e.p == "branchesTo" || e.p == "hasChoice";
        });
        // R-S2: reachable from the first scene.
        std::set<std::string> reached;
        if (!mainLine.empty()) {
            std::deque<std::string> queue{mainLine.front()};
            reached.insert(mainLine.front());
            while (!queue.empty()) {
                const std::string x = queue.front();
                queue.pop_front();
                for (const auto& y : Successors(x, implicit, mainLine)) {
                    if (reached.insert(y).second) queue.push_back(y);
                }
            }
            for (const auto& s : mainLine) {
                if (!reached.contains(s)) Add("R-S2", s, {{"scene", s}});
            }
        }
        // R-S3: everything reaches an ending or a convergence (or, with none, the last scene).
        std::set<std::string> terminals;
        for (const auto& n : g_.nodes) {
            if (n.cls == "Ending" || n.cls == "Convergence") terminals.insert(n.id);
        }
        if (terminals.empty() && !mainLine.empty()) terminals.insert(mainLine.back());
        std::set<std::string> good = terminals;
        for (bool grew = true; grew;) {
            grew = false;
            for (const auto& n : g_.nodes) {
                if (good.contains(n.id)) continue;
                for (const auto& y : Successors(n.id, implicit, mainLine)) {
                    if (good.contains(y)) {
                        good.insert(n.id);
                        grew = true;
                        break;
                    }
                }
            }
        }
        for (const auto& s : scenes) {
            if (!good.contains(s)) Add("R-S3", s, {{"scene", s}});
        }
        // R-S4: every option of a choice changes something.
        for (const auto& n : g_.nodes) {
            if (n.cls != "ChoicePoint") continue;
            for (const auto& option : n.List("options")) {
                const auto out = g_.Out(n.id);
                const bool has = std::any_of(out.begin(), out.end(), [&](const StoryEdge* e) { return e->Text("option") == option; });
                if (!has) Add("R-S4", n.id, {{"choice", n.id}, {"option", option}});
            }
        }
        // R-S5: the climax late; R-S6: kernel scenes turn their value.
        for (size_t i = 0; i < mainLine.size(); ++i) {
            const StoryNode* n = g_.Find(mainLine[i]);
            if (n->Flag("climax") && mainLine.size() > 1) {
                const double at = static_cast<double>(i) / static_cast<double>(mainLine.size() - 1);
                if (at < 0.75) Add("R-S5", n->id, {{"scene", n->id}, {"percent", std::to_string(static_cast<int>(at * 100 + 0.5))}});
            }
        }
        for (const auto& s : scenes) {
            const StoryNode* n = g_.Find(s);
            const auto b = n->fields.find("chargeBefore"), a = n->fields.find("chargeAfter");
            if (n->Flag("kernel") && b != n->fields.end() && a != n->fields.end() && b->is_number() && a->is_number() &&
                b->get<double>() == a->get<double>()) {
                Add("R-S6", s, {{"scene", s}, {"charge", b->dump()}});
            }
        }
    }

    // --- Causality -------------------------------------------------------------------------------
    void Causality() {
        std::set<std::string> kernelScenes;
        for (const auto& n : g_.nodes) {
            if (n.cls == "StoryScene" && n.Flag("kernel")) kernelScenes.insert(n.id);
        }
        for (const auto& n : g_.nodes) {
            if (o_.IsA(n.cls, "Event") && n.Flag("kernel") && !n.Flag("inciting") && g_.In(n.id, "causes").empty() &&
                g_.In(n.id, "enables").empty()) {
                Add("R-C1", n.id, {{"event", n.id}});
            }
            if (o_.IsA(n.cls, "Action")) {
                const auto shown = g_.In(n.id, "presents");
                const bool inKernel = std::any_of(shown.begin(), shown.end(), [&](const StoryEdge* e) { return kernelScenes.contains(e->s); });
                if (inKernel && g_.Out(n.id, "serves").empty()) Add("R-C2", n.id, {{"event", n.id}});
            }
            if (n.Flag("setup") && !n.Flag("openEnded") && g_.In(n.id, "paysOff").empty()) Add("R-C3", n.id, {{"node", n.id}});
            if (n.cls == "Conflict" && !n.Flag("openEnded") && g_.In(n.id, "resolves").empty()) Add("R-C4", n.id, {{"node", n.id}});
        }
    }

    // --- Continuity ------------------------------------------------------------------------------
    bool Connected(const std::string& a, const std::string& b) const {
        std::set<std::string> seen{a};
        std::deque<std::string> queue{a};
        while (!queue.empty()) {
            const std::string x = queue.front();
            queue.pop_front();
            if (x == b) return true;
            for (const auto& e : g_.edges) {
                if (e.p != "connects" && e.p != "inside") continue;
                const std::string* y = e.s == x ? &e.o : e.o == x ? &e.s : nullptr;
                if (y && seen.insert(*y).second) queue.push_back(*y);
            }
        }
        return false;
    }

    bool DiesIn(const std::string& character, const std::string& scene) const {
        for (const auto* e : g_.Out(character, "state")) {
            const std::string state = e->Text("state");
            if ((state == "dead" || state == "destroyed") && e->from == scene) return true;
        }
        return false;
    }

    void Continuity() {
        const auto scenes = g_.SceneOrder();
        for (const auto& s : scenes) {
            const StoryNode* scene = g_.Find(s);
            for (const auto& c : g_.Participants(s)) {
                const StoryNode* n = g_.Find(c);
                if (!n || !o_.IsA(n->cls, "Character")) continue;
                // R-T1: the dead don't take part (flashbacks, visions and the undead aside; dying in
                // the scene is taking part while alive).
                if (!scene->Flag("flashback") && !n->Flag("undead") && !g_.AliveAt(c, s) && !DiesIn(c, s)) {
                    Add("R-T1", s, {{"character", Name(c)}, {"scene", s}});
                }
            }
        }
        // R-T2: one place at a time.
        std::set<std::string> placed;
        for (const auto& e : g_.edges) {
            if (e.p == "locatedAt") placed.insert(e.s);
        }
        for (const auto& c : placed) {
            for (const auto& s : scenes) {
                const auto where = g_.LocationsAt(c, s);
                if (where.size() > 1) {
                    Add("R-T2", c, {{"character", Name(c)}, {"count", std::to_string(where.size())}, {"scene", s}});
                    break;
                }
            }
        }
        // R-T3: speaking only of what one knows.
        for (const auto& e : g_.edges) {
            if (e.p != "mentions") continue;
            const std::string about = e.o.empty() ? e.Text("about") : e.o, scene = e.Text("scene");
            if (!scene.empty() && !g_.KnowsAt(e.s, about, scene)) Add("R-T3", e.s, {{"character", Name(e.s)}, {"about", about}, {"scene", scene}});
        }
        // R-T4: a unique item has one possessor.
        for (const auto& n : g_.nodes) {
            if (n.cls != "Item" || !n.Flag("unique")) continue;
            for (const auto& s : scenes) {
                const auto who = g_.PossessorsAt(n.id, s);
                if (who.size() > 1) {
                    Add("R-T4", n.id, {{"item", Name(n.id)}, {"count", std::to_string(who.size())}, {"scene", s}});
                    break;
                }
            }
        }
        // R-T5: a relationship that changes is shown changing.
        for (const auto& e : g_.edges) {
            if (e.p != "hasRelationship" || e.from.empty() || g_.OrderOf(e.from) < 0) continue;
            std::vector<std::string> present = g_.Participants(e.from);
            for (const auto* shown : g_.Out(e.from, "presents")) {
                for (const auto* p : g_.Out(shown->o, "participant")) present.push_back(p->o);
                for (const auto* a : g_.Out(shown->o)) {
                    if (a->p == "agent") present.push_back(a->o);
                }
                if (const StoryNode* ev = g_.Find(shown->o); ev && !ev->Text("agent").empty()) present.push_back(ev->Text("agent"));
            }
            const bool shown = std::find(present.begin(), present.end(), e.s) != present.end() ||
                               std::find(present.begin(), present.end(), e.o) != present.end();
            if (!shown) Add("R-T5", e.s, {{"subject", Name(e.s)}, {"object", Name(e.o)}, {"scene", e.from}});
        }
        // R-T6: travel between consecutive scenes is possible.
        const bool mapped = std::any_of(g_.edges.begin(), g_.edges.end(), [](const StoryEdge& e) { return e.p == "connects"; });
        if (!mapped) return;
        const auto mainLine = MainLine();
        std::map<std::string, std::string> lastPlace;
        std::set<std::string> reported;
        for (const auto& s : mainLine) {
            const std::string here = g_.LocationOfScene(s);
            if (here.empty()) continue;
            for (const auto& c : g_.Participants(s)) {
                auto it = lastPlace.find(c);
                if (it != lastPlace.end() && it->second != here && !Connected(it->second, here) && reported.insert(c).second) {
                    Add("R-T6", c, {{"character", Name(c)}, {"from", it->second}, {"to", here}});
                }
                lastPlace[c] = here;
            }
        }
    }

    // --- Characters ------------------------------------------------------------------------------
    void Characters() {
        // R-K1: the template's roles are filled.
        for (const auto& n : g_.nodes) {
            if (n.cls != "Story") continue;
            auto t = o_.templates.find(n.Text("structure"));
            if (t == o_.templates.end()) continue;
            for (const auto& role : t->second.roles) {
                bool filled = false;
                for (const auto& e : g_.edges) {
                    if (e.p != "fills") continue;
                    const StoryNode* r = g_.Find(e.o);
                    if (r && (r->Text("archetype") == role || r->id == "role/" + role)) filled = true;
                }
                if (!filled) Add("R-K1", n.id, {{"role", role}});
            }
        }
        std::vector<const StoryNode*> major;
        for (const auto* c : g_.OfClass("Character", o_)) {
            if (c->Text("importance") == "major") major.push_back(c);
            // R-K2: major characters are rounded.
            if (c->Text("importance") == "major") {
                std::vector<std::string> missing;
                if (c->Text("need").empty()) missing.push_back("need");
                if (c->Text("desire").empty()) missing.push_back("desire");
                if (c->List("motivations").empty()) missing.push_back("motivation");
                if (c->Text("arc").empty() && !c->Flag("static")) missing.push_back("arc");
                if (!missing.empty()) {
                    std::string list;
                    for (const auto& m : missing) list += (list.empty() ? "" : ", ") + m;
                    Add("R-K2", c->id, {{"character", Name(c->id)}, {"missing", list}});
                }
            }
            // R-K6: placed characters have something to do.
            if (c->cls != "PlayerCharacter" && !g_.Out(c->id, "locatedAt").empty() && c->Text("importance") != "extra" &&
                c->Text("package").empty() && !c->Flag("idle")) {
                Add("R-K6", c->id, {{"character", Name(c->id)}});
            }
        }
        // R-K3: relationships both ways.
        for (const auto& e : g_.edges) {
            if (e.p != "hasRelationship") continue;
            auto inv = o_.inverse.find(e.Text("type"));
            if (inv == o_.inverse.end()) continue;
            const auto back = g_.Out(e.o, "hasRelationship");
            const bool ok = std::any_of(back.begin(), back.end(), [&](const StoryEdge* b) { return b->o == e.s && b->Text("type") == inv->second; });
            if (!ok) Add("R-K3", e.s, {{"subject", Name(e.s)}, {"type", e.Text("type")}, {"object", Name(e.o)}, {"inverse", inv->second}});
        }
        // R-K4: distinguishable names.
        auto lower = [](std::string s) {
            for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return s;
        };
        for (size_t i = 0; i < major.size(); ++i) {
            for (size_t j = i + 1; j < major.size(); ++j) {
                const std::string a = lower(Name(major[i]->id)), b = lower(Name(major[j]->id));
                if (a.size() >= 3 && b.size() >= 3 && a.substr(0, 3) == b.substr(0, 3)) {
                    Add("R-K4", major[j]->id, {{"a", Name(major[i]->id)}, {"b", Name(major[j]->id)}});
                }
            }
        }
        // R-K5: gameplay roles have what they need.
        static const std::map<std::string, std::pair<const char*, const char*>> needs = {
            {"vendor", {"stock", "stock"}}, {"trainer", {"skills", "skills to teach"}}, {"companion", {"package", "AI package"}},
            {"boss", {"statClass", "fight (a stat class)"}}};
        for (const auto& e : g_.edges) {
            if (e.p != "fills") continue;
            const StoryNode* role = g_.Find(e.o);
            const StoryNode* c = g_.Find(e.s);
            if (!role || !c || role->cls != "GameplayRole") continue;
            std::string r = role->Text("role");
            if (r.empty()) r = role->id.substr(role->id.find_last_of('/') + 1);
            auto it = needs.find(r);
            if (it != needs.end() && c->Text(it->second.first).empty()) {
                Add("R-K5", c->id, {{"character", Name(c->id)}, {"role", r}, {"missing", it->second.second}});
            }
        }
    }

    // --- Quests --------------------------------------------------------------------------------
    void Quests() {
        const auto mainLine = MainLine();
        // R-Q1: kernel gameplay scenes are played by the main quest.
        for (const auto& s : mainLine) {
            const StoryNode* n = g_.Find(s);
            if (!n->Flag("kernel") || n->Text("mode") != "gameplay") continue;
            bool played = false;
            for (const auto* e : g_.In(s, "advancesScene")) {
                const StoryNode* q = g_.Find(g_.QuestOfStage(e->s));
                if (q && q->Text("type") == "main") played = true;
            }
            if (!played) Add("R-Q1", s, {{"scene", s}});
        }
        // R-Q2: aliases have a fill.
        for (const auto& n : g_.nodes) {
            if (n.cls != "Alias") continue;
            const std::string fill = n.Text("fill");
            std::string quest;
            for (const auto* e : g_.In(n.id, "hasAlias")) quest = e->s;
            if (fill.empty() || (fill.find('/') != std::string::npos && fill.find(' ') == std::string::npos && !g_.Find(fill))) {
                Add("R-Q2", n.id, {{"alias", n.id}, {"quest", quest.empty() ? "its quest" : quest}});
            }
        }
        // R-Q3 / R-Q7: needed characters are alive when needed.
        auto excludes = [&](const std::string& a, const std::string& b) {
            for (const auto& e : g_.edges) {
                if (e.p == "excludes" && ((e.s == a && e.o == b) || (e.s == b && e.o == a))) return true;
            }
            return false;
        };
        std::set<std::string> reported;
        for (const auto& e : g_.edges) {
            if (e.p != "needsCharacter") continue;
            const StoryNode* who = g_.Find(e.s);
            const std::string quest = who && who->cls == "QuestStage" ? g_.QuestOfStage(e.s) : e.s;
            std::vector<std::string> when;
            if (!e.Text("at").empty()) when.push_back(e.Text("at"));
            else if (who && who->cls == "QuestStage") when = g_.ScenesAdvancedBy(e.s);
            for (const auto& w : when) {
                if (g_.AliveAt(e.o, w)) continue;
                std::string byQuest;
                for (const auto* d : g_.Out(e.o, "state")) {
                    if ((d->Text("state") == "dead" || d->Text("state") == "destroyed") && g_.Holds(*d, w)) byQuest = d->Text("byQuest");
                }
                const std::string key = quest + "|" + e.o;
                if (!reported.insert(key).second) continue;
                if (byQuest.empty()) Add("R-Q3", quest, {{"quest", quest}, {"character", Name(e.o)}, {"scene", w}});
                else if (byQuest != quest && !excludes(byQuest, quest)) Add("R-Q7", quest, {{"quest", quest}, {"character", Name(e.o)}, {"scene", w}});
            }
        }
        // R-Q4: givers want what their quests are about.
        for (const auto& e : g_.edges) {
            if (e.p != "gives") continue;
            const StoryNode* giver = g_.Find(e.s);
            const StoryNode* quest = g_.Find(e.o);
            if (!giver || !quest || quest->Text("motivation").empty()) continue;
            const auto wants = giver->List("motivations");
            if (std::find(wants.begin(), wants.end(), quest->Text("motivation")) == wants.end()) {
                Add("R-Q4", e.o, {{"giver", Name(e.s)}, {"quest", e.o}, {"motivation", quest->Text("motivation")}});
            }
        }
    }

    // --- Places --------------------------------------------------------------------------------
    void Places() {
        for (const auto& s : g_.SceneOrder()) {
            const std::string loc = g_.LocationOfScene(s);
            if (loc.empty()) continue;
            const StoryNode* n = g_.Find(loc);
            // R-P1: the scene's location exists, with a build type.
            if (!n) Add("R-P1", s, {{"scene", s}, {"location", loc}, {"problem", "doesn't exist"}});
            else if (n->cls != "Location") Add("R-P1", s, {{"scene", s}, {"location", loc}, {"problem", "isn't a location"}});
            else if (n->Text("buildType").empty()) Add("R-P1", s, {{"scene", s}, {"location", loc}, {"problem", "has no build type"}});
            // R-P3: the parts the scene needs are there.
            if (n) {
                const auto parts = n->List("requiredParts");
                for (const auto& part : g_.Find(s)->List("needsParts")) {
                    if (std::find(parts.begin(), parts.end(), part) == parts.end()) Add("R-P3", s, {{"scene", s}, {"part", part}, {"location", loc}});
                }
            }
        }
        // R-P2: every location reachable from the start.
        std::string start;
        for (const auto& n : g_.nodes) {
            if (n.cls == "Location" && n.Flag("start")) start = n.id;
        }
        if (start.empty()) return;
        for (const auto& n : g_.nodes) {
            if (n.cls == "Location" && n.id != start && !Connected(start, n.id)) Add("R-P2", n.id, {{"location", Name(n.id)}});
        }
    }
};

} // namespace

std::vector<StoryIssue> CheckStory(const StoryGraph& graph, const StoryOntology& ontology) {
    std::vector<StoryIssue> out;
    Checker(graph, ontology, out).Run();
    return out;
}

std::vector<StoryIssue> NewErrors(const std::vector<StoryIssue>& before, const std::vector<StoryIssue>& after) {
    std::vector<StoryIssue> out;
    for (const auto& a : after) {
        if (a.severity != "error") continue;
        const bool old = std::any_of(before.begin(), before.end(), [&](const StoryIssue& b) { return b.rule == a.rule && b.message == a.message; });
        if (!old) out.push_back(a);
    }
    return out;
}

nlohmann::json IssuesJson(const std::vector<StoryIssue>& issues) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& i : issues) out.push_back({{"rule", i.rule}, {"severity", i.severity}, {"message", i.message}, {"node", i.node}});
    return out;
}

} // namespace aige
