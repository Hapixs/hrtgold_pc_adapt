#pragma once

#include "platform/sdl/SdlApi.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>

// The emulation worker owns a shared handle to this object, never a SdlRuntime*.
class SdlAudioSink final {
public:
    SdlAudioSink(SdlApi& api, bool available) noexcept;

    void submitAudio(std::span<const std::int16_t> samples);
    void setVolume(float volume) noexcept;
    bool audioAvailable() const noexcept { return available_.load(); }

private:
    friend class SdlRuntime;
    bool takeSubmissionError() noexcept { return submissionError_.exchange(false); }
    void shutdownApi();

    std::atomic<float> volume_{1.0f};
    std::atomic<bool> available_{false};
    std::atomic<bool> submissionError_{false};
    std::mutex streamMutex_;
    SdlApi* api_{nullptr}; // Guarded by streamMutex_; nulled before runtime releases its ownership.
};
