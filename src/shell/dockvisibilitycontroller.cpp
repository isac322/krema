// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockvisibilitycontroller.h"
#include "krema.h"
#include "utils/inputregion.h"
#include <QLoggingCategory>
#include <QQuickView>
#include <QScreen>
#include <QWindow>
#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/activityinfo.h>
#include <taskmanager/regionfiltermode.h>
#include <taskmanager/tasksmodel.h>
#include <taskmanager/virtualdesktopinfo.h>

#include "models/hyprlandtasksmodel.h"
#include "utils/debugmanager.h"
#include <QProcessEnvironment>

namespace krema
{

DockVisibilityController::DockVisibilityController(DockPlatform *platform,
                                                   QAbstractItemModel *tasksModel,
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
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString desktop = env.value(QStringLiteral("XDG_CURRENT_DESKTOP")).toLower();
    bool isHyprland = desktop.contains(QStringLiteral("hyprland")) || env.contains(QStringLiteral("HYPRLAND_INSTANCE_SIGNATURE"));

    if (!isHyprland) {
        // Initialization of the Window Overlap Model.
        // This model is the core engine for the 'Dodge Windows' feature. It performs
        // real-time intersection tests between the dock surface and all active
        // windows on the current virtual desktop and activity.
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
            connect(m_virtualDesktopInfo, &TaskManager::VirtualDesktopInfo::currentDesktopChanged, this, [this]() {
                m_overlapModel->setVirtualDesktop(m_virtualDesktopInfo->currentDesktop());
                m_evaluateTimer.start();
            });
        }
        if (m_activityInfo) {
            m_overlapModel->setActivity(m_activityInfo->currentActivity());
            connect(m_activityInfo, &TaskManager::ActivityInfo::currentActivityChanged, this, [this]() {
                m_overlapModel->setActivity(m_activityInfo->currentActivity());
                m_evaluateTimer.start();
            });
        }
        m_overlapModel->componentComplete();
    }

    m_showTimer.setSingleShot(true);
    m_showTimer.setInterval(200);
    connect(&m_showTimer, &QTimer::timeout, this, [this]() {
        qDebug() << "[DOCKVISIBILITY] Show timer EXPIRED! Calling setVisible(true)";
        setVisible(true);
    });

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(400);
    connect(&m_hideTimer, &QTimer::timeout, this, [this]() {
        qDebug() << "[DOCKVISIBILITY] Hide timer EXPIRED! Calling evaluateVisibility()";
        evaluateVisibility();
    });

    m_evaluateTimer.setSingleShot(true);
    m_evaluateTimer.setInterval(300);
    connect(&m_evaluateTimer, &QTimer::timeout, this, &DockVisibilityController::evaluateVisibility);

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

bool DockVisibilityController::isHovered() const
{
    return m_hovered;
}

void DockVisibilityController::setMode(int mode)
{
    setMode(static_cast<DockPlatform::VisibilityMode>(mode));
}

void DockVisibilityController::setMode(DockPlatform::VisibilityMode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    m_platform->setVisibilityMode(mode);
    m_showTimer.stop();
    m_hideTimer.stop();
    m_evaluateTimer.stop();
    evaluateVisibility();
    Q_EMIT modeChanged();
}

void DockVisibilityController::toggleVisibility()
{
    if (m_mode == DockPlatform::VisibilityMode::AlwaysVisible)
        return;
    m_showTimer.stop();
    m_hideTimer.stop();
    setVisible(!m_visible);
}

void DockVisibilityController::setHovered(bool hovered)
{
    qDebug() << "[DOCKVISIBILITY] setHovered:" << hovered << "current m_hovered:" << m_hovered;
    if (m_hovered == hovered)
        return;
    m_hovered = hovered;
    Q_EMIT hoveredChanged();
    if (m_interactingCount == 0 || hovered)
        applyInputRegion();
    if (hovered) {
        qDebug() << "[DOCKVISIBILITY] Starting show timer";
        m_hideTimer.stop();
        if (!m_visible)
            m_showTimer.start();
    } else {
        qDebug() << "[DOCKVISIBILITY] Stopping show timer";
        m_showTimer.stop();
        if (m_interactingCount > 0)
            return;
        if (m_mode != DockPlatform::VisibilityMode::AlwaysVisible)
            m_hideTimer.start();
    }
}

void DockVisibilityController::applyInputRegion()
{
    if (!m_dockWindow)
        return;

    // --- C++ REGION 1: THE INPUT REGION (The "Mask") ---
    // Defines where the mouse can interact with the dock.
    // This is a logical 'stencil' passed to the Wayland compositor.
    // It combines the dock panel, zoom catch-zone, and settings window.
    InputRegionParams params;
    auto *view = qobject_cast<QQuickView *>(parent());
    params.surfaceWidth = view ? view->width() : m_dockWindow->width();
    params.surfaceHeight = view ? view->height() : m_dockWindow->height();
    params.panelX = m_panelX;
    params.panelY = m_panelY;
    params.panelWidth = m_panelWidth;
    params.panelHeight = m_panelHeight;
    params.zoomOverflowHeight = m_zoomOverflowHeight;
    params.visible = m_visible;
    // FIX: Only pass hovered as true if actually hovering!
    // The previous `m_visible ? true : m_hovered` forced the Wayland input region
    // to be permanently expanded to max zoom bounds, creating an invisible click-blocking wall.
    params.hovered = m_hovered;
    params.edge = static_cast<int>(m_platform->edge());

    // FIX: Expand the input region margin to 64 to fully encompass the shadow bounding box.
    // This allows the shader's smooth alpha clamp to reach 0.0 before the region cuts it off,
    // preventing sharp blurred artifacts in Hyprland.
    params.margin = 64;

    // 1. Create the base stencil for icons
    QRegion finalHitbox = computeDockInputRegion(params);

    // 2. Add the dynamic settings window hitbox
    if (m_liveEditMode && m_settingsWidth > 0) {
        finalHitbox += QRect(m_settingsX, m_settingsY, m_settingsWidth, m_settingsHeight);
    }

    m_platform->setInputRegion(finalHitbox);

    // --- Precise Blur Dispatch (RED 2) ---
    // The blur region is strictly clipped to the visual components.
    // It excludes the 1px trigger strip and zoom padding.
    QRegion blurRegion;
    if (m_visible) {
        blurRegion += QRect(m_panelX, m_panelY, m_panelWidth, m_panelHeight);
        if (m_liveEditMode && m_settingsWidth > 0) {
            blurRegion += QRect(m_settingsX, m_settingsY, m_settingsWidth, m_settingsHeight);
        }
    }
    m_platform->setBlurRegion(blurRegion);
}

void DockVisibilityController::setSettingsRect(qreal x, qreal y, qreal width, qreal height)
{
    m_settingsX = static_cast<int>(x);
    m_settingsY = static_cast<int>(y);
    m_settingsWidth = static_cast<int>(width);
    m_settingsHeight = static_cast<int>(height);
    applyInputRegion();
}

void DockVisibilityController::setLiveEditMode(bool edit)
{
    if (m_liveEditMode == edit)
        return;
    m_liveEditMode = edit;
    updateRegionGeometry();
    Q_EMIT liveEditModeChanged();
}

void DockVisibilityController::evaluateVisibility()
{
    // FORCE VISIBILITY: If the user is interacting with the dock or using keyboard
    // navigation, we force the dock to stay visible regardless of the current mode.
    if (m_interactingCount > 0 || m_keyboardActive || m_hovered) {
        setVisible(true);
    }
    if (m_platform) {
        // EXCLUSIVE ZONE (Window Reservation):
        // Only active in 'Always Visible' mode. This informs the Wayland compositor
        // how much screen real-estate to reserve for pushing maximized windows away.
        if (m_mode == DockPlatform::VisibilityMode::AlwaysVisible && m_reserveSpace) {
            int edgeIndex = static_cast<int>(m_platform->edge());
            int thickness = 0;

            // Selects the 'Exclusive Zone' (maximized window stop point) based on user mode:
            // - Mode 0 (Panel): Windows touch the dock background.
            // - Mode 1 (Icons): Windows stop at the unzoomed icon envelope (The Panel Ceiling).
            if (m_reserveMode == 0) { // Panel Mode
                thickness = (edgeIndex == 2 || edgeIndex == 3) ? m_panelWidth : m_panelHeight;
            } else { // Icon Mode
                thickness = (edgeIndex == 2 || edgeIndex == 3) ? m_contentWidth : m_contentHeight;
            }

            if (thickness > 0) {
                m_platform->setExclusiveZone(thickness + m_floatingPadding);
            } else {
                m_platform->setExclusiveZone(0);
            }
        } else {
            m_platform->setExclusiveZone(0);
        }
    }

    // MODE TRANSITIONS:
    // If we've reached this point, there is no active user interaction.
    // We now apply the logic for the specific visibility mode.
    if (m_interactingCount > 0 || m_keyboardActive || m_hovered)
        return;
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

void DockVisibilityController::setPanelRect(qreal x, qreal y, qreal width, qreal height)
{
    m_panelX = static_cast<int>(x);
    m_panelY = static_cast<int>(y);
    m_panelWidth = static_cast<int>(width);
    m_panelHeight = static_cast<int>(height);
    if (m_visible) {
        m_panelRefX = m_panelX;
        m_panelRefY = m_panelY;
    }
    applyInputRegion();
    Q_EMIT panelRectChanged();
    updateRegionGeometry();
    m_evaluateTimer.start();
}

void DockVisibilityController::setVisible(bool visible)
{
    if (m_visible == visible)
        return;
    m_visible = visible;
    Q_EMIT dockVisibleChanged();
    updateRegionGeometry();
}

QRect DockVisibilityController::panelRect() const
{
    return {m_panelX, m_panelY, m_panelWidth, m_panelHeight};
}

void DockVisibilityController::setShowDelay(int ms)
{
    m_showTimer.setInterval(ms);
}
void DockVisibilityController::setHideDelay(int ms)
{
    m_hideTimer.setInterval(ms);
}

void DockVisibilityController::setZoomOverflowHeight(int height)
{
    if (m_zoomOverflowHeight == height)
        return;
    m_zoomOverflowHeight = height;
    applyInputRegion();
}

bool DockVisibilityController::liveEditMode() const
{
    return m_liveEditMode;
}
bool DockVisibilityController::isInteracting() const
{
    return m_interactingCount > 0;
}

void DockVisibilityController::setInteracting(bool interacting)
{
    if (interacting) {
        ++m_interactingCount;
        m_hideTimer.stop();
        m_evaluateTimer.stop();
        setVisible(true);
    } else {
        m_interactingCount = qMax(0, m_interactingCount - 1);
        if (m_interactingCount == 0 && !m_hovered && m_mode != DockPlatform::VisibilityMode::AlwaysVisible)
            m_hideTimer.start();
    }
    Q_EMIT interactingChanged();
}

void DockVisibilityController::requestEvaluate()
{
    updateRegionGeometry();
    m_evaluateTimer.start();
}

void DockVisibilityController::updateRegionGeometry()
{
    if (m_dockWindow && m_dockWindow->screen()) {
        DockScreenRectParams p;
        p.screenX = m_dockWindow->screen()->geometry().x();
        p.screenY = m_dockWindow->screen()->geometry().y();
        p.screenWidth = m_dockWindow->screen()->geometry().width();
        p.screenHeight = m_dockWindow->screen()->geometry().height();

        auto *view = qobject_cast<QQuickView *>(parent());
        p.surfaceWidth = view ? view->width() : m_dockWindow->width();
        p.surfaceHeight = view ? view->height() : m_dockWindow->height();

        p.panelX = m_panelX;
        p.panelRefY = m_panelRefY;
        p.panelWidth = m_panelWidth;
        p.panelHeight = m_panelHeight;
        p.edge = static_cast<int>(m_platform->edge());

        m_dockScreenRect = computeDockScreenRect(p);
        if (m_overlapModel) {
            m_overlapModel->setScreenGeometry(m_dockScreenRect);
        }
    }

    applyInputRegion();
}

bool DockVisibilityController::hasOverlappingWindow(bool activeOnly) const
{
    if (m_overlapModel) {
        const int count = m_overlapModel->rowCount();
        if (!activeOnly)
            return count > 0;
        for (int i = 0; i < count; ++i) {
            if (m_overlapModel->index(i, 0).data(TaskManager::AbstractTasksModel::IsActive).toBool())
                return true;
        }
        return false;
    }

    // Hyprland Fallback
    auto *hyprModel = qobject_cast<HyprlandTasksModel *>(m_tasksModel);
    if (hyprModel) {
        return hyprModel->hasOverlappingWindow(m_dockScreenRect, activeOnly);
    }

    return false;
}

void DockVisibilityController::setDodgeActiveOnly(bool activeOnly)
{
    if (m_dodgeActiveOnly == activeOnly)
        return;
    m_dodgeActiveOnly = activeOnly;
    if (m_mode == DockPlatform::VisibilityMode::DodgeWindows)
        m_evaluateTimer.start();
}

void DockVisibilityController::setReserveSpace(bool reserve)
{
    if (m_reserveSpace == reserve)
        return;
    m_reserveSpace = reserve;
    if (m_mode == DockPlatform::VisibilityMode::AlwaysVisible)
        m_evaluateTimer.start();
}

void DockVisibilityController::setReserveMode(int mode)
{
    if (m_reserveMode == mode)
        return;
    m_reserveMode = mode;
    if (m_mode == DockPlatform::VisibilityMode::AlwaysVisible)
        m_evaluateTimer.start();
}

void DockVisibilityController::setFloatingPadding(int padding)
{
    if (m_floatingPadding == padding)
        return;
    m_floatingPadding = padding;
    if (m_mode == DockPlatform::VisibilityMode::AlwaysVisible)
        m_evaluateTimer.start();
}

void DockVisibilityController::setContentDimensions(qreal width, qreal height)
{
    int w = static_cast<int>(width);
    int h = static_cast<int>(height);
    if (m_contentWidth == w && m_contentHeight == h)
        return;
    m_contentWidth = w;
    m_contentHeight = h;
    if (m_mode == DockPlatform::VisibilityMode::AlwaysVisible)
        m_evaluateTimer.start();
}

void DockVisibilityController::setKeyboardActive(bool active)
{
    if (m_keyboardActive == active)
        return;
    m_keyboardActive = active;
    m_platform->setKeyboardInteractivity(active);
    if (active) {
        m_hideTimer.stop();
        m_evaluateTimer.stop();
        setVisible(true);
    } else if (m_interactingCount == 0 && !m_hovered && m_mode != DockPlatform::VisibilityMode::AlwaysVisible)
        m_hideTimer.start();
}

void DockVisibilityController::connectModelSignals()
{
    if (m_overlapModel) {
        connect(m_overlapModel, &QAbstractItemModel::rowsInserted, this, [this]() {
            m_evaluateTimer.start();
        });
        connect(m_overlapModel, &QAbstractItemModel::rowsRemoved, this, [this]() {
            m_evaluateTimer.start();
        });
        connect(m_overlapModel, &QAbstractItemModel::modelReset, this, [this]() {
            m_evaluateTimer.start();
        });
        connect(m_overlapModel, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &, const QModelIndex &, const QList<int> &) {
            m_evaluateTimer.start();
        });
    } else if (m_tasksModel) {
        connect(m_tasksModel, &QAbstractItemModel::rowsInserted, this, [this]() {
            m_evaluateTimer.start();
        });
        connect(m_tasksModel, &QAbstractItemModel::rowsRemoved, this, [this]() {
            m_evaluateTimer.start();
        });
        connect(m_tasksModel, &QAbstractItemModel::modelReset, this, [this]() {
            m_evaluateTimer.start();
        });
        connect(m_tasksModel, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &, const QModelIndex &, const QList<int> &) {
            m_evaluateTimer.start();
        });
    }
}

} // namespace krema
