// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "platform/dockplatform.h"

#include <QObject>
#include <QTimer>

namespace TaskManager
{
class TasksModel;
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
 *  - SmartHide: dock hides when any maximized/fullscreen window exists
 */
class DockVisibilityController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool dockVisible READ isDockVisible NOTIFY dockVisibleChanged)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)

public:
    explicit DockVisibilityController(DockPlatform *platform, TaskManager::TasksModel *tasksModel, QWindow *dockWindow, QObject *parent = nullptr);
    ~DockVisibilityController() override;

    [[nodiscard]] bool isDockVisible() const;

    [[nodiscard]] int mode() const;
    void setMode(int mode);
    void setMode(DockPlatform::VisibilityMode mode);

    /// Called when the mouse enters/leaves the dock area.
    Q_INVOKABLE void setHovered(bool hovered);

    /// Called from QML when the panel geometry changes.
    /// Used to restrict the input region to the visible panel area.
    Q_INVOKABLE void setPanelRect(qreal x, qreal width);

Q_SIGNALS:
    void dockVisibleChanged();
    void modeChanged();

private:
    void evaluateVisibility();
    void setVisible(bool visible);

    /// Check if any window overlaps the dock geometry. (DodgeWindows)
    [[nodiscard]] bool hasOverlappingWindow() const;

    /// Check if any window is maximized or fullscreen. (SmartHide)
    [[nodiscard]] bool hasMaximizedOrFullscreenWindow() const;

    void connectModelSignals();

    DockPlatform *m_platform;
    TaskManager::TasksModel *m_tasksModel;
    QWindow *m_dockWindow;

    void applyInputRegion();

    DockPlatform::VisibilityMode m_mode = DockPlatform::VisibilityMode::AlwaysVisible;
    bool m_visible = true;
    bool m_hovered = false;

    // Panel geometry (reported from QML) for input region calculation
    int m_panelX = 0;
    int m_panelWidth = 0;

    // Debounce timer to avoid rapid show/hide flickering
    QTimer m_evaluateTimer;
};

} // namespace krema
