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

        const int regionX = std::max(0, p.panelX - p.margin);
        const int regionW = p.panelWidth + 2 * p.margin;

        int regionY;
        int regionH;
        if (p.edge == 0) {
            // Top: panel at top, zoom extends downward
            regionY = 0;
            int bottom = p.hovered ? (p.panelY + p.panelHeight + p.zoomOverflowHeight + p.margin) : (p.panelY + p.panelHeight + p.margin);
            regionH = std::min(bottom, p.surfaceHeight);
        } else {
            // Bottom: panel at bottom, zoom extends upward
            regionY = p.hovered ? std::max(0, p.panelY - p.zoomOverflowHeight - p.margin) : std::max(0, p.panelY - p.margin);
            regionH = p.surfaceHeight - regionY;
        }

        QRegion region(regionX, regionY, regionW, regionH);
        return region.united(triggerStrip);
    } else {
        // Vertical (Left/Right)
        if (p.panelHeight <= 0) {
            return {};
        }

        const int regionY = std::max(0, p.panelY - p.margin);
        const int regionH = p.panelHeight + 2 * p.margin;

        int regionX;
        int regionW;
        if (p.edge == 2) {
            // Left: panel at left, zoom extends rightward
            regionX = 0;
            int right = p.hovered ? (p.panelX + p.panelWidth + p.zoomOverflowHeight + p.margin) : (p.panelX + p.panelWidth + p.margin);
            regionW = std::min(right, p.surfaceWidth);
        } else {
            // Right: panel at right, zoom extends leftward
            regionX = p.hovered ? std::max(0, p.panelX - p.zoomOverflowHeight - p.margin) : std::max(0, p.panelX - p.margin);
            regionW = p.surfaceWidth - regionX;
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
