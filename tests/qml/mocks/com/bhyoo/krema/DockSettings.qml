// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Mock of KremaSettings (KConfigXT). Defaults mirror src/config/krema.kcfg.
pragma Singleton
import QtQuick

QtObject {
    property int iconSize: 48
    property int iconSpacing: 4
    property real maxZoomFactor: 1.6
    // 0 = Parabolic (neighbours move aside), 1 = InPlace
    property int zoomStyle: 0
    // 0 = Natural, 1 = Quick, 2 = Relaxed, 3 = Instant, 4 = Custom
    property int zoomAnimationPreset: 0
    // Custom preset timing (unscaled ms) and easing indices (see ZoomAnimationProfile).
    property int zoomInDuration: 180
    property int zoomOutDuration: 240
    property int zoomInEasing: 3
    property int zoomOutEasing: 3
    property real iconOpacityActive: 1.0
    property real iconOpacityInactive: 0.8
    property real iconOpacityMinimized: 0.5
    property real iconScale: 1.0
    property int cornerRadius: 12
    property bool reserveScreenSpace: true
    property bool separateLaunchers: false
    property int attentionAnimation: 2
    property int attentionAnimationDuration: 5
    property int badgeDisplayMode: 0
    property real otherDesktopOpacity: 0.4
    property bool previewEnabled: true
    property int previewHoverDelay: 500
    property int singleWindowClickAction: 0
    property int groupedWindowClickAction: 0
    property int previewThumbnailSize: 200
    property bool shadowEnabled: false
    property int shadowElevation: 15
    property int shadowLightX: 0
    property int shadowLightY: -150
    property int shadowLightZ: 500
    property real shadowLightRadius: 5.0
    property color shadowColor: "#80000000"
    property real shadowIntensity: 0.3

    // Restores every property listed in _resettable to its declared value.
    readonly property var _resettable: ["iconSize", "iconSpacing", "maxZoomFactor", "zoomStyle", "zoomAnimationPreset", "zoomInDuration", "zoomOutDuration", "zoomInEasing", "zoomOutEasing", "iconOpacityActive", "iconOpacityInactive", "iconOpacityMinimized", "iconScale", "cornerRadius", "reserveScreenSpace", "separateLaunchers", "attentionAnimation", "attentionAnimationDuration", "badgeDisplayMode", "otherDesktopOpacity", "previewEnabled", "previewHoverDelay", "singleWindowClickAction", "groupedWindowClickAction", "previewThumbnailSize", "shadowEnabled", "shadowElevation", "shadowLightX", "shadowLightY", "shadowLightZ", "shadowLightRadius", "shadowColor", "shadowIntensity"]
    property var _defaults: ({})
    Component.onCompleted: {
        for (const k of _resettable) _defaults[k] = this[k]
    }
    function reset() {
        for (const k of _resettable) this[k] = _defaults[k]
    }
}
