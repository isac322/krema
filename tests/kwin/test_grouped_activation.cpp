// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Left-click and wheel actions on grouped and launcher dock items.
//
// Drives the real DockModel/DockActions against a KWin virtual compositor.
// The app windows come from a child process (this binary started with
// --child) so they are ordinary xdg toplevels that libtaskmanager groups under
// one task, exactly like a real app with two windows.
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

namespace
{

using TaskManager::AbstractTasksModel;

constexpr int kTimeoutMs = 15000;
const QString kWindowA = QStringLiteral("krema-group-A");
const QString kWindowB = QStringLiteral("krema-group-B");

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

} // namespace

TEST_CASE("Left-click on a grouped app cycles through its windows", "[grouped-activation]")
{
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--child")});
    const auto stopChild = qScopeGuard([&] {
        child.kill();
        child.waitForFinished();
    });
    REQUIRE(child.waitForStarted(kTimeoutMs));

    REQUIRE(QTest::qWaitFor(
        [] {
            return groupRow() >= 0;
        },
        kTimeoutMs));
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

int main(int argc, char *argv[])
{
    if (argc > 1 && qstrcmp(argv[1], "--child") == 0) {
        return childMain(argc, argv);
    }
    QApplication application(argc, argv);
    return Catch::Session().run(argc, argv);
}
