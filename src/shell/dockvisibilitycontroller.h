// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "platform/dockplatform.h"
#include "platform/kwinpanelwatcher.h"
#include "platform/kwinpointermotionwatcher.h"
#include <QRect>
#include <memory>

#include <QModelIndex>
#include <QObject>
#include <QTimer>
#include <QVariantList>

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
public:
    explicit DockVisibilityController(DockPlatform *platform,
                                      TaskManager::TasksModel *tasksModel,
                                      TaskManager::VirtualDesktopInfo *virtualDesktopInfo,
                                      TaskManager::ActivityInfo *activityInfo,
                                      QWindow *dockWindow,
                                      QObject *parent = nullptr);
    ~DockVisibilityController() override;

    [[nodiscard]] bool isDockVisible() const;
    /// Whether the dock is held shown by an interaction lock (context menu,
    /// preview, drag, settings dialog) or keyboard navigation.
    [[nodiscard]] bool isInteracting() const;

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
    /// Top-left of the panel at its visible rest position, reported by QML even
    /// while the dock is slid off-screen. Used when the dock has not been
    /// visible yet (e.g. it starts hidden behind a window under Dodge Windows).
    Q_INVOKABLE void setPanelRestPosition(qreal x, qreal y);

    /// Set the delay before the dock appears when the mouse enters the trigger area.
    void setShowDelay(int ms);

    /// Set the delay before the dock hides when the mouse leaves the dock area.
    void setHideDelay(int ms);

    /// Set the animated outward extent so the hovered input region follows
    /// zoomed and bouncing icons without including unrelated surface space.
    void setZoomOverflowHeight(int height);

    /// Depth of the band of other panels along the screen edge that the dock
    /// is drawn past (see InputRegionParams::edgeInset).
    void setEdgeInset(int inset);

    /// Where a hidden dock is revealed from when other panels occupy the
    /// screen edge: 0 = the outer screen edge (watched through KWin),
    /// 1 = the dock's resting edge, past the panels' band.
    void setRevealTriggerLocation(int location);

    /// Recompute the outer screen-edge strip watched for revealing the dock
    /// (after a screen, screen geometry or edge change).
    void updateEdgeTriggerStrip();

    /// Increment/decrement interaction lock (context menu, settings window open).
    /// While interacting, the dock will never hide.
    Q_INVOKABLE void setInteracting(bool interacting);

    /// Set keyboard navigation active state. Prevents dock from hiding.
    /// Entering remembers the active window; leaving with @p restoreFocus
    /// re-activates it, because KWin keeps a layer surface active after its
    /// keyboard interactivity is dropped. Pass false when leaving because a
    /// task was just activated (that activation already moves focus).
    Q_INVOKABLE void setKeyboardActive(bool active, bool restoreFocus = true);

    /// Set internal (reorder) drag active state. Holds the interaction lock and
    /// grabs layer-shell keyboard interactivity so Escape can cancel the drag;
    /// the release hands focus back to the previously active window.
    Q_INVOKABLE void setDragActive(bool active);

    /// Current panel rectangle (surface-local coordinates).
    [[nodiscard]] QRect panelRect() const;

    /// Request a deferred visibility re-evaluation (e.g. after screen change).
    void requestEvaluate();

    /// Set whether DodgeWindows mode only dodges the active window.
    void setDodgeActiveOnly(bool activeOnly);

    /// Whether the KWin screen-edge trigger is active for this dock.
    [[nodiscard]] bool isExternalEdgeTriggerActive() const;

Q_SIGNALS:
    void dockVisibleChanged();
    void modeChanged();
    void panelRectChanged();
    /// The pointer moved (anywhere on screen) during keyboard navigation.
    void pointerMovedDuringKeyboardNavigation();

private:
    void evaluateVisibility();
    void setVisible(bool visible);

    /// Check if any window overlaps the dock geometry.
    /// @param activeOnly If true, only checks if an active window overlaps.
    [[nodiscard]] bool hasOverlappingWindow(bool activeOnly = false) const;

    /// Check if any window is maximized or fullscreen.
    [[nodiscard]] bool hasMaximizedOrFullscreenWindow() const;

    void connectModelSignals();

    /// Re-activate the window that was active when keyboard interactivity
    /// (keyboard navigation or internal drag) was first acquired.
    void restoreReturnTask();

    /// Task (top-level or grouped child) whose window ids equal @p windowIds.
    [[nodiscard]] QModelIndex findTask(const QVariantList &windowIds) const;

    /// Apply layer-shell keyboard interactivity: exclusive while keyboard
    /// navigating or dragging, none otherwise. Remembers the active window
    /// on acquisition and hands focus back on full release.
    void applyKeyboardInteractivity();

    DockPlatform *m_platform;
    TaskManager::TasksModel *m_tasksModel;
    TaskManager::TasksModel *m_overlapModel = nullptr;
    TaskManager::VirtualDesktopInfo *m_virtualDesktopInfo = nullptr;
    TaskManager::ActivityInfo *m_activityInfo = nullptr;
    QWindow *m_dockWindow;

    /// Combine surface and screen-edge hover into m_hovered and drive the
    /// show/hide timers. @p transitFromEdge: the pointer just left the screen
    /// edge and may be crossing the panels' band toward the dock.
    void updateHoveredState(bool transitFromEdge);

    /// Arm the screen-edge watcher when the reveal trigger must sit on the
    /// outer screen edge, and refresh the input region accordingly.
    void updateEdgeTriggerWatcher();
    void checkCursorAtEdge(const QPoint &pos);
    void applyInputRegion();
    void updateRegionGeometry();

    /// Calculate the dock panel rect in screen coordinates.
    /// Layer-shell surfaces don't report screen position via QWindow::geometry(),
    /// so we compute it from screen geometry + edge + panel position.
    [[nodiscard]] QRect dockScreenRect() const;

    DockPlatform::VisibilityMode m_mode = DockPlatform::VisibilityMode::AlwaysVisible;
    bool m_visible = true;
    bool m_hovered = false; // m_surfaceHovered || m_edgeHovered
    bool m_surfaceHovered = false; // pointer on the dock surface (from QML)
    bool m_edgeHovered = false; // pointer on the outer screen edge (from KWin)

    // Panel geometry (reported from QML) for input region calculation
    int m_panelX = 0;
    int m_panelY = 0;
    int m_panelWidth = 0;
    int m_panelHeight = 0;

    // Panel Y coordinate when the dock is visible (for overlap detection).
    // This value persists even while m_panelY moves off-screen during hide animation.
    int m_panelRefY = 0;
    // Same for X on the Left/Right edges, where the dock hides sideways.
    int m_panelRefX = 0;
    // True once m_panelRef* hold a position recorded while the dock was visible.
    bool m_haveVisibleRef = false;

    // Animated outward extent (pixels beyond the panel that transformed icons
    // occupy). Used to restrict hovered input to the interactive area.
    int m_zoomOverflowHeight = 0;

    // Band of other panels next to the screen edge (kept click-through).
    int m_edgeInset = 0;

    // Reveal trigger location (0 = screen edge, 1 = dock position)
    int m_revealTriggerLocation = 0;

    // Configured hide delay; the hide timer may run longer while the pointer
    // crosses the panels' band from the screen edge.
    int m_hideDelay = 400;
    // Hold while the pointer crosses the panels' band from the screen edge
    bool m_crossingBand = false;

    // Shared KWin script when watching the outer screen edge past panels
    std::shared_ptr<KWinPanelWatcher> m_edgeWatcher;
    QRect m_edgeStripRect;
    bool m_watchingEdge = false;

    // Interaction lock: dock stays visible while context menu / settings window is open
    int m_interactingCount = 0;

    // Keyboard navigation active: dock stays visible while keyboard-navigating
    bool m_keyboardActive = false;

    // Window ids of the task that was active when keyboard interactivity was
    // acquired (focus returns there)
    QVariantList m_returnWindowIds;
    // Reports pointer motion outside the dock surface while keyboard-navigating
    KWinPointerMotionWatcher m_pointerMotionWatcher;

    // Internal reorder drag active: dock holds keyboard focus so Escape reaches it
    bool m_dragActive = false;

    // Layer-shell keyboard interactivity currently requested from the platform
    bool m_interactivityActive = false;

    // DodgeWindows sub-option: true = dodge active window only, false = dodge all
    bool m_dodgeActiveOnly = false;

    // Show timer: fires after mouse dwells in trigger area for showDelay ms
    QTimer m_showTimer;

    // Hide timer: fires after mouse leaves dock area for hideDelay ms
    QTimer m_hideTimer;

    // Debounce timer for window state changes (fixed 300ms)
    QTimer m_evaluateTimer;
};

} // namespace krema
