#include "app/AppBootstrap.h"

#include <QtTest>

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
};

QTEST_MAIN(AppBootstrapTest)
#include "tst_app_bootstrap.moc"
