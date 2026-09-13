// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "inputregion.h"

#include <algorithm>

namespace krema
{

QRegion computeDockInputRegion(const InputRegionParams &p)
{
    // Trigger strip: thin strip along the screen edge to detect mouse entry
    QRegion triggerStrip;
    switch (p.edge) {
    case 0: // Top
        triggerStrip = QRegion(0, 0, p.surfaceWidth, p.triggerStripHeight);
        break;
    case 1: // Bottom
        triggerStrip = QRegion(0, p.surfaceHeight - p.triggerStripHeight, p.surfaceWidth, p.triggerStripHeight);
        break;
    case 2: // Left
        triggerStrip = QRegion(0, 0, p.triggerStripHeight, p.surfaceHeight);
        break;
    case 3: // Right
        triggerStrip = QRegion(p.surfaceWidth - p.triggerStripHeight, 0, p.triggerStripHeight, p.surfaceHeight);
        break;
    }

    if (!p.visible) {
        return triggerStrip;
    }

    if (p.edge <= 1) {
        // Horizontal (Top/Bottom)
        if (p.panelWidth <= 0) {
            return {};
        }

        // Sideways extension: margin + cursor activation radius (events from
        // the sides of the dock also trigger zoom)
        const int sideExt = p.margin + std::max(0, p.triggerRadius);
        const int regionX = std::max(0, p.panelX - sideExt);
        const int regionW = p.panelWidth + 2 * sideExt;

        int regionY;
        int regionH;
        // Depth extension: zoom overflow + cursor activation radius, applied
        // on BOTH depth sides of the panel (toward the screen edge AND away
        // from it) and ALWAYS — not only when already hovered. Otherwise the
        // cursor approaching from the far side (e.g. below a bottom dock)
        // never receives events and the radius can't trigger on entry.
        const int depthExt = p.zoomOverflowHeight + p.margin + std::max(0, p.triggerRadius);
        if (p.edge == 0) {
            // Top: panel at top
            regionY = std::max(0, p.panelY - depthExt);
            const int bottom = std::min(p.surfaceHeight, p.panelY + p.panelHeight + depthExt);
            regionH = bottom - regionY;
        } else {
            // Bottom: panel at bottom
            regionY = std::max(0, p.panelY - depthExt);
            const int bottom = std::min(p.surfaceHeight, p.panelY + p.panelHeight + depthExt);
            regionH = bottom - regionY;
        }

        QRegion region(regionX, regionY, regionW, regionH);
        return region.united(triggerStrip);
    } else {
        // Vertical (Left/Right)
        if (p.panelHeight <= 0) {
            return {};
        }

        // Sideways extension: margin + cursor activation radius (events from
        // above/below the dock also trigger zoom)
        const int sideExt = p.margin + std::max(0, p.triggerRadius);
        const int regionY = std::max(0, p.panelY - sideExt);
        const int regionH = p.panelHeight + 2 * sideExt;

        int regionX;
        int regionW;
        // Depth extension on BOTH sides, always (see horizontal case above).
        const int depthExt = p.zoomOverflowHeight + p.margin + std::max(0, p.triggerRadius);
        if (p.edge == 2) {
            // Left: panel at left
            regionX = std::max(0, p.panelX - depthExt);
            const int right = std::min(p.surfaceWidth, p.panelX + p.panelWidth + depthExt);
            regionW = right - regionX;
        } else {
            // Right: panel at right
            regionX = std::max(0, p.panelX - depthExt);
            const int right = std::min(p.surfaceWidth, p.panelX + p.panelWidth + depthExt);
            regionW = right - regionX;
        }

        QRegion region(regionX, regionY, regionW, regionH);
        return region.united(triggerStrip);
    }
}

QRect computeDockScreenRect(const DockScreenRectParams &p)
{
    int surfaceX = 0;
    int surfaceY = 0;

    switch (p.edge) {
    case 0: // Top
        surfaceX = p.screenX;
        surfaceY = p.screenY;
        break;
    case 1: // Bottom
        surfaceX = p.screenX;
        surfaceY = p.screenY + p.screenHeight - p.surfaceHeight;
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
