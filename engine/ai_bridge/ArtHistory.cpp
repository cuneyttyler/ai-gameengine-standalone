#include "ai_bridge/ArtHistory.h"

#include <chrono>
#include <ctime>
#include <fstream>

namespace aige {

namespace {

std::string NowIso() {
    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}

} // namespace

ArtHistory::ArtHistory(std::filesystem::path file, size_t maxEntries)
    : file_(std::move(file)), maxEntries_(maxEntries) {}

void ArtHistory::Load() {
    entries_ = nlohmann::json::array();
    nextId_ = 1;
    std::ifstream in(file_);
    if (!in) {
        return;
    }
    nlohmann::json data = nlohmann::json::parse(in, nullptr, false);
    if (!data.is_object() || !data.contains("entries") || !data["entries"].is_array()) {
        return;
    }
    for (auto& entry : data["entries"]) {
        if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_number_integer()) {
            continue;
        }
        // A request still drawing when the editor closed never finished.
        if (entry.value("state", "") == "working") {
            entry["state"] = "failed";
            entry["errors"] = {"The editor closed before the images came back"};
        }
        nextId_ = std::max(nextId_, entry["id"].get<int>() + 1);
        entries_.push_back(entry);
    }
}

int ArtHistory::Begin(const nlohmann::json& request) {
    const int id = nextId_++;
    entries_.push_back({{"id", id},
                        {"created", NowIso()},
                        {"request", request},
                        {"state", "working"},
                        {"errors", nlohmann::json::array()},
                        {"results", nlohmann::json::array()}});
    if (entries_.size() > maxEntries_) {
        entries_.erase(entries_.begin(), entries_.begin() + static_cast<long>(entries_.size() - maxEntries_));
    }
    Save();
    return id;
}

void ArtHistory::Finish(int id, const nlohmann::json& result) {
    for (auto& entry : entries_) {
        if (entry["id"] != id) {
            continue;
        }
        const bool ok = result.is_object() && result.value("ok", false);
        entry["state"] = ok ? "done" : "failed";
        entry["errors"] = result.is_object() ? result.value("errors", nlohmann::json::array()) : nlohmann::json::array();
        nlohmann::json results = nlohmann::json::array();
        if (result.is_object()) {
            for (const auto& asset : result.value("assets", nlohmann::json::array())) {
                if (asset.is_object() && asset.contains("path") && asset["path"].is_string()) {
                    results.push_back({{"path", asset["path"]}, {"discarded", false}});
                }
            }
        }
        entry["results"] = results;
        Save();
        return;
    }
}

void ArtHistory::Discarded(const std::string& path) {
    bool changed = false;
    for (auto& entry : entries_) {
        for (auto& result : entry["results"]) {
            if (result.value("path", "") == path && !result.value("discarded", false)) {
                result["discarded"] = true;
                changed = true;
            }
        }
    }
    if (changed) {
        Save();
    }
}

void ArtHistory::Moved(const std::string& from, const std::string& to) {
    bool changed = false;
    for (auto& entry : entries_) {
        for (auto& result : entry["results"]) {
            if (result.value("path", "") == from) {
                result["path"] = to;
                changed = true;
            }
        }
    }
    if (changed) {
        Save();
    }
}

nlohmann::json ArtHistory::Entries() const {
    nlohmann::json out = nlohmann::json::array();
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        out.push_back(*it);
    }
    return out;
}

void ArtHistory::Clear() {
    entries_ = nlohmann::json::array();
    std::error_code ec;
    std::filesystem::remove(file_, ec);
}

void ArtHistory::Save() const {
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    std::ofstream out(file_);
    out << nlohmann::json{{"version", 1}, {"entries", entries_}}.dump();
}

} // namespace aige
