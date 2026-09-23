#include "emulation/fake/FakeEmulatorCore.h"

#include <QtTest>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
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
        for (int attempt = 0; attempt < 100; ++attempt) {
            FakeEmulatorCore core;
            QVERIFY(core.load(validConfig()).ok());
            EmulationResult startResult;

            std::thread starter([&] { startResult = core.start(); });
            while (!core.isRunning()) {
                std::this_thread::yield();
            }
            const auto stopStarted = std::chrono::steady_clock::now();
            const auto stopResult = core.stop();
            starter.join();

            QVERIFY(startResult.ok());
            QVERIFY(stopResult.ok());
            QVERIFY(std::chrono::steady_clock::now() - stopStarted < 250ms);
            QVERIFY(!core.isRunning());
            QVERIFY2(core.start().ok(), "A worker survived an overlapping start/stop");
            QVERIFY(core.stop().ok());
        }
    }
};

QTEST_GUILESS_MAIN(FakeEmulatorCoreTest)
#include "tst_fake_emulator.moc"
