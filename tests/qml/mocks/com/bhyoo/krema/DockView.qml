// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Mock of DockView (production: per-engine context property).
pragma Singleton
import QtQuick

QtObject {
    // 0=Top, 1=Bottom, 2=Left, 3=Right
    property int edge: 1
    readonly property bool isVertical: edge === 2 || edge === 3
    property int backgroundStyleType: 0
    property color backgroundColor: "#99000000"
    property int floatingPadding: 8
    property int iconCacheVersion: 0

    // Restores every property listed in _resettable to its declared value.
    readonly property var _resettable: ["edge", "backgroundStyleType", "backgroundColor", "floatingPadding", "iconCacheVersion"]
    property var _defaults: ({})
    Component.onCompleted: {
        for (const k of _resettable) _defaults[k] = this[k]
    }
    function reset() {
        for (const k of _resettable) this[k] = _defaults[k]
    }
}
