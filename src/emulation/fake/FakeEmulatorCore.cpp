#include "emulation/fake/FakeEmulatorCore.h"

#include <array>
#include <chrono>
#include <utility>

using namespace std::chrono_literals;

namespace {
constexpr std::array<std::uint32_t, 8> kColorBars{
    0xffffffff,
    0xffffff00,
    0xff00ffff,
    0xff00ff00,
    0xffff00ff,
    0xffff0000,
    0xff0000ff,
    0xff000000,
};
}

FakeEmulatorCore::FakeEmulatorCore(std::function<void()> beforeWorkerStartForTest)
    : beforeWorkerStartForTest_(std::move(beforeWorkerStartForTest)) {}

FakeEmulatorCore::~FakeEmulatorCore() {
    shutdownWorker();
}

EmulationResult FakeEmulatorCore::load(const EmulationConfig& config) {
    std::scoped_lock lock(mutex_);
    calls_.push_back("load");
    if ((state_ != State::Unloaded && state_ != State::Loaded) || worker_.joinable()) {
        return failure(EmulationErrorCode::CoreFailure,
                       "Cannot load a configuration while the fake core is running.");
    }
    if (config.romPath.empty() || config.savePath.empty() || config.romSha256.empty()) {
        return failure(EmulationErrorCode::InvalidRom,
                       "The fake core requires ROM, save, and ROM hash values.");
    }
    state_ = State::Loaded;
    nextSequence_ = 1;
    return success();
}

EmulationResult FakeEmulatorCore::start() {
    std::scoped_lock lock(mutex_);
    calls_.push_back("start");
    if (state_ != State::Loaded || worker_.joinable()) {
        return failure(EmulationErrorCode::CoreFailure,
                       "The fake core must be loaded before it starts.");
    }
    state_ = State::Running;
    try {
        if (beforeWorkerStartForTest_) {
            beforeWorkerStartForTest_();
        }
        worker_ = std::jthread([this](std::stop_token stopToken) { run(stopToken); });
    } catch (...) {
        state_ = State::Loaded;
        return failure(EmulationErrorCode::CoreFailure,
                       "The fake core worker could not be started.");
    }
    return success();
}

EmulationResult FakeEmulatorCore::pause() {
    std::scoped_lock lock(mutex_);
    calls_.push_back("pause");
    if (state_ != State::Running) {
        return failure(EmulationErrorCode::CoreFailure,
                       "The fake core can pause only while running.");
    }
    state_ = State::Paused;
    wake_.notify_all();
    return success();
}

EmulationResult FakeEmulatorCore::resume() {
    std::scoped_lock lock(mutex_);
    calls_.push_back("resume");
    if (state_ != State::Paused) {
        return failure(EmulationErrorCode::CoreFailure,
                       "The fake core can resume only while paused.");
    }
    state_ = State::Running;
    wake_.notify_all();
    return success();
}

EmulationResult FakeEmulatorCore::stop() {
    std::jthread stoppingWorker;
    {
        std::scoped_lock lock(mutex_);
        calls_.push_back("stop");
        if (state_ != State::Running && state_ != State::Paused) {
            return failure(EmulationErrorCode::CoreFailure,
                           "The fake core can stop only after it starts.");
        }
        state_ = State::Stopping;
        worker_.request_stop();
        stoppingWorker = std::move(worker_);
        wake_.notify_all();
    }
    if (stoppingWorker.joinable()) {
        stoppingWorker.join();
    }
    {
        std::scoped_lock lock(mutex_);
        state_ = State::Loaded;
    }
    return success();
}

void FakeEmulatorCore::setButtons(DsButtonMask pressed) {
    buttons_.store(pressed, std::memory_order_relaxed);
}

std::shared_ptr<const VideoFrame> FakeEmulatorCore::latestFrame() const {
    return mailbox_.latest();
}

bool FakeEmulatorCore::isRunning() const noexcept {
    std::scoped_lock lock(mutex_);
    return state_ == State::Running || state_ == State::Paused;
}

std::vector<std::string> FakeEmulatorCore::calls() const {
    std::scoped_lock lock(mutex_);
    return calls_;
}

DsButtonMask FakeEmulatorCore::buttons() const noexcept {
    return buttons_.load(std::memory_order_relaxed);
}

EmulationResult FakeEmulatorCore::success() {
    return {};
}

EmulationResult FakeEmulatorCore::failure(EmulationErrorCode code, std::string message) {
    return EmulationResult{code, std::move(message)};
}

void FakeEmulatorCore::run(std::stop_token stopToken) {
    std::unique_lock lock(mutex_);
    while (!stopToken.stop_requested()) {
        wake_.wait(lock, stopToken, [this] { return state_ == State::Running; });
        if (stopToken.stop_requested()) {
            break;
        }

        mailbox_.publish(makeFrame(nextSequence_++));
        wake_.wait_for(lock, stopToken, 16ms,
                       [this] { return state_ != State::Running; });
    }
}

void FakeEmulatorCore::shutdownWorker() noexcept {
    std::jthread stoppingWorker;
    {
        std::scoped_lock lock(mutex_);
        state_ = State::Stopping;
        if (worker_.joinable()) {
            worker_.request_stop();
            stoppingWorker = std::move(worker_);
        }
        wake_.notify_all();
    }
    if (stoppingWorker.joinable()) {
        stoppingWorker.join();
    }
    {
        std::scoped_lock lock(mutex_);
        state_ = State::Unloaded;
    }
}

std::shared_ptr<VideoFrame> FakeEmulatorCore::makeFrame(std::uint64_t sequence) const {
    auto frame = std::make_shared<VideoFrame>();
    frame->sequence = sequence;
    for (int y = 0; y < kDsScreenHeight; ++y) {
        for (int x = 0; x < kDsScreenWidth; ++x) {
            const auto shiftedX = (x + static_cast<int>(sequence % kDsScreenWidth)) %
                                  kDsScreenWidth;
            const auto bar = static_cast<std::size_t>(shiftedX * kColorBars.size() /
                                                      kDsScreenWidth);
            frame->rgba[static_cast<std::size_t>(y * kDsScreenWidth + x)] = kColorBars[bar];
        }
    }
    return frame;
}
