// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "platform/dockplatform.h"

#include <QObject>
#include <QTimer>

class QAbstractItemModel;

namespace TaskManager
{
class ActivityInfo;
class TasksModel;
class VirtualDesktopInfo;
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
 *  - AutoHide: dock is hidden, shown on edge hover
 *  - DodgeWindows: dock hides when a window overlaps its geometry
 */
class DockVisibilityController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool dockVisible READ isDockVisible NOTIFY dockVisibleChanged)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool interacting READ isInteracting NOTIFY interactingChanged)
    Q_PROPERTY(bool liveEditMode READ liveEditMode WRITE setLiveEditMode NOTIFY liveEditModeChanged)
    Q_PROPERTY(QRect panelRect READ panelRect NOTIFY panelRectChanged)
    Q_PROPERTY(bool hovered READ isHovered NOTIFY hoveredChanged)

public:
    void updateRegionGeometry();
    [[nodiscard]] bool liveEditMode() const;
    explicit DockVisibilityController(DockPlatform *platform,
                                      QAbstractItemModel *tasksModel,
                                      TaskManager::VirtualDesktopInfo *virtualDesktopInfo,
                                      TaskManager::ActivityInfo *activityInfo,
                                      QWindow *dockWindow,
                                      QObject *parent = nullptr);
    ~DockVisibilityController() override;

    [[nodiscard]] bool isDockVisible() const;
    bool isInteracting() const;
    bool isHovered() const;

    [[nodiscard]] bool isLiveEditMode() const;
    void setLiveEditMode(bool edit);

    // Q_INVOKABLE means we allow the QML file to talk to this specific function
    Q_INVOKABLE void setSettingsRect(qreal x, qreal y, qreal width, qreal height);

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
    // Change line 95 to this:
    Q_INVOKABLE void setZoomOverflowHeight(int height);

    /// Increment/decrement interaction lock (context menu, settings window open).
    /// While interacting, the dock will never hide.
    Q_INVOKABLE void setInteracting(bool interacting);

    /// Set keyboard navigation active state. Prevents dock from hiding.
    Q_INVOKABLE void setKeyboardActive(bool active);

    /// Current panel rectangle (surface-local coordinates).
    [[nodiscard]] QRect panelRect() const;

    /// Request a deferred visibility re-evaluation (e.g. after screen change).
    void requestEvaluate();

    /// Set whether DodgeWindows mode only dodges the active window.
    void setDodgeActiveOnly(bool activeOnly);
    void setReserveSpace(bool reserve);
    void setReserveMode(int mode);
    void setFloatingPadding(int padding);

    /// Set unzoomed content dimensions (the icon envelope) for reserve space calculation.
    Q_INVOKABLE void setContentDimensions(qreal width, qreal height);

Q_SIGNALS:
    void dockVisibleChanged();
    void modeChanged();
    void panelRectChanged();
    void interactingChanged();
    void liveEditModeChanged();
    void hoveredChanged();

private:
    // These are the "Storage Boxes" for the settings dimensions
    int m_settingsX = 0;
    int m_settingsY = 0;
    int m_settingsWidth = 0;
    int m_settingsHeight = 0;

    void evaluateVisibility();
    void setVisible(bool visible);

    /// Check if any window overlaps the dock geometry.
    /// @param activeOnly If true, only checks if an active window overlaps.
    [[nodiscard]] bool hasOverlappingWindow(bool activeOnly = false) const;

    void connectModelSignals();

    DockPlatform *m_platform;
    QAbstractItemModel *m_tasksModel;
    TaskManager::TasksModel *m_overlapModel = nullptr; // Note: Overlap model currently still relies on KDE TasksModel for geometry filtering.
    TaskManager::VirtualDesktopInfo *m_virtualDesktopInfo = nullptr;
    TaskManager::ActivityInfo *m_activityInfo = nullptr;
    QWindow *m_dockWindow;

    void applyInputRegion();

    /// Calculate the dock panel rect in screen coordinates.
    /// Layer-shell surfaces don't report screen position via QWindow::geometry(),
    /// so we compute it from screen geometry + edge + panel position.
    [[nodiscard]] QRect dockScreenRect() const;

    QRect m_dockScreenRect;
    bool m_liveEditMode = false;

    DockPlatform::VisibilityMode m_mode = DockPlatform::VisibilityMode::AlwaysVisible;
    bool m_visible = true;
    bool m_hovered = false;

    // Panel geometry (reported from QML) for input region calculation
    int m_panelX = 0;
    int m_panelY = 0;
    int m_panelWidth = 0;
    int m_panelHeight = 0;

    // --- Interaction: m_zoomOverflowHeight (Extra interaction "catch zone" above icons) ---
    int m_zoomOverflowHeight = 0;

    // Panel coordinates when the dock is visible (for overlap detection).
    // These values persist even while m_panelY moves off-screen during hide animation.
    int m_panelRefY = 0;
    int m_panelRefX = 0;

    // Interaction lock: dock stays visible while context menu / settings window is open
    int m_interactingCount = 0;

    // Keyboard navigation active: dock stays visible while keyboard-navigating
    bool m_keyboardActive = false;

    // DodgeWindows sub-option: true = dodge active window only, false = dodge all
    bool m_dodgeActiveOnly = false;
    bool m_reserveSpace = true;
    int m_reserveMode = 1; // 0 = Panel, 1 = Icons
    int m_floatingPadding = 0;

    // Unzoomed content dimensions (the icon envelope) for reserve space
    int m_contentWidth = 0;
    int m_contentHeight = 0;

    // Show timer: fires after mouse dwells in trigger area for showDelay ms
    QTimer m_showTimer;

    // Hide timer: fires after mouse leaves dock area for hideDelay ms
    QTimer m_hideTimer;

    // Debounce timer for window state changes (fixed 300ms)
    QTimer m_evaluateTimer;
};

} // namespace krema
