#include "ui/UiState.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <vector>

namespace {
class ProbeCore final : public EmulatorCore {
public:
    EmulationResult load(const EmulationConfig& config) override {
        loaded = config;
        ++loads;
        return {};
    }
    EmulationResult start() override { running = true; return {}; }
    EmulationResult pause() override { ++pauses; return {}; }
    EmulationResult resume() override { ++resumes; return {}; }
    EmulationResult stop() override { running = false; return {}; }
    void setButtons(DsButtonMask buttons) override { lastButtons = buttons; }
    std::shared_ptr<const VideoFrame> latestFrame() const override { return {}; }
    bool isRunning() const noexcept override { return running; }
    EmulationConfig loaded;
    int loads{0};
    int pauses{0};
    int resumes{0};
    bool running{false};
    DsButtonMask lastButtons{0};
};

EmulationConfig fakeConfig() {
    return {std::filesystem::path{L"C:/fake/heartgold.nds"},
            std::filesystem::path{L"C:/fake/save.sav"}, std::string(64, 'a')};
}
}

class UiStateTest final : public QObject {
    Q_OBJECT
private slots:
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
};

QTEST_GUILESS_MAIN(UiStateTest)
#include "tst_ui_state.moc"
