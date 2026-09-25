#include "platform/sdl/SdlRuntime.h"
#include "platform/sdl/SdlAudioQueue.h"

#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class FakeSdlApi : public SdlApi {
public:
    bool init() override { ++initCount; return initSucceeds; }
    std::optional<Event> pollEvent() override {
        if (events.empty()) return std::nullopt;
        const auto event = events.front();
        events.pop_front();
        return event;
    }
    std::optional<InputDeviceKind> openGamepad(int id) override {
        opened.push_back(id);
        if (onOpen) onOpen();
        const auto it = kinds.find(id);
        return it == kinds.end() ? std::nullopt : std::optional(it->second);
    }
    void closeGamepad(int id) override { closed.push_back(id); if (onClose) onClose(); }
    bool openAudioStream() override { ++audioOpenCount; return audioSucceeds; }
    bool putAudio(std::span<const std::int16_t> samples) override {
        return sdl_audio::submitBoundedAudio(*this, samples);
    }
    int queuedBytes() const { return static_cast<int>(queued.size() * sizeof(std::int16_t)); }
    int drainOldestFrames(int frames) {
        if (zeroOutputDrain) return 0;
        const auto discarded = std::min<std::size_t>(queued.size(), static_cast<std::size_t>(frames) * 2);
        queued.erase(queued.begin(), queued.begin() + discarded);
        return static_cast<int>(discarded * sizeof(std::int16_t));
    }
    bool clearQueued() { ++clearCount; queued.clear(); return true; }
    bool putAudioSamples(std::span<const std::int16_t> samples) {
        submitted.assign(samples.begin(), samples.end());
        if (!putSucceeds) return false;
        queued.insert(queued.end(), samples.begin(), samples.end());
        return true;
    }
    void shutdown() override { ++shutdownCount; }

    bool initSucceeds{true};
    bool audioSucceeds{true};
    bool putSucceeds{true};
    bool zeroOutputDrain{false};
    int clearCount{0};
    int initCount{0};
    int audioOpenCount{0};
    int shutdownCount{0};
    std::map<int, InputDeviceKind> kinds;
    std::deque<Event> events;
    std::vector<int> opened;
    std::vector<int> closed;
    std::vector<std::int16_t> submitted;
    std::vector<std::int16_t> queued;
    std::function<void()> onOpen;
    std::function<void()> onClose;
};

class BlockingAudioApi final : public FakeSdlApi {
public:
    bool putAudio(std::span<const std::int16_t> samples) override {
        {
            std::unique_lock lock(mutex);
            inPut = true;
            condition.notify_all();
            condition.wait(lock, [&] { return releasePut; });
            inPut = false;
            condition.notify_all();
        }
        return FakeSdlApi::putAudio(samples);
    }

    void shutdown() override {
        {
            std::unique_lock lock(mutex);
            shutdownEntered = true;
            shutdownOverlappedPut = inPut;
            condition.notify_all();
            condition.wait(lock, [&] { return !inPut; });
        }
        FakeSdlApi::shutdown();
    }

    std::mutex mutex;
    std::condition_variable condition;
    bool inPut{false};
    bool releasePut{false};
    bool shutdownEntered{false};
    bool shutdownOverlappedPut{false};
};

class SdlRuntimeTest final : public QObject {
    Q_OBJECT
private slots:
    void ownsInitAndShutdown();
    void audioFailureIsNonfatal();
    void startupAudioFailureReportsError();
    void initFailureSkipsAudioAndPump();
    void hotplugPreservesOtherGamepadsAndFallsBack();
    void unplugReleasesHeldInput();
    void buttonEventsUseSemanticActionsAndDeviceKind();
    void axisThresholdReleasesAtDeadZone();
    void dpadAndAxisDoNotReleaseEachOther();
    void audioDropsOldestSamplesAtQuarterSecond();
    void audioDropsIncompleteStereoFrameWithoutShiftingChannels();
    void volumeClampsAndMuteSilences();
    void failedSubmissionDisablesAudio();
    void sameKindPadsKeepActionHeldUntilLastHolderReleases();
    void audioQueueRetainsNewestQuarterSecondAcrossSubmissions();
    void reentrantPumpDefersRemovalUntilInputSignalReturns();
    void reentrantPumpDuringOpenAndCloseKeepsEventOrder();
    void destructionWaitsForInFlightAudioSubmission();
    void heldAudioSinkRemainsSafeAfterRuntimeDestruction();
    void zeroOutputDrainKeepsNewestChunkAndAudioAvailable();
};

void SdlRuntimeTest::ownsInitAndShutdown() {
    FakeSdlApi api;
    { SdlRuntime runtime(api); QVERIFY(runtime.audioAvailable()); QCOMPARE(api.initCount, 1); QCOMPARE(api.audioOpenCount, 1); }
    QCOMPARE(api.shutdownCount, 1);
}

void SdlRuntimeTest::audioFailureIsNonfatal() {
    FakeSdlApi api;
    api.audioSucceeds = false;
    SdlRuntime runtime(api);
    QVERIFY(!runtime.audioAvailable());
    runtime.audioSink()->submitAudio(std::vector<std::int16_t>{1, 2});
    QVERIFY(api.submitted.empty());
    api.kinds[12] = InputDeviceKind::Xbox;
    api.events.push_back({SdlApi::EventType::GamepadAdded, 12});
    runtime.pump();
    QCOMPARE(api.opened, std::vector<int>{12});
}

void SdlRuntimeTest::startupAudioFailureReportsError() {
    FakeSdlApi api;
    api.audioSucceeds = false;
    SdlRuntime runtime(api);
    QSignalSpy errors(&runtime, &SdlRuntime::audioError);
    QCoreApplication::processEvents();
    QCOMPARE(errors.size(), 1);
}

void SdlRuntimeTest::initFailureSkipsAudioAndPump() {
    FakeSdlApi api;
    api.initSucceeds = false;
    SdlRuntime runtime(api);
    api.events.push_back({SdlApi::EventType::GamepadAdded, 3});
    runtime.pump();
    QCOMPARE(api.audioOpenCount, 0);
    QVERIFY(api.opened.empty());
    QVERIFY(!runtime.audioAvailable());
}

void SdlRuntimeTest::hotplugPreservesOtherGamepadsAndFallsBack() {
    FakeSdlApi api;
    api.kinds = {{12, InputDeviceKind::Xbox}, {13, InputDeviceKind::PlayStation}};
    SdlRuntime runtime(api);
    QSignalSpy devices(&runtime, &SdlRuntime::activeDeviceChanged);
    api.events.push_back({SdlApi::EventType::GamepadAdded, 12});
    api.events.push_back({SdlApi::EventType::GamepadAdded, 13});
    api.events.push_back({SdlApi::EventType::ButtonDown, 13, SdlApi::Button::South});
    runtime.pump();
    QCOMPARE(runtime.activeDevice(), InputDeviceKind::PlayStation);
    api.events.push_back({SdlApi::EventType::GamepadRemoved, 12});
    runtime.pump();
    QCOMPARE(api.closed, std::vector<int>{12});
    QCOMPARE(runtime.activeDevice(), InputDeviceKind::PlayStation);
    api.events.push_back({SdlApi::EventType::GamepadRemoved, 13});
    runtime.pump();
    QCOMPARE(api.closed, (std::vector<int>{12, 13}));
    QCOMPARE(runtime.activeDevice(), InputDeviceKind::Keyboard);
    QCOMPARE(devices.last().at(0).value<InputDeviceKind>(), InputDeviceKind::Keyboard);
}

void SdlRuntimeTest::unplugReleasesHeldInput() {
    FakeSdlApi api;
    api.kinds[4] = InputDeviceKind::Xbox;
    SdlRuntime runtime(api);
    QSignalSpy inputs(&runtime, &SdlRuntime::inputEvent);
    api.events.push_back({SdlApi::EventType::GamepadAdded, 4});
    api.events.push_back({SdlApi::EventType::ButtonDown, 4, SdlApi::Button::South});
    api.events.push_back({SdlApi::EventType::GamepadRemoved, 4});
    runtime.pump();
    QCOMPARE(inputs.size(), 2);
    QCOMPARE(inputs.last().at(0).value<InputEvent>().phase, InputPhase::Released);
    QCOMPARE(inputs.last().at(0).value<InputEvent>().action, InputAction::Confirm);
}

void SdlRuntimeTest::buttonEventsUseSemanticActionsAndDeviceKind() {
    FakeSdlApi api;
    api.kinds[4] = InputDeviceKind::Xbox;
    SdlRuntime runtime(api);
    QSignalSpy inputs(&runtime, &SdlRuntime::inputEvent);
    api.events.push_back({SdlApi::EventType::GamepadAdded, 4});
    api.events.push_back({SdlApi::EventType::ButtonDown, 4, SdlApi::Button::DpadUp});
    api.events.push_back({SdlApi::EventType::ButtonUp, 4, SdlApi::Button::DpadUp});
    api.events.push_back({SdlApi::EventType::ButtonDown, 4, SdlApi::Button::South});
    api.events.push_back({SdlApi::EventType::ButtonDown, 4, SdlApi::Button::East});
    api.events.push_back({SdlApi::EventType::ButtonDown, 4, SdlApi::Button::Start});
    runtime.pump();
    QCOMPARE(inputs.size(), 5);
    QCOMPARE(inputs.at(0).at(0).value<InputEvent>().action, InputAction::MoveUp);
    QCOMPARE(inputs.at(0).at(0).value<InputEvent>().phase, InputPhase::Pressed);
    QCOMPARE(inputs.at(1).at(0).value<InputEvent>().phase, InputPhase::Released);
    QCOMPARE(inputs.at(2).at(0).value<InputEvent>().action, InputAction::Confirm);
    QCOMPARE(inputs.at(3).at(0).value<InputEvent>().action, InputAction::Back);
    QCOMPARE(inputs.at(4).at(0).value<InputEvent>().action, InputAction::OpenMenu);
    QCOMPARE(inputs.at(4).at(0).value<InputEvent>().device, InputDeviceKind::Xbox);
}

void SdlRuntimeTest::axisThresholdReleasesAtDeadZone() {
    FakeSdlApi api;
    api.kinds[5] = InputDeviceKind::GenericGamepad;
    SdlRuntime runtime(api);
    QSignalSpy inputs(&runtime, &SdlRuntime::inputEvent);
    api.events.push_back({SdlApi::EventType::GamepadAdded, 5});
    api.events.push_back({SdlApi::EventType::AxisMotion, 5, {}, SdlApi::Axis::LeftX, -16000});
    api.events.push_back({SdlApi::EventType::AxisMotion, 5, {}, SdlApi::Axis::LeftX, -15999});
    api.events.push_back({SdlApi::EventType::AxisMotion, 5, {}, SdlApi::Axis::LeftX, 16000});
    runtime.pump();
    QCOMPARE(inputs.size(), 3);
    QCOMPARE(inputs.at(0).at(0).value<InputEvent>().action, InputAction::MoveLeft);
    QCOMPARE(inputs.at(0).at(0).value<InputEvent>().phase, InputPhase::Pressed);
    QCOMPARE(inputs.at(1).at(0).value<InputEvent>().phase, InputPhase::Released);
    QCOMPARE(inputs.at(2).at(0).value<InputEvent>().action, InputAction::MoveRight);
}

void SdlRuntimeTest::dpadAndAxisDoNotReleaseEachOther() {
    FakeSdlApi api;
    api.kinds[5] = InputDeviceKind::GenericGamepad;
    SdlRuntime runtime(api);
    QSignalSpy inputs(&runtime, &SdlRuntime::inputEvent);
    api.events.push_back({SdlApi::EventType::GamepadAdded, 5});
    api.events.push_back({SdlApi::EventType::ButtonDown, 5, SdlApi::Button::DpadLeft});
    api.events.push_back({SdlApi::EventType::AxisMotion, 5, {}, SdlApi::Axis::LeftX, -20000});
    api.events.push_back({SdlApi::EventType::ButtonUp, 5, SdlApi::Button::DpadLeft});
    runtime.pump();
    QCOMPARE(inputs.size(), 1);
    api.events.push_back({SdlApi::EventType::AxisMotion, 5, {}, SdlApi::Axis::LeftX, 0});
    runtime.pump();
    QCOMPARE(inputs.size(), 2);
    QCOMPARE(inputs.last().at(0).value<InputEvent>().phase, InputPhase::Released);
}

void SdlRuntimeTest::audioDropsOldestSamplesAtQuarterSecond() {
    FakeSdlApi api;
    SdlRuntime runtime(api);
    std::vector<std::int16_t> samples(96000 * 2, 7);
    samples.back() = 42;
    runtime.audioSink()->submitAudio(samples);
    QCOMPARE(api.submitted.size(), std::size_t{24000});
    QCOMPARE(api.submitted.back(), std::int16_t{42});
}

void SdlRuntimeTest::audioDropsIncompleteStereoFrameWithoutShiftingChannels() {
    FakeSdlApi api;
    SdlRuntime runtime(api);
    runtime.audioSink()->submitAudio(std::vector<std::int16_t>{10, 20, 30, 40, 50});
    QCOMPARE(api.submitted, (std::vector<std::int16_t>{10, 20, 30, 40}));
}

void SdlRuntimeTest::volumeClampsAndMuteSilences() {
    FakeSdlApi api;
    SdlRuntime runtime(api);
    runtime.setVolume(2.0f);
    runtime.audioSink()->submitAudio(std::vector<std::int16_t>{32767, -32768});
    QCOMPARE(api.submitted, (std::vector<std::int16_t>{32767, -32768}));
    runtime.setVolume(-1.0f);
    runtime.audioSink()->submitAudio(std::vector<std::int16_t>{1234, -1234});
    QCOMPARE(api.submitted, (std::vector<std::int16_t>{0, 0}));
    runtime.setVolume(0.5f);
    runtime.audioSink()->submitAudio(std::vector<std::int16_t>{2000, -2000});
    QCOMPARE(api.submitted, (std::vector<std::int16_t>{1000, -1000}));
}

void SdlRuntimeTest::failedSubmissionDisablesAudio() {
    FakeSdlApi api;
    SdlRuntime runtime(api);
    QSignalSpy errors(&runtime, &SdlRuntime::audioError);
    api.putSucceeds = false;
    runtime.audioSink()->submitAudio(std::vector<std::int16_t>{1, 2});
    QVERIFY(!runtime.audioAvailable());
    runtime.pump();
    QCOMPARE(errors.size(), 1);
}

void SdlRuntimeTest::sameKindPadsKeepActionHeldUntilLastHolderReleases() {
    FakeSdlApi api;
    api.kinds = {{1, InputDeviceKind::Xbox}, {2, InputDeviceKind::Xbox}};
    SdlRuntime runtime(api);
    QSignalSpy inputs(&runtime, &SdlRuntime::inputEvent);
    api.events.push_back({SdlApi::EventType::GamepadAdded, 1});
    api.events.push_back({SdlApi::EventType::GamepadAdded, 2});
    api.events.push_back({SdlApi::EventType::ButtonDown, 1, SdlApi::Button::South});
    api.events.push_back({SdlApi::EventType::ButtonDown, 2, SdlApi::Button::South});
    api.events.push_back({SdlApi::EventType::GamepadRemoved, 1});
    runtime.pump();
    QCOMPARE(inputs.size(), 1);
    QCOMPARE(inputs.at(0).at(0).value<InputEvent>().phase, InputPhase::Pressed);
    api.events.push_back({SdlApi::EventType::ButtonUp, 2, SdlApi::Button::South});
    runtime.pump();
    QCOMPARE(inputs.size(), 2);
    QCOMPARE(inputs.at(1).at(0).value<InputEvent>().phase, InputPhase::Released);
}

void SdlRuntimeTest::audioQueueRetainsNewestQuarterSecondAcrossSubmissions() {
    FakeSdlApi api;
    SdlRuntime runtime(api);
    std::vector<std::int16_t> first(48000 * 2 * 240 / 1000, 1);
    std::fill(first.end() - 960, first.end(), 2);
    const std::vector<std::int16_t> second(48000 * 2 * 20 / 1000, 3);
    runtime.audioSink()->submitAudio(first);
    runtime.audioSink()->submitAudio(second);
    QCOMPARE(api.queued.size(), std::size_t{24000});
    QCOMPARE(api.queued.front(), std::int16_t{1});
    QCOMPARE(api.queued.at(24000 - 960 - second.size()), std::int16_t{2});
    QCOMPARE(api.queued.back(), std::int16_t{3});
}

void SdlRuntimeTest::reentrantPumpDefersRemovalUntilInputSignalReturns() {
    FakeSdlApi api;
    api.kinds[7] = InputDeviceKind::Xbox;
    SdlRuntime runtime(api);
    QSignalSpy inputs(&runtime, &SdlRuntime::inputEvent);
    api.events.push_back({SdlApi::EventType::GamepadAdded, 7});
    api.events.push_back({SdlApi::EventType::ButtonDown, 7, SdlApi::Button::South});
    bool returnedFromNestedPump = false;
    QObject::connect(&runtime, &SdlRuntime::inputEvent, &runtime, [&](InputEvent event) {
        if (event.phase == InputPhase::Pressed) {
            api.events.push_back({SdlApi::EventType::GamepadRemoved, 7});
            runtime.pump();
            returnedFromNestedPump = api.closed.empty();
        }
    });
    runtime.pump();
    QVERIFY(returnedFromNestedPump);
    QCOMPARE(inputs.size(), 2);
    QCOMPARE(api.closed, std::vector<int>{7});
    QCOMPARE(inputs.at(1).at(0).value<InputEvent>().phase, InputPhase::Released);
}

void SdlRuntimeTest::reentrantPumpDuringOpenAndCloseKeepsEventOrder() {
    FakeSdlApi api;
    api.kinds = {{7, InputDeviceKind::Xbox}, {8, InputDeviceKind::PlayStation}};
    SdlRuntime runtime(api);
    QSignalSpy inputs(&runtime, &SdlRuntime::inputEvent);
    bool openReturnedBeforeButton = false;
    bool closeReturnedBeforeNextAdd = false;
    api.onOpen = [&] {
        if (api.opened.back() == 7) {
            runtime.pump();
            openReturnedBeforeButton = inputs.empty();
        }
    };
    api.onClose = [&] {
        runtime.pump();
        closeReturnedBeforeNextAdd = api.opened.size() == 1;
    };
    api.events.push_back({SdlApi::EventType::GamepadAdded, 7});
    api.events.push_back({SdlApi::EventType::ButtonDown, 7, SdlApi::Button::South});
    api.events.push_back({SdlApi::EventType::GamepadRemoved, 7});
    api.events.push_back({SdlApi::EventType::GamepadAdded, 8});
    runtime.pump();
    QVERIFY(openReturnedBeforeButton);
    QVERIFY(closeReturnedBeforeNextAdd);
    QCOMPARE(inputs.size(), 2);
    QCOMPARE(api.opened, (std::vector<int>{7, 8}));
    api.onOpen = {};
    api.onClose = {};
}

void SdlRuntimeTest::destructionWaitsForInFlightAudioSubmission() {
    BlockingAudioApi api;
    auto runtime = std::make_unique<SdlRuntime>(api);
    auto sink = runtime->audioSink();
    std::thread submitter([sink] { sink->submitAudio(std::vector<std::int16_t>{1, 2}); });
    {
        std::unique_lock lock(api.mutex);
        api.condition.wait(lock, [&] { return api.inPut; });
    }
    std::atomic<bool> destructionStarted{false};
    std::thread destroyer([&] {
        destructionStarted.store(true);
        runtime.reset();
    });
    while (!destructionStarted.load()) std::this_thread::yield();
    {
        std::unique_lock lock(api.mutex);
        api.condition.wait_for(lock, std::chrono::milliseconds(100), [&] { return api.shutdownEntered; });
        api.releasePut = true;
        api.condition.notify_all();
    }
    submitter.join();
    destroyer.join();
    QVERIFY(!api.shutdownOverlappedPut);
    QCOMPARE(api.shutdownCount, 1);
}

void SdlRuntimeTest::heldAudioSinkRemainsSafeAfterRuntimeDestruction() {
    FakeSdlApi api;
    auto runtime = std::make_unique<SdlRuntime>(api);
    auto sink = runtime->audioSink();
    std::mutex gateMutex;
    std::condition_variable gate;
    bool paused = false;
    bool resume = false;
    std::thread submitter([sink, &gateMutex, &gate, &paused, &resume] {
        {
            std::unique_lock lock(gateMutex);
            paused = true;
            gate.notify_all();
            gate.wait(lock, [&] { return resume; });
        }
        sink->submitAudio(std::vector<std::int16_t>{1, 2});
    });
    {
        std::unique_lock lock(gateMutex);
        gate.wait(lock, [&] { return paused; });
    }
    runtime.reset();
    {
        std::lock_guard lock(gateMutex);
        resume = true;
    }
    gate.notify_all();
    submitter.join();
    QVERIFY(!sink->audioAvailable());
    QVERIFY(api.submitted.empty());
    QCOMPARE(api.shutdownCount, 1);
}

void SdlRuntimeTest::zeroOutputDrainKeepsNewestChunkAndAudioAvailable() {
    FakeSdlApi api;
    api.zeroOutputDrain = true;
    SdlRuntime runtime(api);
    runtime.audioSink()->submitAudio(std::vector<std::int16_t>(48000 * 2 * 240 / 1000, 1));
    const std::vector<std::int16_t> newest(24000, 7);
    runtime.audioSink()->submitAudio(newest);
    QVERIFY(runtime.audioAvailable());
    QCOMPARE(api.queued, newest);
    QCOMPARE(api.clearCount, 1);
}

QTEST_MAIN(SdlRuntimeTest)
#include "tst_sdl_runtime.moc"
