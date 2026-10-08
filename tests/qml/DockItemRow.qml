// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Test harness: a horizontal row of real DockItem delegates over the mock
// DockModel.tasksModel, wired to settings the same way main.qml's Repeater
// delegate is. Pointer state (mouseX/mouseInside) is driven directly by
// tests, isolating DockItem's zoom from main.qml's hit testing.
//
// The zoom inputs (zoomScale, zoomOffset) come from the same pipeline as
// main.qml's dockPanel: zoomCursor holds the last pointer position, both zoom
// styles ease only the global zoomAmount in and out (timing and easing from
// ZoomAnimationProfile), and DockView.zoomLayout() (production
// krema::computeDockZoom) lays the row out in its rest frame. The background
// bounds are left open (no surface here).
import QtQuick
import com.bhyoo.krema 1.0
import "../../src/qml" as Krema

Item {
    id: host
    width: 800
    height: 200

    property real mouseX: -1
    property bool mouseInside: false
    readonly property alias count: repeater.count

    function itemAt(i) {
        return repeater.itemAt(i)
    }

    readonly property bool _inside: mouseInside && mouseX >= 0
    readonly property int zoomStyle: DockSettings.zoomStyle
    property real zoomCursor: 0
    onMouseXChanged: if (mouseX >= 0) zoomCursor = mouseX

    // Resolved zoom timing/easing, exposed so tests can read effective durations.
    readonly property alias profile: zoomProfile
    Krema.ZoomAnimationProfile {
        id: zoomProfile
    }

    property real zoomAmount: _inside ? 1.0 : 0.0
    Behavior on zoomAmount {
        id: zoomAmountBehavior
        enabled: zoomProfile.animated
        NumberAnimation {
            duration: zoomAmountBehavior.targetValue > 0.5
                ? zoomProfile.effectiveZoomInDuration
                : zoomProfile.effectiveZoomOutDuration
            easing.type: zoomAmountBehavior.targetValue > 0.5
                ? zoomProfile.zoomInEasingType
                : zoomProfile.zoomOutEasingType
        }
    }
    readonly property var zoomLayout: DockView.zoomLayout(
        repeater.count, 0,
        DockSettings.iconSize, DockSettings.iconSpacing,
        -1, 0,
        0, row.width,
        1.0 + (DockSettings.maxZoomFactor - 1.0) * zoomAmount,
        zoomStyle,
        zoomAmount > 0,
        zoomCursor, -Infinity, Infinity)

    Row {
        id: row
        spacing: DockSettings.iconSpacing

        Repeater {
            id: repeater
            model: DockModel.tasksModel

            Krema.DockItem {
                iconSize: DockSettings.iconSize
                maxZoomFactor: DockSettings.maxZoomFactor
                spacing: DockSettings.iconSpacing
                zoomScale: host.zoomLayout.scales?.[index] ?? 1.0
                zoomOffset: host.zoomLayout.offsets?.[index] ?? 0.0
                itemCenterX: x + width / 2
            }
        }
    }
}
