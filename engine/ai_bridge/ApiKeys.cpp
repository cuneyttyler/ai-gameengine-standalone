#include "ai_bridge/ApiKeys.h"

#include "ai_bridge/UserPrefs.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <set>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dpapi.h>
#endif

namespace aige {

namespace fs = std::filesystem;
using json = nlohmann::json;

const std::vector<ApiKeyKind>& ApiKeyKinds() {
    static const std::vector<ApiKeyKind> kinds = {
        {"MISTRAL_API_KEY", "Mistral", "writing, the canon check, vision, help search, prefab art", "https://console.mistral.ai/api-keys"},
        {"ANTHROPIC_API_KEY", "Anthropic (Claude)", "writing, and vision with Claude", "https://console.anthropic.com/settings/keys"},
        {"OPENROUTER_API_KEY", "OpenRouter", "writing, the canon check and vision with its models", "https://openrouter.ai/keys"},
        {"BFL_API_KEY", "Black Forest Labs (FLUX)", "painting places and backgrounds, prefab art, FLUX video", "https://dashboard.bfl.ai/keys"},
        {"PIXELLAB_API_KEY", "PixelLab", "pixel art, characters, animating things", "https://www.pixellab.ai/account"},
        {"AUTO_SPRITE_API_KEY", "AutoSprite", "animating characters", "https://www.autosprite.io"},
    };
    return kinds;
}

namespace {

bool Known(const std::string& env) {
    const auto& kinds = ApiKeyKinds();
    return std::any_of(kinds.begin(), kinds.end(), [&](const ApiKeyKind& k) { return k.env == env; });
}

const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64(const std::string& in) {
    std::string out;
    int val = 0, bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out.push_back(kAlphabet[(val >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) out.push_back(kAlphabet[((val << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string Unbase64(const std::string& in) {
    std::string out;
    int val = 0, bits = -8;
    for (char ch : in) {
        const char* at = std::strchr(kAlphabet, ch);
        if (ch == '=' || at == nullptr || ch == 0) break;
        val = (val << 6) + static_cast<int>(at - kAlphabet);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

// Encrypted for this Windows user (no one else, on this machine or another, can read it back).
std::string Seal(const std::string& plain) {
#ifdef _WIN32
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))}, out{};
    if (!CryptProtectData(&in, L"AIGE API key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    std::string sealed(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return Base64(sealed);
#else
    return Base64(plain);
#endif
}

std::string Unseal(const std::string& stored) {
    const std::string sealed = Unbase64(stored);
#ifdef _WIN32
    DATA_BLOB in{static_cast<DWORD>(sealed.size()), reinterpret_cast<BYTE*>(const_cast<char*>(sealed.data()))}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    std::string plain(reinterpret_cast<char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return plain;
#else
    return sealed;
#endif
}

std::string Trimmed(std::string s) {
    s.erase(0, s.find_first_not_of(" \t\r\n\"'"));
    s.erase(s.find_last_not_of(" \t\r\n\"'") + 1);
    return s;
}

} // namespace

ApiKeys::ApiKeys(fs::path file) : file_(std::move(file)) {
    std::ifstream in(file_);
    const json j = in ? json::parse(in, nullptr, false) : json();
    if (!j.is_object()) return;
    for (const auto& [env, v] : j.items()) {
        if (Known(env) && v.is_string() && !v.get<std::string>().empty()) sealed_[env] = v.get<std::string>();
    }
}

fs::path ApiKeys::DefaultPath() {
    const fs::path prefs = UserPrefs::DefaultPath();
    return prefs.empty() ? fs::path("api-keys.json") : prefs.parent_path() / "api-keys.json";
}

std::string ApiKeys::Set(const std::string& env, const std::string& value) {
    if (!Known(env)) return "not a key the app uses: " + env;
    const std::string key = Trimmed(value);
    if (key.empty()) {
        sealed_.erase(env);
    } else {
        if (key.find_first_of(" \t\r\n") != std::string::npos || key.size() < 8) return "that doesn't look like a key";
        const std::string sealed = Seal(key);
        if (sealed.empty()) return "Windows couldn't encrypt the key";
        sealed_[env] = sealed;
    }
    Save();
    return {};
}

std::map<std::string, std::string> ApiKeys::Environment() const {
    std::map<std::string, std::string> out;
    for (const auto& [env, sealed] : sealed_) {
        if (std::string plain = Unseal(sealed); !plain.empty()) out[env] = plain;
    }
    return out;
}

json ApiKeys::Status(const fs::path& serviceDir) const {
    // Given another way: the system's environment, or the service's own .env (a source build's).
    std::set<std::string> inDotEnv;
    {
        std::ifstream in(serviceDir / ".env");
        for (std::string line; std::getline(in, line);) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos || line.rfind('#', 0) == 0) continue;
            if (!Trimmed(line.substr(eq + 1)).empty()) inDotEnv.insert(Trimmed(line.substr(0, eq)));
        }
    }
    const auto plain = Environment();
    json out = json::array();
    for (const auto& k : ApiKeyKinds()) {
        const auto it = plain.find(k.env);
        char* system = nullptr;
        size_t n = 0;
        const bool inSystem = _dupenv_s(&system, &n, k.env.c_str()) == 0 && system != nullptr && *system != 0;
        free(system);
        json row = {{"env", k.env}, {"provider", k.provider}, {"uses", k.uses}, {"url", k.url}, {"set", it != plain.end()},
                    {"inEnv", inSystem ? "the system's environment" : inDotEnv.contains(k.env) ? "ai_service/.env" : ""}};
        if (it != plain.end()) row["tail"] = it->second.size() > 4 ? it->second.substr(it->second.size() - 4) : "";
        out.push_back(row);
    }
    return out;
}

void ApiKeys::Save() const {
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    json j = json::object();
    for (const auto& [env, sealed] : sealed_) j[env] = sealed;
    const fs::path temp = fs::path(file_).concat(".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << j.dump(2);
    }
    fs::rename(temp, file_, ec);
}

} // namespace aige
