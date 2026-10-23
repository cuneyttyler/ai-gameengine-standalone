#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace aige {

// The profiler overlay (development_plan.md 24): named sections timed each frame
// (scripts, AI, combat, physics, rendering...), a history of frame times, and the
// overlay's lines. Cheap enough to stay on: a section is two clock reads.
class Profiler {
public:
    static Profiler& Get();

    // Starts a frame: what the last frame's sections took moves into the history.
    void BeginFrame();
    void Add(const std::string& section, double milliseconds);
    // Frame times (ms), oldest first, up to 120.
    const std::vector<double>& FrameHistory() const { return frames_; }
    // The last frame's sections and their averages over the last second.
    std::map<std::string, double> LastFrame() const { return last_; }
    std::map<std::string, double> Averages() const;
    double AverageFrame() const;
    double WorstFrame() const;
    // For the overlay: "frame 16.4 ms (61 fps), worst 22.1", then each section by cost.
    std::vector<std::string> Lines() const;
    void Reset();

    // Times a scope: Profiler::Scope s("physics");
    class Scope {
    public:
        explicit Scope(const char* section) : section_(section), start_(std::chrono::steady_clock::now()) {}
        ~Scope() {
            const auto end = std::chrono::steady_clock::now();
            Profiler::Get().Add(section_, std::chrono::duration<double, std::milli>(end - start_).count());
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        const char* section_;
        std::chrono::steady_clock::time_point start_;
    };

private:
    std::chrono::steady_clock::time_point frameStart_{};
    bool started_ = false;
    std::vector<double> frames_;
    std::map<std::string, double> current_, last_;
    std::vector<std::map<std::string, double>> sectionHistory_;
};

} // namespace aige
