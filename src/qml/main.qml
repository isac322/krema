// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick

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

    // Hovered item tracking (for custom tooltip)
    property int hoveredIndex: -1
    property string hoveredName: ""

    function updateHoveredItem() {
        if (dockPanel.mouseX < 0) {
            hoveredIndex = -1
            hoveredName = ""
            return
        }
        for (let i = 0; i < dockRow.children.length; i++) {
            let item = dockRow.children[i]
            if (item.itemCenterX === undefined) continue
            if (Math.abs(dockPanel.mouseX - item.itemCenterX) < item.width / 2) {
                if (hoveredIndex !== i) {
                    hoveredIndex = i
                    hoveredName = item.displayName
                    tooltipTimer.restart()
                }
                return
            }
        }
        hoveredIndex = -1
        hoveredName = ""
    }

    MouseArea {
        id: dockMouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton

        // Track hover state for visibility controller
        onEntered: dockVisibility.setHovered(true)
        onExited: {
            dockPanel.mouseX = -1
            root.hoveredIndex = -1
            root.hoveredName = ""
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
            // Only activate zoom when mouse is near the panel vertically
            let panelTop = dockPanel.y
            let panelBottom = dockPanel.y + dockPanel.height
            if (mouse.y >= panelTop - dockView.iconSize && mouse.y <= panelBottom) {
                dockPanel.mouseX = mouse.x - dockPanel.x
            } else {
                dockPanel.mouseX = -1
            }
            root.updateHoveredItem()
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

        // Delay enabling animations until after initial layout to avoid startup flicker
        property bool animationsReady: false
        Component.onCompleted: Qt.callLater(function() { animationsReady = true })

        Behavior on y {
            enabled: dockPanel.animationsReady
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

        // Report panel geometry to visibility controller for input region
        onXChanged: dockVisibility.setPanelRect(x, width)
        onWidthChanged: dockVisibility.setPanelRect(x, width)

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

    // Custom tooltip (rendered within the scene, not as a Popup/Window)
    // Avoids Wayland popup surface that intercepts hover events.
    Timer {
        id: tooltipTimer
        interval: 500
        onTriggered: tooltipItem.show = true
    }

    Rectangle {
        id: tooltipItem
        property bool show: false
        visible: show && root.hoveredName.length > 0

        // Reset when hover changes
        onVisibleChanged: if (!visible) show = false

        // Position above the hovered icon
        x: {
            if (root.hoveredIndex < 0 || root.hoveredIndex >= dockRow.children.length)
                return 0
            let item = dockRow.children[root.hoveredIndex]
            if (!item) return 0
            return dockPanel.x + dockRow.x + item.x + item.width / 2 - width / 2
        }
        y: dockPanel.y - height - 8

        width: tooltipLabel.implicitWidth + 16
        height: tooltipLabel.implicitHeight + 8
        radius: 4
        color: "#cc000000"
        z: 100

        Text {
            id: tooltipLabel
            anchors.centerIn: parent
            text: root.hoveredName
            color: "white"
            font.pixelSize: 12
        }

        // Hide tooltip when mouse leaves dock area
        Connections {
            target: root
            function onHoveredIndexChanged() {
                if (root.hoveredIndex < 0) {
                    tooltipItem.show = false
                    tooltipTimer.stop()
                }
            }
        }
    }
}
