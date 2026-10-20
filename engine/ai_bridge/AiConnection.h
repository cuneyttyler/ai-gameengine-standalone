#pragma once

#include "ai_bridge/AiClient.h"
#include "ai_bridge/UserPrefs.h"

#include <functional>
#include <memory>

namespace aige {

// Whether the user wants AI at all. Undecided until they answer the
// first-run question; nothing AI-related runs until they say yes.
enum class AiChoice { Undecided, On, Off };

// Owns the AI service's lifetime according to the user's choice (product.md
// §4.1: the editor must be fully usable with AI off, and then nothing may be
// sent to a model). The service is launched only while the choice is On;
// choosing Off stops it. `launch` creates and starts the client — injected so
// tests can check the start logic without spawning a process.
class AiConnection {
public:
    using LaunchFn = std::function<std::unique_ptr<AiClient>()>;

    AiConnection(UserPrefs& prefs, LaunchFn launch);

    // Launches the service if the user has chosen On; otherwise does nothing.
    void Start();
    // Records the choice and starts or stops the service to match.
    void SetEnabled(bool enabled);
    // Stops the service and starts it again (the model changed in Preferences).
    void Restart();

    AiChoice Choice() const;
    // The running client, or null while AI is off or undecided.
    AiClient* Client() { return client_.get(); }

private:
    UserPrefs& prefs_;
    LaunchFn launch_;
    std::unique_ptr<AiClient> client_;
};

} // namespace aige
