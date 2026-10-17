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

/// Extra reserve needed while a zoomed icon performs its largest bounce.
///
/// DockItem scales its complete icon cell (the icon plus the fixed indicator
/// cell) around the screen-edge side, then applies the child bounce translate.
/// Both the transformed cell extent and the largest attention bounce therefore
/// have to be included in the outward reserve.  The indicator cell mirrors
/// DockItem's `4 + Kirigami.Units.smallSpacing` (8 px on supported themes).
inline int animationOverflowHeight(int iconSize, int padding, double maxZoomFactor)
{
    constexpr double indicatorSpace = 8.0;
    constexpr double maxBounceDistance = 14.0;
    const double scale = std::max(1.0, maxZoomFactor);
    const double transformedCellGrowth = (iconSize + indicatorSpace) * (scale - 1.0);
    const double transformedBounce = maxBounceDistance * scale;
    const double reserve = transformedCellGrowth + transformedBounce - padding;
    // Avoid promoting an exactly integral geometric result because of the
    // last-bit error in the floating-point multiplication above.
    return std::max(0, static_cast<int>(std::ceil(reserve - 1e-9)));
}

/// Total surface height including dock, animation/tooltip reserve, and floating padding.
/// Shadow is rendered within available space and naturally clips at surface boundaries.
inline int surfaceHeight(int iconSize, int padding, double maxZoomFactor, int tooltipReserve, int floatingPadding)
{
    const int dockHeight = iconSize + padding * 2;
    const int overflow = animationOverflowHeight(iconSize, padding, maxZoomFactor);

    const int topOverflow = std::max(overflow, tooltipReserve);
    const int bottomOverflow = floatingPadding;

    return dockHeight + topOverflow + bottomOverflow;
}

/// Height of the visible panel bar (icon + padding + floating gap).
inline int panelBarHeight(int iconSize, int padding, int floatingPadding)
{
    return iconSize + padding * 2 + floatingPadding;
}

} // namespace krema
