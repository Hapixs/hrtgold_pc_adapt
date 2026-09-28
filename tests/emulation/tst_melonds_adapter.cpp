#include "emulation/melonds/MelonDsAdapter.h"
#include "emulation/melonds/testing/MelonDsTestAccess.h"
#include "emulation/melonds/MelonConversions.h"
#include "platform/sdl/SdlAudioSink.h"
#include <QtTest>
#include <atomic>
#include <fstream>
#include <thread>

class AudioRecorder final : public SdlApi {
public:
    std::atomic<int> submissions{0};
    std::atomic<std::size_t> maximumSamples{0};
    bool init() override { return true; }
    std::optional<Event> pollEvent() override { return {}; }
    std::optional<InputDeviceKind> openGamepad(int) override { return {}; }
    void closeGamepad(int) override {}
    bool openAudioStream() override { return true; }
    bool putAudio(std::span<const std::int16_t> samples) override {
        maximumSamples = std::max(maximumSamples.load(), samples.size());
        ++submissions;
        return true;
    }
    void shutdown() override {}
};

class MelonDsAdapterTest final : public QObject {
    Q_OBJECT
    static QByteArray syntheticHeader() {
        // Generated zero-logo header only: never executed and not a game dump.
        QByteArray bytes(0x9000, '\0');
        bytes.replace(0xC, 4, "IPKF");
        const auto put32 = [&](int offset, unsigned value) {
            for (int i = 0; i < 4; ++i) bytes[offset+i] = static_cast<char>(value >> (8*i));
        };
        put32(0x20, 0x8000); put32(0x24, 0x02000000); put32(0x28, 0x02000000); put32(0x2C, 4);
        put32(0x30, 0x8004); put32(0x34, 0x03800000); put32(0x38, 0x03800000); put32(0x3C, 4);
        return bytes;
    }
    static EmulationConfig writeHeader(const std::filesystem::path& root, const QByteArray& bytes) {
        const auto path = root / "generated-header.nds";
        std::ofstream file(path, std::ios::binary);
        file.write(bytes.data(), bytes.size());
        const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
        return {path, root / hash / "game.sav", hash};
    }
private slots:
    void bufferSizesAvoidNarrowingAndUpstreamPaddingOverflow() {
        QVERIFY(MelonDsTestAccess::bufferSizesAccepted(0x1000, 0));
        QVERIFY(MelonDsTestAccess::bufferSizesAccepted(0x80000000ULL, 0xFFFFFFFFULL));
        QVERIFY(!MelonDsTestAccess::bufferSizesAccepted(0x80000001ULL, 0));
        QVERIFY(!MelonDsTestAccess::bufferSizesAccepted(0xFFFFFFFFULL, 0));
        QVERIFY(!MelonDsTestAccess::bufferSizesAccepted(0x100000000ULL, 0));
        QVERIFY(!MelonDsTestAccess::bufferSizesAccepted(0x1000, 0x100000000ULL));
        QVERIFY(!MelonDsTestAccess::bufferSizesAccepted(0, 0));
        QVERIFY(!MelonDsTestAccess::bufferSizesAccepted(0xFFF, 0));
    }
    void syntheticHeaderLoadsSaveAndRejectsOversizedSaveBeforeBoot() {
        QTemporaryDir temporary;
        const std::filesystem::path root(temporary.path().toStdWString());
        const auto config = writeHeader(root, syntheticHeader());
        SaveRepository repository(config.savePath.parent_path(), std::chrono::system_clock::now);
        QCOMPARE(repository.replace(std::vector<std::uint8_t>{3, 7, 11}), SaveWriteError::None);
        std::vector<std::uint8_t> save;
        const auto result = MelonDsTestAccess::inspectCartridge(config, repository, save);
        QVERIFY2(result.ok(), result.message.c_str());
        QCOMPARE(save.size(), std::size_t{524288});
        QCOMPARE(save[0], 3); QCOMPARE(save[1], 7); QCOMPARE(save[2], 11);
        QCOMPARE(save.back(), 255);
        QCOMPARE(repository.replace(std::vector<std::uint8_t>(524289, 42)), SaveWriteError::None);
        QCOMPARE(MelonDsTestAccess::inspectCartridge(config, repository, save).code, EmulationErrorCode::SaveIo);
        MelonDsAdapter adapter(repository, {}, root);
        QVERIFY(adapter.load(config).ok());
        QCOMPARE(adapter.start().code, EmulationErrorCode::SaveIo);
        QVERIFY(!adapter.isRunning());
    }
    void unsafeDirectBootHeadersAreRejected_data() {
        QTest::addColumn<int>("offset"); QTest::addColumn<unsigned>("value");
        QTest::newRow("arm9-outside") << 0x20 << 0xFFFFFFFCU;
        QTest::newRow("arm9-zero") << 0x2C << 0U;
        QTest::newRow("arm9-unaligned") << 0x2C << 3U;
        QTest::newRow("arm7-outside") << 0x30 << 0xFFFFFFFCU;
        QTest::newRow("arm7-too-long") << 0x3C << 0xFFFFFFFFU;
        QTest::newRow("arm9-bad-ram") << 0x28 << 0x04000000U;
        QTest::newRow("arm7-bad-entry") << 0x34 << 0x04000000U;
        QTest::newRow("other-game") << 0x0C << 0x41424344U;
        QTest::newRow("dsi") << 0x12 << 2U;
    }
    void unsafeDirectBootHeadersAreRejected() {
        QFETCH(int, offset); QFETCH(unsigned, value);
        QTemporaryDir temporary;
        const std::filesystem::path root(temporary.path().toStdWString());
        auto bytes = syntheticHeader();
        for (int i=0; i<4; ++i) bytes[offset+i] = static_cast<char>(value >> (8*i));
        const auto config = writeHeader(root, bytes);
        SaveRepository repository(config.savePath.parent_path(), std::chrono::system_clock::now);
        std::vector<std::uint8_t> save;
        QCOMPARE(MelonDsTestAccess::inspectCartridge(config, repository, save).code, EmulationErrorCode::InvalidRom);
    }
    void directBootRequiresMatchingHashAndSettings() {
        QTemporaryDir temporary;
        const std::filesystem::path root(temporary.path().toStdWString());
        auto config = writeHeader(root, syntheticHeader());
        SaveRepository repository(config.savePath.parent_path(), std::chrono::system_clock::now);
        MelonDsAdapter adapter(repository, {}, root);
        auto wrong = config; wrong.useDirectBoot = false;
        QVERIFY(!adapter.load(wrong).ok());
        wrong = config; wrong.sampleRate = 0;
        QVERIFY(!adapter.load(wrong).ok());
        wrong = config; wrong.romPath = "relative.nds";
        QVERIFY(!adapter.load(wrong).ok());
        config.romSha256.assign(64, 'f');
        std::vector<std::uint8_t> save;
        QCOMPARE(MelonDsTestAccess::inspectCartridge(config, repository, save).code, EmulationErrorCode::InvalidRom);
    }
    void allButtonsUseActiveLowCoreBits_data() {
        QTest::addColumn<unsigned>("pressed");
        QTest::addColumn<unsigned>("expected");
        QTest::newRow("none") << 0U << 0xFFFU;
        QTest::newRow("A") << 1U << 0xFFEU;
        QTest::newRow("B") << 2U << 0xFFDU;
        QTest::newRow("Select") << 4U << 0xFFBU;
        QTest::newRow("Start") << 8U << 0xFF7U;
        QTest::newRow("Right") << 16U << 0xFEFU;
        QTest::newRow("Left") << 32U << 0xFDFU;
        QTest::newRow("Up") << 64U << 0xFBFU;
        QTest::newRow("Down") << 128U << 0xF7FU;
        QTest::newRow("R") << 256U << 0xEFFU;
        QTest::newRow("L") << 512U << 0xDFFU;
        QTest::newRow("X") << 1024U << 0xBFFU;
        QTest::newRow("Y") << 2048U << 0x7FFU;
        QTest::newRow("all") << 0xFFFU << 0U;
        QTest::newRow("reserved") << 0xF000U << 0xFFFU;
    }
    void allButtonsUseActiveLowCoreBits() {
        QFETCH(unsigned, pressed);
        QFETCH(unsigned, expected);
        QCOMPARE(toActiveLowKeyMask(static_cast<DsButtonMask>(pressed)), expected);
    }
    void pixelChannelsMatchTheProjectFrameContract() {
        QCOMPARE(toProjectRgba(0xFF123456U), 0xFF123456U);
        QCOMPARE(toProjectRgba(0x00123456U), 0xFF123456U);
        QCOMPARE(toProjectRgba(0xFFFF0000U), 0xFFFF0000U);
        QCOMPARE(toProjectRgba(0xFF0000FFU), 0xFF0000FFU);
    }
    void freeBiosProducesAnUpperFrameWithoutNintendoFiles() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const std::filesystem::path root(temporary.path().toStdWString());
        SaveRepository saves(root / std::string(64, 'a'), std::chrono::system_clock::now);
        MelonDsAdapter adapter(std::move(saves), {}, root);
        QVERIFY(MelonDsTestAccess::startFreeBios(adapter).ok());
        QTRY_VERIFY_WITH_TIMEOUT(adapter.latestFrame() && adapter.latestFrame()->sequence >= 3, 3000);
        const auto frame = adapter.latestFrame();
        QCOMPARE(frame->width, 256);
        QCOMPARE(frame->height, 192);
        QVERIFY(adapter.stop().ok());
    }
    void lifecyclePausesFramesAndAudioAndStopsPromptly() {
        QTemporaryDir temporary;
        const std::filesystem::path root(temporary.path().toStdWString());
        AudioRecorder api;
        auto sink = std::make_shared<SdlAudioSink>(api, true);
        const std::weak_ptr<SdlAudioSink> weak = sink;
        MelonDsAdapter adapter(SaveRepository(root / std::string(64, 'a'), std::chrono::system_clock::now), sink, root);
        sink.reset();
        QVERIFY(!weak.expired());
        QVERIFY(!adapter.start().ok());
        QVERIFY(!adapter.isRunning());
        QVERIFY(!adapter.pause().ok());
        for (int restart = 0; restart < 2; ++restart) {
            QVERIFY(MelonDsTestAccess::startFreeBios(adapter).ok());
            QVERIFY(adapter.isRunning());
            QVERIFY(!adapter.start().ok());
            QTRY_VERIFY_WITH_TIMEOUT(adapter.latestFrame() && api.submissions > 0, 3000);
            for (int cycle = 0; cycle < 3; ++cycle) {
                QVERIFY(adapter.pause().ok());
                QVERIFY(adapter.isRunning());
                const auto sequence = adapter.latestFrame()->sequence;
                const auto submissions = api.submissions.load();
                QTest::qWait(35);
                QCOMPARE(adapter.latestFrame()->sequence, sequence);
                QCOMPARE(api.submissions.load(), submissions);
                adapter.setButtons(static_cast<DsButtonMask>(DsButton::A));
                QVERIFY(adapter.resume().ok());
                QTRY_VERIFY_WITH_TIMEOUT(adapter.latestFrame()->sequence > sequence, 1000);
            }
            QVERIFY(adapter.pause().ok());
            QElapsedTimer timer;
            timer.start();
            QVERIFY(adapter.stop().ok());
            QVERIFY(timer.elapsed() < 500);
            QVERIFY(!adapter.isRunning());
        }
        QVERIFY(api.maximumSamples > 0);
        QVERIFY(api.maximumSamples <= 2048);
    }
    void signalStopWakesPausedWorkerAndPreservesReason() {
        QTemporaryDir temporary;
        const std::filesystem::path root(temporary.path().toStdWString());
        MelonDsAdapter adapter(SaveRepository(root / std::string(64, 'a'), std::chrono::system_clock::now), {}, root);
        QVERIFY(MelonDsTestAccess::startFreeBios(adapter).ok());
        QVERIFY(adapter.pause().ok());
        MelonDsTestAccess::signalPowerOff(adapter);
        QTRY_VERIFY_WITH_TIMEOUT(!adapter.isRunning(), 500);
        QVERIFY(adapter.lastResult().message.find("PowerOff") != std::string::npos);
        QVERIFY(adapter.stop().ok());
    }
    void saveCallbackCopiesEntireBufferAndWritesOnWorker() {
        QTemporaryDir temporary;
        const std::filesystem::path root(temporary.path().toStdWString());
        const auto directory = root / std::string(64, 'a');
        std::thread::id writer;
        std::atomic<bool> recorded{false};
        SaveRepository repository(directory, [&] { writer = std::this_thread::get_id(); recorded = true; return std::chrono::system_clock::now(); });
        const std::vector<std::uint8_t> original{1,2,3,4};
        QCOMPARE(repository.replace(original), SaveWriteError::None);
        MelonDsAdapter adapter(repository, {}, root);
        QVERIFY(MelonDsTestAccess::startFreeBios(adapter).ok());
        QVERIFY(adapter.pause().ok());
        std::vector<std::uint8_t> changed{8,7,6,5};
        MelonDsTestAccess::writeSave(adapter, changed, 2, 1);
        changed.assign(4, 0);
        QTRY_VERIFY_WITH_TIMEOUT(recorded.load(), 1000);
        QVERIFY(adapter.stop().ok()); // joins and flushes before reading
        QCOMPARE(repository.load(), (std::vector<std::uint8_t>{8,7,6,5}));
        QVERIFY(writer != std::this_thread::get_id());
        QVERIFY(!std::filesystem::exists(directory/"game.sav.tmp"));
    }
    void workerStartupFailureIsNotReportedRunning() {
        QTemporaryDir temporary;
        const std::filesystem::path root(temporary.path().toStdWString());
        MelonDsAdapter adapter(SaveRepository(root / std::string(64, 'a'), std::chrono::system_clock::now), {}, root);
        EmulationConfig config{root/"absent.nds", root/std::string(64, 'a')/"game.sav", std::string(64, 'a')};
        QVERIFY(adapter.load(config).ok());
        QVERIFY(!adapter.start().ok());
        QVERIFY(!adapter.isRunning());
        QVERIFY(adapter.load(config).ok());
        QVERIFY(!adapter.start().ok());
        QVERIFY(!adapter.isRunning());
    }
    void firstFrameAfterRestartHasANewerSequence() {
        QTemporaryDir temporary;
        const std::filesystem::path root(temporary.path().toStdWString());
        MelonDsAdapter adapter(SaveRepository(root/std::string(64, 'a'), std::chrono::system_clock::now), {}, root);
        QVERIFY(MelonDsTestAccess::startFreeBios(adapter).ok());
        QTRY_VERIFY_WITH_TIMEOUT(adapter.latestFrame() && adapter.latestFrame()->sequence >= 8, 2000);
        QElapsedTimer timer; timer.start();
        QVERIFY(adapter.stop().ok());
        QVERIFY(timer.elapsed() < 500); // stop while running, not only from pause
        const auto before = adapter.latestFrame()->sequence;
        QVERIFY(MelonDsTestAccess::startFreeBios(adapter).ok());
        QTRY_VERIFY_WITH_TIMEOUT(adapter.latestFrame()->sequence != before, 1000);
        QVERIFY(adapter.pause().ok());
        QVERIFY(adapter.latestFrame()->sequence > before);
        QVERIFY(adapter.stop().ok());
    }
    void unreadableSaveIsNotTreatedAsAnEmptySave() {
        QTemporaryDir temporary;
        const std::filesystem::path root(temporary.path().toStdWString());
        const auto config = writeHeader(root, syntheticHeader());
        std::filesystem::create_directories(config.savePath);
        SaveRepository repository(config.savePath.parent_path(), std::chrono::system_clock::now);
        std::vector<std::uint8_t> bytes;
        QCOMPARE(MelonDsTestAccess::inspectCartridge(config, repository, bytes).code, EmulationErrorCode::SaveIo);
    }
    void saveIoErrorStopsWorkerAndRemainsVisible() {
        QTemporaryDir temporary;
        const std::filesystem::path root(temporary.path().toStdWString());
        const auto directory = root/std::string(64, 'a');
        std::filesystem::create_directories(directory/"game.sav");
        MelonDsAdapter adapter(SaveRepository(directory, std::chrono::system_clock::now), {}, root);
        QVERIFY(MelonDsTestAccess::startFreeBios(adapter).ok());
        MelonDsTestAccess::writeSave(adapter, std::vector<std::uint8_t>{1,2,3}, 0, 3);
        QTRY_VERIFY_WITH_TIMEOUT(!adapter.isRunning(), 1000);
        QCOMPARE(adapter.lastResult().code, EmulationErrorCode::SaveIo);
        QCOMPARE(adapter.stop().code, EmulationErrorCode::SaveIo);
        QVERIFY(std::filesystem::is_directory(directory/"game.sav"));
    }
    void generatedFirmwareIsWrittenUnderAppDataOnly() {
        QTemporaryDir temporary;
        const std::filesystem::path root(temporary.path().toStdWString());
        MelonDsAdapter adapter(SaveRepository(root/std::string(64, 'a'), std::chrono::system_clock::now), {}, root);
        QVERIFY(MelonDsTestAccess::startFreeBios(adapter).ok());
        QVERIFY(adapter.pause().ok());
        const auto expected = MelonDsTestAccess::writeGeneratedFirmware(adapter);
        QVERIFY(adapter.stop().ok());
        std::ifstream input(root/"generated-firmware.bin", std::ios::binary);
        const std::vector<std::uint8_t> actual((std::istreambuf_iterator<char>(input)), {});
        QCOMPARE(actual, expected);
    }
};
QTEST_GUILESS_MAIN(MelonDsAdapterTest)
#include "tst_melonds_adapter.moc"
