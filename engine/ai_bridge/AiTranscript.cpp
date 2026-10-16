#include "ai_bridge/AiTranscript.h"

#include <fstream>

namespace aige {

namespace {

constexpr int kSessionStride = 1000000;

} // namespace

AiTranscript::AiTranscript(std::filesystem::path file, size_t maxEvents)
    : file_(std::move(file)), maxEvents_(maxEvents) {}

bool AiTranscript::Records(const std::string& type) {
    static const std::set<std::string> kept = {
        "ai:message",     "ai:proposal",    "ai:decided",     "ai:taskProgress", "ai:taskStopped",
        "ai:split",       "ai:consistency", "ai:autoUndone",  "ai:scriptCheck",
    };
    return kept.contains(type);
}

void AiTranscript::Load() {
    events_ = nlohmann::json::array();
    context_.clear();
    session_ = 1;
    std::ifstream in(file_);
    if (!in) {
        return;
    }
    nlohmann::json data = nlohmann::json::parse(in, nullptr, false);
    if (!data.is_object()) {
        return;
    }
    for (const auto& event : data.value("events", nlohmann::json::array())) {
        if (event.is_object() && event.contains("message") && event["message"].is_object()) {
            events_.push_back(event);
            session_ = std::max(session_, event.value("session", 0) + 1);
        }
    }
    for (const auto& turn : data.value("context", nlohmann::json::array())) {
        if (turn.is_array() && turn.size() == 2 && turn[0].is_string() && turn[1].is_string()) {
            context_.emplace_back(turn[0].get<std::string>(), turn[1].get<std::string>());
        }
    }
}

void AiTranscript::Record(const nlohmann::json& message, const Turns& context) {
    if (!Records(message.value("type", ""))) {
        return;
    }
    events_.push_back({{"session", session_}, {"message", message}});
    if (events_.size() > maxEvents_) {
        events_.erase(events_.begin(), events_.begin() + static_cast<long>(events_.size() - maxEvents_));
    }
    context_ = context;
    Save();
}

nlohmann::json AiTranscript::RestoreEvents() const {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& event : events_) {
        nlohmann::json message = event["message"];
        // This run's ids are live (its proposals can still be applied).
        if (event.value("session", 0) != session_ && message.contains("id") && message["id"].is_number_integer()) {
            message["id"] = -(event.value("session", 0) * kSessionStride + message["id"].get<int>());
        }
        out.push_back(std::move(message));
    }
    return out;
}

void AiTranscript::Clear() {
    events_ = nlohmann::json::array();
    context_.clear();
    std::error_code ec;
    std::filesystem::remove(file_, ec);
}

void AiTranscript::Save() const {
    nlohmann::json context = nlohmann::json::array();
    for (const auto& [role, text] : context_) {
        context.push_back({role, text});
    }
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    std::ofstream out(file_);
    out << nlohmann::json{{"version", 1}, {"events", events_}, {"context", context}}.dump();
}

} // namespace aige
