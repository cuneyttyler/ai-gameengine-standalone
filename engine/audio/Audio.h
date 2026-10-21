#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

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

// Where a sound goes (development_plan.md 12.6): each bus has its own volume,
// which players set in their settings; every bus goes through master.
enum class AudioBus {
    Master,
    Music,
    Effects,
    Voice,
    Ambience,
    Ui,
    Count
};
const char* AudioBusName(AudioBus bus);
std::optional<AudioBus> AudioBusFromName(const std::string& name);

// Sound for a game (12.6): one-shot sounds on a bus, optionally placed in the
// world (panned and quieter with distance from the listener); music that
// streams, loops and cross-fades from one track to the next; one ambience loop
// that cross-fades the same way; bus volumes; and ducking (music and ambience
// quieter while someone speaks).
//
// If no device is available (headless machine, CI), construction still
// succeeds and everything is silent — a missing speaker shouldn't take the
// game down. `noDevice` mixes without a device (tests can read the volumes).
class AudioEngine {
public:
    explicit AudioEngine(bool noDevice = false);
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool IsAvailable() const { return impl_ != nullptr; }

    // A one-shot sound.
    bool Play(const std::filesystem::path& path, AudioBus bus = AudioBus::Effects, float volume = 1.0f);
    // A one-shot sound at a point in the world, heard from the listener.
    bool PlayAt(const std::filesystem::path& path, float x, float y, AudioBus bus = AudioBus::Effects,
                float volume = 1.0f);
    // Where the listener is (the player, or the camera's centre), in cells, and
    // how far away a sound fades out entirely.
    void SetListener(float x, float y, float hearingDistance = 16.0f);

    // Music: streams `path`, looping, fading in over `fadeSeconds` while the
    // current track fades out. The same track again does nothing; "" fades out.
    void PlayMusic(const std::filesystem::path& path, float fadeSeconds = 1.5f);
    const std::string& CurrentMusic() const { return music_; }
    // The same for the ambience loop (wind, a tavern's murmur).
    void PlayAmbience(const std::filesystem::path& path, float fadeSeconds = 2.0f);
    const std::string& CurrentAmbience() const { return ambience_; }

    void SetBusVolume(AudioBus bus, float volume); // 0..1
    float BusVolume(AudioBus bus) const { return volumes_[static_cast<int>(bus)]; }
    // Music and ambience at this fraction while voice plays (dialogue): 1 = no ducking.
    void SetDucking(float fraction) { duck_ = fraction; }

    // Advances fades; call once per frame.
    void Update(float seconds);

    // What a world sound at (x, y) would play at: volume 0..1 and pan -1..1
    // (tests, the debug overlay).
    std::pair<float, float> Placement(float x, float y) const;
    // The music track's current fade volume, 0..1 (tests).
    float MusicFade() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::array<float, static_cast<int>(AudioBus::Count)> volumes_{1, 1, 1, 1, 1, 1};
    float listenerX_ = 0.0f, listenerY_ = 0.0f, hearing_ = 16.0f;
    float duck_ = 1.0f;
    std::string music_;
    std::string ambience_;
};

} // namespace aige
