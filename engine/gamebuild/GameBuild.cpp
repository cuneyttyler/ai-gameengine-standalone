#include "gamebuild/GameBuild.h"

#include "assets/AssetDatabase.h"
#include "assets/Catalog.h"
#include "assets/Prefab.h"
#include "assets/Project.h"
#include "assets/SceneFiles.h"
#include "assets/SceneIO.h"
#include "editing/PlaceScene.h"
#include "gameplay/Archetypes.h"
#include "gameplay/Behaviour.h"
#include "gameplay/Components.h"
#include "gameplay/Cutscene.h"
#include "gameplay/EngineReflection.h"
#include "gameplay/Items.h"
#include "gameplay/Pathfinding.h"
#include "gameplay/Settings.h"
#include "gameplay/Story.h"
#include "gameplay/TileGrid.h"
#include "gameplay/TileLayer.h"
#include "physics2d/Components.h"
#include "places/Ontology.h"
#include "planner/PlannerDocs.h"
#include "records/Records.h"
#include "story/StoryOntology.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string Text(const json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::optional<std::pair<int, int>> NearestOpen(const TileGrid& grid, int cx, int cy, int radius = 40) {
    for (int r = 0; r <= radius; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                if (!grid.IsSolid(cx + dx, cy + dy)) return std::make_pair(cx + dx, cy + dy);
            }
        }
    }
    return std::nullopt;
}

// The open cells' bounds and middle.
struct Extent {
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool any = false;
};

Extent OpenExtent(Registry& registry, const TileGrid& grid) {
    Extent e;
    for (auto [entity, tile, at] : registry.view<Tile, WorldTransform2D>().each()) {
        const int x = static_cast<int>(std::floor(at.x)), y = static_cast<int>(std::floor(at.y));
        if (grid.IsSolid(x, y)) continue;
        if (!e.any) e = {x, y, x, y, true};
        e.minX = std::min(e.minX, x);
        e.minY = std::min(e.minY, y);
        e.maxX = std::max(e.maxX, x);
        e.maxY = std::max(e.maxY, y);
    }
    return e;
}

Entity AddMarker(Registry& registry, const std::string& name, float x, float y) {
    for (auto [e, m] : registry.view<Marker>().each()) {
        if (m.name == name) {
            registry.get_or_emplace<WorldTransform2D>(e) = {x, y};
            return e;
        }
    }
    const Entity e = registry.create();
    registry.emplace<WorldTransform2D>(e, x, y);
    registry.emplace<Marker>(e, name);
    registry.emplace<Name>(e, "Marker " + name);
    return e;
}

std::optional<std::pair<float, float>> MarkerAt(Registry& registry, const std::string& name) {
    for (auto [e, m, at] : registry.view<Marker, WorldTransform2D>().each()) {
        if (m.name == name) return std::make_pair(at.x, at.y);
    }
    return std::nullopt;
}

// Objective actions as the engine's quest objective types.
std::string ObjectiveType(const std::string& action) {
    static const std::map<std::string, std::string> types = {
        {"talk", "talk"},       {"goto", "reach"},       {"reach", "reach"},     {"kill", "defeat"},     {"defeat-count", "defeat"},
        {"gather", "collect"},  {"collect", "collect"},  {"deliver", "deliver"}, {"use", "use"},         {"interact", "use"},
        {"escort", "escort"},   {"survive", "survive"},  {"examine", "reach"},   {"spy", "reach"},       {"stealth", "reach"},
        {"defend", "survive"},  {"capture", "defeat"},   {"free", "use"},        {"craft", "have"},      {"wait", "survive"},
        {"choose", "talk"}};
    auto it = types.find(action);
    return it == types.end() ? "reach" : it->second;
}

} // namespace

json EngineQuestFromDesign(const json& design, const json& bindings) {
    json quest = {{"name", design.value("name", design.value("id", ""))}, {"description", design.value("rationale", "")}};
    std::map<std::string, std::string> fills; // alias -> what the game knows it by
    for (const auto& a : design.value("aliases", json::array())) {
        std::string fill = a.value("fill", "");
        for (const auto& b : bindings.value("aliases", json::array())) {
            if (b.value("quest", "") == design.value("id", "") && b.value("alias", "") == a.value("id", "")) {
                if (!b.value("marker", "").empty()) fill = b.value("marker", "");
                else if (!b.value("entity", "").empty()) fill = b.value("entity", "");
            }
        }
        fills[a.value("id", "")] = fill;
    }
    json aliases = json::array();
    for (const auto& a : design.value("aliases", json::array())) {
        const std::string kind = a.value("kind", "character");
        aliases.push_back({{"id", a.value("id", "")},
                           {"kind", kind == "spot" || kind == "container" ? "entity" : kind},
                           {"fill", fills[a.value("id", "")]}});
    }
    quest["aliases"] = aliases;
    const std::string giverAlias = design.value("giver", json::object()).value("alias", "");
    if (!giverAlias.empty() && fills.count(giverAlias)) quest["giver"] = fills[giverAlias];
    json stages = json::array();
    for (const auto& s : design.value("stages", json::array())) {
        json stage = {{"index", s.value("index", 10)}, {"journal", s.value("journal", "")}};
        json objectives = json::array();
        int k = 0;
        for (const auto& o : s.value("objectives", json::array())) {
            json obj = {{"id", o.value("id", "o" + std::to_string(s.value("index", 10)) + "-" + std::to_string(++k))},
                        {"text", o.value("text", s.value("journal", ""))},
                        {"type", ObjectiveType(o.value("action", "reach"))}};
            const std::string target = o.value("target", "");
            if (!target.empty()) obj["target"] = "@" + target;
            if (o.value("count", 1) > 1) obj["count"] = o.value("count", 1);
            objectives.push_back(obj);
        }
        if (!objectives.empty()) stage["objectives"] = objectives;
        const std::string terminal = s.value("terminal", "");
        if (terminal == "completed" || terminal == "complete") stage["ends"] = "complete";
        if (terminal == "failed" || terminal == "fail") stage["ends"] = "fail";
        stages.push_back(stage);
    }
    quest["stages"] = stages;
    json rewards = json::object();
    std::string items;
    for (const auto& r : design.value("rewards", json::array())) {
        if (r.contains("xp")) rewards["experience"] = r["xp"];
        if (r.contains("gold")) rewards["gold"] = r["gold"];
        if (r.contains("item")) items += (items.empty() ? "" : ", ") + PlanSlug(r.value("item", ""));
    }
    if (!items.empty()) rewards["items"] = items;
    if (!rewards.empty()) quest["rewards"] = rewards;
    return quest;
}

// --- The build ------------------------------------------------------------------------------------

GameBuild::GameBuild(Host host) : host_(std::move(host)), store_(host_.projectRoot) {}
GameBuild::~GameBuild() = default;

void GameBuild::Record(const std::string& kind, const std::string& subject, const std::string& chosen, const std::string& reason) {
    PlanDecision d;
    d.stage = stage_;
    d.actor = "planner";
    d.kind = kind;
    d.subject = subject;
    d.chosen = chosen;
    d.reason = reason;
    store_.Decide(d);
}

std::string GameBuild::Start(const std::string& mode) {
    const json state = store_.State();
    if (state.value("baseline", json()).is_null()) return "the design has no baseline yet: validate it first (⑨)";
    const json brief = store_.Read("brief").value_or(json::object());
    mode_ = mode.empty() ? BriefGet(brief, "plan.building").is_string() ? BriefGet(brief, "plan.building").get<std::string>() : "automatic" : mode;
    if (mode_ == "confirm-per-place" || mode_ == "ask-per-place") mode_ = "manual";
    rows_.clear();
    jobs_.clear();
    builds_.clear();
    pending_.clear();
    bindings_ = {{"schema", "aige.bindings/1"}, {"aliases", json::array()}, {"actors", json::array()}};
    files_ = json::object();
    playtest_ = json::object();
    stage_ = "build";
    status_ = "working";
    message_ = "⑩ Building the places…";
    MakeBuildPlan();
    Record("build-plan", "buildplan", std::to_string(rows_.size()) + " places, " + mode_);
    Save();
    return {};
}

void GameBuild::Stop() {
    for (auto& r : rows_) {
        if (r.status == "building") r.status = "waiting";
        r.inEditor = false;
    }
    builds_.clear();
    pending_.clear();
    status_ = "stopped";
    message_ = "Stopped: Restart builds builds every place again (with the art setting as it is then).";
    Save();
}

std::string GameBuild::Open() {
    if (!fs::exists(store_.Dir() / "buildstate.json")) return "nothing is being built";
    Load();
    pending_.clear();
    for (auto& j : jobs_) {
        if (j.status == "running") j.status = "queued";
    }
    // A build that was waiting on the AI reopens where it was (PlaceBuild::Open).
    for (auto& r : rows_) {
        if (r.status == "building" && !r.build.empty()) {
            // Past its plan (its art being found, its scene being built) when the editor closed: the
            // Build panel finishes it, from where it was.
            std::ifstream sessionIn(host_.projectRoot / "places" / r.build / "session.json", std::ios::binary);
            const json session = json::parse(sessionIn, nullptr, false);
            const std::string stage = session.is_object() && session.value("stage", json()).is_string() ? session["stage"].get<std::string>() : "";
            const bool pastPlan = stage == "planned" || stage == "sourcing" || stage == "sourced" || stage == "building" || stage == "built";
            if (pastPlan && host_.buildInEditor) {
                r.resumeInEditor = true;
                continue;
            }
            auto build = std::make_unique<PlaceBuild>(BuildHost(r.place));
            if (build->Open(r.build).empty()) builds_[r.place] = std::move(build);
            else r.status = "waiting";
        }
    }
    Save();
    return {};
}

std::string GameBuild::Resume() {
    if (rows_.empty()) return "nothing to continue: start the build";
    if (status_ == "working") return {};
    if (stage_ == "done") return "the game is built: Restart builds to build it again";
    status_ = "working";
    message_ = stage_ == "build" ? "⑩ Carrying on with the places…" : "Carrying on…";
    Save();
    return {};
}

void GameBuild::Save() {
    json rows = json::array();
    for (const auto& r : rows_) {
        rows.push_back({{"place", r.place}, {"scene", r.scene}, {"buildType", r.buildType}, {"prompt", r.prompt}, {"mode", r.mode},
                        {"status", r.status}, {"build", r.build}, {"error", r.error}, {"dependsOn", r.dependsOn}, {"brief", r.brief},
                        {"checks", r.checks}});
    }
    json jobs = json::array();
    for (const auto& j : jobs_) {
        jobs.push_back({{"id", j.id}, {"kind", j.kind}, {"subject", j.subject}, {"status", j.status}, {"error", j.error}, {"attempts", j.attempts}});
    }
    std::ofstream(store_.Dir() / "buildplan.json", std::ios::binary) << json{{"schema", "aige.build-plan/1"}, {"mode", mode_}, {"rows", rows}}.dump(2);
    std::ofstream(store_.Dir() / "buildstate.json", std::ios::binary)
        << json{{"stage", stage_}, {"status", status_}, {"message", message_}, {"mode", mode_}, {"jobs", jobs}, {"nextJob", nextJob_},
                {"bindings", bindings_}, {"files", files_}, {"playtest", playtest_}}
               .dump(2);
}

void GameBuild::Load() {
    std::ifstream in(store_.Dir() / "buildstate.json", std::ios::binary);
    const json s = json::parse(in, nullptr, false);
    std::ifstream planIn(store_.Dir() / "buildplan.json", std::ios::binary);
    const json plan = json::parse(planIn, nullptr, false);
    if (s.is_discarded()) return;
    stage_ = s.value("stage", "");
    status_ = s.value("status", "");
    message_ = s.value("message", "");
    mode_ = s.value("mode", "automatic");
    nextJob_ = s.value("nextJob", 1);
    bindings_ = s.value("bindings", json::object());
    files_ = s.value("files", json::object());
    playtest_ = s.value("playtest", json::object());
    jobs_.clear();
    for (const auto& j : s.value("jobs", json::array())) {
        Job job;
        job.id = j.value("id", "");
        job.kind = j.value("kind", "");
        job.subject = j.value("subject", "");
        job.status = j.value("status", "queued");
        job.error = j.value("error", "");
        job.attempts = j.value("attempts", 0);
        jobs_.push_back(job);
    }
    rows_.clear();
    if (!plan.is_discarded()) {
        for (const auto& r : plan.value("rows", json::array())) {
            Row row;
            row.place = r.value("place", "");
            row.scene = r.value("scene", "");
            row.buildType = r.value("buildType", "");
            row.prompt = r.value("prompt", "");
            row.mode = r.value("mode", "automatic");
            row.status = r.value("status", "waiting");
            row.build = r.value("build", "");
            row.error = r.value("error", "");
            row.brief = r.value("brief", json::object());
            row.checks = r.value("checks", json::object());
            for (const auto& d : r.value("dependsOn", json::array())) row.dependsOn.push_back(d.get<std::string>());
            rows_.push_back(std::move(row));
        }
    }
}

GameBuild::Row* GameBuild::FindRow(const std::string& place) {
    for (auto& r : rows_) {
        if (r.place == place) return &r;
    }
    return nullptr;
}

std::string GameBuild::StartScene() const {
    for (const auto& r : rows_) {
        const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
        if (const StoryNode* n = g.Find(r.place); n && n->Flag("start")) return r.scene;
    }
    return rows_.empty() ? std::string() : rows_.front().scene;
}

bool GameBuild::Owns(int requestId) const { return pending_.contains(requestId); }

void GameBuild::OnResponse(int requestId, const json& body) {
    auto it = pending_.find(requestId);
    if (it == pending_.end()) return;
    const std::string who = it->second;
    pending_.erase(it);
    // Kept for Pump (which may be heavy): the build's answers and the jobs'.
    if (who.rfind("build:", 0) == 0) buildAnswers_[who.substr(6)].push_back({requestId, body});
    else if (who.rfind("job:", 0) == 0) jobAnswers_.push_back({who.substr(4), body});
}

PlaceBuild::Host GameBuild::BuildHost(const std::string& place) {
    PlaceBuild::Host h;
    h.ontology = host_.places;
    const fs::path root = host_.projectRoot;
    auto places = host_.places;
    h.catalog = [root, places] {
        AssetDatabase assets{Project{root}};
        assets.Refresh();
        return BuildCatalog(assets, &places(), {});
    };
    h.isoGrid = [] { return ActiveSettings().grid.style != GridStyle::TopDown; };
    h.request = [this, place](const std::string& path, const json& body, int timeout) {
        const int id = host_.request ? host_.request(path, body, timeout) : 0;
        if (id != 0) pending_[id] = "build:" + place;
        return id;
    };
    h.projectRoot = root;
    h.layoutStep = false; // the planner confirms the zones with the rest
    h.buildMethod = host_.buildMethod;
    return h;
}

// What the story needs of a place, for its build: the game's logline and synopsis, the scenes set
// there (in order), who is there, what the quests need in it, and its purpose and mood.
json GameBuild::StoryAnchors(const std::string& place) const {
    const json desc = store_.Read("places/" + PlanSlug(place)).value_or(json::object());
    json out = json::array();
    for (const auto& a : desc.value("storyAnchors", json::array())) {
        if (!a.is_object() || a.value("id", "").empty()) continue;
        std::string about = a.value("about", a.value("description", ""));
        if (about.empty()) about = a.value("module", "");
        out.push_back({{"id", a.value("id", "")}, {"about", about}});
    }
    return out;
}

json GameBuild::StoryFor(const std::string& place) const {
    const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
    auto text = [](const json& j, const char* key) {
        const auto it = j.is_object() ? j.find(key) : j.end();
        return j.is_object() && it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    const json outline = store_.Read("outline").value_or(json::object());
    const json storyPart = outline.is_object() ? outline.value("story", json::object()) : json::object();
    std::string synopsis = text(storyPart, "synopsis");
    if (synopsis.size() > 900) synopsis = synopsis.substr(0, 900) + "…";
    json scenes = json::array();
    for (const auto& id : g.SceneOrder()) {
        if (g.LocationOfScene(id) != place) continue;
        const json s = store_.Read("story/scenes/" + id).value_or(json::object());
        const json card = s.is_object() ? s.value("card", json::object()) : json::object();
        json scene = {{"id", id}, {"goal", text(card, "goal")}, {"summary", text(s, "summary")}};
        if (card.is_object() && card.contains("needsParts")) scene["needs"] = card["needsParts"];
        scenes.push_back(scene);
    }
    json people = json::array();
    for (const auto& e : g.edges) {
        if (e.p != "locatedAt" || e.o != place) continue;
        const StoryNode* n = g.Find(e.s);
        if (n) people.push_back({{"id", n->id}, {"name", n->Text("name")}, {"line", n->Text("summary")}});
    }
    json needs = json::array();
    for (const auto& doc : store_.List("quests")) {
        const json q = store_.Read(doc).value_or(json::object());
        if (!q.is_object()) continue;
        for (const auto& a : q.value("aliases", json::array())) {
            if (!a.is_object() || text(a, "in") != place) continue;
            needs.push_back({{"quest", text(q, "name")}, {"alias", text(a, "id")}, {"kind", text(a, "kind")},
                             {"where", a.value("where", json())}});
        }
    }
    const json desc = store_.Read("places/" + PlanSlug(place)).value_or(json::object());
    const json d = desc.is_object() ? desc.value("description", json::object()) : json::object();
    json out = {{"logline", text(storyPart, "logline")}, {"synopsis", synopsis}, {"scenesHere", scenes},
                {"charactersHere", people}, {"questNeeds", needs}, {"purpose", text(d, "purpose")}, {"mood", text(d, "mood")}};
    return out;
}

PlaceBuild* GameBuild::BuildOf(const std::string& place) {
    auto it = builds_.find(place);
    return it == builds_.end() ? nullptr : it->second.get();
}

// --- ⑩ The build plan ----------------------------------------------------------------------------------

void GameBuild::MakeBuildPlan() {
    const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
    std::vector<Row> rows;
    for (const auto& doc : store_.List("places")) {
        const json p = store_.Read(doc).value_or(json::object());
        Row row;
        row.place = p.value("id", "location/" + PlanSlug(doc));
        row.scene = "scenes/" + PlanSlug(row.place) + ".aige";
        row.brief = p.value("brief", json::object());
        row.buildType = row.brief.value("buildType", p.value("kind", ""));
        const StoryNode* n = g.Find(row.place);
        const std::string name = n && !n->Text("name").empty() ? n->Text("name") : PlanSlug(row.place);
        row.prompt = name + ": " + p.value("description", json::object()).value("purpose", row.buildType);
        if (!row.brief.contains("name")) row.brief["name"] = name;
        for (const auto* e : g.Out(row.place, "inside")) row.dependsOn.push_back(e->o); // the outside before the inside
        row.mode = mode_ == "manual" ? "manual" : "automatic";
        rows.push_back(std::move(row));
    }
    // Parents first.
    std::set<std::string> placed;
    while (!rows.empty()) {
        bool moved = false;
        for (auto it = rows.begin(); it != rows.end();) {
            const bool ready = std::all_of(it->dependsOn.begin(), it->dependsOn.end(), [&](const std::string& d) {
                return placed.contains(d) || std::none_of(rows.begin(), rows.end(), [&](const Row& r) { return r.place == d; });
            });
            if (ready) {
                placed.insert(it->place);
                rows_.push_back(std::move(*it));
                it = rows.erase(it);
                moved = true;
            } else {
                ++it;
            }
        }
        if (!moved) { // a cycle: the rest in order
            for (auto& r : rows) rows_.push_back(std::move(r));
            rows.clear();
        }
    }
}

std::string GameBuild::SetMode(const std::string& place, const std::string& mode) {
    Row* r = FindRow(place);
    if (!r) return "no " + place + " in the build plan";
    r->mode = mode == "manual" ? "manual" : "automatic";
    Record("set-mode", place, r->mode);
    Save();
    return {};
}

std::string GameBuild::Retry(const std::string& place) {
    Row* r = FindRow(place);
    if (!r) return "no " + place + " in the build plan";
    r->status = "waiting";
    r->error.clear();
    r->steps = 0;
    builds_.erase(place);
    if (stage_ != "build") {
        stage_ = "build"; // the rest follows again
        status_ = "working";
    }
    Record("retry", place, "");
    Save();
    return {};
}

std::string GameBuild::Rebuild(const std::string& place) {
    Row* r = FindRow(place);
    if (!r) return "no " + place + " in the build plan";
    if (r->status == "building") return "it's being built now";
    r->status = "waiting";
    r->error.clear();
    r->steps = 0;
    r->build.clear(); // a new build of it, not the old one carried on
    r->inEditor = false;
    r->resumeInEditor = false;
    builds_.erase(place);
    stage_ = "build"; // the rest follows again
    status_ = "working";
    message_ = "⑩ Building " + PlanSlug(place) + " again…";
    Record("rebuild", place, "");
    Save();
    return {};
}

std::string GameBuild::InstallBuilt(const std::string& place, const std::string& buildId) {
    if (fs::exists(store_.Dir() / "buildstate.json")) Load();
    Row* r = FindRow(place);
    if (!r) return "no " + place + " in the build plan";
    r->build = buildId;
    r->error.clear();
    r->inEditor = r->resumeInEditor = false;
    builds_.erase(place);
    try {
        FinishInEditor(*r);
    } catch (const std::exception& e) {
        return e.what();
    }
    Save();
    return {};
}

std::string GameBuild::UsePlaceholder(const std::string& place) {
    Row* r = FindRow(place);
    if (!r) return "no " + place + " in the build plan";
    const std::string why = PlaceholderScene(*r);
    if (!why.empty()) return why;
    Record("placeholder", place, r->scene, "the user chose a placeholder");
    Save();
    return {};
}

std::string GameBuild::ManualDone(const std::string& place) {
    Row* r = FindRow(place);
    PlaceBuild* build = BuildOf(place);
    if (!r || !build) return "no build for " + place;
    if (build->Stage() != "planned" && build->Stage() != "built") return "its build isn't laid out yet";
    const std::string why = BuildScene(*r, *build);
    if (!why.empty()) return why;
    Record("build-step", place, "built", "the user finished it in the Build panel");
    Save();
    return {};
}

bool GameBuild::DriveBuilds() {
    bool did = false;
    auto depsDone = [&](const Row& row) {
        return std::all_of(row.dependsOn.begin(), row.dependsOn.end(), [&](const std::string& d) {
            const Row* dep = FindRow(d);
            return !dep || dep->status == "built" || dep->status == "placeholder";
        });
    };
    for (auto& row : rows_) {
        if (row.status == "building") {
            did = DriveRow(row) || did;
            return did; // one automatic build at a time
        }
    }
    for (auto& row : rows_) {
        if (row.status != "waiting" || !depsDone(row)) continue;
        auto build = std::make_unique<PlaceBuild>(BuildHost(row.place));
        const std::string why = build->StartFromBrief(row.prompt, row.brief);
        if (why.empty()) build->SetStory(StoryFor(row.place));
        if (!why.empty()) {
            row.error = why;
            row.status = "failed";
        } else {
            row.build = build->Id();
            builds_[row.place] = std::move(build);
            row.status = row.mode == "manual" ? "needs-you" : "building";
        }
        did = true;
        if (row.status == "building") return DriveRow(row) || did;
        if (row.status == "failed" && row.mode == "automatic") {
            PlaceholderScene(row);
            Record("placeholder", row.place, row.scene, "its build couldn't start: " + row.error);
        }
    }
    return did;
}

bool GameBuild::DriveRow(Row& row) {
    if (row.resumeInEditor) {
        // Reopened past its plan: the Build panel picks it up again.
        row.resumeInEditor = false;
        const std::string why = host_.buildInEditor ? host_.buildInEditor(row.build, row.scene) : "the editor isn't there";
        if (!why.empty()) throw std::runtime_error(why);
        row.inEditor = true;
        return true;
    }
    if (row.inEditor) {
        // The Build panel is building it (art, scene, checks): the next place waits.
        const std::string status = host_.editorBuildStatus ? host_.editorBuildStatus(row.build) : "failed: the editor isn't there";
        if (status == "working") return false;
        row.inEditor = false;
        if (status != "built") throw std::runtime_error(status.rfind("failed: ", 0) == 0 ? status.substr(8) : status);
        FinishInEditor(row);
        return true;
    }
    PlaceBuild* build = BuildOf(row.place);
    if (!build) {
        row.status = "waiting";
        return false;
    }
    bool did = false;
    try {
        auto answers = std::move(buildAnswers_[row.place]);
        buildAnswers_.erase(row.place);
        for (auto& [id, body] : answers) {
            build->OnResponse(id, body);
            did = true;
        }
        for (int step = 0; step < 12; ++step) {
            const std::string stage = build->Stage();
            std::string why;
            // The request is confirmed first (it moves on to the assets), then the build.
            if (stage == "request") why = build->Step("assets");
            else if (stage == "assets") why = build->Confirm();
            else if (stage == "layout") {
                // Checked by the vision model first (its errors revised); then the plan.
                if (build->LayoutChecked() || !build->CheckLayout().empty()) why = build->PlanFromLayout();
            }
            else if (stage == "plan") {
                // Its zones checked by the vision model first (built without ② Layout); then approved.
                if (build->LayoutChecked() || !build->CheckLayout().empty()) why = build->Approve();
            }
            else if ((stage == "planned" || stage == "built") && host_.buildInEditor) {
                // Built by the Build panel itself, as the user would: its art found or made, the scene
                // built, checked and saved; this build lets go of it meanwhile.
                const std::string id = build->Id();
                row.build = id;
                builds_.erase(row.place);
                const std::string why = host_.buildInEditor(id, row.scene);
                if (!why.empty()) throw std::runtime_error(why);
                row.inEditor = true;
                Record("build-step", row.place, "planned → the Build panel", "automatic: built in the editor");
                return true;
            } else if (stage == "planned" || stage == "built") {
                const std::string built = BuildScene(row, *build);
                if (!built.empty()) throw std::runtime_error(built);
                return true;
            } else {
                // Waiting on the AI (or stuck: nothing asked, nothing to press).
                const bool asking = std::any_of(pending_.begin(), pending_.end(), [&](const auto& p) { return p.second == "build:" + row.place; });
                if (!asking && ++row.steps > 3) throw std::runtime_error("the build stopped at '" + stage + "': " + build->State().value("status", ""));
                return did;
            }
            if (!why.empty()) throw std::runtime_error(stage + ": " + why);
            Record("build-step", row.place, stage + " → " + build->Stage(), "automatic: checks passed");
            row.steps = 0;
            did = true;
        }
    } catch (const std::exception& e) {
        row.error = e.what();
        row.status = "failed";
        builds_.erase(row.place);
        if (row.mode == "automatic") {
            // So binding and the playtest can go on; rebuilt later with Retry.
            PlaceholderScene(row);
            Record("placeholder", row.place, row.scene, "the build failed: " + row.error);
        }
        return true;
    }
    return did;
}

std::string GameBuild::BuildScene(Row& row, PlaceBuild& build) {
    const Project project{host_.projectRoot};
    const Ontology& places = host_.places();
    PrefabLibrary prefabs(project);
    AssetDatabase assets(project);
    assets.Refresh();
    PlaceSceneOptions options;
    options.place = build.Id();
    options.isoGrid = ActiveSettings().grid.style != GridStyle::TopDown;
    const TerrainSet terrains = ProjectTerrainSet(prefabs, project);
    const json plan = PlanPlaceAssets(places, build.Spec(), build.Solved(), BuildCatalog(assets, &places, {}), prefabs, assets, terrains, options, nullptr);
    Registry registry;
    json report;
    BuildPlaceScene(registry, prefabs, build.Solved(), plan, terrains, options, &report);
    const int made = AddStoryMarkers(registry, row, build.Solved(), options.originX, options.originY);
    std::error_code ec;
    fs::create_directories(project.Resolve(row.scene).parent_path(), ec);
    SaveScene(registry, project.Resolve(row.scene).string());
    int objects = 0;
    const json counts = report.value("objects", json::object());
    for (const auto& [layer, n] : counts.items()) objects += n.is_number() ? n.get<int>() : 0;
    row.checks = {{"objects", objects}, {"pieces", report.value("pieces", 0)}, {"anchorsMade", made}};
    row.build = build.Id();
    row.status = "built";
    files_[row.scene] = {{"planner", "places/" + PlanSlug(row.place)}, {"build", build.Id()}};
    return {};
}

int GameBuild::AddStoryMarkers(Registry& registry, const Row& row, const json& solved, int originX, int originY) {
    // Its story anchors as markers: on the part that shows the anchor's module, else in the middle.
    const TileGrid grid = TileGrid::Build(registry);
    const Extent extent = OpenExtent(registry, grid);
    const int midX = extent.any ? (extent.minX + extent.maxX) / 2 : 0, midY = extent.any ? (extent.minY + extent.maxY) / 2 : 0;
    const json desc = store_.Read("places/" + PlanSlug(row.place)).value_or(json::object());
    int made = 0;
    // A painted place's markers are already where its painting shows them (PlacePaint): kept.
    std::map<std::string, std::pair<float, float>> placed;
    for (auto [e, m, w] : registry.view<Marker, WorldTransform2D>().each()) placed[m.name] = {w.x, w.y};
    for (const auto& a : desc.value("storyAnchors", json::array())) {
        if (!a.is_object()) continue;
        const std::string id = a.value("id", ""), module = a.value("module", "");
        if (placed.contains(id)) continue;
        std::optional<std::pair<int, int>> at;
        for (const auto& o : solved.value("objects", json::array())) {
            if (!o.is_object()) continue;
            const std::string node = o.value("node", "");
            if (o.value("shows", "") == module || o.value("slot", "") == id || node.size() >= id.size() + 1 && node.compare(node.size() - id.size(), id.size(), id) == 0) {
                at = NearestOpen(grid, o.value("x", 0) + originX, o.value("y", 0) + originY);
                break;
            }
        }
        if (!at) {
            at = NearestOpen(grid, midX + made, midY);
            ++made;
        }
        if (at) AddMarker(registry, id, at->first + 0.5f, at->second + 0.5f);
    }
    if (const auto at = placed.find("arrive"); at != placed.end()) {
        if (row.scene == StartScene()) CreatePlayer(registry, at->second.first, at->second.second);
    } else if (auto arrive = NearestOpen(grid, midX, midY)) {
        AddMarker(registry, "arrive", arrive->first + 0.5f, arrive->second + 0.5f);
        if (row.scene == StartScene()) CreatePlayer(registry, arrive->first + 0.5f, arrive->second + 0.5f);
    }
    return made;
}

using LevelArea = PlaceLevelArea;

// Only what stands in [x0,x1)x[y0,y1) (a cell round it for the walls) is kept: the rest goes.
static void KeepOnly(Registry& registry, int x0, int y0, int x1, int y1, const std::string& level) {
    x0 -= 1, y0 -= 1, x1 += 1, y1 += 1;
    auto inside = [&](float x, float y) { return x >= x0 && y >= y0 && x < x1 && y < y1; };
    std::vector<Entity> gone;
    for (auto e : registry.view<entt::entity>()) {
        // A painted level's backdrop stands on the screen, not a cell: kept with its own level only.
        if (const auto* n = registry.try_get<PlaceNode>(e); n && n->statement.rfind("backdrop#", 0) == 0) {
            if (n->node != level) gone.push_back(e);
            continue;
        }
        if (const auto* t = registry.try_get<Tile>(e)) {
            if (!inside(static_cast<float>(t->gx), static_cast<float>(t->gy))) gone.push_back(e);
        } else if (const auto* w = registry.try_get<WorldTransform2D>(e)) {
            if (!inside(w->x, w->y)) gone.push_back(e);
        }
    }
    for (auto e : gone) registry.destroy(e);
    for (auto& [name, layer] : LayersOf(registry).layers) {
        std::vector<std::pair<int, int>> out;
        layer.ForEach([&](int gx, int gy, uint16_t) {
            if (!inside(static_cast<float>(gx), static_cast<float>(gy))) out.push_back({gx, gy});
        });
        for (const auto& [gx, gy] : out) layer.Erase(gx, gy);
    }
}

void GameBuild::FinishInEditor(Row& row) {
    const Project project{host_.projectRoot};
    const std::string path = project.Resolve(row.scene).string();
    Registry registry;
    if (!LoadScene(registry, path)) throw std::runtime_error("the scene the Build panel saved can't be read: " + row.scene);
    PlaceBuild build(BuildHost(row.place));
    int made = 0;
    std::vector<LevelArea> levels;
    int ox = 0, oy = 0;
    if (build.Open(row.build).empty()) {
        std::tie(ox, oy) = build.Origin();
        made = AddStoryMarkers(registry, row, build.Solved(), ox, oy);
        if (host_.places) levels = PlaceLevelAreas(host_.places(), build.Spec(), build.Solved());
    }
    files_[row.scene] = {{"planner", "places/" + PlanSlug(row.place)}, {"build", row.build}};
    if (levels.empty()) {
        SaveScene(registry, path);
    } else {
        // Each level its own scene (docs/build-fixes.md): the first at the row's scene, the others
        // beside it; stairs lead down to the next level and back up (a gateway on each, arriving by
        // the other level's stairs).
        const std::string whole = SerializeScene(registry);
        const fs::path base = fs::path(row.scene);
        auto sceneOf = [&](size_t i) {
            return i == 0 ? row.scene : (base.parent_path() / (base.stem().string() + "-" + PlanSlug(levels[i].id) + base.extension().string())).generic_string();
        };
        auto stairsOf = [&](const LevelArea& l, bool last) -> std::pair<float, float> {
            if (!l.stairs.empty()) {
                const auto& s = last ? l.stairs.back() : l.stairs.front();
                return {ox + s.first + 0.5f, oy + s.second + 0.5f};
            }
            return {ox + (l.x0 + l.x1) / 2.0f, oy + (l.y0 + l.y1) / 2.0f};
        };
        json sceneList = json::array();
        for (size_t i = 0; i < levels.size(); ++i) {
            Registry level;
            DeserializeScene(level, whole);
            KeepOnly(level, ox + levels[i].x0, oy + levels[i].y0, ox + levels[i].x1, oy + levels[i].y1, levels[i].id);
            auto gateway = [&](float x, float y, const std::string& scene, const std::string& marker, const std::string& label) {
                const Entity g = level.create();
                level.emplace<WorldTransform2D>(g, x, y);
                level.emplace<Gateway>(g, scene, marker);
                level.emplace<Name>(g, label);
            };
            if (i + 1 < levels.size()) {
                const auto [sx, sy] = stairsOf(levels[i], true);
                gateway(sx, sy, sceneOf(i + 1), "from-" + PlanSlug(levels[i].id), "Stairs down to " + levels[i + 1].id);
                AddMarker(level, "from-" + PlanSlug(levels[i + 1].id), sx + 1.0f, sy);
            }
            if (i > 0) {
                const auto [sx, sy] = stairsOf(levels[i], false);
                gateway(sx, sy, sceneOf(i - 1), "from-" + PlanSlug(levels[i].id), "Stairs up to " + levels[i - 1].id);
                AddMarker(level, "from-" + PlanSlug(levels[i - 1].id), sx + 1.0f, sy);
            }
            const std::string file = project.Resolve(sceneOf(i)).string();
            std::error_code ec;
            fs::create_directories(fs::path(file).parent_path(), ec);
            SaveScene(level, file);
            files_[sceneOf(i)] = {{"planner", "places/" + PlanSlug(row.place)}, {"build", row.build}, {"level", levels[i].id}};
            sceneList.push_back(sceneOf(i));
        }
        row.checks["levels"] = sceneList;
    }
    row.checks["anchorsMade"] = made;
    row.checks["builtIn"] = "the Build panel";
    row.status = "built";
    Record("built", row.place, row.scene, "built by the Build panel: art found, scene built and checked");
}

std::string GameBuild::PlaceholderScene(Row& row) {
    // A plain room of floor in walls, big enough to walk and hold its anchors.
    const Project project{host_.projectRoot};
    Registry registry;
    const int w = row.buildType == "settlement" || row.buildType == "wilderness" || row.buildType == "region" ? 24 : 12;
    for (int y = 0; y < w; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool edge = x == 0 || y == 0 || x == w - 1 || y == w - 1;
            CreateTile(registry, x, y, edge ? TileKind::Wall : TileKind::Dirt);
        }
    }
    const json desc = store_.Read("places/" + PlanSlug(row.place)).value_or(json::object());
    int k = 0;
    for (const auto& a : desc.value("storyAnchors", json::array())) {
        AddMarker(registry, a.value("id", ""), 2.5f + static_cast<float>((k * 3) % (w - 4)), 2.5f + static_cast<float>(((k * 3) / (w - 4)) * 2));
        ++k;
    }
    AddMarker(registry, "arrive", w / 2 + 0.5f, w / 2 + 0.5f);
    if (row.scene == StartScene()) CreatePlayer(registry, w / 2 + 0.5f, w / 2 + 0.5f);
    std::error_code ec;
    fs::create_directories(project.Resolve(row.scene).parent_path(), ec);
    SaveScene(registry, project.Resolve(row.scene).string());
    row.status = "placeholder";
    row.checks = {{"placeholder", true}};
    files_[row.scene] = {{"planner", "places/" + PlanSlug(row.place)}, {"placeholder", true}};
    return {};
}

void GameBuild::ConnectScenes() {
    const Project project{host_.projectRoot};
    const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
    std::vector<std::pair<std::string, std::string>> links;
    for (const auto& e : g.edges) {
        if ((e.p == "connects" || e.p == "inside") && FindRow(e.s) && FindRow(e.o)) links.push_back({e.s, e.o});
    }
    // Each scene's gateways go round its edge: east, west, south, north, then inward.
    std::map<std::string, int> used;
    auto place = [&](const std::string& from, const std::string& to) {
        Row* a = FindRow(from);
        Row* b = FindRow(to);
        Registry registry;
        if (!LoadScene(registry, project.Resolve(a->scene).string())) return;
        const TileGrid grid = TileGrid::Build(registry);
        const Extent e = OpenExtent(registry, grid);
        const int k = used[from]++;
        const int midX = (e.minX + e.maxX) / 2, midY = (e.minY + e.maxY) / 2;
        const int targets[4][2] = {{e.maxX, midY}, {e.minX, midY}, {midX, e.maxY}, {midX, e.minY}};
        const auto cell = NearestOpen(grid, targets[k % 4][0], targets[k % 4][1]);
        if (!cell) return;
        CreateGateway(registry, cell->first + 0.5f, cell->second + 0.5f, b->scene, "from-" + PlanSlug(from));
        // Where you arrive coming back: a step inside from the gateway.
        const int ix = cell->first + (cell->first > midX ? -2 : cell->first < midX ? 2 : 0);
        const int iy = cell->second + (cell->second > midY ? -2 : cell->second < midY ? 2 : 0);
        const auto in = NearestOpen(grid, ix, iy);
        if (in) AddMarker(registry, "from-" + PlanSlug(to), in->first + 0.5f, in->second + 0.5f);
        SaveScene(registry, project.Resolve(a->scene).string());
    };
    for (const auto& [a, b] : links) {
        place(a, b);
        place(b, a);
        Record("connect", a + " ↔ " + b, "gateways both ways");
    }
}

// --- ⑪ Populate and bind -------------------------------------------------------------------------------

void GameBuild::Populate() {
    const Project project{host_.projectRoot};
    const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
    const StoryOntology& gso = StoryOntology::Engine();
    std::map<std::string, std::unique_ptr<Registry>> scenes;
    auto sceneOf = [&](const std::string& place) -> Registry* {
        Row* r = FindRow(place);
        if (!r) return nullptr;
        auto& reg = scenes[r->scene];
        if (!reg) {
            reg = std::make_unique<Registry>();
            if (!LoadScene(*reg, project.Resolve(r->scene).string())) return nullptr;
        }
        return reg.get();
    };
    auto spot = [&](Registry& reg, const std::string& marker, int offset) -> std::pair<float, float> {
        if (auto m = MarkerAt(reg, marker)) return *m;
        const TileGrid grid = TileGrid::Build(reg);
        const auto arrive = MarkerAt(reg, "arrive").value_or(std::make_pair(5.5f, 5.5f));
        const auto c = NearestOpen(grid, static_cast<int>(arrive.first) + offset, static_cast<int>(arrive.second) + 1);
        return c ? std::make_pair(c->first + 0.5f, c->second + 0.5f) : arrive;
    };
    auto homeOf = [&](const std::string& character) -> std::string {
        for (const auto* e : g.Out(character, "locatedAt")) {
            if (FindRow(e->o)) return e->o;
        }
        const json c = store_.Read("characters/" + PlanSlug(character)).value_or(json::object());
        if (FindRow(c.value("home", ""))) return c.value("home", "");
        // Where it's seen first.
        for (const auto& s : g.SceneOrder()) {
            const auto who = g.Participants(s);
            if (std::find(who.begin(), who.end(), character) != who.end() && FindRow(g.LocationOfScene(s))) return g.LocationOfScene(s);
        }
        return rows_.empty() ? std::string() : rows_.front().place;
    };
    json actors = json::array(), aliases = json::array();
    std::map<std::string, std::string> entityOf; // character -> entity name
    int offset = 0;
    // The characters, in their homes.
    for (const auto* n : g.OfClass("Character", gso)) {
        if (n->cls == "PlayerCharacter") continue;
        const std::string home = homeOf(n->id);
        Registry* reg = sceneOf(home);
        if (!reg) continue;
        const std::string name = n->Text("name").empty() ? PlanSlug(n->id) : n->Text("name");
        const json c = store_.Read("characters/" + PlanSlug(n->id)).value_or(json::object());
        const auto roles = c.value("gameplay", json::object()).value("roles", json::array());
        bool boss = false;
        for (const auto* e : g.Out(n->id, "fills")) {
            const StoryNode* role = g.Find(e->o);
            if (role && (role->Text("role") == "boss" || role->Text("role") == "enemy" || role->Text("role") == "elite")) boss = true;
        }
        const auto at = spot(*reg, "home-" + PlanSlug(n->id), offset++ % 5);
        Entity e;
        if (boss) {
            e = CreateEnemy(*reg, at.first, at.second, entt::null);
        } else {
            e = CreateNpc(*reg, at.first, at.second);
            reg->emplace_or_replace<Talk>(e, "dialogue/" + PlanSlug(n->id) + ".json", n->id, 1.5f);
            std::string packages;
            for (const auto& p : c.value("behaviour", json::object()).value("packages", json::array())) {
                packages += (packages.empty() ? "" : ", ") + PlanSlug(n->id) + "-" + p.value("template", "");
            }
            if (!packages.empty()) reg->get_or_emplace<Behaviour>(e).packages = packages;
        }
        reg->emplace_or_replace<Name>(e, name);
        entityOf[n->id] = name;
        actors.push_back({{"character", n->id}, {"scene", FindRow(home)->scene}, {"entity", name}, {"boss", boss}});
    }
    // The quests' aliases.
    for (const auto& doc : store_.List("quests")) {
        const json q = store_.Read(doc).value_or(json::object());
        for (const auto& a : q.value("aliases", json::array())) {
            json b = {{"quest", q.value("id", "")}, {"alias", a.value("id", "")}};
            const std::string kind = a.value("kind", ""), fill = a.value("fill", ""), in = a.value("in", "");
            if (kind == "character") {
                if (entityOf.count(fill)) {
                    b["how"] = "fixed";
                    b["entity"] = fill; // talk targets are World Bible keys
                    for (const auto& act : actors) {
                        if (act.value("character", "") == fill) b["scene"] = act.value("scene", "");
                    }
                } else {
                    b["how"] = "failed";
                    b["reason"] = fill + " isn't placed anywhere";
                }
            } else if (kind == "location") {
                const std::string place = fill.empty() ? in : fill;
                if (Row* r = FindRow(place)) {
                    b["how"] = "fixed";
                    b["scene"] = r->scene;
                    b["marker"] = "arrive";
                } else {
                    b["how"] = "failed";
                    b["reason"] = place + " isn't built";
                }
            } else {
                // A spot, an item or a container: its story anchor in its place, else the place's markers.
                const std::string place = !in.empty() ? in : homeOf(fill);
                Registry* reg = sceneOf(place);
                const std::string anchor = a.value("where", json::object()).value("anchor", a.value("id", ""));
                if (!reg) {
                    b["how"] = "failed";
                    b["reason"] = "no scene for " + place;
                } else {
                    std::vector<std::string> candidates;
                    for (auto [e, m] : reg->view<Marker>().each()) candidates.push_back(m.name);
                    const std::string marker = q.value("id", "") + "/" + a.value("id", "");
                    std::pair<float, float> at;
                    if (std::find(candidates.begin(), candidates.end(), anchor) != candidates.end()) {
                        at = *MarkerAt(*reg, anchor);
                        b["how"] = "story-anchor";
                    } else {
                        at = spot(*reg, anchor, offset++ % 5);
                        b["how"] = "made";
                    }
                    b["candidates"] = candidates.size();
                    AddMarker(*reg, marker, at.first, at.second);
                    b["scene"] = FindRow(place)->scene;
                    b["marker"] = marker;
                    if (kind == "item" && !fill.empty()) {
                        const Entity item = reg->create();
                        reg->emplace<WorldTransform2D>(item, at.first, at.second);
                        reg->emplace<ItemPickup>(item, PlanSlug(fill), 1, false);
                        reg->emplace<Name>(item, PlanSlug(fill));
                    }
                }
            }
            aliases.push_back(b);
        }
    }
    for (auto& [scene, reg] : scenes) {
        if (reg) SaveScene(*reg, project.Resolve(scene).string());
    }
    bindings_ = {{"schema", "aige.bindings/1"}, {"aliases", aliases}, {"actors", actors}};
    std::ofstream(store_.Dir() / "bindings.json", std::ios::binary) << bindings_.dump(2);
    int failed = 0;
    for (const auto& b : aliases) failed += b.value("how", "") == "failed";
    Record("bind", "bindings", std::to_string(aliases.size() - failed) + " of " + std::to_string(aliases.size()) + " aliases bound");
}

// --- ⑫ Writing the game ----------------------------------------------------------------------------------

void GameBuild::QueueWriting() {
    // A dialogue for every character someone can talk to.
    for (const auto& a : bindings_.value("actors", json::array())) {
        if (a.value("boss", false)) continue;
        Job job;
        job.id = "w-" + std::to_string(nextJob_++);
        job.kind = "dialogue";
        job.subject = a.value("character", "");
        jobs_.push_back(job);
    }
}

bool GameBuild::DriveJobs() {
    bool did = false;
    auto answers = std::move(jobAnswers_);
    jobAnswers_.clear();
    for (auto& [id, body] : answers) {
        for (auto& job : jobs_) {
            if (job.id != id) continue;
            did = true;
            if (!body.value("ok", false)) {
                job.status = job.attempts < 2 ? "queued" : "failed";
                job.error = body.contains("errors") ? body["errors"].dump() : "no answer";
                if (job.status == "failed") WriteDialogue(job.subject, FallbackDialogue(job.subject), "fallback");
                continue;
            }
            const auto problems = HandleJob(job, body);
            if (problems.empty()) {
                job.status = "done";
            } else if (job.attempts < 2) {
                job.problems = json(problems);
                job.status = "queued";
            } else {
                job.status = "failed";
                job.error = problems.front();
                WriteDialogue(job.subject, FallbackDialogue(job.subject), "fallback");
            }
        }
    }
    for (auto& job : jobs_) {
        if (job.status != "queued") continue;
        const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
        const json c = store_.Read("characters/" + PlanSlug(job.subject)).value_or(json::object());
        json gives = json::array();
        StoryLibrary story(host_.projectRoot);
        for (const auto& doc : store_.List("quests")) {
            const json q = store_.Read(doc).value_or(json::object());
            for (const auto& a : q.value("aliases", json::array())) {
                if (a.value("id", "") == q.value("giver", json::object()).value("alias", "") && a.value("fill", "") == job.subject) {
                    gives.push_back({{"id", q.value("id", "")}, {"name", q.value("name", "")}, {"stages", q.value("stages", json::array())}});
                }
            }
        }
        json knows = json::array();
        for (const auto* e : g.Out(job.subject, "knows")) knows.push_back(e->o.empty() ? e->Text("about") : e->o);
        json scenes = json::array();
        for (const auto& s : g.SceneOrder()) {
            const auto who = g.Participants(s);
            if (std::find(who.begin(), who.end(), job.subject) == who.end()) continue;
            const json sd = store_.Read("story/scenes/" + s).value_or(json::object());
            if (sd.value("card", json::object()).value("mode", "") == "dialogue") scenes.push_back({{"id", s}, {"summary", sd.value("summary", "")}});
        }
        json body = {{"character", c}, {"key", job.subject}, {"gives", gives}, {"knows", knows}, {"scenes", scenes},
                     {"brief", store_.Read("brief").value_or(json::object()).value("logline", "")}};
        if (!job.problems.empty()) body["problems"] = job.problems;
        const int id = host_.request ? host_.request("/planner/dialogue", body, 300) : 0;
        if (id == 0) {
            // Without the AI: a greeting, and the quests it gives.
            WriteDialogue(job.subject, FallbackDialogue(job.subject), "fallback");
            job.status = "done";
            did = true;
            continue;
        }
        pending_[id] = "job:" + job.id;
        job.status = "running";
        ++job.attempts;
        did = true;
    }
    return did;
}

std::vector<std::string> GameBuild::HandleJob(Job& job, const json& answer) {
    if (job.kind != "dialogue") return {};
    json dialogue = answer.value("dialogue", json::object());
    dialogue["speaker"] = job.subject;
    std::string error;
    const std::string path = "dialogue/" + PlanSlug(job.subject) + ".json";
    auto parsed = DialogueFromJson(dialogue, path, &error);
    if (!parsed) return {error};
    // The quests it names must exist: the ones this plan writes.
    std::set<std::string> quests;
    for (const auto& doc : store_.List("quests")) quests.insert(store_.Read(doc).value_or(json::object()).value("id", ""));
    std::vector<std::string> problems;
    for (const auto& node : parsed->nodes) {
        for (const auto& choice : node.choices) {
            for (const auto& effect : choice.effects) {
                if ((effect.op == "start_quest" || effect.op == "complete_quest") && !quests.contains(effect.name)) {
                    problems.push_back("node " + node.id + ": no quest '" + effect.name + "'");
                }
            }
            if (!choice.next.empty() && !parsed->Node(choice.next)) problems.push_back("node " + node.id + ": no node '" + choice.next + "'");
        }
        if (!node.next.empty() && !parsed->Node(node.next)) problems.push_back("node " + node.id + ": no node '" + node.next + "'");
    }
    if (!parsed->Node(parsed->start)) problems.push_back("no start node '" + parsed->start + "'");
    if (!problems.empty()) return problems;
    PlanDecision d;
    d.stage = "write";
    d.actor = "ai";
    d.kind = "write-dialogue";
    d.subject = path;
    d.rationale = answer.value("rationale", "");
    WriteDialogue(job.subject, DialogueToJson(*parsed), store_.Decide(d));
    return {};
}

json GameBuild::FallbackDialogue(const std::string& character) const {
    const json c = store_.Read("characters/" + PlanSlug(character)).value_or(json::object());
    json choices = json::array();
    for (const auto& doc : store_.List("quests")) {
        const json q = store_.Read(doc).value_or(json::object());
        for (const auto& a : q.value("aliases", json::array())) {
            if (a.value("id", "") == q.value("giver", json::object()).value("alias", "") && a.value("fill", "") == character) {
                choices.push_back({{"text", "Can I help? (" + q.value("name", q.value("id", "")) + ")"},
                                   {"effects", {{{"op", "start_quest"}, {"name", q.value("id", "")}}}},
                                   {"conditions", {{{"quest", q.value("id", "")}, {"is", "inactive"}}}}});
            }
        }
    }
    choices.push_back({{"text", "Farewell."}});
    std::string line = c.value("voice", json::object()).value("sample", "");
    if (line.empty()) line = "Well met, traveller.";
    return {{"speaker", character}, {"start", "greet"}, {"nodes", {{{"id", "greet"}, {"text", line}, {"choices", choices}}}}};
}

void GameBuild::WriteDialogue(const std::string& character, const json& dialogue, const std::string& madeBy) {
    json d = dialogue;
    d["speaker"] = character;
    if (!WriteStoryFile(host_.projectRoot, "dialogue", PlanSlug(character), d)) {
        files_["dialogue/" + PlanSlug(character) + ".json"] = {{"planner", "characters/" + PlanSlug(character)}, {"decision", madeBy}};
    }
}

void GameBuild::WriteFiles() {
    const Project project{host_.projectRoot};
    // Quests.
    for (const auto& doc : store_.List("quests")) {
        const json design = store_.Read(doc).value_or(json::object());
        const std::string id = design.value("id", PlanSlug(doc));
        if (!WriteStoryFile(host_.projectRoot, "quests", id, EngineQuestFromDesign(design, bindings_))) {
            files_["quests/" + id + ".json"] = {{"planner", doc}, {"decision", design.value("madeBy", "")}};
        }
    }
    // Cutscenes from the cutscene scenes: their beats as subtitles between fades.
    for (const auto& doc : store_.List("story/scenes")) {
        const json s = store_.Read(doc).value_or(json::object());
        if (s.value("card", json::object()).value("mode", "") != "cutscene" || s.value("status", "") != "written") continue;
        json lines = json::array();
        double t = 0.5;
        for (const auto& beat : s.value("beats", json::array())) {
            if (beat.value("text", "").empty()) continue;
            lines.push_back({{"at", t}, {"text", beat.value("text", "")}, {"seconds", 3.0}});
            t += 3.5;
        }
        if (lines.empty()) continue;
        const json cutscene = {{"name", s.value("summary", PlanSlug(doc))},
                               {"tracks", {{{"type", "screen"}, {"keys", {{{"at", 0.0}, {"op", "fade_in"}, {"seconds", 0.5}},
                                                                         {{"at", t}, {"op", "fade_out"}, {"seconds", 0.5}}}}},
                                           {{"type", "dialogue"}, {"keys", lines}}}}};
        std::string error;
        if (auto parsed = CutsceneFromJson(cutscene, PlanSlug(doc), &error); parsed && ValidateCutscene(*parsed).empty()) {
            if (!WriteCutsceneFile(host_.projectRoot, PlanSlug(doc), cutscene)) {
                files_["cutscenes/" + PlanSlug(doc) + ".json"] = {{"planner", doc}, {"decision", s.value("madeBy", "")}};
            }
        }
    }
    // Records, and the characters' packages.
    RecordDatabase records;
    records.Open(EngineReflection(), host_.projectRoot);
    const json systems = store_.Read("systems").value_or(json::object());
    for (const auto& r : systems.value("records", json::array())) {
        if (!records.Write(r.value("type", ""), r.value("id", ""), r.value("record", json::object()))) {
            files_["records/" + r.value("type", "") + "/" + r.value("id", "") + ".json"] = {{"planner", "systems"}, {"decision", systems.value("madeBy", "")}};
        }
    }
    for (const auto& doc : store_.List("characters")) {
        const json c = store_.Read(doc).value_or(json::object());
        for (const auto& p : c.value("behaviour", json::object()).value("packages", json::array())) {
            const std::string id = PlanSlug(doc) + "-" + p.value("template", "");
            json package = {{"template", p.value("template", "")}, {"description", c.value("name", "") + ": " + p.value("template", "")}};
            if (!p.value("place", "").empty()) package["location"] = {{"in", p.value("place", "")}};
            if (!records.Write("package", id, package)) files_["records/package/" + id + ".json"] = {{"planner", doc}};
        }
    }
    // The interface's theme, and the game's own file.
    const json ui = store_.Read("ui").value_or(json::object());
    const json brief = store_.Read("brief").value_or(json::object());
    const json game = {{"schema", "aige.game/1"},
                       {"title", brief.value("title", "")},
                       {"logline", brief.value("logline", "")},
                       {"genre", brief.value("genre", "")},
                       {"startScene", StartScene()},
                       {"theme", ui.value("theme", "")},
                       {"screens", ui.value("screens", json::array())},
                       {"hud", ui.value("hud", json::array())},
                       {"provenance", {{"planner", "brief"}, {"decision", brief.value("madeBy", "")}}}};
    std::ofstream(host_.projectRoot / "game.json", std::ios::binary) << game.dump(2) << "\n";
    // The interface's theme, for the game's documents (ui/theme.json).
    if (!ui.value("theme", "").empty()) {
        std::error_code themeEc;
        fs::create_directories(host_.projectRoot / "ui", themeEc);
        std::ofstream(host_.projectRoot / "ui" / "theme.json", std::ios::binary) << json{{"base", ui.value("theme", "")}, {"tokens", json::object()}}.dump(2);
        files_["ui/theme.json"] = {{"planner", "ui"}};
    }
    // The game starts in the plan's first place (Project Settings ▸ Game ▸ start scene).
    ActiveSettings().game.startScene = StartScene();
    SaveSettings(EngineReflection(), project.SettingsPath().string());
    files_["game.json"] = {{"planner", "brief"}};
    std::ofstream(store_.Dir() / "files.json", std::ios::binary) << json{{"schema", "aige.plan-files/1"}, {"files", files_}}.dump(2);
    Record("write", "files", std::to_string(files_.size()) + " files");
}

// --- ⑫ The playtest ---------------------------------------------------------------------------------------

void GameBuild::Playtest() {
    const Project project{host_.projectRoot};
    json checks = json::array();
    int passed = 0, total = 0;
    auto check = [&](const std::string& what, bool ok, const std::string& detail = {}) {
        ++total;
        passed += ok;
        checks.push_back({{"check", what}, {"ok", ok}, {"detail", detail}});
    };
    // Every scene loads, with its gateways leading somewhere.
    std::map<std::string, std::unique_ptr<Registry>> scenes;
    for (const auto& r : rows_) {
        auto reg = std::make_unique<Registry>();
        const bool loaded = LoadScene(*reg, project.Resolve(r.scene).string());
        check("scene " + r.scene + " loads", loaded && reg->view<WorldTransform2D>().size() > 0);
        if (loaded) scenes[r.scene] = std::move(reg);
    }
    const auto gatewayProblems = GatewayProblems(project);
    check("every gateway leads to a scene and marker", gatewayProblems.empty(), gatewayProblems.empty() ? "" : gatewayProblems.front());
    // The scenes reachable from the start through gateways.
    std::set<std::string> reachable;
    std::vector<std::string> todo = {StartScene()};
    while (!todo.empty()) {
        const std::string s = todo.back();
        todo.pop_back();
        if (!reachable.insert(s).second || !scenes.count(s)) continue;
        for (auto [e, gw] : scenes[s]->view<Gateway>().each()) todo.push_back(gw.scene);
    }
    for (const auto& r : rows_) check("scene " + r.scene + " can be reached from the start", reachable.contains(r.scene));
    // Can someone walk there in its scene from where they arrive?
    auto walkable = [&](Registry& reg, float x, float y) {
        const TileGrid grid = TileGrid::Build(reg);
        auto from = MarkerAt(reg, "arrive");
        if (!from) return true;
        const auto path = FindPath([&](int gx, int gy) { return grid.IsSolid(gx, gy); },
                                   {static_cast<int>(std::floor(from->first)), static_cast<int>(std::floor(from->second))},
                                   {static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))});
        return path.has_value();
    };
    // Quests and dialogues.
    StoryLibrary story(host_.projectRoot);
    story.Refresh();
    json quests = json::array();
    for (const auto& doc : store_.List("quests")) {
        const std::string id = store_.Read(doc).value_or(json::object()).value("id", PlanSlug(doc));
        const Quest* quest = story.FindQuest(id);
        json result = {{"id", id}};
        if (!quest) {
            result["result"] = "skipped";
            result["reason"] = "its file didn't load";
            quests.push_back(result);
            check("quest " + id + " loads", false);
            continue;
        }
        const auto problems = ValidateQuest(*quest, [&](const std::string& q) { return story.FindQuest(q); });
        check("quest " + id + " is valid", problems.empty(), problems.empty() ? "" : problems.front());
        std::string stuck;
        for (const auto& stage : quest->stages) {
            for (const auto& o : stage.objectives) {
                const std::string target = quest->Resolve(o.target);
                bool found = target.empty() || o.type == "survive" || o.type == "have" || o.type == "collect";
                for (auto& [scene, reg] : scenes) {
                    if (found || !reachable.contains(scene)) continue;
                    if (o.type == "talk" || o.type == "deliver" || o.type == "escort") {
                        for (auto [e, talk, at] : reg->view<Talk, WorldTransform2D>().each()) {
                            if (talk.character == target && walkable(*reg, at.x, at.y)) found = true;
                        }
                    } else if (o.type == "defeat") {
                        for (auto [e, name, at] : reg->view<Name, WorldTransform2D>().each()) {
                            if ((name.value == target || target == "Enemy" || target == PlanSlug(target)) && reg->all_of<Health>(e) && walkable(*reg, at.x, at.y)) found = true;
                        }
                        if (!found && target.find('/') != std::string::npos) {
                            // A character key: the enemy bound for it is named by the character.
                            for (const auto& a : bindings_.value("actors", json::array())) {
                                if (a.value("character", "") == target && a.value("scene", "") == scene) found = true;
                            }
                        }
                    } else { // reach, use
                        if (auto m = MarkerAt(*reg, target)) found = walkable(*reg, m->first, m->second);
                        if (!found) {
                            for (auto [e, name, at] : reg->view<Name, WorldTransform2D>().each()) {
                                if (name.value == target) found = walkable(*reg, at.x, at.y);
                            }
                        }
                    }
                }
                if (!found && stuck.empty()) stuck = "stage " + std::to_string(stage.index) + ": nowhere to " + o.type + " '" + target + "'";
            }
        }
        result["result"] = stuck.empty() ? "completed" : "stuck";
        if (!stuck.empty()) result["reason"] = stuck;
        check("quest " + id + " can be walked to the end", stuck.empty(), stuck);
        quests.push_back(result);
    }
    for (const auto& d : story.Dialogues()) {
        const auto problems = ValidateDialogue(d, [&](const std::string& q) { return story.FindQuest(q); });
        check("dialogue " + d.path + " is valid", problems.empty(), problems.empty() ? "" : problems.front());
    }
    playtest_ = {{"schema", "aige.plan-report/1"}, {"kind", "playtest"}, {"quests", quests}, {"checks", checks},
                 {"summary", {{"passed", passed}, {"total", total}}},
                 {"note", "A walk through the game's logic: scenes, gateways, quests and dialogue (not simulated combat)."}};
    std::ofstream(store_.Dir() / "playtest.json", std::ios::binary) << playtest_.dump(2);
    Record("playtest", "playtest", std::to_string(passed) + "/" + std::to_string(total) + " checks passed");
}

// --- ⑬ The result --------------------------------------------------------------------------------------------

void GameBuild::WriteResult() {
    const json brief = store_.Read("brief").value_or(json::object());
    const StoryGraph g = StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
    int built = 0, placeholders = 0;
    json places = json::array();
    for (const auto& r : rows_) {
        built += r.status == "built";
        placeholders += r.status == "placeholder";
        places.push_back({{"place", r.place}, {"scene", r.scene}, {"build", r.build}, {"status", r.status}, {"checks", r.checks}, {"error", r.error}});
    }
    int completed = 0;
    for (const auto& q : playtest_.value("quests", json::array())) completed += q.value("result", "") == "completed";
    const size_t quests = store_.List("quests").size();
    auto range = [&](const char* path) {
        const json v = BriefGet(brief, path);
        return v.is_array() && v.size() == 2 ? v[0].dump() + "–" + v[1].dump() : v.dump();
    };
    json asked = json::array();
    asked.push_back({{"what", "places"}, {"asked", range("scope.places")}, {"made", rows_.size()}});
    asked.push_back({{"what", "story scenes"}, {"asked", range("scope.scenes")}, {"made", g.SceneOrder().size()}});
    asked.push_back({{"what", "side quests"}, {"asked", range("scope.sideQuests")}, {"made", quests > 0 ? quests - 1 : 0}});
    asked.push_back({{"what", "characters"}, {"asked", BriefGet(brief, "cast").dump()}, {"made", g.OfClass("Character", StoryOntology::Engine()).size()}});
    const std::string headline = "Planned and built " + brief.value("title", "the game") + ": " + std::to_string(g.SceneOrder().size()) + " scenes, " +
                                 std::to_string(g.OfClass("Character", StoryOntology::Engine()).size()) + " characters, " + std::to_string(quests) +
                                 " quests, " + std::to_string(rows_.size()) + " places (" + std::to_string(placeholders) + " placeholders); " +
                                 std::to_string(completed) + " of " + std::to_string(quests) + " quests can be finished.";
    const json result = {{"schema", "aige.plan-result/1"},
                         {"headline", headline},
                         {"askedVsMade", asked},
                         {"places", places},
                         {"quests", playtest_.value("quests", json::array())},
                         {"checks", playtest_.value("summary", json::object())},
                         {"placeholders", placeholders},
                         {"files", files_.size()},
                         {"startScene", StartScene()},
                         {"whatNext", {"Play from the start", "Open a place's build to refine it", "Rebuild a placeholder place", "Add a side quest"}}};
    std::ofstream(store_.Dir() / "result.json", std::ios::binary) << result.dump(2);
    message_ = headline;
    Record("result", "result", headline);
}

void GameBuild::Next(const std::string& stage) {
    stage_ = stage;
    static const std::map<std::string, std::string> labels = {
        {"populate", "⑪ Placing characters and binding the quests…"}, {"write", "⑫ Writing the game's files…"},
        {"playtest", "⑫ Playtesting…"}, {"result", "⑬ The result…"}};
    if (labels.count(stage)) message_ = labels.at(stage);
}

bool GameBuild::Pump() {
    if (status_ != "working") return false;
    bool did = false;
    if (stage_ == "build") {
        did = DriveBuilds() || did;
        const bool done = std::all_of(rows_.begin(), rows_.end(), [](const Row& r) {
            return r.status == "built" || r.status == "placeholder" || (r.status == "failed" && r.mode == "manual");
        });
        const bool waitingOnYou = std::any_of(rows_.begin(), rows_.end(), [](const Row& r) { return r.status == "needs-you"; });
        if (waitingOnYou && !did) message_ = "⑩ A place waits for you in the Build panel.";
        if (done) {
            ConnectScenes();
            Next("populate");
            did = true;
        }
    }
    if (stage_ == "populate") {
        Populate();
        Next("write");
        QueueWriting();
        did = true;
    }
    if (stage_ == "write") {
        did = DriveJobs() || did;
        const bool idle = std::none_of(jobs_.begin(), jobs_.end(), [](const Job& j) { return j.status == "queued" || j.status == "running"; });
        if (idle) {
            WriteFiles();
            Next("playtest");
            did = true;
        }
    }
    if (stage_ == "playtest") {
        Playtest();
        Next("result");
        did = true;
    }
    if (stage_ == "result") {
        WriteResult();
        stage_ = "done";
        status_ = "done";
        did = true;
    }
    if (did) Save();
    return did;
}

// --- Views ------------------------------------------------------------------------------------------------------

json GameBuild::State() const {
    json rows = json::array();
    for (const auto& r : rows_) {
        // The place builder's own step for the place being built (request, assets, plan, layout...).
        std::string step, phase;
        if (r.status == "building") {
            const auto b = builds_.find(r.place);
            if (b != builds_.end() && b->second) {
                step = b->second->Stage();
                const json s = b->second->State();
                if (s.is_object() && s.value("planPhase", json()).is_string()) phase = s["planPhase"].get<std::string>();
            }
        }
        rows.push_back({{"place", r.place}, {"scene", r.scene}, {"buildType", r.buildType}, {"mode", r.mode}, {"status", r.status},
                        {"step", step}, {"planPhase", phase}, {"build", r.build}, {"error", r.error}, {"checks", r.checks}, {"dependsOn", r.dependsOn}});
    }
    std::ifstream in(store_.Dir() / "result.json", std::ios::binary);
    const json result = json::parse(in, nullptr, false);
    return {{"active", Active()},  {"stage", stage_},           {"status", status_},  {"message", message_}, {"mode", mode_},
            {"rows", rows},        {"bindings", bindings_},     {"playtest", playtest_}, {"files", files_.size()},
            {"result", result.is_discarded() ? json() : result}};
}

json GameBuild::Card() const {
    int built = 0;
    for (const auto& r : rows_) built += r.status == "built" || r.status == "placeholder";
    return {{"stage", stage_}, {"status", status_}, {"message", message_}, {"progress", {{"done", built}, {"total", rows_.size()}}}};
}

} // namespace aige
