// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppBootstrap.h"
#include "emulation/fake/FakeEmulatorCore.h"
#include <QtTest>
#include <QFile>
#include <QQuickWindow>
#include <QTemporaryDir>

namespace {
struct Probe {
    FakeEmulatorCore* core{};
    EmulationResult error;
    EmulationConfig config;
    bool stoppedBeforeSdl{false};
    bool audio{true};
    int privatePicks{};
};
class Api final : public SdlApi {
public:
    explicit Api(Probe& probe) : p(probe) {}
    bool init() override { return true; }
    std::optional<Event> pollEvent() override { return {}; }
    std::optional<InputDeviceKind> openGamepad(int) override { return InputDeviceKind::Xbox; }
    void closeGamepad(int) override {}
    bool openAudioStream() override { return p.audio; }
    bool putAudio(std::span<const std::int16_t>) override { return p.audio; }
    void shutdown() override { p.stoppedBeforeSdl = !p.core || !p.core->isRunning(); }
private:
    Probe& p;
};
AppBootstrap::Dependencies dependencies(Probe& p, RomStatus status = RomStatus::ValidHeartGold) {
    AppBootstrap::Dependencies deps;
    deps.sdl = std::make_shared<Api>(p);
    deps.validate = [status](const std::filesystem::path&) {
        return RomValidation{status, {}, "IPKF", std::string(64, 'a'), 512};
    };
    deps.pickRom = [] { return QStringLiteral("C:/private-test/selected.nds"); };
    deps.pickPrivateFiles = [&] { ++p.privatePicks; };
    deps.createCore = [&](const EmulationConfig& config, std::shared_ptr<SdlAudioSink>, const AppPaths&) {
        p.config = config;
        auto core = std::make_unique<FakeEmulatorCore>(); p.core = core.get(); return core;
    };
    deps.coreResult = [&](const EmulatorCore&) { return p.error; };
    return deps;
}
void press(UiState& ui, int key) { ui.handleKey(key, true); ui.handleKey(key, false); }
}
class ApplicationFlowTest final : public QObject {
    Q_OBJECT
private slots:
    // Catches bypassed validation and raw private paths in user-visible errors.
    void invalidRomReturnsToWelcomeWithReason_data() {
        QTest::addColumn<int>("status"); QTest::addColumn<QString>("reason");
        QTest::newRow("malformed") << int(RomStatus::MalformedHeader) << QStringLiteral("Ce fichier n’est pas une ROM HeartGold valide.");
        QTest::newRow("unsupported") << int(RomStatus::UnsupportedGame) << QStringLiteral("Ce prototype accepte uniquement Pokémon HeartGold.");
    }
    void invalidRomReturnsToWelcomeWithReason() {
        QFETCH(int, status); QFETCH(QString, reason);
        QTemporaryDir temp; Probe p;
        AppBootstrap app(false, temp.filePath("settings.ini"), temp.path(), dependencies(p, RomStatus(status)));
        app.start(); app.uiState()->chooseRom();
        QCOMPARE(app.uiState()->sessionState(), SessionState::NoRom);
        QVERIFY(app.uiState()->welcomeVisible());
        QCOMPARE(app.uiState()->statusMessage(), reason); QVERIFY(!p.core);
    }
    // Catches ignored worker errors, missing join, and private-path logs.
    void coreFailureReturnsToWelcomeAndKeepsLog() {
        QTemporaryDir temp; Probe p;
        AppBootstrap app(false, temp.filePath("settings.ini"), temp.path(), dependencies(p));
        app.start(); app.uiState()->chooseRom();
        QCOMPARE(app.uiState()->sessionState(), SessionState::Playing);
        p.error = {EmulationErrorCode::CoreFailure, "C:/private-test/selected.nds failure"};
        QTRY_COMPARE(app.uiState()->sessionState(), SessionState::NoRom);
        QCOMPARE(app.uiState()->statusMessage(), QStringLiteral("L’émulation s’est arrêtée. Un journal local a été conservé."));
        QVERIFY(!p.core->isRunning());
        QFile log(temp.filePath("HeartGoldPCRemastered/application.log"));
        QVERIFY(log.open(QIODevice::ReadOnly)); const auto bytes = log.readAll();
        QVERIFY(bytes.contains("CoreFailure")); QVERIFY(!bytes.contains("private-test"));
    }
    // Catches audio failure incorrectly preventing play.
    void missingAudioContinuesSessionSilently() {
        QTemporaryDir temp; Probe p; p.audio = false;
        AppBootstrap app(false, temp.filePath("settings.ini"), temp.path(), dependencies(p));
        app.start(); app.uiState()->chooseRom();
        QTRY_COMPARE(app.uiState()->bannerMessage(), QStringLiteral("Audio indisponible — la partie continue sans son."));
        QCOMPARE(app.uiState()->sessionState(), SessionState::Playing);
        QVERIFY(p.core->isRunning()); QVERIFY(!app.uiState()->blockingError());
    }
    // Catches resume and game input escaping an unacknowledged save failure.
    void saveFailurePausesAndBlocksResumeUntilDismissed() {
        QTemporaryDir temp; Probe p;
        AppBootstrap app(false, temp.filePath("settings.ini"), temp.path(), dependencies(p));
        app.start(); app.uiState()->chooseRom();
        QTRY_VERIFY(p.core->latestFrame());
        p.error = {EmulationErrorCode::SaveIo, "C:/private-test/native.sav denied"};
        QTRY_VERIFY(app.uiState()->blockingError());
        QCOMPARE(app.uiState()->statusMessage(), QStringLiteral("La sauvegarde ne peut pas être protégée. La partie est en pause."));
        const auto sequence = p.core->latestFrame()->sequence;
        app.uiState()->resumeGame(); press(*app.uiState(), Qt::Key_Escape);
        QTest::qWait(50); QCOMPARE(p.core->latestFrame()->sequence, sequence);
        QCOMPARE(app.uiState()->sessionState(), SessionState::MenuOpen);
        p.error = {}; app.uiState()->dismissError();
        QCOMPARE(app.uiState()->sessionState(), SessionState::MenuOpen);
        app.uiState()->resumeGame(); QCOMPARE(app.uiState()->sessionState(), SessionState::Playing);
    }
    // Catches lost state observers, menu input leaks, wrong save binding and close ordering.
    void choosePlayOpenMenuNavigateOptionsResumeStop() {
        QTemporaryDir temp; Probe p;
        AppBootstrap app(false, temp.filePath("settings.ini"), temp.path(), dependencies(p));
        QQuickWindow window; app.attachSurface(&window);
        std::vector<SessionState> order; auto& ui = *app.uiState();
        connect(&ui, &UiState::sessionStateChanged, &window, [&] { order.push_back(ui.sessionState()); });
        app.start(); ui.chooseRom();
        QCOMPARE(p.config.savePath.filename().string(), std::string("game.sav"));
        QCOMPARE(p.config.savePath.parent_path().filename().string(), std::string(64, 'a'));
        ui.handleKey(Qt::Key_Right, true); press(ui, Qt::Key_Tab);
        const auto calls = p.core->calls();
        press(ui, Qt::Key_Down); press(ui, Qt::Key_Right); press(ui, Qt::Key_Right);
        QCOMPARE(ui.selectedTile(), 6); press(ui, Qt::Key_Return); QVERIFY(ui.optionsOpen());
        press(ui, Qt::Key_Left); QCOMPARE(ui.volume(), 95);
        press(ui, Qt::Key_Down); press(ui, Qt::Key_Return); QVERIFY(ui.smoothScaling());
        press(ui, Qt::Key_Escape); QVERIFY(!ui.optionsOpen());
        QCOMPARE(p.core->calls(), calls); QCOMPARE(p.core->buttons(), DsButtonMask{0});
        press(ui, Qt::Key_Right); press(ui, Qt::Key_Return);
        QCOMPARE(ui.sessionState(), SessionState::Playing); QCOMPARE(p.core->buttons(), DsButtonMask{0});
        window.show(); window.close();
        QTRY_COMPARE(ui.sessionState(), SessionState::NoRom);
        QVERIFY(!p.core->isRunning()); QVERIFY(p.stoppedBeforeSdl);
        QCOMPARE(order, std::vector<SessionState>({SessionState::Loading, SessionState::Playing,
            SessionState::MenuOpen, SessionState::Playing, SessionState::Stopping, SessionState::NoRom}));
    }
    void missingPrivateFileRequestsPickerWithoutExposingPath() {
        QTemporaryDir temp; Probe p;
        AppBootstrap app(false, temp.filePath("settings.ini"), temp.path(), dependencies(p));
        app.start(); app.uiState()->chooseRom();
        p.error = {EmulationErrorCode::MissingPrivateFile, "C:/private-test/bios.bin"};
        QTRY_COMPARE(p.privatePicks, 1);
        QCOMPARE(app.uiState()->statusMessage(), QStringLiteral("Sélectionnez vos fichiers système DS pour utiliser ce mode."));
        QVERIFY(app.uiState()->welcomeVisible());
    }
    void commandLineRomIsNeverRememberedAndNextSelectionRebindsSave() {
        QTemporaryDir temp; Probe p; auto deps = dependencies(p); int selections = 0;
        deps.validate = [&](const std::filesystem::path&) {
            return RomValidation{RomStatus::ValidHeartGold, {}, "IPKF", std::string(64, ++selections == 1 ? 'a' : 'b'), 512};
        };
        AppBootstrap app(false, temp.filePath("settings.ini"), temp.path(), std::move(deps));
        app.uiState()->setOption("reopenLastGame", true);
        app.start(QStringLiteral("C:/private-test/command.nds"));
        QCOMPARE(app.uiState()->sessionState(), SessionState::Playing);
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        QVERIFY(!settings.contains("lastRomPath"));
        app.uiState()->stopSession(); app.uiState()->chooseRom();
        QCOMPARE(app.uiState()->sessionState(), SessionState::Playing);
        QCOMPARE(p.config.romSha256, std::string(64, 'b'));
        QCOMPARE(p.config.savePath.parent_path().filename().string(), std::string(64, 'b'));
    }
    void queuedRecoveryCannotStopAReplacementSession() {
        QTemporaryDir temp; Probe p;
        AppBootstrap app(false, temp.filePath("settings.ini"), temp.path(), dependencies(p));
        app.start(); app.uiState()->chooseRom();
        app.uiState()->reportEmulationError({EmulationErrorCode::CoreFailure, {}});
        app.uiState()->stopSession(); app.uiState()->chooseRom();
        QCOMPARE(app.uiState()->sessionState(), SessionState::Playing);
        QTest::qWait(40);
        QCOMPARE(app.uiState()->sessionState(), SessionState::Playing);
    }
};
QTEST_MAIN(ApplicationFlowTest)
#include "tst_application_flow.moc"
