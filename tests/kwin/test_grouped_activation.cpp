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
#include <utility>

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
        out += QStringLiteral("row %1 '%2' appId=%3 children=%4 active=%5\n")
                   .arg(row)
                   .arg(idx.data(Qt::DisplayRole).toString(), idx.data(AbstractTasksModel::AppId).toString())
                   .arg(tasks->rowCount(idx))
                   .arg(idx.data(AbstractTasksModel::IsActive).toBool());
        for (int i = 0; i < tasks->rowCount(idx); ++i) {
            const QModelIndex child = tasks->index(i, 0, idx);
            out += QStringLiteral("  child %1 '%2' active=%3 lastActivated=%4 stacking=%5\n")
                       .arg(i)
                       .arg(childTitle(idx, i))
                       .arg(child.data(AbstractTasksModel::IsActive).toBool())
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
    a.show();
    b.show();
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
    auto *tasks = model().tasksModel();
    for (int row = 0; row < tasks->rowCount(); ++row) {
        const QModelIndex idx = tasks->index(row, 0);
        if (tasks->rowCount(idx) == 0 && idx.data(Qt::DisplayRole).toString() == kOtherWindow) {
            return idx;
        }
    }
    return {};
}

// Both child modes run this same executable. Without a desktop entry for
// their app ids, libtaskmanager falls back to the executable and would group
// the --other window with the --child windows, so give each mode its own.
// Installed before the DockModel exists so its KSycoca view already has them.
void installChildDesktopEntries()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/applications");
    QDir().mkpath(dir);
    for (const auto &[id, name] : {std::pair{"krema-grouptest", "Krema Group Test"}, std::pair{"krema-othertest", "Krema Other Test"}}) {
        QFile file(dir + QLatin1Char('/') + QLatin1String(id) + QStringLiteral(".desktop"));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=%1\nExec=true\nIcon=application-x-executable\n").arg(QLatin1String(name)).toUtf8());
        }
    }
    QProcess::execute(QStringLiteral("kbuildsycoca6"), {});
}

// Starts this binary in @p mode; the returned guard kills it.
auto startChild(QProcess &process, const QString &mode)
{
    process.start(QCoreApplication::applicationFilePath(), {mode});
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
            return groupRow() < 0 && !otherWindowIndex().isValid();
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

    CHECK(sequence[2] != sequence[1]);
    CHECK(sequence[3] == sequence[1]);
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
    desktop.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=Krema Wheel Test\nExec=sh -c \"echo x >> %1\"\nIcon=application-x-executable\n")
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
    CHECK(activeGroupWindow() == lastUsed);
}

int main(int argc, char *argv[])
{
    if (argc > 1 && qstrcmp(argv[1], "--child") == 0) {
        return childMain(argc, argv);
    }
    if (argc > 1 && qstrcmp(argv[1], "--other") == 0) {
        return otherMain(argc, argv);
    }
    QApplication application(argc, argv);
    installChildDesktopEntries();
    return Catch::Session().run(argc, argv);
}
