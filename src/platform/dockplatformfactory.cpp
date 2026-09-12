// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockplatformfactory.h"

#include "hyprlanddockplatform.h"
#include "waylanddockplatform.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QProcessEnvironment>

Q_LOGGING_CATEGORY(lcPlatformFactory, "krema.platform.factory")

namespace krema
{

std::unique_ptr<DockPlatform> DockPlatformFactory::create()
{
    const QString platform = QGuiApplication::platformName();
    qCInfo(lcPlatformFactory) << "Detected platform:" << platform;

    if (platform == QLatin1String("wayland")) {
        const auto env = QProcessEnvironment::systemEnvironment();
        const QString desktop = env.value(QStringLiteral("XDG_CURRENT_DESKTOP")).toLower();
        const bool isHyprland = desktop.contains(QStringLiteral("hyprland")) || env.contains(QStringLiteral("HYPRLAND_INSTANCE_SIGNATURE"));

        if (isHyprland) {
            qCInfo(lcPlatformFactory) << "Using HyprlandDockPlatform";
            return std::make_unique<HyprlandDockPlatform>();
        }

        qCInfo(lcPlatformFactory) << "Using WaylandDockPlatform (KDE/Generic)";
        return std::make_unique<WaylandDockPlatform>();
    }

    // TODO: X11DockPlatform for "xcb" platform
    qCCritical(lcPlatformFactory) << "Unsupported platform:" << platform << "— only Wayland is currently supported.";
    return nullptr;
}

} // namespace krema
