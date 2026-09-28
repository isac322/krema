// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QRect>
#include <QRegion>

namespace krema
{

/// Thickness of the edge strip that reveals a hidden dock (and, in Follow
/// Active mode with the mouse trigger, moves the dock to that screen).
inline constexpr int kEdgeTriggerThickness = 4;

struct InputRegionParams {
    int surfaceWidth;
    int surfaceHeight;
    int panelX;
    int panelY;
    int panelWidth;
    int panelHeight;
    int zoomOverflowHeight;
    bool visible;
    bool hovered;
    int triggerStripHeight = kEdgeTriggerThickness;
    int margin = 4;
    int edge = 1; // 0=Top, 1=Bottom, 2=Left, 3=Right
};

/// Compute the input region mask for the dock surface.
QRegion computeDockInputRegion(const InputRegionParams &p);

struct DockScreenRectParams {
    int screenX;
    int screenY;
    int screenWidth;
    int screenHeight;
    int surfaceWidth;
    int surfaceHeight;
    int panelX;
    int panelRefY;
    int panelWidth;
    int panelHeight;
    int edge; // 0=Top, 1=Bottom, 2=Left, 3=Right
};

/// Compute the dock panel rectangle in screen coordinates.
QRect computeDockScreenRect(const DockScreenRectParams &p);

} // namespace krema
