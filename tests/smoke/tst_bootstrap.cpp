#include <QtTest>

class BootstrapTest final : public QObject {
    Q_OBJECT
private slots:
    void applicationIdentityIsStable() {
        QCOMPARE(QStringLiteral("HeartGold PC Remastered"),
                 QStringLiteral(HEARTGOLD_PC_APP_NAME));
    }
};

QTEST_GUILESS_MAIN(BootstrapTest)
#include "tst_bootstrap.moc"
