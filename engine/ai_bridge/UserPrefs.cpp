#include "ai_bridge/UserPrefs.h"

#include <cstdlib>
#include <fstream>

namespace aige {

UserPrefs::UserPrefs(std::filesystem::path file) : file_(std::move(file)) {
    if (file_.empty()) {
        return;
    }
    std::ifstream in(file_);
    if (!in) {
        return;
    }
    nlohmann::json loaded = nlohmann::json::parse(in, nullptr, false);
    if (loaded.is_object()) {
        values_ = std::move(loaded); // a corrupt file just means defaults
    }
}

std::filesystem::path UserPrefs::DefaultPath() {
#ifdef _WIN32
    wchar_t* base = nullptr;
    size_t length = 0;
    std::filesystem::path dir;
    if (_wdupenv_s(&base, &length, L"LOCALAPPDATA") == 0 && base != nullptr) {
        dir = std::filesystem::path(base) / "aige";
    }
    free(base);
#else
    std::filesystem::path dir;
    if (const char* home = std::getenv("HOME")) {
        dir = std::filesystem::path(home) / ".config" / "aige";
    }
#endif
    return dir.empty() ? std::filesystem::path() : dir / "editor.json";
}

nlohmann::json UserPrefs::Get(const std::string& key, const nlohmann::json& fallback) const {
    auto it = values_.find(key);
    return it == values_.end() ? fallback : *it;
}

void UserPrefs::Set(const std::string& key, const nlohmann::json& value) {
    values_[key] = value;
    if (file_.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    std::ofstream(file_) << values_.dump(2);
}

} // namespace aige
