#include "ui/UiState.h"

#include <QSettings>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include <memory>
#include <vector>

namespace {
class ProbeCore final : public EmulatorCore {
public:
    EmulationResult load(const EmulationConfig& config) override {
        if (!loadResult.ok()) return loadResult;
        loaded = config;
        ++loads;
        return {};
    }
    EmulationResult start() override { running = true; return {}; }
    EmulationResult pause() override { ++pauses; return pauseResult; }
    EmulationResult resume() override { ++resumes; return {}; }
    EmulationResult stop() override {
        ++stops;
        if (!stopResult.ok()) return stopResult;
        running = false;
        return {};
    }
    void setButtons(DsButtonMask buttons) override { lastButtons = buttons; }
    std::shared_ptr<const VideoFrame> latestFrame() const override { return {}; }
    bool isRunning() const noexcept override { return running; }
    EmulationConfig loaded;
    int loads{0};
    int pauses{0};
    int resumes{0};
    int stops{0};
    bool running{false};
    DsButtonMask lastButtons{0};
    EmulationResult loadResult;
    EmulationResult pauseResult;
    EmulationResult stopResult;
};

EmulationConfig fakeConfig() {
    return {std::filesystem::path{L"C:/fake/heartgold.nds"},
            std::filesystem::path{L"C:/fake/save.sav"}, std::string(64, 'a')};
}

QString validRom(QTemporaryDir& temp) {
    const auto path = temp.filePath(QStringLiteral("valid.nds"));
    QByteArray bytes(0x200, '\0');
    bytes.replace(0x0c, 4, "IPKF");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    file.close();
    return path;
}
}

class UiStateTest final : public QObject {
    Q_OBJECT
private slots:
    void backgroundMutePersistsAndCanBeNavigatedByController() {
        QTemporaryDir temp;
        const auto settingsFile = temp.filePath("settings.ini");
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        {
            QSettings settings(settingsFile, QSettings::IniFormat);
            SessionController session(std::make_unique<ProbeCore>());
            InputRouter router;
            UiState state(session, router, validator, paths, settings);
            QVERIFY(session.load(fakeConfig()).ok());
            QVERIFY(session.openMenu().ok());
            state.setSelectedTile(6); state.activateSelected();
            for (int step = 0; step < 4; ++step)
                state.handleInput({InputAction::MoveDown, InputPhase::Pressed, InputDeviceKind::Xbox});
            QCOMPARE(state.selectedOption(), 4);
            state.handleInput({InputAction::Confirm, InputPhase::Pressed, InputDeviceKind::Xbox});
            QVERIFY(state.optionsOpen());
            QVERIFY(state.property("muteInBackground").toBool());
            QCOMPARE(settings.value("muteInBackground"), QVariant(true));
            state.handleInput({InputAction::MoveDown, InputPhase::Pressed, InputDeviceKind::Xbox});
            QCOMPARE(state.selectedOption(), 5);
            state.handleInput({InputAction::Confirm, InputPhase::Pressed, InputDeviceKind::Xbox});
            QVERIFY(!state.optionsOpen());
            settings.sync();
        }
        QSettings settings(settingsFile, QSettings::IniFormat);
        SessionController session(std::make_unique<ProbeCore>());
        InputRouter router;
        float audibleVolume = -1.0f;
        UiState state(session, router, validator, paths, settings, {},
                      [&](float volume) { audibleVolume = volume; });
        QVERIFY(state.property("muteInBackground").toBool());
        state.setApplicationActive(false);
        QCOMPARE(audibleVolume, 0.0f);
        state.setOptionValue(4, false);
        QCOMPARE(audibleVolume, 1.0f);
        QCOMPARE(settings.value("muteInBackground"), QVariant(false));
        state.setOption("muteInBackground", "invalid");
        QVERIFY(!state.property("muteInBackground").toBool());
    }

    void unavailableTilesKeepMenuAndReportStatus() {
        QTemporaryDir temp;
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        auto core = std::make_unique<ProbeCore>();
        SessionController session(std::move(core));
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        QVERIFY(session.load(fakeConfig()).ok());
        QVERIFY(session.openMenu().ok());
        const QStringList names{QStringLiteral("Pokédex"), QStringLiteral("Équipe"),
                                QStringLiteral("Sac"), QStringLiteral("Dresseur"),
                                QStringLiteral("Pokégear"), QStringLiteral("Carte"),
                                QStringLiteral("Options"), QStringLiteral("Reprendre")};
        QCOMPARE(state.tileNames(), names);
        for (int tile = 0; tile < 6; ++tile) {
            state.setSelectedTile(tile);
            state.activateSelected();
            QCOMPARE(state.statusMessage(), QStringLiteral("Disponible dans une prochaine étape"));
            QCOMPARE(state.sessionState(), SessionState::MenuOpen);
        }
    }

    void optionsAndResumeAreFunctional() {
        QTemporaryDir temp;
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        auto core = std::make_unique<ProbeCore>();
        auto* observed = core.get();
        SessionController session(std::move(core));
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        QVERIFY(session.load(fakeConfig()).ok());
        QVERIFY(session.openMenu().ok());
        state.setSelectedTile(6);
        state.activateSelected();
        QVERIFY(state.optionsOpen());
        state.closeOptions();
        QVERIFY(!state.optionsOpen());
        state.setSelectedTile(7);
        state.activateSelected();
        QCOMPARE(observed->resumes, 1);
        QCOMPARE(state.sessionState(), SessionState::Playing);
    }

    void menuTransitionClearsHeldInputBeforeResume() {
        QTemporaryDir temp;
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        auto core = std::make_unique<ProbeCore>();
        auto* observed = core.get();
        SessionController session(std::move(core));
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        QVERIFY(session.load(fakeConfig()).ok());
        state.handleInput({InputAction::MoveRight, InputPhase::Pressed, InputDeviceKind::Keyboard});
        QVERIFY(observed->lastButtons != 0);
        QVERIFY(session.openMenu().ok());
        state.resumeGame();
        state.handleInput({InputAction::Confirm, InputPhase::Pressed, InputDeviceKind::Keyboard});
        QCOMPARE(observed->lastButtons, toMask(DsButton::A));
    }

    void settingsPersistOnlyWhitelistedValuesAndConditionalRomPath() {
        QTemporaryDir temp;
        const auto file = temp.filePath("settings.ini");
        QSettings settings(file, QSettings::IniFormat);
        auto core = std::make_unique<ProbeCore>();
        SessionController session(std::move(core));
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        state.setOption(QStringLiteral("volume"), 35);
        state.setOption(QStringLiteral("smoothScaling"), true);
        state.setOption(QStringLiteral("fullscreen"), true);
        state.setOption(QStringLiteral("reopenLastGame"), false);
        state.setOption(QStringLiteral("lastRomPath"), QStringLiteral("C:/private/game.nds"));
        state.setOption(QStringLiteral("volume"), 500);
        state.setOption(QStringLiteral("volume"), true);
        state.setOption(QStringLiteral("volume"), QStringLiteral("72"));
        settings.sync();
        QCOMPARE(state.volume(), 35);
        QVERIFY(state.smoothScaling());
        QVERIFY(state.fullscreen());
        QCOMPARE(settings.value("volume").toInt(), 35);
        QVERIFY(!settings.contains("lastRomPath"));
        QVERIFY(!settings.contains("unknown"));
    }

    void footerTracksActiveDevice() {
        QTemporaryDir temp;
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        auto core = std::make_unique<ProbeCore>();
        SessionController session(std::move(core));
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        QVERIFY(state.footerConfirm().contains(QStringLiteral("Entrée")));
        state.setActiveDevice(InputDeviceKind::Xbox);
        QVERIFY(state.footerConfirm().contains(QStringLiteral("A")));
        state.setActiveDevice(InputDeviceKind::PlayStation);
        QVERIFY(state.footerConfirm().contains(QStringLiteral("Croix")));
    }

    void localFileUrlValidatesBeforeLoadingAndRemoteUrlIsRejected() {
        QTemporaryDir temp;
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        auto core = std::make_unique<ProbeCore>();
        auto* observed = core.get();
        SessionController session(std::move(core));
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        const auto rom = validRom(temp);
        QVERIFY(!rom.isEmpty());

        const auto invalidPath = temp.filePath(QStringLiteral("another-game.nds"));
        QByteArray invalidBytes(0x200, '\0');
        invalidBytes.replace(0x0c, 4, "ABCD");
        QFile invalidFile(invalidPath);
        QVERIFY(invalidFile.open(QIODevice::WriteOnly));
        QCOMPARE(invalidFile.write(invalidBytes), qint64(invalidBytes.size()));
        invalidFile.close();
        state.loadRomUrl(QUrl::fromLocalFile(invalidPath));
        QCOMPARE(observed->loads, 0);
        QCOMPARE(state.sessionState(), SessionState::NoRom);
        QVERIFY(!state.statusMessage().isEmpty());

        state.loadRomUrl(QUrl(QStringLiteral("https://example.invalid/game.nds")));
        QCOMPARE(observed->loads, 0);
        QCOMPARE(state.sessionState(), SessionState::NoRom);
        QVERIFY(!state.statusMessage().isEmpty());

        state.loadRomUrl(QUrl::fromLocalFile(rom));
        QCOMPARE(observed->loads, 1);
        QCOMPARE(state.sessionState(), SessionState::Playing);
        QCOMPARE(observed->loaded.romPath, std::filesystem::path(rom.toStdWString()));
    }

    void controllerConfirmOnWelcomeRequestsPickerOnce() {
        QTemporaryDir temp;
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        SessionController session(std::make_unique<ProbeCore>());
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        QSignalSpy picker(&state, &UiState::romSelectionRequested);

        state.handleInput({InputAction::Confirm, InputPhase::Pressed, InputDeviceKind::Xbox});
        state.handleInput({InputAction::Confirm, InputPhase::Pressed, InputDeviceKind::Xbox, true});

        QCOMPARE(picker.size(), 1);
        QCOMPARE(state.activeDevice(), InputDeviceKind::Xbox);
        QCOMPARE(state.sessionState(), SessionState::NoRom);
    }

    void keyboardResetsFooterAndUsesRouterRepeatPolicy() {
        QTemporaryDir temp;
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        SessionController session(std::make_unique<ProbeCore>());
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        QVERIFY(session.load(fakeConfig()).ok());
        QVERIFY(session.openMenu().ok());
        state.setActiveDevice(InputDeviceKind::Xbox);
        state.handleKey(Qt::Key_Shift, true);
        QCOMPARE(state.selectedTile(), 0);
        QCOMPARE(state.activeDevice(), InputDeviceKind::Keyboard);
        state.setActiveDevice(InputDeviceKind::Xbox);
        state.handleKey(Qt::Key_Right, true);
        QCOMPARE(state.selectedTile(), 1);
        QCOMPARE(state.activeDevice(), InputDeviceKind::Keyboard);
        QVERIFY(state.footerConfirm().contains(QStringLiteral("Entrée")));
        state.setSelectedTile(6);
        state.handleKey(Qt::Key_Return, true, true);
        QVERIFY(!state.optionsOpen());
        state.handleKey(Qt::Key_Return, true);
        QVERIFY(state.optionsOpen());
    }

    void f11RepeatDoesNotFlipFullscreenTwice() {
        QTemporaryDir temp;
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        SessionController session(std::make_unique<ProbeCore>());
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        QVERIFY(!state.fullscreen());
        state.handleKey(Qt::Key_F11, true);
        QVERIFY(state.fullscreen());
        state.handleKey(Qt::Key_F11, true, true);
        QVERIFY(state.fullscreen());
        state.handleKey(Qt::Key_F11, false);
        QVERIFY(state.fullscreen());
    }

    void controllerNavigatesOptionsAdjustsVolumeAndReturns() {
        QTemporaryDir temp;
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        SessionController session(std::make_unique<ProbeCore>());
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        QVERIFY(session.load(fakeConfig()).ok());
        QVERIFY(session.openMenu().ok());
        state.setOption(QStringLiteral("volume"), 98);
        state.setSelectedTile(6);
        state.handleInput({InputAction::Confirm, InputPhase::Pressed, InputDeviceKind::Xbox});
        QVERIFY(state.optionsOpen());
        QCOMPARE(state.selectedOption(), 0);
        state.handleInput({InputAction::MoveRight, InputPhase::Pressed, InputDeviceKind::Xbox});
        QCOMPARE(state.volume(), 100);
        state.handleInput({InputAction::MoveLeft, InputPhase::Pressed, InputDeviceKind::Xbox});
        QCOMPARE(state.volume(), 95);
        state.handleInput({InputAction::MoveDown, InputPhase::Pressed, InputDeviceKind::Xbox});
        QCOMPARE(state.selectedOption(), 1);
        state.handleInput({InputAction::Confirm, InputPhase::Pressed, InputDeviceKind::Xbox});
        QVERIFY(state.smoothScaling());
        state.handleInput({InputAction::Confirm, InputPhase::Pressed, InputDeviceKind::Xbox, true});
        QVERIFY(state.smoothScaling());
        state.handleInput({InputAction::Back, InputPhase::Pressed, InputDeviceKind::Xbox, true});
        QVERIFY(state.optionsOpen());
        state.handleInput({InputAction::Back, InputPhase::Pressed, InputDeviceKind::Xbox});
        QVERIFY(!state.optionsOpen());
        QCOMPARE(state.sessionState(), SessionState::MenuOpen);
        state.handleInput({InputAction::Confirm, InputPhase::Pressed, InputDeviceKind::Xbox});
        QVERIFY(state.optionsOpen());
        state.handleInput({InputAction::MoveUp, InputPhase::Pressed, InputDeviceKind::Xbox});
        QCOMPARE(state.selectedOption(), 5);
        state.handleInput({InputAction::Confirm, InputPhase::Pressed, InputDeviceKind::Xbox});
        QVERIFY(!state.optionsOpen());
    }

    void enablingReopenStoresCurrentValidatedPathAndRestoreRevalidates() {
        QTemporaryDir temp;
        const auto rom = validRom(temp);
        QVERIFY(!rom.isEmpty());
        const auto settingsFile = temp.filePath("settings.ini");
        QSettings settings(settingsFile, QSettings::IniFormat);
        SessionController session(std::make_unique<ProbeCore>());
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        state.loadRomUrl(QUrl::fromLocalFile(rom));
        QCOMPARE(state.sessionState(), SessionState::Playing);
        QVERIFY(!settings.contains("lastRomPath"));
        state.setOption(QStringLiteral("reopenLastGame"), true);
        QCOMPARE(settings.value("lastRomPath").toString(), rom);
        QVERIFY(session.stop().ok());

        SessionController restoredSession(std::make_unique<ProbeCore>());
        InputRouter restoredRouter;
        UiState restored(restoredSession, restoredRouter, validator, paths, settings);
        restored.restoreLastGame();
        QCOMPARE(restored.sessionState(), SessionState::Playing);
        restored.setOption(QStringLiteral("reopenLastGame"), false);
        QVERIFY(!settings.contains("lastRomPath"));
    }

    void missingStoredRomIsVisibleAndNeverLoaded() {
        QTemporaryDir temp;
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("reopenLastGame", true);
        settings.setValue("lastRomPath", temp.filePath("missing.nds"));
        auto core = std::make_unique<ProbeCore>();
        auto* observed = core.get();
        SessionController session(std::move(core));
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        state.restoreLastGame();
        QCOMPARE(observed->loads, 0);
        QCOMPARE(state.sessionState(), SessionState::NoRom);
        QVERIFY(!state.statusMessage().isEmpty());
    }

    void activeErrorChooserStopsBeforeRequestAndValidRetryLoads() {
        QTemporaryDir temp;
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        auto core = std::make_unique<ProbeCore>();
        auto* observed = core.get();
        SessionController session(std::move(core));
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        QSignalSpy picker(&state, &UiState::romSelectionRequested);
        QVERIFY(session.load(fakeConfig()).ok());
        observed->pauseResult = {EmulationErrorCode::CoreFailure, "pause failed"};
        QVERIFY(!session.openMenu().ok());
        QCOMPARE(state.sessionState(), SessionState::Error);

        state.handleInput({InputAction::Confirm, InputPhase::Pressed, InputDeviceKind::Xbox});
        QCOMPARE(observed->stops, 1);
        QCOMPARE(state.sessionState(), SessionState::NoRom);
        QCOMPARE(picker.size(), 1);

        const auto rom = validRom(temp);
        QVERIFY(!rom.isEmpty());
        state.loadRomUrl(QUrl::fromLocalFile(rom));
        QCOMPARE(observed->loads, 2);
        QCOMPARE(state.sessionState(), SessionState::Playing);
    }

    void failedErrorCleanupBlocksChooserAndDirectRomLoad() {
        QTemporaryDir temp;
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        auto core = std::make_unique<ProbeCore>();
        auto* observed = core.get();
        SessionController session(std::move(core));
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        QSignalSpy picker(&state, &UiState::romSelectionRequested);
        QVERIFY(session.load(fakeConfig()).ok());
        observed->pauseResult = {EmulationErrorCode::CoreFailure, "pause failed"};
        observed->stopResult = {EmulationErrorCode::CoreFailure, "cleanup failed"};
        QVERIFY(!session.openMenu().ok());

        state.chooseRom();
        QCOMPARE(state.sessionState(), SessionState::Error);
        QCOMPARE(picker.size(), 0);
        QCOMPARE(observed->stops, 1);
        QCOMPARE(state.statusMessage(), QStringLiteral("L’émulation s’est arrêtée. Un journal local a été conservé."));

        const auto rom = validRom(temp);
        QVERIFY(!rom.isEmpty());
        state.loadRomUrl(QUrl::fromLocalFile(rom));
        QCOMPARE(state.sessionState(), SessionState::Error);
        QCOMPARE(observed->loads, 1);
        QCOMPARE(observed->stops, 2);
        QCOMPARE(picker.size(), 0);
    }

    void inactiveLoadErrorChooserNormalizesWithoutStoppingCoreThenRetries() {
        QTemporaryDir temp;
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        auto core = std::make_unique<ProbeCore>();
        auto* observed = core.get();
        SessionController session(std::move(core));
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        QSignalSpy picker(&state, &UiState::romSelectionRequested);
        observed->loadResult = {EmulationErrorCode::InvalidRom, "inactive load failed"};
        QVERIFY(!session.load(fakeConfig()).ok());
        QCOMPARE(state.sessionState(), SessionState::Error);
        observed->loadResult = {};

        state.chooseRom();
        QCOMPARE(state.sessionState(), SessionState::NoRom);
        QCOMPARE(observed->stops, 0);
        QCOMPARE(picker.size(), 1);

        const auto rom = validRom(temp);
        QVERIFY(!rom.isEmpty());
        state.loadRomPath(rom);
        QCOMPARE(state.sessionState(), SessionState::Playing);
        QCOMPARE(observed->loads, 1);
    }

    void directRomUrlInActiveErrorCleansUpBeforeLoading() {
        QTemporaryDir temp;
        QSettings settings(temp.filePath("settings.ini"), QSettings::IniFormat);
        auto core = std::make_unique<ProbeCore>();
        auto* observed = core.get();
        SessionController session(std::move(core));
        InputRouter router;
        RomValidator validator;
        AppPaths paths(temp.path().toStdWString());
        UiState state(session, router, validator, paths, settings);
        QVERIFY(session.load(fakeConfig()).ok());
        observed->pauseResult = {EmulationErrorCode::CoreFailure, "pause failed"};
        QVERIFY(!session.openMenu().ok());

        const auto rom = validRom(temp);
        QVERIFY(!rom.isEmpty());
        state.loadRomUrl(QUrl::fromLocalFile(rom));
        QCOMPARE(observed->stops, 1);
        QCOMPARE(observed->loads, 2);
        QCOMPARE(state.sessionState(), SessionState::Playing);
    }
};

QTEST_GUILESS_MAIN(UiStateTest)
#include "tst_ui_state.moc"
