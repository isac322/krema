// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "inputregion.h"
#include <algorithm>

namespace krema
{

QRegion computeDockInputRegion(const InputRegionParams &p)
{
    if (p.panelWidth == 0 || p.panelHeight == 0) {
        return QRegion();
    }

    // 1. Create the Trigger Strip (The invisible 4px line that unhides the dock)
    QRegion triggerStrip;
    int ts = p.triggerStripHeight;

    // Rule 17 Compliance: The "Hysteresis Bridge"
    // If the dock is hovered, we expand the trigger strip to 64px.
    // This creates a solid block of Wayland input region to catch rapid mouse twitches,
    // ensuring the mouse doesn't fall out of the region while the dock is hidden or animating up.
    if (p.hovered) {
        ts = std::max(ts, 64);
    }

    switch (p.edge) {
    case 0: // Top
        triggerStrip = QRegion(0, 0, p.surfaceWidth, ts);
        break;
    case 1: // Bottom
        triggerStrip = QRegion(0, p.surfaceHeight - ts, p.surfaceWidth, ts);
        break;
    case 2: // Left
        triggerStrip = QRegion(0, 0, ts, p.surfaceHeight);
        break;
    case 3: // Right
        triggerStrip = QRegion(p.surfaceWidth - ts, 0, ts, p.surfaceHeight);
        break;
    }

    // If dock is hidden, not hovered, and settings are closed, only the trigger line is active
    if (!p.visible && !p.hovered && !p.settingsVisible) {
        return triggerStrip;
    }

    QRegion finalRegion = triggerStrip;

    // 2. Add the Dock Hitbox (The "Hole" for the icons)
    if (p.visible || p.hovered) {
        // FIX: The `p.margin` (64px) is exclusively for drawing the soft drop shadow.
        // It must NOT be added to the input region, otherwise it creates a massive invisible click-blocking wall!
        // Instead, we only use a small 5px buffer around the sides.
        int horizontalBuffer = 5;

        // FIX: Only expand the input region to `zoomOverflowHeight` if actively hovering.
        // If not hovering, we only provide 24px of overflow (enough to catch the tops of unzoomed protruding icons).
        // This dynamically frees the screen for background window clicks!
        int overflow = p.hovered ? p.zoomOverflowHeight : 24;

        int x, y, w, h;
        if (p.edge <= 1) { // Horizontal (Top=0, Bottom=1)
            x = std::max(0, p.panelX - horizontalBuffer);
            w = p.panelWidth + 2 * horizontalBuffer;

            if (p.edge == 1) { // Bottom
                // Rule 17 Compliance: Dynamically cover zoom area
                y = std::max(0, p.panelY - overflow - horizontalBuffer);
                h = p.surfaceHeight - y;
            } else { // Top
                y = 0;
                h = p.panelY + p.panelHeight + overflow + horizontalBuffer;
            }
        } else { // Vertical (Left=2, Right=3)
            y = std::max(0, p.panelY - horizontalBuffer);
            h = p.panelHeight + 2 * horizontalBuffer;

            if (p.edge == 3) { // Right
                x = std::max(0, p.panelX - overflow - horizontalBuffer);
                w = p.surfaceWidth - x;
            } else { // Left
                x = 0;
                w = p.panelX + p.panelWidth + overflow + horizontalBuffer;
            }
        }
        finalRegion += QRect(x, y, w, h);
    }

    // 3. Add the Settings Hitbox (Solid interaction for the menu)
    if (p.settingsVisible) {
        int sx = (p.surfaceWidth - p.settingsWidth) / 2;
        int sy = p.surfaceHeight - p.panelHeight - p.settingsMargin - p.settingsHeight;
        finalRegion += QRect(sx, sy, p.settingsWidth, p.settingsHeight);
    }

    return finalRegion;
}

QRect computeDockScreenRect(const DockScreenRectParams &p)
{
    int surfaceX = p.screenX;
    int surfaceY = p.screenY;
    switch (p.edge) {
    case 0:
        break; // Top anchor
    case 1:
        surfaceY += p.screenHeight - p.surfaceHeight;
        break; // Bottom anchor
    case 2:
        break; // Left anchor
    case 3:
        surfaceX += p.screenWidth - p.surfaceWidth;
        break; // Right anchor
    }
    return QRect(surfaceX + p.panelX, surfaceY + p.panelRefY, p.panelWidth, p.panelHeight);
}

} // namespace krema
