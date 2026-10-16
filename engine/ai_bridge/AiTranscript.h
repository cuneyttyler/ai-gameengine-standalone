#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aige {

// The AI conversation, kept per project (development_plan.md 4.5) in
// `.cache/ai_conversation.json`, so saving, quitting and reopening brings the
// chat back. Two parts:
//  - the panel's events: every host -> UI `ai:*` message that builds the chat
//    (requests, proposals, decisions, task progress, checks), replayed on open;
//  - the model's context: the plain-text turns sent back with follow-ups.
// Proposals can't be applied after a reload (the editor no longer holds
// them); the panel shows those still undecided as expired.
class AiTranscript {
public:
    using Turns = std::vector<std::pair<std::string, std::string>>;

    explicit AiTranscript(std::filesystem::path file, size_t maxEvents = 400);

    // Whether `type` is part of the chat (others, like ai:busy, aren't kept).
    static bool Records(const std::string& type);

    // Reads the file; a missing or broken one is an empty conversation.
    // Starts a new session: this run's request ids restart at 1.
    void Load();

    // Keeps `message` (when Records) along with the model's turns, and saves.
    void Record(const nlohmann::json& message, const Turns& context);

    // The events to replay, oldest first, as `ai:restore {events}`. Earlier
    // runs' ids are rewritten to negative numbers unique per session, so they
    // can't clash with this run's requests; this run's stay as they are.
    nlohmann::json RestoreEvents() const;
    const Turns& Context() const { return context_; }
    bool Empty() const { return events_.empty(); }

    void Clear(); // forgets everything and deletes the file

private:
    void Save() const;

    std::filesystem::path file_;
    size_t maxEvents_;
    int session_ = 1;
    nlohmann::json events_ = nlohmann::json::array(); // {session, message}
    Turns context_;
};

} // namespace aige
