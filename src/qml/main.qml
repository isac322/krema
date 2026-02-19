// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Layouts

/**
 * Main dock container.
 *
 * The root item fills the layer-shell surface. The visible dock panel
 * is a centered rounded rectangle that hugs its content.
 * The panel slides in/out based on dockVisibility.dockVisible.
 */
Item {
    id: root
    anchors.fill: parent

    // C++ context properties: dockView, dockModel, dockVisibility

    MouseArea {
        id: dockMouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton

        // Track hover state for visibility controller
        onEntered: dockVisibility.setHovered(true)
        onExited: {
            dockPanel.mouseX = -1
            dockVisibility.setHovered(false)
        }

        // Mouse wheel: cycle focus between running apps
        onWheel: function(wheel) {
            if (wheel.angleDelta.y > 0) {
                dockModel.activatePreviousTask()
            } else if (wheel.angleDelta.y < 0) {
                dockModel.activateNextTask()
            }
        }

        // Track mouse position for parabolic zoom
        onPositionChanged: function(mouse) {
            dockPanel.mouseX = mouse.x - dockPanel.x
        }
    }

    // The visible dock panel (centered, fits content)
    Rectangle {
        id: dockPanel
        anchors.horizontalCenter: parent.horizontalCenter
        height: dockView.iconSize + 16  // icon + padding
        width: Math.max(dockRow.implicitWidth + 16, 100)  // content + padding, min 100px
        radius: dockView.cornerRadius
        color: dockView.backgroundColor

        // Slide animation: y position controlled by visibility
        y: dockVisibility.dockVisible
           ? parent.height - height
           : parent.height + 8

        Behavior on y {
            NumberAnimation {
                duration: 200
                easing.type: Easing.InOutQuad
            }
        }

        // Fade animation
        opacity: dockVisibility.dockVisible ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: 200 }
        }

        // Mouse X position relative to the panel, -1 when outside
        property real mouseX: -1
        property bool mouseInside: mouseX >= 0

        // Main icon row
        Row {
            id: dockRow
            anchors.centerIn: parent
            spacing: dockView.iconSpacing

            Repeater {
                model: dockModel.tasksModel

                DockItem {
                    // index and model are injected by Repeater into
                    // DockItem's own required properties

                    iconSize: dockView.iconSize
                    maxZoomFactor: dockView.maxZoomFactor
                    panelMouseX: dockPanel.mouseX
                    panelMouseInside: dockPanel.mouseInside
                    spacing: dockView.iconSpacing

                    // Compute this item's center X relative to the panel
                    itemCenterX: x + width / 2 + dockRow.x
                }
            }
        }
    }
}
