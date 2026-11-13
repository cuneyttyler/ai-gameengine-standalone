// The user's API keys (Preferences ▸ Beta Settings, docs/beta-release.md): kept per user, beside the
// preferences (%LOCALAPPDATA%/aige/api-keys.json), each encrypted for this Windows user (DPAPI), and
// given to the AI service in its environment when the editor starts it. A key is never shown back in
// full: the panel sees whether it's set, and its last four characters.
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace aige {

struct ApiKeyKind {
    std::string env;      // what the service reads: MISTRAL_API_KEY
    std::string provider; // who it's for: Mistral
    std::string uses;     // what it's used for, in a few words
    std::string url;      // where to get one
};

// Every key the service can use.
const std::vector<ApiKeyKind>& ApiKeyKinds();

class ApiKeys {
public:
    explicit ApiKeys(std::filesystem::path file);
    static std::filesystem::path DefaultPath(); // beside the preferences

    // Sets (or, with an empty value, removes) a key. Returns why not, or "".
    std::string Set(const std::string& env, const std::string& value);
    void Remove(const std::string& env) { Set(env, ""); }

    // Each set key, decrypted: what the service is started with.
    std::map<std::string, std::string> Environment() const;
    // For the panel: [{env, provider, uses, url, set, tail, inEnv}], `inEnv`: given another way (ai_service/.env
    // in a source build, or the system's environment), which a key set here overrides.
    nlohmann::json Status(const std::filesystem::path& serviceDir) const;

private:
    void Save() const;
    std::filesystem::path file_;
    std::map<std::string, std::string> sealed_; // env -> base64 of the encrypted key
};

} // namespace aige
