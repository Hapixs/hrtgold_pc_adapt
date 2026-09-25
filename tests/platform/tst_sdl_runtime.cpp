#include "platform/sdl/SdlRuntime.h"

#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <deque>
#include <map>
#include <vector>

class FakeSdlApi final : public SdlApi {
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
        const auto it = kinds.find(id);
        return it == kinds.end() ? std::nullopt : std::optional(it->second);
    }
    void closeGamepad(int id) override { closed.push_back(id); }
    bool openAudioStream() override { ++audioOpenCount; return audioSucceeds; }
    bool putAudio(std::span<const std::int16_t> samples) override {
        submitted.assign(samples.begin(), samples.end());
        return putSucceeds;
    }
    void shutdown() override { ++shutdownCount; }

    bool initSucceeds{true};
    bool audioSucceeds{true};
    bool putSucceeds{true};
    int initCount{0};
    int audioOpenCount{0};
    int shutdownCount{0};
    std::map<int, InputDeviceKind> kinds;
    std::deque<Event> events;
    std::vector<int> opened;
    std::vector<int> closed;
    std::vector<std::int16_t> submitted;
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
    runtime.submitAudio(std::vector<std::int16_t>{1, 2});
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
    runtime.submitAudio(samples);
    QCOMPARE(api.submitted.size(), std::size_t{24000});
    QCOMPARE(api.submitted.back(), std::int16_t{42});
}

void SdlRuntimeTest::audioDropsIncompleteStereoFrameWithoutShiftingChannels() {
    FakeSdlApi api;
    SdlRuntime runtime(api);
    runtime.submitAudio(std::vector<std::int16_t>{10, 20, 30, 40, 50});
    QCOMPARE(api.submitted, (std::vector<std::int16_t>{10, 20, 30, 40}));
}

void SdlRuntimeTest::volumeClampsAndMuteSilences() {
    FakeSdlApi api;
    SdlRuntime runtime(api);
    runtime.setVolume(2.0f);
    runtime.submitAudio(std::vector<std::int16_t>{32767, -32768});
    QCOMPARE(api.submitted, (std::vector<std::int16_t>{32767, -32768}));
    runtime.setVolume(-1.0f);
    runtime.submitAudio(std::vector<std::int16_t>{1234, -1234});
    QCOMPARE(api.submitted, (std::vector<std::int16_t>{0, 0}));
    runtime.setVolume(0.5f);
    runtime.submitAudio(std::vector<std::int16_t>{2000, -2000});
    QCOMPARE(api.submitted, (std::vector<std::int16_t>{1000, -1000}));
}

void SdlRuntimeTest::failedSubmissionDisablesAudio() {
    FakeSdlApi api;
    SdlRuntime runtime(api);
    QSignalSpy errors(&runtime, &SdlRuntime::audioError);
    api.putSucceeds = false;
    runtime.submitAudio(std::vector<std::int16_t>{1, 2});
    QVERIFY(!runtime.audioAvailable());
    QCOMPARE(errors.size(), 1);
}

QTEST_MAIN(SdlRuntimeTest)
#include "tst_sdl_runtime.moc"
