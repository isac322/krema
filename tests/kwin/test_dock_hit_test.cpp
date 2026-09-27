// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Pointer hit testing of dock icons on every screen edge.
//
// main.qml maps the pointer to (primary = along the dock, secondary = depth)
// and updateHoveredItem() picks the icon whose zoomed bounds contain it. The
// secondary-axis bounds used to be hard-coded for a bottom dock, so on left
// and right docks only the first icon could be hovered or clicked, and on a
// top dock the zoomed part of an icon did not react.
//
// Drives the real MultiDockManager against a KWin virtual compositor and
// feeds synthetic pointer events into the dock window, so the whole QML path
// (MouseArea -> updateHoveredItem -> onClicked -> DockActions) is exercised.
//
// Must run under run-with-kwin.sh, which provides WAYLAND_DISPLAY, a private
// session bus and throwaway XDG directories.

#include "krema.h"
#include "models/dockactions.h"
#include "models/dockmodel.h"
#include "models/notificationtracker.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/multidockmanager.h"
#include "shell/outputordermonitor.h"

#include <KAboutData>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QQuickItem>
#include <QQuickStyle>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
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

using Edge = krema::DockPlatform::Edge;

constexpr int kTimeoutMs = 15000;
constexpr int kLauncherCount = 5;

struct App {
    std::unique_ptr<KremaSettings> settings;
    std::unique_ptr<krema::DockModel> model;
    std::unique_ptr<krema::NotificationTracker> tracker;
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
        return a;
    }();
    return *instance;
}

QString launcherName(int i)
{
    return QStringLiteral("Krema Hit %1").arg(QChar(u'A' + i));
}

// Pins kLauncherCount launchers whose activation is harmless and observable
// only through DockActions::taskLaunching.
void pinLaunchers()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/applications");
    REQUIRE(QDir().mkpath(dir));
    QStringList urls;
    for (int i = 0; i < kLauncherCount; ++i) {
        QFile desktop(dir + QStringLiteral("/krema-hit-%1.desktop").arg(i));
        REQUIRE(desktop.open(QIODevice::WriteOnly | QIODevice::Truncate));
        desktop.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=%1\nExec=true\nStartupNotify=false\nIcon=utilities-terminal\n")
                          .arg(launcherName(i))
                          .toUtf8());
        desktop.close();
        urls.append(QUrl::fromLocalFile(desktop.fileName()).toString());
    }
    QProcess::execute(QStringLiteral("kbuildsycoca6"), {});
    app().settings->setPinnedLaunchers(urls);
    app().model->setPinnedLaunchers(urls);
}

void collectDelegates(QQuickItem *item, QList<QQuickItem *> &out)
{
    for (auto *child : item->childItems()) {
        if (child->property("itemCenterX").isValid() && child->property("displayName").isValid()) {
            out.append(child);
        }
        collectDelegates(child, out);
    }
}

// Dock icon delegates in dock order (along the primary axis).
QList<QQuickItem *> delegates(krema::DockView *view)
{
    QList<QQuickItem *> items;
    if (view->rootObject()) {
        collectDelegates(view->rootObject(), items);
    }
    std::sort(items.begin(), items.end(), [](QQuickItem *a, QQuickItem *b) {
        return a->property("itemCenterX").toReal() < b->property("itemCenterX").toReal();
    });
    return items;
}

// Unscaled icon rectangle in window coordinates (the Scale transform is
// applied on top of the item's own geometry, so map the parent's rect).
QRectF baseRect(QQuickItem *item)
{
    return item->parentItem()->mapRectToScene(QRectF(item->position(), item->size()));
}

// A point inside the zoomed icon but outside its unscaled rect: past the
// icon's inner side (away from the screen edge), where the zoom grows.
QPointF zoomedExtensionPoint(const QRectF &r, Edge edge, qreal scale)
{
    const QPointF c = r.center();
    switch (edge) {
    case Edge::Top:
        return {c.x(), r.bottom() + r.height() * (scale - 1.0) / 2.0};
    case Edge::Bottom:
        return {c.x(), r.top() - r.height() * (scale - 1.0) / 2.0};
    case Edge::Left:
        return {r.right() + r.width() * (scale - 1.0) / 2.0, c.y()};
    case Edge::Right:
        return {r.left() - r.width() * (scale - 1.0) / 2.0, c.y()};
    }
    return c;
}

const char *edgeName(Edge edge)
{
    switch (edge) {
    case Edge::Top:
        return "top";
    case Edge::Bottom:
        return "bottom";
    case Edge::Left:
        return "left";
    case Edge::Right:
        return "right";
    }
    return "?";
}

void runEdge(Edge edge)
{
    INFO("edge " << edgeName(edge));
    pinLaunchers();
    app().settings->setEdge(static_cast<int>(edge));
    const auto restore = qScopeGuard([] {
        app().settings->setEdge(static_cast<int>(Edge::Bottom));
    });

    // Like Application::run(): create shells only once the Plasma output
    // order is known, otherwise the shell is recreated underneath the test.
    REQUIRE(QTest::qWaitFor(
        [] {
            return krema::OutputOrderMonitor::instance()->orderReady();
        },
        kTimeoutMs));
    auto manager = std::make_unique<krema::MultiDockManager>(app().settings.get(), app().model.get(), app().tracker.get());
    manager->initialize();
    auto *shell = manager->primaryShell();
    REQUIRE(shell);
    auto *view = shell->view();
    REQUIRE(QTest::qWaitForWindowExposed(view, kTimeoutMs));
    REQUIRE(manager->primaryShell() == shell);

    // Wait until all launchers are laid out and the slide-in and add/move
    // transitions have settled (geometry unchanged for 500 ms).
    QList<QRectF> previous;
    QElapsedTimer unchanged;
    unchanged.start();
    REQUIRE(QTest::qWaitFor(
        [&] {
            const auto items = delegates(view);
            QList<QRectF> now;
            for (auto *item : items) {
                now.append(baseRect(item));
            }
            if (items.size() != kLauncherCount || now != previous) {
                previous = now;
                unchanged.restart();
                return false;
            }
            return unchanged.elapsed() >= 500;
        },
        kTimeoutMs));

    const qreal maxZoom = app().settings->maxZoomFactor();
    REQUIRE(maxZoom > 1.2);

    QSignalSpy launching(shell->actions(), &krema::DockActions::taskLaunching);
    QObject *root = view->rootObject();

    for (int i : {0, kLauncherCount / 2, kLauncherCount - 1}) {
        INFO("icon " << i);
        auto items = delegates(view);
        REQUIRE(items.size() == kLauncherCount);
        QQuickItem *icon = items.at(i);
        REQUIRE(icon->property("displayName").toString() == launcherName(i));
        const QRectF r = baseRect(icon);

        // hoveredIndex once it equals i, or its value after a short wait.
        auto hoveredAfterWait = [&] {
            QTest::qWaitFor(
                [&] {
                    return root->property("hoveredIndex").toInt() == i;
                },
                2000);
            return root->property("hoveredIndex").toInt();
        };

        // Icon center selects the icon.
        QTest::mouseMove(view, r.center().toPoint());
        const int centerHovered = hoveredAfterWait();
        qInfo("HIT edge=%s icon=%d where=center at=(%d,%d) hovered=%d", edgeName(edge), i, r.center().toPoint().x(), r.center().toPoint().y(), centerHovered);
        CHECK(centerHovered == i);

        // Let the zoom grow, then hover the zoomed part of the icon.
        CHECK(QTest::qWaitFor(
            [&] {
                return icon->property("currentScale").toReal() > maxZoom - 0.05;
            },
            2000));
        const QPoint ext = zoomedExtensionPoint(r, edge, maxZoom).toPoint();
        QTest::mouseMove(view, ext);
        const int extHovered = hoveredAfterWait();
        qInfo("HIT edge=%s icon=%d where=zoomed at=(%d,%d) scale=%.2f hovered=%d",
              edgeName(edge),
              i,
              ext.x(),
              ext.y(),
              icon->property("currentScale").toReal(),
              extHovered);
        CHECK(extHovered == i);

        // Clicking there activates exactly this launcher.
        launching.clear();
        QTest::mouseClick(view, Qt::LeftButton, {}, ext);
        QTest::qWaitFor(
            [&] {
                return !launching.isEmpty();
            },
            2000);
        const int activated = launching.isEmpty() ? -1 : launching.first().first().toInt();
        qInfo("HIT edge=%s icon=%d where=click at=(%d,%d) activated=%d", edgeName(edge), i, ext.x(), ext.y(), activated);
        CHECK(activated == i);
    }
}

} // namespace

TEST_CASE("Hovering and clicking selects the icon under the pointer on every edge", "[hit-test]")
{
    SECTION("bottom")
    {
        runEdge(Edge::Bottom);
    }
    SECTION("top")
    {
        runEdge(Edge::Top);
    }
    SECTION("left")
    {
        runEdge(Edge::Left);
    }
    SECTION("right")
    {
        runEdge(Edge::Right);
    }
}

int main(int argc, char *argv[])
{
    // Opt into the layer-shell platform plugin, like the real dock.
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
