#include "app/AppBootstrap.h"
#include "ui/GameSurface.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QString{});
    QCoreApplication::setApplicationName(QStringLiteral("HeartGoldPCRemastered"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("HeartGold PC Remastered — prototype local"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("fake-core"), QStringLiteral("Démonstration hors ligne sans ROM.")});
    parser.addOption({QStringLiteral("rom"), QStringLiteral("ROM locale ; chemin non mémorisé."), QStringLiteral("absolute-path")});
    parser.process(application);
    const auto romPath = parser.value(QStringLiteral("rom"));
    if ((!romPath.isEmpty() && !std::filesystem::path(romPath.toStdWString()).is_absolute()) ||
        (parser.isSet(QStringLiteral("rom")) && romPath.isEmpty()) ||
        (parser.isSet(QStringLiteral("fake-core")) && parser.isSet(QStringLiteral("rom")))) {
        qCritical("Utilisez --rom avec un chemin absolu, ou --fake-core seul.");
        return 2;
    }
    qmlRegisterType<GameSurface>("HeartGoldPC", 1, 0, "GameSurface");
    AppBootstrap bootstrap(parser.isSet(QStringLiteral("fake-core")));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("uiState"), bootstrap.uiState());
    engine.load(QUrl(QStringLiteral("qrc:/HeartGoldPC/ui/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    bootstrap.attachSurface(engine.rootObjects().front());
    bootstrap.start(romPath);
    const auto result = application.exec();
    bootstrap.shutdown(); // join/SDL before engine and its QML/scenegraph resources
    return result;
}
