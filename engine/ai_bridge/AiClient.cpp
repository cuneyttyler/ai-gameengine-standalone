#include "ai_bridge/AiClient.h"

#include <httplib.h>

#include <chrono>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace aige {

namespace {

constexpr int kStartupTimeoutSeconds = 45; // first launch may need uv to set up the environment
constexpr int kProposeTimeoutSeconds = 600; // a thinking model (Claude) can take minutes, with its retries

nlohmann::json Failure(const std::string& message) {
    return {{"ok", false}, {"errors", nlohmann::json::array({message})}};
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

AiClient::AiClient(Options options) : options_(std::move(options)) {
    worker_ = std::thread([this] { Worker(); });
}

AiClient::~AiClient() {
    {
        std::lock_guard lock(mutex_);
        quitting_ = true;
        jobs_.clear();
    }
    wake_.notify_all();
    StopProcess(); // first: a request still waiting on a model then fails at once, and the worker can finish
    if (worker_.joinable()) {
        worker_.join();
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
    jobs_.push_back([this, schema = std::move(schema)] { RunStart(schema); });
    wake_.notify_one();
}

int AiClient::Propose(const std::string& instruction, nlohmann::json context, nlohmann::json history) {
    std::lock_guard lock(mutex_);
    int id = nextRequestId_++;
    nlohmann::json request = {{"instruction", instruction}, {"context", std::move(context)}, {"history", std::move(history)}};
    jobs_.push_back([this, id, request = std::move(request)] { RunPropose(id, request); });
    wake_.notify_one();
    return id;
}

int AiClient::Request(const std::string& method, const std::string& path, nlohmann::json body, int timeoutSeconds) {
    std::lock_guard lock(mutex_);
    int id = nextRequestId_++;
    jobs_.push_back([this, id, method, path, body = std::move(body), timeoutSeconds] {
        if (state_ != State::Ready) {
            Push({Result::Kind::Response, id, Failure(StatusText())});
            return;
        }
        std::string error;
        auto response = Send(method, path, &body, timeoutSeconds, &error);
        Push({Result::Kind::Response, id, response ? *response : Failure(error)});
    });
    wake_.notify_one();
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
    std::lock_guard lock(mutex_);
    results_.push_back(std::move(result));
}

void AiClient::Worker() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return quitting_ || !jobs_.empty(); });
            if (quitting_) {
                return;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        job();
    }
}

std::optional<nlohmann::json> AiClient::Send(const std::string& method, const std::string& path,
                                             const nlohmann::json* body, int timeoutSeconds, std::string* error) {
    httplib::Client client(options_.host, options_.port);
    client.set_connection_timeout(2);
    client.set_read_timeout(timeoutSeconds);
    client.set_write_timeout(10);
    httplib::Result response = method == "GET"   ? client.Get(path)
                               : method == "PUT" ? client.Put(path, body->dump(), "application/json")
                                                 : client.Post(path, body->dump(), "application/json");
    if (!response) {
        if (error) *error = "can't reach the AI service (" + httplib::to_string(response.error()) + ")";
        return std::nullopt;
    }
    if (response->status != 200) {
        if (error) *error = "the AI service answered HTTP " + std::to_string(response->status);
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
        setStatus(State::Failed, "AI service running, but no model provider is configured (set MISTRAL_API_KEY in "
                                 "ai_service/.env)");
        return;
    }
    setStatus(State::Ready, "AI ready (" + health->value("model", std::string("unknown model")) + ")");
}

void AiClient::RunPropose(int id, const nlohmann::json& request) {
    if (state_ != State::Ready) {
        Push({Result::Kind::Proposal, id, Failure(StatusText())});
        return;
    }
    std::string error;
    auto response = Send("POST", "/propose", &request, kProposeTimeoutSeconds, &error);
    Push({Result::Kind::Proposal, id, response ? *response : Failure(error)});
}

bool AiClient::LaunchProcess(std::string* error) {
#ifdef _WIN32
    std::string command = options_.launchCommand;
    auto at = command.find("{port}");
    if (at != std::string::npos) {
        command.replace(at, 6, std::to_string(options_.port));
    }
    std::wstring wideCommand(command.begin(), command.end());
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
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, directory.c_str(), &startup, &handle->info)) {
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
