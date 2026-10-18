#include "editing/PlaceBuild.h"

#include "editing/BuildWindow.h"
#include "places/PlaceValidate.h"

#include <cctype>
#include <chrono>
#include <fstream>
#include <set>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

const std::set<std::string> kSkipWords = {"a", "an", "the", "build", "make", "create", "generate", "me", "please",
                                          "with", "and", "of", "for", "some", "design", "us", "i", "want", "to"};

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
    stage_ = "planning";
    status_ = "Brief confirmed. Planning comes next (milestone 7.5).";
    Log({{"step", "confirmed"}, {"brief", brief_}});
    return {};
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
    if (stage_ == "request" || stage_ == "assets" || stage_ == "planning") {
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
    else if (stage_ == "planning") text = "Brief confirmed; planning is next";
    else if (stage_ == "cancelled") text = "Cancelled";
    else if (stage_ == "failed") text = errors_.empty() ? "Failed" : errors_.front();
    return {{"type", "build:card"}, {"buildId", id_}, {"name", brief_.is_object() ? brief_.value("name", "") : ""},
            {"stage", stage_}, {"text", text}, {"query", query_}};
}

} // namespace aige
