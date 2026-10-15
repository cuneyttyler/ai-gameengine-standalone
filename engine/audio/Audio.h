#pragma once

#include <filesystem>
#include <memory>
#include <optional>

namespace aige {

// What decoding an audio file reveals (technical.md §12: miniaudio-backed).
struct AudioInfo {
    unsigned channels = 0;
    unsigned sampleRate = 0;
    double durationSeconds = 0.0;
};

// Decodes the file's header to check it's playable (WAV, MP3, FLAC; OGG
// Vorbis needs miniaudio's extra decoder and is reported as unsupported).
// No audio device needed, so it's safe in tests and import tooling.
std::optional<AudioInfo> ProbeAudio(const std::filesystem::path& path);

// Fire-and-forget sound playback on the default output device. If no device
// is available (headless machine, CI), construction still succeeds and Play
// just returns false — a missing speaker shouldn't take the game down.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool IsAvailable() const { return impl_ != nullptr; }
    bool Play(const std::filesystem::path& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aige
