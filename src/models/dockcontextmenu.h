// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QObject>

namespace krema
{

class DockModel;
class DockActions;

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
    explicit DockContextMenu(DockModel *model, DockActions *actions, QObject *parent = nullptr);

    /// Show the native context menu for the task at @p index.
    Q_INVOKABLE void showForTask(int index);

Q_SIGNALS:
    void settingsRequested();
    void aboutRequested();
    void visibleChanged(bool visible);

private:
    DockModel *m_model;
    DockActions *m_actions;
};

} // namespace krema
