#include "platform/sdl/SdlAudioSink.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr std::size_t maxAudioSamples = 48000 * 2 / 4;
}

SdlAudioSink::SdlAudioSink(SdlApi& api, bool available) noexcept
    : available_(available), api_(&api) {}

void SdlAudioSink::setVolume(float volume) noexcept {
    volume_.store(std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 0.0f);
}

void SdlAudioSink::submitAudio(std::span<const std::int16_t> samples) {
    if (!available_.load() || samples.empty()) return;
    const auto completeFrames = samples.first(samples.size() / 2 * 2);
    const auto count = std::min(completeFrames.size(), maxAudioSamples);
    if (count == 0) return;
    const auto tail = completeFrames.last(count);
    const auto volume = volume_.load();
    std::vector<std::int16_t> scaled;
    scaled.reserve(count);
    for (const auto sample : tail) {
        const auto value = static_cast<int>(std::lround(static_cast<float>(sample) * volume));
        scaled.push_back(static_cast<std::int16_t>(std::clamp(value, -32768, 32767)));
    }
    std::lock_guard lock(streamMutex_);
    if (!api_ || !available_.load()) return;
    if (!api_->putAudio(scaled)) {
        available_.store(false);
        submissionError_.store(true);
    }
}

void SdlAudioSink::shutdownApi() {
    std::lock_guard lock(streamMutex_);
    available_.store(false);
    if (!api_) return;
    api_->shutdown();
    api_ = nullptr;
}
