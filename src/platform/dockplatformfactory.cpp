// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "dockplatformfactory.h"

#include "waylanddockplatform.h"

#include <QGuiApplication>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcPlatformFactory, "krema.platform.factory")

namespace krema
{

std::unique_ptr<DockPlatform> DockPlatformFactory::create()
{
    const QString platform = QGuiApplication::platformName();
    qCInfo(lcPlatformFactory) << "Detected platform:" << platform;

    if (platform == QLatin1String("wayland")) {
        return std::make_unique<WaylandDockPlatform>();
    }

    // TODO: X11DockPlatform for "xcb" platform
    qCCritical(lcPlatformFactory) << "Unsupported platform:" << platform << "— only Wayland is currently supported.";
    return nullptr;
}

} // namespace krema
