#include "app/AppBootstrap.h"
#include "emulation/fake/FakeEmulatorCore.h"

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

class AppBootstrapTest final : public QObject {
    Q_OBJECT
private slots:
    void fakeModeStartsWithoutRomPickerAndOpensMenu() {
        AppBootstrap bootstrap(true);
        bootstrap.start();
        QTRY_COMPARE(bootstrap.uiState()->sessionState(), SessionState::MenuOpen);
        bootstrap.uiState()->resumeGame();
        QCOMPARE(bootstrap.uiState()->sessionState(), SessionState::Playing);
        bootstrap.uiState()->handleKey(Qt::Key_Tab, true);
        QCOMPARE(bootstrap.uiState()->sessionState(), SessionState::MenuOpen);
    }

    void normalStartRestoresStoredValidatedRomButFakeModeDoesNot() {
        QTemporaryDir temp;
        const auto rom = temp.filePath("valid.nds");
        QByteArray bytes(0x200, '\0');
        bytes.replace(0x0c, 4, "IPKF");
        QFile file(rom);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        const auto settingsFile = temp.filePath("settings.ini");
        {
            QSettings settings(settingsFile, QSettings::IniFormat);
            settings.setValue("reopenLastGame", true);
            settings.setValue("lastRomPath", rom);
        }
        {
            AppBootstrap::Dependencies dependencies;
            dependencies.createCore = [](const EmulationConfig&, std::shared_ptr<SdlAudioSink>, const AppPaths&) {
                return std::make_unique<FakeEmulatorCore>();
            };
            AppBootstrap normal(false, settingsFile, temp.path(), std::move(dependencies));
            normal.start();
            QCOMPARE(normal.uiState()->sessionState(), SessionState::Playing);
            QDir saveRoot(temp.filePath("HeartGoldPCRemastered/saves"));
            QCOMPARE(saveRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 1);
        }
        {
            AppBootstrap fake(true, settingsFile, temp.path());
            fake.start();
            QTRY_COMPARE(fake.uiState()->sessionState(), SessionState::MenuOpen);
        }
        {
            // The real default must reject this synthetic header before any
            // instructions execute. This also catches a save binding mismatch:
            // that would incorrectly display the blocking save error instead.
            AppBootstrap production(false, settingsFile, temp.path());
            production.start();
            QTRY_COMPARE(production.uiState()->sessionState(), SessionState::NoRom);
            QCOMPARE(production.uiState()->statusMessage(), QStringLiteral("Ce fichier n’est pas une ROM HeartGold valide."));
            QVERIFY(!production.uiState()->blockingError());
        }
    }
};

QTEST_MAIN(AppBootstrapTest)
#include "tst_app_bootstrap.moc"
