#include "ai_bridge/AiClient.h"

#include <httplib.h>

#include <cctype>
#include <cwchar>
#include <functional>
#include <random>

#include <chrono>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace aige {

namespace {

constexpr int kStartupTimeoutSeconds = 45; // first launch may need uv to set up the environment
constexpr int kProposeTimeoutSeconds = 5 * 3600; // a thinking model can take minutes with its retries, and an answer that draws
                                                 // and animates people (generate) can take hours

nlohmann::json Failure(const std::string& message) {
    return {{"ok", false}, {"errors", nlohmann::json::array({message})}};
}

// Lost, not refused: the service wasn't there (stopped, not started, gone mid-answer). Said so ("unreachable"), so
// what asked can wait for it and ask again instead of taking it as the answer.
nlohmann::json Unreachable(const std::string& message) {
    nlohmann::json f = Failure(message);
    f["unreachable"] = true;
    return f;
}
bool Lost(const std::string& error) { return error.rfind("can't reach the AI service", 0) == 0; }

std::string LowerText(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// What an error says when a provider's account is out of credits or over its quota, and whose it is.
bool OutOfCredits(const std::string& lower) {
    static const char* const said[] = {"insufficient generations and credits", "insufficient credits", "insufficient credit",
                                       "requires more credits", "not enough credits", "out of credits", "no credits",
                                       "credit balance is too low", "credits: 0.0", "insufficient_quota", "exceeded your current quota",
                                       "quota exceeded", "payment required", "answered 402", "http 402", "status 402", "error 402",
                                       "insufficient balance", "billing hard limit"};
    for (const char* s : said) {
        if (lower.find(s) != std::string::npos) return true;
    }
    return false;
}

std::string ProviderIn(const std::string& lower) {
    static const std::pair<const char*, const char*> names[] = {
        {"openrouter", "OpenRouter"}, {"lmstudio", "LM Studio"}, {"lm studio", "LM Studio"}, {"pixellab", "PixelLab"},   {"bfl", "Black Forest Labs (FLUX)"},
        {"flux", "Black Forest Labs (FLUX)"}, {"black forest", "Black Forest Labs (FLUX)"}, {"autosprite", "Autosprite"},
        {"anthropic", "Anthropic"},   {"claude", "Anthropic"},    {"openai", "OpenAI"},  {"gemini", "Google Gemini"},
        {"elevenlabs", "ElevenLabs"}, {"replicate", "Replicate"}, {"fal.ai", "fal"},     {"stability", "Stability AI"}};
    for (const auto& [key, name] : names) {
        if (lower.find(key) != std::string::npos) return name;
    }
    return "An AI provider";
}

} // namespace

// The sidecar process. On Windows it's put in a job object that kills it
// when the editor exits, however the editor exits (crash included), so no
// orphaned Python processes accumulate.
struct AiClient::ProcessHandle {
#ifdef _WIN32
    PROCESS_INFORMATION info{};
    HANDLE job = nullptr;

    ~ProcessHandle() {
        if (info.hProcess) {
            TerminateProcess(info.hProcess, 0);
            CloseHandle(info.hProcess);
            CloseHandle(info.hThread);
        }
        if (job) {
            CloseHandle(job); // kills anything left in the job (uv's child python)
        }
    }
#endif
};

namespace {

// A port nothing listens on (the system's pick), for this editor's own service.
int FreePort(int fallback) {
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return fallback;
    int port = fallback;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET) {
        sockaddr_in at{};
        at.sin_family = AF_INET;
        at.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        at.sin_port = 0;
        int length = sizeof(at);
        if (bind(s, reinterpret_cast<sockaddr*>(&at), sizeof(at)) == 0 && getsockname(s, reinterpret_cast<sockaddr*>(&at), &length) == 0) {
            port = ntohs(at.sin_port);
        }
        closesocket(s);
    }
    WSACleanup();
    return port;
#else
    return fallback + 1;
#endif
}

} // namespace

AiClient::AiClient(Options options) : options_(std::move(options)) {
    // A fresh secret each session (128 bits).
    std::random_device random;
    static const char* hex = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) token_ += hex[random() % 16];
    // A few workers: requests to the service run at once (it answers them in parallel). And one for the status polls
    // alone (GET /progress): with four long requests running (the art, a background, its windows), the progress strip
    // waited behind them and showed nothing until they ended.
    for (int k = 0; k < 4; ++k) workers_.emplace_back([this] { Worker(false); });
    workers_.emplace_back([this] { Worker(true); });
}

AiClient::~AiClient() {
    {
        std::lock_guard lock(mutex_);
        quitting_ = true;
        jobs_.clear();
    }
    wake_.notify_all();
    StopProcess(); // first: a request still waiting on a model then fails at once, and the worker can finish
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
}

std::string AiClient::StatusText() const {
    std::lock_guard lock(statusMutex_);
    return statusText_;
}

void AiClient::Start(nlohmann::json schema) {
    state_ = State::Starting;
    {
        std::lock_guard lock(statusMutex_);
        statusText_ = "Starting the AI service…";
    }
    std::lock_guard lock(mutex_);
    jobs_.push_back({true, [this, schema = std::move(schema)] { RunStart(schema); }});
    wake_.notify_all();
}

int AiClient::Propose(const std::string& instruction, nlohmann::json context, nlohmann::json history,
                      const std::vector<std::string>& images) {
    std::lock_guard lock(mutex_);
    int id = nextRequestId_++;
    nlohmann::json request = {{"instruction", instruction}, {"context", std::move(context)}, {"history", std::move(history)}};
    if (!images.empty()) request["images"] = images;
    ++inFlight_;
    jobs_.push_back({false, [this, id, request = std::move(request)] {
        RunPropose(id, request);
        --inFlight_;
    }});
    wake_.notify_all();
    return id;
}

int AiClient::Request(const std::string& method, const std::string& path, nlohmann::json body, int timeoutSeconds) {
    std::lock_guard lock(mutex_);
    int id = nextRequestId_++;
    const bool counted = path != "/progress";
    if (counted) ++inFlight_;
    jobs_.push_back({false, [this, id, method, path, body = std::move(body), timeoutSeconds, counted] {
        if (state_ != State::Ready) {
            Push({Result::Kind::Response, id, Unreachable(StatusText())});
        } else {
            std::string error;
            auto response = Send(method, path, &body, timeoutSeconds, &error);
            Push({Result::Kind::Response, id, response ? *response : Lost(error) ? Unreachable(error) : Failure(error)});
        }
        if (counted) --inFlight_;
    }, !counted});
    wake_.notify_all();
    return id;
}

std::optional<AiClient::Result> AiClient::Poll() {
    std::lock_guard lock(mutex_);
    if (results_.empty()) {
        return std::nullopt;
    }
    Result result = std::move(results_.front());
    results_.pop_front();
    return result;
}

void AiClient::Push(Result result) {
    if (result.kind != Result::Kind::Status) NoteCredits(result.body);
    std::lock_guard lock(mutex_);
    results_.push_back(std::move(result));
}

void AiClient::NoteCredits(const nlohmann::json& body) {
    // Its errors, wherever they are in it (a step's, a call's): a provider out of credits. Only what's under "error",
    // "errors" or "detail" is read (a model's own words about a shop's billing aren't an alert).
    std::string text;
    std::function<void(const nlohmann::json&, bool)> collect = [&](const nlohmann::json& j, bool isError) {
        if (j.is_string()) {
            if (isError) text += j.get<std::string>() + "\n";
        } else if (j.is_array()) {
            for (const auto& x : j) collect(x, isError);
        } else if (j.is_object()) {
            for (const auto& [k, v] : j.items()) collect(v, isError || k == "error" || k == "errors" || k == "detail");
        }
    };
    collect(body, false);
    if (text.empty()) return;
    const std::string lower = LowerText(text);
    if (!OutOfCredits(lower)) return;
    // Who: the provider named nearest the words that said it.
    std::string provider = "An AI provider";
    std::string said;
    for (const char* key : {"credit", "quota", "402", "billing", "payment", "balance"}) {
        const size_t at = lower.find(key);
        if (at == std::string::npos) continue;
        const size_t from = at > 400 ? at - 400 : 0;
        provider = ProviderIn(lower.substr(from, 600));
        if (provider == "An AI provider") provider = ProviderIn(lower);
        // The provider's own words around it, for the detail.
        const size_t s = at > 160 ? at - 160 : 0;
        said = text.substr(s, 320);
        break;
    }
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(mutex_);
    if (const auto it = creditAlerted_.find(provider); it != creditAlerted_.end() && now - it->second < std::chrono::minutes(15)) return;
    creditAlerted_[provider] = now;
    creditAlerts_.push_back({{"provider", provider},
                             {"message", provider + " is out of credits: its calls fail until the account is topped up. Top it up (or "
                                         "choose another model in Preferences ▸ AI), then go on: Implement what's left, or Continue."},
                             {"said", said}});
}

std::optional<nlohmann::json> AiClient::TakeCreditAlert() {
    std::lock_guard lock(mutex_);
    if (creditAlerts_.empty()) return std::nullopt;
    nlohmann::json alert = std::move(creditAlerts_.front());
    creditAlerts_.pop_front();
    return alert;
}

void AiClient::Worker(bool quick) {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            // A start runs alone; requests wait until the service has started (or failed to).
            // The job to take: a start wherever it is in the queue, else the first request once started.
            auto next = [this, quick]() -> std::ptrdiff_t {
                for (size_t i = 0; i < jobs_.size(); ++i) {
                    if (jobs_[i].start) return quick || starting_ ? -1 : static_cast<std::ptrdiff_t>(i);
                }
                if (starting_ || state_ == State::Starting) return -1;
                // This worker's kind of job: the quick one takes status polls only, the others everything else.
                for (size_t i = 0; i < jobs_.size(); ++i) {
                    if (jobs_[i].quick == quick) return static_cast<std::ptrdiff_t>(i);
                }
                return -1;
            };
            wake_.wait(lock, [&] { return quitting_ || next() >= 0; });
            if (quitting_) {
                return;
            }
            const std::ptrdiff_t at = next();
            job = std::move(jobs_[at]);
            jobs_.erase(jobs_.begin() + at);
            if (job.start) starting_ = true;
        }
        job.run();
        if (job.start) {
            {
                std::lock_guard lock(mutex_);
                starting_ = false;
            }
            wake_.notify_all(); // the requests waiting on it go now
        }
    }
}

std::optional<nlohmann::json> AiClient::Send(const std::string& method, const std::string& path,
                                             const nlohmann::json* body, int timeoutSeconds, std::string* error) {
    httplib::Client client(options_.host, options_.port);
    client.set_connection_timeout(2);
    client.set_read_timeout(timeoutSeconds);
    client.set_write_timeout(10);
    const httplib::Headers headers = {{"X-AIGE-Token", token_}};
    httplib::Result response = method == "GET"   ? client.Get(path, headers)
                               : method == "PUT" ? client.Put(path, headers, body->dump(), "application/json")
                                                 : client.Post(path, headers, body->dump(), "application/json");
    if (!response) {
        if (error) *error = "can't reach the AI service (" + httplib::to_string(response.error()) + ")";
        return std::nullopt;
    }
    if (response->status != 200) {
        if (error) {
            *error = "the AI service answered HTTP " + std::to_string(response->status);
            // Its reason, when it gave one ({"errors": [...]}, or FastAPI's {"detail": ...}).
            const nlohmann::json body = nlohmann::json::parse(response->body, nullptr, false);
            if (body.is_object() && body.contains("errors") && body["errors"].is_array() && !body["errors"].empty() && body["errors"][0].is_string()) {
                *error += ": " + body["errors"][0].get<std::string>();
            } else if (body.is_object() && body.contains("detail")) {
                *error += ": " + (body["detail"].is_string() ? body["detail"].get<std::string>() : body["detail"].dump().substr(0, 300));
            }
        }
        return std::nullopt;
    }
    try {
        return nlohmann::json::parse(response->body);
    } catch (const nlohmann::json::exception&) {
        if (error) *error = "the AI service sent something that isn't JSON";
        return std::nullopt;
    }
}

std::optional<nlohmann::json> AiClient::GetJson(const std::string& path, int timeoutSeconds) {
    return Send("GET", path, nullptr, timeoutSeconds, nullptr);
}

void AiClient::RunStart(nlohmann::json schema) {
    auto setStatus = [this](State state, const std::string& text) {
        state_ = state;
        {
            std::lock_guard lock(statusMutex_);
            statusText_ = text;
        }
        Push({Result::Kind::Status, 0, {{"state", static_cast<int>(state)}, {"text", text}}});
    };

    // Reuse a service that's already running (started by hand, or a
    // previous editor), otherwise launch one and wait for it to come up.
    std::optional<nlohmann::json> health = GetJson("/health", 1);
    // One that answers to another token is another editor's (each has its own, and its own
    // project open there): this one starts its own, on a port of its own.
    if (health) {
        std::string refused;
        if (!Send("PUT", "/schema", &schema, 10, &refused) && (refused.find("401") != std::string::npos || refused.find("403") != std::string::npos)) {
            options_.port = FreePort(options_.port + 1);
            health.reset();
        }
    }
    if (!health) {
        if (options_.serviceDir.empty()) {
            setStatus(State::Failed, "AI service isn't running and no service folder is configured");
            return;
        }
        std::string error;
        if (!LaunchProcess(&error)) {
            setStatus(State::Failed, "Couldn't start the AI service: " + error);
            return;
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kStartupTimeoutSeconds);
        while (!health && std::chrono::steady_clock::now() < deadline) {
            {
                std::unique_lock lock(mutex_);
                if (wake_.wait_for(lock, std::chrono::milliseconds(500), [this] { return quitting_; })) {
                    return;
                }
            }
            health = GetJson("/health", 1);
        }
        if (!health) {
            setStatus(State::Failed, "The AI service didn't start within " + std::to_string(kStartupTimeoutSeconds) +
                                         "s (is uv installed? see ai_service/README.md)");
            return;
        }
    }

    std::string error;
    if (!Send("PUT", "/schema", &schema, 10, &error)) {
        setStatus(State::Failed, "Couldn't send the engine schema: " + error);
        return;
    }
    if (health->value("provider", nlohmann::json()).is_null()) {
        setStatus(State::Failed, "AI service running, but there's no key for the language model: set one in "
                                 "Preferences > Beta Settings (Mistral, Anthropic or OpenRouter)");
        return;
    }
    setStatus(State::Ready, "AI ready (" + health->value("model", std::string("unknown model")) + ")");
}

void AiClient::RunPropose(int id, const nlohmann::json& request) {
    if (state_ != State::Ready) {
        Push({Result::Kind::Proposal, id, Unreachable(StatusText())});
        return;
    }
    std::string error;
    auto response = Send("POST", "/propose", &request, kProposeTimeoutSeconds, &error);
    Push({Result::Kind::Proposal, id, response ? *response : Lost(error) ? Unreachable(error) : Failure(error)});
}

bool AiClient::LaunchProcess(std::string* error) {
#ifdef _WIN32
    std::string command = options_.launchCommand;
    auto at = command.find("{port}");
    if (at != std::string::npos) {
        command.replace(at, 6, std::to_string(options_.port));
    }
    // UTF-8 (a path in it may not be ASCII: the package in a user's own folder).
    std::wstring wideCommand(static_cast<size_t>(MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, wideCommand.data(), static_cast<int>(wideCommand.size()));
    if (!wideCommand.empty() && wideCommand.back() == L'\0') wideCommand.pop_back();
    std::wstring directory = options_.serviceDir.wstring();

    auto handle = std::make_unique<ProcessHandle>();
    handle->job = CreateJobObjectW(nullptr, nullptr);
    if (handle->job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(handle->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }

    STARTUPINFOW startup{sizeof(startup)};
    std::wstring mutableCommand = L"cmd.exe /c " + wideCommand; // resolves uv on PATH like a terminal would
    // Its environment: this one's, and the token it answers to.
    std::wstring environment;
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* at = block; *at; at += wcslen(at) + 1) {
            if (_wcsnicmp(at, L"AIGE_SERVICE_TOKEN=", 19) == 0) continue;
            bool given = false; // one of the options' own wins
            for (const auto& [name, value] : options_.environment) {
                const std::wstring prefix = std::wstring(name.begin(), name.end()) + L"=";
                given = given || _wcsnicmp(at, prefix.c_str(), prefix.size()) == 0;
            }
            if (given) continue;
            environment += at;
            environment.push_back(L'\0');
        }
        FreeEnvironmentStringsW(block);
    }
    for (const auto& [name, value] : options_.environment) {
        environment += std::wstring(name.begin(), name.end()) + L"=" + std::wstring(value.begin(), value.end());
        environment.push_back(L'\0');
    }
    environment += L"AIGE_SERVICE_TOKEN=" + std::wstring(token_.begin(), token_.end());
    environment.push_back(L'\0');
    environment.push_back(L'\0');
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, environment.data(), directory.c_str(), &startup,
                        &handle->info)) {
        if (error) *error = "CreateProcess failed (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    if (handle->job) {
        AssignProcessToJobObject(handle->job, handle->info.hProcess);
    }
    ResumeThread(handle->info.hThread);
    process_ = std::move(handle);
    return true;
#else
    if (error) *error = "launching the AI service is only implemented on Windows so far; start it by hand";
    return false;
#endif
}

void AiClient::StopProcess() {
    process_.reset();
}

} // namespace aige
