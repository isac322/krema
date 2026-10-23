// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// A window started for a pinned launcher must take that launcher's place in
// the real TaskManager -> DockModel order, in both zone modes. Plasma 6.8's
// libtaskmanager appends such a window at the end under manual sorting
// (plasma-workspace 4a513cce1), so DockModel restores the launcher slot. An
// explicit reorder made while that repair is pending must not be undone.
//
// Must run under run-with-kwin.sh.

#include "fakepointer.h"
#include "models/dockmodel.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <LayerShellQt/Window>

#include <wayland-client.h>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMainWindow>
#include <QMouseEvent>
#include <QProcess>
#include <QRect>
#include <QRegion>
#include <QScopeGuard>
#include <QScreen>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QStringList>
#include <QTest>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace
{

using TaskManager::AbstractTasksModel;

constexpr int kTimeoutMs = 15000;
// WaylandStartupTasksModel's unchanged default startup-feedback lifetime.
constexpr int kStartupFeedbackLifetimeMs = 5000;
QMainWindow *g_launcherSurface = nullptr;
QRect g_launcherClickRect;
krema::test::FakePointer *g_pointer = nullptr;
const QStringList kAppIds{QStringLiteral("krema-inplace-a"), QStringLiteral("krema-inplace-b"), QStringLiteral("krema-inplace-c")};

krema::DockModel &model()
{
    // Keep the model alive until process exit. KWin's model owns Wayland-side
    // objects whose teardown order is not useful to this integration test.
    static auto *instance = new krema::DockModel;
    return *instance;
}

krema::test::FakePointer &pointer()
{
    Q_ASSERT(g_pointer);
    return *g_pointer;
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

QString execArgument(QString argument);

QString startupSocketPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/krema-start-")
        + QString::number(QCoreApplication::applicationPid());
}

bool installDesktopEntries()
{
    // Install once, before the task model exists. The Exec and socket remain
    // identical across cases, so no live KService/KSycoca registry is replaced.
    static const bool installed = [] {
        // Unpinned windows also need distinct desktop identities; otherwise
        // libtaskmanager resolves both to the test binary and groups them.
        const QStringList appIds = kAppIds
            + QStringList{QStringLiteral("krema-inplace-unpinned"), QStringLiteral("krema-inplace-unpinned-u"), QStringLiteral("krema-inplace-unpinned-v")};
        for (const QString &appId : appIds) {
            const QString path =
                QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/applications/") + appId + QStringLiteral(".desktop");
            if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
                return false;
            }
            QFile desktop(path);
            if (!desktop.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return false;
            }
            const bool pinned = kAppIds.contains(appId);
            QString exec = execArgument(QCoreApplication::applicationFilePath()) + (pinned ? QStringLiteral(" --startup-child ") : QStringLiteral(" --child "))
                + execArgument(appId) + QLatin1Char(' ') + execArgument(title(appId));
            if (pinned) {
                exec += QLatin1Char(' ') + execArgument(startupSocketPath());
            }
            const QByteArray contents = QStringLiteral(
                                            "[Desktop Entry]\nType=Application\nName=%1\nExec=%2\n"
                                            "Icon=utilities-terminal\nStartupNotify=true\nStartupWMClass=%1\n")
                                            .arg(appId, exec)
                                            .toUtf8();
            if (desktop.write(contents) != contents.size()) {
                return false;
            }
        }
        return QProcess::execute(QStringLiteral("kbuildsycoca6"), {}) == 0;
    }();
    return installed;
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

    void post()
    {
        if (!m_posted) {
            m_posted = true;
            QCoreApplication::postEvent(this, new QEvent(QEvent::User), Qt::HighEventPriority);
        }
    }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::User) {
            m_posted = false;
            m_callback();
            return true;
        }
        return QObject::event(event);
    }

private:
    std::function<void()> m_callback;
    bool m_posted = false;
};

// Blocking IPC deliberately leaves the parent's Qt event loop untouched:
// startup completion and window insertion arrive before the queued repair.
bool socketReady(int fd, short events, QElapsedTimer &deadline)
{
    while (deadline.elapsed() < kTimeoutMs) {
        pollfd descriptor{fd, events, 0};
        const int result = ::poll(&descriptor, 1, kTimeoutMs - static_cast<int>(deadline.elapsed()));
        if (result > 0) {
            return (descriptor.revents & events) != 0;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return false;
}

bool sendLine(int fd, const QByteArray &line)
{
    QElapsedTimer deadline;
    deadline.start();
    qsizetype written = 0;
    while (written < line.size()) {
        if (!socketReady(fd, POLLOUT, deadline)) {
            return false;
        }
        const ssize_t count = ::send(fd, line.constData() + written, line.size() - written, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (count > 0) {
            written += count;
        } else if (count < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

QByteArray receiveLine(int fd)
{
    QElapsedTimer deadline;
    deadline.start();
    QByteArray line;
    while (line.size() < 128 && socketReady(fd, POLLIN, deadline)) {
        char byte;
        const ssize_t count = ::recv(fd, &byte, 1, MSG_DONTWAIT);
        if (count == 1) {
            if (byte == '\n') {
                return line;
            }
            line.append(byte);
        } else if (count < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        } else {
            break;
        }
    }
    return {};
}

QString execArgument(QString argument)
{
    // General desktop-entry unescaping precedes the separate Exec parser.
    argument.replace(QLatin1Char('%'), QStringLiteral("%%"));
    argument.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    argument.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    argument.replace(QLatin1Char('`'), QStringLiteral("\\`"));
    argument.replace(QLatin1Char('$'), QStringLiteral("\\$"));
    argument.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    return QLatin1Char('"') + argument + QLatin1Char('"');
}

class StartupChildren : public QObject
{
public:
    StartupChildren()
        : m_path(startupSocketPath())
    {
        qApp->installEventFilter(this);
        const QByteArray path = QFile::encodeName(m_path);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (path.size() >= static_cast<qsizetype>(sizeof(address.sun_path))) {
            return;
        }
        std::memcpy(address.sun_path, path.constData(), path.size() + 1);
        m_server = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (m_server < 0) {
            return;
        }
        if (::bind(m_server, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
            ::close(m_server);
            m_server = -1;
            return;
        }
        m_bound = true;
        m_listening = ::listen(m_server, 4) == 0;
    }

    ~StartupChildren()
    {
        qApp->removeEventFilter(this);
        // KIO owns these processes. SO_PEERCRED identifies the actual child,
        // not an intermediate launcher PID. POLLHUP marks an exited peer.
        for (const Peer &peer : m_peers) {
            pollfd descriptor{peer.fd, POLLIN, 0};
            const int result = ::poll(&descriptor, 1, 0);
            if (result >= 0 && !(descriptor.revents & (POLLHUP | POLLERR | POLLNVAL))) {
                ::kill(peer.pid, SIGTERM);
            }
            QElapsedTimer deadline;
            deadline.start();
            while (socketReady(peer.fd, POLLIN, deadline)) {
                char bytes[128];
                if (::recv(peer.fd, bytes, sizeof(bytes), MSG_DONTWAIT) <= 0) {
                    break;
                }
            }
            ::close(peer.fd);
        }
        if (m_server >= 0) {
            ::close(m_server);
        }
        if (m_bound) {
            ::unlink(QFile::encodeName(m_path).constData());
        }
    }

    StartupChildren(const StartupChildren &) = delete;
    StartupChildren &operator=(const StartupChildren &) = delete;

    bool install() const
    {
        return m_listening && installDesktopEntries();
    }

    bool start(int launcherPosition)
    {
        m_startFailure.clear();
        if (!g_launcherSurface || !g_launcherSurface->windowHandle()) {
            return failStart(QStringLiteral("requesting-surface"));
        }
        if (!pointer().isValid()) {
            return failStart(QStringLiteral("fake-input-device"));
        }
        if (g_launcherClickRect.isEmpty()) {
            return failStart(QStringLiteral("requesting-surface-click-rect"));
        }
        m_pendingPosition = launcherPosition;
        m_pressed = false;
        m_requestIssued = false;
        m_inputTimer.start();
        m_startupObserved.invalidate();
        pointer().moveTo(g_launcherClickRect.center());
        pointer().click();
        if (!QTest::qWaitFor(
                [this] {
                    return m_pendingPosition < 0;
                },
                kTimeoutMs)) {
            m_pendingPosition = -1;
            return failStart(QStringLiteral("actual-spontaneous-input"));
        }
        if (!m_requestIssued) {
            return false;
        }
        if (!QTest::qWaitFor(
                [this] {
                    pollfd descriptor{m_server, POLLIN, 0};
                    return ::poll(&descriptor, 1, 0) > 0 && (descriptor.revents & POLLIN);
                },
                kTimeoutMs)) {
            return failStart(QStringLiteral("child-connection-wait"));
        }
        const int fd = ::accept4(m_server, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) {
            const int error = errno;
            return failStart(QStringLiteral("child-accept"), error);
        }
        ucred credentials{};
        socklen_t length = sizeof(credentials);
        if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) {
            const int error = errno;
            ::close(fd);
            return failStart(QStringLiteral("child-peer-credentials"), error);
        }
        if (credentials.uid != ::getuid() || credentials.pid <= 0 || credentials.pid == ::getpid()) {
            ::close(fd);
            return failStart(QStringLiteral("child-peer-identity"));
        }
        m_peers.push_back({fd, credentials.pid});
        const QByteArray ready = receiveLine(fd);
        const bool tokenPresent = ready.endsWith(" TOKEN 1");
        if (ready != QByteArray("READY ") + QByteArray::number(credentials.pid) + " TOKEN 1") {
            return failStart(QStringLiteral("child-ready-handshake (token present=%1)").arg(tokenPresent));
        }
        return true;
    }

    const QString &startFailure() const
    {
        return m_startFailure;
    }

    void noteStartupObserved()
    {
        m_startupObserved.start();
    }

    bool release(bool completingStartup = false)
    {
        if (m_peers.empty()) {
            return false;
        }
        const Peer &peer = m_peers.back();
        if (!sendLine(peer.fd, QByteArray("RELEASE\n"))) {
            return false;
        }
        const QByteArray mapped = receiveLine(peer.fd);
        UNSCOPED_INFO("compositor map ACK: " << mapped.toStdString());
        if (completingStartup) {
            if (!m_startupObserved.isValid()) {
                UNSCOPED_INFO("startup fixture failure: no startup observation preceded release");
                return false;
            }
            const qint64 observationToMap = m_startupObserved.elapsed();
            const qint64 launchToMap = m_launchTime.elapsed();
            UNSCOPED_INFO("startup observation -> map ACK: " << observationToMap << "ms; launch -> map ACK: " << launchToMap << "ms");
            if (launchToMap >= kStartupFeedbackLifetimeMs) {
                UNSCOPED_INFO("startup fixture failure: mapping did not complete inside the unchanged 5s startup lifetime");
                return false;
            }
        }
        return mapped == QByteArray("MAPPED ") + QByteArray::number(peer.pid);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (m_pendingPosition < 0) {
            return false;
        }
        const bool target = g_launcherSurface && watched == g_launcherSurface->windowHandle();
        const auto cancel = [this](const QString &stage) {
            m_pendingPosition = -1;
            m_pressed = false;
            failStart(stage);
        };
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseMove) {
            if (!event->spontaneous()) {
                return false;
            }
            const auto *mouse = static_cast<QMouseEvent *>(event);
            const bool hit = target && g_launcherClickRect.contains(mouse->globalPosition().toPoint());
            if (event->type() == QEvent::MouseButtonPress) {
                if (!hit || mouse->button() != Qt::LeftButton || mouse->buttons() != Qt::LeftButton || m_pressed) {
                    cancel(QStringLiteral("actual-left-press-target"));
                    return false;
                }
                m_pressed = true;
                m_pressPosition = mouse->globalPosition().toPoint();
                return true;
            }
            if (event->type() == QEvent::MouseMove) {
                if (m_pressed && (!hit || (mouse->globalPosition().toPoint() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance())) {
                    cancel(QStringLiteral("actual-click-drag"));
                }
                return false;
            }
            if (!hit || !m_pressed || mouse->button() != Qt::LeftButton || mouse->buttons() != Qt::NoButton
                || (mouse->globalPosition().toPoint() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance()) {
                cancel(QStringLiteral("actual-paired-release-target"));
                return false;
            }
            const int position = m_pendingPosition;
            m_pendingPosition = -1;
            m_pressed = false;
            if (m_inputTimer.elapsed() >= kTimeoutMs) {
                failStart(QStringLiteral("actual-input-deadline"));
                return true;
            }
            // Resolve the current row at delivery, then request from the real
            // release callback while Qt still has this native input context.
            auto *tasks = model().tasksModel();
            for (int row = 0; row < tasks->rowCount(); ++row) {
                const QModelIndex index = tasks->index(row, 0);
                if (tasks->launcherPosition(index.data(AbstractTasksModel::LauncherUrlWithoutIcon).toUrl()) == position) {
                    m_requestIssued = true;
                    m_launchTime.start();
                    tasks->requestNewInstance(index);
                    return true;
                }
            }
            failStart(QStringLiteral("launcher-index"));
            return true;
        }
        if (target && m_pressed && (event->type() == QEvent::Leave || event->type() == QEvent::Hide || event->type() == QEvent::UngrabMouse)) {
            cancel(QStringLiteral("actual-click-cancelled"));
        }
        return false;
    }

private:
    bool failStart(const QString &stage, int systemError = 0)
    {
        m_startFailure = stage;
        if (systemError != 0) {
            m_startFailure += QStringLiteral(" errno=%1 (%2)").arg(systemError).arg(QString::fromLocal8Bit(std::strerror(systemError)));
        }
        return false;
    }

    struct Peer {
        int fd;
        pid_t pid;
    };
    QString m_path;
    QString m_startFailure;
    int m_server = -1;
    bool m_bound = false;
    bool m_listening = false;
    std::vector<Peer> m_peers;
    int m_pendingPosition = -1;
    bool m_pressed = false;
    bool m_requestIssued = false;
    QPoint m_pressPosition;
    QElapsedTimer m_inputTimer;
    QElapsedTimer m_launchTime;
    QElapsedTimer m_startupObserved;
};

int startupChildMain(int argc, char **argv)
{
    // QtWayland uses the token when the child window requests activation.
    // Report only its presence, never its value: it is a bearer capability.
    const bool tokenPresent = !qgetenv("XDG_ACTIVATION_TOKEN").isEmpty();
    QGuiApplication::setDesktopFileName(QString::fromLocal8Bit(argv[2]));
    QApplication application(argc, argv);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const QByteArray path = QFile::encodeName(QString::fromLocal8Bit(argv[4]));
    if (path.size() >= static_cast<qsizetype>(sizeof(address.sun_path))) {
        return 3;
    }
    std::memcpy(address.sun_path, path.constData(), path.size() + 1);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return 3;
    }
    const auto closeSocket = qScopeGuard([fd] {
        ::close(fd);
    });
    if (::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0
        || !sendLine(fd, QByteArray("READY ") + QByteArray::number(::getpid()) + " TOKEN " + QByteArray::number(tokenPresent) + '\n')
        || receiveLine(fd) != QByteArray("RELEASE")) {
        return 3;
    }
    QMainWindow window;
    window.setWindowTitle(QString::fromLocal8Bit(argv[3]));
    window.resize(240, 180);
    window.show();
    if (!QTest::qWaitFor(
            [&window] {
                return window.windowHandle() && window.windowHandle()->isExposed();
            },
            kTimeoutMs)) {
        return 4;
    }
    // Configure/exposure precedes the first buffer. Repaint commits that buffer,
    // then sync flushes native requests before the parent unblocks.
    window.repaint();
    QGuiApplication::sync();
    if (!sendLine(fd, QByteArray("MAPPED ") + QByteArray::number(::getpid()) + '\n')) {
        return 4;
    }
    QSocketNotifier disconnected(fd, QSocketNotifier::Read);
    QObject::connect(&disconnected, &QSocketNotifier::activated, &application, [&] {
        char byte;
        if (::recv(fd, &byte, 1, MSG_DONTWAIT) == 0) {
            application.quit();
        }
    });
    return application.exec();
}

bool settleEvents()
{
    bool settled = false;
    PostedMove fence([&settled] {
        settled = true;
    });
    QCoreApplication::postEvent(&fence, new QEvent(QEvent::User));
    return QTest::qWaitFor(
        [&settled] {
            return settled;
        },
        kTimeoutMs);
}

bool awaitLayout(const QStringList &expected)
{
    const bool reached = QTest::qWaitFor(
        [&expected] {
            return layout() == expected;
        },
        kTimeoutMs);
    const bool settled = reached && settleEvents();
    UNSCOPED_INFO("expected " << expected.join(QLatin1Char(' ')).toStdString() << ", have " << layout().join(QLatin1Char(' ')).toStdString());
    return settled && layout() == expected;
}

int windowRows()
{
    auto *tasks = model().tasksModel();
    int count = 0;
    for (int row = 0; row < tasks->rowCount(); ++row) {
        count += tasks->index(row, 0).data(AbstractTasksModel::IsWindow).toBool();
    }
    return count;
}

bool startupAtFirstSlot()
{
    auto *tasks = model().tasksModel();
    if (tasks->rowCount() == 0) {
        return false;
    }
    const QModelIndex index = tasks->index(0, 0);
    return tasks->launcherPosition(index.data(AbstractTasksModel::LauncherUrlWithoutIcon).toUrl()) == 0 && index.data(AbstractTasksModel::IsStartup).toBool()
        && !index.data(AbstractTasksModel::IsLauncher).toBool() && !index.data(AbstractTasksModel::IsWindow).toBool()
        && index.data(AbstractTasksModel::WinIdList).toList().isEmpty();
}

int groupedWindows(int launcherPosition)
{
    auto *tasks = model().tasksModel();
    for (int row = 0; row < tasks->rowCount(); ++row) {
        const QModelIndex index = tasks->index(row, 0);
        if (tasks->launcherPosition(index.data(AbstractTasksModel::LauncherUrlWithoutIcon).toUrl()) == launcherPosition
            && index.data(AbstractTasksModel::IsWindow).toBool()) {
            return index.data(AbstractTasksModel::IsGroupParent).toBool() ? tasks->rowCount(index) : 1;
        }
    }
    return 0;
}

class TransitionTrace : public QObject
{
public:
    TransitionTrace()
        : m_capture([this] {
            capture();
        })
    {
        auto *tasks = model().tasksModel();
        // Proxy signals can fire while their source is still inserting or
        // invalidating cached services. Never query data() on that stack.
        const auto record = [this](const QString &event) {
            m_rows.append(event);
            m_capture.post();
        };
        QObject::connect(tasks, &QAbstractItemModel::rowsInserted, this, [record](const QModelIndex &, int first, int last) {
            record(QStringLiteral("rowsInserted %1-%2").arg(first).arg(last));
        });
        QObject::connect(tasks, &QAbstractItemModel::rowsRemoved, this, [record](const QModelIndex &, int first, int last) {
            record(QStringLiteral("rowsRemoved %1-%2").arg(first).arg(last));
        });
        QObject::connect(tasks, &QAbstractItemModel::rowsMoved, this, [record] {
            record(QStringLiteral("rowsMoved"));
        });
        QObject::connect(tasks, &QAbstractItemModel::layoutChanged, this, [record] {
            record(QStringLiteral("layoutChanged"));
        });
        QObject::connect(tasks, &QAbstractItemModel::dataChanged, this, [record] {
            record(QStringLiteral("dataChanged"));
        });
        capture();
    }

    QString describe() const
    {
        return m_rows.join(QLatin1Char('\n'));
    }

    bool sawStartup(int launcherPosition) const
    {
        return m_startupLaunchers.contains(launcherPosition);
    }

    QString currentRows()
    {
        capture();
        return m_rows.constLast();
    }

private:
    void capture()
    {
        auto *tasks = model().tasksModel();
        QStringList rows;
        for (int row = 0; row < tasks->rowCount(); ++row) {
            const QModelIndex index = tasks->index(row, 0);
            const int position = tasks->launcherPosition(index.data(AbstractTasksModel::LauncherUrlWithoutIcon).toUrl());
            const bool startup = index.data(AbstractTasksModel::IsStartup).toBool();
            if (startup && !m_startupLaunchers.contains(position)) {
                m_startupLaunchers.append(position);
            }
            rows.append(QStringLiteral("%1:p%2/L%3/S%4/W%5/ids%6/app=%7")
                            .arg(row)
                            .arg(position)
                            .arg(index.data(AbstractTasksModel::IsLauncher).toBool())
                            .arg(startup)
                            .arg(index.data(AbstractTasksModel::IsWindow).toBool())
                            .arg(index.data(AbstractTasksModel::WinIdList).toList().size())
                            .arg(index.data(AbstractTasksModel::AppId).toString()));
        }
        m_rows.append(QStringLiteral("rows: ") + rows.join(QLatin1Char(' ')));
    }

    QStringList m_rows;
    QList<int> m_startupLaunchers;
    PostedMove m_capture;
};

void requireStartupChild(StartupChildren &children, int launcherPosition, TransitionTrace &trace)
{
    const bool started = children.start(launcherPosition);
    const QString rows = trace.currentRows();
    INFO("startup fixture prerequisite: launched child connected with an activation token present");
    INFO("startup fixture failure stage: " << children.startFailure().toStdString());
    INFO("current rows: " << rows.toStdString() << "\nstartup trace:\n" << trace.describe().toStdString());
    REQUIRE(started);
}

void requireSettledStartup(StartupChildren &children, TransitionTrace &trace)
{
    // No startup observation means fixture failure, not a window-only pass.
    const bool reached = QTest::qWaitFor(startupAtFirstSlot, kTimeoutMs);
    const QString rows = trace.currentRows();
    INFO("startup prerequisite: " << (trace.sawStartup(0) ? "startup observed; require its pinned slot" : "fixture failure: no real startup row observed"));
    INFO("current rows: " << rows.toStdString() << "\nstartup trace:\n" << trace.describe().toStdString());
    REQUIRE(reached);
    children.noteStartupObserved();
    REQUIRE(settleEvents());
    REQUIRE(startupAtFirstSlot());
    REQUIRE(layout() == QStringList{QStringLiteral("W0"), QStringLiteral("L1"), QStringLiteral("L2")});
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

TEST_CASE("Grouped tasks preserve pinned launcher slots", "[launch-in-place]")
{
    REQUIRE(installDesktopEntries());
    configure(true);
    REQUIRE(reaches({QStringLiteral("L0"), QStringLiteral("L1"), QStringLiteral("L2")}));

    const QString desktopAppId = kAppIds[0] + QStringLiteral(".desktop");
    auto groupedChildCount = [&desktopAppId] {
        auto *tasks = model().tasksModel();
        for (int row = 0; row < tasks->rowCount(); ++row) {
            const QModelIndex index = tasks->index(row, 0);
            const QString appId = index.data(AbstractTasksModel::AppId).toString();
            if ((appId != kAppIds[0] && appId != desktopAppId) || index.data(AbstractTasksModel::IsLauncher).toBool()) {
                continue;
            }
            return tasks->rowCount(index);
        }
        return 0;
    };

    for (const int windowCount : {2, 3}) {
        INFO("grouped window count: " << windowCount);
        const auto restoreLayout = qScopeGuard([] {
            reaches({QStringLiteral("L0"), QStringLiteral("L1"), QStringLiteral("L2")});
        });
        {
            Children children;
            for (int window = 0; window < windowCount; ++window) {
                REQUIRE(children.start(kAppIds[0]));
            }
            REQUIRE(QTest::qWaitFor(
                [&] {
                    return groupedChildCount() == windowCount;
                },
                kTimeoutMs));
            CHECK(reaches({QStringLiteral("W0"), QStringLiteral("L1"), QStringLiteral("L2")}));

            REQUIRE(children.start(kAppIds[1]));
            CHECK(reaches({QStringLiteral("W0"), QStringLiteral("W1"), QStringLiteral("L2")}));
        }
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
        if (posted || startedTaskRow() < 0) {
            return;
        }
        posted = true;
        startedLayout = layout();
        const int unpinnedRow = static_cast<int>(startedLayout.indexOf(QStringLiteral("W-1")));
        movedOk = startedTaskRow() >= 0 && unpinnedRow > 1 && model().moveTask(unpinnedRow, 1);
        movedLayout = layout();
    });
    QObject watcher;
    const auto watch = [&] {
        mover.post();
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

TEST_CASE("A real startup notification retains its pinned slot when its window arrives", "[launch-in-place][startup-regression]")
{
    const QStringList launchers{launcher(kAppIds[0]), launcher(kAppIds[1]), launcher(kAppIds[2])};
    for (const bool separate : {false, true}) {
        INFO("separate launchers: " << separate);
        const auto restoreLayout = qScopeGuard([] {
            CHECK(awaitLayout({QStringLiteral("L0"), QStringLiteral("L1"), QStringLiteral("L2")}));
            CHECK(windowRows() == 0);
        });
        {
            StartupChildren children;
            REQUIRE(children.install());
            configure(separate);
            REQUIRE(awaitLayout({QStringLiteral("L0"), QStringLiteral("L1"), QStringLiteral("L2")}));
            TransitionTrace trace;
            requireStartupChild(children, 0, trace);
            requireSettledStartup(children, trace);

            // The child is still pre-show. Block this process until KWin has
            // exposed its window, batching startup removal and task insertion.
            REQUIRE(children.release(true));
            REQUIRE(QTest::qWaitFor(
                [] {
                    return groupedWindows(0) == 1;
                },
                kTimeoutMs));
            const bool firstInPlace = awaitLayout({QStringLiteral("W0"), QStringLiteral("L1"), QStringLiteral("L2")});
            INFO("real startup -> window trace:\n" << trace.describe().toStdString());
            CHECK(firstInPlace);
            const QModelIndex first = model().tasksModel()->index(0, 0);
            CHECK(first.data(AbstractTasksModel::IsWindow).toBool());
            CHECK_FALSE(first.data(AbstractTasksModel::IsStartup).toBool());
            CHECK_FALSE(first.data(AbstractTasksModel::WinIdList).toList().isEmpty());
            CHECK(model().pinnedLaunchers() == launchers);

            // Real KDE new-instance launches from an already running task
            // must preserve its slot when the group grows to two and three.
            for (const int count : {2, 3}) {
                INFO("real-launch grouped windows: " << count);
                requireStartupChild(children, 0, trace);
                REQUIRE(children.release());
                REQUIRE(QTest::qWaitFor(
                    [count] {
                        return groupedWindows(0) == count;
                    },
                    kTimeoutMs));
                CHECK(awaitLayout({QStringLiteral("W0"), QStringLiteral("L1"), QStringLiteral("L2")}));
            }
            requireStartupChild(children, 1, trace);
            REQUIRE(children.release());
            REQUIRE(QTest::qWaitFor(
                [] {
                    return groupedWindows(1) == 1;
                },
                kTimeoutMs));
            CHECK(awaitLayout({QStringLiteral("W0"), QStringLiteral("W1"), QStringLiteral("L2")}));
            CHECK(model().pinnedLaunchers() == launchers);
        }
        REQUIRE(awaitLayout({QStringLiteral("L0"), QStringLiteral("L1"), QStringLiteral("L2")}));
        CHECK(windowRows() == 0);
    }
}

TEST_CASE("Launcher-list setters distinguish pending repair from new pinned intent", "[launch-in-place][startup-regression]")
{
    const QStringList original{launcher(kAppIds[0]), launcher(kAppIds[1]), launcher(kAppIds[2])};
    QStringList requested;
    QStringList expected;
    SECTION("The same launcher list retains the startup anchor")
    {
        requested = original;
        expected = {QStringLiteral("W0"), QStringLiteral("L1"), QStringLiteral("L2")};
    }
    SECTION("A reordered launcher list replaces the old intent")
    {
        requested = {launcher(kAppIds[2]), launcher(kAppIds[0]), launcher(kAppIds[1])};
        expected = {QStringLiteral("L0"), QStringLiteral("W1"), QStringLiteral("L2")};
    }
    SECTION("A changed launcher list replaces the old intent")
    {
        requested = {launcher(kAppIds[2]), launcher(kAppIds[0])};
        expected = {QStringLiteral("L0"), QStringLiteral("W1")};
    }
    const auto restoreLayout = qScopeGuard([&original] {
        model().setPinnedLaunchers(original);
        CHECK(awaitLayout({QStringLiteral("L0"), QStringLiteral("L1"), QStringLiteral("L2")}));
        CHECK(windowRows() == 0);
    });
    {
        StartupChildren children;
        REQUIRE(children.install());
        configure(false);
        REQUIRE(awaitLayout({QStringLiteral("L0"), QStringLiteral("L1"), QStringLiteral("L2")}));
        TransitionTrace trace;
        requireStartupChild(children, 0, trace);
        requireSettledStartup(children, trace);

        bool posted = false;
        bool setterRan = false;
        QObject watcher;
        PostedMove setter([&] {
            if (posted || groupedWindows(0) != 1) {
                return;
            }
            posted = true;
            model().setPinnedLaunchers(requested);
            setterRan = true;
        });
        auto *tasks = model().tasksModel();
        // The high-priority observer runs after source-model mutation and
        // before the queued repair; signal handlers only schedule it.
        const auto watch = [&] {
            setter.post();
        };
        QObject::connect(tasks, &QAbstractItemModel::rowsInserted, &watcher, watch);
        QObject::connect(tasks, &QAbstractItemModel::rowsRemoved, &watcher, watch);
        QObject::connect(tasks, &QAbstractItemModel::layoutChanged, &watcher, watch);
        QObject::connect(tasks, &QAbstractItemModel::dataChanged, &watcher, watch);
        REQUIRE(children.release(true));
        REQUIRE(QTest::qWaitFor(
            [&setterRan] {
                return setterRan;
            },
            kTimeoutMs));
        CHECK(awaitLayout(expected));
        CHECK(model().pinnedLaunchers() == requested);

        model().setPinnedLaunchers(requested);
        CHECK(awaitLayout(expected));
    }
    model().setPinnedLaunchers(original);
    REQUIRE(awaitLayout({QStringLiteral("L0"), QStringLiteral("L1"), QStringLiteral("L2")}));
    CHECK(windowRows() == 0);
}

TEST_CASE("Real startup replacement preserves mixed-mode unpinned anchors", "[launch-in-place][startup-regression]")
{
    const QString unpinnedU = QStringLiteral("krema-inplace-unpinned-u");
    const QString unpinnedV = QStringLiteral("krema-inplace-unpinned-v");
    const QStringList launchers{launcher(kAppIds[0]), launcher(kAppIds[1])};
    const auto restoreLayout = qScopeGuard([] {
        configure(false);
        CHECK(awaitLayout({QStringLiteral("L0"), QStringLiteral("L1"), QStringLiteral("L2")}));
        CHECK(windowRows() == 0);
    });
    {
        StartupChildren started;
        REQUIRE(started.install());
        configure(false);
        model().setPinnedLaunchers(launchers);
        REQUIRE(awaitLayout({QStringLiteral("L0"), QStringLiteral("L1")}));
        TransitionTrace trace;
        Children children;
        const auto mixedLayout = [&] {
            QStringList rows = layout();
            auto *tasks = model().tasksModel();
            for (int row = 0; row < tasks->rowCount(); ++row) {
                const QModelIndex index = tasks->index(row, 0);
                QString appId = index.data(AbstractTasksModel::AppId).toString();
                appId.remove(QStringLiteral(".desktop"));
                if (index.data(AbstractTasksModel::IsWindow).toBool() && !index.data(AbstractTasksModel::WinIdList).toList().isEmpty() && appId == unpinnedU) {
                    rows[row] = QStringLiteral("U");
                } else if (index.data(AbstractTasksModel::IsWindow).toBool() && !index.data(AbstractTasksModel::WinIdList).toList().isEmpty()
                           && appId == unpinnedV) {
                    rows[row] = QStringLiteral("V");
                }
            }
            return rows;
        };
        REQUIRE(children.start(unpinnedU));
        const QStringList uOnly{QStringLiteral("L0"), QStringLiteral("L1"), QStringLiteral("U")};
        const bool uMapped = QTest::qWaitFor(
            [&] {
                return mixedLayout() == uOnly;
            },
            kTimeoutMs);
        INFO("mapped U prerequisite: expected " << uOnly.join(QLatin1Char(' ')).toStdString() << ", actual "
                                                << mixedLayout().join(QLatin1Char(' ')).toStdString());
        REQUIRE(uMapped);
        REQUIRE(settleEvents());
        REQUIRE(mixedLayout() == uOnly);
        REQUIRE(model().moveTask(2, 1));
        REQUIRE(children.start(unpinnedV));

        const QStringList before{QStringLiteral("L0"), QStringLiteral("U"), QStringLiteral("L1"), QStringLiteral("V")};
        const bool mixedPrepared = QTest::qWaitFor(
            [&] {
                return mixedLayout() == before;
            },
            kTimeoutMs);
        const QString mixedRows = trace.currentRows();
        INFO("mixed fixture pre-startup prerequisite: expected " << before.join(QLatin1Char(' ')).toStdString() << ", actual "
                                                                 << mixedLayout().join(QLatin1Char(' ')).toStdString());
        INFO("current rows with resolved AppIds: " << mixedRows.toStdString() << "\nfixture preparation trace:\n" << trace.describe().toStdString());
        REQUIRE(mixedPrepared);
        REQUIRE(settleEvents());
        REQUIRE(mixedLayout() == before);
        // Prepare V's acknowledged activation before clicking the dock-like
        // layer, whose KeyboardInteractivityNone does not request focus.
        auto *tasks = model().tasksModel();
        tasks->requestActivate(tasks->index(3, 0));
        auto *waylandApp = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
        REQUIRE(waylandApp);
        wl_display *display = waylandApp->display();
        REQUIRE(display);
        struct ActivationSync {
            wl_callback *callback = nullptr;
            bool done = false;
        } activationSync;
        const auto destroyActivationSync = qScopeGuard([&] {
            if (activationSync.callback) {
                wl_callback_destroy(activationSync.callback);
            }
        });
        static const wl_callback_listener activationSyncListener = {
            .done =
                [](void *data, wl_callback *callback, uint32_t) {
                    auto &sync = *static_cast<ActivationSync *>(data);
                    sync.done = true;
                    wl_callback_destroy(callback);
                    sync.callback = nullptr;
                },
        };
        // V may already be active, but acknowledge its forced activation
        // before the launch gesture to keep setup requests out of that action.
        // This callback inherits Qt's display default queue; Qt flushes and
        // dispatches it on the GUI thread while qWaitFor waits for the ack.
        activationSync.callback = wl_display_sync(display);
        REQUIRE(activationSync.callback);
        REQUIRE(wl_callback_add_listener(activationSync.callback, &activationSyncListener, &activationSync) == 0);
        const bool foreignFocusReady = QTest::qWaitFor(
            [&] {
                return activationSync.done && mixedLayout() == before && tasks->index(3, 0).data(AbstractTasksModel::IsActive).toBool()
                    && !g_launcherSurface->windowHandle()->isActive();
            },
            kTimeoutMs);
        INFO("foreign-focus prerequisite: activation acknowledged=" << activationSync.done
                                                                    << ", V active=" << tasks->index(3, 0).data(AbstractTasksModel::IsActive).toBool()
                                                                    << ", launcher active=" << g_launcherSurface->windowHandle()->isActive());
        REQUIRE(foreignFocusReady);
        requireStartupChild(started, 1, trace);
        const auto startupInPlace = [&] {
            auto *tasks = model().tasksModel();
            if (tasks->rowCount() != 4) {
                return false;
            }
            const QModelIndex index = tasks->index(2, 0);
            return tasks->launcherPosition(index.data(AbstractTasksModel::LauncherUrlWithoutIcon).toUrl()) == 1
                && index.data(AbstractTasksModel::IsStartup).toBool() && !index.data(AbstractTasksModel::IsWindow).toBool()
                && !index.data(AbstractTasksModel::IsLauncher).toBool() && index.data(AbstractTasksModel::WinIdList).toList().isEmpty();
        };
        const bool startupReached = QTest::qWaitFor(startupInPlace, kTimeoutMs);
        const QString startupRows = trace.currentRows();
        INFO("startup prerequisite: " << (trace.sawStartup(1) ? "startup observed; require its mixed-mode slot"
                                                              : "fixture failure: no real startup row observed"));
        INFO("current rows: " << startupRows.toStdString() << "\nstartup trace:\n" << trace.describe().toStdString());
        REQUIRE(startupReached);
        started.noteStartupObserved();
        REQUIRE(settleEvents());
        REQUIRE(startupInPlace());
        // IsActive is the TaskManager snapshot before RELEASE, not a claim
        // that V keeps seat focus after the new child maps.
        CHECK(tasks->index(3, 0).data(AbstractTasksModel::IsActive).toBool());
        CHECK_FALSE(g_launcherSurface->windowHandle()->isActive());
        REQUIRE(started.release(true));
        const QStringList after{QStringLiteral("L0"), QStringLiteral("U"), QStringLiteral("W1"), QStringLiteral("V")};
        REQUIRE(QTest::qWaitFor(
            [] {
                return groupedWindows(1) == 1;
            },
            kTimeoutMs));
        const bool placed = QTest::qWaitFor(
            [&] {
                return mixedLayout() == after;
            },
            kTimeoutMs);
        REQUIRE(settleEvents());
        INFO("mixed layout: " << mixedLayout().join(QLatin1Char(' ')).toStdString());
        CHECK(placed);
        CHECK(mixedLayout() == after);
        CHECK(model().pinnedLaunchers() == launchers);
    }
    REQUIRE(awaitLayout({QStringLiteral("L0"), QStringLiteral("L1")}));
    CHECK(windowRows() == 0);
}

int main(int argc, char *argv[])
{
    if (argc > 1 && qstrcmp(argv[1], "--child") == 0) {
        if (argc != 4) {
            return 2;
        }
        return childMain(argc, argv);
    }
    if (argc > 1 && qstrcmp(argv[1], "--startup-child") == 0) {
        if (argc != 5) {
            return 2;
        }
        return startupChildMain(argc, argv);
    }

    // Advertise the persistent pointer before Qt initializes its Wayland seat.
    // Injector roundtrips cannot make a later Qt get_pointer precede a click.
    krema::test::FakePointer input("krema-launch-in-place-test", "Click the launcher surface to request a real startup token");
    if (!input.isValid()) {
        qCritical("Startup fixture prerequisite failed: fake-input device unavailable");
        return 2;
    }
    g_pointer = &input;

    // KIO requests a real Wayland token from the launcher's QWindow. A layer
    // surface models the dock without adding a task to the expected order.
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");
    QApplication application(argc, argv);
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        qCritical("Startup fixture prerequisite failed: no screen for parent layer-shell surface");
        return 2;
    }
    g_launcherClickRect = QRect(screen->geometry().topLeft(), QSize(8, 8));
    QMainWindow launcherSurface;
    launcherSurface.setWindowFlags(Qt::FramelessWindowHint);
    launcherSurface.resize(g_launcherClickRect.size());
    launcherSurface.winId();
    launcherSurface.windowHandle()->setScreen(screen);
    g_launcherSurface = &launcherSurface;
    auto *layer = LayerShellQt::Window::get(launcherSurface.windowHandle());
    if (!layer) {
        qCritical("Startup fixture prerequisite failed: no parent layer-shell surface");
        return 2;
    }
    layer->setScope(QStringLiteral("krema-startup-fixture"));
    layer->setAnchors(LayerShellQt::Window::Anchors{LayerShellQt::Window::AnchorTop} | LayerShellQt::Window::AnchorLeft);
    layer->setLayer(LayerShellQt::Window::LayerTop);
    layer->setExclusiveZone(0);
#ifdef KREMA_COMPAT_NO_LAYERSHELL_SCREEN
    layer->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
#else
    layer->setScreen(screen);
#endif
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    launcherSurface.setMask(QRegion(launcherSurface.rect()));
    launcherSurface.show();
    // The shell integration is initialized in this process. Child launches
    // must keep using their ordinary xdg-shell windows.
    qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");
    if (!QTest::qWaitFor(
            [&launcherSurface] {
                return launcherSurface.windowHandle()->isExposed();
            },
            kTimeoutMs)) {
        qCritical("Startup fixture prerequisite failed: parent layer-shell surface was not exposed");
        return 2;
    }
    launcherSurface.repaint();
    QGuiApplication::sync();
    if (!installDesktopEntries()) {
        qCritical("Startup fixture prerequisite failed: desktop entries or service cache could not be installed");
        return 2;
    }
    // Only the dock-like layer exists in this process; it must not appear as
    // a task when libtaskmanager completes its initial Wayland round trip.
    model();
    QGuiApplication::sync();
    if (!settleEvents() || windowRows() != 0) {
        qCritical("Startup fixture prerequisite failed: parent layer surface appeared as a window task");
        return 2;
    }
    return Catch::Session().run(argc, argv);
}
