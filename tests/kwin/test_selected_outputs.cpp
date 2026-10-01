// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Selected-monitor placement regression (issue #18).
//
// This test deliberately writes MonitorMode=3 and SelectedOutputs through the
// generic KConfig API instead of referring to the new generated settings
// accessors or enum names.  It therefore compiles against the frozen baseline
// too, where the observable failure is an incorrect dock subset rather than a
// missing symbol.
//
// Must run under run-with-kwin.sh with KREMA_TEST_OUTPUT_COUNT=3.

#include "app/application.h"
#include "krema.h"
#include "models/dockmodel.h"
#include "models/launcherentrytracker.h"
#include "models/notificationtracker.h"
#include "outputordermonitortestaccess.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/multidockmanager.h"
#include "shell/outputordermonitor.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <KAboutData>
#include <KConfigGroup>
#include <KSharedConfig>
#include <LayerShellQt/Window>

#include <QApplication>
#include <QCursor>
#include <QPointer>
#include <QProcess>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QScreen>
#include <QSet>
#include <QSignalSpy>
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

constexpr int kTimeoutMs = 15000;

struct App {
    std::unique_ptr<KremaSettings> settings;
    std::unique_ptr<krema::DockModel> model;
    std::unique_ptr<krema::NotificationTracker> tracker;
    std::unique_ptr<krema::LauncherEntryTracker> launcherEntries;
};

App &app()
{
    static App *instance = [] {
        auto *a = new App;
        a->settings = std::make_unique<KremaSettings>();
        a->settings->load();
        a->model = std::make_unique<krema::DockModel>();
        a->model->setPinnedLaunchers(a->settings->pinnedLaunchers());
        a->tracker = std::make_unique<krema::NotificationTracker>();
        a->launcherEntries = std::make_unique<krema::LauncherEntryTracker>();

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
        auto *launcherEntries = a->launcherEntries.get();
        qmlRegisterSingletonType<krema::LauncherEntryTracker>("com.bhyoo.krema",
                                                              1,
                                                              0,
                                                              "LauncherEntryTracker",
                                                              [launcherEntries](QQmlEngine *, QJSEngine *) -> QObject * {
                                                                  QQmlEngine::setObjectOwnership(launcherEntries, QQmlEngine::CppOwnership);
                                                                  return launcherEntries;
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

void writeSelectedMonitorConfig(const QStringList &selectedOutputs)
{
    const auto config = KSharedConfig::openConfig(QStringLiteral("kremarc"));
    auto general = config->group(QStringLiteral("General"));
    general.writeEntry(QStringLiteral("MonitorMode"), 3);
    general.writeEntry(QStringLiteral("SelectedOutputs"), selectedOutputs);
    config->sync();
}

QSet<QString> dockedScreenNames(const krema::MultiDockManager &manager)
{
    QSet<QString> names;
    for (auto *shell : manager.shells()) {
        if (shell && shell->view() && shell->view()->screen()) {
            names.insert(shell->view()->screen()->name());
        }
    }
    return names;
}

QScreen *screenNamed(const QString &name)
{
    for (auto *screen : QGuiApplication::screens()) {
        if (screen->name() == name) {
            return screen;
        }
    }
    return nullptr;
}

krema::DockShell *shellNamed(const krema::MultiDockManager &manager, const QString &name)
{
    for (auto *shell : manager.shells()) {
        if (shell->view()->screen() && shell->view()->screen()->name() == name) {
            return shell;
        }
    }
    return nullptr;
}

std::unique_ptr<krema::MultiDockManager> makeSelectedManager(const QStringList &selected)
{
    auto *order = krema::OutputOrderMonitor::instance();
    REQUIRE(QTest::qWaitFor(
        [order] {
            return order->orderReady();
        },
        kTimeoutMs));
    writeSelectedMonitorConfig(selected);
    auto &a = app();
    a.settings->load();
    auto manager = std::make_unique<krema::MultiDockManager>(a.settings.get(), a.model.get(), a.tracker.get());
    krema::Application::connectSettingsAutoSave(a.settings.get(), manager.get());
    manager->initialize();
    return manager;
}

void requireDocksOn(const krema::MultiDockManager &manager, const QStringList &names)
{
    const QSet<QString> expected(names.cbegin(), names.cend());
    REQUIRE(QTest::qWaitFor(
        [&] {
            return manager.shells().size() == expected.size() && dockedScreenNames(manager) == expected;
        },
        kTimeoutMs));
    for (auto *shell : manager.shells()) {
        REQUIRE(QTest::qWaitFor(
            [&] {
                return shell->view()->isExposed();
            },
            kTimeoutMs));
        REQUIRE(shell->view()->screen());
        CHECK(shell->view()->isVisible());
        CHECK(shell->view()->screen()->geometry().contains(shell->view()->geometry().center()));
    }
}

QStringList savedSelection()
{
    return KSharedConfig::openConfig(QStringLiteral("kremarc"))->group(QStringLiteral("General")).readEntry(QStringLiteral("SelectedOutputs"), QStringList());
}

bool runKscreenDoctor(const QStringList &args)
{
    const auto tool = QStandardPaths::findExecutable(QStringLiteral("kscreen-doctor"));
    if (tool.isEmpty()) {
        return false;
    }
    auto env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("QT_WAYLAND_SHELL_INTEGRATION"));
    QProcess process;
    process.setProcessEnvironment(env);
    process.start(tool, args);
    if (!process.waitForFinished(kTimeoutMs)) {
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        qWarning("kscreen-doctor %s failed: %s",
                 qUtf8Printable(args.join(QLatin1Char(' '))),
                 qUtf8Printable(QString::fromUtf8(process.readAllStandardError())));
        return false;
    }
    return true;
}

bool makePrimary(const QString &name)
{
    // A lone priority.1 command can normalize the existing ranks without
    // changing the primary when another output already has rank 0. Demote
    // the current incumbent and promote an alternate in one compositor
    // transaction first, then promote the requested output.
    auto *order = krema::OutputOrderMonitor::instance();
    auto *currentPrimary = order->primaryScreen();
    if (!currentPrimary) {
        return false;
    }
    const QStringList names = screenNames();
    if (names.size() < 2) {
        return false;
    }
    const QString incumbent = currentPrimary->name();
    QString other;
    for (const auto &candidate : names) {
        if (candidate != name) {
            other = candidate;
            break;
        }
    }
    if (other.isEmpty()) {
        return false;
    }

    QStringList transition;
    if (incumbent == other) {
        transition = {QStringLiteral("output.%1.priority.1").arg(other)};
    } else {
        transition = {
            QStringLiteral("output.%1.priority.%2").arg(incumbent).arg(names.size()),
            QStringLiteral("output.%1.priority.1").arg(other),
        };
    }
    if (!runKscreenDoctor(transition)) {
        return false;
    }
    // Process exit does not mean this client has adopted the new primary.
    if (!QTest::qWaitFor(
            [&] {
                return order->primaryScreen() && order->primaryScreen()->name() == other;
            },
            kTimeoutMs)) {
        return false;
    }
    if (!runKscreenDoctor({QStringLiteral("output.%1.priority.1").arg(name)})) {
        return false;
    }
    return QTest::qWaitFor(
        [&] {
            return order->primaryScreen() && order->primaryScreen()->name() == name;
        },
        kTimeoutMs);
}

void requirePrimary(const QString &name)
{
    auto *order = krema::OutputOrderMonitor::instance();
    QSignalSpy changed(order, &krema::OutputOrderMonitor::primaryOutputChanged);
    REQUIRE(makePrimary(name));
    REQUIRE(QTest::qWaitFor(
        [&] {
            return !changed.isEmpty() && order->primaryScreen() && order->primaryScreen()->name() == name;
        },
        kTimeoutMs));
}

QSet<QString> surfaceOutputNames(const char *scope)
{
    QSet<QString> result;
    for (auto *window : QGuiApplication::topLevelWindows()) {
        auto *layer = LayerShellQt::Window::get(window);
        if (!layer || layer->scope() != QLatin1String(scope)) {
            continue;
        }
#ifdef KREMA_COMPAT_NO_LAYERSHELL_SCREEN
        if (layer->screenConfiguration() == LayerShellQt::Window::ScreenFromQWindow && window->screen()) {
            result.insert(window->screen()->name());
        }
#else
        if (layer->screen()) {
            result.insert(layer->screen()->name());
        }
#endif
    }
    return result;
}

void requireRetained(const QPointer<krema::DockShell> &shell, const QString &name)
{
    REQUIRE(shell);
    REQUIRE(shell->view()->screen());
    CHECK(shell->view()->screen()->name() == name);
}

} // namespace

TEST_CASE("Selected monitors create docks only on the saved outputs", "[selected-output]")
{
    REQUIRE(QGuiApplication::screens().size() == 3);

    auto *outputOrder = krema::OutputOrderMonitor::instance();
    REQUIRE(QTest::qWaitFor(
        [outputOrder] {
            return outputOrder->orderReady();
        },
        kTimeoutMs));
    auto *primary = outputOrder->primaryScreen();
    REQUIRE(primary);

    const QStringList allOutputs = screenNames();
    REQUIRE(allOutputs.size() == 3);

    // Deliberately exclude the Plasma primary: the selected mode must honor
    // the saved subset rather than implicitly adding the primary output.
    QStringList selectedOutputs;
    for (const auto &name : allOutputs) {
        if (name != primary->name()) {
            selectedOutputs.append(name);
        }
    }
    REQUIRE(selectedOutputs.size() == 2);

    // This is the compatibility seam: the test compiles on the unfixed
    // branch, while its shell-subset assertion fails there (mode 3 is not
    // implemented and falls through to the old mode behavior).
    writeSelectedMonitorConfig(selectedOutputs);

    auto &a = app();
    a.settings->load();
    auto manager = std::make_unique<krema::MultiDockManager>(a.settings.get(), a.model.get(), a.tracker.get());
    manager->initialize();

    REQUIRE(QTest::qWaitFor(
        [&] {
            return manager->shells().size() == selectedOutputs.size();
        },
        kTimeoutMs));

    const QSet<QString> expected(selectedOutputs.cbegin(), selectedOutputs.cend());
    REQUIRE(QTest::qWaitFor(
        [&] {
            return dockedScreenNames(*manager) == expected;
        },
        kTimeoutMs));

    for (auto *shell : manager->shells()) {
        REQUIRE(shell);
        auto *view = shell->view();
        REQUIRE(view);
        REQUIRE(QTest::qWaitFor(
            [&] {
                return view->isExposed();
            },
            kTimeoutMs));
        QQuickView *previewView = nullptr;
        REQUIRE(QTest::qWaitFor(
            [&] {
                for (auto *window : QGuiApplication::topLevelWindows()) {
                    auto *quickView = qobject_cast<QQuickView *>(window);
                    auto *layer = LayerShellQt::Window::get(window);
                    if (quickView && layer && layer->scope() == QLatin1String("krema-preview") && quickView->engine() == view->engine()) {
                        previewView = quickView;
                        return previewView->isExposed();
                    }
                }
                return false;
            },
            kTimeoutMs));
        REQUIRE(previewView);
        REQUIRE(previewView->screen());
        CHECK(previewView->screen() == view->screen());
        auto *layer = LayerShellQt::Window::get(previewView);
#ifdef KREMA_COMPAT_NO_LAYERSHELL_SCREEN
        CHECK(layer->screenConfiguration() == LayerShellQt::Window::ScreenFromQWindow);
#else
        CHECK(layer->screen() == view->screen());
#endif
        REQUIRE(view->screen());
        CHECK(expected.contains(view->screen()->name()));
        CHECK(view->isVisible());
        CHECK(view->screen()->geometry().contains(view->geometry().center()));
    }
}

TEST_CASE(
    "Unavailable selected monitors use one temporary primary dock "
    "without changing the saved selection",
    "[selected-output][fallback]")
{
    REQUIRE(QGuiApplication::screens().size() == 3);
    QStringList selected;
    SECTION("empty selection")
    {
        selected = {};
    }
    SECTION("only disconnected names")
    {
        selected = {QStringLiteral("missing-HDMI-A-9"), QStringLiteral("missing-DP-8")};
    }

    auto manager = makeSelectedManager(selected);
    auto *order = krema::OutputOrderMonitor::instance();
    const auto originalPrimary = order->primaryScreen()->name();
    const auto restorePrimary = qScopeGuard([&] {
        makePrimary(originalPrimary);
    });
    requireDocksOn(*manager, {originalPrimary});
    CHECK(savedSelection() == selected);

    const auto names = screenNames();
    const auto newPrimary = names.first() == originalPrimary ? names.last() : names.first();
    requirePrimary(newPrimary);
    requireDocksOn(*manager, {newPrimary});
    CHECK(savedSelection() == selected);
}

TEST_CASE(
    "Removing and returning the last selected output replaces only the "
    "temporary fallback",
    "[selected-output][hotplug]")
{
    REQUIRE(QGuiApplication::screens().size() == 3);
    auto *order = krema::OutputOrderMonitor::instance();
    REQUIRE(QTest::qWaitFor(
        [order] {
            return order->orderReady();
        },
        kTimeoutMs));
    const auto originalPrimary = order->primaryScreen()->name();
    const auto names = screenNames();
    const auto selected = names.first() == originalPrimary ? names.last() : names.first();
    auto manager = makeSelectedManager({selected});
    requireDocksOn(*manager, {selected});
    QPointer<krema::DockShell> removed = manager->shells().first();
    const auto restoreOutput = qScopeGuard([&] {
        runKscreenDoctor({QStringLiteral("output.%1.enable").arg(selected)});
        makePrimary(originalPrimary);
    });

    REQUIRE(runKscreenDoctor({QStringLiteral("output.%1.disable").arg(selected)}));
    REQUIRE(QTest::qWaitFor(
        [&] {
            return screenNamed(selected) == nullptr;
        },
        kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [&] {
            auto *primary = order->primaryScreen();
            return primary && manager->shells().size() == 1 && dockedScreenNames(*manager) == QSet<QString>{primary->name()};
        },
        kTimeoutMs));
    CHECK_FALSE(removed);
    CHECK(savedSelection() == QStringList{selected});
    QPointer<krema::DockShell> fallback = manager->shells().first();

    REQUIRE(runKscreenDoctor({QStringLiteral("output.%1.enable").arg(selected)}));
    REQUIRE(QTest::qWaitFor(
        [&] {
            return screenNamed(selected) != nullptr;
        },
        kTimeoutMs));
    requireDocksOn(*manager, {selected});
    CHECK_FALSE(fallback);
    CHECK(savedSelection() == QStringList{selected});
}

TEST_CASE(
    "Primary changes and unselected hotplug retain selected dock and "
    "preview surfaces",
    "[selected-output][identity]")
{
    REQUIRE(QGuiApplication::screens().size() == 3);
    auto *order = krema::OutputOrderMonitor::instance();
    REQUIRE(QTest::qWaitFor(
        [order] {
            return order->orderReady();
        },
        kTimeoutMs));
    const auto originalPrimary = order->primaryScreen()->name();
    QStringList selected = screenNames();
    selected.removeAll(originalPrimary);
    REQUIRE(selected.size() == 2);
    auto manager = makeSelectedManager(selected);
    requireDocksOn(*manager, selected);
    QPointer<krema::DockShell> first = shellNamed(*manager, selected.first());
    QPointer<krema::DockShell> second = shellNamed(*manager, selected.last());
    REQUIRE(first);
    REQUIRE(second);
    QSignalSpy firstDestroyed(first, &QObject::destroyed);
    QSignalSpy secondDestroyed(second, &QObject::destroyed);
    const auto restoreOutput = qScopeGuard([&] {
        runKscreenDoctor({QStringLiteral("output.%1.enable").arg(originalPrimary)});
        makePrimary(originalPrimary);
    });

    requirePrimary(selected.first());
    // A bounded destroyed-signal observation includes the topology debounce.
    // No fixed sleep or source-level implementation assertion is required.
    CHECK_FALSE(firstDestroyed.wait(1000));
    CHECK_FALSE(secondDestroyed.wait(1000));
    requireRetained(first, selected.first());
    requireRetained(second, selected.last());
    requireDocksOn(*manager, selected);

    REQUIRE(runKscreenDoctor({QStringLiteral("output.%1.disable").arg(originalPrimary)}));
    REQUIRE(QTest::qWaitFor(
        [&] {
            return screenNamed(originalPrimary) == nullptr;
        },
        kTimeoutMs));
    CHECK_FALSE(firstDestroyed.wait(1000));
    CHECK_FALSE(secondDestroyed.wait(1000));
    requireRetained(first, selected.first());
    requireRetained(second, selected.last());

    REQUIRE(runKscreenDoctor({QStringLiteral("output.%1.enable").arg(originalPrimary)}));
    REQUIRE(QTest::qWaitFor(
        [&] {
            return screenNamed(originalPrimary) != nullptr;
        },
        kTimeoutMs));
    CHECK_FALSE(firstDestroyed.wait(1000));
    CHECK_FALSE(secondDestroyed.wait(1000));
    requireDocksOn(*manager, selected);
    requireRetained(first, selected.first());
    requireRetained(second, selected.last());

    const QSet<QString> expected(selected.cbegin(), selected.cend());
    CHECK(surfaceOutputNames("dock") == expected);
    CHECK(surfaceOutputNames("krema-preview") == expected);
}

TEST_CASE(
    "Selected-mode shortcuts resolve through primary adopted order and "
    "saved selection order",
    "[selected-output][shortcuts]")
{
    REQUIRE(QGuiApplication::screens().size() == 3);
    auto *order = krema::OutputOrderMonitor::instance();
    REQUIRE(QTest::qWaitFor(
        [order] {
            return order->orderReady();
        },
        kTimeoutMs));
    auto *unselected = QGuiApplication::primaryScreen();
    REQUIRE(unselected);
    // Finish a real compositor transaction before injecting completed orders,
    // so late priority replies from an earlier test cannot overwrite the seam.
    requirePrimary(unselected->name());
    const auto originalOrder = order->outputOrder();
    const auto restoreOrder = qScopeGuard([&] {
        krema::OutputOrderMonitorTestAccess::deliverOrder(order, originalOrder);
    });
    QStringList selected = screenNames();
    selected.removeAll(unselected->name());
    REQUIRE(selected.size() == 2);
    selected.swapItemsAt(0, 1);
    auto manager = makeSelectedManager(selected);
    requireDocksOn(*manager, selected);
    auto *first = shellNamed(*manager, selected.first());
    auto *second = shellNamed(*manager, selected.last());
    REQUIRE(first);
    REQUIRE(second);

    // A selected primary wins even when the saved selection puts it last.
    krema::OutputOrderMonitorTestAccess::deliverOrder(order, {selected.last(), unselected->name(), selected.first()});
    CHECK(manager->primaryShell() == second);
    CHECK(manager->activeShell() == second);

    // With an unselected primary, use the first live selected output in the
    // compositor's adopted order, not unordered-map iteration or saved order.
    krema::OutputOrderMonitorTestAccess::deliverOrder(order, {unselected->name(), selected.last(), selected.first()});
    CHECK(manager->primaryShell() == second);
    CHECK(manager->activeShell() == second);

    // An incomplete (pending) list must not change the shortcut target.
    krema::OutputOrderMonitorTestAccess::deliverOrder(order, {QStringLiteral("not-yet-connected"), selected.first(), selected.last()});
    CHECK(manager->activeShell() == second);

    // No adopted order: the Qt primary is unselected, so saved-name order wins.
    krema::OutputOrderMonitorTestAccess::deliverOrder(order, {});
    CHECK(manager->primaryShell() == first);
    CHECK(manager->activeShell() == first);

    // The existing Focus Dock route falls back deterministically when the
    // cursor is on an output with no dock. A client surface there makes its
    // global pointer coordinates observable to QtWayland.
    QQuickWindow pointerSurface;
    pointerSurface.setScreen(unselected);
    pointerSurface.setGeometry(QRect(unselected->geometry().topLeft(), QSize(120, 80)));
    auto *layer = LayerShellQt::Window::get(&pointerSurface);
    REQUIRE(layer);
    layer->setScope(QStringLiteral("selected-output-pointer-test"));
#ifndef KREMA_COMPAT_NO_LAYERSHELL_SCREEN
    layer->setScreen(unselected);
#endif
    pointerSurface.show();
    REQUIRE(QTest::qWaitFor(
        [&] {
            return pointerSurface.isExposed();
        },
        kTimeoutMs));
    QTest::mouseMove(&pointerSurface, QPoint(40, 30));
    REQUIRE(QTest::qWaitFor(
        [&] {
            return QGuiApplication::screenAt(QCursor::pos()) == unselected;
        },
        kTimeoutMs));
    CHECK(manager->shellAtCursor() == first);
    manager->shellAtCursor()->focusDock();
    // Navigation starts synchronously; wait for KWin and QML keyboard focus
    // before sending Escape.
    REQUIRE(QTest::qWaitFor(
        [&] {
            return first->view()->isActive() && first->view()->rootObject()->hasActiveFocus()
                && first->view()->rootObject()->property("keyboardNavigating").toBool();
        },
        kTimeoutMs));
    CHECK_FALSE(second->view()->rootObject()->property("keyboardNavigating").toBool());
    QTest::keyClick(first->view(), Qt::Key_Escape);
    REQUIRE(QTest::qWaitFor(
        [&] {
            return !first->view()->rootObject()->property("keyboardNavigating").toBool();
        },
        kTimeoutMs));
}

int main(int argc, char *argv[])
{
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
