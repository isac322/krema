// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.pipewire as PipeWire
import org.kde.taskmanager as TaskManager

/**
 * A single window thumbnail in the preview popup.
 *
 * Uses PipeWire + ScreencastingRequest for GPU-accelerated live thumbnails.
 * Shows close button on hover, click to activate window.
 */
Item {
    id: root

    property var winId
    property string title: ""
    property bool isMinimized: false
    property bool isActive: false
    property int parentIndex: -1
    property int childIndex: 0

    readonly property real thumbnailWidth: dockSettings ? dockSettings.previewThumbnailSize : 200
    readonly property real thumbnailHeight: thumbnailWidth * 0.7

    implicitWidth: thumbnailWidth + 2 * Kirigami.Units.smallSpacing
    implicitHeight: thumbnailHeight + titleLabel.implicitHeight + Kirigami.Units.smallSpacing * 3

    // PipeWire screencasting: uuid → nodeId
    // Match Plasma's pattern: pass winId directly, let QML engine handle QVariant→QString
    TaskManager.ScreencastingRequest {
        id: screencastRequest
        uuid: root.winId ?? ""
        onNodeIdChanged: function() {
            console.log("[krema.preview] nodeId:", screencastRequest.nodeId,
                "for uuid:", screencastRequest.uuid)
        }
    }

    // Retry screencast when nodeId is 0 (KWin hadn't registered the window yet).
    // Called from PreviewPopup.rebuildChildModel() on each KDE model event.
    function retryScreencast() {
        if (screencastRequest.nodeId === 0 && root.winId) {
            screencastRequest.uuid = ""
            screencastRequest.uuid = root.winId ?? ""
        }
    }

    // Thumbnail container
    Rectangle {
        id: thumbnailContainer
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.thumbnailWidth
        height: root.thumbnailHeight
        radius: Kirigami.Units.smallSpacing
        color: Kirigami.Theme.alternateBackgroundColor
        clip: true

        // Passive hover detection — not affected by ToolButton z-order.
        // MouseArea.containsMouse goes false when ToolButton appears on top,
        // but HoverHandler stays hovered because it uses passive grab.
        HoverHandler {
            id: thumbnailHover
        }

        border.color: root.isActive ? Kirigami.Theme.highlightColor : "transparent"
        border.width: root.isActive ? 2 : 0

        // GPU-accelerated PipeWire thumbnail
        PipeWire.PipeWireSourceItem {
            id: pipeWireItem
            anchors.fill: parent
            anchors.margins: parent.border.width > 0 ? 2 : 0
            nodeId: screencastRequest.nodeId
            allowDmaBuf: true
            visible: !root.isMinimized

            onStateChanged: function() {
                console.log("[krema.preview] PipeWire state:", pipeWireItem.state,
                    "ready:", pipeWireItem.ready, "nodeId:", pipeWireItem.nodeId)
            }
        }

        // Fallback: icon (when PipeWire not ready or minimized)
        Kirigami.Icon {
            anchors.centerIn: parent
            width: Kirigami.Units.iconSizes.large
            height: Kirigami.Units.iconSizes.large
            source: {
                let name = dockModel.iconName(root.parentIndex)
                return (name && name.length > 0) ? name : "application-x-executable"
            }
            visible: !pipeWireItem.ready
        }

        // Minimized overlay
        Rectangle {
            anchors.fill: parent
            color: Kirigami.Theme.backgroundColor
            opacity: 0.5
            visible: root.isMinimized
        }

        // Hover highlight
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: Kirigami.Theme.highlightColor
            opacity: thumbnailHover.hovered ? 0.1 : 0
            visible: opacity > 0

            Behavior on opacity {
                NumberAnimation { duration: Kirigami.Units.shortDuration }
            }
        }

        // Close button (top-right corner, always visible)
        QQC2.ToolButton {
            id: closeButton
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Kirigami.Units.smallSpacing
            width: Kirigami.Units.iconSizes.smallMedium
            height: Kirigami.Units.iconSizes.smallMedium
            icon.name: "window-close"
            icon.width: Kirigami.Units.iconSizes.small
            icon.height: Kirigami.Units.iconSizes.small
            z: 10

            onClicked: {
                // childIndex < 0 = single window (parent row IS the window)
                let modelIndex = (root.childIndex < 0)
                    ? dockModel.tasksModel.index(root.parentIndex, 0)
                    : dockModel.tasksModel.makeModelIndex(root.parentIndex, root.childIndex)
                dockModel.tasksModel.requestClose(modelIndex)
            }

            background: Rectangle {
                radius: width / 2
                color: closeButton.hovered
                    ? Kirigami.Theme.negativeTextColor
                    : Kirigami.Theme.backgroundColor
                opacity: closeButton.hovered ? 0.9 : 0.7

                Behavior on color {
                    ColorAnimation { duration: Kirigami.Units.shortDuration }
                }
                Behavior on opacity {
                    NumberAnimation { duration: Kirigami.Units.shortDuration }
                }
            }
        }

        // Click to activate window
        MouseArea {
            id: thumbnailMouseArea
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton

            onClicked: {
                // childIndex < 0 = single window (parent row IS the window)
                let modelIndex = (root.childIndex < 0)
                    ? dockModel.tasksModel.index(root.parentIndex, 0)
                    : dockModel.tasksModel.makeModelIndex(root.parentIndex, root.childIndex)
                dockModel.tasksModel.requestActivate(modelIndex)
                previewController.hidePreview()
            }
        }
    }

    // Window title
    QQC2.Label {
        id: titleLabel
        anchors.top: thumbnailContainer.bottom
        anchors.topMargin: Kirigami.Units.smallSpacing
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.thumbnailWidth
        text: root.title
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        maximumLineCount: 1
        font: Kirigami.Theme.smallFont
    }
}
