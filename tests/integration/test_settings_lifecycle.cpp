// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Settings dialog lifecycle across dock shell rebuilds (issue #16).
//
// Drives the real settings QML (SettingsDialog.qml and its pages under
// qml/settings/) and the real MultiDockManager against a KWin virtual
// compositor. Must run under run-with-kwin.sh, which provides WAYLAND_DISPLAY,
// a private session bus and throwaway XDG directories.

#include "krema.h"
#include "models/dockcontextmenu.h"
#include "models/dockmodel.h"
#include "models/launcherentrytracker.h"
#include "models/notificationtracker.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/dockvisibilitycontroller.h"
#include "shell/edgetrigger.h"
#include "shell/multidockmanager.h"
#include "shell/outputordermonitor.h"

#include "config/screensettings.h"
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
// Every warning raised by the settings window's QML (load errors, binding
// loops, unknown properties, ...).
QStringList g_settingsQmlWarnings;
QtMessageHandler g_previousHandler = nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type != QtDebugMsg && type != QtInfoMsg && message.startsWith(QLatin1String("qrc:/qml/")) && message.contains(QLatin1String("Error"))) {
        g_qmlErrors.append(message);
    }
    if (type != QtDebugMsg && type != QtInfoMsg
        && (message.contains(QLatin1String("qrc:/qml/settings/")) || message.contains(QLatin1String("qrc:/qml/SettingsDialog.qml")))) {
        g_settingsQmlWarnings.append(message);
    }
    g_previousHandler(type, context, message);
}

// Mirrors the object graph and wiring Application::run() builds.
struct App {
    std::unique_ptr<KremaSettings> settings;
    std::unique_ptr<krema::DockModel> model;
    std::unique_ptr<krema::NotificationTracker> tracker;
    std::unique_ptr<krema::LauncherEntryTracker> launcherEntries;
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

// Item in the open settings dialog whose text is @p text and, when @p signal
// is given, that exposes it (a sidebar entry's clicked(), a choice card's
// chosen(), a switch's toggled(), ...). @p extra narrows the match further.
// Items outside the window's item tree are found through its QObject tree.
QQuickItem *findInSettings(const QString &text, const char *signal, bool mustBeVisible, const std::function<bool(QQuickItem *)> &extra = {})
{
    auto *window = settingsWindow();
    if (!window) {
        return nullptr;
    }
    const auto match = [&](QQuickItem *item) {
        return (!mustBeVisible || item->isVisible()) && item->property("text").toString() == text && (!signal || item->metaObject()->indexOfSignal(signal) >= 0)
            && (!extra || extra(item));
    };
    if (auto *item = findItem(window->contentItem(), match)) {
        return item;
    }
    const auto items = window->findChildren<QQuickItem *>();
    const auto it = std::find_if(items.begin(), items.end(), match);
    return it != items.end() ? *it : nullptr;
}

// A visible ChoiceCard (qml/settings/ChoiceCard.qml) labelled @p text.
QQuickItem *findCard(const QString &text)
{
    return findInSettings(text, "chosen()", true);
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

// Selects a sidebar page the way a click does and waits until the page shows
// the item labelled @p expectedControl.
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
            return findInSettings(expectedControl, nullptr, true) != nullptr;
        },
        kTimeoutMs));
}

// Selects a choice card the way a user selection does: the card's chosen()
// signal runs the page's onChosen handler, which writes the setting.
void chooseCard(const QString &text)
{
    auto *card = findCard(text);
    REQUIRE(card);
    REQUIRE(card->property("available").toBool());
    QPointer<QQuickItem> guard(card);
    QMetaObject::invokeMethod(card, "chosen");
    // The control the user just used must survive its own handler.
    REQUIRE(guard);
}

// Monitor mode card labels on the "Monitors & Desktops" page
// (MonitorsPage.qml monitorModeNames), indexed by
// MultiDockManager::MonitorMode.
QString monitorModeCard(MultiDockManager::MonitorMode mode)
{
    switch (mode) {
    case MultiDockManager::PrimaryOnly:
        return QStringLiteral("Primary monitor only");
    case MultiDockManager::AllScreens:
        return QStringLiteral("All monitors");
    case MultiDockManager::FollowActive:
        return QStringLiteral("Follow active screen");
    case MultiDockManager::SelectedScreens:
        return QStringLiteral("Selected monitors");
    }
    return {};
}

void showMonitorsPage()
{
    showPage(QStringLiteral("Monitors & Desktops"), monitorModeCard(MultiDockManager::PrimaryOnly));
}

void chooseMonitorMode(MultiDockManager::MonitorMode mode)
{
    chooseCard(monitorModeCard(mode));
}

// Screen edge zone labels on the "Layout & Position" page (LayoutPage.qml
// edgeNames), indexed by the kcfg Edge enum.
QString edgeZoneText(int edge)
{
    static const QStringList names{QStringLiteral("Top"), QStringLiteral("Bottom"), QStringLiteral("Left"), QStringLiteral("Right")};
    return names.at(edge);
}

// Picks a screen edge the way a click on the monitor schematic does: the edge
// zone's clicked() runs its onClicked handler, which writes DockSettings.edge.
void chooseEdge(int edge)
{
    auto *zone = findInSettings(edgeZoneText(edge), "clicked()", true, [](QQuickItem *item) {
        return item->property("edgeIdx").isValid();
    });
    REQUIRE(zone);
    QPointer<QQuickItem> guard(zone);
    QMetaObject::invokeMethod(zone, "clicked");
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
    app().settings->setSelectedOutputs({});
    startDocks();
    const int expected = mode == MultiDockManager::PrimaryOnly || mode == MultiDockManager::SelectedScreens ? 1 : QGuiApplication::screens().size();
    REQUIRE(app().manager->shells().size() == expected);
    g_qmlErrors.clear();
}

void setOutputSelected(const QString &name, bool selected)
{
    QQuickItem *control = nullptr;
    REQUIRE(QTest::qWaitFor(
        [&] {
            control = findInSettings(name, "toggled()", true);
            return control != nullptr;
        },
        kTimeoutMs));
    REQUIRE(control->setProperty("checked", selected));
    REQUIRE(QMetaObject::invokeMethod(control, "toggled"));
}

DockShell *shellForOutput(const QString &name)
{
    for (auto *shell : app().manager->shells()) {
        if (shell->view()->screen() && shell->view()->screen()->name() == name) {
            return shell;
        }
    }
    return nullptr;
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
    showMonitorsPage();

    // QA-01, QA-03: PrimaryOnly -> AllScreens from the dialog's own mode cards
    chooseMonitorMode(MultiDockManager::AllScreens);
    CHECK(app().settings->monitorMode() == MultiDockManager::AllScreens);
    CHECK(app().manager->shells().size() == 2);

    // QA-02: the very same dialog stays open
    CHECK(dialog);
    CHECK(dialog == settingsWindow());

    // QA-08: further changes from the same dialog keep working
    chooseMonitorMode(MultiDockManager::FollowActive);
    CHECK(app().manager->shells().size() == 2);
    chooseMonitorMode(MultiDockManager::PrimaryOnly);
    CHECK(app().manager->shells().size() == 1);
    CHECK(dialog == settingsWindow());
    CHECK(g_qmlErrors.isEmpty());
}

TEST_CASE("Selected output changes before initialization do not create docks", "[settings][selected-output][initialization]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    app().manager.reset();
    auto &a = app();
    REQUIRE(QGuiApplication::screens().size() == 2);
    const auto selected = QGuiApplication::screens().last()->name();
    a.settings->setMonitorMode(MultiDockManager::SelectedScreens);
    auto manager = std::make_unique<MultiDockManager>(a.settings.get(), a.model.get(), a.tracker.get());
    manager->setMonitorMode(MultiDockManager::SelectedScreens);

    a.settings->setSelectedOutputs({selected});
    QCoreApplication::processEvents();
    CHECK(manager->shells().isEmpty());

    manager->initialize();
    REQUIRE(manager->shells().size() == 1);
    CHECK(manager->shells().first()->view()->screen()->name() == selected);
}

TEST_CASE("Selected output changes leave other monitor modes unchanged", "[settings][selected-output][control]")
{
    REQUIRE(QGuiApplication::screens().size() == 2);
    for (const auto mode : {MultiDockManager::PrimaryOnly, MultiDockManager::AllScreens, MultiDockManager::FollowActive}) {
        resetTo(mode);
        QList<QPointer<DockShell>> retained;
        for (auto *shell : app().manager->shells()) {
            retained.append(shell);
        }
        const auto active = app().manager->activeShell();
        app().settings->setSelectedOutputs({QGuiApplication::screens().last()->name(), QStringLiteral("disconnected-control")});
        QCoreApplication::processEvents();
        CHECK(app().manager->shells().size() == retained.size());
        CHECK(app().manager->activeShell() == active);
        for (const auto &shell : retained) {
            CHECK(shell);
        }
    }
    resetTo(MultiDockManager::PrimaryOnly);
}

TEST_CASE(
    "Selected-output switches preserve the focused settings window and "
    "retained docks",
    "[settings][selected-output][lifetime]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    REQUIRE(QGuiApplication::screens().size() == 2);
    const auto primary = krema::OutputOrderMonitor::instance()->primaryScreen()->name();
    QString other;
    for (auto *screen : QGuiApplication::screens()) {
        if (screen->name() != primary) {
            other = screen->name();
        }
    }
    REQUIRE_FALSE(other.isEmpty());
    openSettingsFrom(app().manager->primaryShell());
    QPointer<QQuickWindow> dialog = settingsWindow();
    REQUIRE(dialog);
    showMonitorsPage();
    chooseMonitorMode(MultiDockManager::SelectedScreens);
    REQUIRE(QTest::qWaitFor(
        [&] {
            return dialog->isActive();
        },
        kTimeoutMs));

    // Selecting the fallback's own output adopts the existing shell; adding a
    // second output must preserve it while the new dock inherits the dialog's
    // interaction lock.
    QPointer<DockShell> origin = shellForOutput(primary);
    REQUIRE(origin);
    setOutputSelected(primary, true);
    REQUIRE(shellForOutput(primary) == origin);
    setOutputSelected(other, true);
    REQUIRE(app().manager->shells().size() == 2);
    QPointer<DockShell> retained = shellForOutput(other);
    REQUIRE(retained);
    CHECK(shellForOutput(primary) == origin);
    CHECK(allDocksVisible());
    CHECK(origin->view()->visibilityController()->isInteracting());
    CHECK(retained->view()->visibilityController()->isInteracting());
    CHECK(dialog == settingsWindow());
    CHECK(dialog->isActive());

    // This signal handler removes the dock on the output from which settings
    // was opened. Its screen overlay must die with it, while the global dialog
    // and the retained dock remain alive and focused.
    QPointer<krema::ScreenSettings> originSettings = origin->findChild<krema::ScreenSettings *>();
    REQUIRE(originSettings);
    setOutputSelected(primary, false);
    CHECK_FALSE(origin);
    CHECK_FALSE(originSettings);
    REQUIRE(app().manager->shells().size() == 1);
    CHECK(shellForOutput(other) == retained);
    CHECK(app().settings->selectedOutputs() == QStringList{other});
    CHECK(dialog == settingsWindow());
    CHECK(dialog->isActive());
    CHECK(retained->view()->visibilityController()->isInteracting());

    setOutputSelected(primary, true);
    REQUIRE(app().manager->shells().size() == 2);
    CHECK(shellForOutput(other) == retained);
    CHECK(allDocksVisible());
    CHECK(shellForOutput(primary)->view()->visibilityController()->isInteracting());
    CHECK(dialog == settingsWindow());
    CHECK(dialog->isActive());
    closeSettings();
    CHECK(QTest::qWaitFor(allDocksHidden, kTimeoutMs));
    for (auto *shell : app().manager->shells()) {
        CHECK_FALSE(shell->view()->visibilityController()->isInteracting());
    }
    CHECK(g_qmlErrors.isEmpty());
    resetTo(MultiDockManager::PrimaryOnly);
}

TEST_CASE(
    "Docks rebuilt while the settings dialog is open stay shown until it "
    "closes",
    "[settings][visibility]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    openSettingsFrom(app().manager->primaryShell());
    showMonitorsPage();

    // QA-04: new shells created by the mode change inherit the open dialog's
    // lock (an unlocked auto-hide dock is hidden as soon as it is set up)
    chooseMonitorMode(MultiDockManager::AllScreens);
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

    // Control: the pointer entering the other screen's edge strip still
    // switches screens. The hidden dock itself is unmapped and gets no pointer
    // events; the mapped edge strip on its screen stands in for it.
    DockShell *other = nullptr;
    for (auto *shell : app().manager->shells()) {
        if (shell != shown.first()) {
            other = shell;
        }
    }
    REQUIRE(other);
    auto *otherController = other->view()->visibilityController();
    krema::EdgeTrigger *edgeTrigger = nullptr;
    for (auto *window : QGuiApplication::allWindows()) {
        if (auto *candidate = qobject_cast<krema::EdgeTrigger *>(window); candidate && candidate->screen() == other->view()->screen()) {
            edgeTrigger = candidate;
        }
    }
    REQUIRE(edgeTrigger);
    CHECK(edgeTrigger->isVisible());
    // An open dialog holds the dock where it is; closing it releases that.
    REQUIRE(!shown.first()->view()->visibilityController()->isInteracting());
    QEnterEvent enter(QPointF(1, 1), QPointF(1, 1), QPointF(1, 1));
    QCoreApplication::sendEvent(edgeTrigger, &enter);
    CHECK(QTest::qWaitFor(
        [&] {
            const auto docks = shownDocks();
            return docks.size() == 1 && docks.first() == other;
        },
        kTimeoutMs));
    // The strip of the now active screen is unmapped, the old screen's mapped.
    CHECK(!edgeTrigger->isVisible());

    // QA-18: global shortcuts route to the visible Follow Active dock. Both
    // paths (focus-dock, toggle-dock, Meta+N) go through shellAtCursor() /
    // activeShell(); before the fix they returned the hidden primary-screen
    // shell, so toggling it only changed that dock's controller.
    CHECK(app().manager->shellAtCursor() == other);
    const bool wasVisible = otherController->isDockVisible();
    app().manager->shellAtCursor()->view()->visibilityController()->toggleVisibility();
    CHECK(otherController->isDockVisible() != wasVisible);

    app().settings->setFollowActiveTrigger(trigger);
    resetTo(MultiDockManager::PrimaryOnly);
}

TEST_CASE("Settings requested from a rebuilt dock reuses the open dialog", "[settings][single-dialog]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    openSettingsFrom(app().manager->primaryShell());
    QPointer<QQuickWindow> dialog = settingsWindow();
    showMonitorsPage();
    chooseMonitorMode(MultiDockManager::AllScreens);

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

// Issue #24: repeated requests must reuse the one open window, whatever
// created it.
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
    showMonitorsPage();

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

TEST_CASE("Background style cards report style availability", "[settings][panel-style]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    openSettingsFrom(app().manager->primaryShell());
    // Rebuild the shells first so the dialog outlives the dock it was opened
    // from.
    showMonitorsPage();
    chooseMonitorMode(MultiDockManager::AllScreens);
    showPage(QStringLiteral("Panel Style"), QStringLiteral("Panel Inherit"));

    // QA-06: every style is available on this system, so none of the four
    // style cards is marked unavailable
    static const QStringList styles{QStringLiteral("Panel Inherit"), QStringLiteral("Transparent"), QStringLiteral("Tinted"), QStringLiteral("Acrylic")};
    for (const auto &style : styles) {
        INFO("style " << style.toStdString());
        auto *card = findCard(style);
        REQUIRE(card);
        CHECK(card->property("available").toBool());
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
    showMonitorsPage();
    chooseMonitorMode(MultiDockManager::AllScreens);
    chooseMonitorMode(MultiDockManager::PrimaryOnly);
    closeSettings();

    // QA-16: the docks torn down by the round trip leave nothing behind that
    // the compositor can still address when the next window is mapped
    openSettingsFrom(app().manager->primaryShell());
    showMonitorsPage();
    CHECK(app().settings->monitorMode() == MultiDockManager::PrimaryOnly);
    closeSettings();
}

TEST_CASE("Control: non-topology settings change from the dialog", "[settings][control]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    openSettingsFrom(app().manager->primaryShell());
    QPointer<QQuickWindow> dialog = settingsWindow();
    showPage(QStringLiteral("Layout & Position"), edgeZoneText(0));

    // QA-09: changing the edge (Top) is applied without rebuilding the dock
    chooseEdge(0);
    CHECK(app().settings->edge() == 0);
    CHECK(app().manager->primaryShell()->view()->edge() == 0);
    CHECK(dialog == settingsWindow());
    chooseEdge(1);
    closeSettings();
}

namespace
{

// Issue #27: SettingsWindow must destroy the window it opened itself. Left to
// the QML engine's teardown, the still-open (or still loading) window crashed
// Krema on Debian 13. A fresh dock graph means a fresh settings engine, so the
// window's QML is loaded from scratch, as when Krema starts.
void restartDocks()
{
    if (!visibleSettingsWindows().isEmpty()) {
        closeSettings();
    }
    app().manager.reset();
    QCoreApplication::processEvents();
    startDocks();
    g_qmlErrors.clear();
}

// The Settings window object, shown or hidden.
QQuickWindow *settingsWindowObject()
{
    const auto windows = QGuiApplication::topLevelWindows();
    for (auto *window : windows) {
        auto *quickWindow = qobject_cast<QQuickWindow *>(window);
        if (quickWindow && quickWindow->title() == QLatin1String("Settings")) {
            return quickWindow;
        }
    }
    return nullptr;
}

// The dock graph is gone: no Settings window object is left, and Krema's own
// QML raised no errors while it was torn down.
void checkSettingsTornDown(const QPointer<QQuickWindow> &window)
{
    QCoreApplication::processEvents();
    CHECK_FALSE(window);
    CHECK(settingsWindowObjectCount() == 0);
    CHECK(g_qmlErrors.isEmpty());
}

} // namespace

TEST_CASE("Every settings page loads without QML warnings", "[settings][pages]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    g_settingsQmlWarnings.clear();
    openSettingsFrom(app().manager->primaryShell());
    QPointer<QQuickWindow> dialog = settingsWindow();
    REQUIRE(dialog);
    auto *loader = dialog->findChild<QQuickItem *>(QStringLiteral("settingsPageLoader"));
    REQUIRE(loader);

    const auto modules = dialog->property("modules").toList();
    REQUIRE(modules.size() == 10);
    for (const auto &entry : modules) {
        const QString moduleId = entry.toMap().value(QStringLiteral("moduleId")).toString();
        INFO("module " << moduleId.toStdString());
        QMetaObject::invokeMethod(dialog, "openModule", Q_ARG(QVariant, moduleId));
        CHECK(dialog->property("currentModule").toString() == moduleId);
        REQUIRE(QTest::qWaitFor(
            [loader] {
                return loader->property("status").toInt() == 1 /* Loader.Ready */ && loader->property("item").value<QQuickItem *>();
            },
            kTimeoutMs));
        // Let delayed bindings, nested Loaders and animations of the page start.
        QTest::qWait(300);
        CHECK(g_settingsQmlWarnings.isEmpty());
        if (!g_settingsQmlWarnings.isEmpty()) {
            UNSCOPED_INFO(g_settingsQmlWarnings.join(QLatin1Char('\n')).toStdString());
            g_settingsQmlWarnings.clear();
        }
    }
    closeSettings();
    CHECK(g_settingsQmlWarnings.isEmpty());
}

TEST_CASE("Shutting down with the settings dialog open", "[settings][shutdown]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    restartDocks();
    openSettingsFrom(app().manager->primaryShell());
    QPointer<QQuickWindow> dialog = settingsWindow();

    // QA-10: destroying the dock graph with the dialog open neither crashes
    // nor leaves the dialog behind — and teardown emits no QML errors
    // (a SettingsWindow destroyed while its engine is still alive logs
    // TypeErrors like "Cannot read property 'isStyleAvailable' of null").
    app().manager.reset();
    checkSettingsTornDown(dialog);
}

TEST_CASE("Shutting down in the same turn as opening settings", "[settings][shutdown-while-opening]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    restartDocks();

    // #27 QA-01: Quit right after "Settings...": the window exists but its
    // pages are still being created when the dock graph goes away
    Q_EMIT app().manager->primaryShell()->contextMenu()->settingsRequested();
    QPointer<QQuickWindow> dialog = settingsWindowObject();
    REQUIRE(dialog);
    app().manager.reset();
    checkSettingsTornDown(dialog);
}

TEST_CASE("Shutting down right after closing settings", "[settings][shutdown-after-close]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    restartDocks();
    openSettingsFrom(app().manager->primaryShell());
    QPointer<QQuickWindow> dialog = settingsWindow();

    // #27 QA-03: the closed window's deferred deletion has not run yet when
    // the dock graph goes away
    dialog->close();
    app().manager.reset();
    checkSettingsTornDown(dialog);
}

TEST_CASE("Shutting down right after reopening settings", "[settings][shutdown-after-reopen]")
{
    resetTo(MultiDockManager::PrimaryOnly);
    restartDocks();
    openSettingsFrom(app().manager->primaryShell());
    QPointer<QQuickWindow> closed = settingsWindow();

    // #27 QA-09: close and reopen with no event loop turn in between, so the
    // closed window still exists next to the new one when the dock graph goes
    // away (on kirigami-addons < 1.8 every open() creates a new window)
    closed->close();
    Q_EMIT app().manager->primaryShell()->contextMenu()->settingsRequested();
    QPointer<QQuickWindow> reopened = settingsWindow();
    REQUIRE(reopened);
    app().manager.reset();
    CHECK_FALSE(closed);
    checkSettingsTornDown(reopened);
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
