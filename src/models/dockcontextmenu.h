// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QObject>

class QMenu;

namespace krema
{

class DockModel;
class DockActions;
class NotificationTracker;

/**
 * Context menus for dock items and empty dock space.
 *
 * Separated from DockModel to isolate UI-level menu construction
 * and its associated signals (settings, about, visibility tracking).
 */
class DockContextMenu : public QObject
{
    Q_OBJECT

    /// True from just before the menu pops up until it starts hiding. QML
    /// holds the dock's hover state while open, so it must already be true
    /// when QtWayland synthesizes the popup-grab leave for the dock window.
    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)

public:
    explicit DockContextMenu(DockModel *model, DockActions *actions, NotificationTracker *tracker, QObject *parent = nullptr);

    /// Show the native context menu for the task at @p index.
    Q_INVOKABLE void showForTask(int index);

    /// Show dock-wide actions from empty dock space, including Quit Krema.
    Q_INVOKABLE void showForDock();

    [[nodiscard]] bool isOpen() const;

Q_SIGNALS:
    void settingsRequested();
    void aboutRequested();
    void openChanged();

private:
    DockModel *m_model;
    DockActions *m_actions;
    NotificationTracker *m_tracker;
    bool m_open = false;

    void setOpen(bool open);
    void popup(QMenu *menu);
};

} // namespace krema
