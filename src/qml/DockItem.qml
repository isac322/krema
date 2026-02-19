// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick

/**
 * A single dock item (app icon + indicator).
 *
 * Handles parabolic zoom calculation and displays the application icon
 * with status indicators beneath.
 */
Item {
    id: dockItem

    // Properties set by the Repeater delegate
    required property int index
    required property var model

    // Display name for tooltip (readable by parent)
    readonly property string displayName: model.display || ""

    // Configuration from DockView
    property int iconSize: 48
    property real maxZoomFactor: 1.6
    property real panelMouseX: -1
    property bool panelMouseInside: false
    property int spacing: 4
    property real itemCenterX: 0

    // Parabolic zoom: number of neighboring icons affected
    readonly property int zoomNeighbors: 3
    // Gaussian sigma for the zoom curve
    readonly property real zoomSigma: zoomNeighbors * (iconSize + spacing) / 2.5

    // Computed zoom factor for this item
    readonly property real zoomFactor: {
        if (!panelMouseInside || panelMouseX < 0) {
            return 1.0
        }

        let distance = Math.abs(panelMouseX - itemCenterX)
        let sigma2 = zoomSigma * zoomSigma
        return 1.0 + (maxZoomFactor - 1.0) * Math.exp(-(distance * distance) / sigma2)
    }

    // Animated scale
    property real currentScale: 1.0
    Behavior on currentScale {
        NumberAnimation {
            duration: 150
            easing.type: Easing.OutCubic
        }
    }

    // Update currentScale when zoomFactor changes
    onZoomFactorChanged: currentScale = zoomFactor

    // Size: base icon size, scaled by zoom
    width: iconSize
    height: iconSize + indicatorRow.height + 4  // icon + gap + indicators

    // Scaled transform (icon grows upward from bottom)
    transform: Scale {
        origin.x: width / 2
        origin.y: height
        xScale: currentScale
        yScale: currentScale
    }

    // Application icon
    Image {
        id: iconImage
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: iconSize
        height: iconSize
        source: {
            let name = dockModel.iconName(dockItem.index)
            if (name && name.length > 0) {
                return "image://icon/" + name
            }
            return ""
        }
        sourceSize: Qt.size(Math.ceil(iconSize * maxZoomFactor), Math.ceil(iconSize * maxZoomFactor))
        smooth: true

        // Highlight for active window
        opacity: {
            if (dockItem.model.IsActive) return 1.0
            if (dockItem.model.IsMinimized) return 0.5
            return 0.8
        }

        Behavior on opacity {
            NumberAnimation { duration: 100 }
        }

        // Fallback placeholder when icon is not available
        Rectangle {
            id: iconPlaceholder
            anchors.fill: parent
            radius: 8
            color: Qt.hsla((dockItem.index * 0.15) % 1.0, 0.6, 0.4, 1.0)
            visible: iconImage.status !== Image.Ready

            Text {
                anchors.centerIn: parent
                text: {
                    let name = dockItem.model.display || ""
                    return name.length > 0 ? name[0].toUpperCase() : "?"
                }
                font.pixelSize: iconSize * 0.4
                font.bold: true
                color: "white"
            }
        }
    }

    // Status indicator dots
    Row {
        id: indicatorRow
        anchors.top: iconImage.bottom
        anchors.topMargin: 4
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 3

        Repeater {
            // Show dots based on window count (max 3)
            model: {
                if (!dockItem.model.IsWindow) return 0
                let count = dockItem.model.ChildCount || 1
                return Math.min(count, 3)
            }

            Rectangle {
                width: dockItem.model.IsActive ? 4 : 3
                height: width
                radius: width / 2
                color: "white"
                opacity: dockItem.model.IsMinimized ? 0.4 : 0.8

                Behavior on opacity {
                    NumberAnimation { duration: 100 }
                }
            }
        }
    }

    // Click handling
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        hoverEnabled: false

        onClicked: function(mouse) {
            if (mouse.button === Qt.LeftButton) {
                dockModel.activate(dockItem.index)
            } else if (mouse.button === Qt.MiddleButton) {
                dockModel.newInstance(dockItem.index)
            } else if (mouse.button === Qt.RightButton) {
                // TODO: context menu
            }
        }
    }
}
