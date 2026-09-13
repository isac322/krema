// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <algorithm>
#include <cmath>

namespace krema
{

/// Extra height above the panel needed for zoomed icons.
inline int zoomOverflowHeight(int iconSize, double maxZoomFactor)
{
    return static_cast<int>(std::ceil(iconSize * (maxZoomFactor - 1.0)));
}

/// Total surface height including dock, zoom overflow / tooltip reserve, and floating padding.
/// Shadow is rendered within available space and naturally clips at surface boundaries.
/// @param triggerDistance cursor activation radius around the panel — the
///        surface extends beyond the panel on the edge-facing side by this
///        amount so mouse events arrive before the cursor reaches the icons.
inline int surfaceHeight(int iconSize, int padding, double maxZoomFactor, int tooltipReserve, int floatingPadding, int triggerDistance = 0)
{
    const int dockHeight = iconSize + padding * 2;
    const int zoomOverflow = zoomOverflowHeight(iconSize, maxZoomFactor);

    // Trigger radius extends the surface on BOTH depth sides of the panel
    // (toward the screen edge and away from it) so the cursor gets events
    // from any direction.
    const int trig = std::max(0, triggerDistance);
    const int topOverflow = std::max(zoomOverflow, tooltipReserve) + trig;
    const int bottomOverflow = floatingPadding + trig;

    return dockHeight + topOverflow + bottomOverflow;
}

/// Height of the visible panel bar (icon + padding + floating gap).
inline int panelBarHeight(int iconSize, int padding, int floatingPadding)
{
    return iconSize + padding * 2 + floatingPadding;
}

} // namespace krema
