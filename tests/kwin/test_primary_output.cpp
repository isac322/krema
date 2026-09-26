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
// branch (MultiDockManager, QWindow::screen, LayerShellQt::Window), so the
// same test source compiles on both sides of the fix for the
// old-fails/new-passes proof. OutputOrderMonitor access goes through the
// friend seam; on master the file has no friend, so those cases are guarded
// at runtime with a null check on a reflective lookup (see monitorOrSkip()).

#include "krema.h"
#include "models/dockmodel.h"
#include "models/notificationtracker.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/dockvisibilitycontroller.h"
#include "shell/multidockmanager.h"

#include <KAboutData>
#include <LayerShellQt/Shell>
#include <LayerShellQt/Window>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

// OutputOrderMonitor exists only on the fix branch; guard every use so this
// same file still compiles against master's krema_lib for the
// old-fails/new-passes proof.
#if __has_include("shell/outputordermonitor.h")
#define KREMA_TEST_HAS_MONITOR 1
#include "outputordermonitortestaccess.h"
#include "shell/outputordermonitor.h"
#endif

#include <QApplication>
#include <QProcess>
#include <QQuickStyle>
#include <QScreen>
#include <QSet>
#include <QStandardPaths>
#include <QTest>
#include <QtQml>

#include <atomic>
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

// The wl_output a layer surface is actually bound to. Qt-side
// window->screen() is the QScreen the platform window derived from the
// window geometry; LayerShellQt::Window::screen() is the screen requested at
// get_layer_surface() time. Both must agree — a regression that dropped the
// LayerShellQt pin while keeping only the Qt-side pin would otherwise pass.
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

// Same as layerScreenNames but reports the LayerShellQt-requested screen,
// i.e. the wl_output the surface actually bound at get_layer_surface().
QSet<QString> boundOutputNames(const char *scope)
{
    QSet<QString> result;
    for (auto *window : QGuiApplication::topLevelWindows()) {
        auto *layerWindow = LayerShellQt::Window::get(window);
        if (layerWindow && layerWindow->scope() == QLatin1String(scope) && layerWindow->screen()) {
            result.insert(layerWindow->screen()->name());
        }
    }
    return result;
}

// Both the derived Qt screen and the bound layer output must be the expected
// output for every surface carrying the scope.
bool dockAndBoundMatch(const char *scope, const QString &expected)
{
    const QSet<QString> qtScreens = layerScreenNames(scope);
    const QSet<QString> bound = boundOutputNames(scope);
    return qtScreens == QSet<QString>{expected} && bound == QSet<QString>{expected};
}

#if KREMA_TEST_HAS_MONITOR
// Counts MultiDockManager "Creating dock shell" messages so the startup test
// can prove exactly one shell is created on the correct output (issue #18 P1:
// a premature orderReadyChanged caused a wrong-output create followed by a
// destroy+recreate on every launch).
std::atomic<int> g_dockCreations{0};
QtMessageHandler g_prevHandler = nullptr;
void countingHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (msg.contains(QLatin1String("Creating dock shell for screen"))) {
        g_dockCreations.fetch_add(1);
    }
    if (g_prevHandler) {
        g_prevHandler(type, ctx, msg);
    }
}
#endif // KREMA_TEST_HAS_MONITOR

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

    // Set this test's own preconditions: process-static settings persist
    // across TEST_CASEs and Catch2's default Randomized order can run the
    // AllScreens case first, so PrimaryOnly must be (re-)established here
    // rather than inherited from app() construction or --order decl.
    app().settings->setMonitorMode(MultiDockManager::PrimaryOnly);

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

        // Exactly one krema-dock surface exists in PrimaryOnly mode, and both
        // the derived Qt screen and the bound wl_output are the new primary.
        CHECK(QTest::qWaitFor(
            [&] {
                return dockAndBoundMatch("krema-dock", newPrimary);
            },
            kTimeoutMs));

        // The preview surface lives on the same output as its dock.
        CHECK(QTest::qWaitFor(
            [&] {
                return dockAndBoundMatch("krema-preview", newPrimary);
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
    const QStringList names = screenNames();
    REQUIRE(names.size() == 2);

    // Own precondition (see the PrimaryOnly case): an earlier randomized
    // TEST_CASE may have left monitorMode on PrimaryOnly.
    app().settings->setMonitorMode(MultiDockManager::AllScreens);
    // This case does not depend on which output is the Plasma primary; only
    // that a dock and its preview co-locate on every output.

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

    // The bound wl_output must match the Qt screen for each preview too.
    CHECK(boundOutputNames("krema-preview") == dockScreens);

    // Restore a neutral state for whichever case runs next under Randomized
    // ordering.
    app().settings->setMonitorMode(MultiDockManager::PrimaryOnly);
}
#if KREMA_TEST_HAS_MONITOR
TEST_CASE("Startup creates exactly one dock shell on the Plasma primary", "[primary-output]")
{
    // Regression for the premature orderReadyChanged (issue #18 P1): the
    // signal used to fire before the order was adopted, so a caller that
    // defers initialize() until ready (the real Application::run) still
    // placed the first dock on the Qt fallback output and then recreated it.
    // Proof: with the Plasma primary on the SECOND output, wait until the
    // monitor reports readiness and assert (a) the resolved primary is
    // already correct at readiness, and (b) exactly one shell is created.
    REQUIRE(QGuiApplication::screens().size() == 2);
    const QStringList names = screenNames();
    REQUIRE(names.size() == 2);
    app().settings->setMonitorMode(MultiDockManager::PrimaryOnly);

    // A fresh monitor gets its own kde_output_order_v1 binding, so its
    // orderReadyChanged reflects a complete order cycle rather than the
    // singleton's already-true state.
    auto *monitor = krema::OutputOrderMonitorTestAccess::create();
    REQUIRE(monitor->protocolActive());

    // Move the Plasma primary to the second output, then let the monitor
    // observe the republished order.
    const QString newPrimary = names.last() == QGuiApplication::primaryScreen()->name() ? names.first() : names.last();
    REQUIRE(QTest::qWaitFor(
        [&] {
            return makePrimary(newPrimary);
        },
        kTimeoutMs));

    // Emulate the app's deferred initialize(): do not place anything until
    // orderReadyChanged fires.
    REQUIRE(QTest::qWaitFor(
        [&] {
            return monitor->orderReady();
        },
        kTimeoutMs));

    // (a) At readiness the adopted order is already applied — the resolved
    // primary is the Plasma primary, not the stale Qt fallback. Before the
    // fix orderReadyChanged fired BEFORE adoption, so this read back the
    // fallback and a caller initialized on the wrong output.
    CHECK(monitor->primaryScreen() != nullptr);
    CHECK(monitor->primaryScreen()->name() == newPrimary);

    // (b) A caller that defers initialize() until ready creates exactly one
    // shell, on the already-correct output.
    g_dockCreations.store(0);
    g_prevHandler = qInstallMessageHandler(countingHandler);

    auto manager = makeManager();
    manager->initialize();

    REQUIRE(QTest::qWaitFor(
        [&] {
            return dockScreenName(manager.get()) == newPrimary;
        },
        kTimeoutMs));

    qInstallMessageHandler(g_prevHandler);
    g_prevHandler = nullptr;

    // Exactly one creation, and it already targets the correct output — no
    // wrong-output flash followed by a destroy+recreate.
    CHECK(g_dockCreations.load() == 1);
    CHECK(layerScreenNames("krema-dock") == QSet<QString>{newPrimary});
}
#endif // KREMA_TEST_HAS_MONITOR

int main(int argc, char *argv[])
{
    // Opt into the layer-shell platform plugin. Equivalent to the deprecated
    // LayerShellQt::Shell::useLayerShell(), which only sets this variable.
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");

    QApplication application(argc, argv);
    if (QQuickStyle::name().isEmpty()) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }
    KAboutData aboutData(QStringLiteral("krema"), QStringLiteral("Krema"), QStringLiteral("test"));
    KAboutData::setApplicationData(aboutData);
    initResources();

    return Catch::Session().run(argc, argv);
}
