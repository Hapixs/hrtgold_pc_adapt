#include "emulation/fake/FakeEmulatorCore.h"

#include <QtTest>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <optional>
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
        std::binary_semaphore startBeforeWorker{0};
        std::binary_semaphore releaseStart{0};
        std::binary_semaphore stopBeforeLock{0};
        std::binary_semaphore releaseStop{0};
        std::binary_semaphore stopAfterLock{0};
        bool startGateTimedOut = false;
        bool stopGateTimedOut = false;
        FakeEmulatorCore core(FakeEmulatorCore::TestHooks{
            .beforeWorkerStart = [&] {
                startBeforeWorker.release();
                startGateTimedOut = !releaseStart.try_acquire_for(3s);
            },
            .beforeStopLock = [&] {
                stopBeforeLock.release();
                stopGateTimedOut = !releaseStop.try_acquire_for(3s);
            },
            .afterStopLock = [&] { stopAfterLock.release(); },
        });
        const auto loadResult = core.load(validConfig());
        std::future<EmulationResult> startFuture =
            std::async(std::launch::async, [&] { return core.start(); });

        const auto startReachedBoundary = startBeforeWorker.try_acquire_for(2s);
        std::future<EmulationResult> stopFuture;
        bool stopReachedBoundary = false;
        bool stopAcquiredBeforeStartReleased = false;
        if (startReachedBoundary) {
            stopFuture = std::async(std::launch::async, [&] { return core.stop(); });
            stopReachedBoundary = stopBeforeLock.try_acquire_for(2s);
            releaseStop.release();
            if (stopReachedBoundary) {
                stopAcquiredBeforeStartReleased = stopAfterLock.try_acquire_for(1s);
            }
        }

        releaseStart.release();
        const auto startReady = startFuture.wait_for(5s) == std::future_status::ready;
        const auto startResult = startFuture.get();

        bool stopReady = false;
        std::optional<EmulationResult> stopResult;
        if (stopFuture.valid()) {
            stopReady = stopFuture.wait_for(5s) == std::future_status::ready;
            stopResult = stopFuture.get();
        } else if (startResult.ok()) {
            releaseStop.release();
            stopResult = core.stop();
            stopReady = true;
        }
        const auto stopReachedEventually =
            stopAcquiredBeforeStartReleased || stopAfterLock.try_acquire_for(2s);

        QVERIFY(loadResult.ok());
        QVERIFY(startReachedBoundary);
        QVERIFY(stopReachedBoundary);
        QVERIFY(!startGateTimedOut);
        QVERIFY(!stopGateTimedOut);
        QVERIFY2(!stopAcquiredBeforeStartReleased,
                 "stop() acquired the lifecycle lock before start() installed the worker");
        QVERIFY(stopReachedEventually);
        QVERIFY(startReady);
        QVERIFY(startResult.ok());
        QVERIFY(stopReady);
        QVERIFY(stopResult.has_value());
        QVERIFY(stopResult->ok());
        QVERIFY(!core.isRunning());
    }
};

QTEST_GUILESS_MAIN(FakeEmulatorCoreTest)
#include "tst_fake_emulator.moc"
