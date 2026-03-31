// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "mocktaskssource.h"
#include "models/dockactions.h"
#include "models/dockmodel.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/activityinfo.h>
#include <taskmanager/tasksmodel.h>
#include <taskmanager/virtualdesktopinfo.h>

#include <QSignalSpy>
#include <QTest>

using namespace krema;
using namespace krema::testing;

class TestDockActions : public QObject
{
    Q_OBJECT

private:
    struct Env {
        TaskManager::TasksModel tasksModel;
        TaskManager::VirtualDesktopInfo vdi;
        TaskManager::ActivityInfo ai;
        std::unique_ptr<DockModel> model;
        std::unique_ptr<DockActions> actions;
    };

    std::unique_ptr<Env> makeEnv()
    {
        auto env = std::make_unique<Env>();
        env->model = std::make_unique<DockModel>(&env->tasksModel, &env->vdi, &env->ai);
        env->actions = std::make_unique<DockActions>(env->model.get());
        return env;
    }

private Q_SLOTS:

    // --- activate on invalid index ---

    void activate_invalidIndex_noCrash()
    {
        auto env = makeEnv();
        // Should not crash
        env->actions->activate(-1);
        env->actions->activate(9999);
    }

    // --- newInstance on invalid index ---

    void newInstance_invalidIndex_noCrash()
    {
        auto env = makeEnv();
        env->actions->newInstance(-1);
        env->actions->newInstance(9999);
    }

    // --- closeTask on invalid index ---

    void closeTask_invalidIndex_noCrash()
    {
        auto env = makeEnv();
        env->actions->closeTask(-1);
        env->actions->closeTask(9999);
    }

    // --- togglePinned on invalid index ---

    void togglePinned_invalidIndex_noCrash()
    {
        auto env = makeEnv();
        env->actions->togglePinned(-1);
        env->actions->togglePinned(9999);
    }

    // --- cycleWindows on invalid index ---

    void cycleWindows_invalidIndex_noCrash()
    {
        auto env = makeEnv();
        env->actions->cycleWindows(-1, true);
        env->actions->cycleWindows(9999, false);
    }

    // --- moveTask ---

    void moveTask_sameIndex_returnsFalse()
    {
        auto env = makeEnv();
        QVERIFY(!env->actions->moveTask(0, 0));
    }

    void moveTask_invalidFrom_returnsFalse()
    {
        auto env = makeEnv();
        QVERIFY(!env->actions->moveTask(-1, 0));
        QVERIFY(!env->actions->moveTask(9999, 0));
    }

    void moveTask_invalidTo_returnsFalse()
    {
        auto env = makeEnv();
        QVERIFY(!env->actions->moveTask(0, -1));
        QVERIFY(!env->actions->moveTask(0, 9999));
    }

    void moveTask_validRows_reordersTasksAndSyncLaunchers()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        QMap<int, QVariant> firstTask;
        firstTask[TaskManager::AbstractTasksModel::AppId] = QStringLiteral("org.kde.first");
        QMap<int, QVariant> secondTask;
        secondTask[TaskManager::AbstractTasksModel::AppId] = QStringLiteral("org.kde.second");
        QMap<int, QVariant> thirdTask;
        thirdTask[TaskManager::AbstractTasksModel::AppId] = QStringLiteral("org.kde.third");

        mock->addTask(firstTask);
        mock->addTask(secondTask);
        mock->addTask(thirdTask);

        env->model->setTasksSource(std::move(mockOwned));

        QSignalSpy spy(env->actions.get(), &DockActions::pinnedLaunchersChanged);

        QVERIFY(env->actions->moveTask(0, 2));

        QCOMPARE(mock->moveCalls.size(), 1);
        QCOMPARE(mock->moveCalls.first().fromRow, 0);
        QCOMPARE(mock->moveCalls.first().toRow, 2);
        QCOMPARE(mock->syncLaunchersCallCount, 1);
        QCOMPARE(mock->data(0, TaskManager::AbstractTasksModel::AppId).toString(), QStringLiteral("org.kde.second"));
        QCOMPARE(mock->data(1, TaskManager::AbstractTasksModel::AppId).toString(), QStringLiteral("org.kde.third"));
        QCOMPARE(mock->data(2, TaskManager::AbstractTasksModel::AppId).toString(), QStringLiteral("org.kde.first"));
        QCOMPARE(spy.count(), 1);
    }

    void moveTask_sameIndex_noCallsOnSource()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        mock->addTask({});
        mock->addTask({});

        env->model->setTasksSource(std::move(mockOwned));

        QSignalSpy spy(env->actions.get(), &DockActions::pinnedLaunchersChanged);

        QVERIFY(!env->actions->moveTask(1, 1));

        QCOMPARE(mock->moveCalls.size(), 0);
        QCOMPARE(mock->syncLaunchersCallCount, 0);
        QCOMPARE(spy.count(), 0);
    }

    void moveTask_invalidRows_noCallsOnSource()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        mock->addTask({});
        mock->addTask({});

        env->model->setTasksSource(std::move(mockOwned));

        QSignalSpy spy(env->actions.get(), &DockActions::pinnedLaunchersChanged);

        QVERIFY(!env->actions->moveTask(-1, 1));
        QVERIFY(!env->actions->moveTask(0, -1));
        QVERIFY(!env->actions->moveTask(9999, 1));
        QVERIFY(!env->actions->moveTask(0, 9999));

        QCOMPARE(mock->moveCalls.size(), 0);
        QCOMPARE(mock->syncLaunchersCallCount, 0);
        QCOMPARE(spy.count(), 0);
    }

    void moveTask_failedMove_doesNotSyncOrEmit()
    {
        auto env = makeEnv();

        class FailingMoveTasksSource final : public MockTasksSource
        {
        public:
            bool move(int fromRow, int toRow) override
            {
                moveCalls.append(MoveCall{.fromRow = fromRow, .toRow = toRow});
                return false;
            }
        };

        auto mockOwned = std::make_unique<FailingMoveTasksSource>();
        FailingMoveTasksSource *mock = mockOwned.get();

        mock->addTask({});
        mock->addTask({});

        env->model->setTasksSource(std::move(mockOwned));

        QSignalSpy spy(env->actions.get(), &DockActions::pinnedLaunchersChanged);

        QVERIFY(!env->actions->moveTask(0, 1));

        QCOMPARE(mock->moveCalls.size(), 1);
        QCOMPARE(mock->moveCalls.first().fromRow, 0);
        QCOMPARE(mock->moveCalls.first().toRow, 1);
        QCOMPARE(mock->syncLaunchersCallCount, 0);
        QCOMPARE(spy.count(), 0);
    }

    // --- addLauncher ---

    void addLauncher_invalidUrl_returnsFalse()
    {
        auto env = makeEnv();
        QVERIFY(!env->actions->addLauncher(QUrl()));
    }

    void addLauncher_nonDesktopUrl_returnsFalse()
    {
        auto env = makeEnv();
        // A random URL that doesn't resolve to an app
        QVERIFY(!env->actions->addLauncher(QUrl(QStringLiteral("https://example.com"))));
    }

    void addLauncher_validDesktopFile()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();
        env->model->setTasksSource(std::move(mockOwned));

        const QUrl url(QStringLiteral("applications:org.kde.dolphin.desktop"));

        QVERIFY(env->actions->addLauncher(url));
        QCOMPARE(mock->addLauncherCalls.size(), 1);
        QCOMPARE(mock->addLauncherCalls.first(), url);
    }

    void addLauncher_emitsPinnedLaunchersChanged()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        env->model->setTasksSource(std::move(mockOwned));

        QSignalSpy spy(env->actions.get(), &DockActions::pinnedLaunchersChanged);

        const QUrl url(QStringLiteral("applications:org.kde.dolphin.desktop"));
        QVERIFY(env->actions->addLauncher(url));
        QCOMPARE(spy.count(), 1);
    }

    // --- removeLauncher ---

    void removeLauncher_invalidIndex_returnsFalse()
    {
        auto env = makeEnv();
        QVERIFY(!env->actions->removeLauncher(-1));
        QVERIFY(!env->actions->removeLauncher(9999));
    }

    void removeLauncher_validRow_callsRequestRemoveLauncher()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        const QUrl launcherUrl(QStringLiteral("applications:org.kde.dolphin.desktop"));

        QMap<int, QVariant> task;
        task[TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon] = launcherUrl;
        mock->addTask(task);
        QVERIFY(mock->requestAddLauncher(launcherUrl));
        mock->addLauncherCalls.clear();

        env->model->setTasksSource(std::move(mockOwned));

        QSignalSpy spy(env->actions.get(), &DockActions::pinnedLaunchersChanged);

        QVERIFY(env->actions->removeLauncher(0));
        QCOMPARE(mock->removeLauncherCalls.size(), 1);
        QCOMPARE(mock->removeLauncherCalls.first(), launcherUrl);
        QCOMPARE(spy.count(), 1);
    }

    void removeLauncher_missingLauncherUrl_returnsFalse()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        mock->addTask({});

        env->model->setTasksSource(std::move(mockOwned));

        QSignalSpy spy(env->actions.get(), &DockActions::pinnedLaunchersChanged);

        QVERIFY(!env->actions->removeLauncher(0));
        QCOMPARE(mock->removeLauncherCalls.size(), 0);
        QCOMPARE(spy.count(), 0);
    }

    // --- openUrlsWithTask ---

    void openUrlsWithTask_validRow_callsRequestOpenUrls()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        mock->addTask({});
        mock->addTask({});

        env->model->setTasksSource(std::move(mockOwned));

        const QList<QUrl> urls = {QUrl::fromLocalFile(QStringLiteral("/tmp/one.txt")), QUrl::fromLocalFile(QStringLiteral("/tmp/two.txt"))};
        env->actions->openUrlsWithTask(1, urls);

        QCOMPARE(mock->openUrlsCalls.size(), 1);
        QCOMPARE(mock->openUrlsCalls.first().row, 1);
        QCOMPARE(mock->openUrlsCalls.first().urls, urls);
    }

    void openUrlsWithTask_invalidIndex_noCallsOnSource()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        mock->addTask({});

        env->model->setTasksSource(std::move(mockOwned));

        const QList<QUrl> urls = {QUrl::fromLocalFile(QStringLiteral("/tmp/test.txt"))};
        env->actions->openUrlsWithTask(-1, urls);
        env->actions->openUrlsWithTask(9999, urls);

        QCOMPARE(mock->openUrlsCalls.size(), 0);
    }

    void openUrlsWithTask_emptyUrls_noCallsOnSource()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        mock->addTask({});

        env->model->setTasksSource(std::move(mockOwned));

        env->actions->openUrlsWithTask(0, {});

        QCOMPARE(mock->openUrlsCalls.size(), 0);
    }

    // --- taskLaunching signal ---

    void newInstance_emitsTaskLaunching()
    {
        auto env = makeEnv();
        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        QMap<int, QVariant> task;
        task[TaskManager::AbstractTasksModel::IsWindow] = false;
        task[TaskManager::AbstractTasksModel::IsLauncher] = true;
        mock->addTask(task);

        env->model->setTasksSource(std::move(mockOwned));

        QSignalSpy spy(env->actions.get(), &DockActions::taskLaunching);
        env->actions->newInstance(0);

        QCOMPARE(mock->newInstanceCalls.size(), 1);
        QCOMPARE(mock->newInstanceCalls.first().row, 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toInt(), 0);
    }

    // -----------------------------------------------------------------------
    // MockTasksSource-based tests: activate, newInstance, cycleWindows, closeTask
    //
    // These tests inject a MockTasksSource into DockModel so that
    // DockActions operates on pre-seeded in-memory window data instead
    // of requiring a live Wayland compositor.
    // -----------------------------------------------------------------------

    // --- Helpers for building mock task role maps ---

    // --- activate with MockTasksSource ---

    /**
     * activate(index) on a window task calls requestActivate on ITasksSource.
     */
    void activate_windowTask_callsRequestActivate()
    {
        auto env = makeEnv();

        // Build mock with one window task and inject
        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        QMap<int, QVariant> task;
        task[TaskManager::AbstractTasksModel::IsWindow] = true;
        task[TaskManager::AbstractTasksModel::IsLauncher] = false;
        mock->addTask(task);

        env->model->setTasksSource(std::move(mockOwned));

        env->actions->activate(0);

        QCOMPARE(mock->activateCalls.size(), 1);
        QCOMPARE(mock->activateCalls.first().row, 0);
        QCOMPARE(mock->newInstanceCalls.size(), 0);
    }

    /**
     * activate(index) on a launcher task calls requestNewInstance on ITasksSource.
     */
    void activate_launcherTask_callsRequestNewInstance()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        QMap<int, QVariant> task;
        task[TaskManager::AbstractTasksModel::IsWindow] = false;
        task[TaskManager::AbstractTasksModel::IsLauncher] = true;
        mock->addTask(task);

        env->model->setTasksSource(std::move(mockOwned));

        env->actions->activate(0);

        QCOMPARE(mock->newInstanceCalls.size(), 1);
        QCOMPARE(mock->newInstanceCalls.first().row, 0);
        QCOMPARE(mock->activateCalls.size(), 0);
    }

    /**
     * activate emits taskLaunching signal when activating a launcher.
     */
    void activate_launcherTask_emitsTaskLaunching()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        QMap<int, QVariant> task;
        task[TaskManager::AbstractTasksModel::IsWindow] = false;
        task[TaskManager::AbstractTasksModel::IsLauncher] = true;
        mock->addTask(task);

        env->model->setTasksSource(std::move(mockOwned));

        QSignalSpy spy(env->actions.get(), &DockActions::taskLaunching);
        env->actions->activate(0);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toInt(), 0);
    }

    /**
     * activate with out-of-range index must not invoke any ITasksSource method.
     */
    void activate_outOfRange_noCallsOnSource()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        QMap<int, QVariant> task;
        task[TaskManager::AbstractTasksModel::IsWindow] = true;
        task[TaskManager::AbstractTasksModel::IsLauncher] = false;
        mock->addTask(task);

        env->model->setTasksSource(std::move(mockOwned));

        env->actions->activate(-1);
        env->actions->activate(9999);

        QCOMPARE(mock->activateCalls.size(), 0);
        QCOMPARE(mock->newInstanceCalls.size(), 0);
    }

    // --- newInstance with MockTasksSource ---

    /**
     * newInstance(index) forwards the selected row through ITasksSource.
     */
    void newInstance_secondRow_callsRequestNewInstance()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        QMap<int, QVariant> task;
        task[TaskManager::AbstractTasksModel::IsWindow] = false;
        task[TaskManager::AbstractTasksModel::IsLauncher] = true;
        mock->addTask(task); // row 0
        mock->addTask(task); // row 1
        mock->addTask(task); // row 2

        env->model->setTasksSource(std::move(mockOwned));

        QSignalSpy spy(env->actions.get(), &DockActions::taskLaunching);
        env->actions->newInstance(1);

        QCOMPARE(mock->newInstanceCalls.size(), 1);
        QCOMPARE(mock->newInstanceCalls.first().row, 1);
        QCOMPARE(mock->activateCalls.size(), 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toInt(), 1);
    }

    /**
     * newInstance with an out-of-range index must not invoke ITasksSource.
     */
    void newInstance_outOfRange_noCallsOnSource()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        QMap<int, QVariant> task;
        task[TaskManager::AbstractTasksModel::IsWindow] = false;
        task[TaskManager::AbstractTasksModel::IsLauncher] = true;
        mock->addTask(task);

        env->model->setTasksSource(std::move(mockOwned));

        QSignalSpy spy(env->actions.get(), &DockActions::taskLaunching);
        env->actions->newInstance(-1);
        env->actions->newInstance(9999);

        QCOMPARE(mock->newInstanceCalls.size(), 0);
        QCOMPARE(spy.count(), 0);
    }

    // --- togglePinned with MockTasksSource ---

    /**
     * togglePinned(index) on an unpinned launcher calls requestAddLauncher.
     */
    void togglePinned_unpinnedLauncher_callsRequestAddLauncher()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        const QUrl launcherUrl(QStringLiteral("applications:org.kde.dolphin.desktop"));

        QMap<int, QVariant> task;
        task[TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon] = launcherUrl;
        mock->addTask(task);

        env->model->setTasksSource(std::move(mockOwned));

        QSignalSpy spy(env->actions.get(), &DockActions::pinnedLaunchersChanged);
        env->actions->togglePinned(0);

        QCOMPARE(mock->addLauncherCalls.size(), 1);
        QCOMPARE(mock->addLauncherCalls.first(), launcherUrl);
        QCOMPARE(mock->removeLauncherCalls.size(), 0);
        QCOMPARE(spy.count(), 1);
    }

    /**
     * togglePinned(index) on a pinned launcher calls requestRemoveLauncher.
     */
    void togglePinned_pinnedLauncher_callsRequestRemoveLauncher()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        const QUrl launcherUrl(QStringLiteral("applications:org.kde.dolphin.desktop"));

        QMap<int, QVariant> task;
        task[TaskManager::AbstractTasksModel::LauncherUrlWithoutIcon] = launcherUrl;
        mock->addTask(task);
        QVERIFY(mock->requestAddLauncher(launcherUrl));
        mock->addLauncherCalls.clear();

        env->model->setTasksSource(std::move(mockOwned));

        QSignalSpy spy(env->actions.get(), &DockActions::pinnedLaunchersChanged);
        env->actions->togglePinned(0);

        QCOMPARE(mock->removeLauncherCalls.size(), 1);
        QCOMPARE(mock->removeLauncherCalls.first(), launcherUrl);
        QCOMPARE(mock->addLauncherCalls.size(), 0);
        QCOMPARE(spy.count(), 1);
    }

    /**
     * togglePinned ignores out-of-range rows and rows without launcher URLs.
     */
    void togglePinned_invalidRowsOrLauncher_noCallsOnSource()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        mock->addTask({}); // row 0: no launcher URL

        env->model->setTasksSource(std::move(mockOwned));

        QSignalSpy spy(env->actions.get(), &DockActions::pinnedLaunchersChanged);
        env->actions->togglePinned(-1);
        env->actions->togglePinned(9999);
        env->actions->togglePinned(0);

        QCOMPARE(mock->addLauncherCalls.size(), 0);
        QCOMPARE(mock->removeLauncherCalls.size(), 0);
        QCOMPARE(spy.count(), 0);
    }

    // --- cycleWindows with MockTasksSource ---

    /**
     * cycleWindows on a single-window task (no grouped children) just
     * calls requestActivate on the top-level row.
     */
    void cycleWindows_singleWindow_callsRequestActivate()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        QMap<int, QVariant> task;
        task[TaskManager::AbstractTasksModel::IsWindow] = true;
        task[TaskManager::AbstractTasksModel::IsLauncher] = false;
        mock->addTask(task);
        // No children → childCount(0) == 0, treated as single window

        env->model->setTasksSource(std::move(mockOwned));

        env->actions->cycleWindows(0, true);

        QCOMPARE(mock->activateCalls.size(), 1);
        QCOMPARE(mock->activateCalls.first().row, 0);
        QCOMPARE(mock->activateChildCalls.size(), 0);
    }

    /**
     * cycleWindows forward on a grouped task advances to the next child.
     *
     * Task has 3 children; child 1 is active.
     * Forward cycle must activate child 2.
     */
    void cycleWindows_groupedForward_activatesNextChild()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        QMap<int, QVariant> parentTask;
        parentTask[TaskManager::AbstractTasksModel::IsWindow] = true;
        parentTask[TaskManager::AbstractTasksModel::IsLauncher] = false;
        mock->addTask(parentTask);

        auto child = [](bool active) {
            QMap<int, QVariant> d;
            d[TaskManager::AbstractTasksModel::IsActive] = active;
            return d;
        };
        mock->addChildTask(0, child(false)); // child 0
        mock->addChildTask(0, child(true)); // child 1 — active
        mock->addChildTask(0, child(false)); // child 2

        env->model->setTasksSource(std::move(mockOwned));

        env->actions->cycleWindows(0, /*forward=*/true);

        QCOMPARE(mock->activateChildCalls.size(), 1);
        QCOMPARE(mock->activateChildCalls.first().row, 0);
        QCOMPARE(mock->activateChildCalls.first().childRow, 2); // (1+1)%3
        QCOMPARE(mock->activateCalls.size(), 0);
    }

    /**
     * cycleWindows backward from child 0 wraps to the last child.
     */
    void cycleWindows_groupedBackward_wrapsToLastChild()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        QMap<int, QVariant> parentTask;
        parentTask[TaskManager::AbstractTasksModel::IsWindow] = true;
        parentTask[TaskManager::AbstractTasksModel::IsLauncher] = false;
        mock->addTask(parentTask);

        auto child = [](bool active) {
            QMap<int, QVariant> d;
            d[TaskManager::AbstractTasksModel::IsActive] = active;
            return d;
        };
        mock->addChildTask(0, child(true)); // child 0 — active
        mock->addChildTask(0, child(false)); // child 1
        mock->addChildTask(0, child(false)); // child 2

        env->model->setTasksSource(std::move(mockOwned));

        env->actions->cycleWindows(0, /*forward=*/false);

        QCOMPARE(mock->activateChildCalls.size(), 1);
        QCOMPARE(mock->activateChildCalls.first().childRow, 2); // (0-1+3)%3
    }

    /**
     * cycleWindows on a launcher task must not call any source method.
     */
    void cycleWindows_launcherTask_noCallsOnSource()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        QMap<int, QVariant> task;
        task[TaskManager::AbstractTasksModel::IsWindow] = false;
        task[TaskManager::AbstractTasksModel::IsLauncher] = true;
        mock->addTask(task);

        env->model->setTasksSource(std::move(mockOwned));

        env->actions->cycleWindows(0, true);

        QCOMPARE(mock->activateCalls.size(), 0);
        QCOMPARE(mock->activateChildCalls.size(), 0);
    }

    // --- closeTask with MockTasksSource ---

    /**
     * closeTask(index) calls requestClose on ITasksSource with the correct row.
     */
    void closeTask_windowTask_callsRequestClose()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        QMap<int, QVariant> task;
        task[TaskManager::AbstractTasksModel::IsWindow] = true;
        task[TaskManager::AbstractTasksModel::IsLauncher] = false;
        mock->addTask(task);

        env->model->setTasksSource(std::move(mockOwned));

        env->actions->closeTask(0);

        QCOMPARE(mock->closeCalls.size(), 1);
        QCOMPARE(mock->closeCalls.first().row, 0);
        QCOMPARE(mock->activateCalls.size(), 0);
    }

    /**
     * closeTask on the correct row in a multi-task mock closes only that row.
     */
    void closeTask_secondRow_closesCorrectRow()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        QMap<int, QVariant> task;
        task[TaskManager::AbstractTasksModel::IsWindow] = true;
        task[TaskManager::AbstractTasksModel::IsLauncher] = false;
        mock->addTask(task); // row 0
        mock->addTask(task); // row 1
        mock->addTask(task); // row 2

        env->model->setTasksSource(std::move(mockOwned));

        env->actions->closeTask(2);

        QCOMPARE(mock->closeCalls.size(), 1);
        QCOMPARE(mock->closeCalls.first().row, 2);
    }

    /**
     * closeTask with an out-of-range index must not call requestClose.
     */
    void closeTask_outOfRange_noCallsOnSource()
    {
        auto env = makeEnv();

        auto mockOwned = std::make_unique<MockTasksSource>();
        MockTasksSource *mock = mockOwned.get();

        QMap<int, QVariant> task;
        task[TaskManager::AbstractTasksModel::IsWindow] = true;
        task[TaskManager::AbstractTasksModel::IsLauncher] = false;
        mock->addTask(task);

        env->model->setTasksSource(std::move(mockOwned));

        env->actions->closeTask(-1);
        env->actions->closeTask(9999);

        QCOMPARE(mock->closeCalls.size(), 0);
    }
};

QTEST_MAIN(TestDockActions)
#include "test_dockactions.moc"
