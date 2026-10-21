#include "audio/Audio.h"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <list>
#include <memory>

namespace aige {

std::optional<AudioInfo> ProbeAudio(const std::filesystem::path& path) {
    ma_decoder decoder;
    if (ma_decoder_init_file_w(path.wstring().c_str(), nullptr, &decoder) != MA_SUCCESS) {
        return std::nullopt;
    }
    AudioInfo info;
    info.channels = decoder.outputChannels;
    info.sampleRate = decoder.outputSampleRate;
    ma_uint64 frames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &frames) == MA_SUCCESS && info.sampleRate > 0) {
        info.durationSeconds = static_cast<double>(frames) / info.sampleRate;
    }
    ma_decoder_uninit(&decoder);
    return info;
}

const char* AudioBusName(AudioBus bus) {
    switch (bus) {
        case AudioBus::Master: return "master";
        case AudioBus::Music: return "music";
        case AudioBus::Effects: return "effects";
        case AudioBus::Voice: return "voice";
        case AudioBus::Ambience: return "ambience";
        case AudioBus::Ui: return "ui";
        case AudioBus::Count: break;
    }
    return "effects";
}

std::optional<AudioBus> AudioBusFromName(const std::string& name) {
    for (int i = 0; i < static_cast<int>(AudioBus::Count); ++i) {
        if (name == AudioBusName(static_cast<AudioBus>(i))) return static_cast<AudioBus>(i);
    }
    return std::nullopt;
}

namespace {

// A sound that loops and fades (music, ambience): the playing one and the one
// fading out under it.
struct Track {
    std::unique_ptr<ma_sound> sound;
    std::string path;
    float fade = 0.0f;   // 0..1, its own fade volume
    float target = 0.0f; // where the fade is going
    float rate = 1.0f;   // fade per second
};

} // namespace

struct AudioEngine::Impl {
    ma_engine engine;
    std::array<ma_sound_group, static_cast<int>(AudioBus::Count)> groups{};
    std::list<std::unique_ptr<ma_sound>> oneShots;
    Track music, musicOut, ambience, ambienceOut;
};

AudioEngine::AudioEngine(bool noDevice) {
    auto impl = std::make_unique<Impl>();
    ma_engine_config config = ma_engine_config_init();
    if (noDevice) {
        config.noDevice = MA_TRUE;
        config.channels = 2;
        config.sampleRate = 48000;
    }
    if (ma_engine_init(&config, &impl->engine) != MA_SUCCESS) {
        std::cerr << "[Audio] no output device available; sounds are disabled" << std::endl;
        return;
    }
    // Master under the engine, every other bus under master.
    ma_sound_group_init(&impl->engine, 0, nullptr, &impl->groups[0]);
    for (int i = 1; i < static_cast<int>(AudioBus::Count); ++i) {
        ma_sound_group_init(&impl->engine, 0, &impl->groups[0], &impl->groups[i]);
    }
    impl_ = std::move(impl);
}

AudioEngine::~AudioEngine() {
    if (!impl_) return;
    for (auto* track : {&impl_->music, &impl_->musicOut, &impl_->ambience, &impl_->ambienceOut}) {
        if (track->sound) ma_sound_uninit(track->sound.get());
    }
    for (auto& sound : impl_->oneShots) ma_sound_uninit(sound.get());
    for (int i = static_cast<int>(AudioBus::Count) - 1; i >= 0; --i) ma_sound_group_uninit(&impl_->groups[i]);
    ma_engine_uninit(&impl_->engine);
}

bool AudioEngine::Play(const std::filesystem::path& path, AudioBus bus, float volume) {
    if (!impl_) return false;
    auto sound = std::make_unique<ma_sound>();
    if (ma_sound_init_from_file_w(&impl_->engine, path.wstring().c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                  &impl_->groups[static_cast<int>(bus)], nullptr, sound.get()) != MA_SUCCESS) {
        return false;
    }
    ma_sound_set_volume(sound.get(), std::clamp(volume, 0.0f, 4.0f));
    ma_sound_start(sound.get());
    impl_->oneShots.push_back(std::move(sound));
    return true;
}

std::pair<float, float> AudioEngine::Placement(float x, float y) const {
    const float dx = x - listenerX_;
    const float dy = y - listenerY_;
    const float distance = std::sqrt(dx * dx + dy * dy);
    // Full volume nearby, fading to silence at the hearing distance.
    const float near = hearing_ * 0.15f;
    float volume = distance <= near ? 1.0f : std::max(0.0f, 1.0f - (distance - near) / std::max(0.01f, hearing_ - near));
    volume *= volume; // perceptually smoother
    const float pan = std::clamp(dx / std::max(1.0f, hearing_ * 0.5f), -1.0f, 1.0f);
    return {volume, pan};
}

bool AudioEngine::PlayAt(const std::filesystem::path& path, float x, float y, AudioBus bus, float volume) {
    const auto [gain, pan] = Placement(x, y);
    if (gain <= 0.001f) return false; // out of earshot
    if (!impl_) return false;
    auto sound = std::make_unique<ma_sound>();
    if (ma_sound_init_from_file_w(&impl_->engine, path.wstring().c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                  &impl_->groups[static_cast<int>(bus)], nullptr, sound.get()) != MA_SUCCESS) {
        return false;
    }
    ma_sound_set_volume(sound.get(), std::clamp(volume * gain, 0.0f, 4.0f));
    ma_sound_set_pan(sound.get(), pan);
    ma_sound_start(sound.get());
    impl_->oneShots.push_back(std::move(sound));
    return true;
}

void AudioEngine::SetListener(float x, float y, float hearingDistance) {
    listenerX_ = x;
    listenerY_ = y;
    hearing_ = std::max(1.0f, hearingDistance);
}

namespace {

void Crossfade(ma_engine* engine, ma_sound_group* group, Track& current, Track& out, const std::string& path,
               float fadeSeconds) {
    const float rate = fadeSeconds > 0.01f ? 1.0f / fadeSeconds : 1000.0f;
    // What's playing fades out (replacing anything still fading).
    if (out.sound) {
        ma_sound_uninit(out.sound.get());
        out.sound.reset();
    }
    if (current.sound) {
        out = std::move(current);
        out.target = 0.0f;
        out.rate = rate;
        current = Track{};
    }
    if (path.empty() || engine == nullptr) return;
    auto sound = std::make_unique<ma_sound>();
    if (ma_sound_init_from_file_w(engine, std::filesystem::path(path).wstring().c_str(),
                                  MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION, group, nullptr,
                                  sound.get()) != MA_SUCCESS) {
        return;
    }
    ma_sound_set_looping(sound.get(), MA_TRUE);
    ma_sound_set_volume(sound.get(), 0.0f);
    ma_sound_start(sound.get());
    current.sound = std::move(sound);
    current.path = path;
    current.fade = 0.0f;
    current.target = 1.0f;
    current.rate = rate;
}

void Step(Track& track, float seconds) {
    if (!track.sound) return;
    if (track.fade < track.target) track.fade = std::min(track.target, track.fade + track.rate * seconds);
    else if (track.fade > track.target) track.fade = std::max(track.target, track.fade - track.rate * seconds);
    ma_sound_set_volume(track.sound.get(), track.fade);
}

} // namespace

void AudioEngine::PlayMusic(const std::filesystem::path& path, float fadeSeconds) {
    const std::string name = path.string();
    if (name == music_) return;
    music_ = name;
    if (impl_) Crossfade(&impl_->engine, &impl_->groups[static_cast<int>(AudioBus::Music)], impl_->music, impl_->musicOut, name, fadeSeconds);
}

void AudioEngine::PlayAmbience(const std::filesystem::path& path, float fadeSeconds) {
    const std::string name = path.string();
    if (name == ambience_) return;
    ambience_ = name;
    if (impl_) {
        Crossfade(&impl_->engine, &impl_->groups[static_cast<int>(AudioBus::Ambience)], impl_->ambience,
                  impl_->ambienceOut, name, fadeSeconds);
    }
}

void AudioEngine::SetBusVolume(AudioBus bus, float volume) {
    volumes_[static_cast<int>(bus)] = std::clamp(volume, 0.0f, 1.0f);
}

float AudioEngine::MusicFade() const { return impl_ && impl_->music.sound ? impl_->music.fade : 0.0f; }

void AudioEngine::Update(float seconds) {
    if (!impl_) return;
    for (int i = 0; i < static_cast<int>(AudioBus::Count); ++i) {
        float volume = volumes_[i];
        if (static_cast<AudioBus>(i) == AudioBus::Music || static_cast<AudioBus>(i) == AudioBus::Ambience) volume *= duck_;
        ma_sound_group_set_volume(&impl_->groups[i], volume);
    }
    for (auto* track : {&impl_->music, &impl_->musicOut, &impl_->ambience, &impl_->ambienceOut}) Step(*track, seconds);
    for (auto* out : {&impl_->musicOut, &impl_->ambienceOut}) {
        if (out->sound && out->fade <= 0.0f) {
            ma_sound_uninit(out->sound.get());
            out->sound.reset();
        }
    }
    // One-shots that have finished.
    for (auto it = impl_->oneShots.begin(); it != impl_->oneShots.end();) {
        if (ma_sound_at_end(it->get()) || !ma_sound_is_playing(it->get())) {
            ma_sound_uninit(it->get());
            it = impl_->oneShots.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace aige
