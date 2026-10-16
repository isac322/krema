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

/// Ground-space shadow overflow along the dock's secondary axis.
inline int shadowOverflowHeight(int panelExtent, int lightOffset, int lightZ, int elevation, double lightRadius)
{
    const double denominator = std::max(lightZ - elevation, 1);
    const double projection = static_cast<double>(elevation) / denominator;
    const double panelOverhang = (std::max(panelExtent, 0) / 2.0 + std::abs(lightOffset)) * projection;
    const double softness = 3.0 * std::max(lightRadius, 0.5) * lightZ / denominator;
    return static_cast<int>(std::ceil(panelOverhang + softness + 10.0));
}

/// Total surface height including dock, zoom / tooltip / shadow reserve, and floating padding.
inline int surfaceHeight(int iconSize,
                         int padding,
                         double maxZoomFactor,
                         int tooltipReserve,
                         int floatingPadding,
                         int shadowOverflow = 0)
{
    const int dockHeight = iconSize + padding * 2;
    const int zoomOverflow = zoomOverflowHeight(iconSize, maxZoomFactor);

    const int topOverflow = std::max({zoomOverflow, tooltipReserve, shadowOverflow});
    const int bottomOverflow = floatingPadding;

    return dockHeight + topOverflow + bottomOverflow;
}

/// Height of the visible panel bar (icon + padding + floating gap).
inline int panelBarHeight(int iconSize, int padding, int floatingPadding)
{
    return iconSize + padding * 2 + floatingPadding;
}

} // namespace krema
