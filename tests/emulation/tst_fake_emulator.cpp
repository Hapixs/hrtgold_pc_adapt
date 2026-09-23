#include "emulation/fake/FakeEmulatorCore.h"

#include <QtTest>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <semaphore>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace {
EmulationConfig validConfig() {
    return EmulationConfig{
        .romPath = std::filesystem::path{L"C:/private/heartgold.nds"},
        .savePath = std::filesystem::path{L"C:/private/game.sav"},
        .romSha256 = std::string(64, 'a'),
    };
}
} // namespace

class FakeEmulatorCoreTest final : public QObject {
    Q_OBJECT

private slots:
    void fakeCoreHonorsLifecycle() {
        FakeEmulatorCore core;

        QVERIFY(core.load(validConfig()).ok());
        QVERIFY(core.start().ok());
        QVERIFY(core.pause().ok());
        QVERIFY(core.resume().ok());
        QVERIFY(core.stop().ok());

        QCOMPARE(core.calls(),
                 std::vector<std::string>({"load", "start", "pause", "resume", "stop"}));
        QVERIFY(!core.isRunning());
    }

    void rejectsIncompleteConfiguration() {
        FakeEmulatorCore core;
        const auto complete = validConfig();
        auto missingRom = complete;
        missingRom.romPath.clear();
        auto missingSave = complete;
        missingSave.savePath.clear();
        auto missingHash = complete;
        missingHash.romSha256.clear();

        QVERIFY(!core.load(missingRom).ok());
        QVERIFY(!core.load(missingSave).ok());
        QVERIFY(!core.load(missingHash).ok());
        QVERIFY(!core.isRunning());
    }

    void sequenceAdvancesPausesAndResumes() {
        FakeEmulatorCore core;
        QVERIFY(core.load(validConfig()).ok());
        QVERIFY(core.start().ok());
        QTRY_VERIFY_WITH_TIMEOUT(core.latestFrame() != nullptr, 250);
        const auto beforePause = core.latestFrame()->sequence;
        QTRY_VERIFY_WITH_TIMEOUT(core.latestFrame()->sequence > beforePause, 250);
        QVERIFY(core.pause().ok());
        const auto pausedSequence = core.latestFrame()->sequence;

        QTest::qWait(50);

        QCOMPARE(core.latestFrame()->sequence, pausedSequence);
        QVERIFY(core.resume().ok());
        QTRY_VERIFY_WITH_TIMEOUT(core.latestFrame()->sequence > pausedSequence, 250);
        QVERIFY(core.stop().ok());
    }

    void storesPressedPositiveButtonMask() {
        FakeEmulatorCore core;
        const auto pressed = static_cast<DsButtonMask>(
            static_cast<DsButtonMask>(DsButton::A) |
            static_cast<DsButtonMask>(DsButton::Start) |
            static_cast<DsButtonMask>(DsButton::Left));

        core.setButtons(pressed);

        QCOMPARE(core.buttons(), pressed);
    }

    void stopJoinsWorkerPromptly() {
        FakeEmulatorCore core;
        QVERIFY(core.load(validConfig()).ok());
        QVERIFY(core.start().ok());
        QTRY_VERIFY_WITH_TIMEOUT(core.latestFrame() != nullptr, 250);
        const auto started = std::chrono::steady_clock::now();

        QVERIFY(core.stop().ok());

        const auto elapsed = std::chrono::steady_clock::now() - started;
        QVERIFY2(elapsed < 250ms, "Fake core worker did not join within 250 ms");
        QVERIFY(!core.isRunning());
    }

    void overlappingStartAndStopLeavesNoWorkerBehind() {
        std::binary_semaphore stopThreadEntered{0};
        std::future<EmulationResult> stopFuture;
        bool stopWasBlockedByStart = false;
        FakeEmulatorCore core([&] {
            stopFuture = std::async(std::launch::async, [&] {
                stopThreadEntered.release();
                return core.stop();
            });
            const auto stopThreadStarted = stopThreadEntered.try_acquire_for(250ms);
            stopWasBlockedByStart =
                stopThreadStarted && stopFuture.wait_for(50ms) == std::future_status::timeout;
        });
        QVERIFY(core.load(validConfig()).ok());

        const auto startResult = core.start();

        QVERIFY2(stopWasBlockedByStart,
                 "stop() completed before start() installed the worker");
        QVERIFY(startResult.ok());
        QCOMPARE(stopFuture.wait_for(250ms), std::future_status::ready);
        QVERIFY(stopFuture.get().ok());
        QVERIFY(!core.isRunning());
    }
};

QTEST_GUILESS_MAIN(FakeEmulatorCoreTest)
#include "tst_fake_emulator.moc"
