// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#pragma once

#include <QRect>
#include <QRegion>

namespace krema
{

struct InputRegionParams {
    int surfaceWidth;
    int surfaceHeight;
    int panelX;
    int panelY;
    int panelWidth;
    int panelHeight;
    int zoomOverflowHeight;
    bool visible;
    bool hovered;
    int triggerStripHeight = 4;
    int margin = 4;
    int edge = 1; // 0=Top, 1=Bottom, 2=Left, 3=Right
};

/// Compute the input region mask for the dock surface.
QRegion computeDockInputRegion(const InputRegionParams &p);

struct DockScreenRectParams {
    int screenX;
    int screenY;
    int screenWidth;
    int screenHeight;
    int surfaceWidth;
    int surfaceHeight;
    int panelX;
    int panelRefY;
    int panelWidth;
    int panelHeight;
    int edge; // 0=Top, 1=Bottom, 2=Left, 3=Right
};

/// Compute the dock panel rectangle in screen coordinates.
QRect computeDockScreenRect(const DockScreenRectParams &p);

struct PreviewInputRegionParams {
    int surfaceWidth;
    int surfaceHeight;
    qreal contentX; // popup x along a horizontal dock (surface-local)
    qreal contentY; // popup y along a vertical dock (surface-local)
    qreal contentWidth;
    qreal contentHeight;
    int edge; // 0=Top, 1=Bottom, 2=Left, 3=Right
};

/// Compute the input region mask for the visible preview popup.
/// The region is exactly the popup rectangle as placed by PreviewPopup.qml
/// (flush with the dock-side surface edge, centred along the dock axis), so
/// the transparent rest of the preview surface never takes pointer focus.
/// Never returns an empty region (an empty mask would accept ALL input).
QRegion computePreviewInputRegion(const PreviewInputRegionParams &p);

} // namespace krema
