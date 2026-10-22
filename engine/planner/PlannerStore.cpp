#include "planner/PlannerStore.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <set>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::optional<json> ReadJsonFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    json j = json::parse(in, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

bool WriteJsonFile(const fs::path& path, const json& j) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    // Written beside and moved over, so a crash mid-write never leaves half a document.
    const fs::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << j.dump(2) << "\n";
        if (!out) return false;
    }
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        fs::rename(temp, path, ec);
    }
    return !ec;
}

std::vector<json> ReadLines(const fs::path& path) {
    std::vector<json> out;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        json j = json::parse(line, nullptr, false);
        if (!j.is_discarded()) out.push_back(std::move(j));
    }
    return out;
}

void AppendLine(const fs::path& path, const json& j) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << j.dump() << "\n";
}

// Documents that aren't the planner's own state.
bool IsDocument(const std::string& rel) {
    static const std::set<std::string> skip = {"state", "defaults", "kg"};
    return !skip.contains(rel) && rel.rfind("history/", 0) != 0;
}

} // namespace

// --- Decisions ------------------------------------------------------------------------------

json PlanDecision::ToJson() const {
    json j = {{"schema", "aige.decision/1"}, {"id", id},       {"time", time},       {"stage", stage},
              {"actor", actor},              {"kind", kind},   {"subject", subject}};
    if (!chosen.empty()) j["chosen"] = chosen;
    if (!rationale.empty()) j["rationale"] = rationale;
    if (!reason.empty()) j["reason"] = reason;
    if (!call.empty()) j["call"] = call;
    if (!inputs.empty()) j["inputs"] = inputs;
    if (!outputs.empty()) j["outputs"] = outputs;
    if (!alternatives.empty()) j["alternatives"] = alternatives;
    if (!checks.empty()) j["checks"] = checks;
    return j;
}

PlanDecision PlanDecision::FromJson(const json& j) {
    PlanDecision d;
    d.id = j.value("id", "");
    d.time = j.value("time", "");
    d.stage = j.value("stage", "");
    d.actor = j.value("actor", "");
    d.kind = j.value("kind", "");
    d.subject = j.value("subject", "");
    d.chosen = j.value("chosen", "");
    d.rationale = j.value("rationale", "");
    d.reason = j.value("reason", "");
    d.call = j.value("call", "");
    d.inputs = j.value("inputs", json::object());
    d.outputs = j.value("outputs", json::array());
    d.alternatives = j.value("alternatives", json::array());
    d.checks = j.value("checks", json::object());
    return d;
}

// --- The store ------------------------------------------------------------------------------

PlannerStore::PlannerStore(fs::path projectRoot) : root_(std::move(projectRoot)) {}

fs::path PlannerStore::PathOf(const std::string& doc) const { return Dir() / (doc + ".json"); }

std::string PlannerStore::Now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::optional<json> PlannerStore::Read(const std::string& doc) const { return ReadJsonFile(PathOf(doc)); }

int PlannerStore::Write(const std::string& doc, json content, const std::string& madeBy, const std::vector<std::string>& inputs) {
    if (!content.is_object()) content = json{{"value", content}};
    const int revision = RevisionOf(doc) + 1;
    content["revision"] = revision;
    if (!madeBy.empty()) content["madeBy"] = madeBy;
    json in = json::object();
    for (const auto& input : inputs) {
        if (input != doc) in[input] = RevisionOf(input);
    }
    content["inputs"] = in;
    WriteJsonFile(PathOf(doc), content);
    return revision;
}

int PlannerStore::Touch(const std::string& doc, const json& content, const std::string& madeBy) {
    json next = content;
    const auto old = Read(doc);
    next["revision"] = RevisionOf(doc) + 1;
    if (!madeBy.empty()) next["madeBy"] = madeBy;
    if (old && old->contains("inputs") && !next.contains("inputs")) next["inputs"] = (*old)["inputs"];
    WriteJsonFile(PathOf(doc), next);
    return next["revision"].get<int>();
}

int PlannerStore::RevisionOf(const std::string& doc) const {
    if (doc == "kg") {
        const auto kg = ReadJsonFile(Dir() / "kg.json");
        return kg ? kg->value("revision", 0) + 1 : 0;
    }
    const auto j = Read(doc);
    return j ? j->value("revision", 1) : 0;
}

std::vector<std::string> PlannerStore::AllDocuments() const {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(Dir(), ec)) return out;
    for (auto it = fs::recursive_directory_iterator(Dir(), ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file() || it->path().extension() != ".json") continue;
        std::string rel = fs::relative(it->path(), Dir(), ec).generic_string();
        rel = rel.substr(0, rel.size() - 5);
        if (IsDocument(rel)) out.push_back(rel);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> PlannerStore::List(const std::string& folder) const {
    std::vector<std::string> out;
    const std::string prefix = folder + "/";
    for (const auto& doc : AllDocuments()) {
        if (doc.rfind(prefix, 0) == 0 && doc.find('/', prefix.size()) == std::string::npos) out.push_back(doc);
    }
    return out;
}

void PlannerStore::Remove(const std::string& doc) {
    std::error_code ec;
    fs::remove(PathOf(doc), ec);
}

bool PlannerStore::Locked(const std::string& doc) const {
    const auto j = Read(doc);
    return j && j->value("locked", false);
}

std::vector<std::string> PlannerStore::Stale() const {
    std::vector<std::string> out;
    for (const auto& doc : AllDocuments()) {
        const auto j = Read(doc);
        if (!j || !j->contains("inputs") || !(*j)["inputs"].is_object()) continue;
        for (const auto& [input, revision] : (*j)["inputs"].items()) {
            if (revision.is_number() && RevisionOf(input) != revision.get<int>()) {
                out.push_back(doc);
                break;
            }
        }
    }
    return out;
}

std::vector<std::string> PlannerStore::Dependents(const std::string& doc) const {
    std::vector<std::string> out;
    for (const auto& d : AllDocuments()) {
        const auto j = Read(d);
        if (j && j->contains("inputs") && (*j)["inputs"].is_object() && (*j)["inputs"].contains(doc)) out.push_back(d);
    }
    return out;
}

std::string PlannerStore::Decide(PlanDecision decision) {
    const auto all = ReadLines(Dir() / "decisions.jsonl");
    decision.id = "d-" + std::to_string(all.size() + 1);
    if (decision.time.empty()) decision.time = Now();
    AppendLine(Dir() / "decisions.jsonl", decision.ToJson());
    return decision.id;
}

std::vector<PlanDecision> PlannerStore::Decisions() const {
    std::vector<PlanDecision> out;
    for (const auto& j : ReadLines(Dir() / "decisions.jsonl")) out.push_back(PlanDecision::FromJson(j));
    return out;
}

std::optional<PlanDecision> PlannerStore::Decision(const std::string& id) const {
    for (const auto& j : ReadLines(Dir() / "decisions.jsonl")) {
        if (j.value("id", "") == id) return PlanDecision::FromJson(j);
    }
    return std::nullopt;
}

std::string PlannerStore::Log(const json& entry) {
    std::error_code ec;
    const fs::path path = Dir() / "log.jsonl";
    size_t n = 0;
    {
        std::ifstream in(path, std::ios::binary);
        std::string line;
        while (std::getline(in, line)) n += !line.empty();
    }
    const std::string id = "c-" + std::to_string(n + 1);
    json j = entry;
    j["id"] = id;
    j["time"] = Now();
    AppendLine(path, j);
    return id;
}

json PlannerStore::State() const { return ReadJsonFile(Dir() / "state.json").value_or(json::object()); }

void PlannerStore::SaveState(const json& state) { WriteJsonFile(Dir() / "state.json", state); }

bool PlannerStore::HasPlan() const {
    std::error_code ec;
    return fs::exists(Dir() / "state.json", ec);
}

int PlannerStore::Commit(const std::string& label) {
    const json history = History();
    const int n = static_cast<int>(history.size()) + 1;
    const fs::path to = Dir() / "history" / std::to_string(n);
    std::error_code ec;
    fs::create_directories(to, ec);
    for (const auto& doc : AllDocuments()) {
        const fs::path target = to / (doc + ".json");
        fs::create_directories(target.parent_path(), ec);
        fs::copy_file(PathOf(doc), target, fs::copy_options::overwrite_existing, ec);
    }
    if (fs::exists(Dir() / "kg.json", ec)) fs::copy_file(Dir() / "kg.json", to / "kg.json", fs::copy_options::overwrite_existing, ec);
    json index = history;
    index.push_back({{"n", n}, {"label", label}, {"time", Now()}});
    WriteJsonFile(Dir() / "history" / "index.json", index);
    return n;
}

json PlannerStore::History() const {
    const auto j = ReadJsonFile(Dir() / "history" / "index.json");
    return j && j->is_array() ? *j : json::array();
}

bool PlannerStore::Restore(int n) {
    const fs::path from = Dir() / "history" / std::to_string(n);
    std::error_code ec;
    if (!fs::is_directory(from, ec)) return false;
    // The documents of then; what came after goes (it's kept in later snapshots).
    for (const auto& doc : AllDocuments()) Remove(doc);
    for (auto it = fs::recursive_directory_iterator(from, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file()) continue;
        const fs::path rel = fs::relative(it->path(), from, ec);
        const fs::path target = Dir() / rel;
        fs::create_directories(target.parent_path(), ec);
        fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
    }
    return true;
}

json PlannerStore::Defaults() const { return ReadJsonFile(Dir() / "defaults.json").value_or(json::object()); }

void PlannerStore::SaveDefaults(const json& defaults) { WriteJsonFile(Dir() / "defaults.json", defaults); }

} // namespace aige
