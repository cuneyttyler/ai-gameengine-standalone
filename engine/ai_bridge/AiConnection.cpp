#include "ai_bridge/AiConnection.h"

namespace aige {

namespace {
constexpr const char* kEnabledKey = "aiEnabled";
}

AiConnection::AiConnection(UserPrefs& prefs, LaunchFn launch) : prefs_(prefs), launch_(std::move(launch)) {}

AiChoice AiConnection::Choice() const {
    nlohmann::json value = prefs_.Get(kEnabledKey);
    if (!value.is_boolean()) {
        return AiChoice::Undecided;
    }
    return value.get<bool>() ? AiChoice::On : AiChoice::Off;
}

void AiConnection::Start() {
    if (Choice() == AiChoice::On && !client_) {
        client_ = launch_();
    }
}

void AiConnection::Restart() {
    if (!client_) return;
    client_.reset();
    Start();
}

void AiConnection::SetEnabled(bool enabled) {
    prefs_.Set(kEnabledKey, enabled);
    if (enabled) {
        Start();
    } else {
        client_.reset(); // stops the service; nothing more is sent to the model
    }
}

} // namespace aige
