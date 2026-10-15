#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace aige {

// Per-user editor preferences — how this person likes to work, not anything
// about the game, so they live outside the project folder. A small JSON file
// (DefaultPath()), read on construction and written through on every Set.
// An empty path keeps them in memory only (tests).
class UserPrefs {
public:
    explicit UserPrefs(std::filesystem::path file);

    // %LOCALAPPDATA%\aige\editor.json (empty if it can't be determined).
    static std::filesystem::path DefaultPath();

    bool Has(const std::string& key) const { return values_.contains(key); }
    nlohmann::json Get(const std::string& key, const nlohmann::json& fallback = nullptr) const;
    void Set(const std::string& key, const nlohmann::json& value);

private:
    std::filesystem::path file_;
    nlohmann::json values_ = nlohmann::json::object();
};

} // namespace aige
