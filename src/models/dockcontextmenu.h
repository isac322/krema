// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QObject>
#include <QPointF>

class QWindow;

namespace krema
{

class DockModel;
class DockActions;
class NotificationTracker;

/**
 * Context menu for dock items.
 *
 * Separated from DockModel to isolate UI-level menu construction
 * and its associated signals (settings, about, visibility tracking).
 */
class DockContextMenu : public QObject
{
    Q_OBJECT

public:
    explicit DockContextMenu(DockModel *model, DockActions *actions, NotificationTracker *tracker, QObject *parent = nullptr);

    /**
     * Set the window the menu is transient for.
     *
     * Wayland refuses to map a popup whose surface has no parent, so without
     * this the menu is created and immediately destroyed and the user sees
     * nothing. It happens to work where Qt can infer a parent from the active
     * window, which is why this went unnoticed: a layer-shell dock does not
     * take keyboard focus.
     */
    void setParentWindow(QWindow *window);

    /// Show the native context menu for the task at @p index near @p globalPosition.
    Q_INVOKABLE void showForTask(int index, const QPointF &globalPosition);

Q_SIGNALS:
    void settingsRequested();
    void aboutRequested();
    void visibleChanged(bool visible);

private:
    DockModel *m_model;
    DockActions *m_actions;
    NotificationTracker *m_tracker;
    QWindow *m_parentWindow = nullptr;
};

} // namespace krema
