// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "application.h"

#include "config/docksettings.h"
#include "models/dockmodel.h"
#include "platform/dockplatform.h"
#include "platform/dockplatformfactory.h"
#include "shell/dockview.h"
#include "shell/dockvisibilitycontroller.h"
#include "shell/settingswindow.h"

#include <KAboutData>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <LayerShellQt/Shell>

#include <QAction>
#include <QLoggingCategory>
#include <QQmlContext>
#include <QQuickStyle>

Q_LOGGING_CATEGORY(lcApp, "krema.app")

// Static library resources must be explicitly initialized.
// Must be called from global namespace, not inside krema namespace.
static void initResources()
{
    Q_INIT_RESOURCE(qml);
}

namespace krema
{

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
{
}

Application::~Application() = default;

int Application::run()
{
    // Ensure Qt Quick Controls use the KDE Plasma style (needed for Kirigami theming)
    if (QQuickStyle::name().isEmpty()) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    // Set up KDE application metadata (required for KGlobalAccel, D-Bus, etc.)
    KAboutData aboutData(QStringLiteral("krema"), i18n("Krema"), QStringLiteral("0.1.0"), i18n("A dock for KDE Plasma 6"), KAboutLicense::GPL_V3);
    KAboutData::setApplicationData(aboutData);
    setDesktopFileName(QStringLiteral("org.krema"));

    // Initialize Qt resources from static library
    initResources();

    // Must be called before any QWindow is created
    LayerShellQt::Shell::useLayerShell();

    // Create platform backend
    auto platform = DockPlatformFactory::create();
    if (!platform) {
        qCCritical(lcApp) << "No platform backend available";
        return 1;
    }

    // Load settings from KConfig (~/.config/kremarc)
    m_settings = std::make_unique<DockSettings>();

    // Create data model
    m_dockModel = std::make_unique<DockModel>();
    m_dockModel->setPinnedLaunchers(m_settings->pinnedLaunchers());

    // Create the dock view
    m_dockView = std::make_unique<DockView>(std::move(platform));
    m_dockView->rootContext()->setContextProperty(QStringLiteral("dockModel"), m_dockModel.get());

    // Apply saved settings to dock view
    m_dockView->setIconSize(m_settings->iconSize());
    m_dockView->setIconSpacing(m_settings->iconSpacing());
    m_dockView->setMaxZoomFactor(m_settings->maxZoomFactor());
    m_dockView->setCornerRadius(m_settings->cornerRadius());
    m_dockView->setFloating(m_settings->floating());

    m_dockView->initialize(m_dockModel->tasksModel(),
                           m_dockModel->virtualDesktopInfo(),
                           m_dockModel->activityInfo(),
                           static_cast<DockPlatform::Edge>(m_settings->edge()),
                           static_cast<DockPlatform::VisibilityMode>(m_settings->visibilityMode()));

    // Auto-save pinned launchers when they change (pin/unpin from context menu)
    connect(m_dockModel.get(), &DockModel::pinnedLaunchersChanged, this, [this]() {
        m_settings->setPinnedLaunchers(m_dockModel->pinnedLaunchers());
    });

    // Apply initial delay settings to visibility controller
    m_dockView->visibilityController()->setShowDelay(m_settings->showDelay());
    m_dockView->visibilityController()->setHideDelay(m_settings->hideDelay());

    // Context menu interaction lock: dock stays visible while menu is open
    connect(m_dockModel.get(), &DockModel::contextMenuVisibleChanged, m_dockView->visibilityController(), &DockVisibilityController::setInteracting);

    // Settings dialog (lazy-loaded on first open)
    m_settingsWindow = std::make_unique<SettingsWindow>(m_settings.get(), this);
    connect(m_dockModel.get(), &DockModel::settingsRequested, this, [this]() {
        m_settingsWindow->show();
    });

    // Settings window interaction lock: dock stays visible while settings is open
    connect(m_settingsWindow.get(), &SettingsWindow::visibleChanged, m_dockView->visibilityController(), &DockVisibilityController::setInteracting);

    // Apply settings changes to dock view in real time
    connect(m_settings.get(), &DockSettings::settingsChanged, this, &Application::applySettings);

    // The dock window is now created with layer-shell.
    // Unset the env var so child processes (launched apps) don't inherit it.
    // Otherwise launched apps create layer-shell surfaces → no decorations,
    // not visible in task switcher (Ctrl+Tab), etc.
    qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");

    // Register global shortcuts (KGlobalAccel)
    registerGlobalShortcuts();

    return exec();
}

void Application::applySettings()
{
    m_dockView->setIconSize(m_settings->iconSize());
    m_dockView->setIconSpacing(m_settings->iconSpacing());
    m_dockView->setMaxZoomFactor(m_settings->maxZoomFactor());
    m_dockView->setCornerRadius(m_settings->cornerRadius());
    m_dockView->setFloating(m_settings->floating());

    // Edge and visibility mode require platform-level changes
    m_dockView->platform()->setEdge(static_cast<DockPlatform::Edge>(m_settings->edge()));
    m_dockView->visibilityController()->setMode(static_cast<DockPlatform::VisibilityMode>(m_settings->visibilityMode()));

    // Apply delay settings
    m_dockView->visibilityController()->setShowDelay(m_settings->showDelay());
    m_dockView->visibilityController()->setHideDelay(m_settings->hideDelay());
}

void Application::registerGlobalShortcuts()
{
    auto *kga = KGlobalAccel::self();

    // Toggle dock visibility: Meta+`
    auto *toggleAction = new QAction(this);
    toggleAction->setObjectName(QStringLiteral("toggle-dock"));
    toggleAction->setText(i18n("Toggle Dock"));
    kga->setDefaultShortcut(toggleAction, {QKeySequence(Qt::META | Qt::Key_QuoteLeft)});
    kga->setShortcut(toggleAction, {QKeySequence(Qt::META | Qt::Key_QuoteLeft)});
    connect(toggleAction, &QAction::triggered, m_dockView->visibilityController(), &DockVisibilityController::toggleVisibility);

    // Meta+1..9: Activate N-th app
    for (int i = 1; i <= 9; ++i) {
        auto *activateAction = new QAction(this);
        activateAction->setObjectName(QStringLiteral("activate-entry-%1").arg(i));
        activateAction->setText(i18n("Activate Entry %1", i));
        const auto seq = QKeySequence(Qt::META | static_cast<Qt::Key>(Qt::Key_1 + i - 1));
        kga->setDefaultShortcut(activateAction, {seq});
        kga->setShortcut(activateAction, {seq});
        connect(activateAction, &QAction::triggered, this, [this, i]() {
            m_dockModel->activate(i - 1);
        });
    }

    // Meta+Shift+1..9: New instance of N-th app
    for (int i = 1; i <= 9; ++i) {
        auto *newInstanceAction = new QAction(this);
        newInstanceAction->setObjectName(QStringLiteral("new-instance-entry-%1").arg(i));
        newInstanceAction->setText(i18n("New Instance of Entry %1", i));
        const auto seq = QKeySequence(Qt::META | Qt::SHIFT | static_cast<Qt::Key>(Qt::Key_1 + i - 1));
        kga->setDefaultShortcut(newInstanceAction, {seq});
        kga->setShortcut(newInstanceAction, {seq});
        connect(newInstanceAction, &QAction::triggered, this, [this, i]() {
            m_dockModel->newInstance(i - 1);
        });
    }

    qCDebug(lcApp) << "Global shortcuts registered";
}

} // namespace krema
