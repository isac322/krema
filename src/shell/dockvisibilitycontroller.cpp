// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockvisibilitycontroller.h"

#include "utils/inputregion.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/activityinfo.h>
#include <taskmanager/regionfiltermode.h>
#include <taskmanager/tasksmodel.h>
#include <taskmanager/virtualdesktopinfo.h>

#include <QLoggingCategory>
#include <QScreen>
#include <QWindow>

Q_LOGGING_CATEGORY(lcVisibility, "krema.shell.visibility")

namespace krema
{

DockVisibilityController::DockVisibilityController(DockPlatform *platform,
                                                   TaskManager::TasksModel *tasksModel,
                                                   TaskManager::VirtualDesktopInfo *virtualDesktopInfo,
                                                   TaskManager::ActivityInfo *activityInfo,
                                                   QWindow *dockWindow,
                                                   QObject *parent)
    : QObject(parent)
    , m_platform(platform)
    , m_tasksModel(tasksModel)
    , m_virtualDesktopInfo(virtualDesktopInfo)
    , m_activityInfo(activityInfo)
    , m_dockWindow(dockWindow)
{
    // Region-filtered model: only windows overlapping the dock geometry pass through.
    // Uses KDE's built-in filterByRegion(Intersect) — same approach as Plasma panels.
    m_overlapModel = new TaskManager::TasksModel(this);
    m_overlapModel->classBegin();
    m_overlapModel->setGroupMode(TaskManager::TasksModel::GroupDisabled);
    m_overlapModel->setFilterByRegion(RegionFilterMode::Intersect);
    m_overlapModel->setFilterMinimized(true);
    m_overlapModel->setFilterHidden(true);
    m_overlapModel->setFilterByVirtualDesktop(true);
    m_overlapModel->setFilterByActivity(true);
    if (m_virtualDesktopInfo) {
        m_overlapModel->setVirtualDesktop(m_virtualDesktopInfo->currentDesktop());
    }
    if (m_activityInfo) {
        m_overlapModel->setActivity(m_activityInfo->currentActivity());
    }
    m_overlapModel->componentComplete();

    // Show timer: fires when mouse has dwelled in trigger area long enough
    m_showTimer.setSingleShot(true);
    m_showTimer.setInterval(200);
    connect(&m_showTimer, &QTimer::timeout, this, [this]() {
        setVisible(true);
    });

    // Hide timer: fires when mouse has been outside dock area long enough
    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(400);
    connect(&m_hideTimer, &QTimer::timeout, this, [this]() {
        evaluateVisibility();
    });

    // Debounce timer for window state changes (fixed 300ms)
    m_evaluateTimer.setSingleShot(true);
    m_evaluateTimer.setInterval(300);
    connect(&m_evaluateTimer, &QTimer::timeout, this, &DockVisibilityController::evaluateVisibility);

    // Re-evaluate when virtual desktop or activity changes
    if (m_virtualDesktopInfo) {
        connect(m_virtualDesktopInfo, &TaskManager::VirtualDesktopInfo::currentDesktopChanged, this, [this]() {
            m_overlapModel->setVirtualDesktop(m_virtualDesktopInfo->currentDesktop());
            m_evaluateTimer.start();
        });
    }
    if (m_activityInfo) {
        connect(m_activityInfo, &TaskManager::ActivityInfo::currentActivityChanged, this, [this]() {
            m_overlapModel->setActivity(m_activityInfo->currentActivity());
            m_evaluateTimer.start();
        });
    }

    connectModelSignals();
}

DockVisibilityController::~DockVisibilityController() = default;

bool DockVisibilityController::isDockVisible() const
{
    return m_visible;
}

int DockVisibilityController::mode() const
{
    return static_cast<int>(m_mode);
}

void DockVisibilityController::setMode(int mode)
{
    setMode(static_cast<DockPlatform::VisibilityMode>(mode));
}

void DockVisibilityController::setMode(DockPlatform::VisibilityMode mode)
{
    if (m_mode == mode) {
        return;
    }

    m_mode = mode;
    m_platform->setVisibilityMode(mode);

    // Stop all pending timers when switching modes
    m_showTimer.stop();
    m_hideTimer.stop();
    m_evaluateTimer.stop();

    qCDebug(lcVisibility) << "Visibility mode changed to:" << static_cast<int>(mode);

    // Immediately evaluate with new mode
    evaluateVisibility();
    Q_EMIT modeChanged();
}

void DockVisibilityController::toggleVisibility()
{
    if (m_mode == DockPlatform::VisibilityMode::AlwaysVisible) {
        return;
    }

    // Cancel any pending show/hide
    m_showTimer.stop();
    m_hideTimer.stop();

    setVisible(!m_visible);
}

void DockVisibilityController::setHovered(bool hovered)
{
    if (m_hovered == hovered) {
        return;
    }

    m_hovered = hovered;

    // Update input region: hovered → include zoom overflow, unhovered → panel only.
    // When interacting (e.g. preview open) and mouse leaves dock, skip input region
    // shrink to prevent compositor pointer-focus oscillation between surfaces.
    if (m_interactingCount == 0 || hovered) {
        applyInputRegion();
    }

    if (hovered) {
        // Mouse entered dock/trigger area
        m_hideTimer.stop(); // Cancel any pending hide

        if (m_visible) {
            // Already visible — nothing to do
        } else {
            // Dock is hidden — start show timer (dwell-based)
            m_showTimer.start();
        }
    } else {
        // Mouse left dock area
        m_showTimer.stop(); // Cancel any pending show

        // Don't start hide timer if interacting (context menu / settings window open)
        if (m_interactingCount > 0) {
            return;
        }

        // In AlwaysVisible mode, never hide
        if (m_mode == DockPlatform::VisibilityMode::AlwaysVisible) {
            return;
        }

        // Start hide timer
        m_hideTimer.start();
    }
}

void DockVisibilityController::evaluateVisibility()
{
    qCDebug(lcVisibility) << "evaluateVisibility: mode=" << static_cast<int>(m_mode) << "hovered=" << m_hovered << "interacting=" << m_interactingCount;

    // If interacting (context menu / settings open), always show
    if (m_interactingCount > 0) {
        setVisible(true);
        return;
    }

    // If keyboard navigation is active, always show
    if (m_keyboardActive) {
        setVisible(true);
        return;
    }

    // If hovered, always show
    if (m_hovered) {
        setVisible(true);
        return;
    }

    switch (m_mode) {
    case DockPlatform::VisibilityMode::AlwaysVisible:
        setVisible(true);
        break;

    case DockPlatform::VisibilityMode::AutoHide:
        setVisible(false);
        break;

    case DockPlatform::VisibilityMode::DodgeWindows:
        setVisible(!hasOverlappingWindow(m_dodgeActiveOnly));
        break;
    }
}

void DockVisibilityController::setDodgeActiveOnly(bool activeOnly)
{
    if (m_dodgeActiveOnly == activeOnly) {
        return;
    }
    m_dodgeActiveOnly = activeOnly;
    if (m_mode == DockPlatform::VisibilityMode::DodgeWindows) {
        m_evaluateTimer.start();
    }
}

void DockVisibilityController::setPanelRect(qreal x, qreal y, qreal width, qreal height)
{
    m_panelX = static_cast<int>(x);
    m_panelY = static_cast<int>(y);
    m_panelWidth = static_cast<int>(width);
    m_panelHeight = static_cast<int>(height);

    // Record the Y coordinate only while the dock is visible (for overlap detection).
    // During hide animation m_panelY moves off-screen, but hasOverlappingWindow()
    // needs to answer "would there be overlap if the dock were visible?",
    // so we must use the last visible-state Y.
    if (m_visible) {
        m_panelRefY = m_panelY;
    }

    qCDebug(lcVisibility) << "setPanelRect: y=" << m_panelY << "refY=" << m_panelRefY << "visible=" << m_visible;

    // Reapply input region with the updated panel geometry
    applyInputRegion();

    // Update overlap model's region geometry for SmartHide/DodgeWindows
    updateRegionGeometry();

    Q_EMIT panelRectChanged();
}

void DockVisibilityController::setShowDelay(int ms)
{
    m_showTimer.setInterval(ms);
    qCDebug(lcVisibility) << "Show delay set to:" << ms << "ms";
}

void DockVisibilityController::setHideDelay(int ms)
{
    m_hideTimer.setInterval(ms);
    qCDebug(lcVisibility) << "Hide delay set to:" << ms << "ms";
}

void DockVisibilityController::setZoomOverflowHeight(int height)
{
    if (m_zoomOverflowHeight == height) {
        return;
    }
    m_zoomOverflowHeight = height;
    applyInputRegion();
}

void DockVisibilityController::setKeyboardActive(bool active)
{
    if (m_keyboardActive == active) {
        return;
    }
    m_keyboardActive = active;
    qCDebug(lcVisibility) << "Keyboard navigation active:" << active;

    // Toggle layer-shell keyboard interactivity with the navigation state
    m_platform->setKeyboardInteractivity(active);

    if (active) {
        m_hideTimer.stop();
        m_evaluateTimer.stop();
        setVisible(true);
    } else {
        // When keyboard navigation ends and mouse is not hovering, start hide timer
        if (m_interactingCount == 0 && !m_hovered) {
            if (m_mode != DockPlatform::VisibilityMode::AlwaysVisible) {
                m_hideTimer.start();
            }
        }
    }
}

void DockVisibilityController::setInteracting(bool interacting)
{
    if (interacting) {
        ++m_interactingCount;
        qCDebug(lcVisibility) << "Interaction lock acquired, count:" << m_interactingCount;

        // While interacting, cancel any pending hide and ensure dock is visible
        m_hideTimer.stop();
        m_evaluateTimer.stop();
        setVisible(true);
    } else {
        m_interactingCount = qMax(0, m_interactingCount - 1);
        qCDebug(lcVisibility) << "Interaction lock released, count:" << m_interactingCount;

        // When all interactions are done and mouse is not hovering, start hide timer
        if (m_interactingCount == 0 && !m_hovered) {
            if (m_mode != DockPlatform::VisibilityMode::AlwaysVisible) {
                m_hideTimer.start();
            }
        }
    }
}

void DockVisibilityController::setVisible(bool visible)
{
    if (m_visible == visible) {
        return;
    }

    m_visible = visible;
    qCDebug(lcVisibility) << "Dock visible:" << visible;

    applyInputRegion();

    Q_EMIT dockVisibleChanged();
}

QRect DockVisibilityController::panelRect() const
{
    return {m_panelX, m_panelY, m_panelWidth, m_panelHeight};
}

void DockVisibilityController::requestEvaluate()
{
    updateRegionGeometry();
    m_evaluateTimer.start();
}

void DockVisibilityController::applyInputRegion()
{
    if (!m_dockWindow) {
        return;
    }

    InputRegionParams params;
    params.surfaceWidth = m_dockWindow->width();
    params.surfaceHeight = m_dockWindow->height();
    params.panelX = m_panelX;
    params.panelY = m_panelY;
    params.panelWidth = m_panelWidth;
    params.panelHeight = m_panelHeight;
    params.zoomOverflowHeight = m_zoomOverflowHeight;
    params.visible = m_visible;
    params.hovered = m_hovered;
    params.edge = static_cast<int>(m_platform->edge());

    m_platform->setInputRegion(computeDockInputRegion(params));
}

QRect DockVisibilityController::dockScreenRect() const
{
    if (!m_dockWindow || !m_dockWindow->screen() || m_panelWidth <= 0) {
        return {};
    }

    const QRect screenGeo = m_dockWindow->screen()->geometry();

    DockScreenRectParams params;
    params.screenX = screenGeo.x();
    params.screenY = screenGeo.y();
    params.screenWidth = screenGeo.width();
    params.screenHeight = screenGeo.height();
    params.surfaceWidth = m_dockWindow->width();
    params.surfaceHeight = m_dockWindow->height();
    params.panelX = m_panelX;
    params.panelRefY = m_panelRefY;
    params.panelWidth = m_panelWidth;
    params.panelHeight = m_panelHeight;
    params.edge = static_cast<int>(m_platform->edge());

    return computeDockScreenRect(params);
}

bool DockVisibilityController::hasOverlappingWindow(bool activeOnly) const
{
    const int count = m_overlapModel->rowCount();
    qCDebug(lcVisibility) << "hasOverlappingWindow: overlapModel rows=" << count << "activeOnly=" << activeOnly
                          << "regionGeometry=" << m_overlapModel->regionGeometry();

    if (!activeOnly) {
        return count > 0;
    }

    // DodgeWindows: only hide when an *active* window overlaps
    for (int i = 0; i < count; ++i) {
        const QModelIndex idx = m_overlapModel->index(i, 0);
        if (idx.data(TaskManager::AbstractTasksModel::IsActive).toBool()) {
            return true;
        }
    }
    return false;
}

bool DockVisibilityController::hasMaximizedOrFullscreenWindow() const
{
    const int count = m_overlapModel->rowCount();
    for (int i = 0; i < count; ++i) {
        const QModelIndex idx = m_overlapModel->index(i, 0);
        if (idx.data(TaskManager::AbstractTasksModel::IsMaximized).toBool() || idx.data(TaskManager::AbstractTasksModel::IsFullScreen).toBool()) {
            return true;
        }
    }
    return false;
}

void DockVisibilityController::connectModelSignals()
{
    // The overlap model already filters by region, virtual desktop, activity,
    // minimized, and hidden windows. Row changes mean the overlap set changed.
    connect(m_overlapModel, &QAbstractItemModel::rowsInserted, this, [this]() {
        m_evaluateTimer.start();
    });
    connect(m_overlapModel, &QAbstractItemModel::rowsRemoved, this, [this]() {
        m_evaluateTimer.start();
    });
    connect(m_overlapModel, &QAbstractItemModel::modelReset, this, [this]() {
        m_evaluateTimer.start();
    });

    // IsActive changes don't add/remove rows — needed for DodgeWindows mode
    connect(m_overlapModel, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
        static const QList<int> relevantRoles = {
            TaskManager::AbstractTasksModel::IsActive,
            TaskManager::AbstractTasksModel::IsMaximized,
            TaskManager::AbstractTasksModel::IsFullScreen,
        };

        if (roles.isEmpty()) {
            m_evaluateTimer.start();
            return;
        }

        for (int role : roles) {
            if (relevantRoles.contains(role)) {
                m_evaluateTimer.start();
                return;
            }
        }
    });
}

void DockVisibilityController::updateRegionGeometry()
{
    const QRect rect = dockScreenRect();
    if (rect == m_overlapModel->regionGeometry()) {
        return;
    }
    qCDebug(lcVisibility) << "updateRegionGeometry:" << rect;
    m_overlapModel->setRegionGeometry(rect);
}

} // namespace krema
