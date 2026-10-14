// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "inputregion.h"

#include <algorithm>

namespace krema
{

QRegion computeDockInputRegion(const InputRegionParams &p)
{
    // Depth of the band other panels occupy along the screen edge (the dock is
    // drawn past it). Nothing here may take their input, so the dock's region
    // and its trigger strip both start past the band.
    const int edgeInset = std::max(0, p.edgeInset);

    // Trigger strip: thin strip along the screen edge (or the panels' inner
    // edge) to detect mouse entry. It also keeps the region non-empty while
    // the dock slides back in from off-surface (an empty mask accepts ALL input).
    QRegion triggerStrip;
    // 1x1 placeholder at the inner corner (away from the screen edge and its
    // panel band) when KWin watches the screen edge: an empty QRegion clears
    // the Wayland input mask and accepts input on the whole surface.
    QRegion placeholder(0, 0, 1, 1);
    switch (p.edge) {
    case 0: // Top
        triggerStrip = QRegion(0, edgeInset, p.surfaceWidth, p.triggerStripHeight);
        placeholder = QRegion(0, std::max(0, p.surfaceHeight - 1), 1, 1);
        break;
    case 1: // Bottom
        triggerStrip = QRegion(0, p.surfaceHeight - edgeInset - p.triggerStripHeight, p.surfaceWidth, p.triggerStripHeight);
        break;
    case 2: // Left
        triggerStrip = QRegion(edgeInset, 0, p.triggerStripHeight, p.surfaceHeight);
        placeholder = QRegion(std::max(0, p.surfaceWidth - 1), 0, 1, 1);
        break;
    case 3: // Right
        triggerStrip = QRegion(p.surfaceWidth - edgeInset - p.triggerStripHeight, 0, p.triggerStripHeight, p.surfaceHeight);
        break;
    }

    if (!p.visible) {
        return p.externalEdgeTrigger ? placeholder : triggerStrip;
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
            regionY = std::min(edgeInset, p.surfaceHeight);
            int bottom = p.hovered ? (p.panelY + p.panelHeight + p.zoomOverflowHeight + p.margin) : (p.panelY + p.panelHeight + p.margin);
            regionH = std::max(0, std::min(bottom, p.surfaceHeight) - regionY);
        } else {
            // Bottom: panel at bottom, zoom extends upward
            regionY = p.hovered ? std::max(0, p.panelY - p.zoomOverflowHeight - p.margin) : std::max(0, p.panelY - p.margin);
            regionH = std::max(0, p.surfaceHeight - edgeInset - regionY);
        }

        QRegion region(regionX, regionY, regionW, regionH);
        if (p.externalEdgeTrigger) {
            return region.isEmpty() ? placeholder : region;
        }
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
            regionX = std::min(edgeInset, p.surfaceWidth);
            int right = p.hovered ? (p.panelX + p.panelWidth + p.zoomOverflowHeight + p.margin) : (p.panelX + p.panelWidth + p.margin);
            regionW = std::max(0, std::min(right, p.surfaceWidth) - regionX);
        } else {
            // Right: panel at right, zoom extends leftward
            regionX = p.hovered ? std::max(0, p.panelX - p.zoomOverflowHeight - p.margin) : std::max(0, p.panelX - p.margin);
            regionW = std::max(0, p.surfaceWidth - edgeInset - regionX);
        }

        QRegion region(regionX, regionY, regionW, regionH);
        if (p.externalEdgeTrigger) {
            return region.isEmpty() ? placeholder : region;
        }
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
