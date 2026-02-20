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

    // Hovered item tracking (for custom tooltip and click targeting)
    property int hoveredIndex: -1
    property string hoveredName: ""

    // Hysteresis flag: once zoom activates (mouse on an icon), it stays active
    // until the mouse leaves the panel zone entirely. This prevents rapid zoom
    // on/off flickering when moving between icons through tiny gaps.
    property bool _zoomActive: false

    function updateHoveredItem() {
        if (dockPanel.mouseX < 0) {
            hoveredIndex = -1
            hoveredName = ""
            _zoomActive = false
            return
        }

        // Rough vertical check: outside the dockRow + zoom extension → reset zoom
        let rowTop = dockRow.y
        let rowBottom = dockRow.y + dockRow.height
        let maxExt = dockView.iconSize * (dockView.maxZoomFactor - 1.0)
        if (dockPanel.mouseY < rowTop - maxExt || dockPanel.mouseY > rowBottom) {
            hoveredIndex = -1
            hoveredName = ""
            _zoomActive = false
            return
        }

        // Find the closest icon whose SCALED 2D bounds contain the mouse.
        // Use Repeater.itemAt() for correct model-index-to-delegate mapping.
        let bestIndex = -1
        let bestDist = Infinity
        for (let i = 0; i < dockRepeater.count; i++) {
            let item = dockRepeater.itemAt(i)
            if (!item) continue

            // Horizontal check: scaled width
            let dist = Math.abs(dockPanel.mouseX - item.itemCenterX)
            let scaledHalfWidth = (item.width * item.currentScale) / 2
            if (dist >= scaledHalfWidth) continue

            // Vertical check: Scale origin.y = height → bottom fixed, grows upward
            let itemBottom = dockRow.y + item.y + item.height
            let itemTop = itemBottom - item.height * item.currentScale
            if (dockPanel.mouseY < itemTop || dockPanel.mouseY > itemBottom) continue

            if (dist < bestDist) { bestIndex = i; bestDist = dist }
        }

        if (bestIndex >= 0) {
            _zoomActive = true  // Activate zoom; stays until mouse leaves panel
            if (hoveredIndex !== bestIndex) {
                hoveredIndex = bestIndex
                hoveredName = dockRepeater.itemAt(bestIndex).displayName
                tooltipTimer.restart()
            }
        } else {
            hoveredIndex = -1
            hoveredName = ""
            // _zoomActive intentionally NOT reset here for horizontal gap hysteresis.
            // When mouse crosses tiny gaps between icons, zoom stays active to prevent
            // flickering. Zoom deactivates only when mouse leaves the vertical zone
            // (rough check above) or the panel zone entirely (mouseX becomes -1).
        }
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
            dockPanel.mouseY = -1
            root.hoveredIndex = -1
            root.hoveredName = ""
            root._zoomActive = false
            dockVisibility.setHovered(false)
        }

        // Click handling: uses hoveredIndex from scaled hit testing
        // so clicks work correctly on zoomed icons
        onClicked: function(mouse) {
            if (root.hoveredIndex < 0) return
            if (mouse.button === Qt.LeftButton) {
                dockModel.activate(root.hoveredIndex)
            } else if (mouse.button === Qt.MiddleButton) {
                dockModel.newInstance(root.hoveredIndex)
            } else if (mouse.button === Qt.RightButton) {
                dockModel.showContextMenu(root.hoveredIndex)
            }
        }

        // Mouse wheel: cycle through child windows of the hovered app
        onWheel: function(wheel) {
            if (root.hoveredIndex < 0) return
            if (wheel.angleDelta.y > 0) {
                dockModel.cycleWindows(root.hoveredIndex, false)
            } else if (wheel.angleDelta.y < 0) {
                dockModel.cycleWindows(root.hoveredIndex, true)
            }
        }

        // Track mouse position for parabolic zoom
        onPositionChanged: function(mouse) {
            // Only activate zoom when mouse is near the panel vertically.
            // Tight zone: panel area + zoom extension above (icons grow upward).
            let panelTop = dockPanel.y
            let panelBottom = dockPanel.y + dockPanel.height
            let zoomExtension = dockView.iconSize * (dockView.maxZoomFactor - 1.0)
            if (mouse.y >= panelTop - zoomExtension && mouse.y <= panelBottom) {
                dockPanel.mouseX = mouse.x - dockPanel.x
                dockPanel.mouseY = mouse.y - dockPanel.y
            } else {
                dockPanel.mouseX = -1
                dockPanel.mouseY = -1
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

        // Mouse position relative to the panel, -1 when outside
        property real mouseX: -1
        property real mouseY: -1
        // Zoom activates when mouse hits an icon (_zoomActive=true) and stays
        // active until mouse leaves the panel zone (mouseX=-1). This hysteresis
        // prevents zoom flickering when crossing tiny gaps between icons.
        property bool mouseInside: mouseX >= 0 && root._zoomActive

        // Report panel geometry to visibility controller for input region
        onXChanged: dockVisibility.setPanelRect(x, width)
        onWidthChanged: dockVisibility.setPanelRect(x, width)

        // Main icon row
        Row {
            id: dockRow
            anchors.centerIn: parent
            spacing: dockView.iconSpacing

            Repeater {
                id: dockRepeater
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

    // Handle launch bounce trigger from C++ signal
    Connections {
        target: dockModel
        function onTaskLaunching(index) {
            let item = dockRepeater.itemAt(index)
            if (item) {
                item.manualLaunching = true
                item.launchFallbackTimer.restart()
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
            if (root.hoveredIndex < 0 || root.hoveredIndex >= dockRepeater.count)
                return 0
            let item = dockRepeater.itemAt(root.hoveredIndex)
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
