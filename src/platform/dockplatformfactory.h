// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include "dockplatform.h"

#include <memory>

namespace krema
{

/**
 * Creates the appropriate DockPlatform for the current display server.
 *
 * Detection is based on QGuiApplication::platformName():
 *   "wayland" → WaylandDockPlatform
 *   "xcb"     → X11DockPlatform (not yet implemented)
 */
class DockPlatformFactory
{
public:
    /// Create a platform backend. Must be called after QGuiApplication is constructed.
    static std::unique_ptr<DockPlatform> create();
};

} // namespace krema
