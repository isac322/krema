// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// A window started for a pinned launcher must take that launcher's place in
// the real TaskManager -> DockModel order, in both zone modes. Plasma 6.8's
// libtaskmanager appends such a window at the end under manual sorting
// (plasma-workspace 4a513cce1), so DockModel restores the launcher slot. An
// explicit reorder made while that repair is pending must not be undone.
//
// Must run under run-with-kwin.sh.

#include "models/dockmodel.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QStringList>
#include <QTest>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace
{

using TaskManager::AbstractTasksModel;

constexpr int kTimeoutMs = 15000;
const QStringList kAppIds{QStringLiteral("krema-inplace-a"), QStringLiteral("krema-inplace-b"), QStringLiteral("krema-inplace-c")};

krema::DockModel &model()
{
    // Keep the model alive until process exit. KWin's model owns Wayland-side
    // objects whose teardown order is not useful to this integration test.
    static auto *instance = new krema::DockModel;
    return *instance;
}

QString launcher(const QString &appId)
{
    return QStringLiteral("applications:%1.desktop").arg(appId);
}

QString title(const QString &appId)
{
    return appId + QStringLiteral("-window");
}

// Row order as "L<n>" for pinned launcher n, "W<n>" for a window task backed
// by launcher n, and "W-1" for any other window task.
QStringList layout()
{
    auto *tasks = model().tasksModel();
    QStringList rows;
    for (int row = 0; row < tasks->rowCount(); ++row) {
        const QModelIndex index = tasks->index(row, 0);
        const int position = tasks->launcherPosition(index.data(AbstractTasksModel::LauncherUrlWithoutIcon).toUrl());
        const bool isLauncher = index.data(AbstractTasksModel::IsLauncher).toBool();
        rows.append((isLauncher ? QStringLiteral("L") : QStringLiteral("W")) + QString::number(position));
    }
    return rows;
}

bool reaches(const QStringList &expected)
{
    // The fix runs in a deferred pass; the order must also stay put.
    const bool reached = QTest::qWaitFor([&expected] { return layout() == expected; }, kTimeoutMs);
    if (reached) {
        QTest::qWait(500);
    }
    const QStringList actual = layout();
    UNSCOPED_INFO("expected " << expected.join(QLatin1Char(' ')).toStdString() << ", have " << actual.join(QLatin1Char(' ')).toStdString());
    return actual == expected;
}

bool installDesktopEntries()
{
    for (const QString &appId : kAppIds) {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/applications/") + appId
            + QStringLiteral(".desktop");
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            return false;
        }
        QFile desktop(path);
        if (!desktop.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        desktop.write(QStringLiteral("[Desktop Entry]\n"
                                     "Type=Application\n"
                                     "Name=%1\n"
                                     "Exec=true\n"
                                     "Icon=utilities-terminal\n")
                          .arg(appId)
                          .toUtf8());
    }
    return QProcess::execute(QStringLiteral("kbuildsycoca6"), {}) == 0;
}

class Children
{
public:
    ~Children()
    {
        for (const auto &process : m_processes) {
            process->kill();
            process->waitForFinished(kTimeoutMs);
        }
    }

    bool start(const QString &appId)
    {
        auto process = std::make_unique<QProcess>();
        process->start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child"), appId, title(appId)});
        const bool started = process->waitForStarted(kTimeoutMs);
        m_processes.push_back(std::move(process));
        return started;
    }

private:
    std::vector<std::unique_ptr<QProcess>> m_processes;
};

void configure(bool separate)
{
    model().setSeparateLaunchers(separate);
    model().setPinnedLaunchers({launcher(kAppIds[0]), launcher(kAppIds[1]), launcher(kAppIds[2])});
}

int childMain(int argc, char **argv)
{
    const QString appId = QString::fromLocal8Bit(argv[2]);
    QGuiApplication::setDesktopFileName(appId);
    QApplication application(argc, argv);
    QMainWindow window;
    window.setWindowTitle(QString::fromLocal8Bit(argv[3]));
    window.resize(240, 180);
    window.show();
    return application.exec();
}

// Runs a callback when it receives a posted QEvent::User.
class PostedMove : public QObject
{
public:
    explicit PostedMove(std::function<void()> callback)
        : m_callback(std::move(callback))
    {
    }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::User) {
            m_callback();
            return true;
        }
        return QObject::event(event);
    }

private:
    std::function<void()> m_callback;
};

} // namespace

TEST_CASE("Started tasks replace their pinned launchers in place", "[launch-in-place]")
{
    REQUIRE(installDesktopEntries());

    for (const bool separate : {false, true}) {
        INFO("separate launchers: " << separate);
        configure(separate);
        REQUIRE(reaches({QStringLiteral("L0"), QStringLiteral("L1"), QStringLiteral("L2")}));
        {
            Children children;
            // Middle launcher: the task lands between the two cold launchers.
            REQUIRE(children.start(kAppIds[1]));
            CHECK(reaches({QStringLiteral("L0"), QStringLiteral("W1"), QStringLiteral("L2")}));

            // First launcher: the task takes row 0 ahead of the running task.
            REQUIRE(children.start(kAppIds[0]));
            CHECK(reaches({QStringLiteral("W0"), QStringLiteral("W1"), QStringLiteral("L2")}));

            // Last launcher: anchored to the task that replaced launcher 1.
            REQUIRE(children.start(kAppIds[2]));
            CHECK(reaches({QStringLiteral("W0"), QStringLiteral("W1"), QStringLiteral("W2")}));
        }
        // Children are gone: every launcher must return before the next mode.
        REQUIRE(reaches({QStringLiteral("L0"), QStringLiteral("L1"), QStringLiteral("L2")}));
    }
}

TEST_CASE("An explicit reorder wins over a pending launcher-slot repair", "[launch-in-place]")
{
    REQUIRE(installDesktopEntries());
    configure(false);
    REQUIRE(reaches({QStringLiteral("L0"), QStringLiteral("L1"), QStringLiteral("L2")}));

    // An unpinned task to reorder: moving it keeps the launcher list as is,
    // so the move does not reset the model behind the pending pass.
    Children children;
    REQUIRE(children.start(QStringLiteral("krema-inplace-unpinned")));
    REQUIRE(reaches({QStringLiteral("L0"), QStringLiteral("L1"), QStringLiteral("L2"), QStringLiteral("W-1")}));

    auto *tasks = model().tasksModel();
    // Row of launcher 1's window task while that launcher is hidden, else -1.
    // A startup notification row for the child comes and goes first.
    const auto startedTaskRow = [tasks]() {
        int taskRow = -1;
        for (int row = 0; row < tasks->rowCount(); ++row) {
            const QModelIndex index = tasks->index(row, 0);
            if (tasks->launcherPosition(index.data(AbstractTasksModel::LauncherUrlWithoutIcon).toUrl()) != 1) {
                continue;
            }
            if (index.data(AbstractTasksModel::IsLauncher).toBool()) {
                return -1;
            }
            if (index.data(AbstractTasksModel::IsWindow).toBool()) {
                taskRow = row;
            }
        }
        return taskRow;
    };

    // The model change that replaces launcher 1 with its task also queues
    // DockModel's launcher-slot pass. A high-priority posted event runs after
    // that change completes but before the queued pass, so the user's move
    // lands while the repair is still pending. Moving the unpinned task
    // right after launcher 0 parts the started task from that launcher,
    // which the stale repair would undo.
    bool posted = false;
    bool movedOk = false;
    QStringList startedLayout;
    QStringList movedLayout;
    PostedMove mover([&] {
        startedLayout = layout();
        const int unpinnedRow = static_cast<int>(startedLayout.indexOf(QStringLiteral("W-1")));
        movedOk = startedTaskRow() >= 0 && unpinnedRow > 1 && model().moveTask(unpinnedRow, 1);
        movedLayout = layout();
    });
    QObject watcher;
    const auto watch = [&] {
        if (!posted && startedTaskRow() >= 0) {
            posted = true;
            QCoreApplication::postEvent(&mover, new QEvent(QEvent::User), Qt::HighEventPriority);
        }
    };
    QObject::connect(tasks, &QAbstractItemModel::rowsInserted, &watcher, watch);
    QObject::connect(tasks, &QAbstractItemModel::rowsRemoved, &watcher, watch);
    QObject::connect(tasks, &QAbstractItemModel::rowsMoved, &watcher, watch);
    QObject::connect(tasks, &QAbstractItemModel::layoutChanged, &watcher, watch);
    QObject::connect(tasks, &QAbstractItemModel::dataChanged, &watcher, watch);

    REQUIRE(children.start(kAppIds[1]));
    REQUIRE(QTest::qWaitFor([&movedLayout] { return !movedLayout.isEmpty(); }, kTimeoutMs));
    INFO("started " << startedLayout.join(QLatin1Char(' ')).toStdString() << ", moved " << movedLayout.join(QLatin1Char(' ')).toStdString());
    REQUIRE(movedOk);
    REQUIRE(movedLayout.mid(0, 2) == QStringList{QStringLiteral("L0"), QStringLiteral("W-1")});
    CHECK(model().pinnedLaunchers() == QStringList{launcher(kAppIds[0]), launcher(kAppIds[1]), launcher(kAppIds[2])});
    // Plasma 6.7 replaced launcher 1 in place, 6.8 appended its task; either
    // way the order the user made must stand.
    CHECK(reaches(movedLayout));
}

int main(int argc, char *argv[])
{
    if (argc > 1 && qstrcmp(argv[1], "--child") == 0) {
        if (argc != 4) {
            return 2;
        }
        return childMain(argc, argv);
    }

    QApplication application(argc, argv);
    return Catch::Session().run(argc, argv);
}
