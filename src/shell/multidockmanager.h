// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "platform/dockplatform.h"

#include <QObject>
#include <QTimer>

#include <memory>
#include <unordered_map>

class KremaSettings;
class QScreen;

namespace krema
{

class DockModel;
class DockShell;
class NotificationTracker;

/**
 * Manages multiple dock instances across screens.
 *
 * Replaces the single DockShell in Application. Creates and destroys
 * DockShell instances based on the monitor mode setting:
 *   - PrimaryOnly: one dock on the primary screen (default, current behavior)
 *   - AllScreens: one dock per connected screen
 *   - FollowActive: all screens have shells, only the active one is visible
 */
class MultiDockManager : public QObject
{
    Q_OBJECT

public:
    enum MonitorMode {
        PrimaryOnly = 0,
        AllScreens = 1,
        FollowActive = 2,
    };

    explicit MultiDockManager(KremaSettings *settings, DockModel *model, NotificationTracker *tracker, QObject *parent = nullptr);
    ~MultiDockManager() override;

    /// Initialize all dock shells based on the current monitor mode.
    void initialize();

    /// Change the monitor mode at runtime (e.g. from settings).
    void setMonitorMode(MonitorMode mode);

    /// Return the "primary" shell (for global shortcuts and focus).
    [[nodiscard]] DockShell *primaryShell() const;

    /// Return the shell on the screen containing the given point, or primaryShell().
    [[nodiscard]] DockShell *shellAtCursor() const;

    /// Return all active shells.
    [[nodiscard]] QList<DockShell *> shells() const;

Q_SIGNALS:
    /// Emitted when pinned launchers change on any shell.
    void pinnedLaunchersChanged();

private:
    enum FollowActiveTrigger {
        TriggerMouse = 0,
        TriggerFocus = 1,
        TriggerComposite = 2,
    };

    void applyMode();
    void setupPrimaryOnly();
    void setupAllScreens();
    void setupFollowActive();

    DockShell *createShellForScreen(QScreen *screen);
    void destroyShellForScreen(QScreen *screen);
    void destroyAllShells();

    void onScreenAdded(QScreen *screen);
    void onScreenRemoved(QScreen *screen);
    void onPrimaryScreenChanged(QScreen *screen);

    /// Debounced handler for screen topology changes.
    void scheduleTopologyUpdate();
    void processTopologyUpdate();

    // --- Follow Active ---
    void setActiveScreen(QScreen *screen);
    void onActiveWindowChanged();
    void onMouseScreenChanged();
    void setShellVisible(DockShell *shell, bool visible);

    KremaSettings *m_settings;
    DockModel *m_model;
    NotificationTracker *m_tracker;

    MonitorMode m_mode = PrimaryOnly;
    std::unordered_map<QScreen *, std::unique_ptr<DockShell>> m_shells;
    QScreen *m_activeScreen = nullptr;

    QTimer m_topologyDebounce;
    QTimer m_followActiveDebounce;
    bool m_initialized = false;
};

} // namespace krema
