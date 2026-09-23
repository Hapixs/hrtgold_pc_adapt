#pragma once

#include "core/emulation/EmulatorCore.h"
#include "core/emulation/FrameMailbox.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class FakeEmulatorCore final : public EmulatorCore {
public:
    struct TestHooks {
        std::function<void()> beforeWorkerStart;
        std::function<void()> beforeStopLock;
        std::function<void()> afterStopLock;
    };

    // Test-only hooks must not throw or re-enter this core.
    explicit FakeEmulatorCore(TestHooks testHooks = {});
    ~FakeEmulatorCore() override;

    FakeEmulatorCore(const FakeEmulatorCore&) = delete;
    FakeEmulatorCore& operator=(const FakeEmulatorCore&) = delete;

    EmulationResult load(const EmulationConfig& config) override;
    EmulationResult start() override;
    EmulationResult pause() override;
    EmulationResult resume() override;
    EmulationResult stop() override;
    void setButtons(DsButtonMask pressed) override;
    std::shared_ptr<const VideoFrame> latestFrame() const override;
    bool isRunning() const noexcept override;

    std::vector<std::string> calls() const;
    DsButtonMask buttons() const noexcept;

private:
    enum class State { Unloaded, Loaded, Running, Paused, Stopping };

    static EmulationResult success();
    static EmulationResult failure(EmulationErrorCode code, std::string message);
    void run(std::stop_token stopToken);
    void shutdownWorker() noexcept;
    std::shared_ptr<VideoFrame> makeFrame(std::uint64_t sequence) const;

    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    State state_{State::Unloaded};
    std::vector<std::string> calls_;
    TestHooks testHooks_;
    std::jthread worker_;
    std::uint64_t nextSequence_{1};
    std::atomic<DsButtonMask> buttons_{0};
    FrameMailbox mailbox_;
};
