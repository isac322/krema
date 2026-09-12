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
#include "utils/debugmanager.h"

#include <KAboutData>
#include <KActionCollection>
#include <KCrash>
#include <KDBusService>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <LayerShellQt/Shell>

#include <QAction>
#include <QCommandLineParser>
#include <QLoggingCategory>
#include <QQuickStyle>
#include <QtQml>

#include <iostream>

void kremaLogHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    using namespace Qt::StringLiterals;
    QByteArray localMsg = msg.toLocal8Bit();
    QString category = QString::fromLatin1(context.category);

    // ANSI Color Codes
    const char *reset = "\x1b[0m";
    const char *red = "\x1b[31m";

    const char *color = reset;
    bool enabled = true;

    // Map categories to DebugManager
    krema::DebugManager::Category catType = krema::DebugManager::Count;
    if (category == u"krema.app"_s)
        catType = krema::DebugManager::App;
    else if (category == u"krema.geom"_s)
        catType = krema::DebugManager::Geom;
    else if (category == u"krema.input"_s)
        catType = krema::DebugManager::Input;
    else if (category == u"krema.anim"_s)
        catType = krema::DebugManager::Anim;
    else if (category == u"krema.preview"_s)
        catType = krema::DebugManager::Preview;
    else if (category == u"krema.model"_s)
        catType = krema::DebugManager::Model;
    else if (category == u"krema.shell"_s)
        catType = krema::DebugManager::Shell;
    else if (category == u"krema.shader"_s)
        catType = krema::DebugManager::Shader;
    else if (category == u"krema.config"_s)
        catType = krema::DebugManager::Config;

    // Filter based on flags (Warnings/Errors/Criticals always pass)
    if (type == QtDebugMsg || type == QtInfoMsg) {
        if (catType != krema::DebugManager::Count) {
            enabled = krema::DebugManager::self()->isEnabled(catType);
        }
    }

    if (!enabled)
        return;

    // Determine Tag and Color
    QString tag = category;
    if (tag.startsWith(u"krema."_s))
        tag.remove(0, 6);
    tag = tag.toUpper();
    if (tag == u"DEFAULT"_s)
        tag = u"DEBUG"_s;

    if (catType != krema::DebugManager::Count) {
        color = krema::DebugManager::categoryColor(catType);
        // Special bold overrides for mandatory mandates
        if (catType == krema::DebugManager::Geom)
            color = "\x1b[1;34m"; // Bold Blue
        if (catType == krema::DebugManager::Input)
            color = "\x1b[1;33m"; // Bold Yellow
        if (catType == krema::DebugManager::Config)
            color = "\x1b[1;35m"; // Bold Magenta
    }

    if (type == QtWarningMsg || type == QtCriticalMsg)
        color = red;

    std::fprintf(stderr, "%s[%s]%s %s\n", color, tag.toLocal8Bit().constData(), reset, localMsg.constData());
}

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
    // Install the global color interceptor immediately
    qInstallMessageHandler(kremaLogHandler);

    // Set up KDE application metadata
    KAboutData aboutData(QStringLiteral("krema"), i18n("Krema"), QStringLiteral(KREMA_VERSION_STRING), i18n("A dock for KDE Plasma 6"), KAboutLicense::GPL_V3);
    aboutData.addAuthor(i18n("Byeonghoon Yoo"), {}, QStringLiteral("bhyoo@bhyoo.com"));
    aboutData.setOrganizationDomain(QByteArrayLiteral("bhyoo.com"));
    KAboutData::setApplicationData(aboutData);
    setDesktopFileName(QStringLiteral("com.bhyoo.krema"));

    // --- Command Line Parser ---
    QCommandLineParser parser;
    parser.setApplicationDescription(i18n("Krema Dock for KDE Plasma 6"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption debugAll(QStringLiteral("debug-all"), i18n("Enable all debug logs"));
    QCommandLineOption debugGeom(QStringLiteral("debug-geom"), i18n("Enable geometry debug logs"));
    QCommandLineOption debugInput(QStringLiteral("debug-input"), i18n("Enable input/hover debug logs"));
    QCommandLineOption debugAnim(QStringLiteral("debug-anim"), i18n("Enable animation debug logs"));
    QCommandLineOption debugPreview(QStringLiteral("debug-preview"), i18n("Enable preview/popup debug logs"));
    QCommandLineOption debugModel(QStringLiteral("debug-model"), i18n("Enable model/data debug logs"));
    QCommandLineOption debugShell(QStringLiteral("debug-shell"), i18n("Enable shell/platform debug logs"));
    QCommandLineOption debugApp(QStringLiteral("debug-app"), i18n("Enable core application debug logs"));
    QCommandLineOption debugConfig(QStringLiteral("debug-config"), i18n("Enable configuration desync debug logs"));
    QCommandLineOption debugIcons(QStringLiteral("debug-icons"), i18n("Enable icon resolution debug logs"));

    parser.addOptions({debugAll, debugGeom, debugInput, debugAnim, debugPreview, debugModel, debugShell, debugApp, debugConfig, debugIcons});
    parser.process(*this);

    auto *dm = DebugManager::self();
    if (parser.isSet(debugAll)) {
        for (int i = 0; i < DebugManager::Count; ++i)
            dm->setEnabled(static_cast<DebugManager::Category>(i), true);
    } else {
        if (parser.isSet(debugGeom))
            dm->setEnabled(DebugManager::Geom, true);
        if (parser.isSet(debugInput))
            dm->setEnabled(DebugManager::Input, true);
        if (parser.isSet(debugAnim))
            dm->setEnabled(DebugManager::Anim, true);
        if (parser.isSet(debugPreview))
            dm->setEnabled(DebugManager::Preview, true);
        if (parser.isSet(debugModel))
            dm->setEnabled(DebugManager::Model, true);
        if (parser.isSet(debugShell))
            dm->setEnabled(DebugManager::Shell, true);
        if (parser.isSet(debugApp))
            dm->setEnabled(DebugManager::App, true);
        if (parser.isSet(debugConfig))
            dm->setEnabled(DebugManager::Config, true);
    }

    // Ensure Qt Quick Controls use the KDE Plasma style
    if (QQuickStyle::name().isEmpty()) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    KCrash::initialize();

    // Enforce single instance via D-Bus
    KDBusService service(KDBusService::Unique);
    connect(&service, &KDBusService::activateRequested, this, [](const QStringList &args, const QString &) {
        qCInfo(lcApp) << "Another instance attempted to start, ignoring. args:" << args;
    });

    initResources();

    m_settings = std::make_unique<KremaSettings>();
    m_settings->load();

    m_dockModel = std::make_unique<DockModel>();
    m_dockModel->setPinnedLaunchers(m_settings->pinnedLaunchers());

    m_notificationTracker = std::make_unique<NotificationTracker>();

    // Register global QML singletons
    qmlRegisterSingletonInstance("com.bhyoo.krema", 1, 0, "KremaDebug", dm);

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

    // Apply initial virtual desktop display mode
    m_dockModel->setVirtualDesktopMode(m_settings->virtualDesktopMode());

    // Auto-save pinned launchers when they change on any shell
    connect(m_dockManager.get(), &MultiDockManager::pinnedLaunchersChanged, this, [this]() {
        m_settings->setPinnedLaunchers(m_dockModel->pinnedLaunchers());
        m_settings->save();
    });

    // Auto-save on any setting change
    auto *s = m_settings.get();
    connect(s, &KremaSettings::configChanged, this, [s]() {
        s->save();
    });

    // Virtual desktop mode change
    connect(s, &KremaSettings::VirtualDesktopModeChanged, this, [this]() {
        m_dockModel->setVirtualDesktopMode(m_settings->virtualDesktopMode());
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
