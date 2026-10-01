// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Exercises automatic publication from the real dock QML. A scripted effect
// reads EffectWindow.iconGeometry inside KWin, independently of libtaskmanager.
// Run under run-with-kwin.sh with KREMA_TEST_KWIN_EFFECTS=tests/kwin/effects.

#include "krema.h"
#include "models/dockmodel.h"
#include "models/launcherentrytracker.h"
#include "models/notificationtracker.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/dockvisibilitycontroller.h"
#include "shell/multidockmanager.h"
#include "shell/outputordermonitor.h"
#include <taskmanager/abstracttasksmodel.h>

#include <KAboutData>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QQuickItem>
#include <QPointer>
#include <QQuickStyle>
#include <QRasterWindow>
#include <QScopeGuard>
#include <QSet>
#include <QTest>
#include <QtQml>

#include <memory>

static void initResources()
{
    Q_INIT_RESOURCE(qml);
}

namespace
{

using TaskManager::AbstractTasksModel;
constexpr int kTimeoutMs = 15000;
constexpr auto kWindowArg = "--geometry-window";
const QString kWindowA = QStringLiteral("krema-geometry-A");
const QString kWindowB = QStringLiteral("krema-geometry-B");

struct App {
    std::unique_ptr<KremaSettings> settings;
    std::unique_ptr<krema::DockModel> model;
    std::unique_ptr<krema::NotificationTracker> tracker;
    std::unique_ptr<krema::LauncherEntryTracker> launcherEntries;
};

// The same QML singletons as Application::run() and the other shell tests.
App &app()
{
    static App *instance = [] {
        auto *a = new App;
        a->settings = std::make_unique<KremaSettings>();
        a->settings->load();
        a->settings->setMonitorMode(krema::MultiDockManager::PrimaryOnly);
        a->model = std::make_unique<krema::DockModel>();
        a->tracker = std::make_unique<krema::NotificationTracker>();
        a->launcherEntries = std::make_unique<krema::LauncherEntryTracker>();

        auto *model = a->model.get();
        qmlRegisterSingletonType<krema::DockModel>("com.bhyoo.krema", 1, 0, "DockModel", [model](QQmlEngine *, QJSEngine *) -> QObject * {
            QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
            return model;
        });
        auto *settings = a->settings.get();
        qmlRegisterSingletonType<KremaSettings>("com.bhyoo.krema", 1, 0, "DockSettings", [settings](QQmlEngine *, QJSEngine *) -> QObject * {
            QQmlEngine::setObjectOwnership(settings, QQmlEngine::CppOwnership);
            return settings;
        });
        auto *tracker = a->tracker.get();
        qmlRegisterSingletonType<krema::NotificationTracker>("com.bhyoo.krema", 1, 0, "NotificationTracker", [tracker](QQmlEngine *, QJSEngine *) -> QObject * {
            QQmlEngine::setObjectOwnership(tracker, QQmlEngine::CppOwnership);
            return tracker;
        });
        auto *launcherEntries = a->launcherEntries.get();
        qmlRegisterSingletonType<krema::LauncherEntryTracker>("com.bhyoo.krema",
                                                              1,
                                                              0,
                                                              "LauncherEntryTracker",
                                                              [launcherEntries](QQmlEngine *, QJSEngine *) -> QObject * {
                                                                  QQmlEngine::setObjectOwnership(launcherEntries, QQmlEngine::CppOwnership);
                                                                  return launcherEntries;
                                                              });
        return a;
    }();
    return *instance;
}

struct ProbeWindow {
    QString caption;
    bool dock = false;
    bool normal = false;
    bool managed = false;
    QRectF frame;
    QRectF icon;
};

struct Snapshot {
    int sequence = 0;
    QList<ProbeWindow> windows;
    QByteArray json;
};

QRectF readRect(const QJsonObject &object)
{
    return {object.value(QStringLiteral("x")).toDouble(),
            object.value(QStringLiteral("y")).toDouble(),
            object.value(QStringLiteral("width")).toDouble(),
            object.value(QStringLiteral("height")).toDouble()};
}

Snapshot latestSnapshot()
{
    QFile log(qEnvironmentVariable("KREMA_TEST_KWIN_LOG"));
    if (!log.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QByteArray marker("KREMA_GEOMETRY ");
    Snapshot latest;
    while (!log.atEnd()) {
        const QByteArray line = log.readLine();
        const qsizetype at = line.indexOf(marker);
        if (at < 0) {
            continue;
        }
        // One JSON record per snapshot: a partial log write cannot masquerade
        // as a fresh snapshot with a missing window or an empty rectangle.
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(line.mid(at + marker.size()), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            continue;
        }
        const auto object = document.object();
        const int sequence = object.value(QStringLiteral("sequence")).toInt();
        if (sequence <= latest.sequence) {
            continue;
        }
        latest.sequence = sequence;
        latest.windows.clear();
        latest.json = line.mid(at + marker.size()).trimmed();
        for (const auto &value : object.value(QStringLiteral("windows")).toArray()) {
            const auto window = value.toObject();
            latest.windows.append({window.value(QStringLiteral("caption")).toString(),
                                   window.value(QStringLiteral("dock")).toBool(),
                                   window.value(QStringLiteral("normal")).toBool(),
                                   window.value(QStringLiteral("managed")).toBool(),
                                   readRect(window.value(QStringLiteral("frame")).toObject()),
                                   readRect(window.value(QStringLiteral("icon")).toObject())});
        }
    }
    return latest;
}

bool setShowingDesktop(bool showing)
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                 QStringLiteral("/KWin"),
                                                 QStringLiteral("org.kde.KWin"),
                                                 QStringLiteral("showDesktop"));
    message << showing;
    return QDBusConnection::sessionBus().call(message, QDBus::Block, kTimeoutMs).type() == QDBusMessage::ReplyMessage;
}

bool takeSnapshot(Snapshot &snapshot)
{
    const int previous = latestSnapshot().sequence;
    // End each observation with Show Desktop off. The false transition is
    // the newest snapshot, and the control remains an ordinary mapped window.
    if (!setShowingDesktop(true) || !setShowingDesktop(false)) {
        return false;
    }
    return QTest::qWaitFor(
        [&] {
            snapshot = latestSnapshot();
            return snapshot.sequence >= previous + 2;
        },
        kTimeoutMs);
}

const ProbeWindow *findWindow(const Snapshot &snapshot, const QString &caption)
{
    for (const auto &window : snapshot.windows) {
        if (window.caption == caption) {
            return &window;
        }
    }
    return nullptr;
}

// Always resolve fresh rows: adding a child can replace a single task with a
// group parent. Passing the parent unchanged must publish to both children.
int taskRow(const QSet<QString> &titles)
{
    auto *tasks = app().model->tasksModel();
    for (int row = 0; row < tasks->rowCount(); ++row) {
        const auto index = tasks->index(row, 0);
        if (!index.data(TaskManager::AbstractTasksModel::IsWindow).toBool()) {
            continue;
        }
        QSet<QString> found;
        if (tasks->rowCount(index) == 0) {
            found.insert(index.data(Qt::DisplayRole).toString());
        } else {
            for (int child = 0; child < tasks->rowCount(index); ++child) {
                found.insert(tasks->index(child, 0, index).data(Qt::DisplayRole).toString());
            }
        }
        if (found == titles) {
            return row;
        }
    }
    return -1;
}

QRectF restSlot(QQuickView *view, QQuickItem *delegate)
{
    auto *row = delegate ? delegate->parentItem() : nullptr;
    auto *panel = row ? row->parentItem() : nullptr;
    auto *root = view ? qobject_cast<QQuickItem *>(view->rootObject()) : nullptr;
    if (!row || !panel || !root) {
        return {};
    }

    // Keep this calculation independent from the geometry proxy. It mirrors
    // the panel's fixed rest slot from the surface dimensions, edge and
    // floating padding, then uses the real Flow/delegate slot dimensions.
    constexpr int kFloatingMargin = 8; // DockView::s_floatingMargin
    const int floatingPadding = app().settings->floating() ? kFloatingMargin : 0;
    const int edge = app().settings->edge();
    const qreal panelX = (edge == 2 || edge == 3)
        ? (edge == 2 ? floatingPadding : root->width() - panel->width() - floatingPadding)
        : (root->width() - panel->width()) / 2.0;
    const qreal panelY = (edge == 0 || edge == 1)
        ? (edge == 0 ? floatingPadding : root->height() - panel->height() - floatingPadding)
        : (root->height() - panel->height()) / 2.0;
    const QPointF rowOrigin(row->x(), row->y());
    const QPointF delegateOrigin(delegate->x(), delegate->y());
    return {panelX + rowOrigin.x() + delegateOrigin.x(),
            panelY + rowOrigin.y() + delegateOrigin.y(),
            delegate->width(),
            delegate->height()};
}

QQuickItem *findDelegate(QQuickItem *root, int row)
{
    for (auto *child : root->childItems()) {
        if (child->property("itemCenterX").isValid() && child->property("displayName").isValid() && child->property("index").toInt() == row) {
            return child;
        }
        if (auto *delegate = findDelegate(child, row)) {
            return delegate;
        }
    }
    return nullptr;
}

QRectF waitForPublished(krema::MultiDockManager &manager, const QSet<QString> &titles)
{
    QRectF previous;
    int stableMatches = 0;
    Snapshot snapshot;
    const bool published = QTest::qWaitFor(
        [&] {
            // takeSnapshot dispatches Qt events. Resolve shell and delegate
            // afterwards so a layout/model change cannot leave stale pointers.
            if (!takeSnapshot(snapshot)) {
                return false;
            }
            auto *shell = manager.primaryShell();
            auto *view = shell ? shell->view() : nullptr;
            const int row = taskRow(titles);
            auto *delegate = view && view->isExposed() && view->rootObject() && row >= 0 ? findDelegate(view->rootObject(), row) : nullptr;
            if (!delegate || !delegate->parentItem() || delegate->width() <= 0 || delegate->height() <= 0) {
                stableMatches = 0;
                return false;
            }
            const ProbeWindow *dock = nullptr;
            for (const auto &window : snapshot.windows) {
                if (window.dock && window.frame.size() == QSizeF(view->size())) {
                    if (dock) {
                        stableMatches = 0;
                        return false;
                    }
                    dock = &window;
                }
            }
            if (!dock) {
                stableMatches = 0;
                return false;
            }
            // KWin receives surface-local coordinates from the rest-slot
            // proxy; the probe reports screen coordinates, so add the actual
            // layer-surface frame origin.
            const QRectF expected = restSlot(view, delegate).translated(dock->frame.topLeft());
            if (expected != previous) {
                previous = expected;
                stableMatches = 0;
            }
            bool matches = true;
            for (const auto &title : titles) {
                const auto *window = findWindow(snapshot, title);
                if (!window || !window->normal || !window->managed || window->icon.isEmpty() || window->icon != expected) {
                    matches = false;
                    break;
                }
            }
            if (!matches) {
                stableMatches = 0;
                return false;
            }
            ++stableMatches;
            return stableMatches >= 3;
        },
        kTimeoutMs);
    INFO("expected icon geometry: " << previous.x() << "," << previous.y() << " " << previous.width() << "x" << previous.height());
    INFO("KWin snapshot: " << snapshot.json.toStdString());
    REQUIRE(published);
    return previous;
}

void startWindow(QProcess &process, const QString &title)
{
    auto env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("QT_WAYLAND_SHELL_INTEGRATION"));
    process.setProcessEnvironment(env);
    process.start(QCoreApplication::applicationFilePath(), {QLatin1String(kWindowArg), title});
    REQUIRE(process.waitForStarted(kTimeoutMs));
}

// Same ordinary xdg-toplevel fixture as test_show_desktop.cpp. No layer-shell
// override, injected icon geometry, fake task model, or mocked compositor.
int runWindow(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("krema-geometry-control"));
    class Window : public QRasterWindow
    {
    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter(this).fillRect(QRect(QPoint(), size()), Qt::darkCyan);
        }
    } window;
    window.setTitle(QString::fromLocal8Bit(argv[2]));
    window.resize(320, 240);
    window.show();
    return application.exec();
}

} // namespace

TEST_CASE("KWin uses dock delegate geometry for managed windows", "[delegate-geometry]")
{
    auto load = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                               QStringLiteral("/Effects"),
                                               QStringLiteral("org.kde.kwin.Effects"),
                                               QStringLiteral("loadEffect"));
    load << QStringLiteral("kremageometryprobe");
    const QDBusReply<bool> loaded = QDBusConnection::sessionBus().call(load, QDBus::Block, kTimeoutMs);
    REQUIRE(loaded.isValid());
    REQUIRE(loaded.value());
    const auto restoreDesktop = qScopeGuard([] {
        setShowingDesktop(false);
    });

    QProcess first;
    QProcess second;
    const auto stopWindows = qScopeGuard([&] {
        for (auto *process : {&second, &first}) {
            if (process->state() != QProcess::NotRunning) {
                process->kill();
                process->waitForFinished(kTimeoutMs);
            }
        }
    });
    startWindow(first, kWindowA);

    // Healthy control: the same KWin-managed ordinary window starts without
    // an animation target before any dock surface exists.
    Snapshot snapshot;
    REQUIRE(QTest::qWaitFor(
        [&] {
            if (!takeSnapshot(snapshot)) {
                return false;
            }
            const auto *window = findWindow(snapshot, kWindowA);
            return window && window->normal && window->managed && window->frame.isValid() && window->icon.isEmpty();
        },
        kTimeoutMs));

    REQUIRE(QTest::qWaitFor([] { return krema::OutputOrderMonitor::instance()->orderReady(); }, kTimeoutMs));
    auto manager = std::make_unique<krema::MultiDockManager>(app().settings.get(), app().model.get(), app().tracker.get());
    manager->initialize();
    const QRectF initial = waitForPublished(*manager, {kWindowA});
    CHECK_FALSE(initial.isEmpty());

    // Parent surface placement and delegate layout changes must refresh the
    // compositor's target without any explicit geometry call from this test.
    app().settings->setEdge(static_cast<int>(krema::DockPlatform::Edge::Left));
    const QRectF moved = waitForPublished(*manager, {kWindowA});
    CHECK(moved != initial);
    app().settings->setIconSize(app().settings->iconSize() + 16);
    const QRectF resized = waitForPublished(*manager, {kWindowA});
    CHECK(resized.size() != moved.size());

    // A hidden AutoHide dock must publish the stable visible slot for a newly
    // opened window, before the edge reveal occurs.
    auto *visibility = manager->primaryShell()->view()->visibilityController();
    REQUIRE(visibility);
    visibility->setMode(krema::DockPlatform::VisibilityMode::AutoHide);
    visibility->setHovered(false);
    REQUIRE(QTest::qWaitFor([&] { return !visibility->isDockVisible(); }, kTimeoutMs));

    startWindow(second, kWindowB);
    const QRectF hidden = waitForPublished(*manager, {kWindowA, kWindowB});
    CHECK_FALSE(hidden.isEmpty());
    CHECK_FALSE(visibility->isDockVisible());

    visibility->setHovered(true);
    REQUIRE(QTest::qWaitFor([&] { return visibility->isDockVisible(); }, kTimeoutMs));
    const QRectF revealed = waitForPublished(*manager, {kWindowA, kWindowB});
    CHECK(revealed == hidden);
    const int group = taskRow({kWindowA, kWindowB});
    REQUIRE(group >= 0);
    CHECK(app().model->tasksModel()->index(group, 0).data(TaskManager::AbstractTasksModel::IsGroupParent).toBool());
    auto *groupView = manager->primaryShell()->view();
    auto *groupDelegate = groupView && groupView->rootObject() ? findDelegate(groupView->rootObject(), group) : nullptr;
    QPointer<QQuickItem> groupProxy;
    if (groupDelegate) {
        groupProxy = qobject_cast<QQuickItem *>(groupDelegate->property("delegateGeometryTarget").value<QObject *>());
    }
    REQUIRE(groupProxy);

    // Destroying the dock surface lets KWin clear its geometry association.
    // Keep both clients alive: disappearance of their windows is not cleanup.
    manager.reset();
    REQUIRE(QTest::qWaitFor([&] { return groupProxy.isNull(); }, kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [&] {
            if (!takeSnapshot(snapshot)) {
                return false;
            }
            for (const auto &window : snapshot.windows) {
                if (window.dock) {
                    return false;
                }
            }
            for (const auto &title : {kWindowA, kWindowB}) {
                const auto *window = findWindow(snapshot, title);
                if (!window || !window->normal || !window->managed || !window->frame.isValid() || !window->icon.isEmpty()) {
                    return false;
                }
            }
            return true;
        },
        kTimeoutMs));
    CHECK(first.state() == QProcess::Running);
    CHECK(second.state() == QProcess::Running);
}

int main(int argc, char *argv[])
{
    if (argc > 2 && qstrcmp(argv[1], kWindowArg) == 0) {
        return runWindow(argc, argv);
    }
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");
    QApplication application(argc, argv);
    if (QQuickStyle::name().isEmpty()) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }
    KAboutData aboutData(QStringLiteral("krema"), QStringLiteral("Krema"), QStringLiteral("test"));
    KAboutData::setApplicationData(aboutData);
    initResources();
    return Catch::Session().run(argc, argv);
}
