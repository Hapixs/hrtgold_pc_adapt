#include "emulation/melonds/MelonPlatformContext.h"
#include "Platform.h"
#include "SPI_Firmware.h"
#include <QtTest>
#include <atomic>
#include <fstream>
#include <thread>

using namespace melonDS::Platform;
class MelonPlatformTest final : public QObject {
    Q_OBJECT
private slots:
    void inactiveCameraHonorsBufferCapacity_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::addColumn<bool>("yuv");
        QTest::addColumn<unsigned>("words");
        QTest::addColumn<unsigned>("black");
        QTest::newRow("rgb-small") << 2 << 2 << false << 4U << 0U;
        QTest::newRow("rgb-odd") << 3 << 3 << false << 9U << 0U;
        QTest::newRow("rgb-640x480") << 640 << 480 << false << 307200U << 0U;
        QTest::newRow("yuv-small") << 2 << 2 << true << 2U << 0x80008000U;
        QTest::newRow("yuv-actual-core-buffer") << 640 << 480 << true << 153600U << 0x80008000U;
        QTest::newRow("zero-width") << 0 << 480 << true << 0U << 0U;
        QTest::newRow("zero-height") << 640 << 0 << false << 0U << 0U;
        QTest::newRow("negative-width") << -1 << 2 << true << 0U << 0U;
        QTest::newRow("negative-height") << 2 << -1 << false << 0U << 0U;
        QTest::newRow("too-wide") << 641 << 480 << false << 0U << 0U;
        QTest::newRow("too-high") << 640 << 481 << true << 0U << 0U;
        QTest::newRow("odd-yuv-width") << 3 << 2 << true << 0U << 0U;
        QTest::newRow("odd-yuv-height") << 2 << 3 << true << 0U << 0U;
    }
    void inactiveCameraHonorsBufferCapacity() {
        QFETCH(int, width); QFETCH(int, height); QFETCH(bool, yuv);
        QFETCH(unsigned, words); QFETCH(unsigned, black);
        constexpr melonDS::u32 canary = 0xABCDEF01U;
        // The logical buffer is exactly 'words'. A large trailing red zone
        // catches the old full-RGB-size YUV write without crashing the test.
        std::vector<melonDS::u32> guarded(307202, canary);
        Camera_CaptureFrame(0, guarded.data()+1, width, height, yuv, nullptr);
        QCOMPARE(guarded.front(), canary);
        QVERIFY(std::all_of(guarded.begin()+1, guarded.begin()+1+words,
                            [black](auto pixel){ return pixel == black; }));
        QVERIFY(std::all_of(guarded.begin()+1+words, guarded.end(),
                            [](auto pixel){ return pixel == canary; }));
        Camera_CaptureFrame(0, nullptr, width, height, yuv, nullptr);
    }
    void filesAreConfinedAndPreserve64BitPositions() {
        QTemporaryDir temporary;
        MelonPlatformContext context;
        context.dataDirectory = std::filesystem::path(temporary.path().toStdWString());
        MelonPlatformScope scope(context);
        QVERIFY(!OpenLocalFile("../escape", Write));
        QVERIFY(!OpenFile("relative", Write));
        QVERIFY(!OpenFile((context.dataDirectory.parent_path()/"escape").string(), Write));
        QVERIFY(!OpenLocalFile("missing", ReadWriteExisting));
        auto* file = OpenLocalFile("unicode-é.dat", ReadWrite);
        QVERIFY(file);
        QCOMPARE(FileWriteFormatted(file, "%s", "abc\n"), 4ULL);
        QVERIFY(FileFlush(file));
        QVERIFY(FileSeek(file, 0x100000000LL, FileSeekOrigin::Start));
        QCOMPARE(FilePosition(file), 0x100000000ULL);
        // Sparse seek checks the actual 64-bit position without a multi-GB write.
        QCOMPARE(FileLength(file), 4ULL);
        QCOMPARE(FilePosition(file), 0x100000000ULL);
        FileRewind(file);
        char line[8]{};
        QVERIFY(FileReadLine(line, 8, file));
        QCOMPARE(QByteArray(line), QByteArray("abc\n"));
        QCOMPARE(FileRead(line, 1, 1, file), 0ULL);
        QVERIFY(IsEndOfFile(file));
        QVERIFY(CloseFile(file));
        file = OpenLocalFile("unicode-é.dat", static_cast<FileMode>(Write|Preserve));
        QVERIFY(file);
        QCOMPARE(FileWrite("Z", 1, 1, file), 1ULL);
        QVERIFY(CloseFile(file));
        file = OpenLocalFile("unicode-é.dat", Read);
        QCOMPARE(FileRead(line, 1, 4, file), 4ULL);
        QCOMPARE(QByteArray(line, 4), QByteArray("Zbc\n"));
        CloseFile(file);
        QVERIFY(CheckLocalFileWritable("unicode-é.dat"));
        QVERIFY(LocalFileExists("unicode-é.dat"));
        QVERIFY(!OpenLocalFile("unicode-é.dat", None));
    }
    void synchronizationAndTiming() {
        auto* sem = Semaphore_Create();
        auto* mutex = Mutex_Create();
        std::atomic<bool> acquired{false};
        Mutex_Lock(mutex);
        auto* worker = Thread_Create([&] {
            acquired = Mutex_TryLock(mutex);
            if (acquired) Mutex_Unlock(mutex);
            Semaphore_Post(sem);
        });
        QVERIFY(Semaphore_TryWait(sem, 500));
        QVERIFY(!acquired);
        Thread_Wait(worker);
        Thread_Free(worker);
        Mutex_Unlock(mutex);
        QVERIFY(Mutex_TryLock(mutex));
        Mutex_Unlock(mutex);
        Semaphore_Post(sem, 2);
        Semaphore_Wait(sem);
        Semaphore_Reset(sem);
        QVERIFY(!Semaphore_TryWait(sem));
        const auto before = GetUSCount();
        Sleep(2000);
        QVERIFY(GetUSCount() >= before + 1000);
        QVERIFY(GetMSCount() <= GetUSCount()/1000);
        Semaphore_Free(sem);
        Mutex_Free(mutex);
    }
    void optionalHardwareIsInactiveAndMicrophoneSilent() {
        std::array<melonDS::s16, 32> mic;
        mic.fill(42);
        Mic_Start(nullptr);
        QCOMPARE(Mic_ReadInput(mic.data(), 32, nullptr), 32);
        QVERIFY(std::ranges::all_of(mic, [](auto sample){ return sample == 0; }));
        Mic_Stop(nullptr);
        QCOMPARE(Net_SendPacket(nullptr, 0, nullptr), 0);
        QCOMPARE(Net_RecvPacket(nullptr, nullptr), 0);
        MP_Begin(nullptr);
        QCOMPARE(MP_SendPacket(nullptr, 0, 0, nullptr), 0);
        QCOMPARE(MP_SendCmd(nullptr, 0, 0, nullptr), 0);
        QCOMPARE(MP_SendReply(nullptr, 0, 0, 0, nullptr), 0);
        QCOMPARE(MP_SendAck(nullptr, 0, 0, nullptr), 0);
        QCOMPARE(MP_RecvPacket(nullptr, nullptr, nullptr), 0);
        QCOMPARE(MP_RecvHostPacket(nullptr, nullptr, nullptr), 0);
        QCOMPARE(MP_RecvReplies(nullptr, 0, 0, nullptr), 0);
        MP_End(nullptr);
        QVERIFY(!AAC_Init());
        QVERIFY(!AAC_Configure(nullptr, 48000, 2));
        QVERIFY(!AAC_DecodeFrame(nullptr, nullptr, 0, nullptr, 0));
        AAC_DeInit(nullptr);
        QVERIFY(!Addon_KeyDown(KeyGuitarGripGreen, nullptr));
        QCOMPARE(Addon_MotionQuery(MotionAccelerationX, nullptr), 0.0f);
        Addon_RumbleStart(1, nullptr);
        Addon_RumbleStop(nullptr);
        std::array<melonDS::u32, 4> pixels{1,2,3,4};
        Camera_Start(0, nullptr);
        Camera_CaptureFrame(0, pixels.data(), 2, 2, false, nullptr);
        QVERIFY(std::ranges::all_of(pixels, [](auto pixel){ return pixel == 0; }));
        Camera_Stop(0, nullptr);
    }
    void loggingIsBoundedRedactedAndInheritedByThreads() {
        QTemporaryDir temporary;
        MelonPlatformContext context;
        context.dataDirectory = std::filesystem::path(temporary.path().toStdWString());
        context.sensitiveDirectories = {"C:/private/roms", "C:/private/saves"};
        MelonPlatformScope scope(context);
        auto* worker = Thread_Create([] {
            for (int i = 0; i < 4000; ++i)
                Log(Info, "C:/private/roms/game.nds C:\\private\\saves\\game.sav %s\n", "details");
        });
        Thread_Free(worker); // joins without terminating a running core thread
        for (const auto* name : {"melonds.log", "melonds.log.1"}) {
            const auto path = context.dataDirectory / name;
            QVERIFY(std::filesystem::exists(path));
            QVERIFY(std::filesystem::file_size(path) <= 65536);
            std::ifstream stream(path);
            std::string contents((std::istreambuf_iterator<char>(stream)), {});
            QVERIFY(contents.find("private") == std::string::npos);
            QVERIFY(contents.find("[redacted]") != std::string::npos);
        }
    }
    void dynamicLibraryBoundary() {
        auto* lib = DynamicLibrary_Load("kernel32.dll");
        QVERIFY(lib);
        QVERIFY(DynamicLibrary_LoadFunction(lib, "GetCurrentProcessId"));
        QVERIFY(!DynamicLibrary_LoadFunction(lib, "NotARealFunction_HeartGold"));
        DynamicLibrary_Unload(lib);
    }
};
QTEST_GUILESS_MAIN(MelonPlatformTest)
#include "tst_melon_platform.moc"
