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
inline int surfaceHeight(int iconSize, int padding, double maxZoomFactor, int tooltipReserve, int floatingPadding)
{
    const int dockHeight = iconSize + padding * 2;
    const int zoomOverflow = zoomOverflowHeight(iconSize, maxZoomFactor);

    const int topOverflow = std::max(zoomOverflow, tooltipReserve);
    const int bottomOverflow = floatingPadding;

    return dockHeight + topOverflow + bottomOverflow;
}

/// Height of the visible panel bar (icon + padding + floating gap).
inline int panelBarHeight(int iconSize, int padding, int floatingPadding)
{
    return iconSize + padding * 2 + floatingPadding;
}

} // namespace krema
