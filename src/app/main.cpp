#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char* argv[])
{
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("HeartGoldPCRemastered"));
    QCoreApplication::setApplicationName(QStringLiteral(HEARTGOLD_PC_APP_NAME));

    QQmlApplicationEngine engine;
    engine.loadData(R"(
        import QtQuick
        import QtQuick.Controls

        ApplicationWindow {
            width: 1280
            height: 720
            visible: true
            title: "HeartGold PC Remastered"
        }
    )");

    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    return application.exec();
}
