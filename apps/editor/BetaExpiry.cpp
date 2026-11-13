#include "BetaExpiry.h"

#include "ai_bridge/UserPrefs.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

#ifndef AIGE_BETA_EXPIRES
#define AIGE_BETA_EXPIRES ""
#endif

// Found in editor.exe by the packaging (tools/package_beta.py checks the package has the date it built in).
extern "C" const char kAigeBetaMarker[] = "AIGE-BETA-EXPIRES=" AIGE_BETA_EXPIRES ";";

namespace aige {

namespace {

std::string Today() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_s(&utc, &now);
    char day[16];
    std::snprintf(day, sizeof(day), "%04d-%02d-%02d", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
    return day;
}

bool IsDay(const std::string& s) {
    return s.size() == 10 && s[4] == '-' && s[7] == '-';
}

} // namespace

std::string BetaExpires() {
    const std::string marker = kAigeBetaMarker;
    const std::string day = marker.substr(marker.find('=') + 1, 10);
    return IsDay(day) ? day : std::string();
}

std::string BetaExpired() {
    const std::string expires = BetaExpires();
    if (expires.empty()) return {};
    // The latest day this computer has seen: the clock set back doesn't count (days as YYYY-MM-DD compare as text).
    const std::filesystem::path prefs = UserPrefs::DefaultPath();
    const std::filesystem::path seenFile = prefs.empty() ? std::filesystem::path() : prefs.parent_path() / "beta-seen";
    std::string seen;
    if (!seenFile.empty()) {
        std::ifstream in(seenFile);
        std::getline(in, seen);
        if (!IsDay(seen)) seen.clear();
    }
    std::string today = Today();
    if (!seen.empty() && seen > today) today = seen;
    if (!seenFile.empty() && today != seen) {
        std::error_code ec;
        std::filesystem::create_directories(seenFile.parent_path(), ec);
        std::ofstream(seenFile, std::ios::trunc) << today;
    }
    if (today <= expires) return {};
    return "This AIGE beta ended on " + expires + ".\n\nA new beta build is on aige-dev.com/beta: download it there (your projects and "
           "API keys are kept).";
}

} // namespace aige
