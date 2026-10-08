// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "application.h"

#include "krema.h"
#include "models/dockactions.h"
#include "models/dockmodel.h"
#include "models/launcherentrytracker.h"
#include "models/notificationtracker.h"
#include "models/taskiconprovider.h"
#include "shell/dockshell.h"
#include "shell/dockview.h"
#include "shell/dockvisibilitycontroller.h"
#include "shell/multidockmanager.h"
#include "shell/outputordermonitor.h"

#include <KAboutData>
#include <KActionCollection>
#include <KConfigGroup>
#include <KCrash>
#include <KDBusService>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QAction>
#include <QIcon>
#include <QLoggingCategory>
#include <QQuickStyle>
#include <QtQml>

#include <algorithm>

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

Application::~Application()
{
    TaskIconProvider::clearRawIcons();
}

void Application::connectSettingsAutoSave(KremaSettings *settings, QObject *context)
{
    auto saveSettings = [settings]() {
        settings->save();
    };
    connect(settings, &KremaSettings::IconSizeChanged, context, saveSettings);
    connect(settings, &KremaSettings::IconSpacingChanged, context, saveSettings);
    connect(settings, &KremaSettings::MaxZoomFactorChanged, context, saveSettings);
    connect(settings, &KremaSettings::ZoomStyleChanged, context, saveSettings);
    connect(settings, &KremaSettings::ZoomAnimationPresetChanged, context, saveSettings);
    connect(settings, &KremaSettings::ZoomInDurationChanged, context, saveSettings);
    connect(settings, &KremaSettings::ZoomOutDurationChanged, context, saveSettings);
    connect(settings, &KremaSettings::ZoomInEasingChanged, context, saveSettings);
    connect(settings, &KremaSettings::ZoomOutEasingChanged, context, saveSettings);
    connect(settings, &KremaSettings::CornerRadiusChanged, context, saveSettings);
    connect(settings, &KremaSettings::FloatingChanged, context, saveSettings);
    connect(settings, &KremaSettings::BackgroundOpacityChanged, context, saveSettings);
    connect(settings, &KremaSettings::BackgroundStyleChanged, context, saveSettings);
    connect(settings, &KremaSettings::TintColorChanged, context, saveSettings);
    connect(settings, &KremaSettings::VisibilityModeChanged, context, saveSettings);
    connect(settings, &KremaSettings::EdgeChanged, context, saveSettings);
    connect(settings, &KremaSettings::ShowDelayChanged, context, saveSettings);
    connect(settings, &KremaSettings::HideDelayChanged, context, saveSettings);
    connect(settings, &KremaSettings::PreviewEnabledChanged, context, saveSettings);
    connect(settings, &KremaSettings::SingleWindowClickActionChanged, context, saveSettings);
    connect(settings, &KremaSettings::PreviewThumbnailSizeChanged, context, saveSettings);
    connect(settings, &KremaSettings::PreviewHoverDelayChanged, context, saveSettings);
    connect(settings, &KremaSettings::PreviewHideDelayChanged, context, saveSettings);
    connect(settings, &KremaSettings::ShadowEnabledChanged, context, saveSettings);
    connect(settings, &KremaSettings::GroupedWindowClickActionChanged, context, saveSettings);
    connect(settings, &KremaSettings::ShadowLightXChanged, context, saveSettings);
    connect(settings, &KremaSettings::ShadowLightYChanged, context, saveSettings);
    connect(settings, &KremaSettings::ShadowLightZChanged, context, saveSettings);
    connect(settings, &KremaSettings::ShadowLightRadiusChanged, context, saveSettings);
    connect(settings, &KremaSettings::ShadowColorChanged, context, saveSettings);
    connect(settings, &KremaSettings::ShadowIntensityChanged, context, saveSettings);
    connect(settings, &KremaSettings::ShadowElevationChanged, context, saveSettings);
    connect(settings, &KremaSettings::IconNormalizationChanged, context, saveSettings);
    connect(settings, &KremaSettings::AttentionAnimationChanged, context, saveSettings);
    connect(settings, &KremaSettings::VirtualDesktopModeChanged, context, saveSettings);
    connect(settings, &KremaSettings::OtherDesktopOpacityChanged, context, saveSettings);
    connect(settings, &KremaSettings::MonitorModeChanged, context, saveSettings);
    connect(settings, &KremaSettings::SelectedOutputsChanged, context, saveSettings);
    connect(settings, &KremaSettings::FollowActiveTriggerChanged, context, saveSettings);
    connect(settings, &KremaSettings::ScreenTransitionChanged, context, saveSettings);
    connect(settings, &KremaSettings::IconScaleChanged, context, saveSettings);
    connect(settings, &KremaSettings::AttentionAnimationDurationChanged, context, saveSettings);
    connect(settings, &KremaSettings::BadgeDisplayModeChanged, context, saveSettings);
    connect(settings, &KremaSettings::UseSystemColorChanged, context, saveSettings);
    connect(settings, &KremaSettings::UseAccentColorChanged, context, saveSettings);
    connect(settings, &KremaSettings::DodgeActiveOnlyChanged, context, saveSettings);
    connect(settings, &KremaSettings::ReserveScreenSpaceChanged, context, saveSettings);
    connect(settings, &KremaSettings::SeparateLaunchersChanged, context, saveSettings);
}

void Application::migrateLegacySettings(KremaSettings *settings)
{
    // Krema 0.10 stored a single ZoomAnimationDuration (only when changed from
    // its 100 ms default) applied with an ease-out curve to both directions.
    // Map it to the Custom preset with the same duration and easing, so the
    // upgraded dock keeps exactly the old feel, and drop the obsolete key.
    // "General" is the krema.kcfg group holding the zoom entries.
    KConfigGroup group = settings->config()->group(QStringLiteral("General"));
    const QString legacyKey = QStringLiteral("ZoomAnimationDuration");
    if (!group.hasKey(legacyKey)) {
        return;
    }

    if (!group.hasKey(QStringLiteral("ZoomAnimationPreset"))) {
        constexpr int customPreset = 4;
        constexpr int easeOut = 2;
        const int duration = std::clamp(group.readEntry(legacyKey, 100), 0, 1000);
        settings->setZoomAnimationPreset(customPreset);
        settings->setZoomInDuration(duration);
        settings->setZoomOutDuration(duration);
        settings->setZoomInEasing(easeOut);
        settings->setZoomOutEasing(easeOut);
    }

    group.deleteEntry(legacyKey);
    settings->save();
}

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
    aboutData.setHomepage(QStringLiteral("https://krema.bhyoo.com/"));
    aboutData.setBugAddress(QByteArrayLiteral("https://github.com/isac322/krema/issues"));
    KAboutData::setApplicationData(aboutData);
    setDesktopFileName(QStringLiteral("com.bhyoo.krema"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("com.bhyoo.krema")));

    // Enforce single instance via D-Bus (exits if another instance is already running)
    KDBusService service(KDBusService::Unique);
    connect(&service, &KDBusService::activateRequested, this, [](const QStringList &args, const QString &) {
        qCInfo(lcApp) << "Another instance attempted to start, ignoring. args:" << args;
    });

    // Initialize Qt resources from static library
    initResources();

    // Opt into the layer-shell platform plugin. Equivalent to the deprecated
    // LayerShellQt::Shell::useLayerShell() (it only sets this variable); must
    // happen before the first QWindow is created.
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");

    // Load settings from KConfig (~/.config/kremarc)
    m_settings = std::make_unique<KremaSettings>();
    m_settings->load();
    migrateLegacySettings(m_settings.get());

    // Create data model
    m_dockModel = std::make_unique<DockModel>();
    m_dockModel->setPinnedLaunchers(m_settings->pinnedLaunchers());
    m_dockModel->setSeparateLaunchers(m_settings->separateLaunchers());

    // Create notification trackers (before QML loading)
    m_notificationTracker = std::make_unique<NotificationTracker>();
    m_launcherEntryTracker = std::make_unique<LauncherEntryTracker>();

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
    auto *launcherEntries = m_launcherEntryTracker.get();
    qmlRegisterSingletonType<LauncherEntryTracker>("com.bhyoo.krema", 1, 0, "LauncherEntryTracker", [launcherEntries](QQmlEngine *, QJSEngine *) -> QObject * {
        QQmlEngine::setObjectOwnership(launcherEntries, QQmlEngine::CppOwnership);
        return launcherEntries;
    });

    // Create and initialize the multi-dock manager (creates DockShell(s) based on monitor mode).
    // Placement uses the Plasma primary output (kde_output_order_v1); wait for
    // KWin's first order list when the protocol is present so the dock does not
    // briefly land on the wrong (first-announced) output before moving.
    m_dockManager = std::make_unique<MultiDockManager>(m_settings.get(), m_dockModel.get(), m_notificationTracker.get(), this);
    auto *outputOrder = OutputOrderMonitor::instance();
    if (outputOrder->orderReady()) {
        m_dockManager->initialize();
    } else {
        connect(outputOrder, &OutputOrderMonitor::orderReadyChanged, this, [this] {
            m_dockManager->initialize();
        });
    }

    // Apply initial virtual desktop display mode
    m_dockModel->setVirtualDesktopMode(m_settings->virtualDesktopMode());

    // Auto-save pinned launchers when they change on any shell
    connect(m_dockManager.get(), &MultiDockManager::pinnedLaunchersChanged, this, [this]() {
        m_settings->setPinnedLaunchers(m_dockModel->pinnedLaunchers());
        m_settings->save();
    });

    // Auto-save on any setting change
    auto *s = m_settings.get();
    connectSettingsAutoSave(s, this);

    // Virtual desktop mode change
    connect(s, &KremaSettings::VirtualDesktopModeChanged, this, [this]() {
        m_dockModel->setVirtualDesktopMode(m_settings->virtualDesktopMode());
    });

    // Keep task partitioning in sync with the live settings page.
    connect(s, &KremaSettings::SeparateLaunchersChanged, this, [this]() {
        m_dockModel->setSeparateLaunchers(m_settings->separateLaunchers());
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
        if (auto *shell = m_dockManager->activeShell()) {
            shell->view()->visibilityController()->toggleVisibility();
        }
    });

    // Focus dock for keyboard navigation: Meta+Alt+D (like Plasma's "Move keyboard
    // focus between panels", Meta+Alt+P). No stock Plasma 6 component claims it;
    // the former default Meta+F5 is KWin's "Move Mouse to Focus", which always wins
    // the key and makes kglobalacceld < 6.7 drop it from this action entirely.
    // In multi-monitor mode, focuses the dock on the screen containing the cursor;
    // in Follow Active mode, the dock on the active screen.
    auto *focusDockAction = m_actionCollection->addAction(QStringLiteral("focus-dock"));
    focusDockAction->setText(i18nc("@action global shortcut", "Focus Dock"));
    const QList<QKeySequence> focusDockShortcut{QKeySequence(Qt::META | Qt::ALT | Qt::Key_D)};
    kga->setDefaultShortcut(focusDockAction, focusDockShortcut);
    kga->setShortcut(focusDockAction, focusDockShortcut);
    // Autoloading keeps the stored shortcut. Move users still on the old default
    // (which never reaches krema on stock KWin) to the new one.
    if (kga->shortcut(focusDockAction) == QList<QKeySequence>{QKeySequence(Qt::META | Qt::Key_F5)}) {
        kga->setShortcut(focusDockAction, focusDockShortcut, KGlobalAccel::NoAutoloading);
    }
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
            if (auto *shell = m_dockManager->activeShell()) {
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
            if (auto *shell = m_dockManager->activeShell()) {
                shell->actions()->newInstance(i - 1);
            }
        });
    }

    qCDebug(lcApp) << "Global shortcuts registered";
}

} // namespace krema
