#include "story/StoryGraph.h"

#include "world/WorldBible.h"

#include <algorithm>
#include <deque>
#include <fstream>
#include <tuple>

namespace aige {

namespace {

const char* kEdgeKeys[] = {"p", "s", "o", "from", "until", "branch", "madeBy"};

bool IsEdgeKey(const std::string& key) {
    for (const char* k : kEdgeKeys) {
        if (key == k) return true;
    }
    return false;
}

std::string Underscored(std::string s) {
    std::replace(s.begin(), s.end(), '-', '_');
    return s;
}

std::string Dashed(std::string s) {
    std::replace(s.begin(), s.end(), '_', '-');
    return s;
}

void SortUnique(std::vector<std::string>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

const std::map<std::string, std::string>& KindClasses() {
    static const std::map<std::string, std::string> m = {{"character", "Character"}, {"faction", "Faction"}, {"location", "Location"},
                                                         {"item", "Item"},           {"event", "Event"},     {"lore", "Lore"}};
    return m;
}

} // namespace

// --- Nodes and edges ------------------------------------------------------------------------

std::string StoryNode::Text(const char* key) const {
    auto it = fields.find(key);
    if (it == fields.end()) return {};
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number()) return it->dump();
    return {};
}

bool StoryNode::Flag(const char* key) const {
    auto it = fields.find(key);
    return it != fields.end() && it->is_boolean() && it->get<bool>();
}

std::vector<std::string> StoryNode::List(const char* key) const {
    std::vector<std::string> out;
    auto it = fields.find(key);
    if (it == fields.end()) return out;
    if (it->is_string()) {
        if (!it->get<std::string>().empty()) out.push_back(it->get<std::string>());
        return out;
    }
    if (!it->is_array()) return out;
    for (const auto& v : *it) {
        if (v.is_string()) out.push_back(v.get<std::string>());
        else if (v.is_array() && !v.empty() && v[0].is_string()) out.push_back(v[0].get<std::string>()); // ["brave", 0.4]
        else if (v.is_object() && v.contains("id") && v["id"].is_string()) out.push_back(v["id"].get<std::string>());
    }
    return out;
}

std::string StoryEdge::Text(const char* key) const {
    auto it = fields.find(key);
    if (it == fields.end()) return {};
    if (it->is_string()) return it->get<std::string>();
    if (it->is_boolean()) return it->get<bool>() ? "true" : "false";
    if (it->is_number()) return it->dump();
    return {};
}

// --- Storage ----------------------------------------------------------------------------------

namespace {
// A key's text, or "" when it holds something else (an AI's "from": true): never a throw.
std::string text(const nlohmann::json& o, const char* key) {
    if (!o.is_object()) return {};
    auto it = o.find(key);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}
} // namespace

StoryGraph StoryGraph::FromJson(const nlohmann::json& json) {
    StoryGraph g;
    g.revision = json.value("revision", 0);
    for (const auto& n : json.value("nodes", nlohmann::json::array())) {
        if (!n.is_object() || !n.contains("id")) continue;
        StoryNode node;
        node.id = text(n, "id");
        node.cls = text(n, "class");
        for (const auto& [k, v] : n.items()) {
            if (k != "id" && k != "class") node.fields[k] = v;
        }
        g.nodes.push_back(std::move(node));
    }
    for (const auto& e : json.value("edges", nlohmann::json::array())) {
        if (!e.is_object()) continue;
        StoryEdge edge;
        edge.p = text(e, "p");
        edge.s = text(e, "s");
        edge.o = text(e, "o");
        edge.from = text(e, "from");
        edge.until = text(e, "until");
        edge.branch = text(e, "branch");
        edge.madeBy = text(e, "madeBy");
        for (const auto& [k, v] : e.items()) {
            if (!IsEdgeKey(k)) edge.fields[k] = v;
        }
        g.edges.push_back(std::move(edge));
    }
    return g;
}

nlohmann::json StoryGraph::ToJson() const {
    nlohmann::json nodesJson = nlohmann::json::array(), edgesJson = nlohmann::json::array();
    for (const auto& n : nodes) {
        nlohmann::json j = {{"id", n.id}, {"class", n.cls}};
        for (const auto& [k, v] : n.fields.items()) j[k] = v;
        nodesJson.push_back(std::move(j));
    }
    for (const auto& e : edges) {
        nlohmann::json j = {{"p", e.p}, {"s", e.s}};
        if (!e.o.empty()) j["o"] = e.o;
        for (const auto& [k, v] : e.fields.items()) j[k] = v;
        if (!e.from.empty()) j["from"] = e.from;
        if (!e.until.empty()) j["until"] = e.until;
        if (!e.branch.empty()) j["branch"] = e.branch;
        if (!e.madeBy.empty()) j["madeBy"] = e.madeBy;
        edgesJson.push_back(std::move(j));
    }
    return {{"revision", revision}, {"nodes", nodesJson}, {"edges", edgesJson}};
}

std::optional<StoryGraph> StoryGraph::Load(const std::filesystem::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "no " + path.filename().string();
        return std::nullopt;
    }
    try {
        return FromJson(nlohmann::json::parse(in));
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

bool StoryGraph::Save(const std::filesystem::path& path) const {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << ToJson().dump(2) << "\n";
    return static_cast<bool>(out);
}

std::filesystem::path StoryGraph::PathIn(const std::filesystem::path& projectRoot) { return projectRoot / "planner" / "kg.json"; }

const StoryNode* StoryGraph::Find(const std::string& id) const {
    for (const auto& n : nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

StoryNode* StoryGraph::Find(const std::string& id) {
    for (auto& n : nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

StoryNode& StoryGraph::Upsert(const std::string& id, const std::string& cls) {
    if (StoryNode* n = Find(id)) {
        if (!cls.empty()) n->cls = cls;
        return *n;
    }
    nodes.push_back({id, cls, nlohmann::json::object()});
    return nodes.back();
}

void StoryGraph::RemoveNode(const std::string& id) {
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const StoryNode& n) { return n.id == id; }), nodes.end());
    edges.erase(std::remove_if(edges.begin(), edges.end(), [&](const StoryEdge& e) { return e.s == id || e.o == id; }), edges.end());
}

std::vector<const StoryNode*> StoryGraph::OfClass(const std::string& cls, const StoryOntology& ontology) const {
    std::vector<const StoryNode*> out;
    for (const auto& n : nodes) {
        if (ontology.IsA(n.cls, cls)) out.push_back(&n);
    }
    return out;
}

std::vector<const StoryEdge*> StoryGraph::Out(const std::string& s, const std::string& p) const {
    std::vector<const StoryEdge*> out;
    for (const auto& e : edges) {
        if (e.s == s && (p.empty() || e.p == p)) out.push_back(&e);
    }
    return out;
}

std::vector<const StoryEdge*> StoryGraph::In(const std::string& o, const std::string& p) const {
    std::vector<const StoryEdge*> out;
    for (const auto& e : edges) {
        if (e.o == o && (p.empty() || e.p == p)) out.push_back(&e);
    }
    return out;
}

// --- Time -------------------------------------------------------------------------------------

std::vector<std::string> StoryGraph::SceneOrder() const {
    std::vector<std::pair<double, std::string>> scenes;
    for (const auto& n : nodes) {
        if (n.cls == "StoryScene") scenes.push_back({n.fields.value("order", 0.0), n.id});
    }
    std::sort(scenes.begin(), scenes.end());
    std::vector<std::string> out;
    for (auto& [order, id] : scenes) out.push_back(std::move(id));
    return out;
}

double StoryGraph::OrderOf(const std::string& scene) const {
    const StoryNode* n = Find(scene);
    if (!n || n->cls != "StoryScene") return -1.0;
    const auto it = n->fields.find("order");
    return it != n->fields.end() && it->is_number() ? it->get<double>() : 0.0;
}

std::string StoryGraph::BranchOf(const std::string& scene) const {
    const StoryNode* n = Find(scene);
    return n ? n->Text("branch") : std::string();
}

bool StoryGraph::Holds(const StoryEdge& edge, const std::string& scene, const std::string& branch) const {
    const std::string onBranch = branch.empty() ? BranchOf(scene) : branch;
    if (!edge.branch.empty() && edge.branch != onBranch) return false;
    const double at = OrderOf(scene);
    if (at < 0) return edge.from.empty() && edge.until.empty();
    if (!edge.from.empty()) {
        const double from = OrderOf(edge.from);
        if (from >= 0 && at < from) return false;
    }
    if (!edge.until.empty()) {
        const double until = OrderOf(edge.until);
        if (until >= 0 && at >= until) return false;
    }
    return true;
}

std::vector<const StoryEdge*> StoryGraph::FactsAt(const std::string& scene, const std::string& branch) const {
    std::vector<const StoryEdge*> out;
    const StoryOntology& o = StoryOntology::Engine();
    for (const auto& e : edges) {
        const auto* p = o.Property(e.p);
        if ((p ? p->temporal : (!e.from.empty() || !e.until.empty())) && Holds(e, scene, branch)) out.push_back(&e);
    }
    return out;
}

// --- Questions --------------------------------------------------------------------------------

bool StoryGraph::AliveAt(const std::string& character, const std::string& scene, const std::string& branch) const {
    for (const auto* e : Out(character, "state")) {
        const std::string state = e->Text("state");
        if ((state == "dead" || state == "destroyed") && Holds(*e, scene, branch)) return false;
    }
    return true;
}

std::vector<std::string> StoryGraph::LocationsAt(const std::string& entity, const std::string& scene, const std::string& branch) const {
    std::vector<std::string> out;
    for (const auto* e : Out(entity, "locatedAt")) {
        if (Holds(*e, scene, branch)) out.push_back(e->o);
    }
    SortUnique(out);
    return out;
}

bool StoryGraph::KnowsAt(const std::string& character, const std::string& about, const std::string& scene, const std::string& branch) const {
    for (const auto* e : Out(character, "knows")) {
        if ((e->o == about || e->Text("about") == about) && Holds(*e, scene, branch)) return true;
    }
    return false;
}

std::vector<std::string> StoryGraph::PossessorsAt(const std::string& item, const std::string& scene, const std::string& branch) const {
    std::vector<std::string> out;
    for (const auto* e : In(item, "possesses")) {
        if (Holds(*e, scene, branch)) out.push_back(e->s);
    }
    SortUnique(out);
    return out;
}

std::string StoryGraph::LocationOfScene(const std::string& scene) const {
    if (const StoryNode* n = Find(scene); n && !n->Text("location").empty()) return n->Text("location");
    const auto at = Out(scene, "at");
    return at.empty() ? std::string() : at.front()->o;
}

std::vector<std::string> StoryGraph::Participants(const std::string& scene) const {
    std::vector<std::string> out;
    for (const auto* e : Out(scene, "participant")) out.push_back(e->o);
    SortUnique(out);
    return out;
}

std::vector<std::string> StoryGraph::ScenesAdvancedBy(const std::string& stage) const {
    std::vector<std::string> out;
    for (const auto* e : Out(stage, "advancesScene")) out.push_back(e->o);
    return out;
}

std::string StoryGraph::QuestOfStage(const std::string& stage) const {
    if (const StoryNode* n = Find(stage); n && !n->Text("quest").empty()) return n->Text("quest");
    const auto in = In(stage, "hasStage");
    return in.empty() ? std::string() : in.front()->s;
}

std::vector<std::string> StoryGraph::RolelessCharacters(const StoryOntology& ontology) const {
    std::vector<std::string> out;
    for (const auto* n : OfClass("Character", ontology)) {
        if (Out(n->id, "fills").empty()) out.push_back(n->id);
    }
    return out;
}

std::vector<std::string> StoryGraph::UnfilledRoles(const StoryOntology& ontology) const {
    std::vector<std::string> out;
    for (const auto& n : nodes) {
        if ((ontology.IsA(n.cls, "NarrativeRole") || ontology.IsA(n.cls, "GameplayRole")) && In(n.id, "fills").empty()) out.push_back(n.id);
    }
    return out;
}

std::vector<std::string> StoryGraph::QuestsBrokenByDeath(const std::string& character, const std::string& scene) const {
    const double dies = OrderOf(scene);
    std::vector<std::string> out;
    for (const auto* e : In(character, "needsCharacter")) {
        // When it's needed: the edge's "at", else the scenes the stage (or the quest's stages) advance.
        std::vector<std::string> when;
        if (!e->Text("at").empty()) when.push_back(e->Text("at"));
        else if (const StoryNode* n = Find(e->s); n && n->cls == "QuestStage") when = ScenesAdvancedBy(e->s);
        else {
            for (const auto* stage : Out(e->s, "hasStage")) {
                for (const auto& s : ScenesAdvancedBy(stage->o)) when.push_back(s);
            }
        }
        const bool broken = when.empty() || std::any_of(when.begin(), when.end(), [&](const std::string& s) { return OrderOf(s) >= dies; });
        if (!broken) continue;
        const StoryNode* n = Find(e->s);
        out.push_back(n && n->cls == "QuestStage" ? QuestOfStage(e->s) : e->s);
    }
    SortUnique(out);
    return out;
}

std::map<std::string, std::set<std::string>> StoryGraph::PlacesUsed() const {
    std::map<std::string, std::set<std::string>> out;
    for (const auto& id : SceneOrder()) {
        const std::string location = LocationOfScene(id);
        if (location.empty()) continue;
        auto& parts = out[location];
        for (const auto& part : Find(id)->List("needsParts")) parts.insert(part);
    }
    return out;
}

std::vector<std::string> StoryGraph::CharactersMet(const std::string& chapter) const {
    std::vector<std::string> out;
    for (const auto& id : SceneOrder()) {
        const StoryNode* scene = Find(id);
        bool inChapter = scene->Text("chapter") == chapter;
        for (const auto* e : Out(id, "partOf")) inChapter = inChapter || e->o == chapter;
        if (!inChapter) continue;
        for (const auto& c : Participants(id)) {
            const StoryNode* n = Find(c);
            if (n && n->cls == "PlayerCharacter") continue;
            if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
        }
    }
    return out;
}

std::string StoryGraph::MadeBy(const std::string& id) const {
    if (const StoryNode* n = Find(id); n && !n->Text("madeBy").empty()) return n->Text("madeBy");
    for (const auto* e : Out(id, "madeBy")) return e->o;
    for (const auto* e : Out(id)) {
        if (!e->madeBy.empty()) return e->madeBy;
    }
    return {};
}

std::vector<std::string> StoryGraph::Dependents(const std::string& id) const {
    std::set<std::string> seen{id};
    std::deque<std::string> queue{id};
    auto add = [&](const std::string& x) {
        if (!x.empty() && seen.insert(x).second) queue.push_back(x);
    };
    static const std::set<std::string> forward = {"causes", "enables", "motivates", "prevents", "actualizes", "realizedBy"};
    static const std::set<std::string> backward = {"presents", "participant", "needsCharacter", "paysOff", "advancesScene",
                                                   "resolves", "gives", "requiresQuest", "fillsAlias"};
    while (!queue.empty()) {
        const std::string x = queue.front();
        queue.pop_front();
        for (const auto& e : edges) {
            if (e.s == x && forward.contains(e.p)) add(e.o);
            if (e.o == x && backward.contains(e.p)) {
                add(e.s);
                if (e.p == "needsCharacter" || e.p == "advancesScene") {
                    if (const StoryNode* n = Find(e.s); n && n->cls == "QuestStage") add(QuestOfStage(e.s));
                }
            }
        }
        for (const auto& n : nodes) {
            if (n.Text("speaker") == x || n.Text("fill") == x) add(n.id); // dialogue it speaks, aliases it fills
        }
    }
    seen.erase(id);
    return {seen.begin(), seen.end()};
}

nlohmann::json StoryGraph::Timeline(const std::string& id) const {
    std::vector<const StoryEdge*> mine;
    for (const auto& e : edges) {
        if (e.s == id || e.o == id) mine.push_back(&e);
    }
    std::stable_sort(mine.begin(), mine.end(), [&](const StoryEdge* a, const StoryEdge* b) {
        const double fa = a->from.empty() ? -1e9 : OrderOf(a->from), fb = b->from.empty() ? -1e9 : OrderOf(b->from);
        return fa < fb;
    });
    nlohmann::json out = nlohmann::json::array();
    for (const auto* e : mine) {
        nlohmann::json j = {{"p", e->p}, {"s", e->s}, {"o", e->o}, {"from", e->from}, {"until", e->until}};
        if (!e->branch.empty()) j["branch"] = e->branch;
        if (!e->fields.empty()) j["fields"] = e->fields;
        out.push_back(std::move(j));
    }
    return out;
}

nlohmann::json StoryGraph::Subgraph(const std::string& scene, const std::string& branch) const {
    const StoryNode* node = Find(scene);
    if (!node) return nlohmann::json::object();
    const double at = OrderOf(scene);
    auto summary = [&](const std::string& id) {
        nlohmann::json j = {{"id", id}};
        if (const StoryNode* n = Find(id)) {
            j["class"] = n->cls;
            for (const char* k : {"name", "importance", "summary", "kind", "buildType", "mood"}) {
                if (!n->Text(k).empty()) j[k] = n->Text(k);
            }
        }
        return j;
    };
    nlohmann::json participants = nlohmann::json::array();
    for (const auto& c : Participants(scene)) {
        nlohmann::json p = summary(c);
        p["alive"] = AliveAt(c, scene, branch);
        p["locations"] = LocationsAt(c, scene, branch);
        nlohmann::json relationships = nlohmann::json::array(), goals = nlohmann::json::array(), knows = nlohmann::json::array(),
                       possesses = nlohmann::json::array();
        for (const auto* e : Out(c)) {
            if (!Holds(*e, scene, branch)) continue;
            if (e->p == "hasRelationship") relationships.push_back({{"with", e->o}, {"type", e->Text("type")}, {"valence", e->fields.value("valence", 0.0)}});
            else if (e->p == "hasGoal") goals.push_back(e->o);
            else if (e->p == "knows") knows.push_back(e->o.empty() ? e->Text("about") : e->o);
            else if (e->p == "possesses") possesses.push_back(e->o);
        }
        p["relationships"] = relationships;
        p["goals"] = goals;
        p["knows"] = knows;
        p["possesses"] = possesses;
        participants.push_back(std::move(p));
    }
    nlohmann::json location = nlohmann::json::object();
    if (const std::string loc = LocationOfScene(scene); !loc.empty()) {
        location = summary(loc);
        nlohmann::json earlier = nlohmann::json::array();
        for (const auto& id : SceneOrder()) {
            if (id != scene && OrderOf(id) < at && LocationOfScene(id) == loc) earlier.push_back(id);
        }
        location["earlierScenes"] = earlier;
    }
    // Open threads: conflicts not yet resolved and setups not yet paid off by this scene.
    auto doneBy = [&](const std::string& id, const char* p) {
        for (const auto* e : In(id, p)) {
            for (const auto* shown : In(e->s, "presents")) {
                if (OrderOf(shown->s) <= at) return true;
            }
            if (OrderOf(e->s) >= 0 && OrderOf(e->s) <= at) return true;
        }
        return false;
    };
    nlohmann::json conflicts = nlohmann::json::array(), setups = nlohmann::json::array();
    for (const auto& n : nodes) {
        if (n.cls == "Conflict" && !doneBy(n.id, "resolves")) conflicts.push_back(n.id);
        if (n.Flag("setup") && !n.Flag("openEnded") && !doneBy(n.id, "paysOff")) setups.push_back(n.id);
    }
    nlohmann::json sceneJson = {{"id", scene}};
    for (const auto& [k, v] : node->fields.items()) sceneJson[k] = v;
    return {{"scene", sceneJson},
            {"participants", participants},
            {"location", location},
            {"openThreads", {{"conflicts", conflicts}, {"setups", setups}}}};
}

// --- Changes ----------------------------------------------------------------------------------

std::string StoryGraph::Apply(const nlohmann::json& change, const StoryOntology& ontology) {
    if (!change.is_object()) return "a change is an object with nodes, assert, retract or remove";
    StoryGraph next = *this;
    for (const auto& n : change.value("nodes", nlohmann::json::array())) {
        if (!n.is_object()) return "a node is an object with id and class";
        const std::string id = text(n, "id"), cls = text(n, "class");
        if (id.empty()) return "a node has no id";
        StoryNode* existing = next.Find(id);
        if (cls.empty() && !existing) return "node " + id + " has no class";
        if (!cls.empty() && !ontology.Class(cls)) return "node " + id + ": no class " + cls;
        StoryNode& node = next.Upsert(id, cls);
        for (const auto& [k, v] : n.items()) {
            if (k == "id" || k == "class") continue;
            if (v.is_null()) node.fields.erase(k);
            else node.fields[k] = v;
        }
    }
    for (const auto& id : change.value("remove", nlohmann::json::array())) {
        if (id.is_string()) next.RemoveNode(id.get<std::string>());
    }
    // The AI panel's story_graph op names them facts and end.
    auto list = [&](const char* a, const char* b) {
        nlohmann::json out = change.value(a, nlohmann::json::array());
        for (const auto& x : change.value(b, nlohmann::json::array())) out.push_back(x);
        return out;
    };
    for (const auto& e : list("assert", "facts")) {
        const auto parsed = FromJson({{"edges", nlohmann::json::array({e})}}).edges;
        if (parsed.empty()) return "a fact is an object with p, s and o";
        StoryEdge edge = parsed[0];
        const auto* property = ontology.Property(edge.p);
        if (!property) return "no property " + edge.p;
        const StoryNode* s = next.Find(edge.s);
        if (!s) return edge.p + ": no node " + edge.s;
        if (!property->domain.empty() &&
            std::none_of(property->domain.begin(), property->domain.end(), [&](const std::string& c) { return ontology.IsA(s->cls, c); })) {
            return edge.p + ": " + edge.s + " is a " + s->cls + ", not one of its subjects";
        }
        if (!property->range.empty()) {
            const StoryNode* o = next.Find(edge.o);
            // "knows" may name what's known by "about" instead of a Belief node.
            if (!o && !(edge.p == "knows" && edge.o.empty() && !edge.Text("about").empty())) return edge.p + ": no node " + edge.o;
            if (o && std::none_of(property->range.begin(), property->range.end(), [&](const std::string& c) { return ontology.IsA(o->cls, c); })) {
                return edge.p + ": " + edge.o + " is a " + o->cls + ", not one of its objects";
            }
        }
        for (const std::string* scene : {&edge.from, &edge.until}) {
            if (!scene->empty() && next.OrderOf(*scene) < 0) return edge.p + ": no scene " + *scene;
        }
        const bool duplicate = std::any_of(next.edges.begin(), next.edges.end(), [&](const StoryEdge& x) {
            return x.p == edge.p && x.s == edge.s && x.o == edge.o && x.from == edge.from && x.branch == edge.branch && x.fields == edge.fields;
        });
        if (!duplicate) next.edges.push_back(std::move(edge));
    }
    for (const auto& r : list("retract", "end")) {
        if (!r.is_object()) return "an ended fact is an object with p, s and o";
        const std::string p = text(r, "p"), s = text(r, "s"), o = text(r, "o"), at = text(r, "at");
        const std::string type = text(r, "type");
        bool found = false;
        for (auto& e : next.edges) {
            if (e.p != p || e.s != s || (!o.empty() && e.o != o) || (!type.empty() && e.Text("type") != type)) continue;
            if (at.empty()) {
                e.until = "\x01remove";
            } else {
                if (next.OrderOf(at) < 0) return "retract " + p + ": no scene " + at;
                if (!e.until.empty() && next.OrderOf(e.until) <= next.OrderOf(at)) continue; // already over
                e.until = at;
            }
            found = true;
        }
        if (!found) return "retract: no " + p + " fact " + s + " -> " + o;
        next.edges.erase(std::remove_if(next.edges.begin(), next.edges.end(), [](const StoryEdge& e) { return e.until == "\x01remove"; }),
                         next.edges.end());
    }
    next.revision = revision + 1;
    *this = std::move(next);
    return {};
}

// --- The World Bible --------------------------------------------------------------------------

std::vector<std::pair<std::string, std::string>> StoryGraph::RelLines(const std::string& key) const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto* e : Out(key)) {
        if (!e->until.empty() || !e->branch.empty() || e->o.empty()) continue; // the end of the main line
        if (e->p == "hasRelationship" && !e->Text("type").empty()) out.push_back({Underscored(e->Text("type")), e->o});
        else if (e->p == "memberOf") out.push_back({"member_of", e->o});
        else if (e->p == "locatedAt") out.push_back({"located_at", e->o});
        else if (e->p == "possesses") out.push_back({"possesses", e->o});
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

int StoryGraph::WriteRelationsTo(WorldBible& bible) const {
    int changed = 0;
    const std::vector<WorldEntry> entries = bible.Entries();
    for (WorldEntry entry : entries) {
        if (!Find(entry.Key())) continue;
        std::vector<WorldRelation> relations;
        for (const auto& [type, target] : RelLines(entry.Key())) relations.push_back({type, target});
        auto same = [](const std::vector<WorldRelation>& a, const std::vector<WorldRelation>& b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                if (a[i].type != b[i].type || a[i].target != b[i].target) return false;
            }
            return true;
        };
        std::vector<WorldRelation> current = entry.relations;
        std::sort(current.begin(), current.end(), [](const WorldRelation& a, const WorldRelation& b) {
            return std::tie(a.type, a.target) < std::tie(b.type, b.target);
        });
        if (same(current, relations)) continue;
        entry.relations = std::move(relations);
        if (!bible.Save(entry)) ++changed;
    }
    return changed;
}

int StoryGraph::ReadRelationsFrom(const WorldBible& bible) {
    int added = 0;
    auto ensure = [&](const std::string& key) -> bool {
        if (Find(key)) return true;
        const WorldEntry* entry = bible.Find(key);
        if (!entry) return false;
        auto cls = KindClasses().find(entry->kind);
        if (cls == KindClasses().end()) return false;
        StoryNode& n = Upsert(key, cls->second);
        if (!entry->name.empty()) n.fields["name"] = entry->name;
        return true;
    };
    for (const auto& entry : bible.Entries()) {
        if (!KindClasses().contains(entry.kind) || entry.relations.empty()) continue;
        if (!ensure(entry.Key())) continue;
        const auto have = RelLines(entry.Key());
        for (const auto& r : entry.relations) {
            const std::string type = r.type == "lives_in" ? "located_at" : r.type; // the same fact
            if (std::find(have.begin(), have.end(), std::make_pair(type, r.target)) != have.end()) continue;
            if (!ensure(r.target)) continue;
            StoryEdge e;
            e.s = entry.Key();
            e.o = r.target;
            if (r.type == "member_of") e.p = "memberOf";
            else if (r.type == "located_at" || r.type == "lives_in") e.p = "locatedAt";
            else if (r.type == "possesses") e.p = "possesses";
            else {
                e.p = "hasRelationship";
                e.fields["type"] = Dashed(r.type);
            }
            e.madeBy = "user";
            edges.push_back(std::move(e));
            ++added;
        }
    }
    if (added) ++revision;
    return added;
}

} // namespace aige
