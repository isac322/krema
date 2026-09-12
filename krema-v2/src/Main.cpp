#include <KDBusService>
#include <QCommandLineParser>
#include <QDebug>
#include <QGuiApplication>
#include <QMap>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>
#include <QSet>
#include <QTimer>

#include "KremaSettings.h"
#include "core/debug/KremaConsole.hpp"
#include "core/engine/BaseIsland.hpp"
#include "core/engine/BaseItem.hpp"
#include "core/engine/BasePanel.hpp"
#include "core/engine/LayoutManager.hpp"
#include "core/tasks/WorkspaceController.hpp"
#include "shell/hyprland/HyprlandProtocol.hpp"
#include "shell/hyprland/HyprlandTaskProvider.hpp"
#include "shell/kwin/KWinProtocol.hpp"
#include "shell/kwin/KWinTaskProvider.hpp"

// Forward declaration of the QML registration function we created earlier
namespace Krema
{
void registerQmlTypes();
extern bool g_debugGeom;
extern bool g_debugHit;
}

int main(int argc, char *argv[])
{
    // LayerShell needs this for proper identification
    qputenv("QT_QPA_PLATFORM", "wayland");

    QGuiApplication app(argc, argv);
    app.setOrganizationDomain(QStringLiteral("org.krema"));
    app.setOrganizationName(QStringLiteral("Krema"));
    app.setApplicationName(QStringLiteral("krema"));
    app.setApplicationDisplayName(QStringLiteral("Krema Dock"));
    app.setDesktopFileName(QStringLiteral("com.bhyoo.krema"));

    // Single Instance Protection
    KDBusService service(KDBusService::Unique);

    QCommandLineParser parser;
    parser.setApplicationDescription("Krema Dock for Plasma 6");
    parser.addHelpOption();

    QCommandLineOption debugGeomOption("debug-geom", "Enable real-time geometry logging to terminal.");
    parser.addOption(debugGeomOption);

    QCommandLineOption debugHitOption("debug-hit", "Enable hit-test diagnostics for repulsion math.");
    parser.addOption(debugHitOption);

    parser.process(app);

    Krema::g_debugGeom = parser.isSet(debugGeomOption);
    Krema::g_debugHit = parser.isSet(debugHitOption);

    // 1. Register C++/QML Types
    Krema::registerQmlTypes();

    QQmlApplicationEngine engine;

    // 2. Initialize Platform Protocol
    Krema::IProtocol *protocol = nullptr;
    Krema::ITaskProvider *taskProvider = nullptr;

    bool forceHyprland = qEnvironmentVariableIsSet("KREMA_FORCE_HYPRLAND");
    bool isHyprland = forceHyprland || qEnvironmentVariableIsSet("HYPRLAND_INSTANCE_SIGNATURE");

    if (isHyprland) {
        qDebug() << "[SHELL] Detecting Hyprland environment...";
        protocol = new Krema::HyprlandProtocol(&app);
        taskProvider = new Krema::HyprlandTaskProvider(&app);
    } else {
        qDebug() << "[SHELL] Defaulting to KWin environment...";
        protocol = new Krema::KWinProtocol(&app);
        taskProvider = new Krema::KWinTaskProvider(&app);
    }

    // 3. Initialize Core Data
    auto panel = new Krema::BasePanel(&app);
    auto layoutManager = new Krema::LayoutManager(&app);
    layoutManager->setPanel(panel);
    layoutManager->setTaskProvider(taskProvider);

    // App Island Logic
    auto appIsland = new Krema::BaseIsland(1, panel);
    panel->addIsland(appIsland);

    // Track active items to avoid race conditions and redundant deletions
    static QMap<QString, Krema::BaseItem *> activeItems;

    auto syncTasks = [appIsland, taskProvider, layoutManager]() {
        const auto tasks = taskProvider->tasks();
        QSet<QString> currentIds;
        for (const auto &task : tasks)
            currentIds.insert(task.id);

        // 1. Remove items no longer in the task list
        auto it = activeItems.begin();
        while (it != activeItems.end()) {
            if (!currentIds.contains(it.key())) {
                auto item = it.value();
                appIsland->removeItem(item);
                item->deleteLater();
                it = activeItems.erase(it);
            } else {
                ++it;
            }
        }

        // 2. Add or Update items
        for (const auto &task : tasks) {
            Krema::BaseItem *item = nullptr;
            if (activeItems.contains(task.id)) {
                item = activeItems[task.id];
            } else {
                item = new Krema::BaseItem(0, appIsland);
                item->setTaskId(task.id);
                activeItems[task.id] = item;
                appIsland->addItem(item);
                KREMA_PROTOCOL_LOG(QStringLiteral("SYNC: Added Item %1 to Island").arg(task.id));
            }

            item->setTaskName(task.name);
            item->setTaskIcon(task.icon);
            item->setIsRunning(task.isRunning);
            item->setContentSize(48);
            item->setUrgency(task.isUrgent ? Krema::UrgencyLevel::Critical : Krema::UrgencyLevel::Idle);
            item->setNotificationCount(task.winCount > 1 ? task.winCount : 0);
        }

        layoutManager->updateLayout();
    };

    QObject::connect(taskProvider, &Krema::ITaskProvider::tasksChanged, &app, syncTasks);
    syncTasks(); // Initial sync

    // Tray/System Island Logic
    auto systemIsland = new Krema::BaseIsland(2, panel);
    auto trayPlaceholder = new Krema::BaseItem(901, systemIsland);
    trayPlaceholder->setTaskName(QStringLiteral("System Tray (Placeholder)"));
    trayPlaceholder->setTaskIcon(QStringLiteral("preferences-system-details"));
    trayPlaceholder->setContentSize(48);
    systemIsland->addItem(trayPlaceholder);
    panel->addIsland(systemIsland);

    // M5.1: Clock/Status Island
    auto statusIsland = new Krema::BaseIsland(3, panel);
    auto clockItem = new Krema::BaseItem(1001, statusIsland);
    clockItem->setTaskName(QStringLiteral("Clock"));
    clockItem->setTaskIcon(QStringLiteral("preferences-system-time"));
    clockItem->setContentSize(48);
    statusIsland->addItem(clockItem);
    panel->addIsland(statusIsland);

    // Workspace Logic
    auto workspaceController = new Krema::WorkspaceController(taskProvider, &app);

    // Expose objects to QML root context
    engine.rootContext()->setContextProperty(QStringLiteral("kremaSettings"), KremaSettings::self());
    engine.rootContext()->setContextProperty(QStringLiteral("shellProtocol"), protocol);
    engine.rootContext()->setContextProperty(QStringLiteral("workspaceController"), workspaceController);
    engine.rootContext()->setContextProperty(QStringLiteral("globalPanel"), panel);
    engine.rootContext()->setContextProperty(QStringLiteral("layoutBrain"), layoutManager);
    engine.rootContext()->setContextProperty(QStringLiteral("isGeomDebug"), Krema::g_debugGeom);
    engine.rootContext()->setContextProperty(QStringLiteral("isHitDebug"), Krema::g_debugHit);

    // Provide screen info to QML for absolute coordinate debugging
    auto screen = app.primaryScreen();
    if (screen) {
        engine.rootContext()->setContextProperty(QStringLiteral("screenHeight"), screen->geometry().height());
    } else {
        engine.rootContext()->setContextProperty(QStringLiteral("screenHeight"), 1080);
    }

    const QUrl url(u"qrc:/org/krema/ui/MainDock.qml"_qs);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url, protocol, layoutManager, panel](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);

            auto window = qobject_cast<QQuickWindow *>(obj);
            if (window) {
                // EXPOSE THE BUG: Log the raw OS window position (Rule 14)
                QObject::connect(window, &QWindow::yChanged, [window]() {
                    KREMA_PROTOCOL_LOG(QStringLiteral("RAW OS WINDOW Y: %1").arg(window->y()));
                });

                // 1. IMMEDIATE REVEAL (Rule 14): Reveal as soon as protocol is ready
                QObject::connect(protocol, &Krema::IProtocol::initialized, window, [window, protocol]() {
                    // On Wayland, Y-coordinate checks (window->y() > 500) are unreliable.
                    // We reveal immediately to ensure the dock is interactive.
                    window->setVisible(true);
                    window->setOpacity(1.0);
                    KREMA_PROTOCOL_LOG(QStringLiteral("Protocol Initialized. Dock Revealed immediately."));

                    // Final anchor enforcement
                    protocol->setAnchors(Qt::BottomEdge);
                });

                // 2. ATTACH Dock personality (triggers initialized signal)
                protocol->setWindow(window);

                // 3. CONFIGURE EVERYTHING before any platform surface exists
                protocol->setLayer(Krema::IProtocol::Layer::Top);
                protocol->setAnchors(Qt::BottomEdge); // FIXED: No Left/Right stretch
                protocol->setMargins(0, 0, 0, 0);

                // Update window geometry when layout changes
                QObject::connect(layoutManager, &Krema::LayoutManager::layoutUpdated, [protocol, panel, window, layoutManager]() {
                    float maxScale = 1.0f;
                    for (auto island : panel->islands()) {
                        for (auto item : island->items()) {
                            maxScale = std::max(maxScale, item->scaleFactor());
                        }
                    }

                    // Rule 12: Interaction Buffer
                    // We make the window wider than the dock to "catch" the mouse early.
                    const int HOVER_BUFFER = 200;
                    int visualW = std::ceil(panel->width());
                    int windowW = visualW + (HOVER_BUFFER * 2);

                    int panelH = std::ceil(panel->thickness());
                    int floatOffset = std::ceil(panel->floatingOffset());

                    float maxZoom = layoutManager->maxZoomFactor();
                    int windowH = std::ceil(panelH * maxZoom + floatOffset + 20);

                    protocol->requestSize(QSize(windowW, windowH));
                    protocol->setExclusiveZone(panelH + floatOffset);

                    // The input region should cover the full buffered width to ensure
                    // we catch the mouse even before it hits the visual background.
                    static int lastW = 0;
                    if (std::abs(windowW - lastW) > 2) {
                        protocol->setInputRegion(QRect(0, 0, windowW, windowH));
                        lastW = windowW;
                    }

                    if (Krema::g_debugGeom && window->screen()) {
                        static QRect lastR;
                        QRect s = window->screen()->geometry();
                        int x = s.x() + (s.width() - windowW) / 2;
                        int y = s.y() + s.height() - windowH;
                        if (QRect(x, y, windowW, windowH) != lastR) {
                            lastR = QRect(x, y, windowW, windowH);
                            KREMA_GEOM_LOG(QStringLiteral("Win:%1x%2 (Visual:%3) | Scale:%4").arg(windowW).arg(windowH).arg(visualW).arg(maxScale, 0, 'f', 2));
                        }
                    }
                });

                layoutManager->updateLayout();
            }
        },
        Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
