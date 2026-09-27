// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Test harness: a horizontal row of real DockItem delegates over the mock
// DockModel.tasksModel, wired to settings the same way main.qml's Repeater
// delegate is. Pointer state (mouseX/mouseInside) is driven directly by
// tests, isolating DockItem's zoom math from main.qml's hit testing.
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

    Row {
        id: row
        spacing: DockSettings.iconSpacing

        Repeater {
            id: repeater
            model: DockModel.tasksModel

            Krema.DockItem {
                iconSize: DockSettings.iconSize
                maxZoomFactor: DockSettings.maxZoomFactor
                panelMouseX: host.mouseX
                panelMouseInside: host.mouseInside
                spacing: DockSettings.iconSpacing
                itemCenterX: x + width / 2
            }
        }
    }
}
