// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "inputregion.h"

#include <algorithm>

namespace krema
{

QRegion computeDockInputRegion(const InputRegionParams &p)
{
    // Full-width trigger strip at the bottom edge of the surface.
    const QRegion triggerStrip(0, p.surfaceHeight - p.triggerStripHeight, p.surfaceWidth, p.triggerStripHeight);

    if (!p.visible) {
        return triggerStrip;
    }

    if (p.panelWidth <= 0) {
        // Panel geometry not yet reported; accept all input as fallback
        return {};
    }

    const int regionX = std::max(0, p.panelX - p.margin);
    const int regionW = p.panelWidth + 2 * p.margin;

    int regionY;
    if (p.hovered) {
        // Include zoom overflow + panel + floating gap
        regionY = std::max(0, p.panelY - p.zoomOverflowHeight - p.margin);
    } else {
        // Include panel + floating gap below
        regionY = std::max(0, p.panelY - p.margin);
    }
    const int regionH = p.surfaceHeight - regionY;

    QRegion region(regionX, regionY, regionW, regionH);
    return region.united(triggerStrip);
}

QRect computeDockScreenRect(const DockScreenRectParams &p)
{
    int surfaceX = 0;
    int surfaceY = 0;

    switch (p.edge) {
    case 0: // Bottom
        surfaceX = p.screenX;
        surfaceY = p.screenY + p.screenHeight - p.surfaceHeight;
        break;
    case 1: // Top
        surfaceX = p.screenX;
        surfaceY = p.screenY;
        break;
    case 2: // Left
        surfaceX = p.screenX;
        surfaceY = p.screenY;
        break;
    case 3: // Right
        surfaceX = p.screenX + p.screenWidth - p.surfaceWidth;
        surfaceY = p.screenY;
        break;
    }

    return QRect(surfaceX + p.panelX, surfaceY + p.panelRefY, p.panelWidth, p.panelHeight);
}

} // namespace krema
