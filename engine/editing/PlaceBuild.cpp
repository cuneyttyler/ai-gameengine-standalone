#include "editing/PlaceBuild.h"
#include "editing/PlaceInspect.h"

#include "assets/ArtAnalysis.h"
#include "assets/LayoutImage.h"
#include "assets/TextureCook.h"
#include "editing/BuildWindow.h"
#include "places/Compile.h"
#include "places/PlaceValidate.h"
#include "places/Planner.h"
#include "places/Solver.h"
#include "places/Where.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <functional>
#include <set>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// About how much land a zone needs for what it's to hold: its count of parcels, each a
// typical lot of its kinds, with its share of the streets and open land (as the compiler: 1.9).
int ZoneNeed(const Ontology& ontology, const json& zone) {
    const ModuleDef* m = ontology.Module(zone.value("module", ""));
    if (m == nullptr || m->level != PlaceLevel::Zone) return 0;
    const std::string field = CountField(*m);
    if (field.empty()) return 0;
    double lot = 0;
    int kinds = 0;
    for (const ChildDef& rule : m->children) {
        std::vector<std::string> ids;
        if (rule.module.rfind("class:", 0) == 0) {
            for (const auto& [id, md] : ontology.Modules()) {
                if (ontology.IsA(id, rule.module.substr(6))) ids.push_back(id);
            }
        } else {
            ids.push_back(rule.module);
        }
        for (const auto& id : ids) {
            const ModuleDef* pm = ontology.Module(id);
            if (pm == nullptr || pm->level != PlaceLevel::Parcel) continue;
            const FieldDef* size = pm->Field("size");
            if (size == nullptr || !size->defaultValue.is_object()) continue;
            auto mid = [](const json& v) { return v.is_array() ? (v[0].get<double>() + v[1].get<double>()) / 2 : v.is_number() ? v.get<double>() : 6.0; };
            lot += mid(size->defaultValue.value("w", json(6))) * mid(size->defaultValue.value("h", json(8)));
            ++kinds;
        }
    }
    const json count = zone.value("quantity", json::object()).value(field, json());
    const double n = count.is_object() && count.contains("count") && count["count"].is_number() ? count["count"].get<double>()
                     : count.is_number()                                                       ? count.get<double>()
                                                                                                : 0;
    if (kinds == 0 || n <= 0) return 0;
    return static_cast<int>(std::lround(n * lot / kinds * 1.9));
}

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
    if (e.find("No AI provider") != std::string::npos || e.find("Say what") != std::string::npos) return true;
    // An image service refusing the request itself (4xx, not 429 "slow down"): the same again is refused again.
    const size_t at = e.find(" answered 4");
    return at != std::string::npos && e.compare(at + 10, 3, "429") != 0;
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

int RemoveBuilds(const fs::path& projectRoot, const std::string& keep) {
    // Every build's folder under places/ but `keep`'s (the one open): what Your builds lists.
    int removed = 0;
    std::error_code ec;
    for (const auto& b : ListBuilds(projectRoot)) {
        const std::string id = b.value("id", "");
        if (id.empty() || id == keep || id.find_first_of("/\\.") != std::string::npos) continue;
        const fs::path folder = projectRoot / "places" / id;
        if (!fs::exists(folder / "request.json", ec)) continue; // only a build's folder
        if (fs::remove_all(folder, ec) > 0 && !ec) ++removed;
    }
    return removed;
}

json PlaceBuild::Advanced(const std::string& what) const {
    // The build's own documents, for the advanced view (8.2): read as they are on disk.
    static const std::map<std::string, std::vector<std::string>> files = {
        {"brief", {"brief.json"}},
        {"spec", {"spec.json"}},
        {"pll", {"layout.ir.json", "zones.ir.json"}},
        {"solved", {"layout.solved.json", "zones.solved.json"}},
        {"assets", {"assets.json"}},
        {"log", {"log.jsonl"}},
    };
    const auto it = files.find(what);
    if (it == files.end()) return {{"what", what}, {"text", "no such document"}};
    for (const auto& name : it->second) {
        std::ifstream in(Folder() / name, std::ios::binary);
        if (!in) continue;
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        constexpr size_t kMax = 600 * 1024;
        bool cut = false;
        if (text.size() > kMax) {
            // The log's end matters most; a document's start.
            text = what == "log" ? text.substr(text.size() - kMax) : text.substr(0, kMax);
            cut = true;
        }
        if (what != "log" && !cut) {
            const json parsed = json::parse(text, nullptr, false);
            if (!parsed.is_discarded()) text = parsed.dump(2);
        }
        return {{"what", what}, {"file", name}, {"text", text}, {"cut", cut}};
    }
    return {{"what", what}, {"text", "not made yet"}};
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
    // Clause by clause ("a, b; c. d"): "the temple north of the market" says where the temple
    // goes, not every zone the request names. A clause naming more zones than the two is left
    // to the AI's own reading.
    std::vector<std::string> clauses;
    {
        std::string clause;
        for (const char ch : text + ".") {
            if (ch == '.' || ch == ';' || ch == '\n' || ch == ',') {
                if (!clause.empty()) clauses.push_back(clause);
                clause.clear();
            } else {
                clause += ch;
            }
        }
    }
    for (const auto& clause : clauses) {
        if (!Mentions(clause, moving)) continue;
        int named = 0;
        for (const auto& c : spec_["root"]["children"]) {
            if (const std::string id = c.value("id", ""); !id.empty() && id != moving && Mentions(clause, id)) ++named;
        }
        if (named != 1) continue;
        const std::string& text = clause;
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
    }
    return out;
}

bool PlaceBuild::IsSubject(const std::string& request, const std::string& zoneId) const {
    // What the request moves, not what it moves it by: in each clause, the part named before its
    // first word of place ("put the craftsmen's ward | between the temple ward and the docks":
    // the craftsmen's ward is moved; the temple ward and the docks are where it goes, and stay).
    std::string text;
    for (const char c : request) text += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // "... and put the farmland to the left of it": a second thing moved, a clause of its own.
    for (const char* verb : {"put", "move", "place", "make", "set", "bring", "shift", "keep", "leave", "let", "have", "turn"}) {
        const std::string joined = std::string(" and ") + verb + " ", split = std::string("; ") + verb + " ";
        for (size_t at = text.find(joined); at != std::string::npos; at = text.find(joined, at + split.size())) text.replace(at, joined.size(), split);
        const std::string then = std::string(" then ") + verb + " ";
        for (size_t at = text.find(then); at != std::string::npos; at = text.find(then, at + split.size())) text.replace(at, then.size(), split);
    }
    static const char* places[] = {" between ", " next to ", " beside ", " besides ", " near ", " along", " north", " south", " east",
                                   " west", " below ", " above ", " under ", " over ", " behind ", " in front of ", " around ",
                                   " where ", " across ", " opposite ", " facing ", " by the ", " left of", " right of", " to the ",
                                   " toward", " away from", " far from", " close to", " inside ", " outside ", " at the ", " on the ",
                                   " into ", " from the ", " upstream", " downstream", " in the middle", " in the cent"};
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find_first_of(".;\n,:", start);
        if (end == std::string::npos) end = text.size();
        const std::string clause = " " + text.substr(start, end - start) + " ";
        start = end + 1;
        // "Leave the temple ward where it is", "keep the docks", "don't move the market": it stays.
        bool stays = false;
        for (const char* w : {" leave ", " keep ", " don't move ", " do not move ", " dont move ", " don't touch ", " do not touch ",
                              " not move ", " stay", " as it is", " where it is", " in place", " untouched", " alone "}) {
            stays = stays || clause.find(w) != std::string::npos;
        }
        if (stays) continue;
        size_t cut = clause.size();
        for (const char* w : places) cut = std::min(cut, clause.find(w));
        if (Mentions(clause.substr(0, cut), zoneId)) return true;
    }
    return false;
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

void PlaceBuild::SetStory(json story) {
    story_ = story.is_object() ? std::move(story) : json();
    json request = ReadJson(Folder() / "request.json");
    if (request.is_object()) {
        request["story"] = story_;
        WriteJson(Folder() / "request.json", request);
    }
}

bool PlaceBuild::Send(Pending what) {
    // The story this place serves, with the rest of what the AI is told (its context).
    if (story_.is_object() && !story_.empty() && what.path.rfind("/places/", 0) == 0 && what.body.is_object()) {
        if (!what.body.contains("context") || !what.body["context"].is_object()) what.body["context"] = json::object();
        what.body["context"]["story"] = story_;
    }
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
    lastRetry_ = FirstError(body);
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
                                          {"sketch", {{"done", sketched_}, {"crops", sketchCrops_}, {"sketches", sketches_}}},
                                          {"paint", {{"done", painted_}, {"backdrops", backdrops_}}},
                                          {"layoutCheck", {{"checked", layoutChecked_}, {"rounds", layoutRounds_}, {"answer", layoutCheck_}}},
                                          {"fit", {{"scale", fitScale_}, {"side", fitSide_}}},
                                          {"locks", locks_},
                                          {"tried", [this] {
                                               json t = json::object();
                                               for (const auto& [k, v] : tried_) {
                                                   t[k] = {{"web", v.web}, {"generate", v.generate}, {"refused", v.refused}, {"approved", v.approved},
                                                           {"refusals", v.refusals}};
                                               }
                                               return t;
                                           }()},
                                          {"sourcing", SourcingJson(sourcing_)},
                                          {"questions", questions_},
                                          {"decisionAnswers", decisionAnswers_},
                                          {"directions", directions_},
                                          {"sourced", SourcingJson(sourced_)},
                                          {"origin", hasOrigin_ ? json::array({originX_, originY_}) : json()}});
}

json PlaceBuild::SourcingJson(const std::map<std::string, Sourcing>& pieces) {
    json out = json::object();
    for (const auto& [key, s] : pieces) {
        out[key] = {{"need", s.need}, {"attempts", s.attempts}, {"reasons", s.reasons}, {"done", s.done}, {"passed", s.passed},
                    {"made", s.made}, {"request", s.request}, {"superseded", s.superseded}, {"verdict", s.verdict}};
    }
    return out;
}

std::map<std::string, PlaceBuild::Sourcing> PlaceBuild::SourcingFrom(const json& saved) {
    std::map<std::string, Sourcing> out;
    if (!saved.is_object()) return out;
    for (const auto& [key, v] : saved.items()) {
        if (!v.is_object()) continue;
        Sourcing s;
        s.need = v.value("need", json::object());
        s.attempts = v.value("attempts", 0);
        s.reasons = v.value("reasons", "");
        s.done = v.value("done", false);
        s.passed = v.value("passed", false);
        s.made = v.value("made", json::array());
        s.request = v.value("request", "");
        s.verdict = v.value("verdict", "");
        s.superseded = v.value("superseded", json::array());
        out[key] = s;
    }
    return out;
}

json PlaceBuild::Defaults() const { return ReadJson(host_.projectRoot / "places" / "defaults.json"); }

// --- Interpret -------------------------------------------------------------------------

std::string PlaceBuild::Start(const std::string& words, const json& choices) {
    choices_ = choices.is_object() ? choices : json::object();
    // Choices without words: a request in words made from them.
    const bool said = words.find_first_not_of(" \t\r\n") != std::string::npos;
    if (!said && choices_.empty()) return "say what to build";
    const std::string query = said ? words : ChoicesQuery(host_.ontology(), choices_);
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
    context_ = InterpretContextFor(host_.ontology(), query_, choices_);
    progressStep_.clear();
    lastRetry_.clear();
    WriteJson(Folder() / "request.json", {{"query", query}, {"choices", choices_}});
    Log({{"step", "start"}, {"query", query}, {"choices", choices_}});
    stage_ = "interpreting";
    status_ = "Reading your request…";
    if (!SendReadings()) {
        InterpretWithoutAi("the AI is off");
    }
    Save();
    return {};
}

std::string PlaceBuild::StartFromBrief(const std::string& words, const json& brief) {
    if (!brief.is_object() || brief.value("buildType", "").empty() || !host_.ontology().Build(brief.value("buildType", ""))) {
        return "the Brief names no build type the engine knows";
    }
    choices_ = json::object();
    query_ = words.empty() ? brief.value("name", brief.value("buildType", "")) : words;
    id_ = PlaceIdFor(host_.projectRoot, query_);
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
    context_ = InterpretContextFor(host_.ontology(), query_, {{"buildType", brief.value("buildType", "")}});
    progressStep_.clear();
    lastRetry_.clear();
    WriteJson(Folder() / "request.json", {{"query", query_}, {"choices", choices_}, {"fromPlanner", true}});
    fromPlanner_ = true;
    Log({{"step", "start"}, {"query", query_}, {"brief", brief}, {"fromPlanner", true}});
    // One draft, sure of itself: the merge keeps it as it is, and the window shows it to confirm.
    drafts_ = {{{"brief", brief}, {"confidence", 1.0}, {"assumptions", json::array()}, {"questions", json::array()}, {"reading", "planner"}}};
    draftProblems_ = {{}};
    FinishInterpreting();
    Notice("This Brief comes from the AI Planner's description of the place: check it, then build.");
    Save();
    return {};
}

// What the AI is given to read the request with: the one build type the words or the form name (a
// village: the settlement's components, scale and fields), else all of them (each is many modules).
json PlaceBuild::InterpretContextFor(const Ontology& ontology, const std::string& query, const json& choices) {
    std::string type = choices.is_object() ? choices.value("buildType", "") : "";
    if (type.empty() || !ontology.Build(type)) type = NamedBuildType(ontology, query);
    return InterpretContext(ontology, type);
}

bool PlaceBuild::SendReadings() {
    // Each reading on its own request: they run side by side, and each shows as it comes.
    bool sent = false;
    for (const char* reading : {"literal", "typical", "generous"}) {
        sent = Send({"interpret", -1, reading, "/places/interpret",
                     {{"query", query_}, {"context", context_}, {"drafts", 1}, {"reading", reading}, {"choices", choices_}}, 400}) || sent;
    }
    return sent;
}

void PlaceBuild::InterpretWithoutAi(const std::string& why) {
    Notice("The AI couldn't read your request (" + why + "), so the Brief below comes from its words alone. Check each field.");
    drafts_ = {FallbackDraft(host_.ontology(), query_)};
    draftProblems_ = {{}};
    pending_.clear();
    FinishInterpreting();
}

void PlaceBuild::OnResponse(int requestId, const json& body) {
    // An AI answer of an odd shape must not throw out of the build (the editor, the game build):
    // it's noted, and the build goes back to where it was.
    const auto asked = pending_.find(requestId);
    const std::string kind = asked == pending_.end() ? "" : asked->second.kind, key = asked == pending_.end() ? "" : asked->second.node;
    const std::optional<Pending> what = asked == pending_.end() ? std::nullopt : std::optional<Pending>(asked->second);
    try {
        HandleResponse(requestId, body);
    } catch (const std::exception& e) {
        Log({{"step", "odd answer"}, {"error", e.what()}});
        // A step of the plan (reading the request, the zones, a repair): asked again, as for an answer
        // that came back wrong (three times at most); it would wait for nothing otherwise.
        static const std::set<std::string> planning = {"interpret", "repair", "questions", "plan-root", "plan-zone", "repair-node"};
        if (what && planning.contains(kind)) {
            pending_.erase(requestId);
            if (Retry(*what, {{"ok", false}, {"errors", json::array({std::string("its answer couldn't be used: ") + e.what()})}})) {
                Save();
                return;
            }
        }
        // A piece of art whose answer couldn't be used: done (its other sources next), so sourcing ends.
        if ((kind == "art" || kind == "web" || kind == "critic") && sourcing_.contains(key)) {
            sourcing_[key].done = true;
            FinishSourcing();
        }
        Notice(std::string("An AI answer couldn't be used (") + e.what() + "); the build goes on without it.");
        if (stage_ == "revising") stage_ = beforeRevision_.empty() ? "plan" : beforeRevision_;
        layoutCheckRevising_ = false;
        inspecting_ = false;
        if (kind == "paint" && painting_ == 0) painted_ = true; // the last painting's answer: painting ends
        if (kind == "sketch" && sketching_ == 0) sketched_ = true;
        status_.clear();
        Save();
    }
}

void PlaceBuild::HandleResponse(int requestId, const json& body) {
    auto it = pending_.find(requestId);
    if (it == pending_.end()) return;
    const Pending what = it->second;
    pending_.erase(it);
    if (what.kind == "art" || what.kind == "web" || what.kind == "critic") {
        SourcingStep(what, body);
        Save();
        return;
    }
    if (what.kind == "scene-review") {
        const bool ok = body.value("ok", false) && body.contains("answer") && body["answer"].is_object();
        if (!ok && Retry(what, body)) return;
        Log({{"step", "scene review"}, {"thinking", body.value("thinking", "")}, {"answer", body.value("answer", json())},
             {"errors", body.value("errors", json::array())}});
        if (result_.is_object()) {
            result_["critique"] = ok ? json{{"summary", body["answer"].value("summary", "")}, {"issues", body["answer"].value("issues", json::array())}}
                                     : json{{"error", FirstError(body)}};
            WriteJson(Folder() / "result.json", result_);
        }
        Save();
        return;
    }
    if (what.kind == "revise" || what.kind == "review") {
        if (stage_ == "revising") OnRevision(what, body);
        Save();
        return;
    }
    if (what.kind == "paint") {
        painting_ = std::max(0, painting_ - 1);
        const size_t i = static_cast<size_t>(std::stoi(what.node));
        const json request = i < paintRequests_.size() ? paintRequests_[i] : json::object();
        Log({{"step", "painted"}, {"level", request.value("level", "")}, {"answer", body}});
        if (body.value("ok", false)) {
            backdrops_.push_back({{"level", request.value("level", "")}, {"area", request.value("area", json::array())},
                                  {"backdrop", body.value("backdrop", body.value("painting", ""))}, {"size", body.value("size", json::array())},
                                  {"prompt", body.value("prompt", "")}, {"map", body.value("map", json::object())},
                                  {"model", body.value("model", "")}, {"steps", body.value("steps", json::array())},
                                  {"blockout", request.value("blockout", "")}, {"legend", request.value("legend", "")}});
        } else {
            Notice("A backdrop wasn't painted: " + FirstError(body));
        }
        if (painting_ == 0) {
            painted_ = true;
            status_ = std::to_string(backdrops_.size()) + " backdrop(s) painted.";
        }
        Save();
        return;
    }
    if (what.kind == "sketch") {
        sketching_ = std::max(0, sketching_ - 1);
        Log({{"step", "sketched"}, {"window", what.node}, {"answer", body}});
        if (body.value("ok", false)) {
            sketches_.push_back({{"window", what.node}, {"sketch", body.value("sketch", "")}, {"prompt", body.value("prompt", "")}});
            const json crops = body.value("crops", json::object()); // named: items() of a temporary dangles
            for (const auto& [key, crop] : crops.items()) {
                if (crop.is_string()) sketchCrops_[key] = crop.get<std::string>();
            }
            const json seen = body.value("seen", json::object());
            for (const auto& [key, words] : seen.items()) {
                if (words.is_string()) sketchSeen_[key] = words.get<std::string>();
            }
        } else {
            sketches_.push_back({{"window", what.node}, {"error", FirstError(body)}});
        }
        if (sketching_ == 0) {
            sketched_ = true;
            status_ = std::to_string(sketchCrops_.size()) + " pieces sketched; their art is drawn from the sketch.";
            if (stage_ == "built" && buildRounds_ <= 1 && !sketchCrops_.empty()) {
                // Painted from the first build: each sketched piece made again from its crop (with what
                // the check refused, in one round). Worse, and the art from before comes back.
                if (buildRounds_ == 0) {
                    buildRounds_ = 1;
                    double sum = 0;
                    int count = 0, errors = 0;
                    const json scores = buildCheck_.value("scores", json::object());
                    for (const auto& [k, v] : scores.items()) {
                        if (v.is_number()) sum += v.get<double>(), ++count;
                    }
                    for (const auto& p : buildCheck_.value("problems", json::array())) errors += p.value("severity", "") == "error";
                    firstBuildWorth_ = (count ? sum / count : 0.0) - 0.5 * errors;
                }
                for (const auto& [key, crop] : sketchCrops_) {
                    if (key.rfind("terrain|", 0) == 0) continue; // the ground is painted, not a sprite
                    for (const auto& n : assetNeeds_) {
                        if (n.value("key", "") != key || n.value("entry", "").empty()) continue;
                        if (!redoneFrom_.contains(key)) redoneFrom_[key] = n.value("entry", "");
                    }
                    tried_[key].generate = false;
                    if (std::find(redo_.begin(), redo_.end(), key) == redo_.end()) redo_.push_back(key);
                    if (!redoReasons_.contains(key)) redoReasons_[key] = "drawn again to match the painting of the whole place";
                }
                Notice(std::to_string(redo_.size()) + " pieces are drawn again from the painting of the place.");
            }
        }
        Save();
        return;
    }
    if (what.kind == "inspect-build") {
        inspecting_ = false;
        buildCheck_ = body;
        Log({{"step", "build check"}, {"answer", body}});
        json issues = json::array();
        for (const auto& p : body.value("problems", json::array())) {
            if (p.is_object()) issues.push_back((p.value("severity", "") == "error" ? "" : "(minor) ") + p.value("issue", ""));
        }
        if (result_.is_object()) {
            result_["critique"] = body.value("ok", false) ? json{{"summary", body.value("summary", "")}, {"issues", issues}, {"scores", body.value("scores", json::object())}}
                                                          : json{{"error", FirstError(body)}};
            WriteJson(Folder() / "result.json", result_);
        }
        // What it found doesn't fit or looks wrong: the needs whose art that is.
        std::map<std::string, std::string> refused;
        auto lower = [](std::string t) {
            for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return t;
        };
        auto names = [&](const std::string& text, const std::string& word) {
            const std::string t = lower(text), w = lower(word);
            if (w.size() < 3) return false;
            for (size_t at = t.find(w); at != std::string::npos; at = t.find(w, at + 1)) {
                const bool before = at == 0 || !std::isalnum(static_cast<unsigned char>(t[at - 1]));
                const size_t end = at + w.size();
                const bool after = end >= t.size() || !std::isalnum(static_cast<unsigned char>(t[end])) || t[end] == 's';
                if (before && after) return true;
            }
            return false;
        };
        auto judge = [&](const std::string& type, const std::string& why) {
            for (const auto& n : assetNeeds_) {
                const std::string source = n.value("source", ""), entry = n.value("entry", "");
                if (entry.empty() || source == "placeholder" || source == "picked" || source == "dropped") continue;
                const std::string role = n.value("role", "");
                std::vector<std::string> words;
                for (size_t at = 0; at <= role.size();) {
                    const size_t bar = role.find('|', at);
                    words.push_back(role.substr(at, bar == std::string::npos ? std::string::npos : bar - at));
                    if (bar == std::string::npos) break;
                    at = bar + 1;
                }
                for (const auto& c : n.value("concepts", json::array())) {
                    if (c.is_string()) words.push_back(c.get<std::string>());
                }
                if (role == "ground-tile") words = {"ground", "floor", "terrain"};
                for (const auto& w : words) {
                    std::string spaced = w;
                    std::replace(spaced.begin(), spaced.end(), '-', ' ');
                    if (names(type, w) || names(type, spaced)) {
                        std::string& r = refused[n.value("key", "")];
                        r += (r.empty() ? "" : "; ") + why;
                        break;
                    }
                }
            }
        };
        for (const char* key : {"fit", "quality"}) {
            for (const auto& e : body.value(key, json::array())) {
                if (e.is_object() && e.contains("ok") && e["ok"].is_boolean() && !e["ok"].get<bool>()) judge(e.value("type", ""), e.value("why", ""));
            }
        }
        // Its worth, to compare the build before and after a redo: the scores' mean, less half a point
        // an error. The check's scores vary a little between looks; a redo is only undone when clearly worse.
        double worth = 0;
        {
            const json scores = body.value("scores", json::object());
            double sum = 0;
            int count = 0, errors = 0;
            for (const auto& [k, v] : scores.items()) {
                if (v.is_number()) sum += v.get<double>(), ++count;
            }
            for (const auto& p : body.value("problems", json::array())) errors += p.value("severity", "") == "error";
            worth = (count ? sum / count : 0.0) - 0.5 * errors;
        }
        if (body.value("ok", false) && buildRounds_ == 1 && worth < firstBuildWorth_ - 0.75 && !redoneFrom_.empty()) {
            // The redo made it worse (docs/build-fixes.md): the art before it comes back, the redo's is
            // refused, and the place is built once more.
            ++buildRounds_;
            for (const auto& [key, old] : redoneFrom_) {
                Tried& t = tried_[key];
                for (const auto& n : assetNeeds_) {
                    if (n.value("key", "") == key && n.value("entry", "") != old) t.refused.insert(n.value("entry", ""));
                }
                t.refused.erase(old);
                t.approved.insert(old);
                t.generate = true; // no more making: the earlier art
                redo_.push_back(key);
            }
            redoneFrom_.clear();
            Notice("The redone art made the place look worse; the art from before is put back.");
        } else if (body.value("ok", false) && !refused.empty() && buildRounds_ < 1) {
            ++buildRounds_;
            firstBuildWorth_ = worth;
            for (const auto& [key, why] : refused) {
                for (const auto& n : assetNeeds_) {
                    if (n.value("key", "") != key) continue;
                    Tried& t = tried_[key];
                    redoneFrom_[key] = n.value("entry", "");
                    t.refused.insert(n.value("entry", ""));
                    t.approved.erase(n.value("entry", ""));
                    t.generate = false; // made again (or found elsewhere), told why
                }
                redo_.push_back(key);
                redoReasons_[key] = "in the built scene: " + why;
            }
            Notice("The check of the built place refused the art of " + std::to_string(redo_.size()) + " part(s); they're found or made again.");
        } else if (buildRounds_ == 1) {
            buildRounds_ = 2; // the redo stands
        }
        status_ = body.value("ok", false) ? "Built and checked: " + body.value("summary", std::string())
                                          : "Built; the AI couldn't check it (" + FirstError(body) + ")";
        Save();
        return;
    }
    if (what.kind == "inspect-layout") {
        // The layout's check: its errors go back as a revision (the layout is checked again after it).
        Log({{"step", "layout check"}, {"answer", body}});
        layoutCheck_ = body;
        stage_ = beforeRevision_.empty() ? "layout" : beforeRevision_;
        status_.clear();
        std::string fixes;
        int n = 0;
        for (const auto& p : body.value("problems", json::array())) {
            if (!p.is_object() || p.value("severity", "") != "error") continue;
            fixes += "\n" + std::to_string(++n) + ") " + p.value("issue", "") + (p.value("fix", "").empty() ? "" : " Fix: " + p.value("fix", ""));
        }
        revisions_.push_back({{"by", "layout check"}, {"summary", body.value("summary", "")}, {"scores", body.value("scores", json::object())},
                              {"issues", [&] {
                                   json issues = json::array();
                                   for (const auto& p : body.value("problems", json::array())) issues.push_back(p.value("issue", ""));
                                   return issues;
                               }()}});
        // The best layout seen is kept (docs/build-fixes.md): a revision can make it worse, and the
        // last one isn't always the one to keep. Its worth: the scores' mean, less a point an error.
        double worth = -1e9;
        if (body.value("ok", false)) {
            const json scores = body.value("scores", json::object());
            double sum = 0;
            int count = 0;
            for (const auto& [k, v] : scores.items()) {
                if (v.is_number()) sum += v.get<double>(), ++count;
            }
            worth = (count ? sum / count : 0.0) - n;
            if (worth > bestLayoutWorth_) {
                bestLayoutWorth_ = worth;
                bestLayout_ = Snapshot();
                bestLayoutCheck_ = body;
            }
        }
        auto keepBest = [&] {
            if (bestLayout_.is_object() && worth < bestLayoutWorth_ - 0.25) {
                Restore(bestLayout_);
                stage_ = beforeRevision_.empty() ? stage_ : beforeRevision_;
                layoutCheck_ = bestLayoutCheck_;
                revisions_.push_back({{"by", "layout check"}, {"summary", "The last revision made the layout worse: the best one is kept."}});
                Log({{"step", "layout check"}, {"kept", "the best layout"}, {"worth", bestLayoutWorth_}});
            }
        };
        if (body.value("ok", false) && n > 0 && layoutRounds_ < 2) {
            ++layoutRounds_;
            // At ③ Plan (built without ② Layout) the zones are frozen for a user's revision; the
            // layout check is that step's check, so its revision may move them.
            layoutCheckRevising_ = true;
            if (Revise("The layout check found these problems in the zones; fix each one with the changes the rules allow, keeping "
                       "what's right:" + fixes).empty()) {
                Save();
                return;
            }
        }
        keepBest();
        layoutChecked_ = true;
        Save();
        return;
    }
    if (what.kind == "questions") {
        // The AI's questions replace the zones' own (kept if it had none).
        const json answer = body.value("answer", json::object());
        const json asked = answer.is_object() ? answer.value("questions", json::array()) : json::array();
        if (body.value("ok", false) && asked.is_array() && !asked.empty()) {
            json kept = json::array();
            for (const auto& q : asked) {
                if (q.is_object() && q.contains("text") && q["text"].is_string()) kept.push_back(q);
                else if (q.is_string()) kept.push_back({{"text", q}});
            }
            if (!kept.empty()) questions_ = kept;
        }
        Log({{"step", "questions"}, {"questions", questions_}, {"errors", body.value("errors", json::array())}});
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
            Log({{"step", "interpret"}, {"reading", what.node}, {"errors", body.value("errors", json::array())}});
            if (Retry(what, body)) {
                status_ = "The AI didn't answer; asking again (" + std::to_string(what.attempts + 1) + " of " + std::to_string(kAttempts) + ")…";
                return;
            }
            // This reading is lost; the others may still come. Only with none at all: the words alone.
            bool others = false;
            for (const auto& [id, p] : pending_) others = others || p.kind == "interpret";
            if (others) return;
            if (drafts_.empty()) {
                InterpretWithoutAi(FirstError(body));
                Save();
                return;
            }
            if (pending_.empty()) FinishInterpreting();
            Save();
            return;
        }
        const size_t before = drafts_.size();
        for (const auto& d : body.value("drafts", json::array())) {
            Log({{"step", "interpret"}, {"reading", d.value("reading", "")}, {"mode", d.value("mode", "")},
                 {"thinking", d.value("thinking", "")}, {"answer", d.value("brief", json())},
                 {"confidence", d.value("confidence", json())}, {"errors", d.value("errors", json::array())}});
            if (!d.contains("brief")) continue;
            drafts_.push_back(d);
            const json complete = CompleteBrief(ontology, d["brief"], Defaults());
            draftProblems_.push_back(ErrorsOf(ValidateBrief(ontology, complete)));
        }
        bool others = false;
        for (const auto& [id, p] : pending_) others = others || p.kind == "interpret";
        if (drafts_.empty() && !others) {
            InterpretWithoutAi("its answers had no Brief");
            Save();
            return;
        }
        // One repair round for each new draft that breaks the rules.
        for (size_t i = before; i < drafts_.size(); ++i) {
            if (draftProblems_[i].empty()) continue;
            Send({"repair", static_cast<int>(i), "", "/places/repair-brief",
                  {{"query", query_}, {"context", context_}, {"brief", drafts_[i]["brief"]}, {"problems", ProblemsJson(draftProblems_[i])}}, 300});
        }
        status_ = "Read " + std::to_string(drafts_.size()) + " of 3 readings…";
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
    // What the user chose in the form wins over every reading of the words: not disputed.
    if (!choices_.empty()) {
        brief_ = ApplyChoices(ontology, brief_, choices_);
        for (auto& f : merge_.fields) {
            for (const auto& [path, value] : choices_.items()) {
                if (f.path == path || f.path.rfind(path + ".", 0) == 0 || path.rfind(f.path + ".", 0) == 0 ||
                    (path == "buildType" && f.path == "buildType")) {
                    f.disputed = false;
                    f.confidence = 1.0;
                    f.alternatives.clear();
                    f.readings.clear();
                }
            }
        }
        Notice("Your choices are kept as you set them; the words filled in the rest.");
    }
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
        } else if (p.where == "shape.layout" || p.where.rfind("style.", 0) == 0 || p.where.rfind("root.", 0) == 0) {
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
    if (host_.layoutStep) {
        // ① Confirm: the layout first. The zones are planned at once; the questions are asked beside them.
        round_ = 2;
        WriteJson(Folder() / "brief.json", brief_);
        StartPlanning();
        AskQuestions();
    }
}

void PlaceBuild::Edit(const std::string& path, const json& value) {
    const bool confirming = stage_ == "layout";
    // ③ Assets (after the plan): only where each component's art comes from.
    const bool sources = (stage_ == "planned" || stage_ == "sourced" || stage_ == "built") && path.size() > 14 && path.substr(path.size() - 14) == ".assets.source";
    if (stage_ != "request" && stage_ != "assets" && !confirming && !sources) return;
    ApplyWindowEdit(host_.ontology(), brief_, path, value);
    // What the zones were planned from changed: the layout is out of date until planned again.
    if (confirming && path != "name" && path.rfind("style.", 0) != 0 && !sources) layoutStale_ = true;
    WriteJson(Folder() / "brief.json", brief_);
    Save();
}

std::string PlaceBuild::ReplanLayout() {
    if (stage_ != "layout") return "there's no layout to plan again";
    const auto errors = ErrorsOf(ValidateBrief(host_.ontology(), brief_));
    if (!errors.empty()) return errors.front().where + ": " + errors.front().message;
    layoutStale_ = false;
    UnfreezeLayout(false);
    WriteJson(Folder() / "brief.json", brief_);
    Log({{"step", "replan layout"}, {"directions", directions_}});
    StartPlanning();
    Save();
    return {};
}

std::string PlaceBuild::SetZoneKind(const std::string& module, const std::string& kind, const json& value) {
    // How many of one kind of building a zone holds (each zone of that module): in the Brief, so
    // Contents and Layout show the same; the zone's land follows (laid out again, each where it was).
    if (stage_ != "layout") return "the zones are set in ① Confirm";
    const Ontology& ontology = host_.ontology();
    const ModuleDef* m = ontology.Module(module);
    if (m == nullptr || m->level != PlaceLevel::Zone) return module + " isn't a zone";
    if (!ontology.ChildRule(module, kind)) return "a " + m->name + " can't hold a " + kind;
    const bool unset = value.is_null() || (value.is_string() && value.get<std::string>().empty());
    if (!unset && (!value.is_number() || value.get<double>() < 0)) return "how many is a whole number";
    const json briefBefore = brief_;
    SetBriefValue(brief_, "components." + module + ".kinds." + kind, unset ? json() : json(static_cast<int>(std::lround(value.get<double>()))));
    if (const json kinds = BriefValue(brief_, "components." + module + ".kinds"); kinds.is_object() && kinds.empty()) {
        brief_["components"][module].erase("kinds");
    }
    const json kinds = BriefValue(brief_, "components." + module + ".kinds");
    int sum = 0;
    if (kinds.is_object()) {
        for (const auto& [k, n] : kinds.items()) sum += n.is_number() ? n.get<int>() : 0;
    }
    const std::string field = CountField(*m);
    for (auto& c : spec_["root"]["children"]) {
        if (c.value("module", "") != module) continue;
        if (kinds.is_object() && !kinds.empty()) c["kinds"] = kinds;
        else c.erase("kinds");
        // At least as many in all as the kinds set.
        if (!field.empty()) {
            const json v = c["quantity"].value(field, json());
            const json n = v.is_object() ? v.value("count", json()) : v;
            const double now = n.is_number() ? n.get<double>() : n.is_array() && n.size() == 2 ? (n[0].get<double>() + n[1].get<double>()) / 2 : 0;
            if (now < sum) c["quantity"][field] = {{"count", sum}};
        }
    }
    WriteJson(Folder() / "brief.json", brief_);
    Log({{"step", "zone kind"}, {"module", module}, {"kind", kind}, {"value", value}});
    {
        // Undo goes back to before it (the Brief changed above: the snapshot wants the one before).
        json now = brief_;
        brief_ = briefBefore;
        Checkpoint((m ? m->name : module) + ": " + kind + (unset ? " left to the plan" : " = " + value.dump()));
        brief_ = now;
    }
    UnfreezeLayout(true);
    LayoutZones();
    Save();
    return {};
}

std::string PlaceBuild::SetZoneField(const std::string& zone, const std::string& field, const json& value) {
    if (stage_ != "layout") return "the zones are set in the layout";
    Checkpoint(zone + ": " + field + " = " + value.dump());
    json* node = SpecNode(spec_, zone);
    const ModuleDef* m = node ? host_.ontology().Module(node->value("module", "")) : nullptr;
    const FieldDef* f = m ? m->Field(field) : nullptr;
    if (f == nullptr) return "a " + (m ? m->name : zone) + " has no " + field;
    json& group = (*node)[f->quantity ? "quantity" : "quality"];
    if (value.is_null() || (value.is_string() && value.get<std::string>().empty())) {
        group.erase(field);
    } else {
        const json given = f->type == "count" && value.is_number() ? json{{"count", value}} : f->type == "area" && value.is_number() ? json{{"area", value}} : value;
        if (const std::string error = CheckFieldValue(host_.ontology(), *f, given); !error.empty()) return field + ": " + error;
        group[field] = given;
    }
    Log({{"step", "zone field"}, {"zone", zone}, {"field", field}, {"value", value}});
    // Its size moves the zones (laid out again, each where it was); its street pattern is for the plan.
    if (f->quantity) {
        UnfreezeLayout(true);
        LayoutZones();
    } else {
        WriteJson(Folder() / "spec.json", spec_);
    }
    Save();
    return {};
}

std::string PlaceBuild::PaintZone(const std::string& zone, const json& cells, bool erase) {
    if (stage_ != "layout") return "the zones are painted in the layout";
    json* node = SpecNode(spec_, zone);
    if (node == nullptr) return "no zone " + zone;
    Checkpoint(std::string(erase ? "Erased from " : "Painted ") + zone);
    const int W = std::max(1, SiteSide());
    std::set<int> stroke;
    for (const auto& c : cells) {
        if (c.is_array() && c.size() == 2 && c[0].is_number() && c[1].is_number()) {
            const int x = c[0].get<int>(), y = c[1].get<int>();
            if (x >= 0 && y >= 0 && x < W && y < W) stroke.insert(y * W + x);
        }
    }
    if (stroke.empty()) return {};
    // Its painted cells: what it had, with the stroke added (or taken away).
    auto paintedOf = [&](const json& n) {
        std::set<int> out;
        for (int c : DecodeCells(n.value("paint", json::array()), W)) out.insert(c);
        return out;
    };
    std::set<int> mine = paintedOf(*node);
    if (node->contains("layoutCells") && !node->contains("paint")) {
        for (int c : DecodeCells((*node)["layoutCells"], W)) mine.insert(c);
    }
    // Not painted yet: the land it has now is where painting starts (a stroke adds to it).
    if (!node->contains("paint") && !node->contains("at") && !node->contains("layoutCells")) {
        for (const auto& r : solved_.value("regions", json::array())) {
            if (r.value("statement", "") != "site/zones" || r.value("id", "") != zone) continue;
            for (int c : DecodeCells(r.value("cells", json::array()), W)) mine.insert(c);
        }
    }
    if (node->contains("at") && (*node)["at"].is_object()) {
        // A rectangle drawn before: painted cells now.
        const json& at = (*node)["at"];
        for (int y = at.value("y", 0); y < at.value("y", 0) + at.value("h", 0); ++y) {
            for (int x = at.value("x", 0); x < at.value("x", 0) + at.value("w", 0); ++x) {
                if (x < W && y < W) mine.insert(y * W + x);
            }
        }
        node->erase("at");
    }
    for (int c : stroke) {
        if (erase) mine.erase(c);
        else mine.insert(c);
    }
    auto store = [&](json& n, const std::set<int>& set) {
        if (set.empty()) {
            n.erase("paint");
            return;
        }
        n["paint"] = EncodeCells(std::vector<int>(set.begin(), set.end()), W);
        n["quantity"]["area"] = {{"area", static_cast<int>(set.size())}};
    };
    store(*node, mine);
    // No other zone keeps the cells painted for this one (its painting, or its kept cells).
    node->erase("layoutCells");
    if (!erase) {
        for (auto& c : spec_["root"]["children"]) {
            if (c.value("id", "") == zone) continue;
            if (c.contains("paint")) {
                std::set<int> theirs = paintedOf(c);
                for (int cell : stroke) theirs.erase(cell);
                store(c, theirs);
            }
            if (c.contains("layoutCells")) {
                std::vector<int> kept;
                for (int cell : DecodeCells(c["layoutCells"], W)) {
                    if (!stroke.contains(cell)) kept.push_back(cell);
                }
                c["layoutCells"] = EncodeCells(kept, W);
            }
        }
    }
    Log({{"step", erase ? "erase zone" : "paint zone"}, {"zone", zone}, {"cells", stroke.size()}});
    LayoutZones();
    Save();
    return {};
}

std::string PlaceBuild::DrawZone(const std::string& zone, const json& rect) {
    if (stage_ != "layout") return "the zones are drawn in the layout";
    json* node = SpecNode(spec_, zone);
    if (node == nullptr) return "no zone " + zone;
    Checkpoint(rect.is_object() && rect.value("w", 0) > 0 ? "Drew " + zone : "Let " + zone + " be placed");
    if (rect.is_object() && rect.value("w", 0) > 0 && rect.value("h", 0) > 0) {
        (*node)["at"] = {{"x", rect.value("x", 0)}, {"y", rect.value("y", 0)}, {"w", rect.value("w", 1)}, {"h", rect.value("h", 1)}};
        // Drawn: its area is what was drawn.
        (*node)["quantity"]["area"] = {{"area", rect.value("w", 1) * rect.value("h", 1)}};
    } else {
        node->erase("at");
        node->erase("paint"); // let it be placed: no drawing of any kind
        node->erase("layoutCells");
    }
    Log({{"step", "draw zone"}, {"zone", zone}, {"rect", rect}});
    // The others stay as they are (their kept cells, but what was drawn for this one).
    if (rect.is_object() && rect.value("w", 0) > 0) {
        const int W = std::max(1, SiteSide());
        const int x0 = rect.value("x", 0), y0 = rect.value("y", 0), x1 = x0 + rect.value("w", 1), y1 = y0 + rect.value("h", 1);
        node->erase("layoutCells");
        for (auto& c : spec_["root"]["children"]) {
            if (c.value("id", "") == zone || !c.contains("layoutCells")) continue;
            std::vector<int> kept;
            for (int cell : DecodeCells(c["layoutCells"], W)) {
                const int x = cell % W, y = cell / W;
                if (x < x0 || x >= x1 || y < y0 || y >= y1) kept.push_back(cell);
            }
            c["layoutCells"] = EncodeCells(kept, W);
        }
    }
    LayoutZones();
    Save();
    return {};
}

std::string PlaceBuild::Step(const std::string& step) {
    if (step == "layout" && (stage_ == "plan" || stage_ == "planned" || stage_ == "sourced" || stage_ == "built") && host_.layoutStep && spec_.contains("root")) {
        // Back to the layout: the zones as planned, without what's inside them.
        for (auto& c : spec_["root"]["children"]) {
            const ModuleDef* m = host_.ontology().Module(c.value("module", ""));
            if (m && m->level == PlaceLevel::Zone) c["children"] = json::array();
        }
        LayoutZones();
        Save();
        return {};
    }
    if ((stage_ == "plan" || stage_ == "planned" || stage_ == "layout") && (step == "request" || step == "assets")) {
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
        AskQuestions();
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

void PlaceBuild::AskQuestions() {
    const Ontology& ontology = host_.ontology();
    // Without the AI (or until it answers): where each zone the Brief turns on should go.
    questions_ = json::array();
    const json components = brief_.value("components", json::object());
    for (const auto& [id, c] : components.items()) {
        const ModuleDef* m = ontology.Module(id);
        if (m == nullptr || m->level != PlaceLevel::Zone || !c.is_object() || !c.value("enabled", true)) continue;
        std::string name = m->name;
        for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        questions_.push_back({{"text", "Where should the " + name + " be (by the river or a road, to the north, near another part)?"},
                              {"about", "placement"}, {"zone", id}});
    }
    Send({"questions", -1, "", "/places/questions",
          {{"query", query_}, {"brief", brief_}, {"context", PlanRootContext(ontology, brief_)}, {"choices", choices_}}, 300});
}

void PlaceBuild::AnswerDecision(const std::string& question, const std::string& answer) {
    if (answer.find_first_not_of(" \t\r\n") == std::string::npos) decisionAnswers_.erase(question);
    else decisionAnswers_[question] = answer;
    if (stage_ == "layout") layoutStale_ = true; // the zones are planned again with it
    Log({{"step", "decision"}, {"question", question}, {"answer", answer}});
    Save();
}

void PlaceBuild::SetDirections(const std::string& text) {
    directions_ = text;
    Log({{"step", "directions"}, {"text", text}});
    Save();
}

void PlaceBuild::StartPlanning() {
    progressStep_.clear(); // the progress page's clock starts again
    lastRetry_.clear();
    // The questions are over: one still unanswered doesn't hold the plan up.
    for (auto it = pending_.begin(); it != pending_.end();) it = it->second.kind == "questions" ? pending_.erase(it) : std::next(it);
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
    json body = {{"brief", brief_}, {"context", PlanRootContext(host_.ontology(), brief_)}, {"examples", ExamplesLikeThis()}};
    // The user's answers, in their words: to the questions, and to the decisions that set no field.
    std::string answers = directions_;
    for (const auto& [q, a] : decisionAnswers_.items()) {
        if (a.is_string()) answers += (answers.empty() ? "" : "\n") + q + " " + a.get<std::string>();
    }
    if (!answers.empty()) body["directions"] = {{"questions", questions_}, {"answers", answers}};
    if (host_.layoutStep) {
        if (const json grid = NetworkGrid(); grid.is_object()) body["grid"] = grid; // the AI sketches the zones on it
    }
    if (!Send({"plan-root", -1, "", "/places/plan-root", body, 600})) {
        Notice("The AI is off, so the plan comes from the Brief and the module rules alone.");
        spec_ = SpecFromRoot(host_.ontology(), brief_, FallbackRootAnswer(host_.ontology(), brief_), id_);
        if (host_.layoutStep) LayoutZones();
        else PlanZones();
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
        if (!RepairNodes("root")) {
            if (host_.layoutStep) LayoutZones();
            else PlanZones();
        }
        return;
    }
    if (what.kind == "plan-zone") {
        if (ok) {
            ApplyZoneAnswer(ontology, brief_, spec_, what.node, body["answer"]);
        } else if (json* zone = SpecNode(spec_, what.node)) {
            Notice("The AI couldn't plan " + what.node + " (" + FirstError(body) + "), so its parcels come from the module rules.");
            ApplyZoneAnswer(ontology, brief_, spec_, what.node, FallbackZoneAnswer(ontology, *zone, spec_["root"]));
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
    if (PlanWaiting() > 0) return;
    if (planPhase_ == "zones" && what.kind == "plan-zone" && RepairNodes("zones")) return;
    if (planPhase_ == "root") {
        if (host_.layoutStep) LayoutZones();
        else PlanZones();
    } else {
        FinishPlanning();
    }
}

std::string PlaceBuild::WhereText(const json& zone, const std::map<std::string, std::string>& names, int side) {
    // Its part of the site, the river, and what it touches: one sentence.
    auto compass = [](double dx, double dy) {
        const double m = std::max(std::abs(dx), std::abs(dy));
        if (m < 0.12) return std::string("the middle");
        std::string out;
        if (std::abs(dy) >= 0.4 * m) out += dy < 0 ? "north" : "south";
        if (std::abs(dx) >= 0.4 * m) out += (out.empty() ? "" : "-") + std::string(dx < 0 ? "west" : "east");
        return out;
    };
    std::string text;
    const json c = zone.value("centre", json::array());
    if (c.is_array() && c.size() == 2 && side > 0) {
        const std::string where = compass(c[0].get<double>() / side - 0.5, c[1].get<double>() / side - 0.5);
        text = where == "the middle" ? "In the middle" : "In the " + where;
    }
    if (const json river = zone.value("river", json()); river.is_object()) {
        if (river.value("nearest", 99) <= 2) {
            const std::string bank = river.value("bank", "both");
            text += std::string(text.empty() ? "" : "; ") + "along the river" + (bank == "both" ? " (both banks)" : " (its " + bank + " bank)");
        }
    }
    std::string beside;
    for (const auto& o : zone.value("others", json::array())) {
        if (!o.value("touching", false)) continue;
        const std::string id = o.value("zone", "");
        auto it = names.find(id);
        std::string name = it != names.end() ? it->second : id;
        for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        beside += (beside.empty() ? "" : ", ") + name + " (" + o.value("lies", "") + ")";
    }
    if (!beside.empty()) text += (text.empty() ? "" : "; ") + std::string("beside ") + beside;
    return text.empty() ? "" : text + ".";
}

// --- Composing a moved zone's place (place-layout.md §8.3) ------------------------------
//
// "South of the temple ward" says where the market goes along one axis only; along the other
// it is where the temple is: the temple on the east bank of a north-south river puts the
// market on the east bank too, and in the east of the map if the temple is. Each relation to a
// reference zone is read as a step along the axes (north-of: north, none across; near: none on
// either), and on an axis it doesn't move along (or moves the way the reference already lies),
// the moved zone takes the reference's side of every divider (a river, a main road) and its
// part of the map. What the request itself says about a divider or a part wins; references
// that disagree on an axis leave it open.

json PlaceBuild::ComposePlace(const std::string& zone, const json& rel, const std::string& request, const std::set<std::string>& moving) const {
    json out = {{"add", json::array()}, {"drop", json::array()}, {"why", json::array()}};
    const json b = solved_.value("grid", json::object()).value("bounds", json());
    const int W = b.is_array() && b.size() == 4 ? b[2].get<int>() : 0;
    const int H = b.is_array() && b.size() == 4 ? b[3].get<int>() : 0;
    if (W <= 0 || H <= 0) return out;
    // The zones as they lie now.
    std::map<std::string, std::vector<int>> cellsOf;
    for (const auto& r : solved_.value("regions", json::array())) {
        if (r.value("statement", "") == "site/zones") cellsOf[r.value("id", "")] = DecodeCells(r.value("cells", json::array()), W);
    }
    // The dividers: the river and the main roads, each running north-south (sides east, west)
    // or west-east (sides north, south); the line's position by row (or column).
    struct Divider {
        std::string id;
        bool northSouth = true;
        std::map<int, double> at; // row -> x (north-south), column -> y (west-east)
    };
    std::vector<Divider> dividers;
    for (const auto& p : solved_.value("paths", json::array())) {
        const std::string cls = p.value("class", "");
        if (cls != "river" && cls != "main") continue;
        const std::string id = p.value("node", p.value("id", ""));
        int x0 = W, x1 = 0, y0 = H, y1 = 0;
        std::map<int, std::pair<double, int>> rows, cols;
        for (const auto& c : p.value("cells", json::array())) {
            const int x = c[0].get<int>(), y = c[1].get<int>();
            x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
            rows[y].first += x, ++rows[y].second;
            cols[x].first += y, ++cols[x].second;
        }
        Divider d;
        d.id = id;
        d.northSouth = (y1 - y0) >= (x1 - x0);
        for (const auto& [k, v] : d.northSouth ? rows : cols) d.at[k] = v.first / v.second;
        if (d.at.size() < 2 || std::any_of(dividers.begin(), dividers.end(), [&](const Divider& e) { return e.id == id; })) continue;
        dividers.push_back(std::move(d));
    }
    auto lineAt = [](const std::map<int, double>& at, int k) {
        auto it = at.lower_bound(k);
        if (it == at.end()) return std::prev(it)->second;
        if (it == at.begin() || it->first == k) return it->second;
        auto before = std::prev(it);
        return k - before->first <= it->first - k ? before->second : it->second;
    };
    // A zone's side of a divider: +1 east (or south), -1 west (or north), 0 both or neither.
    auto sideOf = [&](const std::vector<int>& cells, const Divider& d) {
        if (cells.empty()) return 0;
        int plus = 0;
        for (int c : cells) {
            const int x = c % W, y = c / W;
            plus += d.northSouth ? (x + 0.5 > lineAt(d.at, y)) : (y + 0.5 > lineAt(d.at, x));
        }
        const double share = static_cast<double>(plus) / cells.size();
        return share > 0.85 ? 1 : share < 0.15 ? -1 : 0;
    };
    // Its part of the map: +1 east (south), -1 west (north), 0 the middle.
    auto partOf = [&](const std::vector<int>& cells, bool xAxis) {
        if (cells.empty()) return 0;
        double sum = 0;
        for (int c : cells) sum += xAxis ? (c % W + 0.5) / W : (c / W + 0.5) / H;
        const double f = sum / cells.size();
        return f > 0.58 ? 1 : f < 0.42 ? -1 : 0;
    };
    // What the request says itself, for this zone: a divider or a part it names wins.
    std::string text;
    for (const char c : request) text += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::set<std::string> told;
    for (const auto& [type, to] : DirectionsIn(request, zone)) {
        // A side it names ("east of the river"); "by the river" names none.
        if (type == "north-of" || type == "south-of" || type == "east-of" || type == "west-of") told.insert(to);
        if (type.rfind("in-", 0) == 0) told.insert(type == "in-east" || type == "in-west" ? "part-x" : "part-y");
    }
    for (const char* w : {"bank", "across", "other side", "opposite side", "both sides", "over the river", "beyond the river"}) {
        if (text.find(w) != std::string::npos) {
            for (const auto& d : dividers) {
                if (d.id.find("river") != std::string::npos) told.insert(d.id);
            }
        }
    }
    for (const char* w : {" in the east", " in the west", " to the east", " to the west", " eastern", " western", " east side", " west side"}) {
        if (text.find(w) != std::string::npos) told.insert("part-x");
    }
    for (const char* w : {" in the north", " in the south", " to the north", " to the south", " northern", " southern", " north side", " south side"}) {
        if (text.find(w) != std::string::npos) told.insert("part-y");
    }
    // Each reference, as a step along the axes.
    std::map<std::string, std::pair<int, int>> step; // reference -> (dx, dy)
    std::map<std::string, bool> beside;              // near, adjacent, along: no step, the same side of everything
    for (const auto& r : rel) {
        const std::string type = r.value("type", ""), to = r.value("to", "");
        if (!cellsOf.contains(to) || to == zone || moving.contains(to)) continue;
        auto& s = step[to];
        if (type == "north-of") s.second -= 1;
        else if (type == "south-of") s.second += 1;
        else if (type == "east-of") s.first += 1;
        else if (type == "west-of") s.first -= 1;
        else if (type == "near" || type == "adjacent" || type == "along") beside[to] = true;
        else step.erase(to);
    }
    // On an axis the step doesn't cross (none along it, or the way the reference already lies),
    // the reference's value on that axis carries over.
    auto carries = [](int stepOnAxis, int value) { return value != 0 && (stepOnAxis == 0 || stepOnAxis == value); };
    std::map<std::string, std::set<int>> implied; // divider id (or part-x, part-y) -> values from the references
    std::map<std::string, std::string> because;
    auto name = [&](const std::string& id) {
        json copy = spec_;
        const json* n = SpecNode(copy, id);
        const ModuleDef* m = n ? host_.ontology().Module(n->value("module", "")) : nullptr;
        return m ? m->name : id;
    };
    for (const auto& [ref, s] : step) {
        const auto& cells = cellsOf[ref];
        for (const auto& d : dividers) {
            const int side = sideOf(cells, d);
            const int onAxis = d.northSouth ? s.first : s.second;
            if (carries(onAxis, side) || (beside.contains(ref) && side != 0)) {
                implied[d.id].insert(side);
                because[d.id] = ref;
            }
        }
        if (beside.contains(ref)) continue; // near a zone: its side of things, not its part of the map
        for (const bool xAxis : {true, false}) {
            const int part = partOf(cells, xAxis);
            if (carries(xAxis ? s.first : s.second, part)) {
                implied[xAxis ? "part-x" : "part-y"].insert(part);
                because[xAxis ? "part-x" : "part-y"] = ref;
            }
        }
    }
    for (const auto& [key, values] : implied) {
        if (told.contains(key)) continue;
        if (values.size() != 1) {
            out["why"].push_back(zone + ": its references lie on both sides of " + (key.rfind("part-", 0) == 0 ? std::string("the map") : name(key)) +
                                 ", so that is left open");
            continue;
        }
        const int v = *values.begin();
        std::string type, opposite, to;
        if (key == "part-x") type = v > 0 ? "in-east" : "in-west", opposite = v > 0 ? "in-west" : "in-east", to = "site";
        else if (key == "part-y") type = v > 0 ? "in-south" : "in-north", opposite = v > 0 ? "in-north" : "in-south", to = "site";
        else {
            const bool ns = std::find_if(dividers.begin(), dividers.end(), [&](const Divider& d) { return d.id == key; })->northSouth;
            type = ns ? (v > 0 ? "east-of" : "west-of") : (v > 0 ? "south-of" : "north-of");
            opposite = ns ? (v > 0 ? "west-of" : "east-of") : (v > 0 ? "north-of" : "south-of");
            to = key;
        }
        out["add"].push_back({{"type", type}, {"to", to}});
        out["drop"].push_back({{"type", opposite}, {"to", to}});
        const std::string ref = because[key];
        const std::string where = key.rfind("part-", 0) == 0 ? "in the " + type.substr(3) + " of the map"
                                                             : type.substr(0, type.find('-')) + " of the " + name(key);
        out["why"].push_back(name(zone) + ": " + where + ", as the " + name(ref) + " it's placed by is (the request doesn't say otherwise)");
    }
    return out;
}

void PlaceBuild::RelayoutMoving(const std::set<std::string>& moving) {
    // The zones the user moved, placed anew; every other zone keeps its cells exactly. Only if a
    // moved zone then hasn't the room it needs do the others give way (each from its seed).
    const json spec = spec_;
    const json solved = solved_;
    UnfreezeLayout(true, moving, true);
    LayoutZones();
    bool cramped = false;
    for (const auto& q : LayoutQuality(spec_, solved_).value("checks", json::array())) {
        cramped = cramped || (q.value("check", "") == "room" && moving.contains(q.value("zone", "")));
    }
    if (!cramped) return;
    spec_ = spec;
    solved_ = solved;
    UnfreezeLayout(true, moving, false);
    // What the request named as where the zone goes, or as staying, doesn't give way.
    for (const auto& id : anchored_) {
        json copy = spec;
        const json* was = SpecNode(copy, id);
        json* node = SpecNode(spec_, id);
        if (was && node && was->contains("layoutCells") && !moving.contains(id)) (*node)["layoutCells"] = (*was)["layoutCells"];
    }
    LayoutZones();
    if (!revisions_.empty()) revisions_.back()["notes"].push_back("To make room, the zones around it moved a little.");
    Log({{"step", "relayout"}, {"moving", moving}, {"othersGaveWay", true}});
}

void PlaceBuild::UnfreezeLayout(bool keepPlaces, const std::set<std::string>& moving, bool keepOthersCells) {
    // The user asked for the zones to be laid out again (Revise, a size, "Lay out again"):
    // their kept cells go; with `keepPlaces`, the zones not moving keep their seeds.
    keepSeeds_.clear();
    if (keepPlaces) {
        for (const auto& r : solved_.value("regions", json::array())) {
            if (r.value("statement", "") == "site/zones" && r.contains("seed") && !moving.contains(r.value("id", ""))) {
                keepSeeds_[r.value("id", "")] = {r["seed"][0].get<int>(), r["seed"][1].get<int>()};
            }
        }
    }
    if (!spec_.contains("root")) return;
    for (auto& c : spec_["root"]["children"]) {
        if (!keepOthersCells || moving.contains(c.value("id", ""))) c.erase("layoutCells");
        // Asked to move: a painted or drawn zone isn't held to its painting any more.
        if (moving.contains(c.value("id", "")) && (c.contains("paint") || c.contains("at"))) {
            c.erase("paint");
            c.erase("at");
            if (!revisions_.empty() && revisions_.back().contains("notes")) {
                revisions_.back()["notes"].push_back(c.value("id", "") + ": its painted land was let go, so it could move");
            }
        }
    }
    if (spec_["root"].contains("sketch") && spec_["root"]["sketch"].contains("legend")) {
        json legend = json::object();
        for (const auto& [letter, id] : spec_["root"]["sketch"]["legend"].items()) {
            if (!id.is_string() || !moving.contains(id.get<std::string>())) legend[letter] = id;
        }
        spec_["root"]["sketch"]["legend"] = legend;
    }
    if (!keepPlaces) {
        spec_["root"].erase("layoutSide");
        spec_["root"].erase("layoutSeed");
    }
}

bool PlaceBuild::LayoutFrozen() const {
    if (!spec_.contains("root")) return false;
    for (const auto& c : spec_["root"].value("children", json::array())) {
        if (c.contains("layoutCells")) return true;
    }
    return false;
}

void PlaceBuild::LayoutZones() {
    const Ontology& ontology = host_.ontology();
    for (auto& note : TidySpec(ontology, spec_)) tidied_.push_back(std::move(note));
    // The zones alone: no parcels yet, so each takes the land the plan gives it.
    json zones = spec_;
    for (auto& c : zones["root"]["children"]) {
        const ModuleDef* m = ontology.Module(c.value("module", ""));
        if (m == nullptr || m->level != PlaceLevel::Zone) continue;
        c["children"] = json::array();
        // Room for what it's to hold, at least (a drawn or painted zone has what was drawn).
        if (!c.contains("at") && !c.contains("paint") && !c.contains("layoutCells")) {
            const json area = c.value("quantity", json::object()).value("area", json());
            const double planned = area.is_object() && area.contains("area") && area["area"].is_number() ? area["area"].get<double>() : 0;
            if (const int needed = ZoneNeed(ontology, c); needed > planned) c["quantity"]["area"] = {{"area", needed}};
        }
    }
    CompileOptions options;
    options.seed = Fnv1a(id_);
    // Laid out before: with that layout's seed (the river and roads where they were).
    if (const json s = spec_["root"].value("layoutSeed", json()); s.is_number_unsigned()) options.seed = s.get<uint64_t>();
    options.side = BriefSide();
    {
        // Room for them all (as a plan's site grows for its parcels): the zones' land, and open land between.
        double total = 0;
        for (const auto& c : zones["root"]["children"]) {
            const json area = c.value("quantity", json::object()).value("area", json());
            if (area.is_object() && area.contains("area") && area["area"].is_number()) total += area["area"].get<double>();
        }
        // Mostly zones: open land between them, not a site of bare green (a quarter at most).
        options.side = std::max(options.side, static_cast<int>(std::ceil(std::sqrt(total / 0.75))));
        if (layoutSide_ > 0 && !keepSeeds_.empty()) options.side = std::max(options.side, layoutSide_); // a revision: the same site, or bigger
        // Something drawn or painted: on this very grid (its cells are the site's).
        bool drawn = false;
        for (const auto& c : zones["root"]["children"]) drawn = drawn || c.contains("at") || c.contains("paint") || c.contains("layoutCells");
        if (drawn && layoutSide_ > 0) options.side = layoutSide_;
        // A kept layout: on its own site, exactly.
        if (const int kept = spec_["root"].value("layoutSide", 0); kept > 0 && drawn) {
            options.side = kept;
            options.fixedSide = true;
        }
    }
    options.isoGrid = host_.isoGrid ? host_.isoGrid() : true;
    options.keepSeeds = keepSeeds_; // a revision keeps the zones it didn't move
    keepSeeds_.clear();
    ApplySketch(zones, options.side);
    // A fresh layout (nothing kept): a few seeds, the best by the quality checks; a kept or
    // revised one, as it is.
    const bool fresh = !LayoutFrozen() && options.keepSeeds.empty();
    json best;
    json bestQuality;
    int bestScore = 1 << 30;
    uint64_t bestSeed = options.seed;
    for (int k = 0; k < (fresh ? 4 : 1); ++k) {
        CompileOptions o = options;
        o.seed = options.seed + static_cast<uint64_t>(k) * 7919;
        const json ir = CompileSpec(ontology, zones, o);
        if (k == 0) WriteJson(Folder() / "zones.ir.json", ir);
        const json solved = SolveLayout(ontology, ir).document;
        const json quality = LayoutQuality(spec_, solved);
        const int score = quality.value("score", 0);
        Log({{"step", "layout try"}, {"try", k + 1}, {"score", score}});
        if (score < bestScore) {
            bestScore = score;
            best = solved;
            bestQuality = quality;
            bestSeed = o.seed;
        }
        if (score == 0) break;
    }
    solved_ = best;
    layoutQuality_ = bestQuality;
    WriteJson(Folder() / "spec.json", spec_);
    WriteJson(Folder() / "zones.solved.json", solved_);
    layoutSide_ = SiteSide();
    // The zones' land as laid out is the plan's (the size inputs show it), and kept: each
    // zone's cells, the site's side. The plan fills them; nothing else moves them.
    for (const auto& r : solved_.value("regions", json::array())) {
        if (r.value("statement", "") != "site/zones") continue;
        if (json* node = SpecNode(spec_, r.value("id", ""))) {
            (*node)["quantity"]["area"] = {{"area", r.value("area", 0)}};
            (*node)["layoutCells"] = r.value("cells", json::array());
        }
    }
    spec_["root"]["layoutSide"] = layoutSide_;
    spec_["root"]["layoutSeed"] = bestSeed;
    WriteJson(Folder() / "spec.json", spec_);
    RenderMap();
    {
        std::error_code ec;
        fs::copy_file(Folder() / "layout.png", Folder() / "zones.png", fs::copy_options::overwrite_existing, ec);
    }
    stage_ = "layout";
    status_ = "The zones and where they go: revise them here, then plan what's inside them.";
    Log({{"step", "layout"}, {"zones", ZoneIds(ontology, spec_).size()}});
}

// --- The AI's sketch and the layout's checks (place-layout.md §11) ----------------------

namespace {
constexpr int kSketchGrid = 16;
} // namespace

json PlaceBuild::NetworkGrid() {
    // The site with only what isn't a zone (the river, the roads, the walls, a moat), solved on
    // its own and drawn on a 16x16 grid: what the AI sketches the zones on. Any build type whose
    // zones share one site (a settlement's wards, a castle's baileys, a farm's fields, a camp's
    // areas, a region's biomes); a dungeon's levels and a building's floors have none.
    const Ontology& ontology = host_.ontology();
    json spec = SpecFromRoot(ontology, brief_, FallbackRootAnswer(ontology, brief_), id_);
    if (!spec.contains("root")) return json();
    for (auto& c : spec["root"]["children"]) {
        const ModuleDef* m = ontology.Module(c.value("module", ""));
        if (m && m->level == PlaceLevel::Zone) c["children"] = json::array();
    }
    CompileOptions options;
    options.seed = Fnv1a(id_);
    options.side = BriefSide();
    options.isoGrid = host_.isoGrid ? host_.isoGrid() : true;
    const json ir = CompileSpec(ontology, spec, options);
    bool partition = false;
    for (const auto& st : ir.value("statements", json::array())) partition = partition || (st.value("op", "") == "partition" && st.value("id", "") == "site/zones");
    if (!partition) return json();
    const json solved = SolveLayout(ontology, ir).document;
    const json b = solved.value("grid", json::object()).value("bounds", json());
    const int W = b.is_array() && b.size() == 4 ? b[2].get<int>() : options.side;
    const int H = b.is_array() && b.size() == 4 ? b[3].get<int>() : options.side;
    if (W <= 0 || H <= 0) return json();
    std::vector<uint8_t> water(W * H, 0), road(W * H, 0), wall(W * H, 0), outside(W * H, 0);
    auto mark = [&](std::vector<uint8_t>& v, int x, int y) {
        if (x >= 0 && y >= 0 && x < W && y < H) v[y * W + x] = 1;
    };
    for (const auto& p : solved.value("paths", json::array())) {
        const bool wet = p.value("terrain", "") == "water" || p.value("class", "") == "river";
        for (const auto& c : p.value("cells", json::array())) mark(wet ? water : road, c[0].get<int>(), c[1].get<int>());
    }
    for (const auto& c : solved.value("roads", json::array())) mark(road, c[0].get<int>(), c[1].get<int>());
    for (const auto& r : solved.value("regions", json::array())) {
        const std::string id = r.value("id", "");
        if (id.ends_with("/water")) {
            for (int c : DecodeCells(r.value("cells", json::array()), W)) water[c] = 1;
        }
        // Walled: only inside the walls is for the zones.
        if (id == "site/inner") {
            std::fill(outside.begin(), outside.end(), 1);
            for (int c : DecodeCells(r.value("cells", json::array()), W)) outside[c] = 0;
        }
    }
    for (const auto& l : solved.value("lines", json::array())) {
        for (const auto& e : l.value("edges", json::array())) mark(wall, e[0].get<int>(), e[1].get<int>());
    }
    json rows = json::array();
    std::set<char> seen;
    for (int gy = 0; gy < kSketchGrid; ++gy) {
        std::string row;
        for (int gx = 0; gx < kSketchGrid; ++gx) {
            const int x0 = gx * W / kSketchGrid, x1 = std::max(x0 + 1, (gx + 1) * W / kSketchGrid);
            const int y0 = gy * H / kSketchGrid, y1 = std::max(y0 + 1, (gy + 1) * H / kSketchGrid);
            double n = 0, wet = 0, paved = 0, walls = 0, out = 0;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const int c = y * W + x;
                    ++n;
                    wet += water[c];
                    paved += road[c];
                    walls += wall[c];
                    out += outside[c];
                }
            }
            const char ch = wet / n >= 0.25 ? '~' : walls > 0 ? '#' : out / n > 0.5 ? 'x' : paved / n >= 0.08 ? '=' : '.';
            seen.insert(ch);
            row += ch;
        }
        rows.push_back(row);
    }
    json symbols = {{".", "open land"}};
    if (seen.contains('~')) symbols["~"] = "water (a river, a moat, the coast)";
    if (seen.contains('=')) symbols["="] = "a road or path";
    if (seen.contains('#')) symbols["#"] = "a wall";
    if (seen.contains('x')) symbols["x"] = "outside the walls: no zone of the inside here";
    const int cell = std::max(1, W / kSketchGrid);
    return {{"rows", rows}, {"symbols", symbols}, {"cell", cell}, {"side", W},
            {"note", "a zone of " + std::to_string(cell * cell * 10) + " cells of area is about 10 grid cells"}};
}

void PlaceBuild::ApplySketch(json& zones, int side) const {
    // The AI's sketch, at this site's size: each zone's sketched cells (a guide for where it grows).
    const json sketch = spec_.contains("root") ? spec_["root"].value("sketch", json()) : json();
    if (!sketch.is_object() || side <= 0) return;
    const json rows = sketch.value("rows", json::array());
    const int G = static_cast<int>(rows.size());
    if (G == 0) return;
    const json legend = sketch.value("legend", json::object());
    for (const auto& [letter, id] : legend.items()) {
        if (letter.empty() || !id.is_string()) continue;
        json* node = SpecNode(zones, id.get<std::string>());
        if (node == nullptr || node->contains("paint") || node->contains("at") || node->contains("layoutCells")) continue;
        std::vector<int> cells;
        for (int gy = 0; gy < G; ++gy) {
            const std::string row = rows[gy].is_string() ? rows[gy].get<std::string>() : "";
            const int g = std::max<int>(1, static_cast<int>(row.size()));
            for (int gx = 0; gx < static_cast<int>(row.size()); ++gx) {
                if (row[gx] != letter[0]) continue;
                for (int y = gy * side / G; y < (gy + 1) * side / G; ++y) {
                    for (int x = gx * side / g; x < (gx + 1) * side / g; ++x) cells.push_back(y * side + x);
                }
            }
        }
        std::sort(cells.begin(), cells.end());
        if (!cells.empty()) (*node)["sketchCells"] = EncodeCells(cells, side);
    }
}

json PlaceBuild::LayoutQuality(const json& spec, const json& solved) const {
    // Is the layout a good one (place-layout.md §11.3)? Each zone one piece, compact, not
    // inside another, the room its buildings need, where it was asked to be; the site not
    // mostly bare. Each failing check adds to the score (0: all good); the warnings are shown.
    const Ontology& ontology = host_.ontology();
    json checks = json::array();
    int score = 0;
    const json b = solved.value("grid", json::object()).value("bounds", json());
    const int W = b.is_array() && b.size() == 4 ? b[2].get<int>() : 0;
    const int H = b.is_array() && b.size() == 4 ? b[3].get<int>() : 0;
    if (W <= 0 || H <= 0 || !spec.contains("root")) return {{"score", 0}, {"checks", checks}};
    std::vector<int> owner(W * H, -1);
    std::vector<std::string> ids;
    std::vector<std::vector<int>> cellsOf;
    for (const auto& r : solved.value("regions", json::array())) {
        if (r.value("statement", "") != "site/zones") continue;
        ids.push_back(r.value("id", ""));
        cellsOf.push_back(DecodeCells(r.value("cells", json::array()), W));
        for (int c : cellsOf.back()) owner[c] = static_cast<int>(ids.size()) - 1;
    }
    auto fail = [&](const std::string& zone, const std::string& check, const std::string& text, int weight) {
        checks.push_back({{"zone", zone}, {"check", check}, {"text", text}});
        score += weight;
    };
    std::map<std::string, int> modules;
    long total = 0;
    for (size_t k = 0; k < ids.size(); ++k) {
        const std::string& id = ids[k];
        const auto& cells = cellsOf[k];
        total += static_cast<long>(cells.size());
        json probe = spec;
        const json* node = SpecNode(probe, id);
        if (node == nullptr || cells.empty()) continue;
        ++modules[node->value("module", "")];
        // One piece (a road through it doesn't split it: up to 5 cells apart is joined).
        std::vector<int> part(W * H, -1);
        std::vector<int> sizes;
        for (int start : cells) {
            if (part[start] >= 0) continue;
            const int label = static_cast<int>(sizes.size());
            sizes.push_back(0);
            std::vector<int> stack = {start};
            part[start] = label;
            while (!stack.empty()) {
                const int c = stack.back();
                stack.pop_back();
                ++sizes[label];
                static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                for (int d = 0; d < 4; ++d) {
                    for (int step = 1; step <= 5; ++step) {
                        const int x = c % W + dx[d] * step, y = c / W + dy[d] * step;
                        if (x < 0 || y < 0 || x >= W || y >= H) break;
                        const int n = y * W + x;
                        if (owner[n] == static_cast<int>(k)) {
                            if (part[n] < 0) {
                                part[n] = label;
                                stack.push_back(n);
                            }
                            break;
                        }
                        if (owner[n] >= 0) break; // another zone between: not joined
                    }
                }
            }
        }
        int pieces = 0;
        for (int s : sizes) pieces += s >= 0.08 * cells.size();
        if (pieces > 1) fail(id, "split", id + " lies in " + std::to_string(pieces) + " separate pieces", 3);
        // Compact: not a long straggle.
        int x0 = W, y0 = H, x1 = 0, y1 = 0;
        for (int c : cells) x0 = std::min(x0, c % W), x1 = std::max(x1, c % W), y0 = std::min(y0, c / W), y1 = std::max(y1, c / W);
        const double fill = static_cast<double>(cells.size()) / std::max(1, (x1 - x0 + 1) * (y1 - y0 + 1));
        if (fill < 0.4 && pieces <= 1) fail(id, "compact", id + " straggles (it fills " + std::to_string(std::lround(fill * 100)) + "% of its bounds)", 1);
        // Inside another: most of its edge on one other zone.
        std::map<int, int> edges;
        int edgeTotal = 0;
        for (int c : cells) {
            static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; ++d) {
                const int x = c % W + dx[d], y = c / W + dy[d];
                if (x < 0 || y < 0 || x >= W || y >= H) {
                    ++edgeTotal;
                    continue;
                }
                const int o = owner[y * W + x];
                if (o == static_cast<int>(k)) continue;
                ++edgeTotal;
                if (o >= 0) ++edges[o];
            }
        }
        for (const auto& [o, n] : edges) {
            if (edgeTotal > 0 && n > 0.7 * edgeTotal) fail(id, "enclosed", id + " lies inside " + ids[o], 3);
        }
        // The room its buildings need.
        if (const int need = ZoneNeed(ontology, *node); need > 0 && cells.size() < 0.9 * need) {
            fail(id, "room", id + " has " + std::to_string(cells.size()) + " cells; its buildings need about " + std::to_string(need), 2);
        }
        // Where it was asked to be.
        json expects = json::array();
        for (const auto& r : node->value("rel", json::array())) {
            const std::string type = r.value("type", ""), to = r.value("to", "");
            if (type.empty() || to.empty() || (to == "site" && type.rfind("in-", 0) != 0) || !ontology.Relation(type)) continue;
            expects.push_back({{"node", id}, {"type", type}, {"to", to}});
        }
        for (const auto& c : CheckExpectations(solved, expects)) {
            if (c.value("ok", true)) continue;
            fail(id, "relation", id + " isn't " + c.value("type", "") + " " + c.value("to", "") + " (" + c.value("detail", "") + ")", 2);
        }
    }
    for (const auto& [module, n] : modules) {
        const json given = brief_.value("components", json::object()).value(module, json::object());
        const json count = given.is_object() ? given.value("count", json()) : json();
        const double hi = count.is_number() ? count.get<double>() : count.is_array() && !count.empty() && count.back().is_number() ? count.back().get<double>() : -1;
        if (n > 1 && hi >= 1 && hi < n) fail(module, "duplicate", std::to_string(n) + " zones are a " + module + "; the Brief asks for " + (hi >= 1 ? std::to_string(std::lround(hi)) : std::string("one")), 2);
    }
    if (total < 0.45 * W * H) fail("", "bare", "the zones cover " + std::to_string(std::lround(100.0 * total / (W * H))) + "% of the site: much of it is bare", 1);
    return {{"score", score}, {"checks", checks}};
}

std::string PlaceBuild::PlanFromLayout() {
    if (stage_ != "layout") return "there's no layout to plan from";
    if (layoutStale_) return "the Contents changed since the layout was made: lay it out again first";
    if (const auto errors = ErrorsOf(ValidateBrief(host_.ontology(), brief_)); !errors.empty()) {
        return errors.front().where + ": " + errors.front().message;
    }
    for (auto it = pending_.begin(); it != pending_.end();) it = it->second.kind == "questions" ? pending_.erase(it) : std::next(it);
    // The zones exactly as laid out (their kept cells): the plan only fills them.
    layoutSeeds_.clear();
    layoutSide_ = SiteSide();
    solved_ = json();
    stage_ = "planning";
    status_ = "Planning what's inside each zone…";
    Log({{"step", "plan from layout"}});
    PlanZones();
    Save();
    return {};
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
    const int waiting = PlanWaiting();
    if (waiting > 0) status_ = "Fixing " + std::to_string(waiting) + " part(s) of the plan…";
    return waiting > 0;
}

json PlaceBuild::ConfirmProgress() {
    auto waiting = [&](const std::string& kind) {
        int n = 0;
        for (const auto& [id, p] : pending_) n += p.kind == kind;
        return n;
    };
    const bool reading = stage_ == "interpreting";
    const bool zonesPlanning = stage_ == "planning" && planPhase_ == "root";
    const bool laidOut = stage_ == "layout";
    json steps = json::array();
    auto add = [&](const std::string& id, const std::string& label, const std::string& state, json detail) {
        steps.push_back({{"id", id}, {"label", label}, {"state", state}, {"detail", detail}});
    };
    // 1. The readings of the request.
    {
        json detail = json::array();
        const int asked = 3, got = static_cast<int>(drafts_.size());
        if (reading && got == 0) {
            detail.push_back("The AI reads it three ways, side by side: literally, as a designer typically would, and generously.");
            detail.push_back("Each reading becomes a Brief: the build type, its size in numbers, its parts, its style.");
        } else {
            for (const auto& d : drafts_) detail.push_back("✓ the " + d.value("reading", std::string("reading")) + " reading");
        }
        for (const auto& [id, p] : pending_) {
            if (p.kind == "interpret") detail.push_back("… the " + p.node + " reading");
        }
        for (const auto& [id, p] : pending_) {
            if (p.kind == "interpret" && p.attempts > 1) {
                detail.push_back("The AI didn't answer (" + lastRetry_ + "); asking again: attempt " + std::to_string(p.attempts) + " of " +
                                 std::to_string(kAttempts) + ".");
            }
        }
        add("read", "Reading your request (" + std::to_string(std::min(got, asked)) + " of " + std::to_string(asked) + " readings)",
            reading && waiting("interpret") > 0 ? "active" : "done", detail);
    }
    // 2. Readings that broke the rules, corrected once each.
    {
        json detail = json::array();
        int broken = 0;
        for (size_t i = 0; i < drafts_.size() && i < draftProblems_.size(); ++i) {
            if (draftProblems_[i].empty()) continue;
            ++broken;
            for (const auto& p : draftProblems_[i]) {
                detail.push_back("the " + drafts_[i].value("reading", std::string("")) + " reading: " + p.where + ": " + p.message);
                if (detail.size() >= 6) break;
            }
        }
        const int fixing = waiting("repair");
        // Done only when every reading is in and none is being corrected.
        const std::string state = reading && fixing > 0 ? "active" : reading && (drafts_.empty() || waiting("interpret") > 0) ? "waiting" : "done";
        add("fix", broken == 0 && state == "done" ? "Checking the readings against the rules: all fine"
                                                  : "Correcting " + std::to_string(fixing > 0 ? fixing : broken) + " reading(s) that broke the rules",
            state, detail);
    }
    // 3. The readings compared: where they differ is what's left to decide.
    add("compare", "Comparing the readings", reading ? "waiting" : "done",
        reading ? json::array({"What they disagree on is marked for you to decide."}) : json::array());
    // 4. The zones: what they are and where they go.
    {
        json detail = json::array();
        if (zonesPlanning && waiting("plan-root") > 0) {
            detail.push_back("The AI plans the zones (wards, farmland), the roads and walls, and where each goes.");
            if (!directions_.empty()) detail.push_back("Following your answers: " + directions_);
        }
        if (zonesPlanning && waiting("repair-node") > 0) detail.push_back("Fixing " + std::to_string(waiting("repair-node")) + " zone(s) that broke their rules.");
        // Asked again: why, and which try this is.
        for (const auto& [id, p] : pending_) {
            if (p.kind != "plan-root" || p.attempts <= 1) continue;
            const bool slow = lastRetry_.find("timed out") != std::string::npos || lastRetry_.find("took too long") != std::string::npos;
            detail.push_back(std::string(slow ? "The AI didn't answer in time" : "The AI's answer failed (" + lastRetry_ + ")") +
                             "; asking again: attempt " + std::to_string(p.attempts) + " of " + std::to_string(kAttempts) +
                             ". Without an answer, the zones come from the Brief alone.");
        }
        add("zones", "Planning the zones", zonesPlanning ? "active" : laidOut ? "done" : "waiting", detail);
    }
    // 5. The map of them.
    add("layout", "Laying the zones out on the map", laidOut ? "done" : "waiting", json::array());
    // How far: each step done counts one, the active one half.
    double done = 0;
    std::string active;
    for (const auto& s : steps) {
        if (s["state"] == "done") done += 1;
        else if (s["state"] == "active") done += 0.5, active = active.empty() ? s["id"].get<std::string>() : active;
    }
    const long long now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (active != progressStep_) {
        progressStep_ = active;
        progressSince_ = now;
    }
    return {{"steps", steps}, {"fraction", done / steps.size()}, {"since", progressSince_}, {"now", now}};
}

int PlaceBuild::PlanWaiting() const {
    // The plan's own requests: the questions or the art being made beside it don't hold it up.
    int n = 0;
    for (const auto& [id, p] : pending_) n += p.kind == "plan-root" || p.kind == "plan-zone" || p.kind == "repair-node";
    return n;
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
            ApplyZoneAnswer(ontology, brief_, spec_, zoneId, FallbackZoneAnswer(ontology, zone, spec_["root"]));
            ++zonesDone_;
            offline = true;
        }
    }
    if (offline) Notice("The AI is off, so the zones' parcels come from the module rules.");
    if (PlanWaiting() == 0) {
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
        if (PlanWaiting() > 0) {
            status_ = "Fixing " + std::to_string(PlanWaiting()) + " part(s) of the plan…";
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
    MeetExpectations();
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
    // A kept layout (① Confirm's): its site and its zones' cells, exactly; no round may grow a zone.
    const bool frozen = LayoutFrozen() && spec_["root"].value("layoutSide", 0) > 0;
    if (frozen) {
        options.side = spec_["root"]["layoutSide"].get<int>();
        options.fixedSide = true;
        if (const json s = spec_["root"].value("layoutSeed", json()); s.is_number_unsigned()) options.seed = s.get<uint64_t>();
        options.zoneScale.clear();
        options.keepSeeds.clear();
    }
    // Fitting rounds: a zone that couldn't place all it's planned to gets more
    // land (and the site grows if it must), until each holds its parcels or
    // four rounds are done; the best layout is kept.
    json ir, best, bestIr;
    std::map<std::string, double> bestScale;
    std::vector<PlaceProblem> irProblems;
    json bestProblems = json::array();
    double bestScore = -1e18;
    int rounds = 0;
    // Four rounds, and up to six while the last one still got better (a city's many wards).
    bool improving = true;
    for (int round = 0; round < (frozen ? 1 : 4) || (!frozen && round < 6 && improving); ++round) {
        ++rounds;
        const auto roundScale = options.zoneScale;
        const auto t0 = std::chrono::steady_clock::now();
        ir = CompileSpec(ontology, spec_, options);
        const auto t1 = std::chrono::steady_clock::now();
        const SolvedLayout solved = SolveLayout(ontology, ir);
        const auto t2 = std::chrono::steady_clock::now();
        const json rows = VerifyLayout(ontology, brief_, spec_, solved.document);
        const auto t3 = std::chrono::steady_clock::now();
        auto ms = [](auto a, auto b) { return std::lround(std::chrono::duration<double, std::milli>(b - a).count()); };
        const json took = {{"compile", ms(t0, t1)}, {"solve", ms(t1, t2)}, {"verify", ms(t2, t3)}, {"ops", solved.timing}};
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
        Log({{"step", "fit"}, {"round", round + 1}, {"score", score}, {"short", shortZones}, {"scale", grown}, {"ms", took}});
        improving = score > bestScore;
        if (score > bestScore) {
            bestScore = score;
            best = solved.document;
            bestIr = ir;
            bestProblems = solved.problems;
            bestScale = roundScale;
        }
        if (shortZones == 0) break;
        // The next rounds keep this one's site and where its zones are (their seeds):
        // only the short zones grow, the rest of the place stays as it is.
        if (round == 0 && solved.document.contains("grid")) {
            const json b = solved.document["grid"].value("bounds", json());
            if (b.is_array() && b.size() == 4 && b[2].is_number()) {
                options.side = b[2].get<int>();
                options.fixedSide = true;
            }
            for (const auto& r : solved.document.value("regions", json::array())) {
                if (r.value("statement", "") == "site/zones" && r.contains("seed") && !options.keepSeeds.contains(r.value("id", ""))) {
                    options.keepSeeds[r.value("id", "")] = {r["seed"][0].get<int>(), r["seed"][1].get<int>()};
                }
            }
        }
    }
    fitScale_ = bestScale;
    irProblems = ValidateLayout(ontology, bestIr);
    WriteJson(Folder() / "layout.ir.json", bestIr);
    solved_ = best;
    WriteJson(Folder() / "layout.solved.json", solved_);
    // The plan says what the layout is: each zone's area as laid out, the site's side.
    const int side = SiteSide();
    fitSide_ = side;
    const ModuleDef* rootModule = spec_.contains("root") ? ontology.Module(spec_["root"].value("module", "")) : nullptr;
    if (spec_.contains("root") && rootModule && rootModule->Field("area")) {
        spec_["root"]["quantity"]["area"] = {{"area", side * side}};
        for (const auto& r : solved_.value("regions", json::array())) {
            if (r.value("statement", "") != "site/zones") continue;
            if (json* zone = SpecNode(spec_, r.value("id", ""))) (*zone)["quantity"]["area"] = {{"area", r.value("area", 0)}};
        }
    }
    (void)rounds;
    assetNeeds_ = json::array();
    // A revision that asks a zone for more ("10 more houses"): the zone takes the open land
    // beside it first, and the plan is solved again; only what still doesn't fit is cut.
    if (frozen && roomToRevision_ && !growing_ && GrowShortZones()) {
        growing_ = true;
        Solve();
        growing_ = false;
        return;
    }
    if (frozen) FitToRoom();
    RenderMap();
    Log({{"step", "solved"}, {"rounds", rounds}, {"irProblems", ProblemsJson(irProblems)}, {"problems", bestProblems},
         {"stats", solved_.value("stats", json())}});
}

bool PlaceBuild::GrowShortZones() {
    if (!spec_.contains("root")) return false;
    const Ontology& ontology = host_.ontology();
    const int W = std::max(1, SiteSide());
    // Whose each cell is: the zones' land, and the river's and roads' (never given).
    std::vector<int> owner(static_cast<size_t>(W) * W, -1);
    std::map<std::string, std::vector<int>> land;
    for (const auto& r : solved_.value("regions", json::array())) {
        if (r.value("statement", "") != "site/zones") continue;
        auto& cells = land[r.value("id", "")];
        for (int c : DecodeCells(r.value("cells", json::array()), W)) {
            if (c >= 0 && c < W * W) owner[c] = 1, cells.push_back(c);
        }
    }
    for (const auto& path : solved_.value("paths", json::array())) {
        for (const auto& cell : path.value("cells", json::array())) {
            if (cell.is_array() && cell.size() >= 2) {
                const int x = cell[0].get<int>(), y = cell[1].get<int>();
                if (x >= 0 && y >= 0 && x < W && y < W) owner[y * W + x] = 2;
            }
        }
    }
    bool grew = false;
    for (const auto& row : VerifyLayout(ontology, brief_, spec_, solved_)) {
        if (!row.value("zone", false)) continue;
        const int planned = row.value("planned", 0), built = row.value("built", 0);
        if (built >= planned || planned <= 0) continue;
        const std::string id = row.value("id", "");
        json* zone = SpecNode(spec_, id);
        if (zone == nullptr || !land.contains(id)) continue;
        std::set<int> mine(land[id].begin(), land[id].end());
        // What the missing parts need: the zone's land per part it holds, for each one missing.
        const int want = static_cast<int>(std::ceil(1.15 * mine.size() * (planned - built) / std::max(1, built)));
        std::deque<int> frontier(mine.begin(), mine.end());
        int added = 0;
        while (!frontier.empty() && added < want) {
            const int c = frontier.front();
            frontier.pop_front();
            const int x = c % W, y = c / W;
            for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= W || ny >= W) continue;
                const int n = ny * W + nx;
                if (owner[n] != -1 || mine.contains(n)) continue;
                mine.insert(n);
                owner[n] = 1;
                frontier.push_back(n);
                if (++added >= want) break;
            }
        }
        if (added == 0) continue;
        (*zone)["layoutCells"] = EncodeCells(std::vector<int>(mine.begin(), mine.end()), W);
        (*zone)["quantity"]["area"] = {{"area", static_cast<int>(mine.size())}};
        if (!revisions_.empty()) {
            revisions_.back()["notes"].push_back(id + " took " + std::to_string(added) + " cells of the open land beside it, for the " +
                                                 std::to_string(planned - built) + " more it was asked for");
        }
        grew = true;
    }
    if (grew) Log({{"step", "grow zones"}});
    return grew;
}

void PlaceBuild::FitToRoom() {
    // The zones' land is ① Confirm's: a zone asked for more than it holds keeps what the solver
    // fitted (its templates' counts set to what was placed), and the user is told, with where
    // to make it bigger. Nothing is left planned that isn't there.
    const Ontology& ontology = host_.ontology();
    const BuildType::Unit unit = UnitOf(ontology, spec_);
    const bool buildings = unit.cls == "BuildingParcel";
    auto placedOf = [&](const std::string& id) {
        auto under = [&](const std::string& node) { return node == id || node.rfind(id + "#", 0) == 0 || node.rfind(id + "/", 0) == 0; };
        int n = 0;
        if (buildings) {
            for (const auto& o : solved_.value("objects", json::array())) {
                n += o.value("layer", "") == "building" && o.value("primary", false) && under(o.value("node", ""));
            }
        } else {
            for (const auto& r : solved_.value("regions", json::array())) n += ontology.IsA(r.value("module", ""), unit.cls) && under(r.value("node", ""));
        }
        return n;
    };
    std::vector<std::string> notes;
    for (const auto& row : VerifyLayout(ontology, brief_, spec_, solved_)) {
        if (!row.value("zone", false)) continue;
        const int planned = row.value("planned", 0), built = row.value("built", 0);
        if (built >= planned) continue;
        json* zone = SpecNode(spec_, row.value("id", ""));
        if (zone == nullptr) continue;
        json kids = json::array();
        std::map<std::string, std::pair<int, int>> byKind; // planned, placed
        for (auto c : zone->value("children", json::array())) {
            if (ontology.IsA(c.value("module", ""), unit.cls)) {
                const int placed = placedOf(c.value("id", ""));
                byKind[c.value("module", "")].first += static_cast<int>(std::lround(c.value("repeat", 1.0)));
                byKind[c.value("module", "")].second += placed;
                if (placed == 0) continue; // none of it fitted: it isn't planned
                if (placed == 1) c.erase("repeat");
                else c["repeat"] = placed;
            }
            kids.push_back(c);
        }
        (*zone)["children"] = kids;
        if (const ModuleDef* zm = ontology.Module(zone->value("module", ""))) {
            const std::string field = CountField(*zm);
            if (!field.empty()) (*zone)["quantity"][field] = {{"count", built}};
        }
        const ModuleDef* zm = ontology.Module(zone->value("module", ""));
        std::string cut;
        for (const auto& [kind, n] : byKind) {
            if (n.second >= n.first) continue;
            const ModuleDef* km = ontology.Module(kind);
            cut += (cut.empty() ? " (" : ", ") + (km ? km->name : kind) + " " + std::to_string(n.first) + " → " + std::to_string(n.second);
        }
        if (!cut.empty()) cut += ")";
        // The kinds the user set follow what fitted (so the next plan doesn't ask for them again).
        if (zone->contains("kinds")) {
            for (auto& [kind, n] : (*zone)["kinds"].items()) {
                if (byKind.contains(kind) && byKind[kind].second < n.get<int>()) n = byKind[kind].second;
            }
        }
        notes.push_back((zm ? zm->name : row.value("id", "")) + " has room for " + std::to_string(built) + " of the " + std::to_string(planned) + " " +
                        unit.label + " planned; it has " + std::to_string(built) + cut + ". To fit them all, make it bigger in ① Confirm → Layout.");
    }
    if (notes.empty()) return;
    for (const auto& n : notes) planProblems_.push_back({"layout", n, true});
    if (roomToRevision_ && !revisions_.empty()) {
        std::string list;
        for (const auto& n : notes) list += " " + n;
        revisions_.back()["summary"] = revisions_.back().value("summary", "") + list;
    }
    roomToRevision_ = false;
    WriteJson(Folder() / "spec.json", spec_);
    Log({{"step", "fit to room"}, {"notes", notes}});
}

void PlaceBuild::RenderMap() {
    // What it's counted in: buildings, or a dungeon's rooms, a region's sites.
    const BuildType::Unit unit = UnitOf(host_.ontology(), spec_);
    int buildings = 0;
    if (unit.cls == "BuildingParcel") {
        for (const auto& o : solved_.value("objects", json::array())) buildings += o.value("layer", "") == "building";
    } else {
        for (const auto& r : solved_.value("regions", json::array())) buildings += host_.ontology().IsA(r.value("module", ""), unit.cls);
    }
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
    image.title = brief_.value("name", id_) + ": " + std::to_string(buildings) + " " + unit.label + " in " + std::to_string(zones) + " zones";
    WritePng(Folder() / "layout.png", RenderLayoutImage(solved_, image));
    ++imageVersion_;
    status_ = "Laid out " + std::to_string(buildings) + " " + unit.label + (failed ? " (" + std::to_string(failed) + " check(s) to look at)" : "") +
              ". Build puts it into the scene.";
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
    json body = {{"brief", brief_}, {"plan", input["plan"]}, {"result", input["result"]},
                 {"context", RevisionContext(host_.ontology(), brief_, spec_)}};
    if (solved_.is_object()) {
        // Where everything is now: what a request about place is checked against.
        std::map<std::string, std::string> names;
        for (const auto& c : spec_["root"].value("children", json::array())) {
            if (const ModuleDef* m = host_.ontology().Module(c.value("module", ""))) names[c.value("id", "")] = m->name;
        }
        body["where"] = LayoutWhere(solved_, names);
        // Each zone's land (fixed in the plan) and about how much it holds: what a request for
        // more buildings is answered with.
        if (LayoutFrozen()) {
            const Ontology& ontology = host_.ontology();
            json room = json::array();
            for (const auto& row : VerifyLayout(ontology, brief_, spec_, solved_)) {
                if (!row.value("zone", false)) continue;
                json probe = spec_;
                const json* zone = SpecNode(probe, row.value("id", ""));
                if (zone == nullptr) continue;
                int cells = 0;
                for (const auto& r : solved_.value("regions", json::array())) {
                    if (r.value("id", "") == row.value("id", "") && r.value("statement", "") == "site/zones") cells = r.value("area", 0);
                }
                const int planned = row.value("planned", 0), built = row.value("built", 0);
                const int need = ZoneNeed(ontology, *zone);
                const int holds = need > 0 && planned > 0 ? std::max(built, static_cast<int>(std::floor(cells * static_cast<double>(planned) / need))) : built;
                room.push_back({{"zone", row.value("id", "")}, {"cells", cells}, {"has", built}, {"holds about", holds}});
            }
            body["room"] = room;
        }
    }
    return body;
}

std::string PlaceBuild::Revise(const std::string& request) {
    if (stage_ != "plan" && stage_ != "planned" && stage_ != "built" && stage_ != "layout") return "there's no plan to revise yet";
    if (request.find_first_not_of(" \t\r\n") == std::string::npos) return "say what to change";
    // Two zones changing places (place-layout.md §10): said in the request's own words, run
    // exactly, with no AI: where each goes is where the other is now, which relations can't say.
    if (stage_ == "layout") {
        if (const auto [a, b] = SwapAsked(request); !a.empty()) {
            beforeRevision_ = stage_;
            Pending what{"revise", -1, request, "", json::object()};
            auto name = [](std::string id) {
                std::replace(id.begin(), id.end(), '-', ' ');
                if (!id.empty()) id[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(id[0])));
                return id;
            };
            const json answer = {{"summary", name(a) + " and " + name(b) + " change places."},
                                 {"changes", json::array({{{"op", "swap"}, {"a", a}, {"b", b}}})}};
            Log({{"step", "revise"}, {"request", request}, {"program", answer["changes"]}, {"by", "the request's words"}});
            OnRevision(what, {{"ok", true}, {"answer", answer}});
            Save();
            return {};
        }
    }
    json body = RevisionBody();
    body["instruction"] = request;
    if (stage_ == "layout") body["stage"] = "layout";
    else if (LayoutFrozen()) body["stage"] = "plan"; // the zones are set: only what's inside them changes
    if (!Send({"revise", -1, request, "/places/revise-plan", body, 600})) return "the AI is off, and revising by request needs it";
    beforeRevision_ = stage_;
    stage_ = "revising";
    status_ = std::string(beforeRevision_ == "layout" ? "Revising the layout" : "Revising the plan") + ": “" + request + "”…";
    Log({{"step", "revise"}, {"request", request}});
    Save();
    return {};
}

std::pair<std::string, std::string> PlaceBuild::SwapAsked(const std::string& request) const {
    std::string t = request;
    for (auto& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    // The zones it names, by where they're first named.
    std::vector<std::pair<size_t, std::string>> named;
    const Ontology& ontology = host_.ontology();
    if (!spec_.contains("root")) return {};
    for (const auto& c : spec_["root"].value("children", json::array())) {
        const std::string id = c.value("id", "");
        const ModuleDef* m = ontology.Module(c.value("module", ""));
        if (m == nullptr || m->level != PlaceLevel::Zone || !Mentions(request, id)) continue;
        std::string spaced = id;
        std::replace(spaced.begin(), spaced.end(), '-', ' ');
        size_t at = t.find(spaced);
        if (at == std::string::npos) at = t.find(id);
        named.push_back({at == std::string::npos ? t.size() : at, id});
    }
    if (named.size() != 2) return {};
    std::sort(named.begin(), named.end());
    const auto& [atA, a] = named[0];
    const auto& [atB, b] = named[1];
    for (const char* w : {"swap", "exchange", "switch", "change places", "trade places", "change place", "each other's place",
                          "places with", "place of each other"}) {
        if (t.find(w) != std::string::npos) return {a, b};
    }
    // "Move A to where B is": A named before a "where", B after it (and, both ways, the other
    // way round too). A "where" with no zone after it ("where the river bends") isn't one.
    const size_t where = t.find("where");
    if (where != std::string::npos && atA < where && atB > where) return {a, b};
    return {};
}

std::set<std::string> PlaceBuild::RunExactOps(const json& ops, std::set<std::string>& moving, json& notes) {
    std::set<std::string> placed;
    if (!spec_.contains("root")) return placed;
    const Ontology& ontology = host_.ontology();
    const int W = std::max(1, SiteSide());
    // A zone's land now: what the layout gave it.
    auto landOf = [&](const std::string& id) {
        std::set<int> out;
        for (const auto& r : solved_.value("regions", json::array())) {
            if (r.value("statement", "") != "site/zones" || r.value("id", "") != id) continue;
            for (int c : DecodeCells(r.value("cells", json::array()), W)) out.insert(c);
        }
        return out;
    };
    auto zone = [&](const std::string& id) -> json* {
        json* node = SpecNode(spec_, id);
        const ModuleDef* m = node ? ontology.Module(node->value("module", "")) : nullptr;
        return m && m->level == PlaceLevel::Zone ? node : nullptr;
    };
    // Its land, held exactly (as painted land is).
    auto give = [&](json& node, const std::set<int>& land) {
        node.erase("layoutCells");
        node.erase("at");
        if (land.empty()) {
            node.erase("paint");
            return;
        }
        node["paint"] = EncodeCells(std::vector<int>(land.begin(), land.end()), W);
        node["quantity"]["area"] = {{"area", static_cast<int>(land.size())}};
    };
    // Where one stood, the other stands: their relations and placement go with the place (one's
    // relation to the other turned round).
    auto tradeRelations = [&](json& x, json& y, const std::string& a, const std::string& b) {
        auto turned = [&](json rel, const std::string& self, const std::string& other) {
            for (auto& r : rel) {
                if (r.value("to", "") == self) r["to"] = other;
            }
            return rel;
        };
        const json relX = x.value("rel", json::array()), relY = y.value("rel", json::array());
        x["rel"] = turned(relY, a, b);
        y["rel"] = turned(relX, b, a);
        const json px = x.value("placement", json()), py = y.value("placement", json());
        if (py.is_null()) x.erase("placement"); else x["placement"] = py;
        if (px.is_null()) y.erase("placement"); else y["placement"] = px;
    };
    for (const auto& op : ops) {
        const std::string kind = op.value("op", "");
        if (kind == "swap" || kind == "occupy") {
            const std::string a = kind == "swap" ? op.value("a", "") : op.value("zone", "");
            const std::string b = kind == "swap" ? op.value("b", "") : op.value("where", json::object()).value("zone", "");
            json* x = zone(a);
            json* y = zone(b);
            if (x == nullptr || y == nullptr || a == b) {
                notes.push_back(kind + ": " + a + " / " + b + " isn't two zones of the place");
                continue;
            }
            const std::set<int> landA = landOf(a), landB = landOf(b);
            give(*x, landB);
            placed.insert(a);
            moving.erase(a);
            if (kind == "swap" || op.value("then", "swap") == "swap") {
                give(*y, landA);
                tradeRelations(*x, *y, a, b);
                placed.insert(b);
                moving.erase(b);
                notes.push_back(a + " and " + b + " changed places, each on the other's land exactly");
            } else {
                y->erase("paint");
                y->erase("layoutCells");
                y->erase("at");
                moving.insert(b); // placed again, by its relations
                notes.push_back(a + " took " + b + "'s land; " + b + " was placed again");
            }
        } else if (kind == "shift") {
            const std::string id = op.value("zone", "");
            json* x = zone(id);
            const int dx = op.value("dx", 0), dy = op.value("dy", 0);
            if (x == nullptr || (dx == 0 && dy == 0)) continue;
            std::set<int> land;
            for (int c : landOf(id)) {
                const int nx = c % W + dx, ny = c / W + dy;
                if (nx >= 0 && ny >= 0 && nx < W && ny < W) land.insert(ny * W + nx);
            }
            give(*x, land);
            // The zones under it give way; the land it leaves is open.
            for (auto& c : spec_["root"]["children"]) {
                if (c.value("id", "") == id) continue;
                for (const char* key : {"paint", "layoutCells"}) {
                    if (!c.contains(key)) continue;
                    std::vector<int> theirs;
                    for (int cell : DecodeCells(c[key], W)) {
                        if (!land.contains(cell)) theirs.push_back(cell);
                    }
                    c[key] = EncodeCells(theirs, W);
                }
            }
            placed.insert(id);
            moving.erase(id);
            notes.push_back(id + " moved " + std::to_string(std::abs(dx)) + (dx >= 0 ? " east" : " west") + ", " +
                            std::to_string(std::abs(dy)) + (dy >= 0 ? " south" : " north") + " (cells)");
        }
    }
    return placed;
}

std::string PlaceBuild::CheckLayout() {
    // At ② Layout; or, built without that step (the Planner's), at ③ Plan before it's approved.
    if (stage_ != "layout" && stage_ != "plan") return "there's no layout to check";
    if (!solved_.is_object() || !solved_.contains("regions")) {
        layoutChecked_ = true;
        return "there's no layout to check";
    }
    if (!Send({"inspect-layout", -1, "", "/places/inspect", InspectLayoutRequest(*this, &host_.ontology()), 600})) {
        layoutChecked_ = true; // without the AI, the plan goes on unchecked
        return "the AI is off";
    }
    beforeRevision_ = stage_;
    stage_ = "revising";
    status_ = "The AI is checking the layout: each zone, where it is, and what's next to it…";
    Log({{"step", "check layout"}});
    Save();
    return {};
}

std::string PlaceBuild::Review() {
    if ((stage_ != "planned" && stage_ != "built") || !fs::exists(Folder() / "layout.png")) return "there's no map to review yet";
    json body = RevisionBody();
    // The map with every kind of thing in its own colour and code (docs/build-fixes.md): the AI names
    // each kind and judges where it stands, alone and beside the others, against the look and story.
    const json coded = InspectPlanRequest(*this, &host_.ontology());
    body["image"] = coded["images"][0];
    if (!body["context"].is_object()) body["context"] = json::object();
    body["context"]["map"] = {{"legend", coded["expected"]["legend"]}, {"items", coded["expected"]["items"]}};
    body["context"]["look"] = coded["look"];
    if (story_.is_object() && !story_.empty()) body["context"]["story"] = story_;
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
    struct Unfreeze { // the layout check's revision is over, whichever way it ends
        bool& flag;
        ~Unfreeze() { flag = false; }
    } unfreeze{layoutCheckRevising_};
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
    if (answer.contains("now") && answer["now"].is_string()) entry["now"] = answer["now"]; // where it read things are
    const json changes = answer.value("changes", json::array());
    const json briefChanges = answer.value("brief", json::object());
    // A request about "the others", "the rest" or "all" is about every zone.
    const bool aboutAll = [&] {
        std::string t = what.node;
        for (auto& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        for (const char* w : {"other", "the rest", "all zone", "all the zone", "every", "everything", "each zone", "all of them", "remaining"}) {
            if (t.find(w) != std::string::npos) return true;
        }
        return false;
    }();
    // The spatial facts asked for ("the farmland west of the ward"): the AI's,
    // and the request's own direction words. They're checked on the layout.
    expect_ = json::array();
    {
        std::set<std::string> seen;
        auto expectOne = [&](const std::string& node, const std::string& type, const std::string& to) {
            if (node.empty() || to.empty() || type.empty() || !seen.insert(node + "|" + type + "|" + to).second) return;
            if (!host_.ontology().Relation(type)) return; // a relation the layout doesn't know can't be met or checked
            json probe = spec_;
            if (!SpecNode(probe, node)) return;
            expect_.push_back({{"node", node}, {"type", type}, {"to", to}});
        };
        json facts = json::array();
        for (const auto& e : answer.value("expect", json::array())) {
            if (!e.is_object()) continue;
            // A fact the layout can check names a node, a relation and another node, all by words; one
            // that doesn't ("to": 6, a room count) is no relation (and would throw where it's read).
            bool words = true;
            for (const char* key : {"node", "type", "to"}) words = words && (!e.contains(key) || e[key].is_string());
            if (!words) continue;
            for (auto one : NormalizeRelations(json::array({e}))) {
                one["node"] = e.value("node", "");
                facts.push_back(one);
            }
        }
        for (const auto& e : facts) {
            if (!e.is_object()) continue;
            const std::string node = e.value("node", "");
            if (!review && !aboutAll && !IsSubject(what.node, node)) continue;
            expectOne(node, e.value("type", ""), e.value("to", ""));
        }
        if (!review) {
            for (const auto& r : solved_.value("regions", json::array())) {
                const std::string zone = r.value("id", "");
                if (r.value("statement", "") != "site/zones" || !IsSubject(what.node, zone)) continue;
                for (const auto& [type, to] : DirectionsIn(what.node, zone)) expectOne(zone, type, to);
            }
        }
    }
    const json already = expect_.empty() ? json::array() : CheckExpectations(solved_, expect_);
    bool allHold = true;
    for (const auto& c : already) allHold = allHold && c.value("ok", false);
    if (changes.empty() && briefChanges.empty() && allHold) {
        if (entry["summary"] == "") entry["summary"] = review ? "The map looks right." : "Nothing to change.";
        if (!already.empty()) entry["checks"] = already;
        revisions_.push_back(entry);
        stage_ = beforeRevision_;
        status_.clear();
        expect_ = json();
        return;
    }
    // In the plan, the zones lie where ① Confirm's layout put them: a revision (or the review)
    // changes what's inside them, never their borders, their place, or which zones there are.
    json changesKept = changes;
    json briefKept = briefChanges;
    if (beforeRevision_ != "layout" && LayoutFrozen() && !layoutCheckRevising_) {
        bool dropped = false;
        auto isZone = [&](const std::string& id) {
            if (id.empty() || id.find('/') != std::string::npos) return false;
            json probe = spec_;
            const json* node = SpecNode(probe, id);
            const ModuleDef* m = node ? ontology.Module(node->value("module", "")) : nullptr;
            return m != nullptr && m->level == PlaceLevel::Zone;
        };
        changesKept = json::array();
        for (auto change : changes) {
            const std::string op = change.value("op", "");
            const std::string id = change.contains("node") && change["node"].is_string() ? change["node"].get<std::string>() : "";
            if (op == "add") {
                const ModuleDef* m = ontology.Module(change.value("node", json::object()).is_object() ? change["node"].value("module", "") : "");
                if (m && m->level == PlaceLevel::Zone) {
                    dropped = true;
                    continue;
                }
            } else if ((op == "move" || op == "remove") && isZone(id)) {
                dropped = true;
                continue;
            } else if ((op == "set" || op == "zone") && isZone(id)) {
                dropped = dropped || change.contains("rel") || change.value("quantity", json::object()).contains("area");
                change.erase("rel");
                change.erase("placement");
                if (change.contains("quantity") && change["quantity"].is_object()) change["quantity"].erase("area");
            }
            changesKept.push_back(change);
        }
        briefKept = json::object();
        for (const auto& [path, value] : briefChanges.items()) {
            const bool zones = path.rfind("scale.class", 0) == 0 || path.rfind("scale.area", 0) == 0 ||
                               (path.rfind("components.", 0) == 0 && isZone(path.substr(11, path.find('.', 11) - 11)) &&
                                (path.ends_with(".enabled") || path.ends_with(".count")));
            if (zones) {
                dropped = true;
                continue;
            }
            briefKept[path] = value;
        }
        json facts = json::array();
        for (const auto& e : expect_) {
            if (isZone(e.value("node", ""))) dropped = true;
            else facts.push_back(e);
        }
        expect_ = facts;
        if (dropped) {
            entry["summary"] = entry.value("summary", "") +
                               " Where the zones lie is set in ① Confirm → Layout: the plan only fills them, so that part was left as it is.";
        }
        if (changesKept.empty() && briefKept.empty() && expect_.empty()) {
            revisions_.push_back(entry);
            stage_ = beforeRevision_;
            status_.clear();
            expect_ = json();
            return;
        }
    }
    // What it was, so Undo can go back.
    const int n = static_cast<int>(revisions_.size()) + 1;
    WriteJson(Folder() / "revisions" / (std::to_string(n) + ".json"),
              {{"brief", brief_}, {"spec", spec_}, {"stage", beforeRevision_}, {"solved", solved_}});
    entry["undo"] = n;
    entry["time"] = Now();
    entry["stage"] = beforeRevision_;
    ForgetRedo();
    json notes = json::array();
    // The Brief's totals, if the change needs them; kept only if the Brief still follows the rules.
    if (!briefKept.empty()) {
        json changed = brief_;
        for (const auto& [path, value] : briefKept.items()) SetBriefValue(changed, path, value);
        changed = CompleteBrief(ontology, changed, Defaults());
        const auto errors = ErrorsOf(ValidateBrief(ontology, changed));
        if (errors.empty()) {
            brief_ = changed;
            WriteJson(Folder() / "brief.json", brief_);
        } else {
            notes.push_back("Kept the Brief as it was: " + errors.front().where + " " + errors.front().message);
        }
    }
    // The revision language's exact ops (swap, occupy, shift: place-layout.md §10) are run on
    // the land as it is; the rest (move, set, zone, add, remove) as before.
    json exactOps = json::array();
    {
        json rest = json::array();
        for (const auto& c : changesKept) {
            const std::string op = c.value("op", "");
            (op == "swap" || op == "occupy" || op == "shift" ? exactOps : rest).push_back(c);
        }
        changesKept = rest;
    }
    // A request about one part changes that part: re-planning or removing a
    // zone it doesn't name is left out (the AI review may touch any). A request
    // about "the others", "the rest" or "all" is about every zone.
    json kept = json::array();
    std::set<std::string> moving;
    for (const auto& change : changesKept) {
        const std::string op = change.value("op", "");
        std::string target = change.contains("node") && change["node"].is_string() ? change["node"].get<std::string>()
                             : change.contains("node") && change["node"].is_object() ? change["node"].value("id", "")
                                                                                       : "";
        const std::string zone = target.substr(0, target.find('/'));
        // Adding a part is what a request does; anything else is for the parts it names.
        if (!review && !aboutAll && op != "add" && !Mentions(what.node, zone)) {
            notes.push_back(zone + ": left as it was; the request didn't ask about it");
            continue;
        }
        // Named only as where another goes ("between the temple ward and the docks"): it stays
        // where it is; the AI's new relations for it are left out.
        if (!review && !aboutAll && target == zone && (op == "move" || op == "set" || op == "zone") && !IsSubject(what.node, zone)) {
            json c = change;
            const bool placed = op == "move" || c.contains("rel");
            c.erase("rel");
            c.erase("placement");
            if (placed) notes.push_back(zone + ": kept where it is; the request names it only as where another part goes");
            bool more = false;
            for (const char* key : {"quantity", "quality", "children", "repeat"}) more = more || (c.contains(key) && !c[key].empty());
            if (op == "move" || !more) continue;
            kept.push_back(c);
            continue;
        }
        kept.push_back(change);
        moving.insert(zone);
    }
    // The zones it names that it doesn't move: where the moved ones go, or what it says stays.
    anchored_.clear();
    if (!review) {
        for (const auto& c : spec_["root"].value("children", json::array())) {
            const std::string id = c.value("id", "");
            if (!moving.contains(id) && Mentions(what.node, id) && !IsSubject(what.node, id)) anchored_.insert(id);
        }
    }
    // Where a moved zone goes is what the request says (place-layout.md §8.3): a relation to a
    // part the request doesn't name is the AI's own (the docks, a road), and pulls the zone away
    // from what was asked; it goes. A part of the map ("in the west") only when a direction is
    // said. What follows from the named references is composed below.
    if (!review && !aboutAll) {
        std::string said = what.node;
        for (auto& ch : said) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        bool direction = false;
        for (const char* w : {"north", "south", "east", "west", "left", "right", "top", "bottom", "above", "below", "upper", "lower"}) {
            direction = direction || said.find(w) != std::string::npos;
        }
        auto named = [&](const std::string& to) { return to == "site" ? direction : Mentions(what.node, to); };
        for (auto& c : kept) {
            if (!c.contains("rel") || !c["rel"].is_array()) continue;
            json rel = json::array();
            for (const auto& r : c["rel"]) {
                if (named(r.value("to", ""))) rel.push_back(r);
                else notes.push_back(c.value("node", json("")).is_string() ? c["node"].get<std::string>() + ": left out " + r.value("type", "") + " " +
                                                                                 r.value("to", "") + " (the request doesn't say it)"
                                                                           : std::string());
            }
            c["rel"] = rel;
        }
        json facts = json::array();
        for (const auto& e : expect_) {
            if (named(e.value("to", ""))) facts.push_back(e);
        }
        expect_ = facts;
        json clean = json::array();
        for (const auto& n : notes) {
            if (!n.is_string() || !n.get<std::string>().empty()) clean.push_back(n);
        }
        notes = clean;
    }
    if (!exactOps.empty()) {
        if (beforeRevision_ == "layout") {
            const std::set<std::string> placed = RunExactOps(exactOps, moving, notes);
            // Placed exactly: their relations' checks don't apply (they stand where they were put).
            json facts = json::array();
            for (const auto& e : expect_) {
                if (!placed.contains(e.value("node", ""))) facts.push_back(e);
            }
            expect_ = facts;
            for (const auto& id : placed) anchored_.erase(id);
        } else {
            notes.push_back("Where the zones lie is changed in ① Confirm → Layout, so the swap or move was left out.");
        }
    }
    // A small edit (parts taken out of a zone) edits the layout itself: the
    // rest stays exactly as it was, nothing is laid out again.
    if (const auto removals = LocalRemovals(kept); !removals.empty() && briefKept.empty() && (expect_.empty() || allHold)) {
        json removes = json::array();
        for (const auto& id : removals) removes.push_back({{"op", "remove"}, {"node", id}});
        for (const auto& p : ApplyRevision(ontology, brief_, spec_, removes)) notes.push_back(p.where + ": " + p.message);
        RebalanceBuildings(ontology, brief_, spec_, false); // the counts, nothing re-balanced
        const int gone = RemoveFromLayout(solved_, removals);
        std::string list;
        for (const auto& id : removals) list += (list.empty() ? "" : ", ") + id;
        notes.push_back("Took out " + list + " (" + std::to_string(gone) + " things); the rest of the layout is as it was.");
        WriteJson(Folder() / "spec.json", spec_);
        WriteJson(Folder() / "layout.solved.json", solved_);
        RenderMap();
        entry["notes"] = notes;
        entry["local"] = true;
        revisions_.push_back(entry);
        planProblems_.clear();
        for (const auto& p : ValidateSpec(ontology, spec_, SpecStage::Final, brief_.value("buildType", ""))) {
            planProblems_.push_back({p.where, p.warning ? p.message : "kept as planned, though " + p.message, true});
        }
        Log({{"step", "local edit"}, {"removed", removals}, {"things", gone}});
        stage_ = beforeRevision_ == "built" ? "planned" : beforeRevision_;
        expect_ = json();
        return;
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
            }
        }
    }
    {
        std::set<std::string> moved;
        for (const auto& c : kept) {
            if (c.value("op", "") == "move" && c.contains("node") && c["node"].is_string()) moved.insert(c["node"].get<std::string>());
        }
        for (auto& c : kept) {
            const std::string op = c.value("op", "");
            if ((op == "set" || op == "zone") && c.contains("node") && c["node"].is_string() && moved.contains(c["node"].get<std::string>())) {
                c.erase("rel");
            }
        }
    }
    for (const auto& p : ApplyRevision(ontology, brief_, spec_, kept)) notes.push_back(p.where + ": " + p.message);
    // Each fact is the request's: a relation of its zone, so the first layout tries it too.
    for (const auto& e : expect_) {
        json* node = SpecNode(spec_, e.value("node", ""));
        if (node == nullptr) continue;
        if (!node->contains("rel") || !(*node)["rel"].is_array()) (*node)["rel"] = json::array();
        bool has = false;
        for (const auto& r : (*node)["rel"]) has = has || (r.value("type", "") == e.value("type", "") && r.value("to", "") == e.value("to", ""));
        if (!has) (*node)["rel"].push_back({{"type", e["type"]}, {"to", e["to"]}});
        moving.insert(e.value("node", ""));
    }
    // At the layout: each moved zone's place composed from its references (§8.3): on the axes its
    // relations don't move along, where its references are (their bank, their side of the road,
    // their part of the map). Added to its relations and to the facts checked; what contradicts
    // them (the AI's own guess of a bank) goes.
    if (beforeRevision_ == "layout" && !review) {
        for (const auto& id : moving) {
            json* node = SpecNode(spec_, id);
            if (node == nullptr || !node->contains("rel")) continue;
            const json composed = ComposePlace(id, (*node)["rel"], what.node, moving);
            auto same = [](const json& a, const json& b) { return a.value("type", "") == b.value("type", "") && a.value("to", "") == b.value("to", ""); };
            json rel = json::array();
            for (const auto& r : (*node)["rel"]) {
                bool dropped = false;
                for (const auto& d : composed["drop"]) dropped = dropped || same(r, d);
                if (!dropped) rel.push_back(r);
            }
            for (const auto& a : composed["add"]) {
                if (std::none_of(rel.begin(), rel.end(), [&](const json& r) { return same(r, a); })) rel.push_back(a);
            }
            (*node)["rel"] = rel;
            json facts = json::array();
            for (const auto& e : expect_) {
                bool dropped = false;
                for (const auto& d : composed["drop"]) dropped = dropped || (e.value("node", "") == id && same(e, d));
                if (!dropped) facts.push_back(e);
            }
            for (const auto& a : composed["add"]) {
                json fact = a;
                fact["node"] = id;
                if (std::none_of(facts.begin(), facts.end(), [&](const json& e) { return e.value("node", "") == id && same(e, fact); })) facts.push_back(fact);
            }
            expect_ = facts;
            for (const auto& why : composed["why"]) notes.push_back(why);
        }
    }
    // The zones it changed or moves are placed anew; the others keep their place.
    keepSeeds_.clear();
    for (const auto& r : solved_.value("regions", json::array())) {
        if (r.value("statement", "") != "site/zones" || !r.contains("seed") || moving.contains(r.value("id", ""))) continue;
        keepSeeds_[r.value("id", "")] = {r["seed"][0].get<int>(), r["seed"][1].get<int>()};
    }
    revisionKeep_ = keepSeeds_;
    entry["notes"] = notes;
    revisions_.push_back(entry);
    // At the layout: the zones alone, laid out again.
    if (beforeRevision_ == "layout") {
        for (auto& note : ResolveSpec(ontology, brief_, spec_)) tidied_.push_back(std::move(note));
        WriteJson(Folder() / "brief.json", brief_);
        RelayoutMoving(moving);
        MeetExpectations();
        return;
    }
    // The rest as after planning: the expansion, the fixes, the checks; then laid out again.
    solveAfter_ = beforeRevision_ == "planned" || beforeRevision_ == "built";
    roomToRevision_ = true; // what doesn't fit is said in this revision's summary
    finalRepairAsked_ = false;
    tidied_.clear();
    stage_ = "planning";
    planPhase_ = "final";
    status_ = "Applying the changes…";
    FinishPlanning();
}

std::set<std::string> PlaceBuild::LocalRemovals(const json& changes) const {
    std::set<std::string> out;
    if (changes.empty()) return out;
    const Ontology& ontology = host_.ontology();
    json spec = spec_;
    for (const auto& change : changes) {
        const std::string op = change.value("op", "");
        const std::string id = change.contains("node") && change["node"].is_string() ? change["node"].get<std::string>() : "";
        const json* node = SpecNode(spec, id);
        if (node == nullptr) return {};
        const ModuleDef* m = ontology.Module(node->value("module", ""));
        if (m == nullptr) return {};
        if (op == "remove") {
            // A part of a zone (a parcel, a garden); a zone or a network goes through the full layout.
            if (m->level == PlaceLevel::Zone || m->level == PlaceLevel::Network || m->level == PlaceLevel::Site ||
                id.find('/') == std::string::npos) {
                return {};
            }
            out.insert(id);
        } else if (op == "zone") {
            // A zone re-planned with the same parcels but some: those are taken out.
            if (change.contains("rel") || (change.contains("quantity") && !change["quantity"].empty() &&
                                           change["quantity"] != node->value("quantity", json::object()))) {
                return {};
            }
            std::map<std::string, const json*> before;
            const json& children = node->contains("children") ? (*node)["children"] : json::array(); // pointers into it below
            if (!children.is_array()) return {};
            for (const auto& c : children) before[c.value("id", "")] = &c;
            std::set<std::string> after;
            for (const auto& c : change.value("children", json::array())) {
                std::string cid = c.value("id", "");
                if (cid.rfind(id + "/", 0) != 0) cid = id + "/" + cid;
                auto it = before.find(cid);
                if (it == before.end() || it->second->value("module", "") != c.value("module", "")) return {};
                const json r0 = it->second->value("repeat", json(1)), r1 = c.value("repeat", json(1));
                if (r0 != r1) return {};
                after.insert(cid);
            }
            for (const auto& [cid, c] : before) {
                const ModuleDef* cm = ontology.Module(c->value("module", ""));
                // Its streets aren't parcels to take out.
                if (!after.contains(cid) && cm && cm->level != PlaceLevel::Network) out.insert(cid);
            }
            if (out.empty()) return {};
        } else {
            return {};
        }
    }
    return out;
}

void PlaceBuild::MeetExpectations() {
    if (!expect_.is_array() || expect_.empty()) return;
    auto failing = [](const json& checks) {
        json out = json::array();
        for (const auto& c : checks) {
            if (!c.value("ok", false)) out.push_back(c);
        }
        return out;
    };
    json checks = CheckExpectations(solved_, expect_);
    // Not there yet: those relations count for more, and it's laid out again (twice at most).
    for (int round = 0; round < 2 && !failing(checks).empty(); ++round) {
        for (const auto& c : failing(checks)) {
            json* node = SpecNode(spec_, c.value("node", ""));
            if (node == nullptr) continue;
            if (!node->contains("rel") || !(*node)["rel"].is_array()) (*node)["rel"] = json::array();
            bool found = false;
            for (auto& r : (*node)["rel"]) {
                if (r.value("type", "") == c.value("type", "") && r.value("to", "") == c.value("to", "")) {
                    r["weight"] = r.value("weight", 1.0) * 2.5;
                    found = true;
                }
            }
            if (!found) (*node)["rel"].push_back({{"type", c["type"]}, {"to", c["to"]}, {"weight", 2.5}});
        }
        Log({{"step", "expect"}, {"round", round + 1}, {"failing", failing(checks)}});
        keepSeeds_ = revisionKeep_;
        if (stage_ == "layout") {
            std::set<std::string> pulled;
            for (const auto& c : failing(checks)) pulled.insert(c.value("node", ""));
            RelayoutMoving(pulled);
        } else {
            Solve();
        }
        checks = CheckExpectations(solved_, expect_);
    }
    // Still not there at the layout: the land it asks for (the river's bank) is another zone's.
    // Once more, with the zones the request didn't name giving way; kept if it meets more.
    if (stage_ == "layout" && !failing(checks).empty()) {
        const json spec = spec_, solved = solved_;
        const size_t before = failing(checks).size();
        std::set<std::string> pulled;
        for (const auto& c : failing(checks)) pulled.insert(c.value("node", ""));
        keepSeeds_ = revisionKeep_;
        UnfreezeLayout(true, pulled, false);
        // What the request named as where it goes, or as staying, keeps its land.
        for (const auto& id : anchored_) {
            json copy = spec;
            const json* was = SpecNode(copy, id);
            json* node = SpecNode(spec_, id);
            if (was && node && was->contains("layoutCells") && !pulled.contains(id)) (*node)["layoutCells"] = (*was)["layoutCells"];
        }
        LayoutZones();
        const json after = CheckExpectations(solved_, expect_);
        Log({{"step", "expect"}, {"round", "give way"}, {"failing", failing(after)}});
        if (failing(after).size() < before) {
            checks = after;
            if (!revisions_.empty()) revisions_.back()["notes"].push_back("To make room, the zones the request didn't name moved.");
        } else {
            spec_ = spec; // as it was: laid out again from its kept land (the same layout, and its map)
            (void)solved;
            LayoutZones();
        }
    }
    if (!revisions_.empty()) {
        json& entry = revisions_.back();
        entry["checks"] = checks;
        const json missed = failing(checks);
        if (!missed.empty()) {
            std::string list;
            for (const auto& m : missed) list += (list.empty() ? "" : "; ") + m.value("node", "") + " " + m.value("type", "") + " " + m.value("to", "") + " (now: " + m.value("detail", "") + ")";
            entry["summary"] = entry.value("summary", "") + " Not achieved on the layout: " + list + ".";
        }
    }
    Log({{"step", "expect"}, {"checks", checks}});
    expect_ = json();
    revisionKeep_.clear();
    anchored_.clear();
}

// --- History: every action, undone and redone (8.2) ------------------------------------

json PlaceBuild::Snapshot() const {
    return {{"brief", brief_}, {"spec", spec_}, {"stage", stage_}, {"solved", solved_}};
}

long long PlaceBuild::Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void PlaceBuild::ForgetRedo() {
    // A new action after an undo: what was undone can't come back (it stays in the history).
    for (auto& r : revisions_) {
        if (r.value("undone", false)) r.erase("redo");
    }
}

void PlaceBuild::Checkpoint(const std::string& what) {
    // Before an action in ① Confirm or ② Plan: the state as it was, so Undo goes back to it.
    const int n = static_cast<int>(revisions_.size()) + 1;
    WriteJson(Folder() / "revisions" / (std::to_string(n) + ".json"), Snapshot());
    ForgetRedo();
    revisions_.push_back({{"by", "edit"}, {"text", what}, {"undo", n}, {"local", true}, {"time", Now()}, {"stage", stage_}});
}

void PlaceBuild::Restore(const json& saved) {
    brief_ = saved.value("brief", brief_);
    spec_ = saved.value("spec", spec_);
    WriteJson(Folder() / "brief.json", brief_);
    WriteJson(Folder() / "spec.json", spec_);
    planProblems_.clear();
    for (const auto& p : ValidateSpec(host_.ontology(), spec_, SpecStage::Final, brief_.value("buildType", ""))) {
        planProblems_.push_back({p.where, p.warning ? p.message : "kept as planned, though " + p.message, true});
    }
    const json solved = saved.value("solved", json());
    if (solved.is_object() && solved.contains("regions")) {
        // Exactly as it was: the saved layout itself, not solved again.
        solved_ = solved;
        layoutSide_ = SiteSide();
        WriteJson(Folder() / (stage_ == "layout" ? "zones.solved.json" : "layout.solved.json"), solved_);
        RenderMap();
        if (stage_ == "layout") {
            std::error_code ec;
            fs::copy_file(Folder() / "layout.png", Folder() / "zones.png", fs::copy_options::overwrite_existing, ec);
            status_ = "The zones and where they go: revise them here, then plan what's inside them.";
        }
        assetNeeds_ = json::array();
        return;
    }
    keepSeeds_.clear();
    for (const auto& r : solved.is_object() ? solved.value("regions", json::array()) : json::array()) {
        if (r.value("statement", "") == "site/zones" && r.contains("seed")) keepSeeds_[r.value("id", "")] = {r["seed"][0].get<int>(), r["seed"][1].get<int>()};
    }
    if (stage_ == "layout") LayoutZones();
    else Solve();
}

std::string PlaceBuild::Undo() {
    if (stage_ != "plan" && stage_ != "planned" && stage_ != "built" && stage_ != "layout") return "nothing to undo now";
    for (auto it = revisions_.rbegin(); it != revisions_.rend(); ++it) {
        if (!it->contains("undo") || it->value("undone", false)) continue;
        const int n = (*it)["undo"].get<int>();
        const json saved = ReadJson(Folder() / "revisions" / (std::to_string(n) + ".json"));
        if (!saved.is_object()) return "the saved plan is missing";
        // What it is now, so Redo can come back to it.
        WriteJson(Folder() / "revisions" / (std::to_string(n) + "-redo.json"), Snapshot());
        (*it)["undone"] = true;
        (*it)["redo"] = true;
        Log({{"step", "undo"}, {"revision", n}});
        Restore(saved);
        Save();
        return {};
    }
    return "nothing to undo";
}

std::string PlaceBuild::Redo() {
    if (stage_ != "plan" && stage_ != "planned" && stage_ != "built" && stage_ != "layout") return "nothing to redo now";
    // The earliest action undone (undone ones are the history's end, the latest undone last).
    for (auto& r : revisions_) {
        if (!r.value("undone", false) || !r.value("redo", false)) continue;
        const int n = r["undo"].get<int>();
        const json again = ReadJson(Folder() / "revisions" / (std::to_string(n) + "-redo.json"));
        if (!again.is_object()) return "the redone state is missing";
        r["undone"] = false;
        r.erase("redo");
        Log({{"step", "redo"}, {"revision", n}});
        Restore(again);
        Save();
        return {};
    }
    return "nothing to redo";
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

// --- Editing after the build (7.8) -----------------------------------------------------

json PlaceBuild::NodeInfo(const std::string& node) const {
    if (!spec_.contains("root")) return json();
    const Ontology& ontology = host_.ontology();
    // One instance of a template ("ward/cottages#2"): the template's fields.
    const std::string base = node.substr(0, node.find('#'));
    json spec = spec_;
    const json* n = SpecNode(spec, base);
    if (n == nullptr) return json();
    const ModuleDef* m = ontology.Module(n->value("module", ""));
    if (m == nullptr) return json();
    json path = json::array();
    for (size_t at = base.find('/'); at != std::string::npos; at = base.find('/', at + 1)) path.push_back(base.substr(0, at));
    json fields = json::array();
    for (const auto* defs : {&m->quantity, &m->quality}) for (const auto& [fieldName, f] : *defs) {
        const char* group = defs == &m->quantity ? "quantity" : "quality";
        json one = {{"name", fieldName}, {"group", group}, {"type", f.type}, {"value", n->contains(group) ? (*n)[group].value(fieldName, json()) : json()},
                    {"default", f.defaultValue}};
        if (!f.values.empty()) one["values"] = f.values;
        if (!f.vocabulary.empty()) {
            json values = json::array();
            if (const auto* scheme = ontology.Scheme(f.vocabulary)) {
                for (const auto& c : *scheme) values.push_back(c.id);
            }
            one["values"] = values;
        }
        if (f.min) one["min"] = *f.min;
        if (f.max) one["max"] = *f.max;
        if (!f.unit.empty()) one["unit"] = f.unit;
        fields.push_back(one);
    }
    json parts = json::array();
    for (const auto& c : n->value("children", json::array())) {
        parts.push_back({{"id", c.value("id", "")}, {"module", c.value("module", "")}, {"repeat", c.value("repeat", 1)}});
    }
    bool locked = false;
    for (const auto& l : locks_) locked = locked || node == l || node.rfind(l + "/", 0) == 0 || node.rfind(l + "#", 0) == 0;
    return {{"id", node}, {"base", base}, {"module", m->id}, {"name", m->name}, {"level", PlaceLevelName(m->level)},
            {"path", path}, {"fields", fields}, {"parts", parts}, {"rationale", n->value("rationale", "")},
            {"rel", n->value("rel", json::array())}, {"locked", locked}, {"repeat", n->value("repeat", 1)}};
}

void PlaceBuild::SolveParts(const std::set<std::string>& parts, const std::string& why) {
    const Ontology& ontology = host_.ontology();
    // Undo goes back to before this edit.
    const int n = static_cast<int>(revisions_.size()) + 1;
    WriteJson(Folder() / "revisions" / (std::to_string(n) + ".json"), {{"brief", brief_}, {"spec", spec_}, {"stage", stage_}, {"solved", solved_}});
    ForgetRedo();
    ExpandSpec(ontology, brief_, spec_); // a new part's own parts
    RebalanceBuildings(ontology, brief_, spec_, false);
    CompileOptions options;
    options.seed = Fnv1a(id_);
    options.side = SiteSide();
    options.fixedSide = true;
    options.isoGrid = host_.isoGrid ? host_.isoGrid() : true;
    options.zoneScale = fitScale_;
    const json ir = CompileSpec(ontology, spec_, options);
    SolveOptions solve;
    solve.previous = &solved_;
    solve.resolve = parts;
    solve.locked = locks_;
    const json previous = solved_;
    const SolvedLayout layout = SolveLayout(ontology, ir, solve);
    solved_ = layout.document;
    WriteJson(Folder() / "layout.ir.json", ir);
    WriteJson(Folder() / "layout.solved.json", solved_);
    WriteJson(Folder() / "spec.json", spec_);
    RenderMap();
    changed_.insert(parts.begin(), parts.end());
    json list = json::array();
    for (const auto& p : parts) list.push_back(p);
    revisions_.push_back({{"by", "edit"}, {"text", why}, {"summary", "Laid out again: " + list.dump() + "; the rest as it was."}, {"undo", n},
                          {"parts", list}, {"local", true}, {"time", Now()}, {"stage", stage_}});
    Log({{"step", "edit"}, {"why", why}, {"parts", list}, {"problems", layout.problems}});
    if (stage_ == "plan") stage_ = "plan";
    Save();
}

std::string PlaceBuild::EditNode(const std::string& node, const std::string& field, const json& value) {
    if (stage_ != "plan" && stage_ != "planned" && stage_ != "built") return "there's no plan to edit";
    const std::string base = node.substr(0, node.find('#'));
    json* n = SpecNode(spec_, base);
    if (n == nullptr) return "no part " + node;
    const ModuleDef* m = host_.ontology().Module(n->value("module", ""));
    const FieldDef* f = m ? m->Field(field) : nullptr;
    if (f == nullptr) return "a " + (m ? m->name : std::string("part")) + " has no field " + field;
    if (!value.is_null()) {
        if (const std::string error = CheckFieldValue(host_.ontology(), *f, value); !error.empty()) return field + ": " + error;
    }
    const char* group = f->quantity ? "quantity" : "quality";
    if (!n->contains(group)) (*n)[group] = json::object();
    if (value.is_null()) (*n)[group].erase(field);
    else (*n)[group][field] = value;
    SolveParts({base}, base + "." + field + " = " + value.dump());
    return {};
}

std::string PlaceBuild::Regenerate(const std::string& node) {
    if (!solved_.is_object()) return "the plan isn't laid out";
    json spec = spec_;
    if (!SpecNode(spec, node.substr(0, node.find('#')))) return "no part " + node;
    SolveParts({node}, "lay " + node + " out again");
    return {};
}

void PlaceBuild::Lock(const std::string& node, bool locked) {
    if (node.empty()) return;
    if (locked ? locks_.insert(node).second : locks_.erase(node) > 0) {
        Log({{"step", locked ? "lock" : "unlock"}, {"node", node}});
        Save();
    }
}

std::string PlaceBuild::Paint(const std::string& module, int x0, int y0, int x1, int y1) {
    if (!solved_.is_object()) return "the plan isn't laid out";
    const Ontology& ontology = host_.ontology();
    const ModuleDef* m = ontology.Module(module);
    if (m == nullptr) return "no module " + module;
    if (x1 <= x0 || y1 <= y0) return "draw an area";
    // The zone under the area's middle.
    const int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    std::string zone;
    for (const auto& r : solved_.value("regions", json::array())) {
        if (r.value("statement", "") != "site/zones") continue;
        for (int c : DecodeCells(r.value("cells", json::array()), SiteSide())) {
            if (c % SiteSide() == cx && c / SiteSide() == cy) zone = r.value("id", "");
        }
    }
    if (zone.empty()) return "draw it inside a zone";
    json* z = SpecNode(spec_, zone);
    const ModuleDef* zm = z ? ontology.Module(z->value("module", "")) : nullptr;
    if (zm == nullptr || !ontology.ChildRule(zm->id, module)) return "a " + (zm ? zm->name : zone) + " can't hold a " + m->name;
    std::set<std::string> used;
    std::function<void(const json&)> collect = [&](const json& n) {
        used.insert(n.value("id", ""));
        for (const auto& c : n.value("children", json::array())) collect(c);
    };
    collect(spec_["root"]);
    std::string id = zone + "/" + module + "-painted";
    for (int k = 2; used.contains(id); ++k) id = zone + "/" + module + "-painted-" + std::to_string(k);
    json node = {{"id", id}, {"module", module}, {"quantity", json::object()}, {"quality", json::object()},
                 {"rationale", "painted with the module brush"}, {"children", json::array()},
                 {"at", {{"x", x0}, {"y", y0}, {"w", x1 - x0}, {"h", y1 - y0}}}};
    if (!z->contains("children")) (*z)["children"] = json::array();
    (*z)["children"].push_back(node);
    SolveParts({id}, "paint a " + m->name + " in " + zone);
    return {};
}

json PlaceBuild::BrushModules() const {
    json out = json::array();
    std::set<std::string> seen;
    for (const auto& c : spec_.contains("root") ? spec_["root"].value("children", json::array()) : json::array()) {
        const ModuleDef* zm = host_.ontology().Module(c.value("module", ""));
        if (zm == nullptr || zm->level != PlaceLevel::Zone) continue;
        for (const auto& rule : zm->children) {
            const ModuleDef* cm = host_.ontology().Module(rule.module);
            if (cm == nullptr || (cm->level != PlaceLevel::Feature && cm->level != PlaceLevel::Parcel) || !seen.insert(cm->id).second) continue;
            if (host_.ontology().IsA(cm->id, "LinearFeature")) continue;
            out.push_back({{"id", cm->id}, {"name", cm->name}, {"level", PlaceLevelName(cm->level)}});
        }
    }
    return out;
}

std::set<std::string> PlaceBuild::TakeChanged() {
    std::set<std::string> out;
    out.swap(changed_);
    return out;
}

bool PlaceBuild::AboutThisPlace(const std::string& request) const {
    if (!spec_.contains("root") || !solved_.is_object()) return false;
    for (const auto& c : spec_["root"].value("children", json::array())) {
        const ModuleDef* m = host_.ontology().Module(c.value("module", ""));
        if (m && m->level == PlaceLevel::Zone && Mentions(request, c.value("id", ""))) return true;
    }
    return false;
}

// --- Sourcing the art (7.9) ---------------------------------------------------------------

// A need's role, its first alternative ("barrel|pot" is a barrel's).
static std::string FirstRole(const json& need) {
    const std::string role = need.value("role", "");
    return role.substr(0, role.find('|'));
}

std::vector<std::string> PlaceBuild::DependsOn(const json& need) const {
    std::vector<std::string> out;
    for (const auto& r : host_.ontology().RoleLook(FirstRole(need), "dependsOn")) {
        if (r.is_string()) out.push_back(r.get<std::string>());
    }
    return out;
}

json PlaceBuild::GoesWith(const json& need) const {
    // The art of what it's designed against (docs/build-fixes.md): made in this round (passed), made
    // before, or the place's chosen art; each with what it is and the prompt it was drawn from.
    json out = json::array();
    for (const auto& dep : DependsOn(need)) {
        json found;
        auto from = [&](const std::map<std::string, Sourcing>& pieces) {
            for (const auto& [k, s] : pieces) {
                if (FirstRole(s.need) != dep || !found.is_null()) continue;
                for (const auto& m : s.made) {
                    if (m.value("ok", false)) found = {{"what", s.need.value("title", dep)}, {"description", s.need.value("description", "")},
                                                        {"prompt", s.lastPrompt}, {"image", m.value("path", "")}};
                }
            }
        };
        from(sourcing_);
        from(sourced_);
        if (found.is_null()) {
            for (const auto& n : assetNeeds_) {
                const std::string entry = n.value("entry", "");
                if (FirstRole(n) != dep || entry.empty() || n.value("source", "") == "placeholder" || entry.find('.') == std::string::npos) continue;
                found = {{"what", n.value("title", dep)}, {"description", n.value("description", "")}, {"image", entry.substr(0, entry.find('#'))}};
                break;
            }
        }
        if (found.is_object()) out.push_back(found);
    }
    return out;
}

void PlaceBuild::SendWaiting() {
    // What waited for the pieces it's designed against: made now they're done.
    for (auto it = waitingOn_.begin(); it != waitingOn_.end();) {
        bool ready = true;
        for (const auto& dep : DependsOn(sourcing_[*it].need)) {
            for (const auto& [k, s] : sourcing_) ready = ready && (FirstRole(s.need) != dep || s.done);
        }
        if (!ready) {
            ++it;
            continue;
        }
        const std::string key = *it;
        it = waitingOn_.erase(it);
        if (!SendArt(key)) sourcing_[key].done = true;
    }
}

bool PlaceBuild::StartSourcing(const json& pending, const SourcingSettings& settings) {
    sourcing_.clear();
    waitingOn_.clear();
    sourcingDone_ = false;
    bool sent = false;
    std::set<std::string> pendingRoles;
    for (const auto& need : pending) pendingRoles.insert(FirstRole(need));
    for (const auto& need : pending) {
        const std::string key = need.value("key", "");
        if (key.empty()) continue;
        sourcing_[key] = {need};
        if (auto why = redoReasons_.find(key); why != redoReasons_.end()) {
            // Refused in the built scene: its last prompt, rewritten with why.
            sourcing_[key].reasons = why->second;
            if (auto before = sourced_.find(key); before != sourced_.end()) sourcing_[key].lastPrompt = before->second.lastPrompt;
            redoReasons_.erase(why);
        }
        if (need.value("action", "") == "web") {
            std::string folder = "assets/web/";
            for (const char c : need.value("query", key)) folder += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(c)) : '-';
            sent = Send({"web", -1, key, "/assets/web-search",
                         {{"query", need.value("query", "")}, {"folder", folder.substr(0, 60)}, {"maxAssets", 4},
                          {"shareAlike", settings.licences.shareAlike}, {"queries", need.value("queries", json::array())}},
                         300}) || sent;
        } else if (need.value("action", "") == "check") {
            // Found art: the critic looks at it against the scene before it's used.
            json item = {{"id", need.value("entry", "")}, {"path", need.value("entry", "").substr(0, need.value("entry", "").find('#'))}};
            if (need.contains("rect")) item["rect"] = need["rect"];
            sourcing_[key].made.push_back({{"path", item["path"]}, {"attempt", 0}});
            sent = Send({"critic", -1, key, "/assets/describe",
                         {{"paths", json::array()}, {"items", json::array({item})}, {"roles", json::array({need.value("role", "")})},
                          {"wanted", WantedFor(sourcing_[key])}},
                         300}) || sent;
        } else {
            // Designed against a piece still to be made in this round (a door against its wall): after it.
            bool waits = false;
            for (const auto& dep : DependsOn(need)) waits = waits || (dep != FirstRole(need) && pendingRoles.contains(dep));
            if (waits) {
                waitingOn_.insert(key);
                sent = true;
            } else {
                sent = SendArt(key) || sent;
            }
        }
    }
    if (!sent) {
        sourcing_.clear();
        return false;
    }
    stage_ = "sourcing";
    status_ = "Finding and making the art: " + std::to_string(sourcing_.size()) + " kind(s)…";
    Log({{"step", "sourcing"}, {"needs", pending}});
    Save();
    return true;
}

json PlaceBuild::WantedFor(const Sourcing& s) const {
    // What was wanted, so the critic says whether this is it (and if not, why): the thing, its kind,
    // the view, the room and place it's for, and the place's story.
    const bool isoView = !host_.isoGrid || host_.isoGrid();
    json wanted = {{"what", s.need.value("title", s.need.value("role", ""))}, {"description", s.need.value("description", "")},
                   {"kind", s.need.value("kind", "") == "building" ? "a whole building" : s.need.value("layer", "") == "ground" ? "a ground tile" : "one object"},
                   {"view", s.need.value("layer", "") == "ground" ? "flat, from straight above"
                            : s.need.value("kind", "") == "decal" ? "lying flat on the ground, seen from above (top-down or isometric both do)"
                            : isoView ? "isometric" : "top-down"},
                   {"room", s.need.value("about", s.need.value("component", ""))},
                   {"place", brief_.is_object() ? json{{"name", brief_.value("name", "")}, {"type", brief_.value("buildType", "")}, {"style", brief_.value("style", json::object())}} : json::object()}};
    if (story_.is_object() && !story_.empty()) wanted["story"] = {{"purpose", story_.value("purpose", json())}, {"mood", story_.value("mood", json())}};
    return wanted;
}

bool PlaceBuild::SendArt(const std::string& key) {
    Sourcing& s = sourcing_[key];
    const json& need = s.need;
    ++s.attempts;
    // Its file's name: the place, the need and a stamp (docs/build-fixes.md). By the need and the try
    // alone ("tree-prop-1"), the next place's tree, or this place's next round, wrote over the art a
    // built scene already used.
    auto slug = [](const std::string& text, size_t most) {
        std::string out;
        for (const char c : text) out += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(c)) : '-';
        while (out.find("--") != std::string::npos) out.replace(out.find("--"), 2, "-");
        while (!out.empty() && out.front() == '-') out.erase(0, 1);
        out = out.substr(0, most);
        while (!out.empty() && out.back() == '-') out.pop_back();
        return out;
    };
    std::string stamp;
    for (auto t = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch()).count()) % 2176782336ull; // 36^6
         stamp.size() < 6; t /= 36) {
        stamp.insert(stamp.begin(), "0123456789abcdefghijklmnopqrstuvwxyz"[t % 36]);
    }
    const std::string place = slug(id_, 10);
    std::string name = (place.empty() ? "" : place + "-") + slug(key, 24) + "-" + stamp;
    while (!name.empty() && name.front() == '-') name.erase(0, 1);
    const std::string kind = need.value("layer", "") == "ground" ? "tile" : "object";
    // What to draw, at its size (place-refine.md §2): a role's own words ("a small clump of
    // wildflowers"), at a height in pixels that follows how big it is (a lantern isn't a house).
    const Ontology& ontology = host_.ontology();
    const std::string role = need.value("role", "");
    const bool building = need.value("kind", "") == "building";
    const float size = ontology.RoleSize(role), tall = ontology.RoleTall(role);
    // Twice the pixels a cell's 64 on screen needs (docs/build-fixes.md): close up, a house carries
    // the detail of a hand-painted one (PixelLab draws up to 400).
    int height = building ? std::clamp(192 + 32 * need.value("w", 3), 224, 384) : need.value("kind", "") == "decal" ? 64 : 128;
    if (!building && size > 0) height = std::clamp(static_cast<int>(std::lround(192.0f * size * std::max(0.5f, tall))), 48, 320);
    std::string description = need.value("description", "");
    if (const std::string draw = ontology.RoleDraw(role); !draw.empty() && !building && kind != "tile") {
        std::string concepts;
        for (const auto& c : need.value("concepts", json::array())) concepts += (concepts.empty() ? "" : ", ") + c.get<std::string>();
        // Alone: a thing drawn on its own square of ground looks pasted onto the scene's ground.
        description = draw + (concepts.empty() ? std::string() : " (" + concepts + ")") + " for a game, " +
                      (host_.isoGrid && !host_.isoGrid() ? "top-down" : "isometric") +
                      " view, standing alone with no square of ground, base or plinth under it";
    }
    // One art direction for every piece of the place: the same light, palette and outline.
    description += ". " + ArtDirection();
    if (!s.request.empty()) description += ". The user asks for this change: " + s.request;
    if (!s.reasons.empty()) description += ". Last time it came out wrong: " + s.reasons + "; fix that.";
    json body = {{"name", name}, {"description", description}, {"kind", kind}, {"height", height}, {"style", "pixel"}};
    // The piece's place (place-refine.md §14): the text model first writes what exactly to draw
    // for it there (a dungeon's light is a wall torch, not a street lamp), its view and the way it
    // faces, its proportions; the image model draws that.
    {
        const bool iso = !host_.isoGrid || host_.isoGrid();
        const Role* r = ontology.FindRole(role);
        json around = json::array();
        for (const auto& [k, other] : sourcing_) {
            if (k != key && around.size() < 12) around.push_back(other.need.value("title", other.need.value("role", "")));
        }
        json context = {{"place", {{"name", brief_.value("name", "")}, {"type", brief_.value("buildType", "")},
                                   {"style", brief_.value("style", json::object())}, {"about", BriefValue(brief_, "shape.description")}}},
                        {"role", r ? r->label : role}, {"roleWords", ontology.RoleDraw(role)}, {"concepts", need.value("concepts", json::array())},
                        {"title", need.value("title", "")}, {"partOf", need.value("component", "")}, {"about", need.value("about", "")},
                        {"view", iso ? "isometric" : "top-down"}, {"cells", size > 0 ? size : static_cast<float>(need.value("w", 1))},
                        {"tallForWidth", tall > 0 ? tall : 1.0f}, {"artDirection", ArtDirection()}, {"otherPieces", around}};
        if (const BuildType* type = ontology.Build(brief_.value("buildType", ""))) {
            context["place"]["kind"] = type->name; // a Dungeon, a Village, a Courtyard
            context["place"]["light"] = type->look.is_object() ? type->look.value("lighting", "sun") : "sun";
            if (const ModuleDef* rm = ontology.Module(type->root)) context["place"]["is"] = rm->description;
        }
        const std::string along = iso ? "diagonally, from the upper left down to the lower right (along the isometric grid)" : "from left to right";
        if (ontology.RoleLook(role, "doorway").is_boolean() && ontology.RoleLook(role, "doorway").get<bool>()) {
            context["runs"] = "the wall it stands in runs " + along + "; the door is seen at that angle, set flush in it";
        } else if (!need.value("piece", "").empty()) {
            context["runs"] = "the section runs " + along + "; copies join end to end";
        } else if (!need.value("part", "").empty()) {
            context["runs"] = "a piece of the place's structure, one cell across, standing on its cell";
        }
        if (const std::string style = need.value("style", ""); !style.empty()) context["wished"] = style; // the Brief's words for its look
        if (building) context["whole"] = "building"; // the whole building from outside, not its sign or door
        context["layer"] = need.value("layer", building ? std::string("building") : std::string("prop")); // its kind, for style references
        if (!s.reasons.empty()) context["lastAttemptWrong"] = s.reasons;
        if (!s.lastPrompt.empty() && !s.reasons.empty()) context["previousPrompt"] = s.lastPrompt; // rewritten to fix exactly what went wrong
        // The room it stands in, and what the story needs of the place: so it fits there.
        if (const std::string room = need.value("component", ""); !room.empty()) {
            if (const ModuleDef* m = ontology.Module(room)) context["room"] = {{"is", m->name}, {"about", m->description}};
        }
        if (story_.is_object() && !story_.empty()) {
            context["placeStory"] = {{"purpose", story_.value("purpose", json())}, {"mood", story_.value("mood", json())},
                                     {"logline", story_.value("logline", json())}};
        }
        if (!s.request.empty()) context["userAsks"] = s.request;
        json images = json::array();
        if (const auto crop = sketchCrops_.find(key); crop != sketchCrops_.end()) {
            // The piece as the place's sketch painted it: its look, colours and light are kept; drawn
            // alone, whole, cut out (the crop has its neighbours and the floor in it).
            images.push_back(crop->second);
            context["sketch"] = "the first reference image is this piece cropped from the concept painting of the whole place: keep "
                                "its design, materials, colours and light, and draw it alone, whole and cut out, in the game's view "
                                "(the painting's angle may differ), without the ground or the neighbours the crop shows";
            if (const auto seen = sketchSeen_.find(key); seen != sketchSeen_.end()) context["inTheSketch"] = seen->second;
        }
        if (const json with = GoesWith(need); !with.empty()) {
            context["goesWith"] = with;
            for (const auto& w : with) {
                if (!w.value("image", "").empty()) images.push_back(w["image"]);
            }
        }
        if (!images.empty()) body["withImages"] = images;
        body["context"] = context;
    }
    // Ground covers hundreds of cells: two takes of it, mixed by cell, don't show a repeat.
    // The Brief asks for more than one look for its things: so many takes (each checked).
    if (const int n = need.value("variants", 1); n > 1) {
        json takes = json::array({""});
        for (int k = 1; k < n; ++k) {
            takes.push_back(kind == "tile" ? "another patch of the same kind of ground, clearly different from the first (another crop, another colour or stage), take " + std::to_string(k + 1)
                                           : "another one of the same kind, different in its shape and details (take " + std::to_string(k + 1) + ")");
        }
        body["variants"] = takes;
    }
    if (kind == "tile") {
        // One ground, large (it spans about four cells, so the painted ground repeats rarely),
        // its frame trimmed and its light evened (no grid).
        body["height"] = 256;
        body["evenLight"] = true;
    }
    return Send({"art", -1, key, "/assets/generate", body, 600});
}

std::string PlaceBuild::ArtDirection() const {
    // A hand-made 2D game of the 90s keeps one look across its art: one light, one palette, one
    // outline. Every piece of a build is asked for in the same words (and the Brief's style).
    // Its light: the build type's (place-refine.md §13). Indoors, a dark place lit by its fires.
    const BuildType* type = host_.ontology().Build(brief_.value("buildType", ""));
    const bool indoor = type && type->look.is_object() && type->look.value("lighting", "") == "indoor";
    std::string look = std::string("Art direction shared by every piece of this place: a 16-bit 1990s 2D RPG pixel-art look, ") +
                       (indoor ? "lit from nearby torches and fires in the dark: warm orange highlights, deep cold shadows, dark and muted colours, "
                               : "soft daylight from the upper left, a warm, muted, earthy palette, ") +
                       "gentle shading, a crisp darker outline, readable at small size, no text";
    for (const char* field : {"style.theme", "style.condition", "style.era", "style.climate", "style.mood"}) {
        if (const json v = BriefValue(brief_, field); v.is_string() && !v.get<std::string>().empty()) {
            look += std::string("; ") + (field + 6) + ": " + v.get<std::string>();
        }
    }
    return look;
}

void PlaceBuild::SourcingStep(const Pending& what, const json& body) {
    const std::string key = what.node;
    auto it = sourcing_.find(key);
    if (it == sourcing_.end()) return;
    Sourcing& s = it->second;
    const bool ok = body.value("ok", false);
    if (!ok && what.kind != "critic" && Retry(what, body)) return;
    Log({{"step", "sourcing " + what.kind}, {"need", key}, {"answer", body}});
    if (what.kind == "art" || what.kind == "web") {
        json paths = json::array(), provenance = json::object();
        for (const auto& a : body.value("assets", json::array())) {
            const std::string path = a.value("path", "");
            if (path.empty()) continue;
            paths.push_back(path);
            // The prompt it was drawn from: if it doesn't pass, the next prompt is this one, corrected.
            if (const json meta = a.value("meta", json::object()); meta.is_object() && meta.value("prompt", json()).is_string()) {
                s.lastPrompt = meta["prompt"].get<std::string>();
            }
            provenance[path] = what.kind == "web"
                                   ? json{{"source", "web"}, {"licence", a.value("licence", "")}, {"author", a.value("author", "")},
                                          {"url", a.value("url", "")}, {"retrieved", a.value("retrieved", "")}}
                                   : json{{"source", "generated"}, {"licence", ""}, {"author", "Art tool"}};
        }
        for (const auto& path : paths) s.made.push_back({{"path", path}, {"attempt", s.attempts}});
        if (paths.empty()) {
            s.done = true; // nothing came: the next pass moves on
        } else {
            s.need["provenance"] = provenance;
            // The critic: what the vision model sees in it, against what was asked.
            // What was wanted, so it says whether this is it (and if not, why): the thing, its kind,
            // the view, and the room and place it's for.
            const json wanted = WantedFor(s);
            if (!Send({"critic", -1, key, "/assets/describe", {{"paths", paths}, {"roles", json::array({s.need.value("role", "")})}, {"wanted", wanted}}, 300})) {
                for (const auto& p : paths) newAssets_.push_back({{"path", p}, {"fields", provenance[p.get<std::string>()]}});
                s.done = true;
            }
        }
    } else if (what.kind == "critic") {
        const Ontology& ontology = host_.ontology();
        const std::string role = s.need.value("role", "");
        bool passed = false;
        std::string reasons;
        for (const auto& a : body.value("assets", json::array())) {
            const std::string path = a.value("path", "");
            json fields = s.need.value("provenance", json::object()).value(path, json::object());
            // What it is, for the catalog (as the Assets panel's Analyze writes it).
            std::string tags;
            for (const auto& t : a.value("tags", json::array())) {
                if (t.is_string()) tags += (tags.empty() ? "" : ", ") + t.get<std::string>();
            }
            fields["description"] = a.value("description", "");
            fields["kind"] = a.value("kind", "other");
            fields["tags"] = tags;
            fields["name"] = a.value("name", "");
            fields["roles"] = a.value("role", "");
            const std::string perspective = a.value("perspective", "unknown");
            fields["perspective"] = perspective == "unknown" ? "" : perspective;
            char cells[32];
            std::snprintf(cells, sizeof(cells), "%.2f", a.value("sizeCells", 1.0));
            fields["cells"] = cells;
            fields["blocks"] = a.value("blocks", false) ? "1" : "0";
            // Made for this slot: it takes the slot's layer and size (the model's guesses may differ:
            // a crop read as ground, an inn guessed at 3 cells).
            if (s.need.value("action", "") == "generate") {
                if (const std::string terrain = s.need.value("terrain", ""); !terrain.empty()) fields["terrain"] = terrain;
                if (const std::string layer = s.need.value("layer", ""); !layer.empty()) fields["layer"] = layer;
                // A fence or wall section: found again as its line's piece, a cell long.
                if (const std::string piece = s.need.value("piece", ""); !piece.empty()) fields["pieceSlot"] = s.need.value("pieceSlot", "");
                if (const std::string part = s.need.value("part", ""); !part.empty()) fields["partOf"] = part;
                const float roleSize = ontology.RoleSize(role);
                if (s.need.value("kind", "") != "building" && roleSize > 0) {
                    char across[32];
                    std::snprintf(across, sizeof(across), "%.2f", roleSize);
                    fields["cells"] = across;
                } else if (const int w = s.need.value("w", 0); w > 0) {
                    fields["cells"] = std::to_string(w) + ".00";
                }
                if (s.need.value("kind", "") == "building") fields["blocks"] = "1";
            }
            const bool checking = s.need.value("action", "") == "check";
            if (!checking) newAssets_.push_back({{"path", path}, {"fields", fields}});
            // Does it show what was asked: its role (or a narrower one), or the concept in its words?
            const std::string seen = a.value("role", "");
            // A role of alternatives ("barrel|pot|bench|sign|lamp"): any of them passes.
            std::vector<std::string> wanted;
            for (size_t at = 0; at <= role.size();) {
                const size_t bar = role.find('|', at);
                const std::string one = role.substr(at, bar == std::string::npos ? std::string::npos : bar - at);
                if (!one.empty()) wanted.push_back(one);
                if (bar == std::string::npos) break;
                at = bar + 1;
            }
            auto isWanted = [&](const std::string& r) { return std::find(wanted.begin(), wanted.end(), r) != wanted.end(); };
            bool fills = isWanted(seen);
            for (const Role* r = ontology.FindRole(seen); r && !fills; r = ontology.FindRole(r->broader)) fills = isWanted(r->broader);
            const std::string text = a.value("description", "") + " " + tags + " " + a.value("name", "");
            // A word, not part of one ("inn" isn't in "dinner").
            auto saysWord = [&](const std::string& word) {
                if (word.empty()) return false;
                for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) {
                    const bool before = at == 0 || !std::isalnum(static_cast<unsigned char>(text[at - 1]));
                    const bool after = at + word.size() >= text.size() || !std::isalnum(static_cast<unsigned char>(text[at + word.size()]));
                    if (before && after) return true;
                }
                return false;
            };
            for (const auto& c : s.need.value("concepts", json::array())) {
                if (c.is_string() && saysWord(c.get<std::string>())) fills = true;
            }
            // A ground tile: ground is what it must be.
            if (!s.need.value("terrain", "").empty() && (a.value("kind", "") == "ground" || isWanted("ground-tile"))) {
                fills = fills || a.value("kind", "") == "ground" || seen == "terrain" || seen == "ground-tile";
            }
            // Or what it says it is names one of them (the model's role may be broader: "prop").
            for (const auto& w : wanted) {
                if (saysWord(w) && (seen.empty() || seen == "prop" || seen == "building")) fills = true;
            }
            // A building must be one: an inn's sign, door or gate is no inn.
            if (fills && s.need.value("kind", "") == "building" && a.value("kind", "") != "building") {
                fills = false;
                reasons += (reasons.empty() ? "" : "; ") + std::string("it shows ") + (a.value("description", "").empty() ? "a part" : a.value("description", "")) +
                           ", not the whole building seen from outside";
            }
            // Its view must be the scene's (place-refine.md §4): an isometric scene's buildings and
            // things aren't seen from straight above; its ground is.
            const bool iso = !host_.isoGrid || host_.isoGrid();
            const bool ground = !s.need.value("terrain", "").empty();
            std::string wrongView;
            if (iso && !ground && perspective == "top-down") wrongView = "drawn from straight above, not in the scene's isometric view";
            // What runs along a line (a door in its wall, a fence or wall section) is seen at an angle
            // in an isometric scene: drawn facing the viewer, it can't sit in a diagonal wall.
            const bool runs = !s.need.value("piece", "").empty() || ontology.RoleLook(role, "doorway") == json(true);
            if (iso && runs && (perspective == "front" || perspective == "side" || perspective == "three-quarter" || perspective == "unknown")) {
                wrongView = "drawn facing the viewer, not at an angle along a wall that runs diagonally (isometric)";
            }
            if (ground && (perspective == "isometric" || perspective == "three-quarter")) wrongView = "a ground tile drawn in perspective, not flat from above";
            if (fills && !wrongView.empty()) {
                fills = false;
                reasons += (reasons.empty() ? "" : "; ") + wrongView;
            }
            // A fence or wall section must join end to end: one straight run, along a diagonal
            // of the isometric grid (or across, top-down). Measured from its pixels; the image
            // model often draws a little closed enclosure instead.
            if (fills && !s.need.value("piece", "").empty()) {
                const auto image = DecodeImage(host_.projectRoot / path);
                const std::string kind = image ? PieceKind(AnalyzeShape(*image), false) : "";
                const bool straight = iso ? (kind == "straight-x" || kind == "straight-y") : (kind == "straight-h" || kind == "straight-v");
                if (image && !straight) {
                    fills = false;
                    reasons += std::string(reasons.empty() ? "" : "; ") +
                               "not one straight section that joins end to end (it reads as " + (kind.empty() ? std::string("something else") : kind) +
                               "): draw a single straight run of fence, two posts and its rails, no corners, not a closed square";
                }
            }
            // Measured, whatever the model says (docs/build-fixes.md): a thing that stands in the scene
            // must be cut out (not on a box of background) and be one thing (not a sheet of many).
            if (fills && ground == false) {
                if (const auto image = DecodeImage(host_.projectRoot / path)) {
                    ImageRect region;
                    if (const json r = s.need.value("rect", json()); r.is_array() && r.size() == 4) region = {r[0].get<int>(), r[1].get<int>(), r[2].get<int>(), r[3].get<int>()};
                    const ArtShape shape = AnalyzeShape(*image, region);
                    if (shape.edgesOpaque) {
                        fills = false;
                        reasons += std::string(reasons.empty() ? "" : "; ") + "it stands on a solid box of background, not cut out on transparency";
                    } else if (region.Empty() && SegmentObjects(*image).size() >= 5) {
                        fills = false;
                        reasons += std::string(reasons.empty() ? "" : "; ") + "it's a sheet of many separate pieces, not one thing";
                    }
                }
            }
            // It passed, but the model named no role (or a broader one): it's the slot's, for the catalog.
            if (fills && !checking && !isWanted(seen) && !wanted.empty()) newAssets_.back()["fields"]["roles"] = wanted.front();
            // The verdict on this one, shown under its picture.
            for (auto& m : s.made) {
                if (m.value("path", "") == path) {
                    m["seen"] = seen.empty() ? a.value("kind", "") : seen;
                    m["ok"] = fills;
                }
            }
            if (fills && !passed) {
                // A refined piece in: the ones it replaces leave the catalog's choices.
                for (const auto& old : s.superseded) {
                    if (old.is_string() && old != path) {
                        newAssets_.push_back({{"path", old}, {"fields", {{"roles", ""}, {"description", "replaced by a refined version: " + path}}}});
                    }
                }
                s.superseded = json::array();
            }
            // The vision model's own verdict on what was wanted (and why not): it decides when it says no.
            if (a.value("sheet", false)) {
                fills = false;
                reasons += (reasons.empty() ? "" : "; ") + std::string("it's a sheet of many separate pieces, not one");
            } else if (a.contains("fits") && a["fits"].is_boolean() && !a["fits"].get<bool>()) {
                std::string why;
                for (const auto& p : a.value("problems", json::array())) {
                    if (p.is_string()) why += (why.empty() ? "" : "; ") + p.get<std::string>();
                }
                if (fills) reasons += (reasons.empty() ? "" : "; ") + (why.empty() ? std::string("it doesn't fit what was asked") : why);
                fills = false;
            }
            if (fills) passed = true;
            else if (reasons.empty() || reasons.find(a.value("description", "\x01")) == std::string::npos)
                reasons += (reasons.empty() ? "" : "; ") + std::string("it looks like ") + (seen.empty() ? a.value("kind", "something else") : seen) +
                           ", not a " + s.need.value("title", role);
        }
        if (!body.value("ok", false)) reasons = FirstError(body);
        // A generated one that didn't pass: again, with the reasons (3 tries at most).
        if (!passed && s.need.value("action", "") == "generate" && s.attempts < 3) {
            s.reasons = reasons;
            if (SendArt(key)) {
                status_ = "Making the art again for " + key + " (" + reasons + ")…";
                return;
            }
        }
        s.passed = passed;
        s.done = true;
    }
    FinishSourcing();
}

json PlaceBuild::SourcingProgress(const std::map<std::string, Sourcing>& pieces) {
    json out = json::array();
    for (const auto& [key, s] : pieces) {
        const std::string role = s.need.value("role", "");
        const std::string title = s.need.value("title", role.substr(0, role.find('|')));
        std::string others = role.find('|') == std::string::npos ? "" : role.substr(role.find('|') + 1);
        std::replace(others.begin(), others.end(), '|', ',');
        out.push_back({{"key", key}, {"role", title}, {"alternatives", others}, {"description", s.need.value("description", "")},
                       {"action", s.need.value("action", "generate")}, {"attempts", s.attempts}, {"done", s.done}, {"passed", s.passed},
                       {"made", s.made}, {"request", s.request}, {"verdict", s.verdict}});
    }
    return out;
}

std::string PlaceBuild::SetArtVerdict(const std::string& key, const std::string& verdict, std::vector<std::string>* paths) {
    if (verdict != "kept" && verdict != "rejected" && !verdict.empty()) return "a verdict is kept or rejected";
    auto it = sourced_.find(key);
    if (it == sourced_.end()) return "no art was made for that here";
    it->second.verdict = verdict;
    if (verdict == "rejected") {
        it->second.passed = false;
        tried_[key].generate = true; // made and refused: its other sources next
    }
    if (paths) {
        for (const auto& m : it->second.made) {
            if (m.value("ok", true)) paths->push_back(m.value("path", ""));
        }
    }
    Log({{"step", "art verdict"}, {"need", key}, {"verdict", verdict}});
    Save();
    return {};
}

std::string PlaceBuild::RefineArt(const std::string& key, const std::string& text) {
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) return "say what to change";
    const bool during = stage_ == "sourcing" && sourcing_.contains(key);
    if (!during && !(stage_ == "built" || stage_ == "planned" || stage_ == "sourced")) return "the art is made when the place is built";
    if (!during && !sourced_.contains(key)) return "no art was made for that here";
    if (!during) {
        // After the build: a round of sourcing for this piece alone.
        sourcing_.clear();
        sourcing_[key] = sourced_[key];
        sourcingDone_ = false;
    }
    Sourcing& s = sourcing_[key];
    // What it made so far is replaced by what comes now (when it passes).
    for (const auto& m : s.made) s.superseded.push_back(m.value("path", ""));
    s.request = text;
    s.reasons.clear();
    s.attempts = 0;
    s.done = false;
    s.passed = false;
    s.need["action"] = "generate";
    if (!SendArt(key)) {
        if (!during) sourcing_.clear();
        s.done = true;
        return "the Art tool needs the AI";
    }
    stage_ = "sourcing";
    status_ = "Making the " + s.need.value("role", key) + " again: " + text;
    Log({{"step", "refine art"}, {"need", key}, {"request", text}});
    Save();
    return {};
}

void PlaceBuild::SkipSourcing() {
    if (stage_ != "sourcing") return;
    for (auto& [key, s] : sourcing_) s.done = true; // answers still on their way are ignored
    for (auto it = pending_.begin(); it != pending_.end();) {
        const std::string kind = it->second.kind;
        it = kind == "art" || kind == "web" || kind == "critic" ? pending_.erase(it) : std::next(it);
    }
    Log({{"step", "sourcing skipped"}});
    FinishSourcing();
    Save();
}

void PlaceBuild::FinishSourcing() {
    SendWaiting();
    for (const auto& [key, s] : sourcing_) {
        if (!s.done) return;
    }
    for (const auto& [key, s] : sourcing_) {
        Tried& t = tried_[key];
        const std::string action = s.need.value("action", "");
        if (action == "check") {
            // Found art, looked at: used, or the next candidate (down to the Art tool) next time.
            // Under both names: the copy checked, and the candidate it came from (a library's art is
            // copied into the project first; the next decision sees the library's again).
            const std::string entry = s.need.value("entry", ""), candidate = s.need.value("candidate", "");
            (s.passed ? t.approved : t.refused).insert(entry);
            if (!candidate.empty()) (s.passed ? t.approved : t.refused).insert(candidate);
            if (!s.passed) ++t.refusals;
            continue;
        }
        if (action == "web") t.web = true;
        else t.generate = true;
        for (const auto& m : s.made) {
            if (m.value("ok", false)) t.approved.insert(m.value("path", "")); // made and passed: no need to look again
        }
    }
    for (const auto& [key, s] : sourcing_) sourced_[key] = s; // kept for Refine
    sourcing_.clear();
    sourcingDone_ = true;
    stage_ = "sourced";
    status_ = "The art is ready. Build puts the place into the scene.";
    Log({{"step", "sourced"}, {"newAssets", newAssets_}});
}

void PlaceBuild::SetAssetNeeds(const json& needs) {
    // ③ Assets: what each need gets (the project's art, the library's, made, or a placeholder), and why.
    assetNeeds_ = json::array();
    for (const auto& n : needs) {
        json row = {{"key", n.value("key", "")}, {"role", n.value("role", "")}, {"concepts", n.value("concepts", json::array())},
                    {"source", n.value("source", "")}, {"entry", n.value("entry", "")}, {"name", n.value("name", "")},
                    {"why", n.value("why", "")}, {"uses", n.value("uses", 1)}, {"component", n.value("component", "")},
                    {"policy", n.value("policy", "auto")}, {"override", n.value("override", "")},
                    {"title", n.value("title", "")}, {"description", n.value("description", "")}};
        if (n.contains("decision")) row["decision"] = n["decision"];
        if (const ModuleDef* m = host_.ontology().Module(n.value("component", ""))) row["componentName"] = m->name;
        assetNeeds_.push_back(row);
    }
}

void PlaceBuild::MarkSourced(const json& needs) {
    SetAssetNeeds(needs);
    if (stage_ != "sourcing") {
        stage_ = "sourced";
        status_ = "The art is ready. Build puts the place into the scene.";
    }
    Save();
}

void PlaceBuild::SetBuildProgress(int done, int total, const std::string& text) {
    buildProgress_ = {{"done", done}, {"total", total}, {"text", text}};
    status_ = text;
}

json PlaceBuild::TakeNewAssets() {
    json out = newAssets_;
    newAssets_ = json::array();
    return out;
}

json PlaceBuild::Sites() const {
    json out = json::array();
    if (!solved_.is_object()) return out;
    const Ontology& ontology = host_.ontology();
    json spec = spec_;
    for (const auto& r : solved_.value("regions", json::array())) {
        const ModuleDef* m = ontology.Module(r.value("module", ""));
        if (m == nullptr || !(m->compile.is_object() && m->compile.value("nested", false))) continue;
        const json* node = SpecNode(spec, r.value("node", ""));
        const json fields = node ? node->value("quality", json::object()) : json::object();
        auto field = [&](const std::string& name) {
            const json v = fields.value(name, json());
            if (v.is_string()) return v.get<std::string>();
            const FieldDef* f = m->Field(name);
            return f && f->defaultValue.is_string() ? f->defaultValue.get<std::string>() : std::string();
        };
        out.push_back({{"id", r.value("id", "")}, {"node", r.value("node", "")}, {"name", field("name")}, {"buildType", field("build-type")},
                       {"scale", field("scale-class")}, {"bounds", r.value("bounds", json())}});
    }
    return out;
}

std::string PlaceBuild::StartBuilding() {
    if (stage_ != "planned" && stage_ != "sourced" && stage_ != "built" && stage_ != "building") return "approve the plan first";
    if (!solved_.is_object()) return "the plan isn't laid out";
    stage_ = "building";
    status_ = "Putting the place into the scene…";
    Log({{"step", "building"}});
    Save();
    return {};
}

void PlaceBuild::BuildFailed(const std::string& why) {
    stage_ = sourced_.empty() && assetNeeds_.empty() ? "planned" : "sourced";
    status_ = "The build failed (" + why + "). Build tries again.";
    Notice(status_);
    Log({{"step", "build failed"}, {"why", why}});
    Save();
}

void PlaceBuild::Built(const json& result) {
    result_ = result;
    WriteJson(Folder() / "result.json", result_);
    stage_ = "built";
    int failed = 0;
    for (const auto& c : result_.value("after", json::array())) failed += !c.value("ok", true) && !c.value("warning", false);
    status_ = failed ? std::to_string(failed) + " check(s) still failing after the repairs" : "Built and verified.";
    Log({{"step", "built"}, {"report", result_.value("report", json())}, {"repairs", result_.value("repairs", json::array())}});
    if (failed == 0) KeepAsExample();
    Save();
}

std::string PlaceBuild::Paint(const json& requests) {
    backdrops_ = json::array();
    paintRequests_ = requests.is_array() ? requests : json::array();
    if (paintRequests_.empty()) {
        painted_ = true;
        return "nothing to paint";
    }
    for (size_t i = 0; i < paintRequests_.size(); ++i) {
        const json& r = paintRequests_[i];
        // Its endpoint and body: the block-out painted by the Paint Provider (BlockoutPaintRequests), or a
        // backdrop painted from words (BackdropRequests).
        const std::string path = r.value("path", "/places/backdrop");
        const json body = r.contains("body") ? r["body"] : json{{"context", r.value("context", json::object())}, {"out", r.value("out", "")}};
        if (Send({"paint", -1, std::to_string(i), path, body, 900})) {
            ++painting_;
        }
    }
    if (painting_ == 0) {
        painted_ = true;
        return "the AI is off";
    }
    status_ = "Painting the place: one backdrop per level, then its map read from it…";
    Log({{"step", "paint"}, {"backdrops", painting_}});
    Save();
    return {};
}

std::string PlaceBuild::Sketch(const json& requests) {
    if (!requests.is_array() || requests.empty()) {
        sketched_ = true;
        return "nothing to sketch";
    }
    sketches_ = json::array();
    sketchCrops_.clear();
    sketchSeen_.clear();
    for (const auto& r : requests) {
        if (Send({"sketch", -1, r.value("name", ""), "/assets/sketch", r, 600})) ++sketching_;
    }
    if (sketching_ == 0) {
        sketched_ = true; // without the AI, the art is made without a sketch
        return "the AI is off";
    }
    status_ = "Sketching the place, window by window: one painting each, the art of every piece from it…";
    Log({{"step", "sketch"}, {"windows", sketching_}});
    Save();
    return {};
}

std::string PlaceBuild::InspectBuilt(const json& body) {
    if (stage_ != "built") return "the place isn't built";
    if (!Send({"inspect-build", -1, "", "/places/inspect", body, 900})) return "the AI is off";
    inspecting_ = true;
    if (result_.is_object()) result_["critique"] = {{"looking", true}};
    status_ = "The AI is checking the built place: each kind of thing, where it stands, how it fits and looks…";
    Log({{"step", "check build"}, {"windows", body.value("images", json::array()).size()}});
    Save();
    return {};
}

void PlaceBuild::KeepAsExample() const {
    // places/examples.json: the project's built and verified places, latest first.
    const fs::path file = host_.projectRoot / "places" / "examples.json";
    json examples = ReadJson(file);
    if (!examples.is_array()) examples = json::array();
    json zones = json::array();
    for (const auto& c : spec_["root"].value("children", json::array())) {
        const ModuleDef* m = host_.ontology().Module(c.value("module", ""));
        if (m == nullptr || m->level != PlaceLevel::Zone) continue;
        json one = {{"module", m->id}};
        for (const char* field : {"plots", "area"}) {
            if (c.contains("quantity") && c["quantity"].contains(field)) one[field] = c["quantity"][field];
        }
        if (c.contains("quality") && c["quality"].contains("street-style")) one["street-style"] = c["quality"]["street-style"];
        if (c.contains("rel")) one["rel"] = c["rel"];
        json parcels = json::array();
        for (const auto& p : c.value("children", json::array())) {
            parcels.push_back({{"module", p.value("module", "")}, {"repeat", p.value("repeat", 1)}});
        }
        one["parcels"] = parcels;
        zones.push_back(one);
    }
    json example = {{"id", id_}, {"request", query_}, {"buildType", brief_.value("buildType", "")},
                    {"class", BriefValue(brief_, "scale.class")}, {"buildings", CountBuildings(host_.ontology(), spec_)}, {"zones", zones}};
    json kept = json::array({example});
    for (const auto& e : examples) {
        if (e.value("id", "") != id_ && kept.size() < 20) kept.push_back(e);
    }
    WriteJson(file, kept);
}

json PlaceBuild::ExamplesLikeThis() const {
    const json examples = ReadJson(host_.projectRoot / "places" / "examples.json");
    json out = json::array();
    if (!examples.is_array()) return out;
    for (const auto& e : examples) {
        if (out.size() >= 2) break;
        if (e.value("id", "") == id_ || e.value("buildType", "") != brief_.value("buildType", "")) continue;
        if (e.value("class", json()) != BriefValue(brief_, "scale.class")) continue;
        json shown = e;
        shown.erase("id");
        out.push_back(shown);
    }
    return out;
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
    choices_ = request.value("choices", json::object());
    story_ = request.value("story", json());
    fromPlanner_ = request.value("fromPlanner", false);
    pending_.clear();
    errors_.clear();
    notices_.clear();
    revisions_ = json::array();
    result_ = json();
    hasOrigin_ = false;
    round_ = 1;
    const Ontology& ontology = host_.ontology();
    context_ = InterpretContextFor(ontology, query_, choices_);
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
        if (const json sk = session.value("sketch", json::object()); sk.is_object()) {
            sketched_ = sk.value("done", false);
            sketches_ = sk.value("sketches", json::array());
            const json crops = sk.value("crops", json::object()); // named: items() of a temporary dangles
            for (const auto& [key, crop] : crops.items()) {
                if (crop.is_string()) sketchCrops_[key] = crop.get<std::string>();
            }
        }
        if (const json pt = session.value("paint", json::object()); pt.is_object()) {
            painted_ = pt.value("done", false);
            backdrops_ = pt.value("backdrops", json::array());
        }
        if (const json lc = session.value("layoutCheck", json::object()); lc.is_object()) {
            layoutChecked_ = lc.value("checked", false);
            layoutRounds_ = lc.value("rounds", 0);
            layoutCheck_ = lc.value("answer", json());
        }
        fitScale_.clear();
        const json fit = session.value("fit", json::object());
        const json fitScale = fit.value("scale", json::object());
        for (const auto& [id, f] : fitScale.items()) {
            if (f.is_number()) fitScale_[id] = f.get<double>();
        }
        fitSide_ = session.value("fit", json::object()).value("side", 0);
        locks_.clear();
        for (const auto& l : session.value("locks", json::array())) {
            if (l.is_string()) locks_.insert(l.get<std::string>());
        }
        tried_.clear();
        const json tried = session.value("tried", json::object());
        for (const auto& [k, v] : tried.items()) {
            Tried t{v.value("web", false), v.value("generate", false)};
            t.refusals = v.value("refusals", 0);
            for (const auto& p : v.value("refused", json::array())) {
                if (p.is_string()) t.refused.insert(p.get<std::string>());
            }
            for (const auto& p : v.value("approved", json::array())) {
                if (p.is_string()) t.approved.insert(p.get<std::string>());
            }
            tried_[k] = t;
        }
        sourcing_ = SourcingFrom(session.value("sourcing", json::object()));
        questions_ = session.value("questions", json::array());
        decisionAnswers_ = session.value("decisionAnswers", json::object());
        directions_ = session.value("directions", "");
        sourced_ = SourcingFrom(session.value("sourced", json::object()));
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
    solved_ = ReadJson(folder / (stage == "layout" ? "zones.solved.json" : "layout.solved.json"));
    if (stage == "revising") {
        Notice("A revision was still running when the build was closed; ask for it again.");
        stage.clear();
    }
    // Closed while the art was being made: what wasn't done yet is asked for again (with the AI);
    // without it, the plan stands approved and Build goes on with placeholders.
    bool resumeSourcing = false;
    if (stage == "sourcing" && !sourcing_.empty()) {
        for (auto& [key, s] : sourcing_) {
            if (s.done) continue;
            if (s.need.value("action", "") == "web") s.done = true; // a search isn't resumed: the next pass moves on
            else resumeSourcing = SendArt(key) || resumeSourcing;
            if (!resumeSourcing) s.done = true;
        }
        if (!resumeSourcing) {
            for (const auto& [key, s] : sourcing_) sourced_[key] = s;
            sourcing_.clear();
        }
    }
    if (resumeSourcing) {
        stage = "sourcing";
        status_ = "Making the rest of the art…";
    } else if (stage == "building" || stage == "sourcing") {
        stage = sourced_.empty() ? "planned" : "sourced"; // Build again
    }
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
        if (!SendReadings()) {
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
    json state = {{"type", "build:state"}, {"id", id_}, {"query", query_}, {"fromPlanner", fromPlanner_}, {"stage", stage_}, {"round", round_},
                  {"status", status_}, {"errors", errors_}, {"notices", notices_}, {"drafts", drafts_.size()}};
    if (stage_ == "plan" || stage_ == "planned" || stage_ == "sourcing" || stage_ == "sourced" || stage_ == "building" || stage_ == "built") {
        state["plan"] = PlanSummary(host_.ontology(), spec_);
        // Why each part is as it is: its rationale (already there), its relations, and what was fixed.
        json spec = spec_;
        for (auto& row : state["plan"]["rows"]) {
            const std::string id = row.value("id", "");
            if (const json* node = SpecNode(spec, id); node && node->contains("rel")) row["rel"] = (*node)["rel"];
            json notes = json::array();
            for (const auto& p : planProblems_) {
                if (p.where == id || p.where.rfind(id + ".", 0) == 0 || p.where.rfind(id + "/", 0) == 0) notes.push_back(p.where + ": " + p.message);
            }
            if (!notes.empty()) row["notes"] = notes;
        }
        state["sketch"] = PlanSketch(host_.ontology(), spec_, SiteSide());
        state["problems"] = ProblemsJson(planProblems_);
        if (solved_.is_object()) {
            state["image"] = "places/" + id_ + "/layout.png?v=" + std::to_string(imageVersion_);
            state["solved"] = {{"stats", solved_.value("stats", json())}, {"checks", solved_.value("checks", json::array())},
                               {"problems", solved_.value("problems", json::array())}};
        }
        state["name"] = brief_.value("name", id_);
    }
    if (!sourced_.empty() && stage_ != "sourcing") state["sourced"] = SourcingProgress(sourced_);
    if (stage_ == "sourcing") {
        state["sourcing"] = SourcingProgress(sourcing_);
        int done = 0;
        for (const auto& [key, s] : sourcing_) done += s.done;
        state["progress"] = {{"done", done}, {"total", sourcing_.size()}};
    }
    if (stage_ == "built" && result_.is_object()) {
        json shown = result_;
        if (shown.contains("assets")) shown["assets"].erase("byObject");
        state["result"] = shown;
    }
    if (hasOrigin_) state["origin"] = {originX_, originY_};
    if (solved_.is_object()) {
        state["brushModules"] = BrushModules();
        state["locks"] = locks_;
        if (const json sites = Sites(); !sites.empty()) state["sites"] = sites;
    }
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
    if (stage_ == "interpreting" || (stage_ == "planning" && planPhase_ == "root")) state["confirmProgress"] = ConfirmProgress();
    if (stage_ == "planning") {
        state["progress"] = {{"zones", zonesTotal_}, {"done", zonesDone_}};
        state["planPhase"] = planPhase_; // root: the zones (① Confirm); zones, final: what's inside them (② Plan)
    }
    if (stage_ == "revising") state["revisingFrom"] = beforeRevision_;
    // ③ Layout: each zone (and network), what it is, where it was meant to go and where it lies.
    const bool planStands = host_.layoutStep && LayoutFrozen() &&
                            (stage_ == "plan" || stage_ == "planned" || stage_ == "sourced" || stage_ == "built" ||
                             (stage_ == "revising" && beforeRevision_ != "layout"));
    if (stage_ == "layout" || (stage_ == "revising" && beforeRevision_ == "layout") || planStands) {
        const Ontology& ontology = host_.ontology();
        std::map<std::string, std::string> names;
        for (const auto& c : spec_["root"].value("children", json::array())) {
            if (const ModuleDef* m = ontology.Module(c.value("module", ""))) names[c.value("id", "")] = m->name;
        }
        const json where = solved_.is_object() ? LayoutWhere(solved_, names) : json();
        json zones = json::array();
        for (const auto& c : spec_["root"].value("children", json::array())) {
            const ModuleDef* m = ontology.Module(c.value("module", ""));
            if (m == nullptr || m->id == "ground") continue;
            const std::string id = c.value("id", "");
            json row = {{"id", id}, {"module", m->id}, {"name", m->name}, {"level", PlaceLevelName(m->level)}, {"description", m->description},
                        {"placement", c.value("placement", "")}, {"rationale", c.value("rationale", "")}, {"rel", c.value("rel", json::array())}};
            const std::string field = CountField(*m);
            // As one number (the AI may write a range, [6, 10]: its middle).
            if (!field.empty() && c.contains("quantity") && c["quantity"].contains(field)) {
                const json v = c["quantity"][field];
                const json n = v.is_object() ? v.value("count", json()) : v;
                if (n.is_number()) row["count"] = {{"count", static_cast<int>(std::lround(n.get<double>()))}};
                else if (n.is_array() && n.size() == 2 && n[0].is_number() && n[1].is_number()) {
                    row["count"] = {{"count", static_cast<int>(std::lround((n[0].get<double>() + n[1].get<double>()) / 2))}};
                }
            }
            // Its kinds of building, each with how many (the user's, or about how many the plan makes).
            if (m->level == PlaceLevel::Zone) {
                const double total = row.contains("count") ? row["count"]["count"].get<double>() : 0;
                const json kinds = ZoneKinds(ontology, *m, UnitOf(ontology, spec_).cls, BriefValue(brief_, "components." + m->id + ".kinds"), total);
                if (!kinds.empty()) row["kinds"] = kinds;
            }
            if (!field.empty()) row["countField"] = field;
            if (m->Field("street-style")) {
                row["style"] = c.value("quality", json::object()).value("street-style", json());
                row["hasStyle"] = true;
            }
            if (c.contains("at")) row["drawn"] = c["at"];
            if (c.contains("paint")) row["painted"] = true;
            row["area"] = c.value("quantity", json::object()).value("area", json());
            // Room for what it's to hold: its count of parcels, each a typical lot with its share of the streets.
            if (const int needed = ZoneNeed(ontology, c); needed > 0) {
                int got = 0;
                for (const auto& r : solved_.value("regions", json::array())) {
                    if (r.value("statement", "") == "site/zones" && r.value("id", "") == id) got = r.value("area", 0);
                }
                // Within a tenth is room enough (the plan's fitting finds the rest).
                row["space"] = {{"cells", got}, {"needed", needed}, {"ok", got >= needed * 0.9}};
            }
            if (where.is_object() && where.contains("zones")) {
                for (const auto& z : where["zones"]) {
                    if (z.value("id", "") == id) row["where"] = WhereText(z, names, SiteSide());
                }
            }
            zones.push_back(row);
        }
        state["layout"] = {{"zones", zones}, {"unit", UnitOf(ontology, spec_).label}, {"stale", layoutStale_}};
        // The main pattern, and the patterns a zone may take.
        state["layout"]["main"] = BriefValue(brief_, "shape.layout");
        if (const auto* patterns = ontology.Scheme("layout")) {
            json list = json::array();
            for (const auto& c : *patterns) list.push_back(c.id);
            state["layout"]["patterns"] = list;
        }
        state["layout"]["side"] = SiteSide();
        state["layout"]["quality"] = LayoutQuality(spec_, solved_).value("checks", json::array()); // §11.3's warnings
        state["layout"]["sketched"] = spec_.contains("root") && spec_["root"].contains("sketch");
        if (where.is_object() && where.contains("map")) state["layout"]["map"] = where["map"];
        state["revisions"] = revisions_;
        state["questions"] = questions_;
        state["directions"] = directions_;
        state["decisionAnswers"] = decisionAnswers_;
        if (solved_.is_object()) state["image"] = "places/" + id_ + "/layout.png?v=" + std::to_string(imageVersion_);
        // The zones' own map (the plan's map is the plan's).
        const bool zonesMap = fs::exists(Folder() / "zones.png");
        state["layout"]["image"] = "places/" + id_ + (zonesMap ? "/zones.png" : "/layout.png") + "?v=" + std::to_string(imageVersion_);
        if (planStands) state["layout"]["view"] = true; // looked at from the plan: changing it discards the plan
        state["name"] = brief_.value("name", id_);
        // The Contents tab: everything the Request step had, and each part's settings.
        const Catalog catalog = host_.catalog ? host_.catalog() : Catalog{};
        state["window"] = BriefWindow(ontology, brief_, &merge_, &catalog, host_.isoGrid ? host_.isoGrid() : true, 2);
        // The zones as laid out, for the drawing tool.
        json cells = json::array();
        for (const auto& r : solved_.value("regions", json::array())) {
            if (r.value("statement", "") == "site/zones") cells.push_back({{"id", r.value("id", "")}, {"cells", r.value("cells", json::array())}});
        }
        state["layout"]["regions"] = cells;
        json paths = json::array();
        for (const auto& pth : solved_.value("paths", json::array())) {
            const std::string cls = pth.value("class", "");
            if (cls == "street" || cls == "lane" || cls == "trail") continue; // the zones' own streets are the plan's
            paths.push_back({{"class", cls}, {"cells", pth.value("cells", json::array())}});
        }
        state["layout"]["paths"] = paths;
    }
    // ③ Assets (after the plan): the art plan and where each component's art comes from.
    if (stage_ == "planned" || stage_ == "sourcing" || stage_ == "sourced" || stage_ == "built") state["assetNeeds"] = assetNeeds_;
    if (stage_ == "building") state["buildProgress"] = buildProgress_;
    if (host_.layoutStep && (stage_ == "planned" || stage_ == "sourcing" || stage_ == "sourced" || stage_ == "built")) {
        const Catalog catalog = host_.catalog ? host_.catalog() : Catalog{};
        state["window"] = BriefWindow(host_.ontology(), brief_, &merge_, &catalog, host_.isoGrid ? host_.isoGrid() : true, 2);
    }
    if (stage_ == "request" || stage_ == "assets") {
        state["questions"] = questions_;
        state["directions"] = directions_;
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
    else if (stage_ == "layout") text = "① Confirm and ② Layout: what's built and where it goes";
    else if (stage_ == "planned") text = "④ Assets: find the art, then build";
    else if (stage_ == "sourced") text = "④ Assets: the art is ready; Build puts it into the scene";
    else if (stage_ == "revising") text = status_.empty() ? "Revising the plan…" : status_;
    else if (stage_ == "building") text = "④ Assets: building it into the scene…";
    else if (stage_ == "sourcing") text = "④ Assets: " + status_;
    else if (stage_ == "built") text = "⑤ Result: " + status_;
    else if (stage_ == "cancelled") text = "Cancelled";
    else if (stage_ == "failed") text = errors_.empty() ? "Failed" : errors_.front();
    return {{"type", "build:card"}, {"buildId", id_}, {"name", brief_.is_object() ? brief_.value("name", "") : ""},
            {"stage", stage_}, {"text", text}, {"query", query_}};
}

} // namespace aige
