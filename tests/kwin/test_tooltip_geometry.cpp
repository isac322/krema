// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// The launcher tooltip must lie fully inside the dock surface on every edge.
//
// Left and right docks open the tooltip beside the panel, so the surface
// needs room for the tooltip's width there, not just the height a horizontal
// dock reserves above/below its panel. Anything outside the layer surface is
// clipped by the compositor. Found while auditing PR #15.
//
// Drives the real MultiDockManager against a KWin virtual compositor with a
// pinned launcher and opens the tooltip the way hovering does (hoveredIndex
// change -> tooltip timer -> launcher tooltip).
//
// Must run under run-with-kwin.sh.

#include "krema.h"
#include "models/dockmodel.h"
#include "models/notificationtracker.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/multidockmanager.h"

#include <KAboutData>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QQuickItem>
#include <QQuickStyle>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTest>
#include <QtQml>

#include <memory>

static void initResources()
{
    Q_INIT_RESOURCE(qml);
}

namespace
{

constexpr int kTimeoutMs = 15000;
const QString kLauncherName = QStringLiteral("Krema Tooltip Geometry Test Launcher");

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

// Pins one launcher (no window) so the dock shows a launcher tooltip for it.
void pinLauncher()
{
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    REQUIRE(QDir().mkpath(dataHome + QStringLiteral("/applications")));
    QFile desktop(dataHome + QStringLiteral("/applications/krema-tooltiptest.desktop"));
    REQUIRE(desktop.open(QIODevice::WriteOnly));
    desktop.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=%1\nExec=true\nIcon=application-x-executable\n").arg(kLauncherName).toUtf8());
    desktop.close();
    QProcess::execute(QStringLiteral("kbuildsycoca6"), {});
    app().model->setPinnedLaunchers({QUrl::fromLocalFile(desktop.fileName()).toString()});
}

} // namespace

TEST_CASE("Launcher tooltip lies inside the dock surface on every edge", "[tooltip]")
{
    using Edge = krema::DockPlatform::Edge;
    pinLauncher();
    const auto restore = qScopeGuard([] {
        app().settings->setEdge(static_cast<int>(Edge::Bottom));
    });

    const Edge edge = GENERATE(Edge::Left, Edge::Right, Edge::Bottom, Edge::Top);
    INFO("edge " << static_cast<int>(edge));
    app().settings->setEdge(static_cast<int>(edge));

    krema::MultiDockManager manager(app().settings.get(), app().model.get(), app().tracker.get());
    manager.initialize();
    REQUIRE(QTest::qWaitFor(
        [&] {
            return app().model->tasksModel()->rowCount() > 0;
        },
        kTimeoutMs));

    // What hovering the first icon does: set the hovered item, which starts
    // the tooltip timer; a launcher-only task then shows the text tooltip.
    // The manager may still replace the shell while it adopts the output
    // order, so every poll looks the dock up again and re-hovers a new one.
    QQuickView *view = nullptr;
    QQuickItem *tooltip = nullptr;
    REQUIRE(QTest::qWaitFor(
        [&] {
            auto *shell = manager.primaryShell();
            view = shell ? shell->view() : nullptr;
            QQuickItem *root = view && view->isExposed() ? view->rootObject() : nullptr;
            tooltip = root ? root->findChild<QQuickItem *>(QStringLiteral("dockTooltip")) : nullptr;
            if (!tooltip) {
                return false;
            }
            if (root->property("hoveredIndex").toInt() != 0) {
                root->setProperty("hoveredName", kLauncherName);
                root->setProperty("hoveredIndex", 0);
            }
            return tooltip->isVisible() && tooltip->width() > 0;
        },
        kTimeoutMs));

    const QRectF surface(0, 0, view->width(), view->height());
    const QRectF tip(tooltip->x(), tooltip->y(), tooltip->width(), tooltip->height());
    INFO("surface " << surface.width() << "x" << surface.height() << ", tooltip " << tip.x() << "," << tip.y() << " " << tip.width() << "x" << tip.height());
    CHECK(surface.contains(tip));

    // An ordinary app name is shown in full, not elided to fit.
    bool truncated = true;
    for (QQuickItem *child : tooltip->childItems()) {
        if (child->property("truncated").isValid()) {
            truncated = child->property("truncated").toBool();
        }
    }
    CHECK_FALSE(truncated);
}

int main(int argc, char *argv[])
{
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
