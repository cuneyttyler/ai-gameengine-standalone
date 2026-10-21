#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace aige {

// The engine-side client of the AI Orchestration Service (technical.md §6,
// §10): starts the Python sidecar, talks to it over localhost HTTP on a
// worker thread, and hands results back to the main thread through Poll(),
// so the editor never blocks on a model call.
class AiClient {
public:
    struct Options {
        std::filesystem::path serviceDir; // ai_service/ (has pyproject.toml); empty = don't launch
        std::string host = "127.0.0.1";
        int port = 8765;
        // How the service is launched (inside serviceDir). uv provides the
        // project's own Python environment.
        std::string launchCommand = "uv run uvicorn app.main:app --host 127.0.0.1 --port {port}";
    };

    explicit AiClient(Options options);
    // The secret the service it starts is given (AIGE_SERVICE_TOKEN) and every request carries
    // (X-AIGE-Token): only this editor is answered (development_plan.md 11).
    const std::string& Token() const { return token_; }
    ~AiClient(); // stops the sidecar it started

    AiClient(const AiClient&) = delete;
    AiClient& operator=(const AiClient&) = delete;

    // Launches the sidecar (unless one already answers on the port) and,
    // once it's healthy, sends it `schema`. Returns immediately; progress
    // arrives through Poll() as "status" results.
    void Start(nlohmann::json schema);

    enum class State { Stopped, Starting, Ready, Failed };
    State GetState() const { return state_; }
    std::string StatusText() const;

    // Queues a request; its answer arrives through Poll() with this id.
    int Propose(const std::string& instruction, nlohmann::json context, nlohmann::json history = nlohmann::json::array());

    // Queues any other call to the service ("PUT /project", "POST /world/search");
    // its answer arrives through Poll() as a Response result with this id.
    int Request(const std::string& method, const std::string& path, nlohmann::json body, int timeoutSeconds = 30);

    struct Result {
        enum class Kind { Status, Proposal, Response };
        Kind kind;
        int requestId = 0;
        nlohmann::json body; // the service's response (or {"ok":false,"errors":[...]})
    };
    // Finished work, on the caller's thread. Call once per frame.
    std::optional<Result> Poll();

    // Blocking GET for tests and diagnostics; nullopt if unreachable.
    std::optional<nlohmann::json> GetJson(const std::string& path, int timeoutSeconds = 2);

private:
    std::string token_;
    struct Job;
    void Worker();
    void RunStart(nlohmann::json schema);
    void RunPropose(int id, const nlohmann::json& request);
    std::optional<nlohmann::json> Send(const std::string& method, const std::string& path, const nlohmann::json* body,
                                       int timeoutSeconds, std::string* error);
    void Push(Result result);
    bool LaunchProcess(std::string* error);
    void StopProcess();

    Options options_;
    std::atomic<State> state_{State::Stopped};
    mutable std::mutex statusMutex_;
    std::string statusText_ = "AI service not started";

    std::mutex mutex_;
    std::condition_variable wake_;
    // Each job; starting the service runs first and alone, requests then run side by side
    // (a long build step doesn't hold up the AI panel).
    struct Job {
        bool start = false;
        std::function<void()> run;
    };
    std::deque<Job> jobs_;
    std::deque<Result> results_;
    bool quitting_ = false;
    bool starting_ = false; // a start job is running
    int nextRequestId_ = 1;
    std::vector<std::thread> workers_;

    struct ProcessHandle;
    std::unique_ptr<ProcessHandle> process_;
};

} // namespace aige
