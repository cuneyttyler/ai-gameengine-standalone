#include "gameplay/Profiler.h"

#include <algorithm>
#include <cstdio>

namespace aige {

Profiler& Profiler::Get() {
    static Profiler profiler;
    return profiler;
}

void Profiler::BeginFrame() {
    const auto now = std::chrono::steady_clock::now();
    if (started_) {
        frames_.push_back(std::chrono::duration<double, std::milli>(now - frameStart_).count());
        if (frames_.size() > 120) frames_.erase(frames_.begin());
        last_ = current_;
        sectionHistory_.push_back(current_);
        if (sectionHistory_.size() > 60) sectionHistory_.erase(sectionHistory_.begin());
    }
    current_.clear();
    frameStart_ = now;
    started_ = true;
}

void Profiler::Add(const std::string& section, double milliseconds) { current_[section] += milliseconds; }

std::map<std::string, double> Profiler::Averages() const {
    std::map<std::string, double> sums;
    for (const auto& frame : sectionHistory_) {
        for (const auto& [name, ms] : frame) sums[name] += ms;
    }
    for (auto& [name, ms] : sums) ms /= static_cast<double>(std::max<size_t>(1, sectionHistory_.size()));
    return sums;
}

double Profiler::AverageFrame() const {
    if (frames_.empty()) return 0.0;
    double sum = 0.0;
    const size_t n = std::min<size_t>(60, frames_.size());
    for (size_t i = frames_.size() - n; i < frames_.size(); ++i) sum += frames_[i];
    return sum / static_cast<double>(n);
}

double Profiler::WorstFrame() const { return frames_.empty() ? 0.0 : *std::max_element(frames_.begin(), frames_.end()); }

std::vector<std::string> Profiler::Lines() const {
    std::vector<std::string> out;
    char line[160];
    const double avg = AverageFrame();
    std::snprintf(line, sizeof(line), "frame %.1f ms (%.0f fps), worst %.1f ms", avg, avg > 0 ? 1000.0 / avg : 0.0, WorstFrame());
    out.push_back(line);
    std::vector<std::pair<double, std::string>> sections;
    for (const auto& [name, ms] : Averages()) sections.push_back({ms, name});
    std::sort(sections.rbegin(), sections.rend());
    for (const auto& [ms, name] : sections) {
        const int bar = static_cast<int>(std::min(30.0, ms / std::max(0.001, avg) * 30.0));
        std::snprintf(line, sizeof(line), "%-12s %6.2f ms %s", name.c_str(), ms, std::string(static_cast<size_t>(bar), '|').c_str());
        out.push_back(line);
    }
    // The last 60 frames as a sparkline: one character a frame, taller for slower.
    if (!frames_.empty()) {
        static const char* levels = " .:-=+*#%@";
        std::string spark;
        const size_t n = std::min<size_t>(60, frames_.size());
        for (size_t i = frames_.size() - n; i < frames_.size(); ++i) {
            const int level = std::clamp(static_cast<int>(frames_[i] / 33.3 * 9.0), 0, 9);
            spark += levels[level];
        }
        out.push_back("[" + spark + "]");
    }
    return out;
}

void Profiler::Reset() {
    frames_.clear();
    current_.clear();
    last_.clear();
    sectionHistory_.clear();
    started_ = false;
}

} // namespace aige
