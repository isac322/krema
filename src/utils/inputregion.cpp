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

QRegion computePreviewInputRegion(const PreviewInputRegionParams &p)
{
    // Mirror PreviewPopup.qml's popup placement: flush with the surface edge
    // facing the dock, positioned along the dock axis by contentX/contentY.
    QRectF popup(0, 0, p.contentWidth, p.contentHeight);
    switch (p.edge) {
    case 0: // Top: popup at the top of the surface
        popup.moveTopLeft(QPointF(p.contentX, 0));
        break;
    case 1: // Bottom: popup at the bottom of the surface
        popup.moveTopLeft(QPointF(p.contentX, p.surfaceHeight - p.contentHeight));
        break;
    case 2: // Left: popup at the left of the surface
        popup.moveTopLeft(QPointF(0, p.contentY));
        break;
    case 3: // Right: popup at the right of the surface
        popup.moveTopLeft(QPointF(p.surfaceWidth - p.contentWidth, p.contentY));
        break;
    }

    const QRect rect = popup.toAlignedRect().intersected(QRect(0, 0, p.surfaceWidth, p.surfaceHeight));
    if (rect.isEmpty()) {
        // No popup geometry yet: block input with a 1x1 corner region
        // (an empty QRegion clears the mask and accepts ALL input).
        return QRegion(0, 0, 1, 1);
    }
    return QRegion(rect);
}

} // namespace krema
