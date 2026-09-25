#include "app/AppBootstrap.h"
#include "ui/GameSurface.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <algorithm>

int main(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("HeartGoldPCRemastered"));
    QCoreApplication::setApplicationName(QStringLiteral(HEARTGOLD_PC_APP_NAME));

    const auto arguments = application.arguments();
    const bool fakeMode = std::any_of(arguments.cbegin(), arguments.cend(),
                                      [](const QString& value) { return value == QLatin1String("--fake-core"); });
    qmlRegisterType<GameSurface>("HeartGoldPC", 1, 0, "GameSurface");
    AppBootstrap bootstrap(fakeMode);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("uiState"), bootstrap.uiState());
    engine.load(QUrl(QStringLiteral("qrc:/HeartGoldPC/ui/qml/Main.qml")));

    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    bootstrap.attachSurface(engine.rootObjects().front());
    bootstrap.start();
    return application.exec();
}
