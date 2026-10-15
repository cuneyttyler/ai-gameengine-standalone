#include "audio/Audio.h"

#include "miniaudio.h"

#include <iostream>

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

struct AudioEngine::Impl {
    ma_engine engine;
};

AudioEngine::AudioEngine() {
    auto impl = std::make_unique<Impl>();
    if (ma_engine_init(nullptr, &impl->engine) == MA_SUCCESS) {
        impl_ = std::move(impl);
    } else {
        std::cerr << "[Audio] no output device available; sounds are disabled" << std::endl;
    }
}

AudioEngine::~AudioEngine() {
    if (impl_) {
        ma_engine_uninit(&impl_->engine);
    }
}

bool AudioEngine::Play(const std::filesystem::path& path) {
    if (!impl_) {
        return false;
    }
    return ma_engine_play_sound(&impl_->engine, path.string().c_str(), nullptr) == MA_SUCCESS;
}

} // namespace aige
