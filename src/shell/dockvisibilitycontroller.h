// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "platform/dockplatform.h"

#include <QObject>
#include <QTimer>

namespace TaskManager
{
class ActivityInfo;
class TasksModel;
class VirtualDesktopInfo;
class WindowTasksModel;
}

class QWindow;

namespace krema
{

/**
 * Controls dock visibility based on the selected mode.
 *
 * Monitors the task model for window state changes and decides
 * whether the dock should be shown or hidden. Exposes a single
 * `dockVisible` property for the QML slide animation.
 *
 * Modes:
 *  - AlwaysVisible: dock is always shown
 *  - AlwaysHidden: dock is hidden, shown on edge hover
 *  - DodgeWindows: dock hides when a window overlaps its geometry
 *  - SmartHide: dock hides when any window overlaps its geometry
 */
class DockVisibilityController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool dockVisible READ isDockVisible NOTIFY dockVisibleChanged)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)

public:
    explicit DockVisibilityController(DockPlatform *platform,
                                      TaskManager::TasksModel *tasksModel,
                                      TaskManager::VirtualDesktopInfo *virtualDesktopInfo,
                                      TaskManager::ActivityInfo *activityInfo,
                                      QWindow *dockWindow,
                                      QObject *parent = nullptr);
    ~DockVisibilityController() override;

    [[nodiscard]] bool isDockVisible() const;

    [[nodiscard]] int mode() const;
    void setMode(int mode);
    void setMode(DockPlatform::VisibilityMode mode);

    /// Toggle dock visibility. Used by global shortcut.
    /// In AlwaysVisible mode this is a no-op.
    Q_INVOKABLE void toggleVisibility();

    /// Called when the mouse enters/leaves the dock area.
    Q_INVOKABLE void setHovered(bool hovered);

    /// Called from QML when the panel geometry changes.
    /// Used to restrict the input region to the visible panel area.
    Q_INVOKABLE void setPanelRect(qreal x, qreal y, qreal width, qreal height);

    /// Set the delay before the dock appears when the mouse enters the trigger area.
    void setShowDelay(int ms);

    /// Set the delay before the dock hides when the mouse leaves the dock area.
    void setHideDelay(int ms);

    /// Set the zoom overflow height so the hovered input region excludes
    /// non-interactive space above the zoom area (e.g. tooltip reserve).
    void setZoomOverflowHeight(int height);

    /// Increment/decrement interaction lock (context menu, settings window open).
    /// While interacting, the dock will never hide.
    Q_INVOKABLE void setInteracting(bool interacting);

Q_SIGNALS:
    void dockVisibleChanged();
    void modeChanged();

private:
    void evaluateVisibility();
    void setVisible(bool visible);

    /// Check if any window overlaps the dock geometry. (DodgeWindows / SmartHide)
    [[nodiscard]] bool hasOverlappingWindow() const;

    /// Check if any window is maximized or fullscreen.
    [[nodiscard]] bool hasMaximizedOrFullscreenWindow() const;

    void connectModelSignals();

    DockPlatform *m_platform;
    TaskManager::TasksModel *m_tasksModel;
    TaskManager::WindowTasksModel *m_windowModel = nullptr;
    TaskManager::VirtualDesktopInfo *m_virtualDesktopInfo = nullptr;
    TaskManager::ActivityInfo *m_activityInfo = nullptr;
    QWindow *m_dockWindow;

    void applyInputRegion();

    /// Calculate the dock panel rect in screen coordinates.
    /// Layer-shell surfaces don't report screen position via QWindow::geometry(),
    /// so we compute it from screen geometry + edge + panel position.
    [[nodiscard]] QRect dockScreenRect() const;

    DockPlatform::VisibilityMode m_mode = DockPlatform::VisibilityMode::AlwaysVisible;
    bool m_visible = true;
    bool m_hovered = false;

    // Panel geometry (reported from QML) for input region calculation
    int m_panelX = 0;
    int m_panelY = 0;
    int m_panelWidth = 0;
    int m_panelHeight = 0;

    // Panel Y coordinate when the dock is visible (for overlap detection).
    // This value persists even while m_panelY moves off-screen during hide animation.
    int m_panelRefY = 0;

    // Zoom overflow height (pixels above the panel that zoomed icons occupy).
    // Used to restrict hovered input region to only the interactive area.
    int m_zoomOverflowHeight = 0;

    // Interaction lock: dock stays visible while context menu / settings window is open
    int m_interactingCount = 0;

    // Show timer: fires after mouse dwells in trigger area for showDelay ms
    QTimer m_showTimer;

    // Hide timer: fires after mouse leaves dock area for hideDelay ms
    QTimer m_hideTimer;

    // Debounce timer for window state changes (fixed 300ms)
    QTimer m_evaluateTimer;
};

} // namespace krema
