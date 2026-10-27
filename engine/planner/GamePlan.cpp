#include "planner/GamePlan.h"

#include "story/StoryRules.h"
#include "world/WorldBible.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <set>

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

const std::vector<std::string>& StageOrder() {
    static const std::vector<std::string> order = {"concept", "outline", "story", "characters", "quests",
                                                   "places", "systems", "visuals", "validate", "done"};
    return order;
}

const std::map<std::string, std::string>& StageLabels() {
    static const std::map<std::string, std::string> labels = {
        {"concept", "① Concept"},  {"outline", "② Outline"}, {"story", "③ Story"},     {"characters", "④ Characters"},
        {"quests", "⑤ Quests"},    {"places", "⑥ Places"},   {"systems", "⑦ Systems"}, {"visuals", "⑧ Visuals"},
        {"validate", "⑨ Validate"}, {"done", "Done"}};
    return labels;
}

void Flatten(const json& j, const std::string& prefix, std::map<std::string, json>& out) {
    if (j.is_object() && !j.empty()) {
        for (const auto& [k, v] : j.items()) Flatten(v, prefix.empty() ? k : prefix + "." + k, out);
    } else if (!prefix.empty()) {
        out[prefix] = j;
    }
}

std::string Lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

// --- Briefs without the AI, and merging -------------------------------------------------------

json FallbackGameBrief(const std::string& request) {
    const std::string words = Lower(request);
    json brief = json::object();
    std::string genre = "arpg";
    if (words.find("life") != std::string::npos || words.find("farm") != std::string::npos || words.find("cozy") != std::string::npos) genre = "life-sim";
    else if (words.find("adventure") != std::string::npos || words.find("zelda") != std::string::npos) genre = "action-adventure";
    else if (words.find("top-down") != std::string::npos || words.find("top down") != std::string::npos) genre = "rpg";
    brief["genre"] = genre;
    std::smatch m;
    if (std::regex_search(request, m, std::regex(R"((\d+(?:\.\d+)?)\s*(?:hours?|h)\b)", std::regex::icase))) {
        brief["playTime"] = std::stod(m[1].str());
    }
    std::string title;
    int taken = 0;
    for (size_t i = 0; i < request.size() && taken < 4;) {
        while (i < request.size() && !std::isalnum(static_cast<unsigned char>(request[i]))) ++i;
        size_t j = i;
        while (j < request.size() && std::isalnum(static_cast<unsigned char>(request[j]))) ++j;
        if (j > i) {
            std::string w = request.substr(i, j - i);
            w[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(w[0])));
            title += (title.empty() ? "" : " ") + w;
            ++taken;
        }
        i = j;
    }
    brief["title"] = title.empty() ? "Untitled" : title;
    brief["logline"] = request.substr(0, 300);
    brief["setting"] = {{"world", request.substr(0, 300)}};
    return brief;
}

json MergeGameBriefs(const std::vector<json>& drafts, json* marks) {
    std::vector<std::map<std::string, json>> flat;
    std::vector<std::string> readings;
    for (const auto& d : drafts) {
        std::map<std::string, json> f;
        Flatten(d.value("brief", json::object()), "", f);
        flat.push_back(std::move(f));
        readings.push_back(d.value("reading", ""));
    }
    std::set<std::string> paths;
    for (const auto& f : flat) {
        for (const auto& [p, v] : f) paths.insert(p);
    }
    json merged = json::object();
    json m = json::object();
    for (const auto& path : paths) {
        std::vector<std::pair<json, std::vector<size_t>>> values; // value -> drafts holding it
        for (size_t i = 0; i < flat.size(); ++i) {
            auto it = flat[i].find(path);
            if (it == flat[i].end()) continue;
            auto v = std::find_if(values.begin(), values.end(), [&](const auto& x) { return x.first == it->second; });
            if (v == values.end()) values.push_back({it->second, {i}});
            else v->second.push_back(i);
        }
        if (values.empty()) continue;
        size_t best = 0;
        for (size_t k = 1; k < values.size(); ++k) {
            const bool more = values[k].second.size() > values[best].second.size();
            const bool tieTypical = values[k].second.size() == values[best].second.size() &&
                                    std::any_of(values[k].second.begin(), values[k].second.end(), [&](size_t i) { return readings[i] == "typical"; });
            if (more || tieTypical) best = k;
        }
        BriefSet(merged, path, values[best].first);
        // Confidence: how many readings agree, times what they said themselves.
        double said = 0;
        for (size_t i : values[best].second) {
            const json conf = drafts[i].value("confidence", json::object());
            said += conf.contains(path) && conf[path].is_number() ? conf[path].get<double>() : 0.8;
        }
        said /= static_cast<double>(values[best].second.size());
        const double agree = static_cast<double>(values[best].second.size()) / static_cast<double>(std::max<size_t>(1, drafts.size()));
        json mark = {{"confidence", std::round(agree * said * 100.0) / 100.0}};
        json differ = json::array();
        for (size_t k = 0; k < values.size(); ++k) {
            if (k != best) differ.push_back(values[k].first);
        }
        if (!differ.empty()) mark["differ"] = differ;
        mark["guessed"] = agree * said < 0.5;
        m[path] = mark;
    }
    if (marks) *marks = m;
    return merged;
}

// --- The plan ---------------------------------------------------------------------------------

GamePlan::GamePlan(Host host) : host_(std::move(host)), store_(host_.projectRoot), gso_(StoryOntology::ForProject(host_.projectRoot)) {}

void GamePlan::SetBuildMethod(const std::string& method) {
    buildMethod_ = method == "Assembled" || method == "Paint" ? method : "";
    Save();
}

std::string GamePlan::Start(const std::string& request, const json& choices) {
    if (request.find_first_not_of(" \t\r\n") == std::string::npos) return "say what game to make";
    std::error_code ec;
    // A new plan replaces the old one (its history goes with it).
    fs::remove_all(store_.Dir(), ec);
    jobs_.clear();
    pending_.clear();
    concept_ = {{"drafts", json::array()}};
    toReview_ = json::array();
    findings_ = json::array();
    usage_ = {{"calls", 0}, {"inputTokens", 0}, {"outputTokens", 0}};
    nextJob_ = 1;
    paused_ = false;
    request_ = request;
    choices_ = choices.is_object() ? choices : json::object();
    stage_ = "concept";
    status_ = "working";
    message_ = "Reading your request…";
    PlanDecision d;
    d.stage = "concept";
    d.actor = "user";
    d.kind = "request";
    d.subject = "brief";
    d.chosen = request;
    store_.Decide(d);
    for (const char* reading : {"literal", "typical", "generous"}) AddJob("interpret", reading);
    Save();
    Pump();
    return {};
}

std::string GamePlan::Open() {
    if (!store_.HasPlan()) return "this project has no plan yet";
    Load();
    pending_.clear();
    for (auto& job : jobs_) {
        if (job.status == "running") job.status = "queued"; // its answer was lost with the session
    }
    // Paused when the editor closed: it carries on now, from where it was.
    if (pausedForClose_) {
        pausedForClose_ = false;
        paused_ = false;
        if (status_ == "paused") status_ = "working";
    }
    if (status_ == "working") message_ = "Carrying on where the plan was left…";
    // What failed but has the AI's answer in the log: re-applied quietly (the engine now
    // repairs what it once refused), so the boards show it made.
    QueueReplays();
    Save();
    Pump();
    return {};
}

json GamePlan::LoggedAnswer(const std::string& kind, const std::string& subject) const {
    std::ifstream in(store_.Dir() / "log.jsonl", std::ios::binary);
    json found;
    for (std::string line; std::getline(in, line);) {
        if (line.find("\"" + kind + "\"") == std::string::npos) continue; // cheap filter before parsing
        const json e = json::parse(line, nullptr, false);
        if (!e.is_object() || e.value("kind", json()) != kind || e.value("subject", json()) != subject) continue;
        const json a = e.value("answer", json());
        if (!a.is_object() || !a.value("ok", false)) continue;
        found = a;
    }
    if (found.is_object()) {
        for (const char* k : {"ok", "usage", "call", "problems", "errors", "label"}) found.erase(k);
    }
    return found;
}

int GamePlan::QueueReplays() {
    int n = 0;
    std::set<std::pair<std::string, std::string>> seen;
    for (auto it = jobs_.rbegin(); it != jobs_.rend(); ++it) {
        if (it->status != "failed") continue;
        if (!seen.insert({it->kind, it->subject}).second) {
            it->status = "skipped"; // an older try at the same thing
            continue;
        }
        if (it->replayed) continue;
        json answer = LoggedAnswer(it->kind, it->subject);
        if (!answer.is_object()) continue;
        Requeue(*it);
        it->replay = true;
        it->lastAnswer = answer;
        ++n;
    }
    return n;
}

void GamePlan::Save() {
    json jobs = json::array();
    for (const auto& j : jobs_) {
        jobs.push_back({{"id", j.id}, {"kind", j.kind}, {"subject", j.subject}, {"status", j.status}, {"attempts", j.attempts},
                        {"after", j.after}, {"error", j.error}, {"problems", j.problems}, {"decision", j.decision},
                        {"lastAnswer", j.lastAnswer}, {"replay", j.replay}, {"replayed", j.replayed}});
    }
    json state = {{"schema", "aige.plan-state/1"}, {"stage", stage_},         {"status", status_},     {"message", message_},
                  {"request", request_},           {"choices", choices_},     {"buildMethod", buildMethod_},     {"jobs", jobs},            {"paused", paused_},     {"usage", usage_},
                  {"concept", concept_},           {"toReview", toReview_},   {"findings", findings_}, {"nextJob", nextJob_},
                  {"stale", Stale()}, {"resume", resume_}, {"autoFixed", autoFixed_}, {"fixRounds", fixRounds_}, {"fixScope", fixScope_},
                  {"pausedForClose", pausedForClose_}};
    const json old = store_.State();
    if (old.contains("baseline")) state["baseline"] = old["baseline"];
    store_.SaveState(state);
}

void GamePlan::Load() {
    const json s = store_.State();
    stage_ = s.value("stage", "");
    status_ = s.value("status", "");
    message_ = s.value("message", "");
    request_ = s.value("request", "");
    choices_ = s.value("choices", json::object());
    buildMethod_ = s.contains("buildMethod") && s["buildMethod"].is_string() ? s["buildMethod"].get<std::string>() : "";
    paused_ = s.value("paused", false);
    usage_ = s.value("usage", usage_);
    concept_ = s.value("concept", json::object());
    toReview_ = s.value("toReview", json::array());
    findings_ = s.value("findings", json::array());
    nextJob_ = s.value("nextJob", 1);
    resume_ = s.value("resume", json());
    autoFixed_ = s.value("autoFixed", false);
    fixRounds_ = s.value("fixRounds", autoFixed_ ? 1 : 0);
    fixScope_ = s.value("fixScope", std::string("errors"));
    pausedForClose_ = s.value("pausedForClose", false);
    jobs_.clear();
    for (const auto& j : s.value("jobs", json::array())) {
        Job job;
        job.id = j.value("id", "");
        job.kind = j.value("kind", "");
        job.subject = j.value("subject", "");
        job.status = j.value("status", "queued");
        job.attempts = j.value("attempts", 0);
        job.error = j.value("error", "");
        job.problems = j.value("problems", json::array());
        job.lastAnswer = j.value("lastAnswer", json());
        job.replay = j.value("replay", false);
        job.replayed = j.value("replayed", false);
        job.decision = j.value("decision", "");
        for (const auto& a : j.value("after", json::array())) job.after.push_back(a.get<std::string>());
        jobs_.push_back(std::move(job));
    }
}

std::string GamePlan::AddJob(const std::string& kind, const std::string& subject, std::vector<std::string> after) {
    Job job;
    job.id = "j-" + std::to_string(nextJob_++);
    job.kind = kind;
    job.subject = subject;
    job.after = std::move(after);
    // The same thing made again (Regenerate, a critic's rewrite): its earlier failed try is
    // replaced, not tried again beside it.
    for (auto& j : jobs_) {
        if (j.kind == kind && j.subject == subject && j.status == "failed") j.status = "skipped";
    }
    jobs_.push_back(job);
    return job.id;
}

GamePlan::Job* GamePlan::FindJob(const std::string& id) {
    for (auto& j : jobs_) {
        if (j.id == id) return &j;
    }
    return nullptr;
}

std::string GamePlan::StageOfJob(const std::string& kind) const {
    static const std::map<std::string, std::string> stages = {
        {"interpret", "concept"}, {"outline", "outline"},       {"act", "story"},       {"scene", "story"},
        {"critic", "story"},      {"character", "characters"}, {"quests", "quests"},   {"quest", "quests"},   {"place", "places"},
        {"systems", "systems"},   {"visuals", "visuals"},      {"review", "validate"}, {"report", "validate"}};
    auto it = stages.find(kind);
    return it == stages.end() ? "" : it->second;
}

std::string GamePlan::NextStage(const std::string& stage) const {
    const auto& order = StageOrder();
    auto it = std::find(order.begin(), order.end(), stage);
    return it == order.end() || it + 1 == order.end() ? "done" : *(it + 1);
}

bool GamePlan::StageOn(const std::string& stage) const {
    if (stage == "concept" || stage == "outline" || stage == "validate" || stage == "done") return true;
    const GenreProfile* profile = PlannerData::Engine().Profile(Brief().value("genre", ""));
    if (!profile || !profile->stages.is_object()) return true; // a profile without stage settings: every stage on
    const auto it = profile->stages.find(stage);
    return it == profile->stages.end() || !it->is_string() || it->get<std::string>() != "off";
}

bool GamePlan::StageJobsDone(const std::string& stage) const {
    for (const auto& j : jobs_) {
        if (StageOfJob(j.kind) == stage && (j.status == "queued" || j.status == "running")) return false;
    }
    return true;
}

void GamePlan::Pump() {
    // A paused, stopped or finished plan makes no AI calls; re-applying a logged answer isn't one.
    const bool replaysOnly = paused_ || status_ == "failed" || status_ == "done";
    if (replaysOnly && std::none_of(jobs_.begin(), jobs_.end(), [](const Job& j) { return j.status == "queued" && j.replay; })) return;
    int running = 0;
    for (const auto& j : jobs_) running += j.status == "running";
    const int budget = BriefGet(Brief(), "plan.budget.calls").is_number() ? BriefGet(Brief(), "plan.budget.calls").get<int>() : 0;
    // Scenes are written in story order, one at a time: each reads what the earlier ones made
    // true (a retried s1-01 goes before s1-03, whatever order the jobs were added in).
    std::vector<size_t> visit(jobs_.size());
    for (size_t i = 0; i < visit.size(); ++i) visit[i] = i;
    std::map<std::string, int> sceneOrder;
    {
        const auto order = Graph().SceneOrder();
        for (size_t i = 0; i < order.size(); ++i) sceneOrder[order[i]] = static_cast<int>(i);
    }
    auto orderOf = [&](const Job& j) {
        const auto it = sceneOrder.find(j.subject);
        return it == sceneOrder.end() ? 1 << 30 : it->second;
    };
    {
        std::vector<size_t> slots, scenes;
        for (size_t i = 0; i < jobs_.size(); ++i) {
            if (jobs_[i].kind == "scene") slots.push_back(i), scenes.push_back(i);
        }
        std::stable_sort(scenes.begin(), scenes.end(), [&](size_t a, size_t b) { return orderOf(jobs_[a]) < orderOf(jobs_[b]); });
        for (size_t k = 0; k < slots.size(); ++k) visit[slots[k]] = scenes[k];
    }
    for (const size_t i : visit) {
        if (jobs_[i].status != "queued") continue;
        if (replaysOnly && !jobs_[i].replay) continue;
        const bool ready = std::all_of(jobs_[i].after.begin(), jobs_[i].after.end(), [&](const std::string& id) {
            const Job* dep = FindJob(id);
            return !dep || (dep->status != "queued" && dep->status != "running");
        });
        if (!ready) continue;
        if (jobs_[i].kind == "scene" && std::any_of(jobs_.begin(), jobs_.end(), [&](const Job& j) {
                return &j != &jobs_[i] && j.kind == "scene" && (j.status == "queued" || j.status == "running") && orderOf(j) < orderOf(jobs_[i]);
            })) {
            continue; // an earlier scene first
        }
        if (jobs_[i].kind == "report") {
            if (std::any_of(jobs_.begin(), jobs_.end(), [&](const Job& j) {
                    return j.kind != "report" && (j.status == "queued" || j.status == "running");
                })) {
                continue; // after the fixes and remakes it checks (another report isn't work to wait for)
            }
            // One report does for all that are waiting.
            for (auto& j : jobs_) {
                if (&j != &jobs_[i] && j.kind == "report" && j.status == "queued") j.status = "skipped";
            }
            LocalJob(jobs_[i]);
            return; // LocalJob moves the plan on (and pumps again)
        }
        if (running >= std::max(1, host_.parallel)) break;
        if (!jobs_[i].replay && budget > 0 && usage_.value("calls", 0) >= budget) {
            paused_ = true;
            status_ = "paused";
            message_ = "The plan's budget of " + std::to_string(budget) + " AI calls is used up: raise it (Plan tab) to go on.";
            Save();
            return;
        }
        bool sent = false;
        try {
            sent = Send(jobs_[i]);
        } catch (const std::exception& error) {
            // Its request couldn't be put together (a document of a shape nothing expected): this
            // step fails, with Retry; the plan goes on.
            Finish(jobs_[i], false, std::string("its request couldn't be prepared (") + error.what() + ")");
            continue;
        }
        if (!sent) {
            if (jobs_[i].replay) continue; // waits for the AI service (Kick), quietly
            if (jobs_[i].kind == "interpret") {
                // Without the AI the Brief comes from the request's own words.
                for (auto& j : jobs_) {
                    if (j.kind == "interpret") j.status = "skipped";
                }
                message_ = "The AI isn't available: the Brief below comes from your request's words alone. Check each field.";
                AfterStage("concept");
                return;
            }
            Fail("The AI isn't available: the plan waits. Turn the AI on and choose Continue.");
            jobs_[i].status = "queued";
            return;
        }
        ++running;
    }
    Save();
}

bool GamePlan::Send(Job& job) {
    if (!host_.request) return false;
    json body = RequestBody(job);
    if (job.replay && job.lastAnswer.is_object()) {
        body["replay"] = job.lastAnswer;
        job.attempts = std::max(job.attempts, 1); // so it's handled as a last try: what fits is kept
    }
    if (!job.problems.empty()) body["problems"] = job.problems;
    // A retry with the answer it fixes: the AI changes only what the problems name (quick, no
    // thinking pass), instead of writing the whole thing again.
    if (!job.problems.empty() && job.lastAnswer.is_object()) body["answer"] = job.lastAnswer;
    // What the progress strip calls it ("④ Character: mira").
    const auto stage = StageLabels().find(StageOfJob(job.kind));
    std::string label = stage == StageLabels().end() ? job.kind : stage->second;
    if (job.subject != job.kind && !job.subject.empty()) {
        label += " · " + (job.kind == "interpret" ? job.subject + " reading" : (job.kind == "critic" ? std::string("review") : job.kind) + ": " + PlanSlug(job.subject));
    }
    body["label"] = label;
    const int id = host_.request(PathFor(job), body, 600);
    if (id == 0) return false;
    if (pending_.empty() || !busySince_) busySince_ = std::chrono::steady_clock::now(); // the AI busy from now
    pending_[id] = job.id;
    job.status = "running";
    ++job.attempts;
    if (!body.contains("replay")) usage_["calls"] = usage_.value("calls", 0) + 1; // a replay calls no model
    return true;
}

void GamePlan::OnResponse(int requestId, const json& body) {
    auto it = pending_.find(requestId);
    if (it == pending_.end()) return;
    const std::string jobId = it->second;
    pending_.erase(it);
    if (pending_.empty() && busySince_) {
        // Nothing in flight now: the time it was busy, once however many calls overlapped.
        const double busy = std::chrono::duration<double>(std::chrono::steady_clock::now() - *busySince_).count();
        usage_["seconds"] = usage_.value("seconds", 0.0) + busy;
        busySince_.reset();
    }
    Job* job = FindJob(jobId);
    if (!job) return;
    if (job->status != "running") return; // cancelled meanwhile (Stop fix): its answer is ignored
    if (job->replay) {
        job->replay = false;
        job->replayed = true;
    }
    if (body.contains("usage") && body["usage"].is_object()) {
        for (const char* k : {"input_tokens", "inputTokens"}) usage_["inputTokens"] = usage_.value("inputTokens", 0) + body["usage"].value(k, 0);
        for (const char* k : {"output_tokens", "outputTokens"}) usage_["outputTokens"] = usage_.value("outputTokens", 0) + body["usage"].value(k, 0);
    }
    json logged = body;
    logged.erase("usage");
    const std::string call = store_.Log({{"job", job->id}, {"kind", job->kind}, {"subject", job->subject}, {"path", PathFor(*job)},
                                         {"attempt", job->attempts}, {"answer", logged}});
    if (!body.value("ok", false)) {
        std::string error = "the AI's answer didn't come";
        if (body.contains("errors") && body["errors"].is_array() && !body["errors"].empty()) error = body["errors"][0].dump();
        else if (body.contains("error")) error = body["error"].is_string() ? body["error"].get<std::string>() : body["error"].dump();
        if (job->attempts < 3) {
            job->status = "queued"; // tried again
            job->error = error;
        } else {
            Finish(*job, false, error);
        }
        Save();
        Pump();
        return;
    }
    json answered = body;
    answered["call"] = call;
    // Kept for a quick fix if it has problems (only the answer, not the bookkeeping).
    job->lastAnswer = body;
    for (const char* k : {"ok", "usage", "call", "problems", "errors", "rationale", "label"}) job->lastAnswer.erase(k);
    std::vector<std::string> problems;
    try {
        problems = Handle(*job, answered);
    } catch (const std::exception& error) {
        // A value of the wrong kind somewhere in the answer (true where a scene id goes): the
        // answer goes back like any other problem, the editor carries on.
        problems = {std::string("the answer has a value of the wrong kind (") + error.what() + ")"};
    }
    if (!problems.empty()) Retry(*job, problems);
    else Finish(*job, true);
    Save();
    Pump();
}

void GamePlan::Retry(Job& job, const std::vector<std::string>& problems) {
    if (job.attempts < 2) {
        // Back once with the exact problems (place-interaction.md B1's repair round).
        job.problems = json(problems);
        job.status = "queued";
        return;
    }
    for (const auto& p : problems) Finding(StageOfJob(job.kind), job.subject, p, "error");
    // Failed, not done: the stage goes on without it (unless it's the outline or an act), and
    // Retry (or Continue) tries it again.
    Finish(job, false, problems.empty() ? "" : problems.front());
}

void GamePlan::Finish(Job& job, bool ok, const std::string& error) {
    // The findings this job was fixing: off the list when it made its item, back to open if not.
    if (auto report = store_.Read("report"); report && report->contains("findings") && (*report)["findings"].is_array()) {
        bool changed = false;
        for (auto& f : (*report)["findings"]) {
            if (!f.is_object() || f.value("fixJob", json()) != job.id || f.value("status", json()) != "fixing") continue;
            f["status"] = ok ? "fixed" : "open";
            f.erase("fixJob");
            changed = true;
        }
        if (changed) store_.Touch("report", *report, "");
    }
    job.status = ok ? "done" : "failed";
    job.error = error;
    job.problems = json::array();
    if (ok) job.lastAnswer = json(); // a failed one keeps it: Retry corrects it instead of starting over
    // Something that couldn't be made is an error; a correction that didn't come through leaves
    // the item as it was (its finding is open again), and isn't one.
    const std::string madeDoc = DocOfJob(job);
    const bool correction = job.kind == "revise" || (!madeDoc.empty() && store_.Exists(madeDoc));
    if (!ok && !correction) Finding(StageOfJob(job.kind), job.subject, "couldn't be made: " + error, "error");
    // An outline or an act that couldn't be made stops the plan; one that couldn't be corrected (a
    // fix) keeps what it had, and the plan goes on.
    const bool existed = job.kind == "outline" ? store_.Exists("outline") : store_.Exists("story/acts/" + PlanSlug(job.subject));
    if (!ok && (job.kind == "outline" || job.kind == "act") && !existed) {
        Fail("The " + StageLabels().at(StageOfJob(job.kind)) + " stage couldn't finish (" + error + "). Regenerate it to try again.");
        return;
    }
    const std::string stage = StageOfJob(job.kind);
    if (stage == stage_ && status_ == "working" && StageJobsDone(stage)) AfterStage(stage);
    // Regenerating, revising or retrying: back to what the plan was doing once nothing is left.
    const bool idle = std::none_of(jobs_.begin(), jobs_.end(), [](const Job& j) { return j.status == "queued" || j.status == "running"; });
    if (idle && status_ == "working" && resume_.is_object()) {
        status_ = resume_.value("status", "waiting");
        message_ = resume_.value("message", "");
        if (status_ == "done" && job.kind != "revise") message_ = "Regenerated. Validate again (⑨) to refresh the report.";
        resume_ = json();
    } else if (idle && status_ == "working" && stage_ == "done") {
        status_ = "done";
        if (job.kind != "revise") message_ = "Regenerated. Validate again (⑨) to refresh the report.";
    }
    if (status_ != "working") resume_ = json(); // the stage itself moved on
}

void GamePlan::Requeue(Job& job) {
    job.status = "queued";
    job.attempts = 0;
    // Its last answer, if it had one, goes back with what was wrong with it (a quick fix, not a
    // new start); a step that never had an answer is asked afresh.
    job.problems = job.lastAnswer.is_object() && !job.error.empty() ? json::array({job.error}) : json::array();
    if (!job.problems.empty() && job.lastAnswer.empty()) job.problems = json::array();
    job.error.clear();
    // Its findings go: it's being made again.
    const std::string stage = StageOfJob(job.kind);
    json kept = json::array();
    for (const auto& f : findings_) {
        if (!(f.value("subject", "") == job.subject && f.value("stage", "") == stage)) kept.push_back(f);
    }
    findings_ = kept;
}

void GamePlan::BeginExtraWork() {
    if ((status_ == "waiting" || status_ == "done") && !resume_.is_object()) resume_ = {{"status", status_}, {"message", message_}};
    if (status_ == "waiting" || status_ == "done" || status_ == "failed") status_ = "working";
}

std::string GamePlan::DocOfJob(const Job& job) const {
    if (job.kind == "outline") return "outline";
    if (job.kind == "act" || job.kind == "critic") return "story/acts/" + PlanSlug(job.subject);
    if (job.kind == "scene") return "story/scenes/" + PlanSlug(job.subject);
    if (job.kind == "character") return "characters/" + PlanSlug(job.subject);
    if (job.kind == "quests") return "quests";
    if (job.kind == "quest") {
        for (const auto& doc : store_.List("quests")) {
            const json q = store_.Read(doc).value_or(json::object());
            if (q.is_object() && q.value("slot", json()) == job.subject) return doc;
        }
        return "quests/" + job.subject;
    }
    if (job.kind == "place") return "places/" + PlanSlug(job.subject);
    if (job.kind == "systems") return "systems";
    if (job.kind == "visuals") return "ui";
    if (job.kind == "review" || job.kind == "report") return "report";
    return {};
}

std::string GamePlan::RetryJob(const std::string& id) {
    Job* job = FindJob(id);
    if (!job) return "there's no step " + id;
    if (job->status != "failed") return "only a step that failed is tried again";
    if (paused_) {
        paused_ = false; // retrying is going on
        if (status_ == "paused") status_ = "working";
    }
    // A stopped plan (its outline or an act failed): everything that failed goes again.
    if (status_ == "failed") return RetryFailed();
    Requeue(*job);
    BeginExtraWork();
    message_ = "Trying again…";
    Save();
    Pump();
    return {};
}

std::string GamePlan::RetryFailed() {
    // Retrying is going on: a paused plan resumes (else the retry would wait unseen).
    if (paused_) {
        paused_ = false;
        if (status_ == "paused") status_ = "working";
    }
    int n = 0;
    // One try per thing: of two failed jobs for the same scene (a plan from before AddJob
    // replaced them), the latest goes again, the older is retired.
    std::set<std::pair<std::string, std::string>> seen;
    for (auto it = jobs_.rbegin(); it != jobs_.rend(); ++it) {
        if (it->status != "failed") continue;
        if (!seen.insert({it->kind, it->subject}).second) {
            it->status = "skipped";
            continue;
        }
        Requeue(*it);
        // The AI's answer is in the log and hasn't been re-applied: that first (no model call).
        if (!it->replayed) {
            if (json answer = LoggedAnswer(it->kind, it->subject); answer.is_object()) {
                it->replay = true;
                it->lastAnswer = answer;
            }
        }
        ++n;
    }
    if (n == 0 && status_ != "failed") return "nothing failed";
    if (status_ == "failed") {
        status_ = "working";
        resume_ = json();
    } else {
        BeginExtraWork();
    }
    message_ = "Trying again…";
    Save();
    Pump();
    return {};
}

std::string GamePlan::StopFixes() {
    std::set<std::string> fixJobs;
    auto report = store_.Read("report");
    if (report && report->contains("findings") && (*report)["findings"].is_array()) {
        for (auto& f : (*report)["findings"]) {
            if (!f.is_object() || f.value("status", json()) != "fixing") continue;
            if (f.contains("fixJob") && f["fixJob"].is_string()) fixJobs.insert(f["fixJob"].get<std::string>());
            f["status"] = "open";
            f.erase("fixJob");
        }
        store_.Touch("report", *report, "");
    }
    // At ⑨ every job is a fix's (or the check after them); elsewhere only those the findings name.
    int stopped = 0;
    for (auto& j : jobs_) {
        if (j.status != "queued" && j.status != "running") continue;
        if (stage_ != "validate" && !fixJobs.contains(j.id) && j.kind != "report") continue;
        j.status = "skipped";
        ++stopped;
    }
    for (auto it = pending_.begin(); it != pending_.end();) {
        const Job* j = FindJob(it->second);
        it = j && j->status == "skipped" ? pending_.erase(it) : std::next(it);
    }
    resume_ = json();
    if (stage_ == "validate") {
        status_ = "waiting";
        message_ = "Fixing stopped: what wasn't fixed is open again.";
    }
    Save();
    return stopped == 0 && fixJobs.empty() ? "no fix is running" : std::string();
}

int GamePlan::Busy() const {
    if (paused_ || status_ != "working") return 0;
    return static_cast<int>(std::count_if(jobs_.begin(), jobs_.end(), [](const Job& j) { return j.status == "queued" || j.status == "running"; }));
}

void GamePlan::PauseForClose() {
    if (Busy() == 0) return;
    for (auto& j : jobs_) {
        if (j.status == "running") j.status = "queued"; // its answer won't come now: asked again next time
    }
    pending_.clear();
    paused_ = true;
    pausedForClose_ = true;
    status_ = "paused";
    message_ = "Paused when the editor closed: it carries on when the project is opened again.";
    Save();
}

void GamePlan::Kick() {
    if (status_ == "failed" && message_.rfind("The AI isn't available", 0) == 0) {
        status_ = "working";
        message_ = "The AI is back: carrying on…";
        Save();
    }
    Pump();
}

void GamePlan::Fail(const std::string& message) {
    status_ = "failed";
    message_ = message;
    Save();
}

void GamePlan::Finding(const std::string& stage, const std::string& subject, const std::string& message, const std::string& severity) {
    findings_.push_back({{"stage", stage}, {"subject", subject}, {"message", message}, {"severity", severity}});
}

void GamePlan::AfterStage(const std::string& stage) {
    if (stage == "concept") {
        MergeConcept();
        status_ = "waiting";
        if (message_.empty() || message_ == "Reading your request…") message_ = "Check the Game Brief, then Continue.";
        Save();
        return;
    }
    if (stage == "validate") return; // the report decides (LocalJob)
    store_.Commit(stage);
    const std::string autonomy = BriefGet(Brief(), "plan.autonomy." + stage).is_string()
                                     ? BriefGet(Brief(), "plan.autonomy." + stage).get<std::string>()
                                     : "review";
    if (autonomy == "confirm" || autonomy == "ask") { // "ask": plans from before it was renamed
        status_ = "waiting";
        message_ = "Review " + StageLabels().at(stage) + ", then Continue.";
        Save();
        return;
    }
    if (autonomy == "review") toReview_.push_back(stage);
    BeginStage(NextStage(stage));
}

void GamePlan::BeginStage(const std::string& stage) {
    stage_ = stage;
    status_ = "working";
    message_ = StageLabels().count(stage) ? StageLabels().at(stage) + "…" : "";
    if (stage == "done") {
        status_ = "done";
        message_ = "The design is ready.";
        Save();
        return;
    }
    if (!StageOn(stage)) {
        AfterStage(stage);
        return;
    }
    const StoryGraph graph = Graph();
    if (stage == "outline") {
        AddJob("outline", "outline");
    } else if (stage == "story") {
        std::string previous;
        for (const auto& act : Outline().value("story", json::object()).value("acts", json::array())) {
            previous = AddJob("act", act.value("id", ""), previous.empty() ? std::vector<std::string>{} : std::vector<std::string>{previous});
        }
    } else if (stage == "characters") {
        // Major characters first; each band side by side.
        std::vector<std::pair<int, std::string>> cast;
        static const std::map<std::string, int> rank = {{"major", 0}, {"supporting", 1}, {"minor", 2}, {"extra", 3}};
        for (const auto* n : graph.OfClass("Character", gso_)) {
            auto r = rank.find(n->Text("importance"));
            cast.push_back({r == rank.end() ? 2 : r->second, n->id});
        }
        std::sort(cast.begin(), cast.end());
        for (const auto& [r, id] : cast) {
            if (!store_.Locked("characters/" + PlanSlug(id))) AddJob("character", id);
        }
    } else if (stage == "quests") {
        // One call per quest (the main quest, then each side quest): a small answer each, and a
        // fix or retry touches only that quest.
        if (!store_.Locked("quests/q-main")) AddJob("quest", "q-main");
        const json side = BriefGet(Brief(), "scope.sideQuests");
        int sides = 3;
        if (side.is_array() && side.size() == 2 && side[0].is_number() && side[1].is_number())
            sides = static_cast<int>(std::lround((side[0].get<double>() + side[1].get<double>()) / 2.0));
        else if (side.is_number()) sides = side.get<int>();
        const GenreProfile* profile = PlannerData::Engine().Profile(Brief().value("genre", json("")).is_string() ? Brief().value("genre", "") : "");
        if (profile && profile->stages.is_object() && profile->stages.value("quests", json("full")) == "main-only") sides = 0;
        for (int i = 1; i <= std::clamp(sides, 0, 12); ++i) AddJob("quest", "q-side-" + std::to_string(i));
    } else if (stage == "places") {
        for (const auto* n : graph.OfClass("Location", gso_)) {
            if (!store_.Locked("places/" + PlanSlug(n->id))) AddJob("place", n->id);
        }
    } else if (stage == "systems") {
        AddJob("systems", "systems");
    } else if (stage == "visuals") {
        AddJob("visuals", "ui");
    } else if (stage == "validate") {
        autoFixed_ = false;
        fixRounds_ = 0;
        fixScope_ = "errors";
        const std::string review = AddJob("review", "review");
        AddJob("report", "report", {review});
    }
    Save();
    if (StageJobsDone(stage)) {
        AfterStage(stage); // nothing to do in it
        return;
    }
    Pump();
}

std::string GamePlan::Approve() {
    if (status_ == "failed") return RetryFailed(); // go on: what failed is tried again
    if (status_ != "waiting") return "nothing is waiting";
    if (stage_ == "concept") return Confirm();
    if (stage_ == "validate") {
        status_ = "working";
        autoFixed_ = false;
        fixRounds_ = 0;
        fixScope_ = "errors";
        AddJob("report", "report");
        Pump();
        return {};
    }
    PlanDecision d;
    d.stage = stage_;
    d.actor = "user";
    d.kind = "approve";
    d.subject = stage_;
    store_.Decide(d);
    BeginStage(NextStage(stage_));
    return {};
}

void GamePlan::Pause(bool paused) {
    paused_ = paused;
    if (paused) {
        status_ = "paused";
        message_ = "Paused.";
    } else if (status_ == "paused") {
        status_ = "working";
        message_.clear();
    }
    Save();
    if (!paused) Pump();
}

// --- ① The Brief --------------------------------------------------------------------------------

void GamePlan::MergeConcept() {
    std::vector<json> drafts;
    for (const auto& d : concept_.value("drafts", json::array())) drafts.push_back(d);
    if (drafts.empty()) drafts.push_back({{"reading", "typical"}, {"brief", FallbackGameBrief(request_)}, {"confidence", json::object()}});
    json marks;
    json merged = MergeGameBriefs(drafts, &marks);
    std::vector<std::string> assumed;
    json brief = CompleteGameBrief(merged, store_.Defaults(), &assumed);
    brief["assumed"] = assumed;
    // What the readings put in a field that isn't one of its options goes back to the profile's.
    const auto repaired = RepairGameBrief(brief, gso_, store_.Defaults());
    // The user's choices (the home page's): as chosen, whatever the readings made of them; not assumed.
    if (!choices_.empty()) {
        json assumedNow = json::array();
        for (const auto& a : brief.value("assumed", json::array())) {
            if (!choices_.contains(a.get<std::string>())) assumedNow.push_back(a);
        }
        for (const auto& [path, value] : choices_.items()) BriefSet(brief, path, value);
        brief["assumed"] = assumedNow;
    }
    brief["request"] = request_;
    brief["confirmed"] = false;
    concept_["marks"] = marks;
    json toDecide = json::array(), assumptions = json::array();
    std::set<std::string> asked;
    for (const auto& d : drafts) {
        for (const auto& q : d.value("decisions", json::array())) {
            if (q.is_object() && asked.insert(q.value("question", "")).second) toDecide.push_back(q);
        }
        for (const auto& a : d.value("assumptions", json::array())) assumptions.push_back(a);
    }
    concept_["toDecide"] = toDecide;
    for (const auto& r : repaired) assumptions.push_back("Replaced " + r + " (the reading suggested something that isn't an option)");
    concept_["assumptions"] = assumptions;
    int differ = 0;
    for (const auto& [p, m] : marks.items()) differ += m.contains("differ");
    PlanDecision d;
    d.stage = "concept";
    d.actor = "ai";
    d.kind = "merge";
    d.subject = "brief";
    d.chosen = brief.value("title", "");
    d.rationale = "Merged " + std::to_string(drafts.size()) + " readings of the request; " + std::to_string(differ) +
                  " fields differ between them and " + std::to_string(assumed.size()) + " come from the genre profile and scope table.";
    store_.Write("brief", brief, store_.Decide(d));
}

void GamePlan::EditBrief(const std::string& path, const json& value) {
    json brief = Brief();
    if (brief.empty()) return;
    auto assumed = brief.value("assumed", json::array());
    json kept = json::array();
    std::vector<std::string> stillAssumed;
    for (const auto& a : assumed) {
        if (a.get<std::string>() != path) {
            kept.push_back(a);
            stillAssumed.push_back(a.get<std::string>());
        }
    }
    BriefSet(brief, path, value);
    if (path == "genre") {
        // A new genre: the fields the user hasn't set take its profile's defaults.
        for (const auto& a : stillAssumed) BriefSet(brief, a, nullptr);
        std::vector<std::string> refilled;
        brief = CompleteGameBrief(brief, store_.Defaults(), &refilled);
        kept = refilled;
    } else if (path == "playTime") {
        for (const char* k : {"scopeClass", "scope.places", "scope.scenes", "scope.mainQuestStages", "scope.sideQuests"}) {
            if (std::find(stillAssumed.begin(), stillAssumed.end(), k) != stillAssumed.end()) BriefSet(brief, k, nullptr);
        }
        brief = CompleteGameBrief(brief, store_.Defaults());
    }
    brief["assumed"] = kept;
    if (concept_.contains("marks")) concept_["marks"].erase(path);
    PlanDecision d;
    d.stage = "concept";
    d.actor = "user";
    d.kind = "edit";
    d.subject = "brief:" + path;
    d.chosen = value.dump();
    if (path.rfind("plan.", 0) == 0 && brief.value("confirmed", false)) {
        // How the plan runs (autonomy, building, art, budget), not what it designs: written in place,
        // so nothing made from the Brief goes stale.
        store_.Decide(d);
        std::ofstream(store_.PathOf("brief"), std::ios::binary) << brief.dump(2) << "\n";
    } else {
        store_.Touch("brief", brief, store_.Decide(d));
    }
    Save();
}

std::string GamePlan::Confirm() {
    if (stage_ != "concept" || status_ != "waiting") return "the Brief isn't waiting for confirmation";
    json brief = Brief();
    // A Brief saved with values that aren't options (from an earlier reading): put right first.
    if (const auto repaired = RepairGameBrief(brief, gso_, store_.Defaults()); !repaired.empty()) {
        PlanDecision fix;
        fix.stage = "concept";
        fix.actor = "rule";
        fix.kind = "fix";
        fix.subject = "brief";
        for (const auto& r : repaired) fix.chosen += (fix.chosen.empty() ? "" : "; ") + r;
        fix.rationale = "Values that aren't among the fields' options were replaced by the genre profile's.";
        store_.Touch("brief", brief, store_.Decide(fix));
    }
    const auto problems = ValidateGameBrief(brief, gso_);
    if (HasErrors(problems)) {
        std::string out;
        for (const auto& p : ProblemTexts(problems)) out += (out.empty() ? "" : "; ") + p;
        return out;
    }
    brief["confirmed"] = true;
    PlanDecision d;
    d.stage = "concept";
    d.actor = "user";
    d.kind = "confirm";
    d.subject = "brief";
    d.chosen = brief.value("title", "");
    store_.Write("brief", brief, store_.Decide(d));
    // The answers that should hold for the next plan too.
    json defaults = store_.Defaults();
    for (const char* k : {"plan", "audience", "artStyle", "visuals"}) {
        if (brief.contains(k)) defaults[k] = brief[k];
    }
    store_.SaveDefaults(defaults);
    store_.Commit("concept");
    BeginStage("outline");
    return {};
}

// --- Items ------------------------------------------------------------------------------------

std::vector<std::string> GamePlan::Stale() const {
    std::vector<std::string> out = store_.Stale();
    for (const auto& doc : store_.List("story/scenes")) {
        const auto j = store_.Read(doc);
        if (j && j->value("needsRewrite", false)) out.push_back(doc);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    out.erase(std::remove_if(out.begin(), out.end(), [&](const std::string& d) { return d == "report" || store_.Locked(d); }), out.end());
    return out;
}

std::string GamePlan::Edit(const std::string& doc, const std::string& pointer, const json& value, const std::string& reason) {
    if (doc == "brief" && stage_ == "concept") {
        std::string path = pointer;
        if (!path.empty() && path[0] == '/') path = path.substr(1);
        std::replace(path.begin(), path.end(), '/', '.');
        EditBrief(path, value);
        return {};
    }
    auto content = store_.Read(doc);
    if (!content) return "no " + doc;
    try {
        (*content)[json::json_pointer(pointer)] = value;
    } catch (const std::exception& e) {
        return std::string("can't change ") + pointer + ": " + e.what();
    }
    if (doc.rfind("story/scenes/", 0) == 0 && pointer.rfind("/card", 0) == 0) (*content)["needsRewrite"] = true;
    PlanDecision d;
    d.stage = StageOfJob(doc.rfind("story/", 0) == 0 ? "scene" : doc.rfind("characters/", 0) == 0 ? "character"
                                                    : doc.rfind("quests/", 0) == 0    ? "quests"
                                                    : doc.rfind("places/", 0) == 0    ? "place"
                                                                                       : "outline");
    d.actor = "user";
    d.kind = "edit";
    d.subject = doc + pointer;
    d.chosen = value.dump();
    d.reason = reason;
    store_.Touch(doc, *content, store_.Decide(d));
    // A character's or place's structured facts follow its document.
    if (doc.rfind("characters/", 0) == 0 || doc.rfind("places/", 0) == 0) {
        StoryGraph g = Graph();
        const std::string node = (doc.rfind("characters/", 0) == 0 ? "character/" : "location/") + PlanSlug(doc);
        if (StoryNode* n = g.Find(node)) {
            const json bio = content->value("biography", json::object());
            if (bio.contains("need")) n->fields["need"] = bio["need"];
            if (bio.contains("desire")) n->fields["desire"] = bio["desire"];
            if (bio.contains("motivations")) n->fields["motivations"] = bio["motivations"];
            if (content->contains("name")) n->fields["name"] = (*content)["name"];
            SaveGraph(g);
        }
    }
    Save();
    return {};
}

std::string GamePlan::Lock(const std::string& doc, bool locked) {
    auto content = store_.Read(doc);
    if (!content) return "no " + doc;
    if (locked) (*content)["locked"] = true;
    else content->erase("locked");
    // Locking isn't a change of content: written in place, the revision kept.
    PlanDecision d;
    d.stage = stage_;
    d.actor = "user";
    d.kind = locked ? "lock" : "unlock";
    d.subject = doc;
    store_.Decide(d);
    std::ofstream(store_.PathOf(doc), std::ios::binary) << content->dump(2) << "\n";
    return {};
}

std::string GamePlan::Regenerate(const std::string& doc) {
    if (store_.Locked(doc)) return doc + " is locked";
    std::string kind, subject;
    if (doc == "outline") kind = "outline", subject = "outline";
    else if (doc.rfind("story/acts/", 0) == 0) kind = "act", subject = PlanSlug(doc);
    else if (doc.rfind("story/scenes/", 0) == 0) kind = "scene", subject = PlanSlug(doc);
    else if (doc.rfind("characters/", 0) == 0) kind = "character", subject = "character/" + PlanSlug(doc);
    else if (doc.rfind("quests/", 0) == 0) {
        // One quest: made again in its slot (the main quest, or that side quest).
        const json q = store_.Read(doc).value_or(json::object());
        kind = "quest", subject = q.is_object() && q.value("slot", json()).is_string() && !q["slot"].get<std::string>().empty()
                                      ? q["slot"].get<std::string>() : PlanSlug(doc);
    }
    else if (doc.rfind("places/", 0) == 0) kind = "place", subject = "location/" + PlanSlug(doc);
    else if (doc == "systems") kind = "systems", subject = "systems";
    else if (doc == "ui") kind = "visuals", subject = "ui";
    else if (doc == "report") kind = "review", subject = "review";
    else return "can't regenerate " + doc;
    // One job per subject at a time.
    for (const auto& j : jobs_) {
        if (j.kind == kind && j.subject == subject && (j.status == "queued" || j.status == "running")) return {};
    }
    if (kind == "scene") UndoScene(subject);
    PlanDecision d;
    d.stage = StageOfJob(kind);
    d.actor = "user";
    d.kind = "regenerate";
    d.subject = doc;
    store_.Decide(d);
    const std::string id = AddJob(kind, subject);
    if (kind == "review") AddJob("report", "report", {id});
    // Regenerating after the stage: work on it without moving the plan's stage.
    BeginExtraWork();
    Save();
    Pump();
    return {};
}

std::string GamePlan::RegenerateStale() {
    const auto stale = Stale();
    if (stale.empty()) return "nothing is stale";
    for (const auto& doc : stale) Regenerate(doc);
    return {};
}

std::string GamePlan::Revise(const std::string& instruction) {
    if (!store_.Exists("outline")) return "the plan has no outline yet";
    PlanDecision d;
    d.stage = stage_;
    d.actor = "user";
    d.kind = "revise";
    d.subject = "plan";
    d.chosen = instruction;
    store_.Decide(d);
    AddJob("revise", instruction);
    if (status_ == "done" || status_ == "waiting") status_ = "working";
    Save();
    Pump();
    return {};
}

bool GamePlan::AboutThePlan(const std::string& text) const {
    if (!Active() || stage_ == "concept") return false;
    const std::string words = Lower(text);
    const StoryGraph g = Graph();
    for (const auto& n : g.nodes) {
        const std::string name = Lower(n.Text("name"));
        if (name.size() >= 3 && words.find(name) != std::string::npos && (gso_.IsA(n.cls, "Character") || n.cls == "Location")) return true;
    }
    for (const auto& doc : store_.List("quests")) {
        const std::string name = Lower(store_.Read(doc).value_or(json::object()).value("name", ""));
        if (name.size() >= 3 && words.find(name) != std::string::npos) return true;
    }
    return false;
}

json GamePlan::Document(const std::string& doc) const { return store_.Read(doc).value_or(json()); }

std::string GamePlan::DocOfNode(const std::string& node) const {
    if (node.rfind("character/", 0) == 0) return "characters/" + PlanSlug(node);
    if (node.rfind("location/", 0) == 0) return "places/" + PlanSlug(node);
    if (store_.Exists("story/scenes/" + node)) return "story/scenes/" + node;
    if (store_.Exists("story/acts/" + node)) return "story/acts/" + node;
    if (store_.Exists("quests/" + node)) return "quests/" + node;
    return node;
}

json GamePlan::Why(const std::string& docOrNode) const {
    std::string doc = store_.Exists(docOrNode) ? docOrNode : DocOfNode(docOrNode);
    std::string madeBy;
    json content = store_.Read(doc).value_or(json::object());
    madeBy = content.value("madeBy", "");
    if (madeBy.empty()) {
        // A graph node made by a scene, a character...: its own madeBy.
        const StoryGraph g = Graph();
        madeBy = g.MadeBy(docOrNode);
    }
    json out = {{"subject", docOrNode}, {"document", doc}};
    if (content.contains("rationale")) out["rationale"] = content["rationale"];
    if (content.contains("revision")) out["revision"] = content["revision"];
    if (content.contains("inputs")) out["inputs"] = content["inputs"];
    if (auto d = store_.Decision(madeBy)) {
        out["decision"] = d->ToJson();
        if (!out.contains("rationale") && !d->rationale.empty()) out["rationale"] = d->rationale;
    }
    json history = json::array();
    for (const auto& d : store_.Decisions()) {
        if (d.subject == doc || d.subject == docOrNode || d.subject.rfind(doc + "/", 0) == 0) history.push_back(d.ToJson());
    }
    out["history"] = history;
    return out;
}

std::string GamePlan::Restore(int snapshot) {
    if (!store_.Restore(snapshot)) return "no snapshot " + std::to_string(snapshot);
    PlanDecision d;
    d.stage = stage_;
    d.actor = "user";
    d.kind = "restore";
    d.subject = "history/" + std::to_string(snapshot);
    store_.Decide(d);
    Save();
    return {};
}

// --- Views ------------------------------------------------------------------------------------

json GamePlan::Card() const {
    int done = 0, total = 0;
    for (const auto& j : jobs_) {
        if (StageOfJob(j.kind) != stage_) continue;
        ++total;
        done += j.status == "done" || j.status == "failed" || j.status == "skipped";
    }
    const json brief = Brief();
    const StoryGraph g = Graph();
    std::string headline = brief.value("title", "A new game");
    if (stage_ != "concept") {
        headline += ": " + std::to_string(g.SceneOrder().size()) + " scenes, " + std::to_string(g.OfClass("Character", gso_).size()) +
                    " characters, " + std::to_string(store_.List("quests").size()) + " quests, " +
                    std::to_string(g.OfClass("Location", gso_).size()) + " places";
    }
    return {{"stage", stage_},
            {"stageLabel", StageLabels().count(stage_) ? StageLabels().at(stage_) : stage_},
            {"status", status_},
            {"message", message_},
            {"progress", {{"done", done}, {"total", total}}},
            {"headline", headline}};
}

// Fields of the plan's documents, read for showing: whatever the AI left there (an object where
// a text goes, a text where a number does) shows as best it can, never throws.
std::string TextOf(const json& j, const char* key) {
    if (!j.is_object()) return {};
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) return {};
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number() || it->is_boolean()) return it->dump();
    std::string out; // an object or a list: its texts, joined
    for (const auto& v : *it) {
        if (v.is_string() && !v.get<std::string>().empty()) out += (out.empty() ? "" : "; ") + v.get<std::string>();
    }
    return out.empty() ? it->dump() : out;
}
bool BoolOf(const json& j, const char* key) {
    if (!j.is_object()) return false;
    const auto it = j.find(key);
    return it != j.end() && (it->is_boolean() ? it->get<bool>() : it->is_number() ? it->get<double>() != 0 : false);
}
// A document as an object: anything else (missing, null, a list) reads as empty.
json ObjectOf(const std::optional<json>& doc) { return doc && doc->is_object() ? *doc : json::object(); }
double NumOf(const json& j, const char* key) {
    if (!j.is_object()) return 0.0;
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : 0.0;
}

json GamePlan::State() const {
    const json brief = Brief();
    const StoryGraph graph = Graph();
    const auto issues = CheckStory(graph, gso_);
    const auto stale = Stale();
    auto isStale = [&](const std::string& doc) { return std::find(stale.begin(), stale.end(), doc) != stale.end(); };
    // An issue a later stage resolves (a place's parts: ⑥ Places; a character's package: ④; a
    // quest's stages: ⑤) is a note while that stage hasn't run, not a warning to worry about.
    const auto& stageOrder = StageOrder();
    const auto stageIndex = [&](const std::string& s) {
        return static_cast<int>(std::find(stageOrder.begin(), stageOrder.end(), s) - stageOrder.begin());
    };
    auto laterStage = [&](const std::string& rule) -> std::string {
        const std::string owner = rule.rfind("R-P", 0) == 0 ? "places" : rule.rfind("R-K", 0) == 0 ? "characters" : rule.rfind("R-Q", 0) == 0 ? "quests" : "";
        if (owner.empty() || !StageOn(owner)) return {};
        const int at = stageIndex(stage_), of = stageIndex(owner);
        return of > at || (of == at && status_ == "working") ? owner : std::string();
    };
    auto issuesOf = [&](const std::string& node) {
        json out = json::array();
        for (const auto& i : issues) {
            if (i.node == node && laterStage(i.rule).empty()) out.push_back(i.rule + ": " + i.message);
        }
        return out;
    };
    auto notesOf = [&](const std::string& node) {
        json out = json::array();
        for (const auto& i : issues) {
            if (i.node != node) continue;
            if (const std::string later = laterStage(i.rule); !later.empty()) out.push_back("Handled in " + StageLabels().at(later) + ": " + i.message);
        }
        return out;
    };

    json stages = json::array();
    const auto& order = StageOrder();
    const auto current = std::find(order.begin(), order.end(), stage_);
    for (auto it = order.begin(); it != order.end() && *it != "done"; ++it) {
        int done = 0, total = 0, staleCount = 0, failedCount = 0;
        for (const auto& j : jobs_) {
            if (StageOfJob(j.kind) != *it) continue;
            ++total;
            done += j.status == "done" || j.status == "failed" || j.status == "skipped";
            failedCount += j.status == "failed";
        }
        for (const auto& s : stale) {
            const std::string stageOf = s == "outline"                     ? "outline"
                                        : s.rfind("story/", 0) == 0        ? "story"
                                        : s.rfind("characters/", 0) == 0   ? "characters"
                                        : s.rfind("quests/", 0) == 0       ? "quests"
                                        : s.rfind("places/", 0) == 0       ? "places"
                                        : s == "systems"                   ? "systems"
                                        : s == "ui"                        ? "visuals"
                                                                           : "";
            staleCount += stageOf == *it;
        }
        std::string status = "pending";
        if (current != order.end() && it < current) status = "done";
        if (*it == stage_) status = status_;
        if (!StageOn(*it)) status = "skipped";
        stages.push_back({{"id", *it}, {"label", StageLabels().at(*it)}, {"status", status}, {"done", done}, {"total", total}, {"failed", failedCount},
                          {"stale", staleCount}, {"review", std::find(toReview_.begin(), toReview_.end(), *it) != toReview_.end()}});
    }

    // ③ Story: acts, chapters, scene rows. Each scene's latest job says where it is: planned (a
    // card), queued, being written, rewritten after the act's review, failed or written.
    std::map<std::string, const Job*> sceneJobs;
    for (const auto& j : jobs_) {
        if (j.kind == "scene") sceneJobs[j.subject] = &j;
    }
    int scenesWritten = 0, scenesTotal = 0;
    json acts = json::array();
    for (const auto& actDoc : store_.List("story/acts")) {
        const json act = ObjectOf(store_.Read(actDoc));
        json chapters = json::array();
        for (const auto& ch : act.value("chapters", json::array())) {
            if (!ch.is_object()) continue;
            json scenes = json::array();
            for (const auto& sid : ch.value("scenes", json::array())) {
                if (!sid.is_string()) continue;
                const std::string doc = "story/scenes/" + sid.get<std::string>();
                const json s = ObjectOf(store_.Read(doc));
                const json card = s.value("card", json::object());
                const bool written = TextOf(s, "status") == "written";
                const auto jt = sceneJobs.find(sid.get<std::string>());
                const Job* sj = jt == sceneJobs.end() ? nullptr : jt->second;
                const bool rewrite = static_cast<int>(NumOf(s, "criticRewrites")) > 0;
                std::string progress = written ? "written" : "planned";
                if (sj && sj->status == "queued") progress = rewrite ? "rewrite-queued" : "queued";
                else if (sj && sj->status == "running") progress = rewrite ? "rewriting" : "writing";
                else if (sj && sj->status == "failed" && !written) progress = "failed";
                ++scenesTotal;
                scenesWritten += written;
                scenes.push_back({{"id", sid},
                                  {"doc", doc},
                                  {"order", NumOf(s, "order")},
                                  {"mode", TextOf(card, "mode")},
                                  {"kernel", BoolOf(card, "kernel")},
                                  {"branch", TextOf(card, "branch")},
                                  {"location", TextOf(card, "location")},
                                  {"goal", TextOf(card, "goal")},
                                  {"summary", TextOf(s, "summary")},
                                  {"written", written},
                                  {"progress", progress},
                                  {"job", sj ? sj->id : ""},
                                  {"error", sj && sj->status == "failed" ? sj->error : ""},
                                  {"attempt", sj ? sj->attempts : 0},
                                  {"criticNote", TextOf(s, "criticNote")},
                                  {"stale", isStale(doc)},
                                  {"locked", BoolOf(s, "locked")},
                                  {"issues", issuesOf(sid.get<std::string>())}, {"notes", notesOf(sid.get<std::string>())}});
            }
            chapters.push_back({{"id", TextOf(ch, "id")}, {"question", TextOf(ch, "question")}, {"summary", TextOf(ch, "summary")},
                                {"scenes", scenes}});
        }
        acts.push_back({{"id", PlanSlug(actDoc)}, {"doc", actDoc}, {"summary", TextOf(act, "summary")}, {"chapters", chapters},
                        {"stale", isStale(actDoc)}, {"locked", BoolOf(act, "locked")}});
    }
    // ④ ⑤ ⑥ rows.
    json characters = json::array();
    for (const auto* n : graph.OfClass("Character", gso_)) {
        const std::string doc = "characters/" + PlanSlug(n->id);
        const json c = ObjectOf(store_.Read(doc));
        characters.push_back({{"id", n->id},
                              {"doc", doc},
                              {"name", n->Text("name").empty() ? n->id : n->Text("name")},
                              {"importance", n->Text("importance")},
                              {"player", n->cls == "PlayerCharacter"},
                              {"line", n->Text("summary")},
                              {"detailed", store_.Exists(doc)},
                              {"roles", c.contains("gameplay") && c["gameplay"].is_object() ? c["gameplay"].value("roles", json::array()) : json::array()},
                              {"stale", isStale(doc)},
                              {"locked", BoolOf(c, "locked")},
                              {"issues", issuesOf(n->id)}, {"notes", notesOf(n->id)}});
    }
    json quests = json::array();
    for (const auto& doc : store_.List("quests")) {
        const json q = ObjectOf(store_.Read(doc));
        quests.push_back({{"id", (TextOf(q, "id").empty() ? PlanSlug(doc) : TextOf(q, "id"))}, {"doc", doc}, {"name", TextOf(q, "name")}, {"type", TextOf(q, "type")},
                          {"stages", q.value("stages", json::array()).size()}, {"motivation", TextOf(q, "motivation")},
                          {"strategy", TextOf(q, "strategy")}, {"stale", isStale(doc)}, {"locked", BoolOf(q, "locked")},
                          {"issues", issuesOf((TextOf(q, "id").empty() ? PlanSlug(doc) : TextOf(q, "id")))}, {"notes", notesOf((TextOf(q, "id").empty() ? PlanSlug(doc) : TextOf(q, "id")))}});
    }
    json places = json::array();
    for (const auto* n : graph.OfClass("Location", gso_)) {
        const std::string doc = "places/" + PlanSlug(n->id);
        const json p = ObjectOf(store_.Read(doc));
        json problems = json::array();
        if (host_.places && !p.empty()) {
            for (const auto& t : ProblemTexts(ValidatePlaceDesc(p, host_.places()))) problems.push_back(t);
        }
        places.push_back({{"id", n->id},
                          {"doc", doc},
                          {"name", n->Text("name").empty() ? n->id : n->Text("name")},
                          {"kind", n->Text("buildType")},
                          {"described", store_.Exists(doc)},
                          {"purpose", TextOf(p.value("description", json::object()), "purpose")},
                          {"problems", problems},
                          {"stale", isStale(doc)},
                          {"locked", BoolOf(p, "locked")},
                          {"issues", issuesOf(n->id)}, {"notes", notesOf(n->id)}});
    }
    int queued = 0, running = 0, failed = 0, done = 0;
    // The report with each open finding's remedy (worked out now: a report made before remedies
    // existed gets Fix buttons too).
    json reportShown = store_.Read("report").value_or(json());
    if (reportShown.is_object() && reportShown.contains("findings") && reportShown["findings"].is_array()) {
        // The step each finding comes from, by its rule (a failed step's: from the stage that had it).
        auto stepOf = [&](const json& f) -> std::string {
            const std::string rule = TextOf(f, "rule");
            std::string stage;
            if (rule.rfind("R-S", 0) == 0 || rule.rfind("R-C", 0) == 0 || rule.rfind("R-T", 0) == 0) stage = "story";
            else if (rule.rfind("R-K", 0) == 0 || rule == "X-character") stage = "characters";
            else if (rule.rfind("R-Q", 0) == 0) stage = "quests";
            else if (rule.rfind("R-P", 0) == 0 || rule == "X-place" || rule == "X-anchor") stage = "places";
            else if (rule == "X-item" || rule == "X-system") stage = "systems";
            else if (rule == "X-screen") stage = "visuals";
            else if (rule == "critic") stage = "validate";
            else if (rule == "stage") {
                for (const auto& s : findings_) {
                    if (TextOf(s, "subject") == TextOf(f, "subject") && TextOf(s, "message") == TextOf(f, "message")) stage = TextOf(s, "stage");
                }
            }
            if (stage == "validate") return "⑨ Review";
            return StageLabels().count(stage) ? StageLabels().at(stage) : std::string();
        };
        for (auto& f : reportShown["findings"]) {
            f["step"] = stepOf(f);
            f.erase("remedy"); // the current one, not one an older report stored
            if (TextOf(f, "status") != "open") continue;
            if (const json remedy = RemedyOf(f); remedy.is_object()) f["remedy"] = remedy;
        }
    }
    json failedJobs = json::array(); // each failed step: what, why, Retry
    for (const auto& j : jobs_) {
        if (j.status != "failed") continue;
        failedJobs.push_back({{"id", j.id}, {"kind", j.kind}, {"subject", PlanSlug(j.subject)}, {"stage", StageOfJob(j.kind)},
                              {"error", j.error}, {"doc", DocOfJob(j)}});
    }
    for (const auto& j : jobs_) {
        queued += j.status == "queued";
        running += j.status == "running";
        failed += j.status == "failed";
        done += j.status == "done";
    }
    return {{"active", Active()},
            {"stage", stage_},
            {"status", status_},
            {"message", message_},
            {"paused", paused_},
            {"request", request_},
            {"stages", stages},
            {"brief", brief},
            {"window", brief.empty() ? json::array() : BriefWindow(brief, concept_.value("marks", json::object()), gso_)},
            {"toDecide", concept_.value("toDecide", json::array())},
            {"assumptions", concept_.value("assumptions", json::array())},
            {"outline", Outline()},
            {"acts", acts},
            {"characters", characters},
            {"quests", quests},
            {"places", places},
            {"systems", store_.Read("systems").value_or(json())},
            {"ui", store_.Read("ui").value_or(json())},
            {"report", reportShown},
            {"findings", findings_},
            {"stale", stale},
            {"jobs", {{"queued", queued}, {"running", running}, {"failed", failed}, {"done", done}}},
            {"failedJobs", failedJobs},
            {"runningJobs", [&] {
                 json ids = json::array(); // what's being worked on now (⑨ shows which finding's fix)
                 for (const auto& j : jobs_) {
                     if (j.status == "running") ids.push_back(j.id);
                 }
                 return ids;
             }()},
            {"storyProgress", {{"written", scenesWritten}, {"total", scenesTotal}}},
            {"usage", [this] {
                 // With the time of the calls in flight now (State is asked about once a second while it works).
                 json u = usage_;
                 if (busySince_) u["seconds"] = u.value("seconds", 0.0) + std::chrono::duration<double>(std::chrono::steady_clock::now() - *busySince_).count();
                 return u;
             }()},
            {"decisions", store_.Decisions().size()},
            {"history", store_.History()},
            {"baseline", store_.State().value("baseline", json())},
            {"card", Card()}};
}

// --- Helpers ----------------------------------------------------------------------------------

json GamePlan::Brief() const { return store_.Read("brief").value_or(json::object()); }
json GamePlan::Outline() const { return store_.Read("outline").value_or(json::object()); }

StoryGraph GamePlan::Graph() const {
    return StoryGraph::Load(StoryGraph::PathIn(host_.projectRoot)).value_or(StoryGraph{});
}

bool GamePlan::SaveGraph(const StoryGraph& graph) { return graph.Save(StoryGraph::PathIn(host_.projectRoot)); }

std::vector<std::string> GamePlan::SceneIds() const { return Graph().SceneOrder(); }

void GamePlan::WorldStub(const std::string& key, const std::string& name, const std::string& summary, const std::string& body) {
    const size_t slash = key.find('/');
    if (slash == std::string::npos) return;
    WorldBible bible{Project{host_.projectRoot}};
    bible.Refresh();
    WorldEntry entry;
    if (const WorldEntry* existing = bible.Find(key)) entry = *existing;
    entry.kind = key.substr(0, slash);
    entry.id = key.substr(slash + 1);
    if (entry.name.empty()) entry.name = name.empty() ? entry.id : name;
    if (!summary.empty()) entry.summary = summary;
    if (!body.empty()) entry.body = body;
    bible.Save(entry);
}

} // namespace aige
