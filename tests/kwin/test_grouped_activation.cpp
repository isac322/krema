// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Left-click and wheel actions on grouped and launcher dock items.
//
// Drives the real DockModel/DockActions against a KWin virtual compositor.
// The app windows come from a child process (this binary started with
// --child) so they are ordinary xdg toplevels that libtaskmanager groups under
// one task, exactly like a real app with two windows. A second child
// (--other) is a one-window app used to move focus away from the group.
//
// Must run under run-with-kwin.sh, which provides WAYLAND_DISPLAY, a private
// session bus and throwaway XDG directories.

#include "models/dockactions.h"
#include "models/dockmodel.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QWidget>

#include <memory>

namespace
{

using TaskManager::AbstractTasksModel;

constexpr int kTimeoutMs = 15000;
const QString kWindowA = QStringLiteral("krema-group-A");
const QString kWindowB = QStringLiteral("krema-group-B");
const QString kOtherWindow = QStringLiteral("krema-other");

krema::DockModel &model()
{
    static auto *instance = new krema::DockModel;
    return *instance;
}

QString childTitle(const QModelIndex &parent, int row)
{
    return parent.model()->index(row, 0, parent).data(Qt::DisplayRole).toString();
}

// Row of the grouped task holding both child-process windows, or -1.
int groupRow()
{
    auto *tasks = model().tasksModel();
    for (int row = 0; row < tasks->rowCount(); ++row) {
        const QModelIndex idx = tasks->index(row, 0);
        if (tasks->rowCount(idx) != 2) {
            continue;
        }
        const QSet<QString> titles{childTitle(idx, 0), childTitle(idx, 1)};
        if (titles == QSet<QString>{kWindowA, kWindowB}) {
            return row;
        }
    }
    return -1;
}

// One line per task row and child, for failure messages.
std::string describeTasks()
{
    auto *tasks = model().tasksModel();
    QString out;
    for (int row = 0; row < tasks->rowCount(); ++row) {
        const QModelIndex idx = tasks->index(row, 0);
        out += QStringLiteral("row %1 '%2' appId=%3 children=%4 active=%5 minimized=%6\n")
                   .arg(row)
                   .arg(idx.data(Qt::DisplayRole).toString(), idx.data(AbstractTasksModel::AppId).toString())
                   .arg(tasks->rowCount(idx))
                   .arg(idx.data(AbstractTasksModel::IsActive).toBool())
                   .arg(idx.data(AbstractTasksModel::IsMinimized).toBool());
        for (int i = 0; i < tasks->rowCount(idx); ++i) {
            const QModelIndex child = tasks->index(i, 0, idx);
            out += QStringLiteral(
                       "  child %1 '%2' active=%3 minimized=%4 "
                       "lastActivated=%5 stacking=%6\n")
                       .arg(i)
                       .arg(childTitle(idx, i))
                       .arg(child.data(AbstractTasksModel::IsActive).toBool())
                       .arg(child.data(AbstractTasksModel::IsMinimized).toBool())
                       .arg(child.data(AbstractTasksModel::LastActivated).toDateTime().toString(Qt::ISODateWithMs))
                       .arg(child.data(AbstractTasksModel::StackingOrder).toInt());
        }
    }
    return out.toStdString();
}

// Title of the active window inside the group, or empty when none is active.
QString activeGroupWindow()
{
    auto *tasks = model().tasksModel();
    const int row = groupRow();
    if (row < 0) {
        return {};
    }
    const QModelIndex idx = tasks->index(row, 0);
    for (int i = 0; i < tasks->rowCount(idx); ++i) {
        const QModelIndex child = tasks->index(i, 0, idx);
        if (child.data(AbstractTasksModel::IsActive).toBool()) {
            return child.data(Qt::DisplayRole).toString();
        }
    }
    return {};
}

// libtaskmanager may rewrite a pinned file URL to an applications: URL once
// KSycoca knows the entry, so match by desktop file id as well as by URL.
int launcherRow(const QUrl &url)
{
    const QString fileName = url.fileName();
    const QString appId = fileName.chopped(qstrlen(".desktop"));
    auto *tasks = model().tasksModel();
    for (int row = 0; row < tasks->rowCount(); ++row) {
        const QModelIndex idx = tasks->index(row, 0);
        const QUrl rowUrl = idx.data(AbstractTasksModel::LauncherUrlWithoutIcon).toUrl();
        if (rowUrl == url || rowUrl.fileName() == fileName || rowUrl.path() == fileName || idx.data(AbstractTasksModel::AppId).toString() == appId) {
            return row;
        }
    }
    return -1;
}

// Resolve fresh indices after focus, minimization, or grouping changes.
QModelIndex windowIndex(const QString &title)
{
    auto *tasks = model().tasksModel();
    for (int row = 0; row < tasks->rowCount(); ++row) {
        const QModelIndex parent = tasks->index(row, 0);
        if (tasks->rowCount(parent) == 0 && parent.data(Qt::DisplayRole).toString() == title) {
            return parent;
        }
        for (int child = 0; child < tasks->rowCount(parent); ++child) {
            const QModelIndex index = tasks->makeModelIndex(row, child);
            if (index.data(Qt::DisplayRole).toString() == title) {
                return index;
            }
        }
    }
    return {};
}

int childMain(int argc, char *argv[])
{
    QApplication::setDesktopFileName(QStringLiteral("krema-grouptest"));
    QApplication app(argc, argv);
    QWidget a;
    QWidget b;
    a.setWindowTitle(kWindowA);
    b.setWindowTitle(kWindowB);
    a.resize(200, 200);
    b.resize(200, 200);
    const QString mode = QString::fromLocal8Bit(argv[1]);
    if (mode != QLatin1String("--single-b")) {
        a.show();
    }
    if (mode != QLatin1String("--single")) {
        b.show();
    }
    return app.exec();
}

// A single window of a different app, used to take focus away from the group.
int otherMain(int argc, char *argv[])
{
    QApplication::setDesktopFileName(QStringLiteral("krema-othertest"));
    QApplication app(argc, argv);
    QWidget other;
    other.setWindowTitle(kOtherWindow);
    other.resize(200, 200);
    other.show();
    return app.exec();
}

QModelIndex otherWindowIndex()
{
    return windowIndex(kOtherWindow);
}

// Path of the executable for @p mode. Without a desktop entry for their app
// ids, libtaskmanager identifies both children by their executable, which
// would put the --other window into the --child group. So --other runs from
// a copy of this binary under another name, in run-with-kwin.sh's scratch
// runtime directory.
QString childExecutable(const QString &mode)
{
    const QString self = QCoreApplication::applicationFilePath();
    if (mode != QLatin1String("--other")) {
        return self;
    }
    static const QString copy = [&self] {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/krema-other-app");
        QFile::remove(path);
        QFile::copy(self, path);
        return path;
    }();
    return copy;
}

// Starts this binary in @p mode; the returned guard kills it.
auto startChild(QProcess &process, const QString &mode)
{
    process.start(childExecutable(mode), {mode});
    return qScopeGuard([&process] {
        process.kill();
        process.waitForFinished();
    });
}

// Windows of the children killed by an earlier test or section can linger in
// the model for a moment; wait them out so the rows a test finds are its own.
bool noStaleTestWindows()
{
    return QTest::qWaitFor(
        [] {
            return !windowIndex(kWindowA).isValid() && !windowIndex(kWindowB).isValid() && !otherWindowIndex().isValid();
        },
        kTimeoutMs);
}

} // namespace

TEST_CASE("Left-click on a grouped app cycles through its windows", "[grouped-activation]")
{
    REQUIRE(noStaleTestWindows());
    QProcess child;
    const auto stopChild = startChild(child, QStringLiteral("--child"));
    REQUIRE(child.waitForStarted(kTimeoutMs));

    const bool grouped = QTest::qWaitFor(
        [] {
            return groupRow() >= 0;
        },
        kTimeoutMs);
    INFO("tasks:\n" << describeTasks());
    REQUIRE(grouped);
    // KWin focuses newly mapped windows; let that settle so the first click
    // starts from a stable state instead of racing the initial activation.
    (void)QTest::qWaitFor(
        [] {
            return !activeGroupWindow().isEmpty();
        },
        kTimeoutMs);

    krema::DockActions actions(&model());
    QSignalSpy launching(&actions, &krema::DockActions::taskLaunching);

    // Each click must move focus to the other window of the group: A→B→A.
    QStringList sequence{activeGroupWindow()};
    for (int click = 0; click < 3; ++click) {
        const QString before = sequence.last();
        actions.activate(groupRow());
        INFO("click " << click + 1 << ", active before: " << before.toStdString());
        REQUIRE(QTest::qWaitFor(
            [&] {
                const QString now = activeGroupWindow();
                return !now.isEmpty() && now != before;
            },
            kTimeoutMs));
        sequence.append(activeGroupWindow());
    }

    CHECK(sequence[2].toStdString() != sequence[1].toStdString());
    CHECK(sequence[3].toStdString() == sequence[1].toStdString());
    // Clicking a running group activates windows; it never launches the app.
    CHECK(launching.isEmpty());
    CHECK(model().tasksModel()->rowCount(model().tasksModel()->index(groupRow(), 0)) == 2);
}

TEST_CASE("Wheel over a non-running pinned launcher does not launch it", "[grouped-activation]")
{
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    REQUIRE(QDir().mkpath(dataHome + QStringLiteral("/applications")));
    const QString marker = dataHome + QStringLiteral("/krema-launch-marker");
    QFile::remove(marker);

    QFile desktop(dataHome + QStringLiteral("/applications/krema-wheeltest.desktop"));
    REQUIRE(desktop.open(QIODevice::WriteOnly));
    desktop.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=Krema Wheel Test\nExec=sh "
                                 "-c \"echo x >> %1\"\nIcon=application-x-executable\n")
                      .arg(marker)
                      .toUtf8());
    desktop.close();
    QProcess::execute(QStringLiteral("kbuildsycoca6"), {});

    const QUrl url = QUrl::fromLocalFile(desktop.fileName());
    auto *tasks = model().tasksModel();
    tasks->requestAddLauncher(url);
    const auto unpin = qScopeGuard([&] {
        const int row = launcherRow(url);
        if (row >= 0) {
            tasks->requestRemoveLauncher(tasks->index(row, 0).data(AbstractTasksModel::LauncherUrlWithoutIcon).toUrl());
        }
    });
    const bool pinned = QTest::qWaitFor(
        [&] {
            return launcherRow(url) >= 0;
        },
        kTimeoutMs);
    if (!pinned) {
        for (int row = 0; row < tasks->rowCount(); ++row) {
            const QModelIndex idx = tasks->index(row, 0);
            WARN("row " << row << " appId=" << idx.data(AbstractTasksModel::AppId).toString().toStdString()
                        << " url=" << idx.data(AbstractTasksModel::LauncherUrlWithoutIcon).toUrl().toString().toStdString());
        }
    }
    REQUIRE(pinned);
    const QModelIndex idx = tasks->index(launcherRow(url), 0);
    REQUIRE(idx.data(AbstractTasksModel::IsLauncher).toBool());
    REQUIRE_FALSE(idx.data(AbstractTasksModel::IsWindow).toBool());

    krema::DockActions actions(&model());
    for (int tick = 0; tick < 3; ++tick) {
        actions.cycleWindows(launcherRow(url), tick % 2 == 0);
    }
    // A launch writes the marker within a second; give it ample time.
    QTest::qWait(3000);
    CHECK_FALSE(QFile::exists(marker));

    // Control: a left-click on the same launcher does launch, so the
    // assertion above is not vacuous.
    actions.activate(launcherRow(url));
    CHECK(QTest::qWaitFor(
        [&] {
            return QFile::exists(marker);
        },
        kTimeoutMs));
}

TEST_CASE("Clicking a group returns to its most recently used window", "[grouped-activation]")
{
    REQUIRE(noStaleTestWindows());
    QProcess group;
    const auto stopGroup = startChild(group, QStringLiteral("--child"));
    REQUIRE(group.waitForStarted(kTimeoutMs));
    const bool grouped = QTest::qWaitFor(
        [] {
            return groupRow() >= 0;
        },
        kTimeoutMs);
    INFO("tasks:\n" << describeTasks());
    REQUIRE(grouped);

    auto *tasks = model().tasksModel();
    krema::DockActions actions(&model());

    // KWin focuses the group's windows as they map; let that settle so it
    // cannot override the activation below.
    (void)QTest::qWaitFor(
        [] {
            return !activeGroupWindow().isEmpty();
        },
        kTimeoutMs);
    QTest::qWait(500);

    // Use the group's second child (model order) as the last-used window:
    // entering at the first child, the old behavior, cannot pass by accident.
    const QString lastUsed = childTitle(tasks->index(groupRow(), 0), 1);
    INFO("last used window: " << lastUsed.toStdString());
    // Activate the first child, then the last-used one, each after the
    // previous activation lands: KWin's initial focus changes can share a
    // millisecond timestamp, which would leave LastActivated tied.
    for (int child : {0, 1}) {
        const QString title = childTitle(tasks->index(groupRow(), 0), child);
        tasks->requestActivate(tasks->makeModelIndex(groupRow(), child));
        REQUIRE(QTest::qWaitFor(
            [&] {
                return activeGroupWindow() == title;
            },
            kTimeoutMs));
        QTest::qWait(10);
    }

    // Leave the group by opening another app: KWin focuses its new window.
    QProcess other;
    const auto stopOther = startChild(other, QStringLiteral("--other"));
    REQUIRE(other.waitForStarted(kTimeoutMs));
    const bool left = QTest::qWaitFor(
        [] {
            return activeGroupWindow().isEmpty() && otherWindowIndex().data(AbstractTasksModel::IsActive).toBool();
        },
        kTimeoutMs);
    INFO("after opening the other app:\n" << describeTasks());
    REQUIRE(left);

    SECTION("left-click")
    {
        actions.activate(groupRow());
    }
    SECTION("wheel up (backward) enters at the same window")
    {
        actions.cycleWindows(groupRow(), false);
    }
    SECTION("wheel down (forward) enters at the same window")
    {
        actions.cycleWindows(groupRow(), true);
    }

    REQUIRE(QTest::qWaitFor(
        [] {
            return !activeGroupWindow().isEmpty();
        },
        kTimeoutMs));
    // Compare std::string: Catch2 prints a QString as {?}, which would hide
    // the entered window on failure.
    INFO("after entering the group:\n" << describeTasks());
    CHECK(activeGroupWindow().toStdString() == lastUsed.toStdString());
}

TEST_CASE("Single-window minimize clicks honor actual focus and minimized state", "[grouped-activation][click-minimize]")
{
    REQUIRE(noStaleTestWindows());
    QProcess group;
    const auto stopGroup = startChild(group, QStringLiteral("--child"));
    REQUIRE(group.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return groupRow() >= 0 && !activeGroupWindow().isEmpty();
        },
        kTimeoutMs));

    QProcess other;
    const auto stopOther = startChild(other, QStringLiteral("--other"));
    REQUIRE(other.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return otherWindowIndex().data(AbstractTasksModel::IsActive).toBool() && !otherWindowIndex().data(AbstractTasksModel::IsMinimized).toBool();
        },
        kTimeoutMs));
    REQUIRE(otherWindowIndex().data(AbstractTasksModel::IsWindow).toBool());
    REQUIRE(otherWindowIndex().data(AbstractTasksModel::IsMinimizable).toBool());
    REQUIRE_FALSE(otherWindowIndex().data(AbstractTasksModel::IsGroupParent).toBool());

    auto *tasks = model().tasksModel();
    krema::DockActions actions(&model());
    SECTION("active single minimizes")
    {
        actions.activateOrMinimize(otherWindowIndex().row());
        REQUIRE(QTest::qWaitFor(
            [] {
                return otherWindowIndex().data(AbstractTasksModel::IsMinimized).toBool() && !otherWindowIndex().data(AbstractTasksModel::IsActive).toBool();
            },
            kTimeoutMs));
    }
    SECTION("background single activates without minimizing")
    {
        tasks->requestActivate(windowIndex(kWindowA));
        REQUIRE(QTest::qWaitFor(
            [] {
                return activeGroupWindow() == kWindowA && !otherWindowIndex().data(AbstractTasksModel::IsActive).toBool();
            },
            kTimeoutMs));
        actions.activateOrMinimize(otherWindowIndex().row());
        REQUIRE(QTest::qWaitFor(
            [] {
                return otherWindowIndex().data(AbstractTasksModel::IsActive).toBool() && !otherWindowIndex().data(AbstractTasksModel::IsMinimized).toBool();
            },
            kTimeoutMs));
    }
    SECTION("minimized single restores and takes focus")
    {
        tasks->requestToggleMinimized(otherWindowIndex());
        REQUIRE(QTest::qWaitFor(
            [] {
                return otherWindowIndex().data(AbstractTasksModel::IsMinimized).toBool() && !otherWindowIndex().data(AbstractTasksModel::IsActive).toBool();
            },
            kTimeoutMs));
        tasks->requestActivate(windowIndex(kWindowA));
        REQUIRE(QTest::qWaitFor(
            [] {
                return activeGroupWindow() == kWindowA;
            },
            kTimeoutMs));
        actions.activateOrMinimize(otherWindowIndex().row());
        REQUIRE(QTest::qWaitFor(
            [] {
                return otherWindowIndex().data(AbstractTasksModel::IsActive).toBool() && !otherWindowIndex().data(AbstractTasksModel::IsMinimized).toBool();
            },
            kTimeoutMs));
    }

    INFO("after the single-window click:\n" << describeTasks());
    REQUIRE(windowIndex(kWindowA).isValid());
    REQUIRE(windowIndex(kWindowB).isValid());
    CHECK_FALSE(windowIndex(kWindowA).data(AbstractTasksModel::IsMinimized).toBool());
    CHECK_FALSE(windowIndex(kWindowB).data(AbstractTasksModel::IsMinimized).toBool());
}

TEST_CASE("Default activation keeps an active single window focused and unminimized", "[grouped-activation][click-minimize]")
{
    REQUIRE(noStaleTestWindows());
    QProcess group;
    const auto stopGroup = startChild(group, QStringLiteral("--child"));
    REQUIRE(group.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return groupRow() >= 0 && !activeGroupWindow().isEmpty();
        },
        kTimeoutMs));

    QProcess other;
    const auto stopOther = startChild(other, QStringLiteral("--other"));
    REQUIRE(other.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return otherWindowIndex().data(AbstractTasksModel::IsActive).toBool() && !otherWindowIndex().data(AbstractTasksModel::IsMinimized).toBool();
        },
        kTimeoutMs));

    krema::DockActions actions(&model());
    actions.activate(otherWindowIndex().row());
    // Observe a subsequent compositor state change instead of checking before
    // an erroneous asynchronous minimize request could have reached KWin.
    model().tasksModel()->requestToggleMinimized(windowIndex(kWindowA));
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowIndex(kWindowA).data(AbstractTasksModel::IsMinimized).toBool();
        },
        kTimeoutMs));

    INFO("after default activation:\n" << describeTasks());
    CHECK(otherWindowIndex().data(AbstractTasksModel::IsActive).toBool());
    CHECK_FALSE(otherWindowIndex().data(AbstractTasksModel::IsMinimized).toBool());
    REQUIRE(windowIndex(kWindowB).isValid());
    CHECK_FALSE(windowIndex(kWindowB).data(AbstractTasksModel::IsMinimized).toBool());
}

TEST_CASE("Active grouped-window clicks minimize only the current child", "[grouped-activation][click-minimize]")
{
    REQUIRE(noStaleTestWindows());
    QProcess group;
    const auto stopGroup = startChild(group, QStringLiteral("--child"));
    REQUIRE(group.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return groupRow() >= 0 && !activeGroupWindow().isEmpty();
        },
        kTimeoutMs));

    auto *tasks = model().tasksModel();
    REQUIRE(tasks->index(groupRow(), 0).data(AbstractTasksModel::IsGroupParent).toBool());
    bool otherMinimized = false;
    SECTION("another unminimized child stays unminimized")
    {
        REQUIRE_FALSE(windowIndex(kWindowB).data(AbstractTasksModel::IsMinimized).toBool());
    }
    SECTION("another already minimized child stays minimized")
    {
        tasks->requestToggleMinimized(windowIndex(kWindowB));
        REQUIRE(QTest::qWaitFor(
            [] {
                return windowIndex(kWindowB).data(AbstractTasksModel::IsMinimized).toBool();
            },
            kTimeoutMs));
        otherMinimized = true;
    }
    tasks->requestActivate(windowIndex(kWindowA));
    REQUIRE(QTest::qWaitFor(
        [] {
            return activeGroupWindow() == kWindowA && !windowIndex(kWindowA).data(AbstractTasksModel::IsMinimized).toBool();
        },
        kTimeoutMs));
    REQUIRE(windowIndex(kWindowA).data(AbstractTasksModel::IsWindow).toBool());
    REQUIRE(windowIndex(kWindowA).data(AbstractTasksModel::IsMinimizable).toBool());

    krema::DockActions actions(&model());
    actions.activateOrMinimize(groupRow());
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowIndex(kWindowA).data(AbstractTasksModel::IsMinimized).toBool() && !windowIndex(kWindowA).data(AbstractTasksModel::IsActive).toBool();
        },
        kTimeoutMs));

    INFO("after minimizing the active child:\n" << describeTasks());
    REQUIRE(windowIndex(kWindowB).isValid());
    CHECK(windowIndex(kWindowB).data(AbstractTasksModel::IsMinimized).toBool() == otherMinimized);
}

TEST_CASE(
    "Background grouped-window minimize clicks enter only the most "
    "recently used child",
    "[grouped-activation][click-minimize]")
{
    REQUIRE(noStaleTestWindows());
    QProcess group;
    const auto stopGroup = startChild(group, QStringLiteral("--child"));
    REQUIRE(group.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return groupRow() >= 0 && !activeGroupWindow().isEmpty();
        },
        kTimeoutMs));

    auto *tasks = model().tasksModel();
    const QString firstUsed = childTitle(tasks->index(groupRow(), 0), 0);
    const QString lastUsed = childTitle(tasks->index(groupRow(), 0), 1);
    tasks->requestActivate(windowIndex(firstUsed));
    REQUIRE(QTest::qWaitFor(
        [&] {
            return activeGroupWindow() == firstUsed && windowIndex(firstUsed).data(AbstractTasksModel::LastActivated).toDateTime().isValid();
        },
        kTimeoutMs));
    const QDateTime firstActivation = windowIndex(firstUsed).data(AbstractTasksModel::LastActivated).toDateTime();
    // Establish distinct MRU timestamps without a fixed sleep.
    REQUIRE(QTest::qWaitFor(
        [&] {
            return QDateTime::currentDateTime() > firstActivation;
        },
        kTimeoutMs));
    tasks->requestActivate(windowIndex(lastUsed));
    REQUIRE(QTest::qWaitFor(
        [&] {
            return activeGroupWindow() == lastUsed && windowIndex(lastUsed).data(AbstractTasksModel::LastActivated).toDateTime() > firstActivation;
        },
        kTimeoutMs));

    QProcess other;
    const auto stopOther = startChild(other, QStringLiteral("--other"));
    REQUIRE(other.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return activeGroupWindow().isEmpty() && otherWindowIndex().data(AbstractTasksModel::IsActive).toBool();
        },
        kTimeoutMs));

    bool firstMinimized = false;
    SECTION("no children are minimized")
    {
        REQUIRE_FALSE(windowIndex(firstUsed).data(AbstractTasksModel::IsMinimized).toBool());
        REQUIRE_FALSE(windowIndex(lastUsed).data(AbstractTasksModel::IsMinimized).toBool());
    }
    SECTION("the non-MRU child is already minimized")
    {
        tasks->requestToggleMinimized(windowIndex(firstUsed));
        REQUIRE(QTest::qWaitFor(
            [&] {
                return windowIndex(firstUsed).data(AbstractTasksModel::IsMinimized).toBool();
            },
            kTimeoutMs));
        firstMinimized = true;
    }
    SECTION("all children are minimized")
    {
        for (const QString &title : {firstUsed, lastUsed}) {
            tasks->requestToggleMinimized(windowIndex(title));
            REQUIRE(QTest::qWaitFor(
                [&] {
                    return windowIndex(title).data(AbstractTasksModel::IsMinimized).toBool();
                },
                kTimeoutMs));
        }
        firstMinimized = true;
    }
    REQUIRE(activeGroupWindow().isEmpty());
    REQUIRE(otherWindowIndex().data(AbstractTasksModel::IsActive).toBool());

    krema::DockActions actions(&model());
    actions.activateOrMinimize(groupRow());
    REQUIRE(QTest::qWaitFor(
        [&] {
            return activeGroupWindow() == lastUsed && !windowIndex(lastUsed).data(AbstractTasksModel::IsMinimized).toBool();
        },
        kTimeoutMs));

    INFO("after entering the background group:\n" << describeTasks());
    REQUIRE(windowIndex(firstUsed).isValid());
    CHECK(windowIndex(firstUsed).data(AbstractTasksModel::IsMinimized).toBool() == firstMinimized);
    CHECK_FALSE(otherWindowIndex().data(AbstractTasksModel::IsActive).toBool());
    CHECK_FALSE(otherWindowIndex().data(AbstractTasksModel::IsMinimized).toBool());
}

TEST_CASE(
    "Grouped minimize clicks follow current KWin focus rather than the "
    "previous target",
    "[grouped-activation][click-minimize]")
{
    REQUIRE(noStaleTestWindows());
    QProcess group;
    const auto stopGroup = startChild(group, QStringLiteral("--child"));
    REQUIRE(group.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return groupRow() >= 0 && !activeGroupWindow().isEmpty();
        },
        kTimeoutMs));

    auto *tasks = model().tasksModel();
    tasks->requestActivate(windowIndex(kWindowA));
    REQUIRE(QTest::qWaitFor(
        [] {
            return activeGroupWindow() == kWindowA;
        },
        kTimeoutMs));
    krema::DockActions actions(&model());
    actions.activateOrMinimize(groupRow());
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowIndex(kWindowA).data(AbstractTasksModel::IsMinimized).toBool() && !windowIndex(kWindowA).data(AbstractTasksModel::IsActive).toBool();
        },
        kTimeoutMs));

    // Explicitly observe B becoming active; never assume KWin's focus fallback.
    tasks->requestActivate(windowIndex(kWindowB));
    REQUIRE(QTest::qWaitFor(
        [] {
            return activeGroupWindow() == kWindowB && !windowIndex(kWindowB).data(AbstractTasksModel::IsMinimized).toBool();
        },
        kTimeoutMs));
    REQUIRE(windowIndex(kWindowA).data(AbstractTasksModel::IsMinimized).toBool());
    actions.activateOrMinimize(groupRow());
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowIndex(kWindowB).data(AbstractTasksModel::IsMinimized).toBool() && !windowIndex(kWindowB).data(AbstractTasksModel::IsActive).toBool();
        },
        kTimeoutMs));

    INFO("after clicking the newly focused child:\n" << describeTasks());
    REQUIRE(windowIndex(kWindowA).isValid());
    CHECK(windowIndex(kWindowA).data(AbstractTasksModel::IsMinimized).toBool());
    CHECK(activeGroupWindow().isEmpty());
}

TEST_CASE(
    "Invalid minimize-click indices preserve unrelated focus and "
    "minimized windows",
    "[grouped-activation][click-minimize]")
{
    REQUIRE(noStaleTestWindows());
    QProcess group;
    const auto stopGroup = startChild(group, QStringLiteral("--child"));
    REQUIRE(group.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return groupRow() >= 0 && !activeGroupWindow().isEmpty();
        },
        kTimeoutMs));
    QProcess other;
    const auto stopOther = startChild(other, QStringLiteral("--other"));
    REQUIRE(other.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return otherWindowIndex().data(AbstractTasksModel::IsActive).toBool();
        },
        kTimeoutMs));
    auto *tasks = model().tasksModel();
    tasks->requestToggleMinimized(windowIndex(kWindowB));
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowIndex(kWindowB).data(AbstractTasksModel::IsMinimized).toBool();
        },
        kTimeoutMs));
    REQUIRE_FALSE(otherWindowIndex().data(AbstractTasksModel::IsMinimized).toBool());
    REQUIRE_FALSE(windowIndex(kWindowA).data(AbstractTasksModel::IsMinimized).toBool());

    int invalidIndex = -1;
    SECTION("negative index")
    {
        invalidIndex = -1;
    }
    SECTION("past-the-end index")
    {
        invalidIndex = tasks->rowCount();
    }
    CAPTURE(invalidIndex);
    // Record consumer state through real model notifications so an accidental
    // focus change cannot be hidden by KWin's later focus fallback.
    bool protectedStatePreserved = true;
    const auto observed = QObject::connect(tasks, &QAbstractItemModel::dataChanged, tasks, [&] {
        protectedStatePreserved = protectedStatePreserved && otherWindowIndex().data(AbstractTasksModel::IsActive).toBool()
            && !otherWindowIndex().data(AbstractTasksModel::IsMinimized).toBool() && windowIndex(kWindowB).isValid()
            && windowIndex(kWindowB).data(AbstractTasksModel::IsMinimized).toBool();
    });
    const auto disconnect = qScopeGuard([&] {
        QObject::disconnect(observed);
    });

    krema::DockActions actions(&model());
    actions.activateOrMinimize(invalidIndex);
    // A subsequent valid request gives the compositor a positive observed
    // boundary; the two protected windows must remain unchanged throughout.
    tasks->requestToggleMinimized(windowIndex(kWindowA));
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowIndex(kWindowA).data(AbstractTasksModel::IsMinimized).toBool();
        },
        kTimeoutMs));

    INFO("after the invalid click:\n" << describeTasks());
    CHECK(protectedStatePreserved);
    CHECK(otherWindowIndex().data(AbstractTasksModel::IsActive).toBool());
    CHECK_FALSE(otherWindowIndex().data(AbstractTasksModel::IsMinimized).toBool());
    CHECK(windowIndex(kWindowB).data(AbstractTasksModel::IsMinimized).toBool());
}

TEST_CASE("Minimize-click targeting tracks single-group-single window membership", "[grouped-activation][click-minimize]")
{
    REQUIRE(noStaleTestWindows());
    QProcess first;
    const auto stopFirst = startChild(first, QStringLiteral("--single"));
    REQUIRE(first.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowIndex(kWindowA).data(AbstractTasksModel::IsActive).toBool();
        },
        kTimeoutMs));
    REQUIRE_FALSE(windowIndex(kWindowA).data(AbstractTasksModel::IsGroupParent).toBool());

    krema::DockActions actions(&model());
    actions.activateOrMinimize(windowIndex(kWindowA).row());
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowIndex(kWindowA).data(AbstractTasksModel::IsMinimized).toBool() && !windowIndex(kWindowA).data(AbstractTasksModel::IsActive).toBool();
        },
        kTimeoutMs));
    actions.activateOrMinimize(windowIndex(kWindowA).row());
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowIndex(kWindowA).data(AbstractTasksModel::IsActive).toBool() && !windowIndex(kWindowA).data(AbstractTasksModel::IsMinimized).toBool();
        },
        kTimeoutMs));

    QProcess second;
    const auto stopSecond = startChild(second, QStringLiteral("--single-b"));
    REQUIRE(second.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return groupRow() >= 0 && activeGroupWindow() == kWindowB;
        },
        kTimeoutMs));
    auto *tasks = model().tasksModel();
    REQUIRE(tasks->index(groupRow(), 0).data(AbstractTasksModel::IsGroupParent).toBool());
    actions.activateOrMinimize(groupRow());
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowIndex(kWindowB).data(AbstractTasksModel::IsMinimized).toBool() && !windowIndex(kWindowB).data(AbstractTasksModel::IsActive).toBool();
        },
        kTimeoutMs));
    CHECK_FALSE(windowIndex(kWindowA).data(AbstractTasksModel::IsMinimized).toBool());

    tasks->requestClose(windowIndex(kWindowB));
    REQUIRE(QTest::qWaitFor(
        [] {
            return !windowIndex(kWindowB).isValid() && windowIndex(kWindowA).isValid()
                && !windowIndex(kWindowA).data(AbstractTasksModel::IsGroupParent).toBool() && groupRow() < 0;
        },
        kTimeoutMs));
    tasks->requestActivate(windowIndex(kWindowA));
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowIndex(kWindowA).data(AbstractTasksModel::IsActive).toBool() && !windowIndex(kWindowA).data(AbstractTasksModel::IsMinimized).toBool();
        },
        kTimeoutMs));
    actions.activateOrMinimize(windowIndex(kWindowA).row());
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowIndex(kWindowA).data(AbstractTasksModel::IsMinimized).toBool() && !windowIndex(kWindowA).data(AbstractTasksModel::IsActive).toBool();
        },
        kTimeoutMs));
    INFO("after returning to a single window:\n" << describeTasks());
    CHECK_FALSE(windowIndex(kWindowA).data(AbstractTasksModel::IsGroupParent).toBool());
    CHECK_FALSE(windowIndex(kWindowB).isValid());
}

int main(int argc, char *argv[])
{
    if (argc > 1 && (qstrcmp(argv[1], "--child") == 0 || qstrcmp(argv[1], "--single") == 0 || qstrcmp(argv[1], "--single-b") == 0)) {
        return childMain(argc, argv);
    }
    if (argc > 1 && qstrcmp(argv[1], "--other") == 0) {
        return otherMain(argc, argv);
    }
    QApplication application(argc, argv);
    return Catch::Session().run(argc, argv);
}
