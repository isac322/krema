// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Settings dialog lifecycle across dock shell rebuilds (issue #16).
//
// Drives the real settings QML (SettingsDialog.qml + BehaviorPage.qml) and the
// real MultiDockManager against a KWin virtual compositor. Must run under
// run-with-kwin.sh, which provides WAYLAND_DISPLAY, a private session bus and
// throwaway XDG directories.

#include "krema.h"
#include "models/dockcontextmenu.h"
#include "models/dockmodel.h"
#include "models/notificationtracker.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/dockvisibilitycontroller.h"
#include "shell/multidockmanager.h"
#include "shell/outputordermonitor.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <KAboutData>

#include <QApplication>
#include <QPointer>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSet>
#include <QTest>
#include <QtQml>

#include <algorithm>
#include <functional>
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

constexpr int kTimeoutMs = 10000;
constexpr int kHideDelayMs = 50;

// QML runtime errors (TypeError, ReferenceError, ...) raised by Krema's own
// QML.
QStringList g_qmlErrors;
QtMessageHandler g_previousHandler = nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type != QtDebugMsg && type != QtInfoMsg && message.startsWith(QLatin1String("qrc:/qml/")) && message.contains(QLatin1String("Error"))) {
        g_qmlErrors.append(message);
    }
    g_previousHandler(type, context, message);
}

// Mirrors the object graph and wiring Application::run() builds.
struct App {
    std::unique_ptr<KremaSettings> settings;
    std::unique_ptr<krema::DockModel> model;
    std::unique_ptr<krema::NotificationTracker> tracker;
    std::unique_ptr<MultiDockManager> manager;
};

App &app()
{
    static App *instance = [] {
        auto *a = new App;
        a->settings = std::make_unique<KremaSettings>();
        a->settings->load();
        // Auto-hide with a short delay: a dock whose settings-dialog interaction
        // lock is missing hides almost immediately, which makes the lock
        // observable.
        a->settings->setVisibilityMode(1);
        a->settings->setShowDelay(0);
        a->settings->setHideDelay(kHideDelayMs);
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

// Creates the dock shells the way Application::run() does, if not running.
void startDocks()
{
    auto &a = app();
    if (a.manager) {
        return;
    }
    a.manager = std::make_unique<MultiDockManager>(a.settings.get(), a.model.get(), a.tracker.get());

    // As in Application::run(): the first docks are created only once the
    // Plasma output order is known. Created earlier, they would be rebuilt
    // when the primary output resolves, which production never does.
    auto *outputOrder = krema::OutputOrderMonitor::instance();
    REQUIRE(QTest::qWaitFor(
        [outputOrder] {
            return outputOrder->orderReady();
        },
        kTimeoutMs));
    a.manager->initialize();

    // Same connection as Application::run(): the settings dialog writes
    // DockSettings.monitorMode and the manager rebuilds its shells.
    QObject::connect(a.settings.get(), &KremaSettings::MonitorModeChanged, a.manager.get(), [&a]() {
        a.manager->setMonitorMode(static_cast<MultiDockManager::MonitorMode>(a.settings->monitorMode()));
    });

    // As in Application::run(): only the dock surfaces are layer-shell surfaces.
    qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");
}

QQuickItem *findItem(QQuickItem *item, const std::function<bool(QQuickItem *)> &match)
{
    if (!item) {
        return nullptr;
    }
    if (match(item)) {
        return item;
    }
    const auto children = item->childItems();
    for (auto *child : children) {
        if (auto *found = findItem(child, match)) {
            return found;
        }
    }
    return nullptr;
}

QList<QQuickWindow *> visibleSettingsWindows()
{
    QList<QQuickWindow *> result;
    const auto windows = QGuiApplication::topLevelWindows();
    for (auto *window : windows) {
        auto *quickWindow = qobject_cast<QQuickWindow *>(window);
        if (quickWindow && quickWindow->isVisible() && quickWindow->title() == QLatin1String("Settings")) {
            result.append(quickWindow);
        }
    }
    return result;
}

QQuickWindow *settingsWindow()
{
    const auto windows = visibleSettingsWindows();
    return windows.size() == 1 ? windows.first() : nullptr;
}

// Every Settings window object, shown or hidden: a closed window that was
// never destroyed still counts.
int settingsWindowObjectCount()
{
    const auto windows = QGuiApplication::topLevelWindows();
    return static_cast<int>(std::count_if(windows.begin(), windows.end(), [](QWindow *window) {
        return qobject_cast<QQuickWindow *>(window) && window->title() == QLatin1String("Settings");
    }));
}

// Item in the open settings dialog whose text is @p text and that exposes
// @p signal (a sidebar entry's clicked() or a combobox's activated(int)).
// Popup content such as the sidebar drawer is not always parented into the
// window's item tree, so the window's QObject tree is searched as well.
QQuickItem *findInSettings(const QString &text, const char *signal, bool mustBeVisible)
{
    auto *window = settingsWindow();
    if (!window) {
        return nullptr;
    }
    const auto match = [&](QQuickItem *item) {
        return (!mustBeVisible || item->isVisible()) && item->property("text").toString() == text && item->metaObject()->indexOfSignal(signal) >= 0;
    };
    if (auto *item = findItem(window->contentItem(), match)) {
        return item;
    }
    const auto items = window->findChildren<QQuickItem *>();
    const auto it = std::find_if(items.begin(), items.end(), match);
    return it != items.end() ? *it : nullptr;
}

// Settings page controls; pages stay cached (hidden) after switching away.
QQuickItem *findControl(const QString &label)
{
    return findInSettings(label, "activated(int)", true);
}

void openSettingsFrom(DockShell *shell)
{
    REQUIRE(shell);
    Q_EMIT shell->contextMenu()->settingsRequested();
    REQUIRE(QTest::qWaitFor(
        [] {
            return settingsWindow() != nullptr;
        },
        kTimeoutMs));
}

// Selects a sidebar module the way a click does.
void showPage(const QString &moduleText, const QString &expectedControl)
{
    // The sidebar creates its entries a few frames after the window maps.
    QQuickItem *entry = nullptr;
    REQUIRE(QTest::qWaitFor(
        [&] {
            entry = findInSettings(moduleText, "clicked()", false);
            return entry != nullptr;
        },
        kTimeoutMs));
    QMetaObject::invokeMethod(entry, "clicked");
    REQUIRE(QTest::qWaitFor(
        [&] {
            return findControl(expectedControl) != nullptr;
        },
        kTimeoutMs));
}

// Picks an entry in a settings combobox the way a user selection does: the
// delegate's activated(int) signal runs the page's onActivated handler.
void chooseInCombo(const QString &label, int index)
{
    auto *combo = findControl(label);
    REQUIRE(combo);
    QPointer<QQuickItem> guard(combo);
    QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, index));
    // The control the user just used must survive its own handler.
    REQUIRE(guard);
}

void closeSettings()
{
    if (auto *window = settingsWindow()) {
        window->close();
    }
    REQUIRE(QTest::qWaitFor(
        [] {
            return visibleSettingsWindows().isEmpty();
        },
        kTimeoutMs));
}

bool allDocksVisible()
{
    const auto shells = app().manager->shells();
    return std::all_of(shells.begin(), shells.end(), [](DockShell *shell) {
        return shell->view()->visibilityController()->isDockVisible();
    });
}

bool allDocksHidden()
{
    const auto shells = app().manager->shells();
    return std::all_of(shells.begin(), shells.end(), [](DockShell *shell) {
        return !shell->view()->visibilityController()->isDockVisible();
    });
}

void resetTo(MultiDockManager::MonitorMode mode)
{
    if (!visibleSettingsWindows().isEmpty()) {
        closeSettings();
    }
    app().settings->setMonitorMode(mode);
    startDocks();
    REQUIRE(app().manager->shells().size() == (mode == MultiDockManager::PrimaryOnly ? 1 : QGuiApplication::screens().size()));
    g_qmlErrors.clear();
}

} // namespace

TEST_CASE(
    "Changing monitor mode from the settings dialog keeps Krema and the "
    "dialog alive",
    "[settings][monitor-mode]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    REQUIRE(QGuiApplication::screens().size() == 2);

    openSettingsFrom(app().manager->primaryShell());
    QPointer<QQuickWindow> dialog = settingsWindow();
    showPage(QStringLiteral("Behavior"), QStringLiteral("Monitor mode"));

    // QA-01, QA-03: PrimaryOnly -> AllScreens from the dialog's own combobox
    chooseInCombo(QStringLiteral("Monitor mode"), MultiDockManager::AllScreens);
    CHECK(app().settings->monitorMode() == MultiDockManager::AllScreens);
    CHECK(app().manager->shells().size() == 2);

    // QA-02: the very same dialog stays open
    CHECK(dialog);
    CHECK(dialog == settingsWindow());

    // QA-08: further changes from the same dialog keep working
    chooseInCombo(QStringLiteral("Monitor mode"), MultiDockManager::FollowActive);
    CHECK(app().manager->shells().size() == 2);
    chooseInCombo(QStringLiteral("Monitor mode"), MultiDockManager::PrimaryOnly);
    CHECK(app().manager->shells().size() == 1);
    CHECK(dialog == settingsWindow());
    CHECK(g_qmlErrors.isEmpty());
}

TEST_CASE(
    "Docks rebuilt while the settings dialog is open stay shown until it "
    "closes",
    "[settings][visibility]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    openSettingsFrom(app().manager->primaryShell());
    showPage(QStringLiteral("Behavior"), QStringLiteral("Monitor mode"));

    // QA-04: new shells created by the mode change inherit the open dialog's
    // lock (an unlocked auto-hide dock is hidden as soon as it is set up)
    chooseInCombo(QStringLiteral("Monitor mode"), MultiDockManager::AllScreens);
    REQUIRE(app().manager->shells().size() == 2);
    CHECK(allDocksVisible());

    // ... and the lock is released exactly once when the dialog closes
    closeSettings();
    CHECK(QTest::qWaitFor(allDocksHidden, kTimeoutMs));
}

TEST_CASE("Opening settings does not switch the Follow Active dock", "[settings][follow-active]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    const int trigger = app().settings->followActiveTrigger();
    app().settings->setFollowActiveTrigger(0); // mouse
    resetTo(MultiDockManager::FollowActive);

    const auto shownDocks = [] {
        QList<DockShell *> shown;
        for (auto *shell : app().manager->shells()) {
            if (shell->view()->isVisible()) {
                shown.append(shell);
            }
        }
        return shown;
    };
    REQUIRE(shownDocks().size() == 1);
    auto *active = shownDocks().first();
    const QScreen *activeScreen = active->view()->screen();

    // QA-17: the dialog's lock also keeps the hidden docks' controllers shown;
    // that is not pointer activity and must not move the dock to another
    // screen (the Follow Active switch is debounced by 300 ms).
    openSettingsFrom(active);
    QTest::qWait(1200);
    const auto shown = shownDocks();
    REQUIRE(shown.size() == 1);
    CHECK(shown.first()->view()->screen() == activeScreen);

    closeSettings();

    // Control: pointer hover over the hidden dock still switches screens.
    DockShell *other = nullptr;
    for (auto *shell : app().manager->shells()) {
        if (shell != shown.first()) {
            other = shell;
        }
    }
    REQUIRE(other);
    auto *otherController = other->view()->visibilityController();
    REQUIRE(QTest::qWaitFor(
        [otherController] {
            return !otherController->isDockVisible();
        },
        kTimeoutMs));
    otherController->setHovered(true);
    CHECK(QTest::qWaitFor(
        [&] {
            const auto docks = shownDocks();
            return docks.size() == 1 && docks.first() == other;
        },
        kTimeoutMs));
    otherController->setHovered(false);

    // QA-18: global shortcuts route to the visible Follow Active dock. Both
    // paths (focus-dock, toggle-dock, Meta+N) go through shellAtCursor() /
    // activeShell(); before the fix they returned the hidden primary-screen
    // shell, so toggling it only changed that dock's controller.
    CHECK(app().manager->shellAtCursor() == other);
    app().manager->shellAtCursor()->view()->visibilityController()->toggleVisibility();
    CHECK(QTest::qWaitFor(
        [otherController] {
            return !otherController->isDockVisible();
        },
        kTimeoutMs));


    app().settings->setFollowActiveTrigger(trigger);
    resetTo(MultiDockManager::PrimaryOnly);
}

TEST_CASE("Settings requested from a rebuilt dock reuses the open dialog", "[settings][single-dialog]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    openSettingsFrom(app().manager->primaryShell());
    QPointer<QQuickWindow> dialog = settingsWindow();
    showPage(QStringLiteral("Behavior"), QStringLiteral("Monitor mode"));
    chooseInCombo(QStringLiteral("Monitor mode"), MultiDockManager::AllScreens);

    // QA-05: every shell, including the rebuilt ones, opens the same single
    // dialog, from both the "Settings..." and the "About" menu entries
    const auto shells = app().manager->shells();
    for (auto *shell : shells) {
        Q_EMIT shell->contextMenu()->settingsRequested();
        QCoreApplication::processEvents();
        CHECK(visibleSettingsWindows().size() == 1);
        CHECK(dialog == settingsWindow());
        Q_EMIT shell->contextMenu()->aboutRequested();
        QCoreApplication::processEvents();
        CHECK(visibleSettingsWindows().size() == 1);
        CHECK(dialog == settingsWindow());
    }
    CHECK(allDocksVisible());

    // Closing it releases every dock's lock, including the shells that
    // re-requested the already open dialog.
    closeSettings();
    CHECK(QTest::qWaitFor(allDocksHidden, kTimeoutMs));
}

// Issue #24: on kirigami-addons < 1.8 ConfigurationView has no configViewItem,
// so the window open() creates has to be found another way. The same contract
// holds on every kirigami-addons version.
TEST_CASE("Opening settings twice keeps one tracked window", "[settings][single-window]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    const auto shell = [] {
        return app().manager->primaryShell();
    };
    REQUIRE(QTest::qWaitFor(allDocksHidden, kTimeoutMs));

    openSettingsFrom(shell());
    QPointer<QQuickWindow> dialog = settingsWindow();

    // QA-01, QA-02: asking again, from either menu entry, raises the same
    // window instead of opening another one
    Q_EMIT shell()->contextMenu()->settingsRequested();
    QCoreApplication::processEvents();
    CHECK(visibleSettingsWindows().size() == 1);
    CHECK(dialog == settingsWindow());
    Q_EMIT shell()->contextMenu()->aboutRequested();
    QCoreApplication::processEvents();
    CHECK(visibleSettingsWindows().size() == 1);
    CHECK(dialog == settingsWindow());

    // QA-03: the auto-hide dock is shown and stays shown past its hide delay
    // while the dialog is open
    REQUIRE(QTest::qWaitFor(allDocksVisible, kTimeoutMs));
    QTest::qWait(kHideDelayMs * 10);
    CHECK(allDocksVisible());

    // QA-04: closing that window releases the lock, so the tracked window is
    // the Settings window and not another Krema surface
    closeSettings();
    CHECK(QTest::qWaitFor(allDocksHidden, kTimeoutMs));
    CHECK(g_qmlErrors.isEmpty());
}

TEST_CASE("Settings reopen cleanly after being closed", "[settings][reopen-after-close]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    const auto shell = [] {
        return app().manager->primaryShell();
    };

    openSettingsFrom(shell());
    showPage(QStringLiteral("Behavior"), QStringLiteral("Monitor mode"));

    // QA-06: Escape closes the dialog, releases the lock and destroys the
    // window instead of leaving a hidden one behind
    QTest::keyClick(settingsWindow(), Qt::Key_Escape);
    CHECK(QTest::qWaitFor(allDocksHidden, kTimeoutMs));
    CHECK(QTest::qWaitFor(
        [] {
            return settingsWindowObjectCount() == 0;
        },
        kTimeoutMs));

    // QA-05: reopening tracks the new window: one Settings window exists, the
    // lock is taken again, and a second request still reuses it
    openSettingsFrom(shell());
    QPointer<QQuickWindow> dialog = settingsWindow();
    CHECK(settingsWindowObjectCount() == 1);
    REQUIRE(QTest::qWaitFor(allDocksVisible, kTimeoutMs));
    QTest::qWait(kHideDelayMs * 10);
    CHECK(allDocksVisible());
    Q_EMIT shell()->contextMenu()->settingsRequested();
    QCoreApplication::processEvents();
    CHECK(dialog == settingsWindow());

    closeSettings();
    CHECK(QTest::qWaitFor(allDocksHidden, kTimeoutMs));
    CHECK(QTest::qWaitFor(
        [] {
            return settingsWindowObjectCount() == 0;
        },
        kTimeoutMs));
    CHECK(g_qmlErrors.isEmpty());
}

TEST_CASE("Background style list reports style availability", "[settings][appearance]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    openSettingsFrom(app().manager->primaryShell());
    // Rebuild the shells first so the dialog outlives the dock it was opened
    // from.
    showPage(QStringLiteral("Behavior"), QStringLiteral("Monitor mode"));
    chooseInCombo(QStringLiteral("Monitor mode"), MultiDockManager::AllScreens);
    showPage(QStringLiteral("Appearance"), QStringLiteral("Style"));

    // QA-06: every style is available on this system, so none is marked
    // unavailable
    auto *combo = findControl(QStringLiteral("Style"));
    REQUIRE(combo);
    const auto entries = combo->property("model").toStringList();
    CHECK(entries.size() == 4);
    for (const auto &entry : entries) {
        CHECK_FALSE(entry.contains(QLatin1String("unavailable")));
    }
    CHECK(g_qmlErrors.isEmpty());
}

TEST_CASE("Rebuilding docks releases every dock surface", "[settings][teardown]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    QCoreApplication::processEvents();
    const auto baseline = QGuiApplication::topLevelWindows().size();

    // QA-07: shells torn down by mode changes leave no surfaces behind and
    // raise no QML errors while being destroyed.
    app().settings->setMonitorMode(MultiDockManager::AllScreens);
    app().settings->setMonitorMode(MultiDockManager::PrimaryOnly);
    app().settings->setMonitorMode(MultiDockManager::FollowActive);
    app().settings->setMonitorMode(MultiDockManager::PrimaryOnly);

    CHECK(QTest::qWaitFor(
        [baseline] {
            return QGuiApplication::topLevelWindows().size() == baseline;
        },
        kTimeoutMs));
    CHECK(g_qmlErrors.isEmpty());
}

TEST_CASE("All monitors mode puts one dock on every screen", "[monitor-mode][placement]")
{
    resetTo(MultiDockManager::PrimaryOnly);

    // QA-15: each dock is placed on its own screen, not all on the primary one
    app().settings->setMonitorMode(MultiDockManager::AllScreens);
    QSet<QScreen *> docked;
    const auto shells = app().manager->shells();
    for (auto *shell : shells) {
        auto *view = shell->view();
        REQUIRE(view->screen());
        CHECK(view->screen()->geometry().contains(view->geometry().center()));
        docked.insert(view->screen());
    }
    const auto screens = QGuiApplication::screens();
    CHECK(docked == QSet<QScreen *>(screens.begin(), screens.end()));
}

TEST_CASE("Settings reopen after a monitor mode round trip", "[settings][monitor-mode][reopen]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    openSettingsFrom(app().manager->primaryShell());
    showPage(QStringLiteral("Behavior"), QStringLiteral("Monitor mode"));
    chooseInCombo(QStringLiteral("Monitor mode"), MultiDockManager::AllScreens);
    chooseInCombo(QStringLiteral("Monitor mode"), MultiDockManager::PrimaryOnly);
    closeSettings();

    // QA-16: the docks torn down by the round trip leave nothing behind that
    // the compositor can still address when the next window is mapped
    openSettingsFrom(app().manager->primaryShell());
    showPage(QStringLiteral("Behavior"), QStringLiteral("Monitor mode"));
    CHECK(app().settings->monitorMode() == MultiDockManager::PrimaryOnly);
    closeSettings();
}

TEST_CASE("Control: non-topology settings change from the dialog", "[settings][control]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    openSettingsFrom(app().manager->primaryShell());
    QPointer<QQuickWindow> dialog = settingsWindow();
    showPage(QStringLiteral("Behavior"), QStringLiteral("Screen edge"));

    // QA-09: changing the edge (Top) is applied without rebuilding the dock
    chooseInCombo(QStringLiteral("Screen edge"), 0);
    CHECK(app().settings->edge() == 0);
    CHECK(app().manager->primaryShell()->view()->edge() == 0);
    CHECK(dialog == settingsWindow());
    chooseInCombo(QStringLiteral("Screen edge"), 1);
    closeSettings();
}

TEST_CASE("Shutting down with the settings dialog open", "[settings][shutdown]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    openSettingsFrom(app().manager->primaryShell());

    // QA-10: destroying the dock graph with the dialog open neither crashes
    // nor leaves the dialog behind — and teardown emits no QML errors
    // (a SettingsWindow destroyed while its engine is still alive logs
    // TypeErrors like "Cannot read property 'isStyleAvailable' of null").
    g_qmlErrors.clear();
    app().manager.reset();
    QCoreApplication::processEvents();
    CHECK(visibleSettingsWindows().isEmpty());
    CHECK(g_qmlErrors.isEmpty());
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
    // As in Application::run(): opt into the layer-shell platform plugin
    // (what the deprecated LayerShellQt::Shell::useLayerShell() did).
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");
    g_previousHandler = qInstallMessageHandler(messageHandler);

    return Catch::Session().run(argc, argv);
}
