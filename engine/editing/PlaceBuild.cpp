#include "editing/PlaceBuild.h"

#include "editing/BuildWindow.h"
#include "places/PlaceValidate.h"
#include "assets/LayoutImage.h"
#include "assets/TextureCook.h"
#include "places/Compile.h"
#include "places/Planner.h"
#include "places/Solver.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>

#include <set>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

const std::set<std::string> kSkipWords = {"a", "an", "the", "build", "make", "create", "generate", "me", "please",
                                          "with", "and", "of", "for", "some", "design", "us", "i", "want", "to"};

uint64_t Fnv1a(const std::string& text) {
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char c : text) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::vector<PlaceProblem> ErrorsOf(const std::vector<PlaceProblem>& problems) {
    std::vector<PlaceProblem> out;
    for (const auto& p : problems) {
        if (!p.warning) out.push_back(p);
    }
    return out;
}

json ProblemsJson(const std::vector<PlaceProblem>& problems) {
    json out = json::array();
    for (const auto& p : problems) out.push_back({{"where", p.where}, {"message", p.message}, {"warning", p.warning}});
    return out;
}

json ReadJson(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return json();
    json value = json::parse(in, nullptr, false);
    return value.is_discarded() ? json() : value;
}

void WriteJson(const fs::path& path, const json& value) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << value.dump(2) << "\n";
}

} // namespace

std::string PlaceIdFor(const fs::path& projectRoot, const std::string& query) {
    std::string slug, word;
    int words = 0;
    auto flush = [&] {
        if (!word.empty() && !kSkipWords.contains(word) && words < 4) {
            slug += (slug.empty() ? "" : "-") + word;
            ++words;
        }
        word.clear();
    };
    for (const char c : query) {
        if (std::isalnum(static_cast<unsigned char>(c))) word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else flush();
    }
    flush();
    if (slug.empty()) slug = "place";
    std::string id = slug;
    std::error_code ec;
    for (int n = 2; fs::exists(projectRoot / "places" / id, ec); ++n) id = slug + "-" + std::to_string(n);
    return id;
}

std::string PlaceBuild::Start(const std::string& query) {
    if (query.find_first_not_of(" \t\r\n") == std::string::npos) return "say what to build";
    id_ = PlaceIdFor(host_.projectRoot, query);
    query_ = query;
    round_ = 1;
    drafts_.clear();
    draftProblems_.clear();
    errors_.clear();
    brief_ = json::object();
    merge_ = {};
    context_ = InterpretContext(host_.ontology());
    const int id = host_.request ? host_.request("/places/interpret", {{"query", query}, {"context", context_}, {"drafts", 3}}, 600) : 0;
    if (id == 0) {
        stage_ = "failed";
        errors_.push_back("Turn AI on to build places from a request");
        return errors_.back();
    }
    pending_[id] = {"interpret", -1};
    stage_ = "interpreting";
    status_ = "Reading your request…";
    WriteJson(Folder() / "request.json", {{"query", query}});
    Log({{"step", "start"}, {"query", query}});
    return {};
}

void PlaceBuild::Log(json entry) const {
    if (id_.empty()) return;
    entry["time"] = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::error_code ec;
    fs::create_directories(Folder(), ec);
    std::ofstream(Folder() / "log.jsonl", std::ios::app) << entry.dump() << "\n";
}

json PlaceBuild::Defaults() const { return ReadJson(host_.projectRoot / "places" / "defaults.json"); }

void PlaceBuild::OnResponse(int requestId, const json& body) {
    auto it = pending_.find(requestId);
    if (it == pending_.end()) return;
    const Pending what = it->second;
    pending_.erase(it);
    if (stage_ == "planning") {
        OnPlanAnswer(what, body);
        return;
    }
    if (stage_ != "interpreting") return; // cancelled meanwhile

    const Ontology& ontology = host_.ontology();
    if (what.kind == "interpret") {
        for (const auto& w : body.value("warnings", json::array())) {
            if (w.is_string()) Log({{"step", "interpret"}, {"warning", w}});
        }
        if (!body.value("ok", false)) {
            stage_ = "failed";
            for (const auto& e : body.value("errors", json::array())) {
                if (e.is_string()) errors_.push_back(e.get<std::string>());
            }
            if (errors_.empty()) errors_.push_back("the AI couldn't read the request");
            Log({{"step", "interpret"}, {"errors", errors_}});
            return;
        }
        for (const auto& d : body.value("drafts", json::array())) {
            Log({{"step", "interpret"}, {"reading", d.value("reading", "")}, {"mode", d.value("mode", "")},
                 {"thinking", d.value("thinking", "")}, {"answer", d.value("brief", json())},
                 {"confidence", d.value("confidence", json())}, {"errors", d.value("errors", json::array())}});
            if (!d.contains("brief")) continue;
            drafts_.push_back(d);
            const json complete = CompleteBrief(ontology, d["brief"], Defaults());
            draftProblems_.push_back(ErrorsOf(ValidateBrief(ontology, complete)));
        }
        // One repair round for each draft that breaks the rules.
        for (size_t i = 0; i < drafts_.size(); ++i) {
            if (draftProblems_[i].empty()) continue;
            const int id = host_.request("/places/repair-brief", {{"query", query_}, {"context", context_},
                                                                    {"brief", drafts_[i]["brief"]},
                                                                    {"problems", ProblemsJson(draftProblems_[i])}},
                                         300);
            if (id != 0) pending_[id] = {"repair", static_cast<int>(i)};
        }
        status_ = pending_.empty() ? status_ : "Fixing " + std::to_string(pending_.size()) + " draft(s)…";
    } else if (what.kind == "repair" && what.draft >= 0 && what.draft < static_cast<int>(drafts_.size())) {
        Log({{"step", "repair"}, {"reading", drafts_[what.draft].value("reading", "")}, {"answer", body.value("brief", json())},
             {"before", ProblemsJson(draftProblems_[what.draft])}, {"errors", body.value("errors", json::array())}});
        if (body.value("ok", false) && body.contains("brief")) {
            json& draft = drafts_[what.draft];
            draft["brief"] = body["brief"];
            for (const char* key : {"confidence", "assumptions", "questions"}) {
                if (body.contains(key)) draft[key] = body[key];
            }
            draftProblems_[what.draft] = ErrorsOf(ValidateBrief(ontology, CompleteBrief(ontology, draft["brief"], Defaults())));
        }
    }
    if (pending_.empty()) FinishInterpreting();
}

void PlaceBuild::FinishInterpreting() {
    const Ontology& ontology = host_.ontology();
    // Merge the drafts that follow the rules; if none does, all of them (the window shows what's wrong).
    std::vector<json> good;
    for (size_t i = 0; i < drafts_.size(); ++i) {
        if (draftProblems_[i].empty()) good.push_back(drafts_[i]);
    }
    if (good.empty()) good = drafts_;
    if (good.empty()) {
        stage_ = "failed";
        errors_.push_back("the AI wrote no usable Brief");
        return;
    }
    merge_ = MergeBriefs(good);
    brief_ = CompleteBrief(ontology, merge_.brief, Defaults());
    WriteJson(Folder() / "drafts.json", drafts_);
    stage_ = "request";
    status_.clear();
    Log({{"step", "merged"}, {"brief", brief_}, {"drafts", good.size()},
         {"disputed", [&] {
              json list = json::array();
              for (const auto& f : merge_.fields) {
                  if (f.disputed) list.push_back(f.path);
              }
              return list;
          }()}});
}

void PlaceBuild::Edit(const std::string& path, const json& value) {
    if (stage_ != "request" && stage_ != "assets") return;
    ApplyWindowEdit(host_.ontology(), brief_, path, value);
}

std::string PlaceBuild::Step(const std::string& step) {
    if (stage_ == "plan" && (step == "request" || step == "assets")) {
        // Back from the plan: it's made again after the next Build.
        spec_ = json();
        planProblems_.clear();
        stage_ = "assets";
    }
    if (stage_ != "request" && stage_ != "assets") return "nothing to confirm yet";
    if (step == "request") {
        stage_ = "request";
        round_ = 1;
        return {};
    }
    if (step == "assets") {
        const auto errors = ErrorsOf(ValidateBrief(host_.ontology(), brief_));
        if (!errors.empty()) return errors.front().where + ": " + errors.front().message;
        stage_ = "assets";
        round_ = 2;
        return {};
    }
    return "unknown step " + step;
}

std::string PlaceBuild::Confirm() {
    if (stage_ != "assets") return "confirm the request first";
    const auto errors = ErrorsOf(ValidateBrief(host_.ontology(), brief_));
    if (!errors.empty()) return errors.front().where + ": " + errors.front().message;
    WriteJson(Folder() / "brief.json", brief_);
    WriteJson(host_.projectRoot / "places" / "defaults.json", DefaultsAfter(brief_, Defaults()));
    Log({{"step", "confirmed"}, {"brief", brief_}});
    StartPlanning();
    return {};
}

std::string PlaceBuild::Approve() {
    if (stage_ != "plan") return "there's no plan to approve";
    const auto errors = ErrorsOf(planProblems_);
    if (!errors.empty()) return errors.front().where + ": " + errors.front().message;
    stage_ = "planned";
    Log({{"step", "approved"}});
    // Compile and solve (7.6): the layout, cell by cell.
    CompileOptions options;
    options.seed = Fnv1a(id_);
    options.side = SiteSide();
    options.isoGrid = host_.isoGrid ? host_.isoGrid() : true;
    const json ir = CompileSpec(host_.ontology(), spec_, options);
    const auto irProblems = ValidateLayout(host_.ontology(), ir);
    WriteJson(Folder() / "layout.ir.json", ir);
    const SolvedLayout solved = SolveLayout(host_.ontology(), ir);
    solved_ = solved.document;
    WriteJson(Folder() / "layout.solved.json", solved_);
    int buildings = 0;
    for (const auto& o : solved_.value("objects", json::array())) buildings += o.value("layer", "") == "building";
    int failed = 0;
    for (const auto& c : solved_.value("checks", json::array())) failed += !c.value("ok", true);
    // The map of it, with its legend: places/<id>/layout.png.
    {
        LayoutImageOptions image;
        std::map<std::string, int> seen, total;
        for (const auto& c : spec_["root"].value("children", json::array())) total[c.value("module", "")]++;
        for (const auto& c : spec_["root"].value("children", json::array())) {
            const ModuleDef* m = host_.ontology().Module(c.value("module", ""));
            if (m == nullptr) continue;
            const int n = ++seen[m->id];
            image.names[c.value("id", "")] = m->name + (total[m->id] > 1 ? " " + std::to_string(n) : "");
        }
        std::function<void(const json&)> walk = [&](const json& node) {
            image.modules[node.value("id", "")] = node.value("module", "");
            for (const auto& c : node.value("children", json::array())) walk(c);
        };
        walk(spec_["root"]);
        for (const auto& [id, m] : host_.ontology().Modules()) image.moduleNames[id] = m.name;
        int zones = 0;
        for (const auto& [module, count] : seen) {
            const ModuleDef* m = host_.ontology().Module(module);
            if (m && m->level == PlaceLevel::Zone) zones += count;
        }
        image.title = brief_.value("name", id_) + ": " + std::to_string(buildings) + " buildings in " + std::to_string(zones) + " zones";
        WritePng(Folder() / "layout.png", RenderLayoutImage(solved_, image));
        ++imageVersion_;
    }
    status_ = "Laid out " + std::to_string(buildings) + " buildings" +
              (failed ? " (" + std::to_string(failed) + " check(s) to look at)" : "") +
              ". Putting it into the scene comes next (milestone 7.7).";
    Log({{"step", "solved"}, {"irProblems", ProblemsJson(irProblems)}, {"problems", solved.problems},
         {"stats", solved_.value("stats", json())}});
    return {};
}

int PlaceBuild::SiteSide() const {
    const json area = BriefValue(brief_, "scale.area.area");
    double a = 0;
    if (area.is_number()) a = area.get<double>();
    if (area.is_array() && area.size() == 2) a = (area[0].get<double>() + area[1].get<double>()) / 2;
    return a > 0 ? static_cast<int>(std::lround(std::sqrt(a))) : 80;
}

// --- Planning -----------------------------------------------------------------------------

void PlaceBuild::PlanFailed(const std::string& why) {
    stage_ = "failed";
    errors_.push_back("planning: " + why);
    Log({{"step", "plan-failed"}, {"why", why}});
}

void PlaceBuild::StartPlanning() {
    stage_ = "planning";
    planPhase_ = "root";
    repaired_.clear();
    planProblems_.clear();
    zonesTotal_ = zonesDone_ = 0;
    spec_ = json();
    const int id = host_.request("/places/plan-root", {{"brief", brief_}, {"context", PlanRootContext(host_.ontology(), brief_)}}, 600);
    if (id == 0) return PlanFailed("the AI isn't available");
    pending_[id] = {"plan-root", -1, ""};
    status_ = "Planning the zones, roads and walls…";
}

void PlaceBuild::OnPlanAnswer(const Pending& what, const json& body) {
    const Ontology& ontology = host_.ontology();
    Log({{"step", what.kind}, {"node", what.node}, {"mode", body.value("mode", "")}, {"thinking", body.value("thinking", "")},
         {"answer", body.value("answer", json())}, {"errors", body.value("errors", json::array())}});
    const bool ok = body.value("ok", false) && body.contains("answer");
    if (what.kind == "plan-root") {
        if (!ok) {
            const json errors = body.value("errors", json::array());
            return PlanFailed(errors.empty() || !errors[0].is_string() ? "no answer" : errors[0].get<std::string>());
        }
        spec_ = SpecFromRoot(ontology, brief_, body["answer"], id_);
        if (!RepairNodes("root")) PlanZones();
        return;
    }
    if (what.kind == "plan-zone") {
        if (ok) ApplyZoneAnswer(ontology, brief_, spec_, what.node, body["answer"]);
        ++zonesDone_;
        status_ = "Planned " + std::to_string(zonesDone_) + " of " + std::to_string(zonesTotal_) + " zones…";
    } else if (what.kind == "repair-node" && ok) {
        const json& answer = body["answer"];
        if (json* node = SpecNode(spec_, what.node)) {
            if (planPhase_ == "zones") {
                ApplyZoneAnswer(ontology, brief_, spec_, what.node, answer);
            } else {
                for (const char* key : {"quantity", "quality"}) {
                    if (answer.contains(key) && answer[key].is_object()) (*node)[key] = answer[key];
                }
            }
        }
    }
    if (!pending_.empty()) return;
    // A phase is done when its answers (and repairs) are all in.
    if (what.kind == "plan-zone" && RepairNodes("zones")) return;
    if (planPhase_ == "root") PlanZones();
    else FinishPlanning();
}

bool PlaceBuild::RepairNodes(const std::string& phase) {
    planPhase_ = phase;
    const Ontology& ontology = host_.ontology();
    const auto problems = ErrorsOf(ValidateSpec(ontology, spec_, SpecStage::Draft, brief_.value("buildType", "")));
    // Each node's problems, for nodes of this phase that haven't had their repair yet.
    std::map<std::string, std::vector<PlaceProblem>> byNode;
    const auto zones = ZoneIds(ontology, spec_);
    for (const auto& p : problems) {
        const std::string node = p.where.substr(0, p.where.find('.'));
        // The root step wrote the root and its children's own fields; a zone
        // step its zone's fields again and everything inside the zone.
        const bool inside = node.find('/') != std::string::npos;
        const bool zone = std::find(zones.begin(), zones.end(), node) != zones.end();
        if (phase == "root" ? inside : !(inside || zone)) continue;
        // A zone's parcel problems go to the zone: its step wrote them.
        const std::string owner = phase == "zones" && node.find('/') != std::string::npos ? node.substr(0, node.find('/')) : node;
        if (!repaired_.contains(owner)) byNode[owner].push_back(p);
    }
    for (const auto& [nodeId, list] : byNode) {
        json* node = SpecNode(spec_, nodeId);
        if (node == nullptr) continue;
        json given = *node;
        if (phase == "root") given.erase("children");
        json listed = json::array();
        for (const auto& p : list) listed.push_back({{"where", p.where}, {"message", p.message}});
        const json context = phase == "root" ? ontology.Subgraph(node->value("module", ""))
                                             : PlanZoneContext(ontology, brief_, node->value("module", ""));
        const int id = host_.request("/places/repair-node", {{"node", given}, {"problems", listed}, {"context", context}}, 300);
        if (id == 0) continue;
        pending_[id] = {"repair-node", -1, nodeId};
        repaired_.insert(nodeId);
    }
    if (!pending_.empty()) status_ = "Fixing " + std::to_string(pending_.size()) + " part(s) of the plan…";
    return !pending_.empty();
}

void PlaceBuild::PlanZones() {
    planPhase_ = "zones";
    const Ontology& ontology = host_.ontology();
    const auto zones = ZoneIds(ontology, spec_);
    json siblings = json::array();
    for (const auto& c : spec_["root"].value("children", json::array())) {
        siblings.push_back({{"id", c.value("id", "")}, {"module", c.value("module", "")}, {"quantity", c.value("quantity", json())},
                            {"quality", c.value("quality", json())}});
    }
    zonesTotal_ = static_cast<int>(zones.size());
    zonesDone_ = 0;
    for (const auto& zoneId : zones) {
        json zone = *SpecNode(spec_, zoneId);
        zone.erase("children");
        const int id = host_.request("/places/plan-zone",
                                     {{"brief", brief_}, {"zone", zone}, {"siblings", siblings},
                                      {"context", PlanZoneContext(ontology, brief_, zone.value("module", ""))}},
                                     600);
        if (id != 0) pending_[id] = {"plan-zone", -1, zoneId};
    }
    if (pending_.empty()) {
        FinishPlanning();
        return;
    }
    status_ = "Planning " + std::to_string(zonesTotal_) + " zones…";
}

void PlaceBuild::FinishPlanning() {
    const Ontology& ontology = host_.ontology();
    ExpandSpec(ontology, brief_, spec_);
    const int buildings = RebalanceBuildings(ontology, brief_, spec_);
    planProblems_ = ValidateSpec(ontology, spec_, SpecStage::Final, brief_.value("buildType", ""));
    WriteJson(Folder() / "spec.json", spec_);
    stage_ = "plan";
    status_.clear();
    Log({{"step", "planned"}, {"buildings", buildings}, {"problems", ProblemsJson(planProblems_)}});
}

void PlaceBuild::Cancel() {
    if (!Active()) return;
    pending_.clear();
    stage_ = "cancelled";
    Log({{"step", "cancelled"}});
}

json PlaceBuild::State() {
    json state = {{"type", "build:state"}, {"id", id_}, {"query", query_}, {"stage", stage_}, {"round", round_},
                  {"status", status_}, {"errors", errors_}, {"drafts", drafts_.size()}};
    if (stage_ == "plan" || stage_ == "planned") {
        state["plan"] = PlanSummary(host_.ontology(), spec_);
        state["sketch"] = PlanSketch(host_.ontology(), spec_, SiteSide());
        state["problems"] = ProblemsJson(planProblems_);
        if (solved_.is_object()) {
            state["image"] = "places/" + id_ + "/layout.png?v=" + std::to_string(imageVersion_);
            state["solved"] = {{"stats", solved_.value("stats", json())}, {"checks", solved_.value("checks", json::array())},
                               {"problems", solved_.value("problems", json::array())}};
        }
        state["name"] = brief_.value("name", id_);
    }
    if (stage_ == "planning") state["progress"] = {{"zones", zonesTotal_}, {"done", zonesDone_}};
    if (stage_ == "request" || stage_ == "assets") {
        const Catalog catalog = host_.catalog ? host_.catalog() : Catalog{};
        const bool iso = host_.isoGrid ? host_.isoGrid() : true;
        state["window"] = BriefWindow(host_.ontology(), brief_, &merge_, &catalog, iso, round_);
        state["name"] = brief_.value("name", id_);
    }
    return state;
}

json PlaceBuild::Card() const {
    std::string text;
    if (stage_ == "interpreting") text = status_.empty() ? "Reading your request…" : status_;
    else if (stage_ == "request") text = "① Request: confirm what to build";
    else if (stage_ == "assets") text = "② Assets: choose where the art comes from";
    else if (stage_ == "planning") text = status_.empty() ? "Planning…" : status_;
    else if (stage_ == "plan") text = "③ Plan: review the zones before building";
    else if (stage_ == "planned") text = "Plan approved; building comes next";
    else if (stage_ == "cancelled") text = "Cancelled";
    else if (stage_ == "failed") text = errors_.empty() ? "Failed" : errors_.front();
    return {{"type", "build:card"}, {"buildId", id_}, {"name", brief_.is_object() ? brief_.value("name", "") : ""},
            {"stage", stage_}, {"text", text}, {"query", query_}};
}

} // namespace aige
