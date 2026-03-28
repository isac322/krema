// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "application.h"

#include "krema.h"
#include "models/dockactions.h"
#include "models/dockmodel.h"
#include "models/notificationtracker.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/dockvisibilitycontroller.h"
#include "shell/multidockmanager.h"

#include <KAboutData>
#include <KActionCollection>
#include <KCrash>
#include <KDBusService>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <LayerShellQt/Shell>

#include <QAction>
#include <QLoggingCategory>
#include <QQuickStyle>
#include <QtQml>

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

    // Initialize KDE crash handler (must be called early)
    KCrash::initialize();

    // Set up KDE application metadata (required for KGlobalAccel, D-Bus, etc.)
    KAboutData aboutData(QStringLiteral("krema"), i18n("Krema"), QStringLiteral(KREMA_VERSION_STRING), i18n("A dock for KDE Plasma 6"), KAboutLicense::GPL_V3);
    aboutData.addAuthor(i18n("Byeonghoon Yoo"), {}, QStringLiteral("bhyoo@bhyoo.com"));
    aboutData.setOrganizationDomain(QByteArrayLiteral("bhyoo.com"));
    KAboutData::setApplicationData(aboutData);
    setDesktopFileName(QStringLiteral("com.bhyoo.krema"));

    // Enforce single instance via D-Bus (exits if another instance is already running)
    KDBusService service(KDBusService::Unique);
    connect(&service, &KDBusService::activateRequested, this, [](const QStringList &args, const QString &) {
        qCInfo(lcApp) << "Another instance attempted to start, ignoring. args:" << args;
    });

    // Initialize Qt resources from static library
    initResources();

    // Must be called before any QWindow is created
    LayerShellQt::Shell::useLayerShell();

    // Load settings from KConfig (~/.config/kremarc)
    m_settings = std::make_unique<KremaSettings>();
    m_settings->load();

    // Create data model
    m_dockModel = std::make_unique<DockModel>();
    m_dockModel->setPinnedLaunchers(m_settings->pinnedLaunchers());

    // Create notification tracker (before QML loading)
    m_notificationTracker = std::make_unique<NotificationTracker>();

    // Register global QML singletons (must be before any QML loading)
    // Use qmlRegisterSingletonType (not qmlRegisterSingletonInstance) so multiple
    // QML engines (dock + settings window) can access the same C++ objects.
    auto *model = m_dockModel.get();
    qmlRegisterSingletonType<DockModel>("com.bhyoo.krema", 1, 0, "DockModel", [model](QQmlEngine *, QJSEngine *) -> QObject * {
        QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
        return model;
    });
    auto *settings = m_settings.get();
    qmlRegisterSingletonType<KremaSettings>("com.bhyoo.krema", 1, 0, "DockSettings", [settings](QQmlEngine *, QJSEngine *) -> QObject * {
        QQmlEngine::setObjectOwnership(settings, QQmlEngine::CppOwnership);
        return settings;
    });
    auto *tracker = m_notificationTracker.get();
    qmlRegisterSingletonType<NotificationTracker>("com.bhyoo.krema", 1, 0, "NotificationTracker", [tracker](QQmlEngine *, QJSEngine *) -> QObject * {
        QQmlEngine::setObjectOwnership(tracker, QQmlEngine::CppOwnership);
        return tracker;
    });

    // Create and initialize the multi-dock manager (creates DockShell(s) based on monitor mode)
    m_dockManager = std::make_unique<MultiDockManager>(m_settings.get(), m_dockModel.get(), m_notificationTracker.get(), this);
    m_dockManager->initialize();

    // Apply initial virtual desktop filter setting
    m_dockModel->setFilterByVirtualDesktop(m_settings->filterByVirtualDesktop());

    // Auto-save pinned launchers when they change on any shell
    connect(m_dockManager.get(), &MultiDockManager::pinnedLaunchersChanged, this, [this]() {
        m_settings->setPinnedLaunchers(m_dockModel->pinnedLaunchers());
        m_settings->save();
    });

    // Auto-save on any setting change
    auto *s = m_settings.get();
    auto saveSettings = [this]() {
        m_settings->save();
    };
    connect(s, &KremaSettings::IconSizeChanged, this, saveSettings);
    connect(s, &KremaSettings::IconSpacingChanged, this, saveSettings);
    connect(s, &KremaSettings::MaxZoomFactorChanged, this, saveSettings);
    connect(s, &KremaSettings::CornerRadiusChanged, this, saveSettings);
    connect(s, &KremaSettings::FloatingChanged, this, saveSettings);
    connect(s, &KremaSettings::BackgroundOpacityChanged, this, saveSettings);
    connect(s, &KremaSettings::BackgroundStyleChanged, this, saveSettings);
    connect(s, &KremaSettings::TintColorChanged, this, saveSettings);
    connect(s, &KremaSettings::VisibilityModeChanged, this, saveSettings);
    connect(s, &KremaSettings::EdgeChanged, this, saveSettings);
    connect(s, &KremaSettings::ShowDelayChanged, this, saveSettings);
    connect(s, &KremaSettings::HideDelayChanged, this, saveSettings);
    connect(s, &KremaSettings::PreviewEnabledChanged, this, saveSettings);
    connect(s, &KremaSettings::PreviewThumbnailSizeChanged, this, saveSettings);
    connect(s, &KremaSettings::PreviewHoverDelayChanged, this, saveSettings);
    connect(s, &KremaSettings::PreviewHideDelayChanged, this, saveSettings);
    connect(s, &KremaSettings::ShadowEnabledChanged, this, saveSettings);
    connect(s, &KremaSettings::ShadowLightXChanged, this, saveSettings);
    connect(s, &KremaSettings::ShadowLightYChanged, this, saveSettings);
    connect(s, &KremaSettings::ShadowLightZChanged, this, saveSettings);
    connect(s, &KremaSettings::ShadowLightRadiusChanged, this, saveSettings);
    connect(s, &KremaSettings::ShadowColorChanged, this, saveSettings);
    connect(s, &KremaSettings::ShadowIntensityChanged, this, saveSettings);
    connect(s, &KremaSettings::ShadowElevationChanged, this, saveSettings);
    connect(s, &KremaSettings::IconNormalizationChanged, this, saveSettings);
    connect(s, &KremaSettings::AttentionAnimationChanged, this, saveSettings);
    connect(s, &KremaSettings::FilterByVirtualDesktopChanged, this, saveSettings);
    connect(s, &KremaSettings::MonitorModeChanged, this, saveSettings);
    connect(s, &KremaSettings::FollowActiveTriggerChanged, this, saveSettings);
    connect(s, &KremaSettings::ScreenTransitionChanged, this, saveSettings);

    // Virtual desktop filter toggle
    connect(s, &KremaSettings::FilterByVirtualDesktopChanged, this, [this]() {
        m_dockModel->setFilterByVirtualDesktop(m_settings->filterByVirtualDesktop());
    });

    // Monitor mode change
    connect(s, &KremaSettings::MonitorModeChanged, this, [this]() {
        m_dockManager->setMonitorMode(static_cast<MultiDockManager::MonitorMode>(m_settings->monitorMode()));
    });

    // The dock window is now created with layer-shell.
    // Unset the env var so child processes (launched apps) don't inherit it.
    qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");

    // Register global shortcuts (KGlobalAccel)
    registerGlobalShortcuts();

    return exec();
}

void Application::registerGlobalShortcuts()
{
    m_actionCollection = new KActionCollection(this, QStringLiteral("krema"));
    auto *kga = KGlobalAccel::self();

    // Toggle dock visibility: Meta+`
    auto *toggleAction = m_actionCollection->addAction(QStringLiteral("toggle-dock"));
    toggleAction->setText(i18nc("@action global shortcut", "Toggle Dock"));
    kga->setDefaultShortcut(toggleAction, {QKeySequence(Qt::META | Qt::Key_QuoteLeft)});
    kga->setShortcut(toggleAction, {QKeySequence(Qt::META | Qt::Key_QuoteLeft)});
    connect(toggleAction, &QAction::triggered, this, [this]() {
        if (auto *shell = m_dockManager->primaryShell()) {
            shell->view()->visibilityController()->toggleVisibility();
        }
    });

    // Focus dock for keyboard navigation: Meta+F5
    // In multi-monitor mode, focuses the dock on the screen containing the cursor.
    auto *focusDockAction = m_actionCollection->addAction(QStringLiteral("focus-dock"));
    focusDockAction->setText(i18nc("@action global shortcut", "Focus Dock"));
    kga->setDefaultShortcut(focusDockAction, {QKeySequence(Qt::META | Qt::Key_F5)});
    kga->setShortcut(focusDockAction, {QKeySequence(Qt::META | Qt::Key_F5)});
    connect(focusDockAction, &QAction::triggered, this, [this]() {
        if (auto *shell = m_dockManager->shellAtCursor()) {
            shell->focusDock();
        }
    });

    // Meta+1..9: Activate N-th app (targets primary dock)
    for (int i = 1; i <= 9; ++i) {
        auto *activateAction = m_actionCollection->addAction(QStringLiteral("activate-entry-%1").arg(i));
        activateAction->setText(i18nc("@action global shortcut", "Activate Entry %1", i));
        const auto seq = QKeySequence(Qt::META | static_cast<Qt::Key>(Qt::Key_1 + i - 1));
        kga->setDefaultShortcut(activateAction, {seq});
        kga->setShortcut(activateAction, {seq});
        connect(activateAction, &QAction::triggered, this, [this, i]() {
            if (auto *shell = m_dockManager->primaryShell()) {
                shell->actions()->activate(i - 1);
            }
        });
    }

    // Meta+Shift+1..9: New instance of N-th app (targets primary dock)
    for (int i = 1; i <= 9; ++i) {
        auto *newInstanceAction = m_actionCollection->addAction(QStringLiteral("new-instance-entry-%1").arg(i));
        newInstanceAction->setText(i18nc("@action global shortcut", "New Instance of Entry %1", i));
        const auto seq = QKeySequence(Qt::META | Qt::SHIFT | static_cast<Qt::Key>(Qt::Key_1 + i - 1));
        kga->setDefaultShortcut(newInstanceAction, {seq});
        kga->setShortcut(newInstanceAction, {seq});
        connect(newInstanceAction, &QAction::triggered, this, [this, i]() {
            if (auto *shell = m_dockManager->primaryShell()) {
                shell->actions()->newInstance(i - 1);
            }
        });
    }

    qCDebug(lcApp) << "Global shortcuts registered";
}

} // namespace krema
