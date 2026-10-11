// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// A window started for a pinned launcher must take that launcher's place in
// the real TaskManager -> DockModel order, in both zone modes. Plasma 6.8's
// libtaskmanager appends such a window at the end under manual sorting
// (plasma-workspace 4a513cce1), so DockModel restores the launcher slot.
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

#include <memory>
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
