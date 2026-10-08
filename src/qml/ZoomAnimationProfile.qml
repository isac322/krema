// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0

/**
 * Resolves the hover-zoom animation timing from the ZoomAnimationPreset,
 * ZoomIn/OutDuration and ZoomIn/OutEasing settings (krema.kcfg).
 *
 * Presets: 0 = Natural (default), 1 = Quick, 2 = Relaxed, 3 = Instant,
 * 4 = Custom (the ZoomIn/Out* settings). Out-of-range presets fall back to
 * Natural.
 *
 * Easing indices: 0 = Linear, 1 = Ease in, 2 = Ease out,
 * 3 = Ease in and out, 4 = Gentle ease in and out. No overshoot curves:
 * icons must never exceed maxZoomFactor, the surface is sized for it.
 */
QtObject {
    id: profile

    readonly property int customPreset: 4

    // Indexed by preset 0..3; durations are unscaled milliseconds.
    readonly property var presets: [
        { inDuration: 180, inEasing: 3, outDuration: 240, outEasing: 3 }, // Natural
        { inDuration: 100, inEasing: 2, outDuration: 100, outEasing: 2 }, // Quick
        { inDuration: 300, inEasing: 4, outDuration: 400, outEasing: 4 }, // Relaxed
        { inDuration: 0, inEasing: 3, outDuration: 0, outEasing: 3 }      // Instant
    ]

    function easingType(index) {
        switch (index) {
        case 0: return Easing.Linear
        case 1: return Easing.InCubic
        case 2: return Easing.OutCubic
        case 3: return Easing.InOutCubic
        case 4: return Easing.InOutSine
        default: return Easing.InOutCubic
        }
    }

    property int preset: DockSettings.zoomAnimationPreset

    readonly property bool _custom: preset === customPreset
    readonly property var _presetTiming: preset >= 0 && preset < presets.length
        ? presets[preset]
        : presets[0]

    readonly property int zoomInDuration: _custom ? DockSettings.zoomInDuration : _presetTiming.inDuration
    readonly property int zoomOutDuration: _custom ? DockSettings.zoomOutDuration : _presetTiming.outDuration
    readonly property int zoomInEasing: _custom ? DockSettings.zoomInEasing : _presetTiming.inEasing
    readonly property int zoomOutEasing: _custom ? DockSettings.zoomOutEasing : _presetTiming.outEasing

    readonly property int zoomInEasingType: easingType(zoomInEasing)
    readonly property int zoomOutEasingType: easingType(zoomOutEasing)

    // Durations are an unscaled baseline: 100 ms matches shortDuration at
    // normal speed. Plasma scaling also makes Instant/reduced motion snap.
    readonly property int effectiveZoomInDuration: Math.round(
        zoomInDuration * Kirigami.Units.shortDuration / 100.0)
    readonly property int effectiveZoomOutDuration: Math.round(
        zoomOutDuration * Kirigami.Units.shortDuration / 100.0)

    readonly property bool animated: effectiveZoomInDuration > 0 || effectiveZoomOutDuration > 0
}
