// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "models/itaskssource.h"

#include <QList>
#include <QMap>
#include <QStringList>
#include <QUrl>
#include <QVariant>

namespace krema::testing
{

/**
 * In-memory ITasksSource for unit tests.
 *
 * Supports addTask() / removeTask() for building up test data without
 * a live Wayland compositor, and records every mutation call so tests
 * can assert that DockModel/DockActions invoke the correct operations.
 *
 * Usage example:
 * @code
 *   MockTasksSource mock;
 *
 *   // Build a window task
 *   QMap<int, QVariant> task;
 *   task[TaskManager::AbstractTasksModel::IsWindow] = true;
 *   task[TaskManager::AbstractTasksModel::IsActive] = false;
 *   mock.addTask(task);
 *
 *   // Exercise the system under test
 *   dockActions.activate(0);
 *
 *   // Assert the right operation was recorded
 *   QCOMPARE(mock.activateCalls.size(), 1);
 *   QCOMPARE(mock.activateCalls.first().row, 0);
 * @endcode
 */
class MockTasksSource : public ITasksSource
{
public:
    // -----------------------------------------------------------------------
    // Recorded call structs (public for direct test assertion access)
    // -----------------------------------------------------------------------

    struct ActivateCall {
        int row = -1;
    };

    struct ActivateChildCall {
        int row = -1;
        int childRow = -1;
    };

    struct NewInstanceCall {
        int row = -1;
    };

    struct CloseCall {
        int row = -1;
    };

    struct OpenUrlsCall {
        int row = -1;
        QList<QUrl> urls;
    };

    struct MoveCall {
        int fromRow = -1;
        int toRow = -1;
    };

    // -----------------------------------------------------------------------
    // Test-setup API
    // -----------------------------------------------------------------------

    /**
     * Append a new top-level task populated with @p roleData.
     *
     * Keys in @p roleData are Qt item-model role integers; values are the
     * corresponding QVariant returned by data().
     */
    void addTask(const QMap<int, QVariant> &roleData)
    {
        m_tasks.append(TaskEntry{.roleData = roleData, .children = {}});
    }

    /**
     * Append a child window entry to the task at @p parentRow.
     *
     * The child is accessible via childData(parentRow, childIndex, role).
     */
    void addChildTask(int parentRow, const QMap<int, QVariant> &roleData)
    {
        if (parentRow >= 0 && parentRow < static_cast<int>(m_tasks.size())) {
            m_tasks[parentRow].children.append(roleData);
        }
    }

    /**
     * Remove the top-level task at @p row (0-based).
     * Does nothing if @p row is out of range.
     */
    void removeTask(int row)
    {
        if (row >= 0 && row < static_cast<int>(m_tasks.size())) {
            m_tasks.remove(row);
        }
    }

    /**
     * Reset all tasks and clear all recorded call history.
     */
    void reset()
    {
        m_tasks.clear();
        m_launcherList.clear();
        activateCalls.clear();
        activateChildCalls.clear();
        newInstanceCalls.clear();
        closeCalls.clear();
        addLauncherCalls.clear();
        removeLauncherCalls.clear();
        openUrlsCalls.clear();
        moveCalls.clear();
        syncLaunchersCallCount = 0;
    }

    // -----------------------------------------------------------------------
    // ITasksSource — query methods
    // -----------------------------------------------------------------------

    [[nodiscard]] int rowCount() const override
    {
        return static_cast<int>(m_tasks.size());
    }

    [[nodiscard]] int childCount(int row) const override
    {
        if (row < 0 || row >= static_cast<int>(m_tasks.size())) {
            return 0;
        }
        return static_cast<int>(m_tasks[row].children.size());
    }

    [[nodiscard]] QVariant data(int row, int role) const override
    {
        if (row < 0 || row >= static_cast<int>(m_tasks.size())) {
            return {};
        }
        return m_tasks[row].roleData.value(role);
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    [[nodiscard]] QVariant childData(int row, int childRow, int role) const override
    {
        if (row < 0 || row >= static_cast<int>(m_tasks.size())) {
            return {};
        }
        const auto &children = m_tasks[row].children;
        if (childRow < 0 || childRow >= static_cast<int>(children.size())) {
            return {};
        }
        return children[childRow].value(role);
    }

    // -----------------------------------------------------------------------
    // ITasksSource — launcher management
    // -----------------------------------------------------------------------

    [[nodiscard]] QStringList launcherList() const override
    {
        return m_launcherList;
    }

    bool requestAddLauncher(const QUrl &url) override
    {
        addLauncherCalls.append(url);
        const QString urlStr = url.toString();
        if (!m_launcherList.contains(urlStr)) {
            m_launcherList.append(urlStr);
            return true;
        }
        return false;
    }

    bool requestRemoveLauncher(const QUrl &url) override
    {
        removeLauncherCalls.append(url);
        const QString urlStr = url.toString();
        if (m_launcherList.contains(urlStr)) {
            m_launcherList.removeAll(urlStr);
            return true;
        }
        return false;
    }

    // -----------------------------------------------------------------------
    // ITasksSource — task actions (recorded for test assertions)
    // -----------------------------------------------------------------------

    void requestActivate(int row) override
    {
        activateCalls.append(ActivateCall{.row = row});
    }

    void requestActivateChild(int row, int childRow) override
    {
        activateChildCalls.append(ActivateChildCall{.row = row, .childRow = childRow});
    }

    void requestNewInstance(int row) override
    {
        newInstanceCalls.append(NewInstanceCall{.row = row});
    }

    void requestClose(int row) override
    {
        closeCalls.append(CloseCall{.row = row});
    }

    void requestOpenUrls(int row, const QList<QUrl> &urls) override
    {
        openUrlsCalls.append(OpenUrlsCall{.row = row, .urls = urls});
    }

    // -----------------------------------------------------------------------
    // ITasksSource — reordering
    // -----------------------------------------------------------------------

    bool move(int fromRow, int toRow) override
    {
        moveCalls.append(MoveCall{.fromRow = fromRow, .toRow = toRow});
        if (fromRow == toRow) {
            return false;
        }
        if (fromRow < 0 || fromRow >= static_cast<int>(m_tasks.size())) {
            return false;
        }
        if (toRow < 0 || toRow >= static_cast<int>(m_tasks.size())) {
            return false;
        }
        m_tasks.move(fromRow, toRow);
        return true;
    }

    void syncLaunchers() override
    {
        ++syncLaunchersCallCount;
    }

    // -----------------------------------------------------------------------
    // Recorded call history (public for test assertions)
    // -----------------------------------------------------------------------

    QList<ActivateCall> activateCalls;
    QList<ActivateChildCall> activateChildCalls;
    QList<NewInstanceCall> newInstanceCalls;
    QList<CloseCall> closeCalls;
    QList<QUrl> addLauncherCalls;
    QList<QUrl> removeLauncherCalls;
    QList<OpenUrlsCall> openUrlsCalls;
    QList<MoveCall> moveCalls;
    int syncLaunchersCallCount = 0;

private:
    struct TaskEntry {
        QMap<int, QVariant> roleData;
        QList<QMap<int, QVariant>> children;
    };

    QList<TaskEntry> m_tasks;
    QStringList m_launcherList;
};

} // namespace krema::testing
