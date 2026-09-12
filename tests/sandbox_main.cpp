#include "../src/models/taskiconprovider.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // Force a standard theme so fallback/system icons work
    QIcon::setThemeName(QStringLiteral("breeze"));

    QQmlApplicationEngine engine;

    // Initialize provider with normalization ON (to test the math scaling)
    auto *iconProvider = new krema::TaskIconProvider(true);
    engine.addImageProvider(QStringLiteral("taskicon"), iconProvider);

    // Resolve QML path safely (works whether you run from project root or inside build/)
    QString qmlPath = QDir::currentPath() + QStringLiteral("/tests/IconSandbox.qml");
    if (!QFile::exists(qmlPath)) {
        qmlPath = QDir::currentPath() + QStringLiteral("/../../tests/IconSandbox.qml");
    }

    if (!QFile::exists(qmlPath)) {
        qWarning() << "Could not find IconSandbox.qml. Make sure you are running from the project root or build dir.";
        return -1;
    }

    engine.load(QUrl::fromLocalFile(qmlPath));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
