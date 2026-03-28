// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "multidockmanager.h"

#include "config/screensettings.h"
#include "dockshell.h"
#include "dockview.h"
#include "dockvisibilitycontroller.h"
#include "krema.h"
#include "models/dockactions.h"
#include "models/dockmodel.h"
#include "platform/dockplatform.h"
#include "platform/dockplatformfactory.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <QCursor>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>

Q_LOGGING_CATEGORY(lcMultiDock, "krema.shell.multidock")

namespace krema
{

MultiDockManager::MultiDockManager(KremaSettings *settings, DockModel *model, NotificationTracker *tracker, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_model(model)
    , m_tracker(tracker)
{
    // Debounce screen topology changes (hot-plug, mirror→extended transitions)
    m_topologyDebounce.setSingleShot(true);
    m_topologyDebounce.setInterval(300);
    connect(&m_topologyDebounce, &QTimer::timeout, this, &MultiDockManager::processTopologyUpdate);

    // Debounce follow-active screen switches (prevents flicker on rapid Alt+Tab)
    m_followActiveDebounce.setSingleShot(true);
    m_followActiveDebounce.setInterval(300);

    // Monitor screen lifecycle
    connect(qApp, &QGuiApplication::screenAdded, this, &MultiDockManager::onScreenAdded);
    connect(qApp, &QGuiApplication::screenRemoved, this, &MultiDockManager::onScreenRemoved);
    connect(qApp, &QGuiApplication::primaryScreenChanged, this, &MultiDockManager::onPrimaryScreenChanged);
}

MultiDockManager::~MultiDockManager() = default;

void MultiDockManager::initialize()
{
    m_mode = static_cast<MonitorMode>(m_settings->monitorMode());
    applyMode();
    m_initialized = true;
}

void MultiDockManager::setMonitorMode(MonitorMode mode)
{
    if (m_mode == mode) {
        return;
    }
    qCInfo(lcMultiDock) << "Monitor mode changed:" << m_mode << "->" << mode;
    m_mode = mode;
    if (m_initialized) {
        applyMode();
    }
}

DockShell *MultiDockManager::primaryShell() const
{
    auto *primary = QGuiApplication::primaryScreen();
    if (primary) {
        auto it = m_shells.find(primary);
        if (it != m_shells.end()) {
            return it->second.get();
        }
    }
    // Fallback: return any available shell
    if (!m_shells.empty()) {
        return m_shells.begin()->second.get();
    }
    return nullptr;
}

DockShell *MultiDockManager::shellAtCursor() const
{
    auto *screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen) {
        auto it = m_shells.find(screen);
        if (it != m_shells.end()) {
            return it->second.get();
        }
    }
    return primaryShell();
}

QList<DockShell *> MultiDockManager::shells() const
{
    QList<DockShell *> result;
    result.reserve(static_cast<qsizetype>(m_shells.size()));
    for (const auto &[screen, shell] : m_shells) {
        result.append(shell.get());
    }
    return result;
}

void MultiDockManager::applyMode()
{
    // Stop follow-active tracking when switching modes
    m_followActiveDebounce.stop();
    m_activeScreen = nullptr;

    destroyAllShells();

    switch (m_mode) {
    case PrimaryOnly:
        setupPrimaryOnly();
        break;
    case AllScreens:
        setupAllScreens();
        break;
    case FollowActive:
        setupFollowActive();
        break;
    }
}

void MultiDockManager::setupPrimaryOnly()
{
    auto *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        qCWarning(lcMultiDock) << "No primary screen available";
        return;
    }
    createShellForScreen(screen);
}

void MultiDockManager::setupAllScreens()
{
    const auto screens = QGuiApplication::screens();
    for (auto *screen : screens) {
        if (screen->geometry().isEmpty()) {
            qCDebug(lcMultiDock) << "Skipping screen with empty geometry:" << screen->name();
            continue;
        }
        createShellForScreen(screen);
    }
}

DockShell *MultiDockManager::createShellForScreen(QScreen *screen)
{
    if (auto it = m_shells.find(screen); it != m_shells.end()) {
        qCDebug(lcMultiDock) << "Shell already exists for screen:" << screen->name();
        return it->second.get();
    }

    qCInfo(lcMultiDock) << "Creating dock shell for screen:" << screen->name() << "geometry:" << screen->geometry();

    // Create per-screen settings overlay (falls back to global KremaSettings)
    auto *screenSettings = new ScreenSettings(screen->name(), m_settings, this);

    auto platform = DockPlatformFactory::create();
    auto shell = std::make_unique<DockShell>(m_settings, screenSettings, m_model, m_tracker, std::move(platform), this);

    // Set the screen on the DockView before initialization so layer-shell
    // assigns the surface to the correct output.
    shell->view()->setScreen(screen);

    auto edge = static_cast<DockPlatform::Edge>(screenSettings->edge());
    auto visibilityMode = static_cast<DockPlatform::VisibilityMode>(screenSettings->visibilityMode());
    shell->initialize(edge, visibilityMode);

    // Forward pinned launcher changes from this shell to the manager-level signal
    connect(shell->actions(), &DockActions::pinnedLaunchersChanged, this, &MultiDockManager::pinnedLaunchersChanged);

    auto *shellPtr = shell.get();
    m_shells.emplace(screen, std::move(shell));
    return shellPtr;
}

void MultiDockManager::destroyShellForScreen(QScreen *screen)
{
    if (auto it = m_shells.find(screen); it != m_shells.end()) {
        qCInfo(lcMultiDock) << "Destroying dock shell for screen:" << screen->name();
        m_shells.erase(it);
    }
}

void MultiDockManager::destroyAllShells()
{
    if (!m_shells.empty()) {
        qCDebug(lcMultiDock) << "Destroying all" << m_shells.size() << "dock shells";
        m_shells.clear();
    }
}

void MultiDockManager::setupFollowActive()
{
    // Create shells on all screens (like AllScreens), but only the active one is visible
    const auto screens = QGuiApplication::screens();
    for (auto *screen : screens) {
        if (screen->geometry().isEmpty()) {
            continue;
        }
        createShellForScreen(screen);
    }

    // Determine initial active screen (primary screen)
    auto *initial = QGuiApplication::primaryScreen();
    m_activeScreen = initial;

    // Hide all shells except the active one
    for (const auto &[screen, shell] : m_shells) {
        setShellVisible(shell.get(), screen == m_activeScreen);
    }

    // Set up tracking based on trigger type
    auto trigger = static_cast<FollowActiveTrigger>(m_settings->followActiveTrigger());

    if (trigger == TriggerFocus || trigger == TriggerComposite) {
        // Watch TasksModel for active window changes
        connect(m_model->tasksModel(),
                &QAbstractItemModel::dataChanged,
                this,
                [this](const QModelIndex &topLeft, const QModelIndex &, const QList<int> &roles) {
                    if (roles.contains(TaskManager::AbstractTasksModel::IsActive) && topLeft.data(TaskManager::AbstractTasksModel::IsActive).toBool()) {
                        onActiveWindowChanged();
                    }
                });
    }

    if (trigger == TriggerMouse || trigger == TriggerComposite) {
        // Event-driven mouse detection: when any dock's visibility controller
        // detects hover, switch to that screen. This avoids polling QCursor::pos().
        for (const auto &[screen, shell] : m_shells) {
            connect(shell->view()->visibilityController(), &DockVisibilityController::dockVisibleChanged, this, [this, screen = screen]() {
                if (m_mode == FollowActive && screen != m_activeScreen) {
                    m_followActiveDebounce.stop();
                    connect(&m_followActiveDebounce, &QTimer::timeout, this, [this, screen]() {
                        setActiveScreen(screen);
                    });
                    m_followActiveDebounce.start();
                }
            });
        }
    }

    qCInfo(lcMultiDock) << "Follow Active mode initialized, trigger:" << trigger
                        << "active screen:" << (m_activeScreen ? m_activeScreen->name() : QStringLiteral("none"));
}

void MultiDockManager::setActiveScreen(QScreen *screen)
{
    if (!screen || screen == m_activeScreen) {
        return;
    }

    auto it = m_shells.find(screen);
    if (it == m_shells.end()) {
        return;
    }

    qCInfo(lcMultiDock) << "Active screen changing:" << (m_activeScreen ? m_activeScreen->name() : QStringLiteral("none")) << "->" << screen->name();

    // Hide old active shell
    if (m_activeScreen) {
        auto oldIt = m_shells.find(m_activeScreen);
        if (oldIt != m_shells.end()) {
            setShellVisible(oldIt->second.get(), false);
        }
    }

    // Show new active shell
    m_activeScreen = screen;
    setShellVisible(it->second.get(), true);
}

void MultiDockManager::onActiveWindowChanged()
{
    if (m_mode != FollowActive) {
        return;
    }

    // Find the active task's screen geometry
    auto *tasksModel = m_model->tasksModel();
    for (int i = 0; i < tasksModel->rowCount(); ++i) {
        auto idx = tasksModel->index(i, 0);
        if (!idx.data(TaskManager::AbstractTasksModel::IsActive).toBool()) {
            continue;
        }

        // Skip KDE internal surfaces (task switcher, etc.)
        if (idx.data(TaskManager::AbstractTasksModel::SkipTaskbar).toBool()) {
            continue;
        }

        QRect screenGeo = idx.data(TaskManager::AbstractTasksModel::ScreenGeometry).toRect();
        if (screenGeo.isEmpty()) {
            continue;
        }

        auto *targetScreen = QGuiApplication::screenAt(screenGeo.center());
        if (targetScreen && targetScreen != m_activeScreen) {
            // Debounce: schedule the switch
            m_followActiveDebounce.disconnect();
            connect(&m_followActiveDebounce, &QTimer::timeout, this, [this, targetScreen]() {
                setActiveScreen(targetScreen);
            });
            m_followActiveDebounce.start();
        }
        break;
    }
}

void MultiDockManager::setShellVisible(DockShell *shell, bool visible)
{
    if (!shell || !shell->view()) {
        return;
    }

    if (visible) {
        shell->view()->show();
        shell->view()->platform()->setExclusiveZone(-1); // Restore exclusive zone
    } else {
        shell->view()->platform()->setExclusiveZone(0); // Don't reserve space
        shell->view()->hide();
    }
}

void MultiDockManager::onScreenAdded(QScreen *screen)
{
    qCDebug(lcMultiDock) << "Screen added:" << screen->name() << "geometry:" << screen->geometry();
    if (m_mode == AllScreens || m_mode == FollowActive) {
        scheduleTopologyUpdate();
    }
}

void MultiDockManager::onScreenRemoved(QScreen *screen)
{
    qCDebug(lcMultiDock) << "Screen removed:" << screen->name();

    // Immediately destroy the shell for the removed screen to avoid dangling pointers
    destroyShellForScreen(screen);

    if (m_mode == AllScreens) {
        // No need to schedule — we already removed the shell synchronously
    } else if (m_mode == PrimaryOnly || m_mode == FollowActive) {
        // If the primary screen was removed, we need to migrate to the new primary
        scheduleTopologyUpdate();
    }
}

void MultiDockManager::onPrimaryScreenChanged(QScreen *screen)
{
    qCDebug(lcMultiDock) << "Primary screen changed to:" << (screen ? screen->name() : QStringLiteral("null"));
    if (m_mode == PrimaryOnly || m_mode == FollowActive) {
        scheduleTopologyUpdate();
    }
}

void MultiDockManager::scheduleTopologyUpdate()
{
    m_topologyDebounce.start();
}

void MultiDockManager::processTopologyUpdate()
{
    qCDebug(lcMultiDock) << "Processing topology update, mode:" << m_mode;
    applyMode();
}

} // namespace krema
