// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QList>
#include <QStringList>
#include <QUrl>
#include <QVariant>

namespace krema
{

/**
 * Abstract interface for the tasks data source.
 *
 * Decouples DockModel and DockActions from the concrete
 * TaskManager::TasksModel so that tests can inject a mock
 * without requiring a Wayland compositor.
 *
 * The 14 methods cover all operations DockModel and DockActions
 * need: row/child queries, role-based data access, launcher
 * management, task activation, and reordering.
 */
class ITasksSource
{
public:
    virtual ~ITasksSource() = default;

    ITasksSource() = default;
    ITasksSource(const ITasksSource &) = delete;
    ITasksSource &operator=(const ITasksSource &) = delete;
    ITasksSource(ITasksSource &&) = delete;
    ITasksSource &operator=(ITasksSource &&) = delete;

    // --- Row / child counts ---

    /// Number of top-level task rows.
    [[nodiscard]] virtual int rowCount() const = 0;

    /// Number of child windows for the grouped task at @p row.
    [[nodiscard]] virtual int childCount(int row) const = 0;

    // --- Data access ---

    /// Data for the top-level task at @p row with the given @p role.
    [[nodiscard]] virtual QVariant data(int row, int role) const = 0;

    /// Data for child window @p childRow of the task at @p row with the given @p role.
    [[nodiscard]] virtual QVariant childData(int row, int childRow, int role) const = 0;

    // --- Launcher management ---

    /// Current list of pinned launcher URLs.
    [[nodiscard]] virtual QStringList launcherList() const = 0;

    /// Add @p url to the pinned launcher list. Returns true on success.
    virtual bool requestAddLauncher(const QUrl &url) = 0;

    /// Remove @p url from the pinned launcher list. Returns true on success.
    virtual bool requestRemoveLauncher(const QUrl &url) = 0;

    // --- Task actions ---

    /// Activate (focus/raise) the top-level task at @p row.
    virtual void requestActivate(int row) = 0;

    /// Activate child window @p childRow of the grouped task at @p row.
    virtual void requestActivateChild(int row, int childRow) = 0;

    /// Request a new instance of the application at @p row.
    virtual void requestNewInstance(int row) = 0;

    /// Close all windows for the task at @p row.
    virtual void requestClose(int row) = 0;

    /// Open @p urls with the application at @p row.
    virtual void requestOpenUrls(int row, const QList<QUrl> &urls) = 0;

    // --- Reordering ---

    /// Move the task at @p fromRow to @p toRow. Returns true on success.
    virtual bool move(int fromRow, int toRow) = 0;

    /// Synchronise the pinned launcher list ordering with the visual row order.
    virtual void syncLaunchers() = 0;
};

} // namespace krema
