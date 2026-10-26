// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// With "Enable window preview" off, a task row appearing while a window task
// is hovered must not open the preview popup. The dock re-checks the hovered
// item whenever TasksModel inserts a row (so a launcher that just gained its
// window switches to the preview); that path once skipped the setting, so a
// preview flashed after clicking a launcher even with previews disabled.
//
// Drives the real MultiDockManager against a KWin virtual compositor. The
// windows come from child processes (this binary started with --child or
// --other) so they are ordinary xdg toplevels of two different apps.
//
// Must run under run-with-kwin.sh.

#include "krema.h"
#include "models/dockmodel.h"
#include "models/launcherentrytracker.h"
#include "models/notificationtracker.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/dockvisibilitycontroller.h"
#include "shell/multidockmanager.h"
#include "shell/outputordermonitor.h"
#include "shell/previewcontroller.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <KAboutData>
#include <LayerShellQt/Window>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QApplication>
#include <QFile>
#include <QProcess>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QWidget>
#include <QtQml>

#include <cmath>
#include <memory>

static void initResources()
{
    Q_INIT_RESOURCE(qml);
}

namespace
{

using TaskManager::AbstractTasksModel;

constexpr int kTimeoutMs = 15000;
const QString kHoveredTitle = QStringLiteral("krema-preview-hovered");
const QString kOtherTitle = QStringLiteral("krema-preview-other");

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

int windowMain(int argc, char *argv[], const QString &desktopFileName, const QString &title)
{
    QApplication::setDesktopFileName(desktopFileName);
    QApplication application(argc, argv);
    QWidget window;
    window.setWindowTitle(title);
    window.resize(200, 200);
    window.show();
    return application.exec();
}

// Path of the executable for @p mode. Without a desktop entry for their app
// ids, libtaskmanager identifies both children by their executable, which
// would group them as one app. So --other runs from a copy of this binary
// under another name, in run-with-kwin.sh's scratch runtime directory.
QString childExecutable(const QString &mode)
{
    const QString self = QCoreApplication::applicationFilePath();
    if (mode != QLatin1String("--other")) {
        return self;
    }
    static const QString copy = [&self] {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/krema-preview-other-app");
        QFile::remove(path);
        QFile::copy(self, path);
        return path;
    }();
    return copy;
}

// Starts this binary in @p mode as an ordinary xdg-shell app; the returned
// guard kills it.
auto startChild(QProcess &process, const QString &mode)
{
    auto env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("QT_WAYLAND_SHELL_INTEGRATION"));
    process.setProcessEnvironment(env);
    process.start(childExecutable(mode), {mode});
    return qScopeGuard([&process] {
        process.kill();
        process.waitForFinished();
    });
}

// Row of the window task titled @p title, or -1.
int windowRow(const QString &title)
{
    auto *tasks = app().model->tasksModel();
    for (int row = 0; row < tasks->rowCount(); ++row) {
        const QModelIndex idx = tasks->index(row, 0);
        if (idx.data(AbstractTasksModel::IsWindow).toBool() && idx.data(Qt::DisplayRole).toString() == title) {
            return row;
        }
    }
    return -1;
}

} // namespace

TEST_CASE(
    "A task row appearing while a window is hovered respects the preview "
    "setting",
    "[preview]")
{
    const bool previewEnabled = GENERATE(false, true);
    INFO("previewEnabled " << previewEnabled);
    app().settings->setPreviewEnabled(previewEnabled);
    // Keep the hover timer out of the way: only the row-insertion path may
    // open the preview in this test.
    const int hoverDelay = app().settings->previewHoverDelay();
    app().settings->setPreviewHoverDelay(10 * kTimeoutMs);
    const auto restore = qScopeGuard([hoverDelay] {
        app().settings->setPreviewEnabled(true);
        app().settings->setPreviewHoverDelay(hoverDelay);
    });

    // Windows of children killed by the previous run can linger for a moment.
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowRow(kHoveredTitle) < 0 && windowRow(kOtherTitle) < 0;
        },
        kTimeoutMs));

    // Match Application::run(): wait for the compositor's final output order
    // before creating the shell, so a later primary-output update cannot
    // destroy the QML root while this scenario is driving task insertion.
    auto *outputOrder = krema::OutputOrderMonitor::instance();
    REQUIRE(QTest::qWaitFor(
        [outputOrder] {
            return outputOrder->orderReady();
        },
        kTimeoutMs));

    krema::MultiDockManager manager(app().settings.get(), app().model.get(), app().tracker.get());
    manager.initialize();

    QProcess hoveredApp;
    const auto stopHovered = startChild(hoveredApp, QStringLiteral("--child"));
    REQUIRE(hoveredApp.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowRow(kHoveredTitle) >= 0;
        },
        kTimeoutMs));
    const int hoveredRow = windowRow(kHoveredTitle);

    // Hover the window task the way the mouse does (hoveredIndex change). The
    // manager may still replace the shell while it adopts the output order,
    // so every poll looks the dock up again and re-hovers a new one; the
    // shell must also stay the same for a moment before the test goes on.
    krema::DockShell *shell = nullptr;
    QQuickItem *root = nullptr;
    int stablePolls = 0;
    REQUIRE(QTest::qWaitFor(
        [&] {
            auto *current = manager.primaryShell();
            auto *view = current ? current->view() : nullptr;
            QQuickItem *currentRoot = view && view->isExposed() ? view->rootObject() : nullptr;
            if (!currentRoot) {
                stablePolls = 0;
                return false;
            }
            if (current != shell || currentRoot != root) {
                shell = current;
                root = currentRoot;
                stablePolls = 0;
            }
            if (root->property("hoveredIndex").toInt() != hoveredRow) {
                root->setProperty("hoveredName", kHoveredTitle);
                root->setProperty("hoveredIndex", hoveredRow);
            }
            return ++stablePolls > 20;
        },
        kTimeoutMs));
    krema::PreviewController *preview = shell->previewController();
    REQUIRE_FALSE(preview->isVisible());
    QSignalSpy shown(preview, &krema::PreviewController::visibleChanged);

    // Another app opens a window: TasksModel inserts a row.
    QProcess otherApp;
    const auto stopOther = startChild(otherApp, QStringLiteral("--other"));
    REQUIRE(otherApp.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowRow(kOtherTitle) >= 0;
        },
        kTimeoutMs));
    REQUIRE(root->property("hoveredIndex").toInt() == windowRow(kHoveredTitle));

    if (previewEnabled) {
        // Healthy control: the insertion opens the hovered window's preview.
        CHECK(QTest::qWaitFor(
            [&] {
                return preview->isVisible();
            },
            kTimeoutMs));
    } else {
        // Let queued work settle; the popup must never open, not even briefly.
        QTest::qWait(1000);
        CHECK_FALSE(preview->isVisible());
        CHECK(shown.isEmpty());
    }
}

TEST_CASE("Preview margins follow the visible panel gap and animation reserve on every edge", "[preview][shadow]")
{
    const int edge = GENERATE(0, 1, 2, 3);
    auto *settings = app().settings.get();
    const auto restore = qScopeGuard([settings,
                                      oldEdge = settings->edge(),
                                      visibility = settings->visibilityMode(),
                                      reserve = settings->reserveScreenSpace(),
                                      floating = settings->floating(),
                                      shadow = settings->shadowEnabled(),
                                      lightX = settings->shadowLightX(),
                                      lightY = settings->shadowLightY(),
                                      lightZ = settings->shadowLightZ(),
                                      elevation = settings->shadowElevation(),
                                      radius = settings->shadowLightRadius()] {
        settings->setEdge(oldEdge);
        settings->setVisibilityMode(visibility);
        settings->setReserveScreenSpace(reserve);
        settings->setFloating(floating);
        settings->setShadowEnabled(shadow);
        settings->setShadowLightX(lightX);
        settings->setShadowLightY(lightY);
        settings->setShadowLightZ(lightZ);
        settings->setShadowElevation(elevation);
        settings->setShadowLightRadius(radius);
    });
    settings->setEdge(edge);
    settings->setVisibilityMode(0);
    settings->setReserveScreenSpace(false);
    settings->setFloating(false);
    settings->setShadowEnabled(false);
    settings->setShadowLightX(-150);
    settings->setShadowLightY(-150);
    settings->setShadowLightZ(100);
    settings->setShadowElevation(15);
    settings->setShadowLightRadius(5.0);

    auto *outputOrder = krema::OutputOrderMonitor::instance();
    REQUIRE(QTest::qWaitFor(
        [outputOrder] {
            return outputOrder->orderReady();
        },
        kTimeoutMs));
    krema::MultiDockManager manager(settings, app().model.get(), app().tracker.get());
    manager.initialize();
    auto *shell = manager.primaryShell();
    REQUIRE(shell);
    auto *dock = shell->view();
    REQUIRE(QTest::qWaitFor(
        [dock] {
            return dock->isExposed() && dock->rootObject();
        },
        kTimeoutMs));

    QQuickView *preview = nullptr;
    for (auto *window : QGuiApplication::topLevelWindows()) {
        auto *view = qobject_cast<QQuickView *>(window);
        if (view && view->source() == QUrl(QStringLiteral("qrc:/qml/PreviewPopup.qml"))) {
            preview = view;
            break;
        }
    }
    REQUIRE(preview);
    auto *layer = LayerShellQt::Window::get(preview);
    REQUIRE(layer);
    const auto margin = [&] {
        const auto margins = layer->margins();
        switch (edge) {
        case 0:
            return margins.top();
        case 1:
            return margins.bottom();
        case 2:
            return margins.left();
        default:
            return margins.right();
        }
    };
    // The visible gap is the requested panel inset. The native preview margin
    // follows the visible panel depth plus animation extent, without a
    // shadow-only dead-band.
    for (const bool shadow : {false, true, false}) {
        settings->setShadowEnabled(shadow);
        int depth = 0;
        int gap = 0;
        int expectedGap = 0;
        int expectedMargin = 0;
        const bool followsPanel = QTest::qWaitFor(
            [&] {
                const QRect panel = dock->visibilityController()->panelRect();
                if (panel.isEmpty()) {
                    return false;
                }
                switch (edge) {
                case 0:
                    depth = panel.y() + panel.height();
                    gap = panel.y();
                    break;
                case 1:
                    depth = dock->height() - panel.y();
                    gap = dock->height() - panel.y() - panel.height();
                    break;
                case 2:
                    depth = panel.x() + panel.width();
                    gap = panel.x();
                    break;
                default:
                    depth = dock->width() - panel.x();
                    gap = dock->width() - panel.x() - panel.width();
                    break;
                }
                expectedGap = dock->floatingPadding();
                expectedMargin = depth + dock->animationOverflowHeight();
                // QML publishes its panel rectangle asynchronously. Do not accept
                // the previous rectangle and margin immediately after a toggle.
                if (gap != expectedGap) {
                    return false;
                }
                // QML's visible background omits the 4 px indicator gap.
                return margin() >= expectedMargin && margin() <= expectedMargin + 4;
            },
            kTimeoutMs);
        CAPTURE(edge,
                shadow,
                depth,
                gap,
                expectedGap,
                margin(),
                expectedMargin,
                dock->shadowOverflow(),
                dock->animationOverflowHeight());
        REQUIRE(followsPanel);
    }
}

TEST_CASE("Visible preview input follows shadow and native surface resizes", "[preview][shadow-input]")
{
    const int edge = GENERATE(1, 3);
    auto *settings = app().settings.get();
    const auto restore = qScopeGuard([settings,
                                      oldEdge = settings->edge(),
                                      visibility = settings->visibilityMode(),
                                      reserve = settings->reserveScreenSpace(),
                                      floating = settings->floating(),
                                      shadow = settings->shadowEnabled(),
                                      lightX = settings->shadowLightX(),
                                      lightY = settings->shadowLightY(),
                                      lightZ = settings->shadowLightZ(),
                                      elevation = settings->shadowElevation(),
                                      radius = settings->shadowLightRadius(),
                                      iconSize = settings->iconSize(),
                                      zoom = settings->maxZoomFactor(),
                                      hoverDelay = settings->previewHoverDelay()] {
        settings->setEdge(oldEdge);
        settings->setVisibilityMode(visibility);
        settings->setReserveScreenSpace(reserve);
        settings->setFloating(floating);
        settings->setShadowEnabled(shadow);
        settings->setShadowLightX(lightX);
        settings->setShadowLightY(lightY);
        settings->setShadowLightZ(lightZ);
        settings->setShadowElevation(elevation);
        settings->setShadowLightRadius(radius);
        settings->setIconSize(iconSize);
        settings->setMaxZoomFactor(zoom);
        settings->setPreviewHoverDelay(hoverDelay);
    });
    settings->setEdge(edge);
    settings->setVisibilityMode(0);
    settings->setReserveScreenSpace(false);
    settings->setFloating(false);
    settings->setShadowEnabled(true);
    settings->setShadowLightX(200);
    settings->setShadowLightY(200);
    settings->setShadowLightZ(100);
    settings->setShadowElevation(50);
    // Keep a fixed-size popup smaller than the remaining depth on the
    // wrapper's 1024x768 output, while both edges exercise a depth clamp.
    settings->setIconSize(edge == 1 ? 48 : 96);
    settings->setMaxZoomFactor(edge == 1 ? 1.6 : 2.0);
    settings->setShadowLightRadius(edge == 1 ? 5.0 : 20.0);
    settings->setPreviewHoverDelay(10 * kTimeoutMs);
    auto *outputOrder = krema::OutputOrderMonitor::instance();
    REQUIRE(QTest::qWaitFor(
        [&] {
            return outputOrder->orderReady();
        },
        kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowRow(kHoveredTitle) < 0;
        },
        kTimeoutMs));
    krema::MultiDockManager manager(settings, app().model.get(), app().tracker.get());
    manager.initialize();
    auto *shell = manager.primaryShell();
    REQUIRE(shell);
    auto *dock = shell->view();
    REQUIRE(QTest::qWaitFor(
        [&] {
            return dock->isExposed() && dock->rootObject();
        },
        kTimeoutMs));
    QProcess child;
    const auto stopChild = startChild(child, QStringLiteral("--child"));
    REQUIRE(child.waitForStarted(kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [] {
            return windowRow(kHoveredTitle) >= 0;
        },
        kTimeoutMs));
    auto *controller = shell->previewController();
    controller->showPreview(windowRow(kHoveredTitle), edge == 1 ? dock->x() + dock->width() / 2 : dock->y() + dock->height() / 2, 48);
    controller->setPreviewHovered(true);
    REQUIRE(controller->isVisible());
    QQuickView *preview = nullptr;
    for (auto *window : QGuiApplication::topLevelWindows()) {
        auto *view = qobject_cast<QQuickView *>(window);
        if (view && view->source() == QUrl(QStringLiteral("qrc:/qml/PreviewPopup.qml"))) {
            preview = view;
            break;
        }
    }
    REQUIRE(preview);
    QQuickItem *popup = nullptr;
    REQUIRE(QTest::qWaitFor(
        [&] {
            for (auto *item : preview->rootObject()->childItems()) {
                if (item->isVisible() && item->width() == controller->contentWidth() && item->height() == controller->contentHeight()) {
                    popup = item;
                    return true;
                }
            }
            return false;
        },
        kTimeoutMs));
    const auto popupRegion = [&] {
        return QRegion(popup->mapRectToScene(QRectF(0, 0, popup->width(), popup->height())).toAlignedRect().intersected(QRect(QPoint(0, 0), preview->size())));
    };
    QSizeF observedSize;
    int stablePolls = 0;
    REQUIRE(QTest::qWaitFor(
        [&] {
            const QSizeF size(popup->width(), popup->height());
            if (size != observedSize || size.width() < 100 || size.height() < 100) {
                observedSize = size;
                stablePolls = 0;
                return false;
            }
            return ++stablePolls > 20 && preview->mask() == popupRegion();
        },
        kTimeoutMs));
    const QSizeF contentSize(popup->width(), popup->height());
    const QRegion oldMask = preview->mask();
    const int oldDepth = edge == 1 ? preview->height() : preview->width();
    if (edge == 1) {
        settings->setShadowLightY(250);
    } else {
        settings->setShadowLightX(250);
    }
    REQUIRE(QTest::qWaitFor(
        [&] {
            return (edge == 1 ? preview->height() : preview->width()) == oldDepth
                && preview->mask() == oldMask
                && QSizeF(popup->width(), popup->height()) == contentSize;
        },
        kTimeoutMs));
    REQUIRE(controller->isVisible());
    REQUIRE((edge == 1 ? preview->height() : preview->width()) == oldDepth);
    REQUIRE(preview->mask() == oldMask);
    REQUIRE(QSizeF(popup->width(), popup->height()) == contentSize);

    // A layer-shell configure can change size without calling updateEdge.
    // The mask must follow that native event too, with content unchanged.
    const QSize oldSize = preview->size();
    const QSize nativeSize = oldSize - (edge == 1 ? QSize(0, 20) : QSize(20, 0));
#ifdef KREMA_COMPAT_NO_LAYERSHELL_DESIRED_SIZE
    preview->resize(nativeSize);
#else
    LayerShellQt::Window::get(preview)->setDesiredSize(nativeSize);
    preview->update();
#endif
    REQUIRE(QTest::qWaitFor(
        [&] {
            return preview->size() == nativeSize;
        },
        kTimeoutMs));
    REQUIRE(QTest::qWaitFor(
        [&] {
            return preview->mask() == popupRegion();
        },
        kTimeoutMs));
    REQUIRE(QSizeF(popup->width(), popup->height()) == contentSize);
    controller->hidePreview();
    REQUIRE(preview->mask() == QRegion(0, 0, 1, 1));
}

int main(int argc, char *argv[])
{
    if (argc > 1 && qstrcmp(argv[1], "--child") == 0) {
        return windowMain(argc, argv, QStringLiteral("krema-previewtest"), kHoveredTitle);
    }
    if (argc > 1 && qstrcmp(argv[1], "--other") == 0) {
        return windowMain(argc, argv, QStringLiteral("krema-previewother"), kOtherTitle);
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
