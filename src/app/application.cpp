// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "application.h"

#include "models/dockmodel.h"
#include "platform/dockplatformfactory.h"
#include "shell/dockview.h"

#include <LayerShellQt/Shell>

#include <QLoggingCategory>
#include <QQmlContext>

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

    // Create data model
    m_dockModel = std::make_unique<DockModel>();

    // Default pinned launchers (can be loaded from config later)
    m_dockModel->setPinnedLaunchers({
        QStringLiteral("applications:org.kde.dolphin.desktop"),
        QStringLiteral("applications:org.kde.konsole.desktop"),
        QStringLiteral("applications:org.kde.kate.desktop"),
        QStringLiteral("applications:systemsettings.desktop"),
    });

    // Create the dock view
    m_dockView = std::make_unique<DockView>(std::move(platform));
    m_dockView->rootContext()->setContextProperty(QStringLiteral("dockModel"), m_dockModel.get());

    m_dockView->initialize(m_dockModel->tasksModel());

    return exec();
}

} // namespace krema
