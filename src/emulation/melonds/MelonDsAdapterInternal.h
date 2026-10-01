#pragma once
#include "emulation/melonds/MelonDsAdapter.h"
#include "emulation/melonds/MelonPlatformContext.h"
#include "core/emulation/FrameMailbox.h"
#include <atomic>
#include <condition_variable>
#include <future>
#include <optional>
#include <thread>

struct MelonDsAdapter::Impl {
    Impl(SaveRepository saves, std::shared_ptr<SdlAudioSink> audio, std::filesystem::path directory);
    void run(std::stop_token stop, bool freeBios, std::promise<EmulationResult> started);
    bool flushPending();
    void recordStop(int reason);
    SaveRepository saves;
    std::shared_ptr<SdlAudioSink> audio;
    MelonPlatformContext platform;
    FrameMailbox mailbox;
    std::uint64_t frameSequence{0}; // worker-owned, monotonic across restarts
    EmulationConfig config;
    std::mutex operations;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::jthread worker;
    std::atomic<DsButtonMask> buttons{0};
    std::atomic<bool> running{false};
    bool loaded{false};
    bool paused{false};
    bool pauseAcknowledged{false};
    bool stopRequested{false};
    int stopReason{-1};
    std::optional<std::vector<std::uint8_t>> pendingSave;
    std::optional<std::vector<std::uint8_t>> pendingFirmware;
    EmulationResult result;
};
