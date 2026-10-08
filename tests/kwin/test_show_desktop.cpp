// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Show Desktop must not hide the dock (issue #16), and no Krema surface may
// block KWin's Slide Back effect.
//
// KWin decides what Show Desktop hides from the window type, and a layer-shell
// surface gets its type only from its namespace (scope). The test drives the
// real MultiDockManager against a KWin virtual compositor, toggles Show
// Desktop through the same org.kde.KWin.showDesktop D-Bus call Meta+D reaches,
// and reads KWin's own verdict (EffectWindow.hiddenByShowDesktop) from a
// test-only scripted effect (effects/kremashowdesktopprobe) that logs it.
//
// The same probe logs EffectWindow.normalWindow and .dialog. Slide Back
// (plugins/slideback/slideback.cpp) only fires when the topmost "usable"
// window (normal or dialog, not keepAbove/minimized/deleted) changes, so an
// always-mapped Krema surface typed Normal (any unknown scope) would
// permanently top that list and disable the effect. Neither the dock nor the
// pre-shown preview may be normal or dialog.
//
// A plain xdg toplevel from a child process is the control: it must be hidden,
// which proves Show Desktop actually took effect.
//
// Must run under run-with-kwin.sh with KREMA_TEST_KWIN_EFFECTS pointing at
// tests/kwin/effects.

#include "krema.h"
#include "models/dockmodel.h"
#include "models/launcherentrytracker.h"
#include "models/notificationtracker.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/multidockmanager.h"

#include <KAboutData>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QFile>
#include <QPainter>
#include <QProcess>
#include <QQuickStyle>
#include <QRasterWindow>
#include <QScopeGuard>
#include <QTest>
#include <QtQml>

#include <algorithm>
#include <memory>

static void initResources()
{
    Q_INIT_RESOURCE(qml);
}

namespace
{

constexpr int kTimeoutMs = 15000;
constexpr auto kControlArg = "--control-window";
constexpr auto kControlAppId = "krema-showdesktop-control";

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

// One window as the probe effect reported it for one Show Desktop toggle.
struct ProbeWindow {
    QString windowClass;
    bool dock = false;
    bool hiddenByShowDesktop = false;
    bool normal = false;
    bool dialog = false;
    bool keepAbove = false;
    bool minimized = false;
    QSize size;
};

// The windows KWin reported for the most recent toggle that turned Show
// Desktop on. Deleted surfaces that are still in the stacking order for the
// closing animation are skipped, so the same-size shell teardown does not
// shadow the dock.
QList<ProbeWindow> latestShowingWindows()
{
    QFile log(qEnvironmentVariable("KREMA_TEST_KWIN_LOG"));
    if (!log.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QLatin1String marker("KREMA_SHOWDESKTOP ");
    int latestToggle = -1;
    QList<ProbeWindow> result;
    while (!log.atEnd()) {
        const QString line = QString::fromUtf8(log.readLine()).trimmed();
        const qsizetype at = line.indexOf(marker);
        if (at < 0) {
            continue;
        }
        int toggle = -1;
        bool showing = false;
        bool deleted = false;
        ProbeWindow window;
        const auto fields = QStringView(line).mid(at + marker.size()).split(QLatin1Char(' '));
        for (const auto &field : fields) {
            const qsizetype eq = field.indexOf(QLatin1Char('='));
            if (eq < 0) {
                continue;
            }
            const auto key = field.left(eq);
            const auto value = field.mid(eq + 1);
            if (key == QLatin1String("toggle")) {
                toggle = value.toInt();
            } else if (key == QLatin1String("showing")) {
                showing = value == QLatin1String("true");
            } else if (key == QLatin1String("class")) {
                window.windowClass = value.toString();
            } else if (key == QLatin1String("dock")) {
                window.dock = value == QLatin1String("true");
            } else if (key == QLatin1String("deleted")) {
                deleted = value == QLatin1String("true");
            } else if (key == QLatin1String("hiddenByShowDesktop")) {
                window.hiddenByShowDesktop = value == QLatin1String("true");
            } else if (key == QLatin1String("normal")) {
                window.normal = value == QLatin1String("true");
            } else if (key == QLatin1String("dialog")) {
                window.dialog = value == QLatin1String("true");
            } else if (key == QLatin1String("keepAbove")) {
                window.keepAbove = value == QLatin1String("true");
            } else if (key == QLatin1String("minimized")) {
                window.minimized = value == QLatin1String("true");
            } else if (key == QLatin1String("size")) {
                const auto wh = value.split(QLatin1Char('x'));
                if (wh.size() == 2) {
                    window.size = QSize(wh[0].toInt(), wh[1].toInt());
                }
            }
        }
        if (!showing || deleted || toggle < latestToggle) {
            continue;
        }
        if (toggle > latestToggle) {
            latestToggle = toggle;
            result.clear();
        }
        result.append(window);
    }
    return result;
}

std::string describe(const QList<ProbeWindow> &windows)
{
    std::string out;
    for (const auto &w : windows) {
        out += QStringLiteral("[%1 dock=%2 hiddenByShowDesktop=%3 normal=%4 dialog=%5 keepAbove=%6 minimized=%7 %8x%9] ")
                   .arg(w.windowClass)
                   .arg(w.dock)
                   .arg(w.hiddenByShowDesktop)
                   .arg(w.normal)
                   .arg(w.dialog)
                   .arg(w.keepAbove)
                   .arg(w.minimized)
                   .arg(w.size.width())
                   .arg(w.size.height())
                   .toStdString();
    }
    return out;
}

bool setShowingDesktop(bool showing)
{
    // KWin ends Show Desktop when the requesting D-Bus client disconnects, so
    // the call goes over this process's long-lived session connection.
    auto message =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"), QStringLiteral("showDesktop"));
    message << showing;
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, kTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        qWarning() << "org.kde.KWin.showDesktop failed:" << reply.errorName() << reply.errorMessage();
        return false;
    }
    return true;
}

// A normal application window (xdg toplevel) in a child process.
int runControlWindow(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QGuiApplication::setDesktopFileName(QLatin1String(kControlAppId));
    class Window : public QRasterWindow
    {
    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter(this).fillRect(QRect(QPoint(), size()), Qt::darkCyan);
        }
    } window;
    window.resize(320, 240);
    window.show();
    return application.exec();
}

} // namespace

TEST_CASE("Show Desktop keeps the dock visible", "[show-desktop]")
{
    // Control: a normal window that Show Desktop must hide. The child must
    // not inherit the layer-shell override, or it would not be a normal window.
    QProcess control;
    auto env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("QT_WAYLAND_SHELL_INTEGRATION"));
    control.setProcessEnvironment(env);
    control.start(QCoreApplication::applicationFilePath(), {QLatin1String(kControlArg)});
    REQUIRE(control.waitForStarted(kTimeoutMs));
    const auto stopControl = qScopeGuard([&] {
        control.kill();
        control.waitForFinished(kTimeoutMs);
    });

    krema::MultiDockManager manager(app().settings.get(), app().model.get(), app().tracker.get());
    manager.initialize();
    REQUIRE(QTest::qWaitFor(
        [&] {
            auto *shell = manager.primaryShell();
            return shell && shell->view()->isExposed() && shell->view()->width() > 0 && shell->view()->height() > 0;
        },
        kTimeoutMs));
    const QSize dockSize = manager.primaryShell()->view()->size();
    INFO("dock surface size: " << dockSize.width() << "x" << dockSize.height());

    auto load = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                               QStringLiteral("/Effects"),
                                               QStringLiteral("org.kde.kwin.Effects"),
                                               QStringLiteral("loadEffect"));
    load << QStringLiteral("kremashowdesktopprobe");
    const QDBusReply<bool> loaded = QDBusConnection::sessionBus().call(load, QDBus::Block, kTimeoutMs);
    REQUIRE(loaded.isValid());
    REQUIRE(loaded.value());

    const QString controlClass = QLatin1String(kControlAppId);
    const auto isControl = [&](const ProbeWindow &w) {
        return w.windowClass.contains(controlClass);
    };
    // Both Krema surfaces (dock and preview) share one window class; the dock
    // is the one with the dock view's size.
    const auto isDock = [&](const ProbeWindow &w) {
        return !isControl(w) && w.size == dockSize;
    };

    const auto restore = qScopeGuard([] {
        setShowingDesktop(false);
    });

    // The control maps asynchronously in its own process, so toggle until a
    // Show Desktop pass reports it.
    QList<ProbeWindow> windows;
    REQUIRE(QTest::qWaitFor(
        [&] {
            if (!setShowingDesktop(true)) {
                return false;
            }
            QTest::qWait(500);
            windows = latestShowingWindows();
            if (std::ranges::any_of(windows, isControl)) {
                return true;
            }
            setShowingDesktop(false);
            QTest::qWait(200);
            return false;
        },
        kTimeoutMs));
    INFO("windows while showing the desktop: " << describe(windows));

    bool controlHidden = true;
    int docks = 0;
    bool dockKept = true;
    QString kremaClass;
    for (const auto &w : windows) {
        if (isControl(w)) {
            controlHidden = controlHidden && w.hiddenByShowDesktop;
        } else if (isDock(w)) {
            ++docks;
            dockKept = dockKept && w.dock && !w.hiddenByShowDesktop;
            kremaClass = w.windowClass;
        }
    }

    // Control: Show Desktop really hid the ordinary window.
    CHECK(controlHidden);

    // Exactly one dock surface, typed Dock by KWin and left on screen.
    REQUIRE(docks == 1);
    CHECK(dockKept);

    // Slide Back: no Krema surface may be a usable window (normal or dialog).
    // Krema surfaces share the dock's window class; the preview is the one
    // that is not dock-sized. Requiring it to be present keeps the check from
    // passing vacuously.
    REQUIRE_FALSE(kremaClass.isEmpty());
    int previews = 0;
    bool kremaUnusable = true;
    for (const auto &w : windows) {
        if (isControl(w) || w.windowClass != kremaClass) {
            continue;
        }
        if (!isDock(w)) {
            ++previews;
        }
        kremaUnusable = kremaUnusable && !w.normal && !w.dialog;
    }
    REQUIRE(previews == 1);
    CHECK(kremaUnusable);
}

int main(int argc, char *argv[])
{
    if (argc > 1 && qstrcmp(argv[1], kControlArg) == 0) {
        return runControlWindow(argc, argv);
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
