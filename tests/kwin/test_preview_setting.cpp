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
#include "models/notificationtracker.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/multidockmanager.h"
#include "shell/previewcontroller.h"

#include <taskmanager/abstracttasksmodel.h>
#include <taskmanager/tasksmodel.h>

#include <KAboutData>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QApplication>
#include <QFile>
#include <QProcess>
#include <QQuickItem>
#include <QQuickStyle>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QWidget>
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
const QString kHoveredTitle = QStringLiteral("krema-preview-hovered");
const QString kOtherTitle = QStringLiteral("krema-preview-other");

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

TEST_CASE("A task row appearing while a window is hovered respects the preview setting", "[preview]")
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
