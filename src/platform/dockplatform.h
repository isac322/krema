// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QMargins>
#include <QRegion>
#include <QSize>
#include <QWindow>

namespace krema
{

/**
 * Abstract platform interface for dock window management.
 *
 * Encapsulates display-server-specific logic (Wayland layer-shell, X11 NET_WM)
 * so the rest of the application stays platform-agnostic.
 */
class DockPlatform
{
public:
    enum class Edge {
        Top,
        Bottom,
        Left,
        Right,
    };

    enum class VisibilityMode {
        AlwaysVisible, ///< Dock is always shown, reserves exclusive zone
        AlwaysHidden, ///< Dock is always hidden, revealed by edge hover
        DodgeWindows, ///< Dock hides when a window overlaps it
        SmartHide, ///< Dock hides when any window overlaps it
    };

    virtual ~DockPlatform() = default;

    /// Configure platform-specific properties on @p window before it is shown.
    virtual void setupWindow(QWindow *window) = 0;

    /// Anchor the dock to the given screen edge.
    virtual void setEdge(Edge edge) = 0;

    /// Reserve screen space so other windows do not overlap the dock.
    /// Pass 0 to disable (auto-hide), or the dock height/width.
    virtual void setExclusiveZone(int zone) = 0;

    /// Set the gap between the dock and the screen edge (floating mode).
    virtual void setMargin(int margin) = 0;

    /// Switch between always-visible and auto-hide modes.
    virtual void setVisibilityMode(VisibilityMode mode) = 0;

    /// Set the desired surface size for the dock window.
    /// For layer-shell: a dimension of 0 means "stretch to fill" in that direction.
    virtual void setSize(const QSize &size) = 0;

    /// Set the input region for the dock window.
    /// Used to define a thin trigger strip when the dock is hidden.
    virtual void setInputRegion(const QRegion &region) = 0;

    /// Current edge.
    [[nodiscard]] virtual Edge edge() const = 0;
};

} // namespace krema
