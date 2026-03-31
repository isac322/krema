// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

/**
 * Unit tests for MockTasksSource.
 *
 * Verifies that MockTasksSource correctly records ITasksSource mutation calls
 * for the operations invoked by DockActions::activate, cycleWindows, and
 * closeTask, enabling future mock-based DockActions tests.
 *
 * Each test simulates the exact call sequence a refactored DockActions would
 * make against ITasksSource, then asserts that MockTasksSource recorded the
 * arguments faithfully.
 */

#include "mocktaskssource.h"

#include <taskmanager/abstracttasksmodel.h>

#include <QTest>

using namespace krema::testing;

class TestMockTasksSource : public QObject
{
    Q_OBJECT

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    /** Build a minimal window-task role map. */
    static QMap<int, QVariant> windowTask(bool active = false)
    {
        QMap<int, QVariant> d;
        d[TaskManager::AbstractTasksModel::IsWindow] = true;
        d[TaskManager::AbstractTasksModel::IsLauncher] = false;
        d[TaskManager::AbstractTasksModel::IsActive] = active;
        return d;
    }

    /** Build a minimal launcher-task role map. */
    static QMap<int, QVariant> launcherTask()
    {
        QMap<int, QVariant> d;
        d[TaskManager::AbstractTasksModel::IsWindow] = false;
        d[TaskManager::AbstractTasksModel::IsLauncher] = true;
        d[TaskManager::AbstractTasksModel::IsActive] = false;
        return d;
    }

    /** Build a minimal child-window role map. */
    static QMap<int, QVariant> childWindow(bool active = false)
    {
        QMap<int, QVariant> d;
        d[TaskManager::AbstractTasksModel::IsActive] = active;
        return d;
    }

private Q_SLOTS:

    // -----------------------------------------------------------------------
    // DockActions::activate — window task → requestActivate
    // -----------------------------------------------------------------------

    /** activate(index) on a window task must call requestActivate(row). */
    void activate_windowTask_recordsActivate()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());

        // Simulate what DockActions::activate(0) will do once refactored:
        //   if (isWindow) source->requestActivate(row);
        const bool isWindow = mock.data(0, TaskManager::AbstractTasksModel::IsWindow).toBool();
        QVERIFY(isWindow);

        mock.requestActivate(0);

        QCOMPARE(mock.activateCalls.size(), 1);
        QCOMPARE(mock.activateCalls.first().row, 0);
        // No spurious calls to other record lists
        QCOMPARE(mock.newInstanceCalls.size(), 0);
        QCOMPARE(mock.closeCalls.size(), 0);
    }

    /** activate(index) on a second task records the correct row. */
    void activate_secondRow_recordsCorrectRow()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());
        mock.addTask(windowTask());

        mock.requestActivate(1);

        QCOMPARE(mock.activateCalls.size(), 1);
        QCOMPARE(mock.activateCalls.first().row, 1);
    }

    /** Multiple activate calls are all recorded in order. */
    void activate_multipleCalls_allRecorded()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());
        mock.addTask(windowTask());

        mock.requestActivate(0);
        mock.requestActivate(1);
        mock.requestActivate(0);

        QCOMPARE(mock.activateCalls.size(), 3);
        QCOMPARE(mock.activateCalls[0].row, 0);
        QCOMPARE(mock.activateCalls[1].row, 1);
        QCOMPARE(mock.activateCalls[2].row, 0);
    }

    // -----------------------------------------------------------------------
    // DockActions::activate — launcher task → requestNewInstance
    // -----------------------------------------------------------------------

    /** activate(index) on a launcher must call requestNewInstance(row). */
    void activate_launcherTask_recordsNewInstance()
    {
        MockTasksSource mock;
        mock.addTask(launcherTask());

        // Simulate what DockActions::activate(0) will do for launchers:
        //   else if (isLauncher) source->requestNewInstance(row);
        const bool isLauncher = mock.data(0, TaskManager::AbstractTasksModel::IsLauncher).toBool();
        QVERIFY(isLauncher);

        mock.requestNewInstance(0);

        QCOMPARE(mock.newInstanceCalls.size(), 1);
        QCOMPARE(mock.newInstanceCalls.first().row, 0);
        // No spurious activations
        QCOMPARE(mock.activateCalls.size(), 0);
    }

    // -----------------------------------------------------------------------
    // DockActions::closeTask → requestClose
    // -----------------------------------------------------------------------

    /** closeTask(index) must call requestClose(row). */
    void closeTask_recordsClose()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());

        mock.requestClose(0);

        QCOMPARE(mock.closeCalls.size(), 1);
        QCOMPARE(mock.closeCalls.first().row, 0);
        QCOMPARE(mock.activateCalls.size(), 0);
    }

    /** closeTask on multiple rows records each row correctly. */
    void closeTask_multipleRows_allRecorded()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());
        mock.addTask(windowTask());
        mock.addTask(windowTask());

        mock.requestClose(2);
        mock.requestClose(0);

        QCOMPARE(mock.closeCalls.size(), 2);
        QCOMPARE(mock.closeCalls[0].row, 2);
        QCOMPARE(mock.closeCalls[1].row, 0);
    }

    // -----------------------------------------------------------------------
    // DockActions::cycleWindows — single-window task → requestActivate
    // -----------------------------------------------------------------------

    /**
     * cycleWindows on a task with no grouped children just activates the
     * top-level task via requestActivate(row).
     */
    void cycleWindows_singleWindow_requestsActivate()
    {
        MockTasksSource mock;
        mock.addTask(windowTask(/*active=*/false));
        // No children added → childCount(0) == 0

        // Simulate DockActions::cycleWindows(0, true) for single-window case:
        //   if (childCount <= 1) source->requestActivate(row); return;
        const int count = mock.childCount(0);
        QCOMPARE(count, 0); // no children

        mock.requestActivate(0);

        QCOMPARE(mock.activateCalls.size(), 1);
        QCOMPARE(mock.activateCalls.first().row, 0);
        QCOMPARE(mock.activateChildCalls.size(), 0);
    }

    // -----------------------------------------------------------------------
    // DockActions::cycleWindows — grouped task → requestActivateChild
    // -----------------------------------------------------------------------

    /**
     * cycleWindows(row, forward=true) on a grouped task with an active child
     * must call requestActivateChild(row, (activeIndex + 1) % count).
     */
    void cycleWindows_groupedForward_requestsActivateChild()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());
        // Three child windows; child 1 is active
        mock.addChildTask(0, childWindow(/*active=*/false)); // child 0
        mock.addChildTask(0, childWindow(/*active=*/true)); // child 1 — active
        mock.addChildTask(0, childWindow(/*active=*/false)); // child 2

        // Simulate DockActions::cycleWindows(0, forward=true):
        const int count = mock.childCount(0);
        QCOMPARE(count, 3);

        int activeChild = -1;
        for (int i = 0; i < count; ++i) {
            if (mock.childData(0, i, TaskManager::AbstractTasksModel::IsActive).toBool()) {
                activeChild = i;
                break;
            }
        }
        QCOMPARE(activeChild, 1); // child 1 is active

        const int target = (activeChild + 1) % count; // forward → child 2
        QCOMPARE(target, 2);

        mock.requestActivateChild(0, target);

        QCOMPARE(mock.activateChildCalls.size(), 1);
        QCOMPARE(mock.activateChildCalls.first().row, 0);
        QCOMPARE(mock.activateChildCalls.first().childRow, 2);
        QCOMPARE(mock.activateCalls.size(), 0);
    }

    /**
     * cycleWindows(row, forward=false) must step backward, wrapping around
     * from child 0 to the last child.
     */
    void cycleWindows_groupedBackward_wrapsAround()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());
        mock.addChildTask(0, childWindow(/*active=*/true)); // child 0 — active
        mock.addChildTask(0, childWindow(/*active=*/false)); // child 1
        mock.addChildTask(0, childWindow(/*active=*/false)); // child 2

        const int count = mock.childCount(0);
        QCOMPARE(count, 3);

        int activeChild = -1;
        for (int i = 0; i < count; ++i) {
            if (mock.childData(0, i, TaskManager::AbstractTasksModel::IsActive).toBool()) {
                activeChild = i;
                break;
            }
        }
        QCOMPARE(activeChild, 0);

        // backward: (0 - 1 + 3) % 3 == 2
        const int target = (activeChild - 1 + count) % count;
        QCOMPARE(target, 2);

        mock.requestActivateChild(0, target);

        QCOMPARE(mock.activateChildCalls.size(), 1);
        QCOMPARE(mock.activateChildCalls.first().row, 0);
        QCOMPARE(mock.activateChildCalls.first().childRow, 2);
    }

    /**
     * cycleWindows when no child is active starts from child 0.
     */
    void cycleWindows_noActiveChild_startsFromZero()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());
        mock.addChildTask(0, childWindow(/*active=*/false));
        mock.addChildTask(0, childWindow(/*active=*/false));

        const int count = mock.childCount(0);

        // Find active child — none active → activeChild stays -1
        int activeChild = -1;
        for (int i = 0; i < count; ++i) {
            if (mock.childData(0, i, TaskManager::AbstractTasksModel::IsActive).toBool()) {
                activeChild = i;
                break;
            }
        }
        QCOMPARE(activeChild, -1);

        // DockActions::cycleWindows: if (activeChild < 0) target = 0;
        const int target = 0;
        mock.requestActivateChild(0, target);

        QCOMPARE(mock.activateChildCalls.size(), 1);
        QCOMPARE(mock.activateChildCalls.first().childRow, 0);
    }

    /**
     * cycleWindows forward wraps from last child back to first.
     */
    void cycleWindows_forwardWrap_cyclesToFirstChild()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());
        mock.addChildTask(0, childWindow(/*active=*/false)); // child 0
        mock.addChildTask(0, childWindow(/*active=*/false)); // child 1
        mock.addChildTask(0, childWindow(/*active=*/true)); // child 2 — active (last)

        const int count = mock.childCount(0);
        QCOMPARE(count, 3);

        int activeChild = -1;
        for (int i = 0; i < count; ++i) {
            if (mock.childData(0, i, TaskManager::AbstractTasksModel::IsActive).toBool()) {
                activeChild = i;
                break;
            }
        }
        QCOMPARE(activeChild, 2);

        // forward: (2 + 1) % 3 == 0
        const int target = (activeChild + 1) % count;
        QCOMPARE(target, 0);

        mock.requestActivateChild(0, target);

        QCOMPARE(mock.activateChildCalls.first().childRow, 0);
    }

    // -----------------------------------------------------------------------
    // reset() — clears all recorded calls
    // -----------------------------------------------------------------------

    /** reset() must clear all call recording lists. */
    void reset_clearsAllRecordedCalls()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());
        mock.addTask(launcherTask());
        mock.addChildTask(0, childWindow(/*active=*/true));
        mock.addChildTask(0, childWindow(/*active=*/false));

        // Generate calls
        mock.requestActivate(0);
        mock.requestActivateChild(0, 1);
        mock.requestNewInstance(1);
        mock.requestClose(0);
        mock.requestAddLauncher(QUrl(QStringLiteral("applications:org.kde.dolphin.desktop")));
        mock.requestRemoveLauncher(QUrl(QStringLiteral("applications:org.kde.dolphin.desktop")));
        mock.move(0, 1);
        mock.syncLaunchers();

        // Sanity-check: all non-zero before reset
        QVERIFY(mock.activateCalls.size() > 0);
        QVERIFY(mock.activateChildCalls.size() > 0);
        QVERIFY(mock.newInstanceCalls.size() > 0);
        QVERIFY(mock.closeCalls.size() > 0);
        QVERIFY(mock.addLauncherCalls.size() > 0);
        QVERIFY(mock.removeLauncherCalls.size() > 0);
        QVERIFY(mock.moveCalls.size() > 0);
        QVERIFY(mock.syncLaunchersCallCount > 0);

        mock.reset();

        QCOMPARE(mock.activateCalls.size(), 0);
        QCOMPARE(mock.activateChildCalls.size(), 0);
        QCOMPARE(mock.newInstanceCalls.size(), 0);
        QCOMPARE(mock.closeCalls.size(), 0);
        QCOMPARE(mock.addLauncherCalls.size(), 0);
        QCOMPARE(mock.removeLauncherCalls.size(), 0);
        QCOMPARE(mock.moveCalls.size(), 0);
        QCOMPARE(mock.syncLaunchersCallCount, 0);

        // Tasks are also cleared
        QCOMPARE(mock.rowCount(), 0);
    }

    // -----------------------------------------------------------------------
    // Data query correctness (used by cycleWindows to inspect task state)
    // -----------------------------------------------------------------------

    /** childCount returns 0 for a task with no children. */
    void childCount_noChildren_returnsZero()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());
        QCOMPARE(mock.childCount(0), 0);
    }

    /** childCount returns the correct number after addChildTask. */
    void childCount_withChildren_returnsCorrectCount()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());
        mock.addChildTask(0, childWindow());
        mock.addChildTask(0, childWindow());
        QCOMPARE(mock.childCount(0), 2);
    }

    /** childData returns the IsActive flag set at addChildTask time. */
    void childData_isActive_returnsCorrectFlag()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());
        mock.addChildTask(0, childWindow(/*active=*/false));
        mock.addChildTask(0, childWindow(/*active=*/true));
        mock.addChildTask(0, childWindow(/*active=*/false));

        QVERIFY(!mock.childData(0, 0, TaskManager::AbstractTasksModel::IsActive).toBool());
        QVERIFY(mock.childData(0, 1, TaskManager::AbstractTasksModel::IsActive).toBool());
        QVERIFY(!mock.childData(0, 2, TaskManager::AbstractTasksModel::IsActive).toBool());
    }

    /** data returns the IsWindow flag set at addTask time. */
    void data_isWindow_returnsCorrectFlag()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());
        mock.addTask(launcherTask());

        QVERIFY(mock.data(0, TaskManager::AbstractTasksModel::IsWindow).toBool());
        QVERIFY(!mock.data(1, TaskManager::AbstractTasksModel::IsWindow).toBool());
    }

    /** data returns the IsLauncher flag set at addTask time. */
    void data_isLauncher_returnsCorrectFlag()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());
        mock.addTask(launcherTask());

        QVERIFY(!mock.data(0, TaskManager::AbstractTasksModel::IsLauncher).toBool());
        QVERIFY(mock.data(1, TaskManager::AbstractTasksModel::IsLauncher).toBool());
    }

    /** Out-of-range row returns default QVariant. */
    void data_outOfRange_returnsEmpty()
    {
        MockTasksSource mock;
        QVERIFY(!mock.data(-1, TaskManager::AbstractTasksModel::IsWindow).isValid());
        QVERIFY(!mock.data(0, TaskManager::AbstractTasksModel::IsWindow).isValid());
    }

    /** Out-of-range childRow returns default QVariant. */
    void childData_outOfRange_returnsEmpty()
    {
        MockTasksSource mock;
        mock.addTask(windowTask());
        QVERIFY(!mock.childData(0, 0, TaskManager::AbstractTasksModel::IsActive).isValid());
        QVERIFY(!mock.childData(0, -1, TaskManager::AbstractTasksModel::IsActive).isValid());
    }
};

QTEST_MAIN(TestMockTasksSource)
#include "test_mocktaskssource.moc"
