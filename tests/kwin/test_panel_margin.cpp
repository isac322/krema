// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// The floating gap is measured from the panels on the dock's edge (issue #90).
//
// Drives the real MultiDockManager against a KWin virtual compositor. A
// stand-in panel runs in a child process (this binary in helper mode), so
// KWin reports it under a pid other than Krema's, exactly like plasmashell:
// a 500x60 layer surface on the Top layer, namespace "dock", anchored to the
// bottom edge only, reserving 44 px (a floating Plasma panel: the surface
// also holds the panel's own floating gap). A second stand-in reserves
// nothing, like an auto-hide, dodge or windows-go-below panel.
//
// Moving the gap reference moves the Always visible reservation between the
// dock surface and its probe. An ordinary window from another helper process,
// maximized by KWin, must end at exactly one reservation after every switch
// and after Krema restarts, not at a full-output or doubly reserved size.
//
// An auto-hide dock past the panel is revealed from the outer screen edge
// (watched by a KWin script) or, with Reveal trigger set to Dock position,
// from the strip past the panels' band. Real pointer motion and clicks go
// through KWin's fake-input protocol; the stand-in panel logs the presses it
// receives, so the test sees that the dock never takes the panel's clicks.
//
// Geometry comes from KWin itself: a one-shot KWin script (loaded through
// org.kde.kwin.Scripting) reports every dock-type window's frame and each
// output's MaximizeArea back to this process over D-Bus. The dock bar's
// screen rect is the dock surface's KWin frame plus the panel rect QML
// reports to DockVisibilityController (surface-local).
//
// Must run under run-with-kwin.sh, which provides WAYLAND_DISPLAY, a private
// session bus and throwaway XDG directories.

// Test-only access to the platform's private handoff state. Include all
// headers pulled by the platform header before redefining private.
#include "fakepointer.h"
#include <QMargins>
#include <QPointer>
#include <QRegion>
#include <QScreen>
#include <QSize>
#include <QWindow>
#include <memory>
#include <sstream>
#define private public
#include "platform/waylanddockplatform.h"
#undef private
#include "krema.h"
#include "config/screensettings.h"

#include "models/dockmodel.h"
#include "models/launcherentrytracker.h"
#include "models/notificationtracker.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/dockvisibilitycontroller.h"
#include "shell/multidockmanager.h"
#include "shell/outputordermonitor.h"
#include "utils/surfacegeometry.h"

#include <KAboutData>
#include <LayerShellQt/Window>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QElapsedTimer>
#include <QExposeEvent>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPlatformSurfaceEvent>
#include <QProcess>
#include <QQuickStyle>
#include <QRasterWindow>
#include <QScopeGuard>
#include <QScreen>
#include <QSet>
#include <QTemporaryFile>
#include <QTest>
#include <QtQml>

#include <cmath>
#include <memory>
#include <optional>
#include <vector>

static void initResources()
{
    Q_INIT_RESOURCE(qml);
}

// Named, not anonymous: KWinQuery below goes through moc.
namespace panelmargin
{

constexpr int kTimeoutMs = 15000;
constexpr int kQueryTimeoutMs = 5000;
constexpr auto kPanelArg = "--stand-in-panel";
constexpr auto kMaximizedArg = "--maximized-window";
constexpr auto kMaximizedTitle = "krema-panel-margin-maximized";

// DockView::s_padding (src/shell/dockview.h), private there.
constexpr int kDockPadding = 8;

// The stand-in panel: a floating Plasma panel's surface and reservation.
constexpr int kPanelLength = 500;
constexpr int kPanelDepth = 60;
constexpr int kPanelZone = 44;

constexpr int kEdgeBottom = 1;

struct App {
    std::unique_ptr<KremaSettings> settings;
    std::unique_ptr<krema::DockModel> model;
    std::unique_ptr<krema::NotificationTracker> tracker;
    std::unique_ptr<krema::LauncherEntryTracker> launcherEntries;
};

// Mirrors the QML singletons Application::run() registers.
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

// Always visible on the selected edge, with the given gap reference, margin,
// floating state, and reservation state. Set before the manager exists, or live.
void configure(int reference, int margin, int edge = kEdgeBottom, bool floating = true, bool reserve = true)
{
    auto *s = app().settings.get();
    s->setEdge(edge);
    s->setVisibilityMode(static_cast<int>(krema::DockPlatform::VisibilityMode::AlwaysVisible));
    s->setReserveScreenSpace(reserve);
    s->setFloating(floating);
    s->setShadowEnabled(false);
    s->setFloatingMargin(margin);
    s->setFloatingMarginReference(reference);
}

// ---------------------------------------------------------------------------
// KWin's view of the session
// ---------------------------------------------------------------------------

struct OutputState {
    QRect geometry;
    QRect maximizeArea;
};

struct DockWindow {
    qint64 pid = 0;
    QString output;
    QRect frame;
};

/// An ordinary (xdg toplevel) window as KWin reports it.
struct NormalWindow {
    qint64 pid = 0;
    QString caption;
    QString output;
    QRect frame;
    /// clientArea(KWin.MaximizeArea, window).
    QRect maximizeArea;
};

struct Snapshot {
    QHash<QString, OutputState> outputs;
    QList<DockWindow> docks;
    QList<NormalWindow> normals;
};

QRect rectFrom(const QJsonValue &value)
{
    const QJsonArray a = value.toArray();
    if (a.size() != 4) {
        return {};
    }
    return QRect(std::lround(a.at(0).toDouble()), std::lround(a.at(1).toDouble()), std::lround(a.at(2).toDouble()), std::lround(a.at(3).toDouble()));
}

// One-shot report of every dock-type and normal window and each output's work
// area, after running an optional action (%5, a JS statement block).
// %1 service, %2 object path, %3 interface, %4 token.
const char kQueryScript[] = R"JS(
function rectOf(g) {
    return [Math.round(g.x), Math.round(g.y), Math.round(g.width), Math.round(g.height)];
}
(function () {
%5
})();
var outputs = {};
var screens = workspace.screens;
for (var i = 0; i < screens.length; ++i) {
    outputs[screens[i].name] = {
        geometry: rectOf(screens[i].geometry),
        maximizeArea: rectOf(workspace.clientArea(KWin.MaximizeArea, screens[i], workspace.currentDesktop))
    };
}
var docks = [];
var normals = [];
var windows = workspace.windowList();
for (var j = 0; j < windows.length; ++j) {
    var w = windows[j];
    if (w.deleted) {
        continue;
    }
    if (w.dock) {
        docks.push({ pid: w.pid, output: w.output ? w.output.name : "", frame: rectOf(w.frameGeometry) });
    } else if (w.normalWindow) {
        normals.push({ pid: w.pid, caption: w.caption, output: w.output ? w.output.name : "",
                       frame: rectOf(w.frameGeometry), maximizeArea: rectOf(workspace.clientArea(KWin.MaximizeArea, w)) });
    }
}
callDBus("%1", "%2", "%3", "report", "%4", JSON.stringify({ outputs: outputs, docks: docks, normals: normals }));
)JS";

/// Runs kQueryScript in KWin and receives its report over D-Bus.
class KWinQuery : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.bhyoo.krema.test.PanelMargin")

public:
    static KWinQuery &instance()
    {
        static KWinQuery *query = new KWinQuery;
        return *query;
    }

    /// KWin's current state after running @p action (JS statements) in KWin;
    /// nullopt when KWin did not answer in time.
    std::optional<Snapshot> snapshot(const QString &action = {})
    {
        auto bus = QDBusConnection::sessionBus();
        if (!m_registered) {
            m_registered = bus.registerObject(QLatin1String(kPath), this, QDBusConnection::ExportScriptableSlots);
            if (!m_registered) {
                qWarning() << "Cannot register" << kPath << bus.lastError().message();
                return std::nullopt;
            }
        }

        const QString token = QString::number(++m_counter);
        QTemporaryFile file(QDir::tempPath() + QStringLiteral("/krema-panel-margin-test-XXXXXX.js"));
        if (!file.open()) {
            return std::nullopt;
        }
        file.write(QString::fromLatin1(kQueryScript)
                       .arg(bus.baseService(), QLatin1String(kPath), QStringLiteral("com.bhyoo.krema.test.PanelMargin"), token, action)
                       .toUtf8());
        file.flush();

        const QString plugin = QStringLiteral("krema-panel-margin-test-%1-%2").arg(QCoreApplication::applicationPid()).arg(token);
        auto load = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                   QStringLiteral("/Scripting"),
                                                   QStringLiteral("org.kde.kwin.Scripting"),
                                                   QStringLiteral("loadScript"));
        load << file.fileName() << plugin;
        const QDBusReply<int> id = bus.call(load, QDBus::Block, kQueryTimeoutMs);
        if (!id.isValid() || id.value() < 0) {
            qWarning() << "KWin refused the query script:" << id.error().message();
            return std::nullopt;
        }
        const auto unload = qScopeGuard([&] {
            auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                          QStringLiteral("/Scripting"),
                                                          QStringLiteral("org.kde.kwin.Scripting"),
                                                          QStringLiteral("unloadScript"));
            message << plugin;
            bus.call(message, QDBus::Block, kQueryTimeoutMs);
        });
        auto run = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                  QStringLiteral("/Scripting/Script%1").arg(id.value()),
                                                  QStringLiteral("org.kde.kwin.Script"),
                                                  QStringLiteral("run"));
        m_reply.reset();
        bus.call(run, QDBus::Block, kQueryTimeoutMs);

        const bool answered = QTest::qWaitFor(
            [&] {
                return m_reply.has_value() && m_reply->first == token;
            },
            kQueryTimeoutMs);
        if (!answered) {
            return std::nullopt;
        }
        return parse(m_reply->second);
    }

public Q_SLOTS:
    Q_SCRIPTABLE void report(const QString &token, const QString &json)
    {
        m_reply = std::make_pair(token, json);
    }

private:
    static constexpr auto kPath = "/com/bhyoo/krema/test/PanelMargin";

    static std::optional<Snapshot> parse(const QString &json)
    {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning() << "Malformed KWin report:" << error.errorString();
            return std::nullopt;
        }
        Snapshot snapshot;
        const QJsonObject outputs = doc.object().value(QStringLiteral("outputs")).toObject();
        for (auto it = outputs.begin(); it != outputs.end(); ++it) {
            const QJsonObject entry = it.value().toObject();
            snapshot.outputs.insert(it.key(),
                                    OutputState{rectFrom(entry.value(QStringLiteral("geometry"))), rectFrom(entry.value(QStringLiteral("maximizeArea")))});
        }
        const QJsonArray docks = doc.object().value(QStringLiteral("docks")).toArray();
        for (const QJsonValue &value : docks) {
            const QJsonObject entry = value.toObject();
            snapshot.docks.append(DockWindow{entry.value(QStringLiteral("pid")).toInteger(),
                                             entry.value(QStringLiteral("output")).toString(),
                                             rectFrom(entry.value(QStringLiteral("frame")))});
        }
        const QJsonArray normals = doc.object().value(QStringLiteral("normals")).toArray();
        for (const QJsonValue &value : normals) {
            const QJsonObject entry = value.toObject();
            snapshot.normals.append(NormalWindow{entry.value(QStringLiteral("pid")).toInteger(),
                                                 entry.value(QStringLiteral("caption")).toString(),
                                                 entry.value(QStringLiteral("output")).toString(),
                                                 rectFrom(entry.value(QStringLiteral("frame"))),
                                                 rectFrom(entry.value(QStringLiteral("maximizeArea")))});
        }
        return snapshot;
    }

    bool m_registered = false;
    int m_counter = 0;
    std::optional<std::pair<QString, QString>> m_reply;
};

// ---------------------------------------------------------------------------
// Helper processes (this binary in helper mode)
// ---------------------------------------------------------------------------

/// This binary started in a helper mode; killed on destruction.
class HelperProcess
{
public:
    HelperProcess(const QStringList &args, const QProcessEnvironment &env)
    {
        // Keep the helper's warnings in the test log.
        m_process.setProcessChannelMode(QProcess::ForwardedChannels);
        m_process.setProcessEnvironment(env);
        m_process.start(QCoreApplication::applicationFilePath(), args);
    }
    ~HelperProcess()
    {
        stop();
    }
    HelperProcess(const HelperProcess &) = delete;
    HelperProcess &operator=(const HelperProcess &) = delete;

    bool waitForStarted()
    {
        return m_process.waitForStarted(kTimeoutMs);
    }
    [[nodiscard]] qint64 pid() const
    {
        return m_process.processId();
    }
    void stop()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(kTimeoutMs);
        }
    }

private:
    QProcess m_process;
};

/// A panel-like layer surface on one output. With @p clickLog it appends a
/// line to that file for every mouse button press it receives.
class StandInPanel : public HelperProcess
{
public:
    StandInPanel(int zone, const QString &outputName, const QString &clickLog = {}, int edge = kEdgeBottom)
        : HelperProcess({QLatin1String(kPanelArg), QString::number(zone), QString::number(edge), outputName, clickLog},
                        QProcessEnvironment::systemEnvironment())
    {
    }
};

/// An ordinary application window (xdg toplevel).
class OrdinaryWindow : public HelperProcess
{
public:
    OrdinaryWindow()
        : HelperProcess({QLatin1String(kMaximizedArg)}, environment())
    {
    }

private:
    static QProcessEnvironment environment()
    {
        // main() forces the layer-shell integration for Krema; an inherited
        // override would make this window a layer surface, not a toplevel.
        auto env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("QT_WAYLAND_SHELL_INTEGRATION"));
        return env;
    }
};

int runOrdinaryWindow(int argc, char *argv[])
{
    qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");
    QGuiApplication application(argc, argv);
    QGuiApplication::setDesktopFileName(QLatin1String(kMaximizedTitle));
    class Window : public QRasterWindow
    {
    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter(this).fillRect(QRect(QPoint(), size()), Qt::darkCyan);
        }
    } window;
    window.setTitle(QLatin1String(kMaximizedTitle));
    window.resize(320, 240);
    window.show();
    return application.exec();
}

int runStandInPanel(int argc, char *argv[])
{
    // The panel is a layer surface like the dock; the parent forces the same
    // integration, set it here too so the helper never depends on inheritance.
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");
    QGuiApplication application(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 5) {
        return 2;
    }
    const int zone = args.at(2).toInt();
    const int edge = args.at(3).toInt();
    const QString outputName = args.at(4);
    // Optional: a file that gets one line per mouse button press.
    const QString clickLog = args.value(5);

    QScreen *screen = QGuiApplication::primaryScreen();
    for (QScreen *candidate : QGuiApplication::screens()) {
        if (candidate->name() == outputName) {
            screen = candidate;
        }
    }

    class Panel : public QRasterWindow
    {
    public:
        QString clickLog;

    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter(this).fillRect(QRect(QPoint(), size()), Qt::darkGray);
        }
        void mousePressEvent(QMouseEvent *event) override
        {
            if (clickLog.isEmpty()) {
                return;
            }
            QFile file(clickLog);
            if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                const QPoint pos = event->position().toPoint();
                file.write(QStringLiteral("%1,%2\n").arg(pos.x()).arg(pos.y()).toUtf8());
            }
        }
    } window;
    window.clickLog = clickLog;

    const bool vertical = edge == 2 || edge == 3;
    const QSize size = vertical ? QSize(kPanelDepth, kPanelLength) : QSize(kPanelLength, kPanelDepth);
    window.setFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    window.setScreen(screen);
    window.setGeometry(QRect(screen->geometry().topLeft(), size));

    auto *layer = LayerShellQt::Window::get(&window);
    if (!layer) {
        return 3;
    }
    layer->setLayer(LayerShellQt::Window::LayerTop);
    // KWin types a layer surface a Dock only for this namespace.
    layer->setScope(QStringLiteral("dock"));
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    LayerShellQt::Window::Anchors anchors = LayerShellQt::Window::AnchorBottom;
    switch (edge) {
    case 0:
        anchors = LayerShellQt::Window::AnchorTop;
        break;
    case 2:
        anchors = LayerShellQt::Window::AnchorLeft;
        break;
    case 3:
        anchors = LayerShellQt::Window::AnchorRight;
        break;
    default:
        break;
    }
    layer->setAnchors(anchors);
    layer->setExclusiveZone(zone);
#ifndef KREMA_COMPAT_NO_LAYERSHELL_DESIRED_SIZE
    layer->setDesiredSize(size);
#endif
#ifdef KREMA_COMPAT_NO_LAYERSHELL_SCREEN
    layer->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
#else
    layer->setScreen(screen);
#endif
    window.show();
    return application.exec();
}

// ---------------------------------------------------------------------------
// Measurements
// ---------------------------------------------------------------------------

struct Placement {
    QString outputName;
    QRect output;
    QRect maximizeArea;
    /// Dock surface frame as KWin placed it.
    QRect dockFrame;
    /// Visible dock bar in global coordinates.
    QRect dockBar;
    /// Frames of the stand-in panels on the dock's output.
    QList<QRect> standIns;
    /// Ordinary windows on the dock's output.
    QList<NormalWindow> normals;

    [[nodiscard]] int outputBottom() const
    {
        return output.y() + output.height();
    }
    [[nodiscard]] int dockBottom() const
    {
        return dockBar.y() + dockBar.height();
    }
};

std::string describe(const std::optional<Placement> &p)
{
    if (!p) {
        return "no measurement (dock not mapped, no panel rect, or KWin did not answer)";
    }
    const auto rect = [](const QRect &r) {
        return QStringLiteral("[%1,%2 %3x%4]").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    };
    QString panels;
    for (const QRect &r : p->standIns) {
        panels += rect(r) + QLatin1Char(' ');
    }
    QString normals;
    for (const NormalWindow &w : p->normals) {
        normals += QStringLiteral("%1 (pid %2) frame %3 maximizeArea %4; ").arg(w.caption).arg(w.pid).arg(rect(w.frame), rect(w.maximizeArea));
    }
    return QStringLiteral("output %1 %2, maximizeArea %3, dock frame %4, dock bar %5 (bottom %6, output bottom %7), stand-ins %8, windows %9")
        .arg(p->outputName, rect(p->output), rect(p->maximizeArea), rect(p->dockFrame), rect(p->dockBar))
        .arg(p->dockBottom())
        .arg(p->outputBottom())
        .arg(panels.isEmpty() ? QStringLiteral("none") : panels)
        .arg(normals.isEmpty() ? QStringLiteral("none") : normals)
        .toStdString();
}

/// The dock's placement as KWin reports it, after KWin runs @p action.
std::optional<Placement> measureShell(krema::DockShell *shell,
                                      const QSet<qint64> &standInPids,
                                      const QString &action = {},
                                      bool requirePanelRect = true)
{
    if (!shell || !shell->view()->screen() || !shell->view()->isExposed()) {
        return std::nullopt;
    }
    auto *view = shell->view();
    const QRect panelRect = view->visibilityController()->panelRect();
    if (requirePanelRect && (panelRect.width() <= 0 || panelRect.height() <= 0)) {
        return std::nullopt;
    }
    const auto snapshot = KWinQuery::instance().snapshot(action);
    if (!snapshot) {
        return std::nullopt;
    }
    Placement p;
    p.outputName = view->screen()->name();
    if (!snapshot->outputs.contains(p.outputName)) {
        return std::nullopt;
    }
    p.output = snapshot->outputs.value(p.outputName).geometry;
    p.maximizeArea = snapshot->outputs.value(p.outputName).maximizeArea;
    const qint64 kremaPid = QCoreApplication::applicationPid();
    for (const DockWindow &w : snapshot->docks) {
        if (w.output != p.outputName) {
            continue;
        }
        if (w.pid == kremaPid && w.frame.size() == view->size()) {
            p.dockFrame = w.frame;
        } else if (standInPids.contains(w.pid)) {
            p.standIns.append(w.frame);
        }
    }
    if (p.dockFrame.isNull()) {
        return std::nullopt;
    }
    for (const NormalWindow &w : snapshot->normals) {
        if (w.output == p.outputName) {
            p.normals.append(w);
        }
    }
    p.dockBar = panelRect.translated(p.dockFrame.topLeft());
    return p;
}

std::optional<Placement> measure(krema::MultiDockManager &manager,
                                 const QSet<qint64> &standInPids,
                                 const QString &action = {},
                                 bool requirePanelRect = true)
{
    return measureShell(manager.primaryShell(), standInPids, action, requirePanelRect);
}

/// Waits until @p accept holds for a fresh measurement; reports the last one.
template<typename Accept>
void requireSettles(krema::MultiDockManager &manager, const QSet<qint64> &standInPids, const char *what, Accept accept)
{
    std::optional<Placement> last;
    const bool settled = QTest::qWaitFor(
        [&] {
            last = measure(manager, standInPids);
            return last && accept(*last);
        },
        kTimeoutMs);
    INFO(what << ": " << describe(last));
    REQUIRE(settled);
}

template<typename Accept>
void requireShellSettles(krema::DockShell *shell, const QSet<qint64> &standInPids, const char *what, Accept accept)
{
    std::optional<Placement> last;
    const bool settled = QTest::qWaitFor(
        [&] {
            last = measureShell(shell, standInPids);
            return last && accept(*last);
        },
        kTimeoutMs);
    INFO(what << ": " << describe(last));
    REQUIRE(settled);
}

/// Distance from the output edge to the outward side of @p rect.
int edgeGap(const QRect &output, const QRect &rect, int edge)
{
    switch (edge) {
    case 0:
        return rect.y() - output.y();
    case 1:
        return output.y() + output.height() - (rect.y() + rect.height());
    case 2:
        return rect.x() - output.x();
    case 3:
        return output.x() + output.width() - (rect.x() + rect.width());
    default:
        return -1;
    }
}

/// Distance between the inner side of @p panel and the outward side of @p dock.
int panelDockGap(const QRect &panel, const QRect &dock, int edge)
{
    switch (edge) {
    case 0:
        return dock.y() - (panel.y() + panel.height());
    case 1:
        return panel.y() - (dock.y() + dock.height());
    case 2:
        return dock.x() - (panel.x() + panel.width());
    case 3:
        return panel.x() - (dock.x() + dock.width());
    default:
        return -1;
    }
}

/// The stand-in panel is mapped and still flush with the selected edge.
bool standInUnpushed(const Placement &p, int edge = kEdgeBottom)
{
    if (p.standIns.size() != 1) {
        return false;
    }
    const QRect panel = p.standIns.first();
    if (edge == 0 || edge == 1) {
        return panel.height() == kPanelDepth && edgeGap(p.output, panel, edge) == 0;
    }
    return panel.width() == kPanelDepth && edgeGap(p.output, panel, edge) == 0;
}

/// KWin's maximize area remains on the content side of the visible dock bar.
bool reservationCoversDock(const Placement &p, int edge = kEdgeBottom)
{
    if (p.maximizeArea.isEmpty()) {
        return false;
    }
    switch (edge) {
    case 0:
        return p.maximizeArea.y() >= p.dockBar.y() + p.dockBar.height();
    case 1:
        return p.maximizeArea.y() + p.maximizeArea.height() <= p.dockBar.y();
    case 2:
        return p.maximizeArea.x() >= p.dockBar.x() + p.dockBar.width();
    case 3:
        return p.maximizeArea.x() + p.maximizeArea.width() <= p.dockBar.x();
    default:
        return false;
    }
}

std::unique_ptr<krema::MultiDockManager> startDock()
{
    // Wait until the Plasma output order is known before creating shells, as
    // Application::run() does; otherwise primaryOutputChanged() fires mid-test
    // and processTopologyUpdate() destroys and recreates the shells under
    // local view/visibility pointers.
    REQUIRE(QTest::qWaitFor(
        [] {
            return krema::OutputOrderMonitor::instance()->orderReady();
        },
        kTimeoutMs));
    auto manager = std::make_unique<krema::MultiDockManager>(app().settings.get(), app().model.get(), app().tracker.get());
    manager->initialize();
    return manager;
}

/// KWin script statements: move the window of @p pid to @p outputName (when
/// elsewhere) and maximize it, as the user would.
QString maximizeAction(qint64 pid, const QString &outputName)
{
    return QStringLiteral(R"JS(
    var ws = workspace.windowList();
    var target = null;
    for (var k = 0; k < ws.length; ++k) {
        if (ws[k].pid === PID && ws[k].normalWindow && !ws[k].deleted) {
            target = ws[k];
        }
    }
    var out = null;
    for (var s = 0; s < workspace.screens.length; ++s) {
        if (workspace.screens[s].name === "OUTPUT") {
            out = workspace.screens[s];
        }
    }
    if (!target || !out) {
        return;
    }
    if (!target.output || target.output.name !== out.name) {
        var g = out.geometry;
        target.frameGeometry = { x: g.x + 40, y: g.y + 40, width: 320, height: 240 };
    }
    target.setMaximize(true, true);
)JS")
        .replace(QLatin1String("PID"), QString::number(pid))
        .replace(QLatin1String("OUTPUT"), outputName);
}

/// The window of @p pid on the dock's output, if KWin reported it there.
std::optional<NormalWindow> windowOf(const Placement &p, qint64 pid)
{
    for (const NormalWindow &w : p.normals) {
        if (w.pid == pid) {
            return w;
        }
    }
    return std::nullopt;
}

/// Samples KWin's maximized work-area top inset on every short event-loop turn.
QList<int> sampleMaximizedTop(krema::MultiDockManager &manager, qint64 pid, const QSet<qint64> &, int durationMs)
{
    QList<int> samples;
    auto *shell = manager.primaryShell();
    const QString outputName = shell && shell->view()->screen() ? shell->view()->screen()->name() : QString();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < durationMs) {
        const auto snapshot = KWinQuery::instance().snapshot();
        if (snapshot && snapshot->outputs.contains(outputName)) {
            const QRect output = snapshot->outputs.value(outputName).geometry;
            for (const auto &normal : snapshot->normals) {
                if (normal.pid == pid) {
                    samples.append(normal.maximizeArea.y() - output.y());
                    break;
                }
            }
        }
        QTest::qWait(1);
    }
    return samples;
}

/// The maximized window fills its work area, and the work area holds exactly
/// one dock reservation: the resting panel bar plus @p margin (no other
/// panel reserves space here).
bool maximizedFitsOneReservation(const Placement &p, qint64 pid, int margin)
{
    const auto w = windowOf(p, pid);
    if (!w || w->maximizeArea.isEmpty()) {
        return false;
    }
    const int reserved = krema::panelBarHeight(app().settings->iconSize(), kDockPadding, margin);
    return w->frame == w->maximizeArea && w->maximizeArea.y() + w->maximizeArea.height() == p.outputBottom() - reserved && reservationCoversDock(p);
}

// ---------------------------------------------------------------------------
// Real pointer input
// ---------------------------------------------------------------------------

/// Mouse button presses the stand-in panel logged to @p path.
int clickCount(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    return static_cast<int>(file.readAll().count('\n'));
}

/// @p condition holds throughout the next @p ms milliseconds.
template<typename Condition>
bool holdsFor(int ms, Condition condition)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        if (!condition()) {
            return false;
        }
        QTest::qWait(20);
    }
    return condition();
}

} // namespace panelmargin

using namespace panelmargin;

TEST_CASE("Panels mode measures the gap from a panel started after Krema", "[panel-margin]")
{
    configure(1, 8);
    const auto restore = qScopeGuard([] {
        configure(1, 8);
    });

    auto manager = startDock();
    REQUIRE(QTest::qWaitFor(
        [&] {
            auto *shell = manager->primaryShell();
            return shell && shell->view()->isExposed() && shell->view()->screen();
        },
        kTimeoutMs));
    const QString outputName = manager->primaryShell()->view()->screen()->name();

    // No panel: Panels measures from the screen edge.
    requireSettles(*manager, {}, "no panel, Panels", [](const Placement &p) {
        return edgeGap(p.output, p.dockBar, kEdgeBottom) == 8 && reservationCoversDock(p);
    });

    // An always-visible floating panel appears on the dock's edge: the dock
    // moves past the panel's whole surface, the panel stays where it is, and
    // maximized windows still end above the dock.
    auto panel = std::make_unique<StandInPanel>(kPanelZone, outputName);
    REQUIRE(panel->waitForStarted());
    QSet<qint64> pids{panel->pid()};
    requireSettles(*manager, pids, "reserving panel, Panels, margin 8", [](const Placement &p) {
        return standInUnpushed(p) && panelDockGap(p.standIns.first(), p.dockBar, kEdgeBottom) == 8 && reservationCoversDock(p);
    });

    // Input: the panel's band below the dock stays click-through, the dock
    // bar itself takes input.
    {
        auto *view = manager->primaryShell()->view();
        QRegion mask;
        QRect panelRect;
        const QRect band(view->width() / 2 - 1, view->height() - kPanelDepth, 2, kPanelDepth);
        const bool settled = QTest::qWaitFor(
            [&] {
                mask = view->mask();
                panelRect = view->visibilityController()->panelRect();
                return !mask.isEmpty() && mask.intersected(band).isEmpty() && mask.contains(panelRect.center());
            },
            kTimeoutMs);
        INFO("surface " << view->width() << "x" << view->height() << ", mask bounding rect " << mask.boundingRect().x() << "," << mask.boundingRect().y() << " "
                        << mask.boundingRect().width() << "x" << mask.boundingRect().height() << ", panel rect " << panelRect.x() << "," << panelRect.y() << " "
                        << panelRect.width() << "x" << panelRect.height());
        REQUIRE(settled);
    }

    // Screen edge: back to the old placement, the margin from the screen edge.
    app().settings->setFloatingMarginReference(0);
    requireSettles(*manager, pids, "reserving panel, Screen edge, margin 8", [](const Placement &p) {
        return edgeGap(p.output, p.dockBar, kEdgeBottom) == 8 && reservationCoversDock(p);
    });

    // Panels again with a wider margin, applied live.
    app().settings->setFloatingMarginReference(1);
    app().settings->setFloatingMargin(40);
    requireSettles(*manager, pids, "reserving panel, Panels, margin 40", [](const Placement &p) {
        return standInUnpushed(p) && panelDockGap(p.standIns.first(), p.dockBar, kEdgeBottom) == 40 && reservationCoversDock(p);
    });

    // A panel that reserves nothing is ignored: the gap is measured from the
    // screen edge again. Plasma gives auto hide, dodge and windows-go-below
    // panels exclusive zone -1 (plasma-workspace shell/panelview.cpp).
    app().settings->setFloatingMargin(8);
    panel.reset();
    panel = std::make_unique<StandInPanel>(-1, outputName);
    REQUIRE(panel->waitForStarted());
    pids = {panel->pid()};
    requireSettles(*manager, pids, "non-reserving panel, Panels, margin 8", [](const Placement &p) {
        return standInUnpushed(p) && edgeGap(p.output, p.dockBar, kEdgeBottom) == 8 && reservationCoversDock(p);
    });
}

TEST_CASE("Panels mode uses same-edge panels and screen references across all dock edges", "[panel-margin][all-edges]")
{
    constexpr int edges[] = {0, 1, 2, 3};
    constexpr int references[] = {0, 1};
    constexpr bool floatingStates[] = {false, true};
    constexpr int margins[] = {8, 40};
    auto *settings = app().settings.get();
    const int oldEdge = settings->edge();
    const int oldVisibilityMode = settings->visibilityMode();
    const bool oldReserve = settings->reserveScreenSpace();
    const bool oldFloating = settings->floating();
    const int oldMargin = settings->floatingMargin();
    const int oldReference = settings->floatingMarginReference();
    const bool oldShadow = settings->shadowEnabled();
    configure(1, 8, kEdgeBottom, true, false);
    const auto restore = qScopeGuard([=] {
        settings->setEdge(oldEdge);
        settings->setVisibilityMode(oldVisibilityMode);
        settings->setReserveScreenSpace(oldReserve);
        settings->setFloating(oldFloating);
        settings->setShadowEnabled(oldShadow);
        settings->setFloatingMargin(oldMargin);
        settings->setFloatingMarginReference(oldReference);
    });

    auto manager = startDock();
    REQUIRE(QTest::qWaitFor(
        [&] {
            auto *shell = manager->primaryShell();
            return shell && shell->view()->isExposed() && shell->view()->screen();
        },
        kTimeoutMs));
    auto *view = manager->primaryShell()->view();
    auto *platform = dynamic_cast<krema::WaylandDockPlatform *>(view->platform());
    REQUIRE(platform);
    const QString outputName = view->screen()->name();

    // Independent no-panel controls cover every dock edge before any panel is
    // mapped, so no other exclusive zone affects the absolute bar geometry.
    for (const int dockEdge : edges) {
        for (const bool floating : floatingStates) {
            for (const int margin : margins) {
                platform->setEdge(static_cast<krema::DockPlatform::Edge>(dockEdge));
                view->setEdge(static_cast<krema::DockPlatform::Edge>(dockEdge));
                app().settings->setFloating(floating);
                app().settings->setFloatingMargin(margin);
                app().settings->setFloatingMarginReference(1);
                const QByteArray label = QStringLiteral("no panel, edge %1, floating %2, margin %3")
                                             .arg(dockEdge)
                                             .arg(floating)
                                             .arg(margin)
                                             .toLocal8Bit();
                requireSettles(*manager, {}, label.constData(), [&](const Placement &p) {
                    return edgeGap(p.output, p.dockBar, dockEdge) == (floating ? margin : 0);
                });
            }
        }
    }

    // Each 4x4 panel/dock-edge cell is exercised live for both references,
    // both floating states, and both margins. KWin frame plus visible bar
    // geometry remains the only oracle.
    for (const int panelEdge : edges) {
        DYNAMIC_SECTION("panel edge " << panelEdge)
        {
            StandInPanel panel(kPanelZone, outputName, {}, panelEdge);
            REQUIRE(panel.waitForStarted());
            const QSet<qint64> pids{panel.pid()};

            for (const int dockEdge : edges) {
                for (const int reference : references) {
                    for (const bool floating : floatingStates) {
                        for (const int margin : margins) {
                            platform->setEdge(static_cast<krema::DockPlatform::Edge>(dockEdge));
                            view->setEdge(static_cast<krema::DockPlatform::Edge>(dockEdge));
                            app().settings->setReserveScreenSpace(false);
                            app().settings->setFloatingMarginReference(reference);
                            app().settings->setFloating(floating);
                            app().settings->setFloatingMargin(margin);
                            const int requestedGap = floating ? margin : 0;
                            const QByteArray label = QStringLiteral("panel edge %1, dock edge %2, reference %3, floating %4, margin %5")
                                                         .arg(panelEdge)
                                                         .arg(dockEdge)
                                                         .arg(reference == 1 ? QStringLiteral("Panels") : QStringLiteral("Screen edge"))
                                                         .arg(floating)
                                                         .arg(margin)
                                                         .toLocal8Bit();
                            requireSettles(*manager, pids, label.constData(), [&](const Placement &p) {
                                if (!standInUnpushed(p, panelEdge)) {
                                    return false;
                                }
                                if (panelEdge == dockEdge) {
                                    if (reference == 1) {
                                        // Panels uses zone -1 and places the bar relative to the panel frame.
                                        return panelDockGap(p.standIns.first(), p.dockBar, dockEdge) == requestedGap;
                                    }
                                    // AlwaysVisible Screen edge uses zone 0 even with reservation off.
                                    // KWin therefore offsets it by the panel's fixture reservation (SET-021).
                                    return edgeGap(p.output, p.dockBar, dockEdge) == kPanelZone + requestedGap;
                                }
                                return edgeGap(p.output, p.dockBar, dockEdge) == requestedGap;
                            });
                        }
                    }
                }
            }
        }
    }

    // Re-enable reservation after the absolute geometry matrix and verify the
    // real KWin work area still excludes the visible dock bar.
    app().settings->setReserveScreenSpace(true);
    app().settings->setFloatingMarginReference(0);
    app().settings->setFloating(true);
    app().settings->setFloatingMargin(40);
    platform->setEdge(krema::DockPlatform::Edge::Bottom);
    view->setEdge(krema::DockPlatform::Edge::Bottom);
    requireSettles(*manager, {}, "reserved-on, no panel, bottom, Screen edge, floating, margin 40", [](const Placement &p) {
        return edgeGap(p.output, p.dockBar, kEdgeBottom) == 40 && p.maximizeArea != p.output && reservationCoversDock(p);
    });
}

TEST_CASE("Screen-edge zone-0 panel shifts the shadow boundary without changing the visible gap", "[panel-margin][shadow]")
{
    auto *settings = app().settings.get();
    const int oldEdge = settings->edge();
    const int oldVisibilityMode = settings->visibilityMode();
    const bool oldReserve = settings->reserveScreenSpace();
    const bool oldFloating = settings->floating();
    const int oldMargin = settings->floatingMargin();
    const int oldReference = settings->floatingMarginReference();
    const bool oldShadow = settings->shadowEnabled();
    const int oldLightX = settings->shadowLightX();
    const int oldLightY = settings->shadowLightY();
    const int oldLightZ = settings->shadowLightZ();
    const int oldElevation = settings->shadowElevation();
    const double oldRadius = settings->shadowLightRadius();
    constexpr int requestedGap = 8;
    configure(0, requestedGap, 0, true, false);
    settings->setShadowEnabled(true);
    settings->setShadowLightY(40);
    settings->setShadowLightZ(100);
    settings->setShadowElevation(50);
    settings->setShadowLightRadius(1.0);
    const auto restore = qScopeGuard([=] {
        settings->setEdge(oldEdge);
        settings->setVisibilityMode(oldVisibilityMode);
        settings->setReserveScreenSpace(oldReserve);
        settings->setFloating(oldFloating);
        settings->setFloatingMargin(oldMargin);
        settings->setFloatingMarginReference(oldReference);
        settings->setShadowEnabled(oldShadow);
        settings->setShadowLightX(oldLightX);
        settings->setShadowLightY(oldLightY);
        settings->setShadowLightZ(oldLightZ);
        settings->setShadowElevation(oldElevation);
        settings->setShadowLightRadius(oldRadius);
    });

    auto manager = startDock();
    REQUIRE(QTest::qWaitFor(
        [&] {
            auto *shell = manager->primaryShell();
            return shell && shell->view()->isExposed() && shell->view()->screen();
        },
        kTimeoutMs));
    auto *view = manager->primaryShell()->view();
    const QString outputName = view->screen()->name();
    StandInPanel panel(kPanelZone, outputName, {}, 0);
    REQUIRE(panel.waitForStarted());
    const QSet<qint64> pids{panel.pid()};

    requireSettles(*manager, pids, "Screen edge, zone 0, same-edge panel, shadow enabled", [&](const Placement &p) {
        if (!standInUnpushed(p, 0)) {
            return false;
        }
        const QRect localSurface(QPoint(), view->size());
        const QRect localPanel = view->visibilityController()->panelRect();
        const int frameOffset = edgeGap(p.output, p.dockFrame, 0);
        const int visibleOffset = edgeGap(p.output, p.dockBar, 0);
        const int localGap = edgeGap(localSurface, localPanel, 0);
        // KWin's zone-0 layer surface starts after the panel reservation.
        // The visible bar keeps the requested local gap; shadowOverflow is
        // larger, so its outward lobe reaches this shifted surface boundary
        // and is intentionally clipped there rather than moving the bar.
        return frameOffset == kPanelZone && visibleOffset == kPanelZone + requestedGap && localGap == requestedGap
            && view->floatingPadding() == requestedGap && view->shadowOverflow() > requestedGap;
    });
}

TEST_CASE("Per-screen Floating overrides follow and diverge from the global setting", "[panel-margin][screen-settings]")
{
    REQUIRE(QGuiApplication::screens().size() >= 2);
    auto *settings = app().settings.get();
    const int oldMonitorMode = settings->monitorMode();
    const int oldEdge = settings->edge();
    const int oldVisibilityMode = settings->visibilityMode();
    const bool oldReserve = settings->reserveScreenSpace();
    const bool oldFloating = settings->floating();
    const int oldMargin = settings->floatingMargin();
    const int oldReference = settings->floatingMarginReference();
    const bool oldShadow = settings->shadowEnabled();

    const auto screens = QGuiApplication::screens();
    const QString firstName = screens.at(0)->name();
    const QString secondName = screens.at(1)->name();
    const auto restore = qScopeGuard([&] {
        for (const QString &name : {firstName, secondName}) {
            krema::ScreenSettings screenSettings(name, settings);
            screenSettings.clearOverrides();
            screenSettings.save();
        }
        settings->setMonitorMode(oldMonitorMode);
        settings->setEdge(oldEdge);
        settings->setVisibilityMode(oldVisibilityMode);
        settings->setReserveScreenSpace(oldReserve);
        settings->setFloating(oldFloating);
        settings->setFloatingMargin(oldMargin);
        settings->setFloatingMarginReference(oldReference);
        settings->setShadowEnabled(oldShadow);
    });

    configure(0, 40, kEdgeBottom, false, true);
    settings->setMonitorMode(krema::MultiDockManager::AllScreens);

    // Seed one real per-screen override before shell creation; the other
    // output deliberately has no override and must follow the global value.
    {
        krema::ScreenSettings firstSettings(firstName, settings);
        firstSettings.clearOverrides();
        firstSettings.setFloating(true);
        firstSettings.save();
        krema::ScreenSettings secondSettings(secondName, settings);
        secondSettings.clearOverrides();
        secondSettings.save();
    }
    settings->setFloating(false);

    auto manager = startDock();
    REQUIRE(QTest::qWaitFor(
        [&] {
            return manager->shells().size() == 2;
        },
        kTimeoutMs));
    const auto shellFor = [&](const QString &name) -> krema::DockShell * {
        for (auto *shell : manager->shells()) {
            if (shell && shell->view()->screen() && shell->view()->screen()->name() == name) {
                return shell;
            }
        }
        return nullptr;
    };
    auto *firstShell = shellFor(firstName);
    auto *secondShell = shellFor(secondName);
    REQUIRE(firstShell);
    REQUIRE(secondShell);
    REQUIRE(QTest::qWaitFor(
        [&] {
            return firstShell->view()->isExposed() && secondShell->view()->isExposed();
        },
        kTimeoutMs));
    auto *firstScreenSettings = firstShell->findChild<krema::ScreenSettings *>();
    auto *secondScreenSettings = secondShell->findChild<krema::ScreenSettings *>();
    REQUIRE(firstScreenSettings);
    REQUIRE(secondScreenSettings);
    CHECK(firstScreenSettings->floating());
    CHECK_FALSE(secondScreenSettings->hasOverrides());
    CHECK_FALSE(secondScreenSettings->floating());

    const auto requireFloatingState = [&](bool firstFloating, bool secondFloating, const char *what) {
        requireShellSettles(firstShell, {}, what, [&](const Placement &p) {
            return edgeGap(p.output, p.dockBar, kEdgeBottom) == (firstFloating ? 40 : 0) && p.maximizeArea != p.output
                && reservationCoversDock(p, kEdgeBottom);
        });
        requireShellSettles(secondShell, {}, what, [&](const Placement &p) {
            return edgeGap(p.output, p.dockBar, kEdgeBottom) == (secondFloating ? 40 : 0) && p.maximizeArea != p.output
                && reservationCoversDock(p, kEdgeBottom);
        });
    };

    // Per-screen true wins over global false; the non-overridden second output
    // follows global false.
    requireFloatingState(true, false, "screen override true, global false");

    settings->setFloating(true);
    // The override remains true and the non-overridden output follows global
    // true in the opposite direction.
    requireFloatingState(true, true, "screen override true, global true");

    // Per-screen false wins over global true.
    firstScreenSettings->setFloating(false);
    requireFloatingState(false, true, "screen override false, global true");

    // clearOverrides() emits the effective-value notification; clearOverride()
    // alone would leave the live dock with the stale value.
    firstScreenSettings->clearOverrides();
    CHECK_FALSE(firstScreenSettings->hasOverrides());
    requireFloatingState(true, true, "screen overrides cleared, global true");
}

TEST_CASE("Panel probe ownership survives transient exposure loss during edge switches", "[panel-margin][issue-135]")
{
    configure(1, 8);
    const auto restore = qScopeGuard([] {
        configure(1, 8);
    });

    auto manager = startDock();
    REQUIRE(QTest::qWaitFor(
        [&] {
            auto *shell = manager->primaryShell();
            return shell && shell->view()->isExposed() && shell->view()->screen();
        },
        kTimeoutMs));
    auto *view = manager->primaryShell()->view();
    const QString outputName = view->screen()->name();
    StandInPanel panel(kPanelZone, outputName);
    REQUIRE(panel.waitForStarted());

    auto *platform = dynamic_cast<krema::WaylandDockPlatform *>(view->platform());
    REQUIRE(platform);
    const auto probeOwnsReservation = [&] {
        return platform->m_probe && platform->m_probeLayerWindow && platform->m_probeLayerWindow->exclusionZone() > 0
            && platform->m_layerWindow && platform->m_layerWindow->exclusionZone() == -1;
    };
    REQUIRE(QTest::qWaitFor(probeOwnsReservation, kTimeoutMs));

    // A real empty expose event is delivered while the native surface remains
    // visible. Force the resulting transient state deterministically so this
    // test also exercises the exact owner-loss branch from the bug report.
    QExposeEvent transient{QRegion()};
    QCoreApplication::sendEvent(platform->m_probe.get(), &transient);
    platform->m_probeMapped = false;
    platform->setExclusiveZone(platform->m_exclusiveZone);
    REQUIRE(probeOwnsReservation());

    const krema::DockPlatform::Edge edges[] = {
        krema::DockPlatform::Edge::Top,
        krema::DockPlatform::Edge::Left,
        krema::DockPlatform::Edge::Right,
        krema::DockPlatform::Edge::Bottom,
    };
    for (const auto edge : edges) {
        platform->setEdge(edge);
        view->setEdge(edge);
        QExposeEvent edgeTransient{QRegion()};
        QCoreApplication::sendEvent(platform->m_probe.get(), &edgeTransient);
        platform->m_probeMapped = false;
        platform->setExclusiveZone(platform->m_exclusiveZone);
        INFO("edge switch retained a single positive reservation");
        REQUIRE(probeOwnsReservation());
    }
}

TEST_CASE("Panel probe edge switch keeps KWin consumer work area single-reserved", "[panel-margin][issue-135]")
{
    configure(1, 8);
    const auto restore = qScopeGuard([] {
        configure(1, 8);
    });

    OrdinaryWindow consumer;
    REQUIRE(consumer.waitForStarted());
    auto manager = startDock();
    REQUIRE(QTest::qWaitFor(
        [&] {
            auto *shell = manager->primaryShell();
            return shell && shell->view()->isExposed() && shell->view()->screen();
        },
        kTimeoutMs));
    auto *view = manager->primaryShell()->view();
    const qint64 consumerPid = consumer.pid();
    REQUIRE(QTest::qWaitFor(
        [&] {
            const auto placement = measure(*manager, {}, maximizeAction(consumerPid, manager->primaryShell()->view()->screen()->name()), false);
            const auto window = placement ? windowOf(*placement, consumerPid) : std::nullopt;
            return window && !window->maximizeArea.isEmpty();
        },
        kTimeoutMs));

    std::optional<Placement> initial;
    REQUIRE(QTest::qWaitFor(
        [&] {
            initial = measure(*manager, {}, {}, false);
            return initial.has_value() && windowOf(*initial, consumerPid).has_value();
        },
        kTimeoutMs));
    const auto initialWindow = windowOf(*initial, consumerPid);
    REQUIRE(initialWindow);
    const int singleReservation = initial->output.bottom() - initialWindow->maximizeArea.bottom();
    REQUIRE(singleReservation > 0);

    auto *platform = dynamic_cast<krema::WaylandDockPlatform *>(view->platform());
    REQUIRE(platform);
    platform->setEdge(krema::DockPlatform::Edge::Top);
    view->setEdge(krema::DockPlatform::Edge::Top);
    // Simulate transient expose loss without detaching the probe. Explicitly
    // request the probe update so the old implementation commits both the
    // existing probe reservation and its fallback dock reservation.
    platform->m_probeMapped = false;
    if (platform->m_probeLayerWindow) {
        platform->m_probeLayerWindow->setExclusiveZone(platform->m_exclusiveZone);
        platform->m_probe->requestUpdate();
    }
    platform->setExclusiveZone(platform->m_exclusiveZone);

    const QList<int> samples = sampleMaximizedTop(*manager, consumerPid, {}, 700);
    REQUIRE(!samples.isEmpty());
    REQUIRE(platform->m_layerWindow->exclusionZone() == -1);
    INFO("KWin top inset sample count " << samples.size() << ", max " << *std::max_element(samples.cbegin(), samples.cend()));
    REQUIRE(*std::max_element(samples.cbegin(), samples.cend()) <= singleReservation);
    REQUIRE(std::find(samples.cbegin(), samples.cend(), singleReservation) != samples.cend());
}

TEST_CASE("Panel probe teardown returns ownership before remapping", "[panel-margin][issue-135]")
{
    configure(1, 8);
    const auto restore = qScopeGuard([] {
        configure(1, 8);
    });

    auto manager = startDock();
    REQUIRE(QTest::qWaitFor(
        [&] {
            auto *shell = manager->primaryShell();
            return shell && shell->view()->isExposed() && shell->view()->screen();
        },
        kTimeoutMs));
    auto *view = manager->primaryShell()->view();
    const QString outputName = view->screen()->name();
    StandInPanel panel(kPanelZone, outputName);
    REQUIRE(panel.waitForStarted());

    auto *platform = dynamic_cast<krema::WaylandDockPlatform *>(view->platform());
    REQUIRE(platform);
    const auto probeOwnsReservation = [&] {
        return platform->m_probe && platform->m_probeLayerWindow && platform->m_probeLayerWindow->exclusionZone() > 0
            && platform->m_layerWindow && platform->m_layerWindow->exclusionZone() == -1;
    };
    REQUIRE(QTest::qWaitFor(probeOwnsReservation, kTimeoutMs));

    platform->m_probe->hide();
    REQUIRE(QTest::qWaitFor(
        [&] {
            return platform->m_probe && !platform->m_probeMapped && platform->m_layerWindow->exclusionZone() > 0;
        },
        kTimeoutMs));

    platform->showProbe();
    REQUIRE(QTest::qWaitFor(probeOwnsReservation, kTimeoutMs));

    // Disable Panels while exposure is transiently lost. The probe must be
    // cleared and destroyed before the dock receives the positive zone.
    QExposeEvent transient{QRegion()};
    QCoreApplication::sendEvent(platform->m_probe.get(), &transient);
    platform->m_probeMapped = false;
    platform->setPanelRelative(false);
    REQUIRE(platform->m_probe == nullptr);
    REQUIRE(platform->m_probeLayerWindow == nullptr);
    REQUIRE(platform->m_layerWindow->exclusionZone() > 0);

    platform->setPanelRelative(true);
    REQUIRE(QTest::qWaitFor(probeOwnsReservation, kTimeoutMs));

    // SurfaceAboutToBeDestroyed is a real teardown boundary, even when the
    // QWindow itself still reports visible.
    QPlatformSurfaceEvent surfaceDestroyed{QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed};
    QCoreApplication::sendEvent(platform->m_probe.get(), &surfaceDestroyed);
    REQUIRE(!platform->m_probeMapped);
    REQUIRE(platform->m_layerWindow->exclusionZone() > 0);

    platform->showProbe();
    REQUIRE(QTest::qWaitFor(probeOwnsReservation, kTimeoutMs));
}

TEST_CASE("Panels mode measures the gap from a panel started before Krema", "[panel-margin]")
{
    configure(1, 8);

    // One reserving panel per output, so the dock finds one wherever it lands.
    std::vector<std::unique_ptr<StandInPanel>> panels;
    QSet<qint64> pids;
    for (QScreen *screen : QGuiApplication::screens()) {
        panels.push_back(std::make_unique<StandInPanel>(kPanelZone, screen->name()));
        REQUIRE(panels.back()->waitForStarted());
        pids.insert(panels.back()->pid());
    }
    // Every stand-in is mapped before Krema starts.
    REQUIRE(QTest::qWaitFor(
        [&] {
            const auto snapshot = KWinQuery::instance().snapshot();
            if (!snapshot) {
                return false;
            }
            qsizetype mapped = 0;
            for (const DockWindow &w : snapshot->docks) {
                mapped += pids.contains(w.pid) && w.frame.height() == kPanelDepth ? 1 : 0;
            }
            return mapped == pids.size();
        },
        kTimeoutMs));

    auto manager = startDock();
    requireSettles(*manager, pids, "reserving panel started first, Panels, margin 8", [](const Placement &p) {
        return standInUnpushed(p) && p.dockBottom() == p.outputBottom() - kPanelDepth - 8 && reservationCoversDock(p);
    });
}

TEST_CASE("Maximized windows follow the reservation as it moves between the dock and its probe", "[panel-margin][maximized]")
{
    // Panels with no panel on the edge: the reservation sits on the probe in
    // Panels mode and on the dock surface in Screen edge mode, so every
    // switch moves it between two surfaces; the reserved depth is the same.
    constexpr int kMargin = 8;
    configure(1, kMargin);
    const auto restore = qScopeGuard([] {
        configure(1, kMargin);
    });

    OrdinaryWindow window;
    REQUIRE(window.waitForStarted());
    const qint64 pid = window.pid();

    auto manager = startDock();
    REQUIRE(QTest::qWaitFor(
        [&] {
            auto *shell = manager->primaryShell();
            return shell && shell->view()->isExposed() && shell->view()->screen();
        },
        kTimeoutMs));
    const QString outputName = manager->primaryShell()->view()->screen()->name();

    // Maximize the window on the dock's output (the window maps
    // asynchronously in its own process, so retry until KWin has it).
    {
        std::optional<Placement> last;
        const bool maximized = QTest::qWaitFor(
            [&] {
                last = measure(*manager, {}, maximizeAction(pid, outputName));
                const auto w = last ? windowOf(*last, pid) : std::nullopt;
                return w && !w->maximizeArea.isEmpty() && w->frame == w->maximizeArea;
            },
            kTimeoutMs);
        INFO("maximizing " << kMaximizedTitle << " (pid " << pid << "): " << describe(last));
        REQUIRE(maximized);
    }
    const auto converges = [&](const char *what) {
        requireSettles(*manager, {}, what, [&](const Placement &p) {
            return maximizedFitsOneReservation(p, pid, kMargin);
        });
    };
    converges("maximized, Panels");

    // Settled switches and quick flip-flops, with varied gaps so the
    // reservation moves while KWin may still be reflowing the window.
    const int delays[] = {50, 120, 200, 80, 160};
    for (int round = 0; round < 5; ++round) {
        const int d = delays[round];
        const int dBack = delays[4 - round];
        INFO("round " << round << ", delays " << d << "/" << dBack << " ms");

        app().settings->setFloatingMarginReference(0);
        converges("switched to Screen edge");

        app().settings->setFloatingMarginReference(1);
        QTest::qWait(d);
        app().settings->setFloatingMarginReference(0);
        QTest::qWait(dBack);
        app().settings->setFloatingMarginReference(1);
        converges("flip-flopped back to Panels");
    }

    // Krema starting with the maximized window already present.
    const auto restart = [&](int reference, bool waitForFullOutput, const char *what) {
        INFO(what);
        manager.reset();
        if (waitForFullOutput) {
            // No dock, no reservation: the window grows to the whole output,
            // so the next start begins from a known state.
            std::optional<Snapshot> snapshot;
            const bool grown = QTest::qWaitFor(
                [&] {
                    snapshot = KWinQuery::instance().snapshot();
                    if (!snapshot || !snapshot->outputs.contains(outputName)) {
                        return false;
                    }
                    const QRect output = snapshot->outputs.value(outputName).geometry;
                    for (const NormalWindow &w : snapshot->normals) {
                        if (w.pid == pid) {
                            return w.maximizeArea == output && w.frame == output;
                        }
                    }
                    return false;
                },
                kTimeoutMs);
            REQUIRE(grown);
        }
        app().settings->setFloatingMarginReference(reference);
        manager = startDock();
        converges(what);
    };
    restart(1, true, "restarted in Panels mode from a full-output window");
    restart(0, true, "restarted in Screen edge mode from a full-output window");
    restart(1, false, "restarted in Panels mode right after the previous dock went away");
}

TEST_CASE("Auto-hide dock beside a panel reveals from the screen edge without stealing panel clicks, or from the dock position", "[panel-margin][reveal]")
{
    // Needs KWin's fake-input protocol to move the pointer and click. Keep one
    // device for the life of the process so the seat's pointer capability stays up.
    static krema::test::FakePointer pointer("krema_panel_margin_tests", "reveal trigger test");
    REQUIRE(pointer.isValid());

    auto *s = app().settings.get();
    const int showDelay = s->showDelay();
    const int hideDelay = s->hideDelay();
    configure(1, 8);
    s->setVisibilityMode(static_cast<int>(krema::DockPlatform::VisibilityMode::AutoHide));
    s->setShowDelay(50);
    s->setHideDelay(150);
    s->setRevealTriggerLocation(0);
    const auto restore = qScopeGuard([s, showDelay, hideDelay] {
        configure(1, 8);
        s->setShowDelay(showDelay);
        s->setHideDelay(hideDelay);
        s->setRevealTriggerLocation(0);
    });

    auto manager = startDock();
    REQUIRE(QTest::qWaitFor(
        [&] {
            auto *shell = manager->primaryShell();
            return shell && shell->view()->isExposed() && shell->view()->screen();
        },
        kTimeoutMs));
    auto *view = manager->primaryShell()->view();
    auto *visibility = view->visibilityController();
    const QString outputName = view->screen()->name();
    const QRect screen = view->screen()->geometry();
    // Off the dock, so auto hide hides it.
    const QPoint away(screen.center().x(), screen.y() + screen.height() / 4);
    pointer.moveTo(away);

    QTemporaryFile clickLog(QDir::tempPath() + QStringLiteral("/krema-panel-clicks-XXXXXX"));
    REQUIRE(clickLog.open());
    StandInPanel panel(kPanelZone, outputName, clickLog.fileName());
    REQUIRE(panel.waitForStarted());
    const QSet<qint64> pids{panel.pid()};

    const auto isVisible = [&] {
        return visibility->isDockVisible();
    };
    const auto isHidden = [&] {
        return !visibility->isDockVisible();
    };
    // The panels' band on the dock surface (anchored to the bottom edge).
    const auto band = [&] {
        return QRect(0, view->height() - kPanelDepth, view->width(), kPanelDepth);
    };
    const auto describeMask = [&] {
        const QRect r = view->mask().boundingRect();
        return QStringLiteral("surface %1x%2, mask bounding rect %3,%4 %5x%6, dock visible %7")
            .arg(view->width())
            .arg(view->height())
            .arg(r.x())
            .arg(r.y())
            .arg(r.width())
            .arg(r.height())
            .arg(visibility->isDockVisible() ? QStringLiteral("yes") : QStringLiteral("no"))
            .toStdString();
    };

    // Hidden past the panel, Screen edge: KWin watches the outer screen edge,
    // so the surface keeps only a 1x1 placeholder (never an empty mask, which
    // would accept all input) and nothing over the band or at its inner edge.
    std::optional<Placement> placed;
    {
        const bool settled = QTest::qWaitFor(
            [&] {
                placed = measure(*manager, pids, {}, false);
                return placed && standInUnpushed(*placed) && isHidden() && view->mask() == QRegion(0, 0, 1, 1);
            },
            kTimeoutMs);
        INFO("hidden, Screen edge: " << describe(placed) << "; " << describeMask());
        REQUIRE(settled);
    }

    const QRect standIn = placed->standIns.first();
    const int x = standIn.center().x();
    const QPoint screenEdge(x, placed->outputBottom() - 1);
    const QPoint panelBand(x, placed->outputBottom() - kPanelDepth / 2);
    // Dock position: the trigger strip just past the panels' band.
    const QPoint innerEdge(x, placed->dockFrame.y() + view->height() - kPanelDepth - 1);
    INFO(describe(placed));
    REQUIRE(standIn.contains(screenEdge));
    REQUIRE(standIn.contains(panelBand));

    // On the panel, off the screen edge: the dock stays hidden and the panel
    // gets the click.
    pointer.moveTo(panelBand);
    REQUIRE(holdsFor(500, isHidden));
    pointer.click();
    REQUIRE(QTest::qWaitFor(
        [&] {
            return clickCount(clickLog.fileName()) == 1;
        },
        kTimeoutMs));

    // The outer screen edge, over the panel: the dock is revealed, its
    // surface still leaves the band to the panel, and the panel still gets
    // the click.
    pointer.moveTo(screenEdge);
    REQUIRE(QTest::qWaitFor(isVisible, kTimeoutMs));
    {
        const bool clear = QTest::qWaitFor(
            [&] {
                const QRegion mask = view->mask();
                return !mask.isEmpty() && mask.intersected(band()).isEmpty();
            },
            kTimeoutMs);
        INFO("revealed from the screen edge: " << describeMask());
        REQUIRE(clear);
    }
    pointer.click();
    REQUIRE(QTest::qWaitFor(
        [&] {
            return clickCount(clickLog.fileName()) == 2;
        },
        kTimeoutMs));
    REQUIRE(isVisible());

    // Leaving the screen edge without entering the dock (e.g. after clicking
    // the panel) hides the dock again after the hide delay.
    pointer.moveTo(away);
    REQUIRE(QTest::qWaitFor(isHidden, kTimeoutMs));

    // Reveal from the screen edge again, then move across the band onto the
    // dock: it stays shown while the pointer is on it.
    pointer.moveTo(screenEdge);
    REQUIRE(QTest::qWaitFor(isVisible, kTimeoutMs));
    {
        std::optional<Placement> shown;
        REQUIRE(QTest::qWaitFor(
            [&] {
                shown = measure(*manager, pids);
                return shown && shown->dockBottom() == shown->outputBottom() - kPanelDepth - 8
                    && view->mask().contains(shown->dockBar.center() - shown->dockFrame.topLeft());
            },
            kTimeoutMs));
        INFO("revealed: " << describe(shown));
        pointer.moveTo(shown->dockBar.center());
        REQUIRE(holdsFor(600, isVisible));
    }

    pointer.moveTo(away);
    REQUIRE(QTest::qWaitFor(isHidden, kTimeoutMs));
    // Dock position: the screen edge no longer reveals the dock; the strip
    // past the band does, and the band stays click-through.
    s->setRevealTriggerLocation(1);
    {
        const bool settled = QTest::qWaitFor(
            [&] {
                const QRegion mask = view->mask();
                return isHidden() && mask.contains(innerEdge - placed->dockFrame.topLeft()) && mask.intersected(band()).isEmpty();
            },
            kTimeoutMs);
        INFO("hidden, Dock position: " << describeMask());
        REQUIRE(settled);
    }
    pointer.moveTo(screenEdge);
    REQUIRE(holdsFor(500, isHidden));
    pointer.click();
    REQUIRE(QTest::qWaitFor(
        [&] {
            return clickCount(clickLog.fileName()) == 3;
        },
        kTimeoutMs));

    pointer.moveTo(innerEdge);
    REQUIRE(QTest::qWaitFor(isVisible, kTimeoutMs));

    pointer.moveTo(away);
    REQUIRE(QTest::qWaitFor(isHidden, kTimeoutMs));

    // Rebuilding the dock shell while external screen-edge reveal is active
    // (e.g. changing Monitor mode) must tear down cleanly without calling into
    // a destroyed platform from QWindow::visibleChanged(false).
    s->setRevealTriggerLocation(0);
    REQUIRE(QTest::qWaitFor(
        [&] {
            return isHidden() && view->mask() == QRegion(0, 0, 1, 1);
        },
        kTimeoutMs));
    manager->setMonitorMode(krema::MultiDockManager::AllScreens);
    manager->setMonitorMode(krema::MultiDockManager::PrimaryOnly);
    REQUIRE(QTest::qWaitFor(
        [&] {
            auto *shell = manager->primaryShell();
            return shell && shell->view()->isExposed() && shell->view()->mask() == QRegion(0, 0, 1, 1);
        },
        kTimeoutMs));
}

int main(int argc, char *argv[])
{
    if (argc > 1 && qstrcmp(argv[1], kPanelArg) == 0) {
        return runStandInPanel(argc, argv);
    }
    if (argc > 1 && qstrcmp(argv[1], kMaximizedArg) == 0) {
        return runOrdinaryWindow(argc, argv);
    }

    // Opt into the layer-shell platform plugin. Equivalent to the deprecated
    // LayerShellQt::Shell::useLayerShell(), which only sets this variable.
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

#include "test_panel_margin.moc"
