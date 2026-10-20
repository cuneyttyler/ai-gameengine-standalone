#include "editing/PlaceBuild.h"

#include "assets/LayoutImage.h"
#include "assets/TextureCook.h"
#include "editing/BuildWindow.h"
#include "places/Compile.h"
#include "places/PlaceValidate.h"
#include "places/Planner.h"
#include "places/Solver.h"

#include <algorithm>
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
constexpr int kAttempts = 3; // an AI call is tried this often before the step falls back

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

std::vector<PlaceProblem> ProblemsFrom(const json& list) {
    std::vector<PlaceProblem> out;
    for (const auto& p : list) {
        if (p.is_object()) out.push_back({p.value("where", ""), p.value("message", ""), p.value("warning", false)});
    }
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

std::string FirstError(const json& body) {
    for (const auto& e : body.value("errors", json::array())) {
        if (e.is_string()) return e.get<std::string>();
    }
    return "no answer";
}

// A failure no retry can fix: the service says it has no model, or the request is empty.
bool Hopeless(const json& body) {
    const std::string e = FirstError(body);
    return e.find("No AI provider") != std::string::npos || e.find("Say what") != std::string::npos;
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

json ListBuilds(const fs::path& projectRoot) {
    std::vector<std::pair<fs::file_time_type, json>> found;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(projectRoot / "places", ec)) {
        if (!entry.is_directory()) continue;
        const fs::path folder = entry.path();
        const json request = ReadJson(folder / "request.json");
        if (!request.is_object()) continue;
        const json session = ReadJson(folder / "session.json");
        // A build from before sessions were kept: its stage from the files it left.
        std::string stage = session.is_object() ? session.value("stage", "") : "";
        if (stage.empty()) {
            stage = fs::exists(folder / "layout.solved.json") ? "planned"
                    : fs::exists(folder / "spec.json")        ? "plan"
                    : fs::exists(folder / "brief.json")       ? "assets"
                    : fs::exists(folder / "drafts.json")      ? "request"
                                                              : "interpreting";
        }
        const json brief = session.is_object() ? session.value("brief", json()) : ReadJson(folder / "brief.json");
        fs::file_time_type updated{};
        for (const auto& file : fs::directory_iterator(folder, ec)) updated = std::max(updated, fs::last_write_time(file.path(), ec));
        found.push_back({updated, {{"id", folder.filename().string()}, {"query", request.value("query", "")},
                                   {"name", brief.is_object() ? brief.value("name", "") : ""}, {"stage", stage}}});
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    json out = json::array();
    for (const auto& [time, build] : found) {
        json b = build;
        b["updated"] = std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
        out.push_back(b);
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> PlaceBuild::DirectionsIn(const std::string& request, const std::string& moving) const {
    std::vector<std::pair<std::string, std::string>> out;
    std::string text = request;
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const std::vector<std::pair<std::string, std::string>> words = {
        {"below", "south-of"}, {"under", "south-of"}, {"beneath", "south-of"}, {"south of", "south-of"},
        {"above", "north-of"}, {"over", "north-of"}, {"north of", "north-of"},
        {"left of", "west-of"}, {"west of", "west-of"}, {"right of", "east-of"}, {"east of", "east-of"}};
    if (!spec_.contains("root")) return out;
    for (const auto& [word, type] : words) {
        for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) {
            // The part named right after the word (within a few words): "below the docks".
            const std::string after = text.substr(at + word.size(), 40);
            std::string best;
            size_t bestAt = std::string::npos;
            for (const auto& c : spec_["root"]["children"]) {
                const std::string id = c.value("id", "");
                if (id.empty() || id == moving) continue;
                for (const auto& name : std::vector<std::string>{id, [&] {
                                                                     std::string s = id;
                                                                     std::replace(s.begin(), s.end(), '-', ' ');
                                                                     return s;
                                                                 }()}) {
                    const size_t found = after.find(name.substr(0, name.find(' ') == std::string::npos ? name.size() : name.find(' ')));
                    if (found != std::string::npos && found < bestAt) bestAt = found, best = id;
                }
            }
            if (!best.empty() && bestAt < 20) out.push_back({type, best});
        }
    }
    return out;
}

bool PlaceBuild::Mentions(const std::string& request, const std::string& zoneId) const {
    auto lower = [](std::string t) {
        for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return t;
    };
    const std::string text = lower(request);
    std::string spaced = lower(zoneId);
    std::replace(spaced.begin(), spaced.end(), '-', ' ');
    if (text.find(lower(zoneId)) != std::string::npos || text.find(spaced) != std::string::npos) return true;
    // By word stems: a word of its id or its module's name (not "ward") whose first
    // five letters start a word of the request ("craftsman's" for the craftsmen's ward).
    auto words = [&](const std::string& t) {
        std::vector<std::string> out;
        std::string w;
        for (const char c : t + " ") {
            if (std::isalpha(static_cast<unsigned char>(c))) w += c;
            else if (!w.empty()) out.push_back(w), w.clear();
        }
        return out;
    };
    const auto said = words(text);
    std::vector<std::string> names = words(spaced);
    json spec = spec_;
    if (const json* zone = SpecNode(spec, zoneId)) {
        if (const ModuleDef* m = host_.ontology().Module(zone->value("module", ""))) {
            if (text.find(lower(m->name)) != std::string::npos) return true;
            for (const auto& w : words(lower(m->name))) names.push_back(w);
        }
    }
    static const std::set<std::string> common = {"ward", "zone", "plot", "area", "the", "and", "with"};
    for (const auto& name : names) {
        if (name.size() < 4 || common.contains(name)) continue;
        const std::string stem = name.substr(0, std::min<size_t>(5, name.size()));
        for (const auto& w : said) {
            if (w.rfind(stem, 0) == 0) return true;
        }
    }
    return false;
}

// --- Requests to the AI --------------------------------------------------------------

bool PlaceBuild::Send(Pending what) {
    const int id = host_.request ? host_.request(what.path, what.body, what.timeout) : 0;
    if (id == 0) return false;
    pending_[id] = std::move(what);
    return true;
}

bool PlaceBuild::Retry(const Pending& what, const json& body) {
    if (body.value("ok", false) || what.attempts >= kAttempts || Hopeless(body)) return false;
    Pending again = what;
    again.attempts += 1;
    Log({{"step", "retry"}, {"kind", what.kind}, {"node", what.node}, {"attempt", again.attempts}, {"error", FirstError(body)}});
    return Send(std::move(again));
}

void PlaceBuild::Notice(const std::string& text) {
    if (std::find(notices_.begin(), notices_.end(), text) == notices_.end()) notices_.push_back(text);
    Log({{"step", "notice"}, {"text", text}});
}

void PlaceBuild::Log(json entry) const {
    if (id_.empty()) return;
    entry["time"] = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::error_code ec;
    fs::create_directories(Folder(), ec);
    std::ofstream(Folder() / "log.jsonl", std::ios::app) << entry.dump() << "\n";
}

void PlaceBuild::Save() const {
    if (id_.empty()) return;
    WriteJson(Folder() / "session.json", {{"schema", "aige.build-session/1"},
                                          {"id", id_},
                                          {"query", query_},
                                          {"stage", stage_},
                                          {"round", round_},
                                          {"brief", brief_},
                                          {"errors", errors_},
                                          {"notices", notices_},
                                          {"planProblems", ProblemsJson(planProblems_)},
                                          {"revisions", revisions_},
                                          {"fit", {{"scale", fitScale_}, {"side", fitSide_}}},
                                          {"origin", hasOrigin_ ? json::array({originX_, originY_}) : json()}});
}

json PlaceBuild::Defaults() const { return ReadJson(host_.projectRoot / "places" / "defaults.json"); }

// --- Interpret -------------------------------------------------------------------------

std::string PlaceBuild::Start(const std::string& query) {
    if (query.find_first_not_of(" \t\r\n") == std::string::npos) return "say what to build";
    id_ = PlaceIdFor(host_.projectRoot, query);
    query_ = query;
    round_ = 1;
    drafts_.clear();
    draftProblems_.clear();
    errors_.clear();
    notices_.clear();
    revisions_ = json::array();
    result_ = json();
    hasOrigin_ = false;
    brief_ = json::object();
    merge_ = {};
    spec_ = solved_ = json();
    context_ = InterpretContext(host_.ontology());
    WriteJson(Folder() / "request.json", {{"query", query}});
    Log({{"step", "start"}, {"query", query}});
    stage_ = "interpreting";
    status_ = "Reading your request…";
    if (!Send({"interpret", -1, "", "/places/interpret", {{"query", query}, {"context", context_}, {"drafts", 3}}, 600})) {
        InterpretWithoutAi("the AI is off");
    }
    Save();
    return {};
}

void PlaceBuild::InterpretWithoutAi(const std::string& why) {
    Notice("The AI couldn't read your request (" + why + "), so the Brief below comes from its words alone. Check each field.");
    drafts_ = {FallbackDraft(host_.ontology(), query_)};
    draftProblems_ = {{}};
    pending_.clear();
    FinishInterpreting();
}

void PlaceBuild::OnResponse(int requestId, const json& body) {
    auto it = pending_.find(requestId);
    if (it == pending_.end()) return;
    const Pending what = it->second;
    pending_.erase(it);
    if (what.kind == "revise" || what.kind == "review") {
        if (stage_ == "revising") OnRevision(what, body);
        Save();
        return;
    }
    if (stage_ == "planning") {
        OnPlanAnswer(what, body);
        Save();
        return;
    }
    if (stage_ != "interpreting") return; // cancelled meanwhile

    const Ontology& ontology = host_.ontology();
    if (what.kind == "interpret") {
        for (const auto& w : body.value("warnings", json::array())) {
            if (w.is_string()) Log({{"step", "interpret"}, {"warning", w}});
        }
        if (!body.value("ok", false)) {
            Log({{"step", "interpret"}, {"errors", body.value("errors", json::array())}});
            if (Retry(what, body)) {
                status_ = "The AI didn't answer; asking again (" + std::to_string(what.attempts + 1) + " of " + std::to_string(kAttempts) + ")…";
                return;
            }
            InterpretWithoutAi(FirstError(body));
            Save();
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
        if (drafts_.empty()) {
            InterpretWithoutAi("its answers had no Brief");
            Save();
            return;
        }
        // One repair round for each draft that breaks the rules.
        for (size_t i = 0; i < drafts_.size(); ++i) {
            if (draftProblems_[i].empty()) continue;
            Send({"repair", static_cast<int>(i), "", "/places/repair-brief",
                  {{"query", query_}, {"context", context_}, {"brief", drafts_[i]["brief"]}, {"problems", ProblemsJson(draftProblems_[i])}}, 300});
        }
        status_ = pending_.empty() ? status_ : "Fixing " + std::to_string(pending_.size()) + " draft(s)…";
    } else if (what.kind == "repair" && what.draft >= 0 && what.draft < static_cast<int>(drafts_.size())) {
        Log({{"step", "repair"}, {"reading", drafts_[what.draft].value("reading", "")}, {"answer", body.value("brief", json())},
             {"before", ProblemsJson(draftProblems_[what.draft])}, {"errors", body.value("errors", json::array())}});
        if (!body.value("ok", false) && Retry(what, body)) return;
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
    Save();
}

void PlaceBuild::FinishInterpreting() {
    const Ontology& ontology = host_.ontology();
    // Merge the drafts that follow the rules; if none does, all of them: what
    // they still break is fixed below or shown in the window.
    std::vector<json> good;
    for (size_t i = 0; i < drafts_.size(); ++i) {
        if (draftProblems_[i].empty()) good.push_back(drafts_[i]);
    }
    if (good.empty()) good = drafts_;
    if (good.empty()) good = {FallbackDraft(ontology, query_)};
    merge_ = MergeBriefs(good);
    brief_ = CompleteBrief(ontology, merge_.brief, Defaults());
    // What the Brief still breaks that needs no question: components it can't
    // have go, settings it can't read fall back to their defaults.
    for (const auto& p : ErrorsOf(ValidateBrief(ontology, brief_))) {
        if (p.where.rfind("components.", 0) == 0) {
            const std::string rest = p.where.substr(11);
            const std::string id = rest.substr(0, rest.find('.'));
            if (rest.find('.') == std::string::npos || p.message.find("isn't a component") != std::string::npos) {
                brief_["components"].erase(id);
                Notice("Left out '" + id + "': " + p.message + ".");
            } else {
                SetBriefValue(brief_, p.where, nullptr);
                Notice("'" + p.where + "' " + p.message + ": its default is used.");
            }
        } else if (p.where == "features" && brief_.contains("features") && brief_["features"].is_array()) {
            json kept = json::array();
            for (const auto& f : brief_["features"]) {
                const std::string module = f.is_object() ? f.value("module", "") : f.is_string() ? f.get<std::string>() : "";
                if (host_.ontology().Module(module)) kept.push_back(f.is_object() ? f : json{{"module", module}});
            }
            if (kept.size() != brief_["features"].size()) {
                brief_["features"] = kept;
                Notice("Left out a feature the Brief named that isn't one the engine knows (" + p.message + ").");
            }
        } else if (p.where == "shape.layout" || p.where.rfind("style.", 0) == 0) {
            SetBriefValue(brief_, p.where, nullptr);
            Notice("'" + p.where + "' " + p.message + ": its default is used.");
        }
    }
    brief_ = CompleteBrief(ontology, brief_, Defaults());
    WriteJson(Folder() / "drafts.json", drafts_);
    stage_ = "request";
    status_.clear();
    json disputed = json::array();
    for (const auto& f : merge_.fields) {
        if (f.disputed) disputed.push_back(f.path);
    }
    Log({{"step", "merged"}, {"brief", brief_}, {"drafts", good.size()}, {"disputed", disputed}});
}

void PlaceBuild::Edit(const std::string& path, const json& value) {
    if (stage_ != "request" && stage_ != "assets") return;
    ApplyWindowEdit(host_.ontology(), brief_, path, value);
    Save();
}

std::string PlaceBuild::Step(const std::string& step) {
    if ((stage_ == "plan" || stage_ == "planned") && (step == "request" || step == "assets")) {
        // Back from the plan: it's made again after the next Build.
        spec_ = solved_ = json();
        planProblems_.clear();
        stage_ = "assets";
    }
    if (stage_ != "request" && stage_ != "assets") return "nothing to confirm yet";
    if (step == "request") {
        stage_ = "request";
        round_ = 1;
        Save();
        return {};
    }
    if (step == "assets") {
        const auto errors = ErrorsOf(ValidateBrief(host_.ontology(), brief_));
        if (!errors.empty()) return errors.front().where + ": " + errors.front().message;
        stage_ = "assets";
        round_ = 2;
        Save();
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
    Save();
    return {};
}

// --- Planning -----------------------------------------------------------------------------

void PlaceBuild::StartPlanning() {
    fitScale_.clear();
    fitSide_ = 0;
    stage_ = "planning";
    planPhase_ = "root";
    repaired_.clear();
    finalRepairAsked_ = false;
    planProblems_.clear();
    tidied_.clear();
    zonesTotal_ = zonesDone_ = 0;
    spec_ = solved_ = json();
    status_ = "Planning the zones, roads and walls…";
    if (!Send({"plan-root", -1, "", "/places/plan-root", {{"brief", brief_}, {"context", PlanRootContext(host_.ontology(), brief_)}}, 600})) {
        Notice("The AI is off, so the plan comes from the Brief and the module rules alone.");
        spec_ = SpecFromRoot(host_.ontology(), brief_, FallbackRootAnswer(host_.ontology(), brief_), id_);
        PlanZones();
    }
}

void PlaceBuild::OnPlanAnswer(const Pending& what, const json& body) {
    const Ontology& ontology = host_.ontology();
    Log({{"step", what.kind}, {"node", what.node}, {"attempt", what.attempts}, {"mode", body.value("mode", "")},
         {"thinking", body.value("thinking", "")}, {"answer", body.value("answer", json())}, {"errors", body.value("errors", json::array())}});
    const bool ok = body.value("ok", false) && body.contains("answer") && body["answer"].is_object();
    if (!ok && Retry(what, body.is_object() ? body : json{{"ok", false}})) return;
    if (what.kind == "plan-root") {
        if (ok) {
            spec_ = SpecFromRoot(ontology, brief_, body["answer"], id_);
        } else {
            Notice("The AI couldn't plan the zones (" + FirstError(body) + "), so they come from the Brief alone.");
            spec_ = SpecFromRoot(ontology, brief_, FallbackRootAnswer(ontology, brief_), id_);
        }
        if (!RepairNodes("root")) PlanZones();
        return;
    }
    if (what.kind == "plan-zone") {
        if (ok) {
            ApplyZoneAnswer(ontology, brief_, spec_, what.node, body["answer"]);
        } else if (json* zone = SpecNode(spec_, what.node)) {
            Notice("The AI couldn't plan " + what.node + " (" + FirstError(body) + "), so its parcels come from the module rules.");
            ApplyZoneAnswer(ontology, brief_, spec_, what.node, FallbackZoneAnswer(ontology, *zone));
        }
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
    // A failed repair needs nothing: the fixes that need no AI come next.
    if (!pending_.empty()) return;
    if (planPhase_ == "zones" && what.kind == "plan-zone" && RepairNodes("zones")) return;
    if (planPhase_ == "root") PlanZones();
    else FinishPlanning();
}

bool PlaceBuild::RepairNodes(const std::string& phase) {
    planPhase_ = phase;
    const Ontology& ontology = host_.ontology();
    // What needs no AI is fixed first (unknown concepts, weights, counts by a field).
    for (auto& note : TidySpec(ontology, spec_)) tidied_.push_back(std::move(note));
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
        const std::string owner = phase == "zones" && inside ? node.substr(0, node.find('/')) : node;
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
        if (Send({"repair-node", -1, nodeId, "/places/repair-node", {{"node", given}, {"problems", listed}, {"context", context}}, 300})) {
            repaired_.insert(nodeId);
        }
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
    bool offline = false;
    for (const auto& zoneId : zones) {
        json zone = *SpecNode(spec_, zoneId);
        zone.erase("children");
        if (!Send({"plan-zone", -1, zoneId, "/places/plan-zone",
                   {{"brief", brief_}, {"zone", zone}, {"siblings", siblings}, {"context", PlanZoneContext(ontology, brief_, zone.value("module", ""))}},
                   600})) {
            ApplyZoneAnswer(ontology, brief_, spec_, zoneId, FallbackZoneAnswer(ontology, zone));
            ++zonesDone_;
            offline = true;
        }
    }
    if (offline) Notice("The AI is off, so the zones' parcels come from the module rules.");
    if (pending_.empty()) {
        FinishPlanning();
        return;
    }
    status_ = "Planning " + std::to_string(zonesTotal_) + " zones…";
}

void PlaceBuild::FinishPlanning() {
    const Ontology& ontology = host_.ontology();
    const std::string type = brief_.value("buildType", "");
    // The lower levels, then every fix that needs no AI.
    ExpandSpec(ontology, brief_, spec_);
    for (auto& note : TidySpec(ontology, spec_)) tidied_.push_back(std::move(note));
    RebalanceBuildings(ontology, brief_, spec_); // before the checks: the count is the Brief's
    for (auto& note : ResolveSpec(ontology, brief_, spec_)) tidied_.push_back(std::move(note));
    ExpandSpec(ontology, brief_, spec_); // what a removal left without its required parts
    int buildings = RebalanceBuildings(ontology, brief_, spec_);
    auto errors = ErrorsOf(ValidateSpec(ontology, spec_, SpecStage::Final, type));

    // What's still wrong goes to the AI once, node by node.
    if (!errors.empty() && !finalRepairAsked_) {
        finalRepairAsked_ = true;
        planPhase_ = "final";
        std::map<std::string, json> byNode;
        for (const auto& p : errors) {
            const std::string node = p.where.substr(0, p.where.find('.'));
            if (SpecNode(spec_, node)) byNode[node].push_back({{"where", p.where}, {"message", p.message}});
        }
        for (const auto& [nodeId, listed] : byNode) {
            json given = *SpecNode(spec_, nodeId);
            given.erase("children");
            Send({"repair-node", -1, nodeId, "/places/repair-node",
                  {{"node", given}, {"problems", listed}, {"context", ontology.Subgraph(given.value("module", ""))}}, 300});
        }
        if (!pending_.empty()) {
            status_ = "Fixing " + std::to_string(pending_.size()) + " part(s) of the plan…";
            return;
        }
    }
    if (finalRepairAsked_) {
        for (auto& note : TidySpec(ontology, spec_)) tidied_.push_back(std::move(note));
        for (auto& note : ResolveSpec(ontology, brief_, spec_)) tidied_.push_back(std::move(note));
        buildings = RebalanceBuildings(ontology, brief_, spec_);
    }
    // Never stuck: what can't be fixed is kept as planned and said so.
    planProblems_.clear();
    for (const auto& p : ValidateSpec(ontology, spec_, SpecStage::Final, type)) {
        planProblems_.push_back({p.where, p.warning ? p.message : "kept as planned, though " + p.message, true});
    }
    planProblems_.insert(planProblems_.end(), tidied_.begin(), tidied_.end());
    Log({{"step", "planned"}, {"buildings", buildings}, {"problems", ProblemsJson(planProblems_)}});
    // Laid out at once, so ③ Plan shows the map it will be, fitted until each
    // zone holds what it's planned to (a revision of an approved plan stays approved).
    stage_ = solveAfter_ ? "planned" : "plan";
    solveAfter_ = false;
    Solve();
    WriteJson(Folder() / "spec.json", spec_);
}

// --- Solve ----------------------------------------------------------------------------------

std::string PlaceBuild::Approve() {
    if (stage_ != "plan") return "there's no plan to approve";
    Log({{"step", "approved"}});
    if (!solved_.is_object()) Solve();
    stage_ = "planned";
    Review(); // the AI checks the map; without the AI, the layout stands as solved
    Save();
    return {};
}

void PlaceBuild::Solve() {
    const Ontology& ontology = host_.ontology();
    CompileOptions options;
    options.seed = Fnv1a(id_);
    options.side = BriefSide();
    options.isoGrid = host_.isoGrid ? host_.isoGrid() : true;
    options.keepSeeds = keepSeeds_;
    // From where the last fitting ended: the same land per zone, and (when
    // zones keep their places) the same site, so the river and roads stay put.
    options.zoneScale = fitScale_;
    if (!keepSeeds_.empty() && fitSide_ > 0) {
        options.side = fitSide_;
        options.fixedSide = true; // the river and roads stay where they were
    }
    keepSeeds_.clear();
    // Fitting rounds: a zone that couldn't place all it's planned to gets more
    // land (and the site grows if it must), until each holds its parcels or
    // four rounds are done; the best layout is kept.
    json ir, best, bestIr;
    std::map<std::string, double> bestScale;
    std::vector<PlaceProblem> irProblems;
    json bestProblems = json::array();
    double bestScore = -1e18;
    int rounds = 0;
    for (int round = 0; round < 4; ++round) {
        ++rounds;
        const auto roundScale = options.zoneScale;
        ir = CompileSpec(ontology, spec_, options);
        const SolvedLayout solved = SolveLayout(ontology, ir);
        const json rows = VerifyLayout(ontology, brief_, spec_, solved.document);
        double score = 0;
        int shortZones = 0;
        json grown = json::object();
        for (const auto& row : rows) {
            if (!row.value("zone", false)) continue;
            const int planned = row.value("planned", 0), built = row.value("built", 0);
            int missing = 0;
            for (const auto& p : row.value("parts", json::array())) missing += p.value("status", "") == "missing";
            score += std::min(built, planned) - 3.0 * missing;
            if (planned > 0 && (built < planned || missing > 0)) {
                const double factor = std::clamp(static_cast<double>(planned) / std::max(1, built), 1.15, 1.8);
                const std::string id = row.value("id", "");
                options.zoneScale[id] = (options.zoneScale.contains(id) ? options.zoneScale[id] : 1.0) * factor;
                grown[id] = options.zoneScale[id];
                ++shortZones;
            }
        }
        Log({{"step", "fit"}, {"round", round + 1}, {"score", score}, {"short", shortZones}, {"scale", grown}});
        if (score > bestScore) {
            bestScore = score;
            best = solved.document;
            bestIr = ir;
            bestProblems = solved.problems;
            bestScale = roundScale;
        }
        if (shortZones == 0) break;
    }
    fitScale_ = bestScale;
    irProblems = ValidateLayout(ontology, bestIr);
    WriteJson(Folder() / "layout.ir.json", bestIr);
    solved_ = best;
    WriteJson(Folder() / "layout.solved.json", solved_);
    // The plan says what the layout is: each zone's area as laid out, the site's side.
    const int side = SiteSide();
    fitSide_ = side;
    if (spec_.contains("root")) {
        spec_["root"]["quantity"]["area"] = {{"area", side * side}};
        for (const auto& r : solved_.value("regions", json::array())) {
            if (r.value("statement", "") != "site/zones") continue;
            if (json* zone = SpecNode(spec_, r.value("id", ""))) (*zone)["quantity"]["area"] = {{"area", r.value("area", 0)}};
        }
    }
    (void)rounds;
    int buildings = 0;
    for (const auto& o : solved_.value("objects", json::array())) buildings += o.value("layer", "") == "building";
    int failed = 0;
    for (const auto& c : solved_.value("checks", json::array())) failed += !c.value("ok", true);
    // The map of it, with its legend: places/<id>/layout.png.
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
    status_ = "Laid out " + std::to_string(buildings) + " buildings" + (failed ? " (" + std::to_string(failed) + " check(s) to look at)" : "") +
              ". Build puts it into the scene.";
    Log({{"step", "solved"}, {"rounds", rounds}, {"irProblems", ProblemsJson(irProblems)}, {"problems", bestProblems},
         {"stats", solved_.value("stats", json())}});
}

int PlaceBuild::SiteSide() const {
    // As laid out (the site grows when its zones need it), else as the Brief says.
    if (solved_.is_object() && solved_.contains("grid")) {
        const json b = solved_["grid"].value("bounds", json());
        if (b.is_array() && b.size() == 4 && b[2].is_number()) return b[2].get<int>();
    }
    return BriefSide();
}

int PlaceBuild::BriefSide() const {
    const json area = BriefValue(brief_, "scale.area.area");
    double a = 0;
    if (area.is_number()) a = area.get<double>();
    if (area.is_array() && area.size() == 2) a = (area[0].get<double>() + area[1].get<double>()) / 2;
    return a > 0 ? static_cast<int>(std::lround(std::sqrt(a))) : 80;
}

// --- Revising --------------------------------------------------------------------------------

json PlaceBuild::RevisionBody() const {
    const json input = RevisionInput(host_.ontology(), spec_, solved_);
    return {{"brief", brief_}, {"plan", input["plan"]}, {"result", input["result"]},
            {"context", RevisionContext(host_.ontology(), brief_, spec_)}};
}

std::string PlaceBuild::Revise(const std::string& request) {
    if (stage_ != "plan" && stage_ != "planned" && stage_ != "built") return "there's no plan to revise yet";
    if (request.find_first_not_of(" \t\r\n") == std::string::npos) return "say what to change";
    json body = RevisionBody();
    body["instruction"] = request;
    if (!Send({"revise", -1, request, "/places/revise-plan", body, 600})) return "the AI is off, and revising by request needs it";
    beforeRevision_ = stage_;
    stage_ = "revising";
    status_ = "Revising the plan: “" + request + "”…";
    Log({{"step", "revise"}, {"request", request}});
    Save();
    return {};
}

std::string PlaceBuild::Review() {
    if ((stage_ != "planned" && stage_ != "built") || !fs::exists(Folder() / "layout.png")) return "there's no map to review yet";
    json body = RevisionBody();
    body["image"] = "places/" + id_ + "/layout.png";
    body["verify"] = VerifyLayout(host_.ontology(), brief_, spec_, solved_);
    if (!Send({"review", -1, "", "/places/review-layout", body, 600})) return "the AI is off";
    beforeRevision_ = stage_;
    stage_ = "revising";
    status_ = "The AI is checking the map…";
    Log({{"step", "review"}});
    Save();
    return {};
}

void PlaceBuild::OnRevision(const Pending& what, const json& body) {
    const Ontology& ontology = host_.ontology();
    const bool review = what.kind == "review";
    Log({{"step", what.kind}, {"attempt", what.attempts}, {"thinking", body.value("thinking", "")}, {"answer", body.value("answer", json())},
         {"errors", body.value("errors", json::array())}});
    const bool ok = body.value("ok", false) && body.contains("answer") && body["answer"].is_object();
    if (!ok && Retry(what, body)) return;
    json entry = {{"by", review ? "review" : "you"}, {"text", what.node}};
    if (!ok) {
        entry["summary"] = std::string(review ? "The AI couldn't check the map" : "The AI couldn't revise the plan") + " (" + FirstError(body) +
                           "). Nothing was changed.";
        revisions_.push_back(entry);
        stage_ = beforeRevision_;
        status_.clear();
        return;
    }
    const json& answer = body["answer"];
    entry["summary"] = answer.value("summary", "");
    entry["issues"] = answer.value("issues", json::array());
    const json changes = answer.value("changes", json::array());
    const json briefChanges = answer.value("brief", json::object());
    if (changes.empty() && briefChanges.empty()) {
        if (entry["summary"] == "") entry["summary"] = review ? "The map looks right." : "Nothing to change.";
        revisions_.push_back(entry);
        stage_ = beforeRevision_;
        status_.clear();
        return;
    }
    // What it was, so Undo can go back.
    const int n = static_cast<int>(revisions_.size()) + 1;
    WriteJson(Folder() / "revisions" / (std::to_string(n) + ".json"),
              {{"brief", brief_}, {"spec", spec_}, {"stage", beforeRevision_}, {"solved", solved_}});
    entry["undo"] = n;
    json notes = json::array();
    // The Brief's totals, if the change needs them; kept only if the Brief still follows the rules.
    if (!briefChanges.empty()) {
        json changed = brief_;
        for (const auto& [path, value] : briefChanges.items()) SetBriefValue(changed, path, value);
        changed = CompleteBrief(ontology, changed, Defaults());
        const auto errors = ErrorsOf(ValidateBrief(ontology, changed));
        if (errors.empty()) {
            brief_ = changed;
            WriteJson(Folder() / "brief.json", brief_);
        } else {
            notes.push_back("Kept the Brief as it was: " + errors.front().where + " " + errors.front().message);
        }
    }
    // A request about one part changes that part: re-planning or removing a
    // zone it doesn't name is left out (the AI review may touch any).
    json kept = json::array();
    std::set<std::string> moving;
    for (const auto& change : changes) {
        const std::string op = change.value("op", "");
        std::string target = change.contains("node") && change["node"].is_string() ? change["node"].get<std::string>()
                             : change.contains("node") && change["node"].is_object() ? change["node"].value("id", "")
                                                                                       : "";
        const std::string zone = target.substr(0, target.find('/'));
        // Adding a part is what a request does; anything else is for the parts it names.
        if (!review && op != "add" && !Mentions(what.node, zone)) {
            notes.push_back(zone + ": left as it was; the request didn't ask about it");
            continue;
        }
        kept.push_back(change);
        moving.insert(zone);
    }
    // The request's own direction ("below the docks"), if the answer moved a
    // zone and lost it: added, as the words say.
    if (!review) {
        for (auto& change : kept) {
            const std::string op = change.value("op", "");
            if (op != "move" && op != "set" && op != "zone") continue;
            if (!change.contains("rel") && op != "move") continue;
            const std::string zone = change.value("node", json("")).is_string() ? change["node"].get<std::string>() : "";
            if (!Mentions(what.node, zone)) continue; // the request's directions are for the part it names
            for (const auto& [type, to] : DirectionsIn(what.node, zone)) {
                bool has = false;
                for (const auto& r : change.value("rel", json::array())) has = has || (r.value("type", "") == type && r.value("to", "") == to);
                if (has) continue;
                change["rel"].push_back({{"type", type}, {"to", to}});
                notes.push_back(zone + ": " + type + " " + to + ", as the request says");
            }
        }
    }
    for (const auto& p : ApplyRevision(ontology, brief_, spec_, kept)) notes.push_back(p.where + ": " + p.message);
    // The zones it changed are placed anew; the others keep their place.
    keepSeeds_.clear();
    for (const auto& r : solved_.value("regions", json::array())) {
        if (r.value("statement", "") != "site/zones" || !r.contains("seed") || moving.contains(r.value("id", ""))) continue;
        keepSeeds_[r.value("id", "")] = {r["seed"][0].get<int>(), r["seed"][1].get<int>()};
    }
    entry["notes"] = notes;
    revisions_.push_back(entry);
    // The rest as after planning: the expansion, the fixes, the checks; then laid out again.
    solveAfter_ = beforeRevision_ == "planned" || beforeRevision_ == "built";
    finalRepairAsked_ = false;
    tidied_.clear();
    stage_ = "planning";
    planPhase_ = "final";
    status_ = "Applying the changes…";
    FinishPlanning();
}

std::string PlaceBuild::Undo() {
    if (stage_ != "plan" && stage_ != "planned" && stage_ != "built") return "nothing to undo now";
    for (auto it = revisions_.rbegin(); it != revisions_.rend(); ++it) {
        if (!it->contains("undo") || it->value("undone", false)) continue;
        const json saved = ReadJson(Folder() / "revisions" / (std::to_string((*it)["undo"].get<int>()) + ".json"));
        if (!saved.is_object()) return "the saved plan is missing";
        brief_ = saved["brief"];
        spec_ = saved["spec"];
        // Laid out where it was: every zone from its old seed.
        keepSeeds_.clear();
        for (const auto& r : saved.value("solved", json::object()).value("regions", json::array())) {
            if (r.value("statement", "") == "site/zones" && r.contains("seed")) {
                keepSeeds_[r.value("id", "")] = {r["seed"][0].get<int>(), r["seed"][1].get<int>()};
            }
        }
        (*it)["undone"] = true;
        WriteJson(Folder() / "brief.json", brief_);
        WriteJson(Folder() / "spec.json", spec_);
        planProblems_.clear();
        for (const auto& p : ValidateSpec(host_.ontology(), spec_, SpecStage::Final, brief_.value("buildType", ""))) {
            planProblems_.push_back({p.where, p.warning ? p.message : "kept as planned, though " + p.message, true});
        }
        Log({{"step", "undo"}, {"revision", (*it)["undo"]}});
        Solve();
        Save();
        return {};
    }
    return "no revision to undo";
}

std::string PlaceBuild::Relayout() {
    if (stage_ != "plan" && stage_ != "planned" && stage_ != "built") return "there's no plan to lay out";
    fitScale_.clear();
    fitSide_ = 0;
    const Ontology& ontology = host_.ontology();
    ExpandSpec(ontology, brief_, spec_);
    for (auto& note : TidySpec(ontology, spec_)) tidied_.push_back(std::move(note));
    RebalanceBuildings(ontology, brief_, spec_);
    for (auto& note : ResolveSpec(ontology, brief_, spec_)) tidied_.push_back(std::move(note));
    ExpandSpec(ontology, brief_, spec_);
    RebalanceBuildings(ontology, brief_, spec_);
    Log({{"step", "relayout"}});
    Solve();
    WriteJson(Folder() / "spec.json", spec_);
    Save();
    return {};
}

std::string PlaceBuild::StartBuilding() {
    if (stage_ != "planned" && stage_ != "built") return "approve the plan first";
    if (!solved_.is_object()) return "the plan isn't laid out";
    stage_ = "building";
    status_ = "Putting the place into the scene…";
    Log({{"step", "building"}});
    Save();
    return {};
}

void PlaceBuild::Built(const json& result) {
    result_ = result;
    WriteJson(Folder() / "result.json", result_);
    stage_ = "built";
    int failed = 0;
    for (const auto& c : result_.value("after", json::array())) failed += !c.value("ok", true) && !c.value("warning", false);
    status_ = failed ? std::to_string(failed) + " check(s) still failing after the repairs" : "Built and verified.";
    Log({{"step", "built"}, {"report", result_.value("report", json())}, {"repairs", result_.value("repairs", json::array())}});
    Save();
}

void PlaceBuild::SetOrigin(int x, int y) {
    hasOrigin_ = true;
    originX_ = x;
    originY_ = y;
    Save();
}

// --- Reopening a build ------------------------------------------------------------------

std::string PlaceBuild::Open(const std::string& id) {
    const fs::path folder = host_.projectRoot / "places" / id;
    const json request = ReadJson(folder / "request.json");
    if (!request.is_object()) return "no build '" + id + "' in this project";
    id_ = id;
    query_ = request.value("query", "");
    pending_.clear();
    errors_.clear();
    notices_.clear();
    revisions_ = json::array();
    result_ = json();
    hasOrigin_ = false;
    round_ = 1;
    const Ontology& ontology = host_.ontology();
    context_ = InterpretContext(ontology);
    const json session = ReadJson(folder / "session.json");
    const json drafts = ReadJson(folder / "drafts.json");
    drafts_.clear();
    draftProblems_.clear();
    if (drafts.is_array()) {
        for (const auto& d : drafts) {
            drafts_.push_back(d);
            draftProblems_.push_back({});
        }
    }
    merge_ = drafts_.empty() ? BriefMerge{} : MergeBriefs(drafts_);
    std::string stage;
    if (session.is_object()) {
        stage = session.value("stage", "");
        round_ = session.value("round", 1);
        brief_ = session.value("brief", json::object());
        for (const auto& n : session.value("notices", json::array())) {
            if (n.is_string()) notices_.push_back(n.get<std::string>());
        }
        planProblems_ = ProblemsFrom(session.value("planProblems", json::array()));
        revisions_ = session.value("revisions", json::array());
        fitScale_.clear();
        const json fit = session.value("fit", json::object());
        const json fitScale = fit.value("scale", json::object());
        for (const auto& [id, f] : fitScale.items()) {
            if (f.is_number()) fitScale_[id] = f.get<double>();
        }
        fitSide_ = session.value("fit", json::object()).value("side", 0);
        if (const json o = session.value("origin", json()); o.is_array() && o.size() == 2) {
            hasOrigin_ = true;
            originX_ = o[0].get<int>();
            originY_ = o[1].get<int>();
        }
    } else {
        brief_ = ReadJson(folder / "brief.json");
        if (!brief_.is_object() && !drafts_.empty()) brief_ = CompleteBrief(ontology, merge_.brief, Defaults());
    }
    spec_ = ReadJson(folder / "spec.json");
    solved_ = ReadJson(folder / "layout.solved.json");
    if (stage == "revising") {
        Notice("A revision was still running when the build was closed; ask for it again.");
        stage.clear();
    }
    if (stage == "building") stage = "planned"; // Build again
    result_ = ReadJson(folder / "result.json");
    if (stage == "built" && !result_.is_object()) stage = "planned";
    if (stage.empty() || stage == "cancelled" || stage == "failed") {
        stage = solved_.is_object()  ? "planned"
                : spec_.is_object()   ? "plan"
                : brief_.is_object() && fs::exists(folder / "brief.json") ? "assets"
                : !drafts_.empty()    ? "request"
                                      : "interpreting";
    }
    Log({{"step", "opened"}, {"stage", stage}});
    // A step that was waiting on the AI starts again.
    if (stage == "interpreting") {
        stage_ = "interpreting";
        status_ = "Reading your request…";
        if (!Send({"interpret", -1, "", "/places/interpret", {{"query", query_}, {"context", context_}, {"drafts", 3}}, 600})) {
            InterpretWithoutAi("the AI is off");
        }
    } else if (stage == "planning") {
        StartPlanning();
    } else {
        stage_ = stage;
        if (stage_ == "assets") round_ = 2;
        if (stage_ == "plan" && planProblems_.empty() && spec_.is_object()) {
            for (const auto& p : ValidateSpec(ontology, spec_, SpecStage::Final, brief_.value("buildType", ""))) {
                planProblems_.push_back({p.where, p.warning ? p.message : "kept as planned, though " + p.message, true});
            }
        }
        if (stage_ == "built") {
            status_ = "Built.";
            ++imageVersion_;
        } else if (stage_ == "planned") {
            int buildings = 0;
            for (const auto& o : solved_.value("objects", json::array())) buildings += o.value("layer", "") == "building";
            status_ = "Laid out " + std::to_string(buildings) + " buildings. Build puts it into the scene.";
            ++imageVersion_;
        } else {
            status_.clear();
        }
    }
    Save();
    return {};
}

void PlaceBuild::Cancel() {
    if (!Active()) return;
    if (stage_ == "revising") {
        pending_.clear();
        stage_ = beforeRevision_;
        status_.clear();
        Log({{"step", "revision stopped"}});
        Save();
        return;
    }
    pending_.clear();
    stage_ = "cancelled";
    Log({{"step", "cancelled"}});
    Save();
}

// --- What the panels show -----------------------------------------------------------------

json PlaceBuild::State() {
    json state = {{"type", "build:state"}, {"id", id_}, {"query", query_}, {"stage", stage_}, {"round", round_},
                  {"status", status_}, {"errors", errors_}, {"notices", notices_}, {"drafts", drafts_.size()}};
    if (stage_ == "plan" || stage_ == "planned" || stage_ == "building" || stage_ == "built") {
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
    if (stage_ == "built" && result_.is_object()) {
        json shown = result_;
        if (shown.contains("assets")) shown["assets"].erase("byObject");
        state["result"] = shown;
    }
    if (hasOrigin_) state["origin"] = {originX_, originY_};
    if (stage_ == "plan" || stage_ == "planned" || stage_ == "revising" || stage_ == "built") {
        state["revisions"] = revisions_;
        if (solved_.is_object()) state["verify"] = VerifyLayout(host_.ontology(), brief_, spec_, solved_);
    }
    if (stage_ == "revising") {
        state["plan"] = PlanSummary(host_.ontology(), spec_);
        state["sketch"] = PlanSketch(host_.ontology(), spec_, SiteSide());
        state["problems"] = ProblemsJson(planProblems_);
        if (solved_.is_object()) state["image"] = "places/" + id_ + "/layout.png?v=" + std::to_string(imageVersion_);
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
    else if (stage_ == "planned") text = "Laid out; Build puts it into the scene";
    else if (stage_ == "revising") text = status_.empty() ? "Revising the plan…" : status_;
    else if (stage_ == "building") text = "④ Build: putting it into the scene…";
    else if (stage_ == "built") text = "⑤ Result: " + status_;
    else if (stage_ == "cancelled") text = "Cancelled";
    else if (stage_ == "failed") text = errors_.empty() ? "Failed" : errors_.front();
    return {{"type", "build:card"}, {"buildId", id_}, {"name", brief_.is_object() ? brief_.value("name", "") : ""},
            {"stage", stage_}, {"text", text}, {"query", query_}};
}

} // namespace aige
