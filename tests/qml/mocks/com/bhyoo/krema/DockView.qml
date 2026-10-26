// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Mock of DockView (production: per-engine context property).
pragma Singleton
import QtQuick
import krema.test

QtObject {
    // 0=Top, 1=Bottom, 2=Left, 3=Right
    property int edge: 1
    readonly property bool isVertical: edge === 2 || edge === 3
    property int backgroundStyleType: 0
    property color backgroundColor: "#99000000"
    property int floatingPadding: 8
    property real displayedEdgeDistance: 0
    property int shadowOverflow: 0
    property int iconCacheVersion: 0
    // Compositor pointer focus (DockView::pointerInside).
    property bool pointerInside: false

    function beginFloatingPaddingTransition() {}
    function releaseFloatingPaddingEnvelope() {}

    // Production DockView::zoomLayout (krema::computeDockZoom), not a copy.
    function zoomLayout(count, restStart, iconSize, spacing, boundary, boundaryGap,
                        restBackgroundStart, restBackgroundEnd, maxZoomFactor, style,
                        active, cursor, minEdge, maxEdge) {
        return ZoomLayoutEngine.zoomLayout(count, restStart, iconSize, spacing, boundary,
                                           boundaryGap, restBackgroundStart, restBackgroundEnd,
                                           maxZoomFactor, style, active, cursor,
                                           minEdge, maxEdge)
    }

    function surfaceHeight(iconSize, padding, maxZoomFactor, tooltipReserve, floatingPadding) {
        return ZoomLayoutEngine.surfaceHeight(iconSize, padding, maxZoomFactor, tooltipReserve, floatingPadding)
    }

    function animationOverflowHeight() {
        return surfaceHeight(DockSettings.iconSize, 8, DockSettings.maxZoomFactor, 0, 0)
            - DockSettings.iconSize - 16
    }

    // Restores every property listed in _resettable to its declared value.
    readonly property var _resettable: ["edge", "backgroundStyleType", "backgroundColor", "floatingPadding", "displayedEdgeDistance", "shadowOverflow", "iconCacheVersion", "pointerInside"]
    property var _defaults: ({})
    Component.onCompleted: {
        for (const k of _resettable) _defaults[k] = this[k]
    }
    function reset() {
        for (const k of _resettable) this[k] = _defaults[k]
    }
}
