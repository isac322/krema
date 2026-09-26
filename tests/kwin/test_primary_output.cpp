// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Plasma primary-output placement of dock and preview surfaces (issue #18).
//
// Drives the real MultiDockManager against a KWin virtual compositor with two
// outputs. The primary output is moved with kscreen-doctor (the same
// kde_output_device_v2 priority knob the System Settings display page uses);
// KWin republishes the user-visible primary order via kde_output_order_v1.
//
// Must run under run-with-kwin.sh, which provides WAYLAND_DISPLAY, a private
// session bus and throwaway XDG directories.
//
// This file intentionally only uses APIs that already exist on the unfixed
// branch (MultiDockManager, QWindow::screen, LayerShellQt::Window::scope), so
// the same test source compiles on both sides of the fix for the
// old-fails/new-passes proof.

#include "krema.h"
#include "models/dockmodel.h"
#include "models/notificationtracker.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/dockvisibilitycontroller.h"
#include "shell/multidockmanager.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <KAboutData>
#include <LayerShellQt/Shell>
#include <LayerShellQt/Window>

#include <QApplication>
#include <QProcess>
#include <QQuickStyle>
#include <QScreen>
#include <QSet>
#include <QStandardPaths>
#include <QTest>
#include <QtQml>

#include <memory>

// Static library resources must be initialized from the global namespace.
static void initResources()
{
    Q_INIT_RESOURCE(qml);
}

namespace
{

using krema::DockShell;
using krema::MultiDockManager;

constexpr int kTimeoutMs = 15000;

// Mirrors the object graph and wiring Application::run() builds.
struct App {
    std::unique_ptr<KremaSettings> settings;
    std::unique_ptr<krema::DockModel> model;
    std::unique_ptr<krema::NotificationTracker> tracker;
};

App &app()
{
    static App *instance = [] {
        auto *a = new App;
        a->settings = std::make_unique<KremaSettings>();
        a->settings->load();
        a->settings->setMonitorMode(MultiDockManager::PrimaryOnly);
        a->model = std::make_unique<krema::DockModel>();
        a->tracker = std::make_unique<krema::NotificationTracker>();

        auto *model = a->model.get();
        qmlRegisterSingletonType<krema::DockModel>("com.bhyoo.krema", 1, 0, "DockModel", [model](QQmlEngine *, QJSEngine *) -> QObject * {
            QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
            return model;
        });
        auto *settings = a->settings.get();
        qmlRegisterSingletonType<KremaSettings>("com.bhyoo.krema", 1, 0, "DockSettings", [settings](QQmlEngine *, QJSEngine *) -> QObject * {
            QQmlEngine::setObjectOwnership(settings, QQmlEngine::CppOwnership);
            return settings;
        });
        auto *tracker = a->tracker.get();
        qmlRegisterSingletonType<krema::NotificationTracker>("com.bhyoo.krema", 1, 0, "NotificationTracker", [tracker](QQmlEngine *, QJSEngine *) -> QObject * {
            QQmlEngine::setObjectOwnership(tracker, QQmlEngine::CppOwnership);
            return tracker;
        });
        return a;
    }();
    return *instance;
}

QStringList screenNames()
{
    QStringList names;
    for (auto *screen : QGuiApplication::screens()) {
        names.append(screen->name());
    }
    return names;
}

// Sets KWin output priority via kscreen-doctor, the same mechanism the
// System Settings display page uses. Returns false when the tool is absent.
bool makePrimary(const QString &outputName)
{
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("kscreen-doctor"));
    if (tool.isEmpty()) {
        return false;
    }
    QProcess proc;
    // kscreen-doctor is a normal Qt client: it must not inherit the test's
    // layer-shell integration override (it is not a dock).
    auto env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("QT_WAYLAND_SHELL_INTEGRATION"));
    proc.setProcessEnvironment(env);
    proc.start(tool, {QStringLiteral("output.%1.priority.1").arg(outputName)});
    proc.waitForFinished(kTimeoutMs);
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        qWarning("kscreen-doctor %s failed (exit %d, started %d): %s", qUtf8Printable(outputName), proc.exitCode(),
                 proc.exitStatus() == QProcess::NormalExit, qUtf8Printable(QString::fromUtf8(proc.readAllStandardError())));
        return false;
    }
    return true;
}

// Screen names of all mapped krema layer surfaces carrying the given scope.
// Layer surfaces permanently bind their wl_output at creation
// (qwaylandlayersurface.cpp), so window->screen() identifies the output the
// surface actually landed on.
QSet<QString> layerScreenNames(const char *scope)
{
    QSet<QString> result;
    for (auto *window : QGuiApplication::topLevelWindows()) {
        auto *layerWindow = LayerShellQt::Window::get(window);
        if (layerWindow && layerWindow->scope() == QLatin1String(scope) && window->screen()) {
            result.insert(window->screen()->name());
        }
    }
    return result;
}

std::unique_ptr<MultiDockManager> makeManager()
{
    auto &a = app();
    auto manager = std::make_unique<MultiDockManager>(a.settings.get(), a.model.get(), a.tracker.get());
    return manager;
}

QString dockScreenName(MultiDockManager *manager)
{
    auto *shell = manager->primaryShell();
    return (shell && shell->view()->screen()) ? shell->view()->screen()->name() : QString();
}

} // namespace

TEST_CASE("Dock surfaces land on and follow the Plasma primary output", "[primary-output]")
{
    REQUIRE(QGuiApplication::screens().size() == 2);
    const QStringList names = screenNames();
    REQUIRE(names.size() == 2);

    // Catch2 runs each SECTION as a fresh invocation, but the KWin session is
    // shared: a priority change made by one section persists into the next
    // invocation. Reset the Plasma primary to the Qt default so each section
    // starts from the known baseline.
    const QString defaultPrimary = QGuiApplication::primaryScreen()->name();
    INFO("default Qt primary: " << defaultPrimary.toStdString());
    REQUIRE(QTest::qWaitFor([&] { return makePrimary(defaultPrimary); }, kTimeoutMs));

    auto manager = makeManager();
    manager->initialize();

    // Control leg: by default the first wl_output (Virtual-0) is also the
    // Plasma primary, so the old code and the fix agree here.
    REQUIRE(QTest::qWaitFor(
        [&] {
            return dockScreenName(manager.get()) == defaultPrimary;
        },
        kTimeoutMs));

    SECTION("runtime reorder migrates the dock to the new primary")
    {
        const QString newPrimary = (names.first() == defaultPrimary) ? names.last() : names.first();
        // kscreen-doctor can race KWin's output enumeration at session start;
        // retry until the priority call is accepted.
        REQUIRE(QTest::qWaitFor([&] { return makePrimary(newPrimary); }, kTimeoutMs));

        REQUIRE(QTest::qWaitFor(
            [&] {
                return dockScreenName(manager.get()) == newPrimary;
            },
            kTimeoutMs));

        // Exactly one krema-dock surface exists in PrimaryOnly mode.
        CHECK(layerScreenNames("krema-dock") == QSet<QString>{newPrimary});

        // The preview surface lives on the same output as its dock.
        CHECK(QTest::qWaitFor(
            [&] {
                return layerScreenNames("krema-preview") == QSet<QString>{newPrimary};
            },
            kTimeoutMs));

        // Diagnostic: Qt's primaryScreen() itself never followed the swap on
        // QtWayland, proving the migration came from the protocol client.
        INFO("Qt primaryScreen() after swap: " << QGuiApplication::primaryScreen()->name().toStdString());
    }

    SECTION("priority set before initialize puts the first dock on the primary")
    {
        const QString newPrimary = (names.first() == defaultPrimary) ? names.last() : names.first();
        REQUIRE(QTest::qWaitFor([&] { return makePrimary(newPrimary); }, kTimeoutMs));

        auto late = makeManager();
        late->initialize();

        CHECK(QTest::qWaitFor(
            [&] {
                return dockScreenName(late.get()) == newPrimary;
            },
            kTimeoutMs));
    }
}

TEST_CASE("Each preview surface binds the same output as its dock (AllScreens)", "[primary-output]")
{
    REQUIRE(QGuiApplication::screens().size() == 2);

    app().settings->setMonitorMode(MultiDockManager::AllScreens);
    auto manager = makeManager();
    manager->initialize();

    REQUIRE(QTest::qWaitFor(
        [&] {
            return manager->shells().size() == 2;
        },
        kTimeoutMs));

    const QSet<QString> dockScreens = [&] {
        QSet<QString> set;
        for (auto *shell : manager->shells()) {
            if (shell->view()->screen()) {
                set.insert(shell->view()->screen()->name());
            }
        }
        return set;
    }();
    REQUIRE(dockScreens.size() == 2);

    REQUIRE(QTest::qWaitFor(
        [&] {
            return layerScreenNames("krema-preview") == dockScreens;
        },
        kTimeoutMs));
}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    if (QQuickStyle::name().isEmpty()) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }
    KAboutData aboutData(QStringLiteral("krema"), QStringLiteral("Krema"), QStringLiteral("test"));
    KAboutData::setApplicationData(aboutData);
    initResources();
    LayerShellQt::Shell::useLayerShell();

    return Catch::Session().run(argc, argv);
}
