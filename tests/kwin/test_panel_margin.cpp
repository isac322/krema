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
// Geometry comes from KWin itself: a one-shot KWin script (loaded through
// org.kde.kwin.Scripting) reports every dock-type window's frame and each
// output's MaximizeArea back to this process over D-Bus. The dock bar's
// screen rect is the dock surface's KWin frame plus the panel rect QML
// reports to DockVisibilityController (surface-local).
//
// Must run under run-with-kwin.sh, which provides WAYLAND_DISPLAY, a private
// session bus and throwaway XDG directories.

#include "krema.h"
#include "models/dockmodel.h"
#include "models/launcherentrytracker.h"
#include "models/notificationtracker.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/dockvisibilitycontroller.h"
#include "shell/multidockmanager.h"

#include <KAboutData>
#include <LayerShellQt/Window>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
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

// Always visible + Reserve screen space + Floating on the bottom edge, with
// the given gap reference and margin. Set before the manager exists, or live.
void configure(int reference, int margin)
{
    auto *s = app().settings.get();
    s->setEdge(kEdgeBottom);
    s->setVisibilityMode(static_cast<int>(krema::DockPlatform::VisibilityMode::AlwaysVisible));
    s->setReserveScreenSpace(true);
    s->setFloating(true);
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

struct Snapshot {
    QHash<QString, OutputState> outputs;
    QList<DockWindow> docks;
};

QRect rectFrom(const QJsonValue &value)
{
    const QJsonArray a = value.toArray();
    if (a.size() != 4) {
        return {};
    }
    return QRect(std::lround(a.at(0).toDouble()), std::lround(a.at(1).toDouble()), std::lround(a.at(2).toDouble()), std::lround(a.at(3).toDouble()));
}

// One-shot report of every dock-type window and each output's work area.
// %1 service, %2 object path, %3 interface, %4 token.
const char kQueryScript[] = R"JS(
function rectOf(g) {
    return [Math.round(g.x), Math.round(g.y), Math.round(g.width), Math.round(g.height)];
}
var outputs = {};
var screens = workspace.screens;
for (var i = 0; i < screens.length; ++i) {
    outputs[screens[i].name] = {
        geometry: rectOf(screens[i].geometry),
        maximizeArea: rectOf(workspace.clientArea(KWin.MaximizeArea, screens[i], workspace.currentDesktop))
    };
}
var docks = [];
var windows = workspace.windowList();
for (var j = 0; j < windows.length; ++j) {
    var w = windows[j];
    if (!w.dock || w.deleted) {
        continue;
    }
    docks.push({ pid: w.pid, output: w.output ? w.output.name : "", frame: rectOf(w.frameGeometry) });
}
callDBus("%1", "%2", "%3", "report", "%4", JSON.stringify({ outputs: outputs, docks: docks }));
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

    /// KWin's current state; nullopt when KWin did not answer in time.
    std::optional<Snapshot> snapshot()
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
        file.write(
            QString::fromLatin1(kQueryScript).arg(bus.baseService(), QLatin1String(kPath), QStringLiteral("com.bhyoo.krema.test.PanelMargin"), token).toUtf8());
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
        return snapshot;
    }

    bool m_registered = false;
    int m_counter = 0;
    std::optional<std::pair<QString, QString>> m_reply;
};

// ---------------------------------------------------------------------------
// Stand-in panel (child process)
// ---------------------------------------------------------------------------

/// This binary in helper mode: a panel-like layer surface on one output.
class StandInPanel
{
public:
    StandInPanel(int zone, const QString &outputName)
    {
        // Keep the helper's warnings in the test log.
        m_process.setProcessChannelMode(QProcess::ForwardedChannels);
        m_process.start(QCoreApplication::applicationFilePath(), {QLatin1String(kPanelArg), QString::number(zone), QString::number(kEdgeBottom), outputName});
    }
    ~StandInPanel()
    {
        stop();
    }
    StandInPanel(const StandInPanel &) = delete;
    StandInPanel &operator=(const StandInPanel &) = delete;

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

    QScreen *screen = QGuiApplication::primaryScreen();
    for (QScreen *candidate : QGuiApplication::screens()) {
        if (candidate->name() == outputName) {
            screen = candidate;
        }
    }

    class Panel : public QRasterWindow
    {
    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter(this).fillRect(QRect(QPoint(), size()), Qt::darkGray);
        }
    } window;

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
    return QStringLiteral("output %1 %2, maximizeArea %3, dock frame %4, dock bar %5 (bottom %6, output bottom %7), stand-ins %8")
        .arg(p->outputName, rect(p->output), rect(p->maximizeArea), rect(p->dockFrame), rect(p->dockBar))
        .arg(p->dockBottom())
        .arg(p->outputBottom())
        .arg(panels.isEmpty() ? QStringLiteral("none") : panels)
        .toStdString();
}

std::optional<Placement> measure(krema::MultiDockManager &manager, const QSet<qint64> &standInPids)
{
    auto *shell = manager.primaryShell();
    if (!shell || !shell->view()->screen() || !shell->view()->isExposed()) {
        return std::nullopt;
    }
    auto *view = shell->view();
    const QRect panelRect = view->visibilityController()->panelRect();
    if (panelRect.width() <= 0 || panelRect.height() <= 0) {
        return std::nullopt;
    }
    const auto snapshot = KWinQuery::instance().snapshot();
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
    p.dockBar = panelRect.translated(p.dockFrame.topLeft());
    return p;
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

/// The stand-in panel is mapped and still flush with the bottom edge.
bool standInUnpushed(const Placement &p)
{
    return p.standIns.size() == 1 && p.standIns.first().height() == kPanelDepth && p.standIns.first().y() + kPanelDepth == p.outputBottom();
}

/// Maximized windows end at or above the dock bar.
bool reservationCoversDock(const Placement &p)
{
    return !p.maximizeArea.isEmpty() && p.maximizeArea.y() + p.maximizeArea.height() <= p.dockBar.y();
}

std::unique_ptr<krema::MultiDockManager> startDock()
{
    auto manager = std::make_unique<krema::MultiDockManager>(app().settings.get(), app().model.get(), app().tracker.get());
    manager->initialize();
    return manager;
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
        return p.dockBottom() == p.outputBottom() - 8 && reservationCoversDock(p);
    });

    // An always-visible floating panel appears on the dock's edge: the dock
    // moves past the panel's whole surface, the panel stays where it is, and
    // maximized windows still end above the dock.
    auto panel = std::make_unique<StandInPanel>(kPanelZone, outputName);
    REQUIRE(panel->waitForStarted());
    QSet<qint64> pids{panel->pid()};
    requireSettles(*manager, pids, "reserving panel, Panels, margin 8", [](const Placement &p) {
        return standInUnpushed(p) && p.dockBottom() == p.outputBottom() - kPanelDepth - 8 && reservationCoversDock(p);
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
        return p.dockBottom() == p.outputBottom() - 8;
    });

    // Panels again with a wider margin, applied live.
    app().settings->setFloatingMarginReference(1);
    app().settings->setFloatingMargin(40);
    requireSettles(*manager, pids, "reserving panel, Panels, margin 40", [](const Placement &p) {
        return standInUnpushed(p) && p.dockBottom() == p.outputBottom() - kPanelDepth - 40 && reservationCoversDock(p);
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
        return standInUnpushed(p) && p.dockBottom() == p.outputBottom() - 8 && reservationCoversDock(p);
    });
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

int main(int argc, char *argv[])
{
    if (argc > 1 && qstrcmp(argv[1], kPanelArg) == 0) {
        return runStandInPanel(argc, argv);
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
