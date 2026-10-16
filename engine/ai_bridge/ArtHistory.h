#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace aige {

// The Art tab's history (development_plan.md 4.5), in `.cache/art_history.json`:
// every generation request with its settings, what came back (or why not),
// and which of its images were later discarded or moved, so a request can be
// used again after the files are long gone.
class ArtHistory {
public:
    explicit ArtHistory(std::filesystem::path file, size_t maxEntries = 200);

    void Load(); // a missing or broken file is an empty history

    // A request sent to the service; returns its entry id.
    int Begin(const nlohmann::json& request);
    // The service's answer: {ok, assets: [{path, ...}], errors}.
    void Finish(int id, const nlohmann::json& result);
    void Discarded(const std::string& path);
    void Moved(const std::string& from, const std::string& to);

    // Newest first: {id, created, request, state: "working"|"done"|"failed",
    // errors, results: [{path, discarded}]}.
    nlohmann::json Entries() const;

    void Clear();

private:
    void Save() const;

    std::filesystem::path file_;
    size_t maxEntries_;
    nlohmann::json entries_ = nlohmann::json::array(); // oldest first
    int nextId_ = 1;
};

} // namespace aige
