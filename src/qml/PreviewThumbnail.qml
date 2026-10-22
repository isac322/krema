// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.pipewire as PipeWire
import org.kde.taskmanager as TaskManager
import com.bhyoo.krema 1.0

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
    property bool isKeyboardFocused: false
    property var frameCacheOwner
    property bool snapshotPending: false
    property int snapshotCaptureSerial: 0
    property bool liveCaptureEnabled: true

    Accessible.role: Accessible.Button
    Accessible.name: {
        let parts = [title]
        if (isActive) parts.push(i18n("Active"))
        if (isMinimized) parts.push(i18n("Minimized"))
        return parts.join(", ")
    }
    Accessible.focusable: true
    Accessible.focused: isKeyboardFocused
    Accessible.onPressAction: thumbnailMouseArea.clicked(null)

    readonly property real thumbnailWidth: DockSettings.previewThumbnailSize
    readonly property real thumbnailHeight: thumbnailWidth * 0.7
    readonly property url cachedFrameUrl: {
        if (!frameCacheOwner) return ""
        if (winId === undefined || winId === null) return ""
        let frame = frameCacheOwner.thumbnailFrames[String(winId)]
        return frame ? frame.url : ""
    }

    implicitWidth: thumbnailWidth + 2 * Kirigami.Units.smallSpacing
    implicitHeight: thumbnailHeight + titleLabel.implicitHeight + Kirigami.Units.smallSpacing * 3

    // PipeWire screencasting: uuid → nodeId
    // Match Plasma's pattern: pass winId directly, let QML engine handle QVariant→QString
    TaskManager.ScreencastingRequest {
        id: screencastRequest
        uuid: root.liveCaptureEnabled ? (root.winId ?? "") : ""
        onNodeIdChanged: function() {
            console.log("[krema.preview] nodeId:", screencastRequest.nodeId,
                "for uuid:", screencastRequest.uuid)
        }
    }

    // Retry screencast when nodeId is 0 (KWin hadn't registered the window yet).
    // Called from PreviewPopup.rebuildChildModel() on each KDE model event.
    function retryScreencast() {
        if (root.liveCaptureEnabled && screencastRequest.nodeId === 0 && root.winId) {
            screencastRequest.uuid = ""
            screencastRequest.uuid = root.winId ?? ""
        }
    }

    function captureSnapshot(force) {
        if (!root.winId || !PreviewController.visible
                || !pipeWireItem.ready || root.snapshotPending) return
        if (!force && root.cachedFrameUrl.toString() !== "") return

        root.snapshotPending = true
        let serial = ++root.snapshotCaptureSerial
        let capturedWinId = String(root.winId)

        // Let Qt Quick render the latest PipeWire frame before grabbing it.
        Qt.callLater(function() {
            if (serial !== root.snapshotCaptureSerial
                    || capturedWinId !== String(root.winId)) {
                return
            }

            let started = pipeWireItem.grabToImage(function(result) {
                if (serial === root.snapshotCaptureSerial) {
                    root.snapshotPending = false
                }
                if (capturedWinId === String(root.winId) && root.frameCacheOwner) {
                    root.frameCacheOwner.rememberThumbnailFrame(capturedWinId, result)
                }
            }, Qt.size(root.thumbnailWidth, root.thumbnailHeight))

            if (!started && serial === root.snapshotCaptureSerial) {
                root.snapshotPending = false
            }
        })
    }

    onWinIdChanged: {
        // An in-flight grab belongs to the previous window delegate identity.
        snapshotCaptureSerial++
        snapshotPending = false
    }

    onIsMinimizedChanged: {
        if (isMinimized && PreviewController.visible) captureSnapshot(true)
    }

    // Keyboard focus ring for preview thumbnail
    Rectangle {
        id: previewFocusRing
        anchors.fill: thumbnailContainer
        anchors.margins: -2
        radius: thumbnailContainer.radius + 2
        color: "transparent"
        border.color: Kirigami.Theme.focusColor
        border.width: 2
        visible: root.isKeyboardFocused
        Accessible.ignored: true
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
            nodeId: root.liveCaptureEnabled ? screencastRequest.nodeId : 0
            allowDmaBuf: true
            // Keep the window stream active for as long as the preview popup
            // is visible, including while the window is minimized. The parent
            // preview row hides this item when the popup closes, releasing the
            // PipeWire/GPU resources through the normal visibility cascade.
            visible: true

            onReadyChanged: {
                if (ready && PreviewController.visible) {
                    root.captureSnapshot(false)
                }
            }
            Accessible.ignored: true

            onStateChanged: function() {
                console.log("[krema.preview] PipeWire state:", pipeWireItem.state,
                    "ready:", pipeWireItem.ready, "nodeId:", pipeWireItem.nodeId)
            }
        }

        // Keep the last captured frame as a fallback if a minimized window's
        // PipeWire source cannot provide a frame. The image is bounded to the
        // thumbnail size; a ready stream takes precedence so it can keep updating.
        Image {
            id: cachedThumbnail
            anchors.fill: parent
            source: root.cachedFrameUrl
            sourceSize: Qt.size(root.thumbnailWidth, root.thumbnailHeight)
            smooth: true
            visible: root.isMinimized && !pipeWireItem.ready && status === Image.Ready
            Accessible.ignored: true
        }

        // Fallback: icon when neither a PipeWire frame nor a saved frame is available.
        Item {
            id: fallbackIcon
            anchors.centerIn: parent
            width: Kirigami.Units.iconSizes.large
            height: Kirigami.Units.iconSizes.large
            readonly property string iconName: {
                // iconName() is invokable, so track the model's artwork revision.
                let iconCacheVersion = DockView.iconCacheVersion
                return DockModel.iconName(root.parentIndex) || "application-x-executable"
            }
            readonly property bool isRawIcon: iconName.startsWith("raw:")
            visible: !pipeWireItem.ready && !cachedThumbnail.visible

            Kirigami.Icon {
                anchors.fill: parent
                source: fallbackIcon.isRawIcon ? "" : fallbackIcon.iconName
                visible: !fallbackIcon.isRawIcon
            }

            Image {
                anchors.fill: parent
                source: fallbackIcon.isRawIcon
                    ? "image://icon/" + fallbackIcon.iconName + "?v=" + DockView.iconCacheVersion
                    : ""
                sourceSize: Qt.size(width, height)
                smooth: true
                visible: fallbackIcon.isRawIcon
            }
        }

        // Minimized overlay
        Rectangle {
            anchors.fill: parent
            color: Kirigami.Theme.backgroundColor
            opacity: 0.5
            visible: root.isMinimized && !pipeWireItem.ready && !cachedThumbnail.visible
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
            z: 10

            // Draw the close glyph locally: the system icon theme may not
            // provide window-close, leaving a working button visually empty.
            contentItem: Item {
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small

                Rectangle {
                    anchors.centerIn: parent
                    width: Kirigami.Units.iconSizes.small * 0.78
                    height: Math.max(2, Kirigami.Units.smallSpacing / 2)
                    radius: height / 2
                    color: Kirigami.Theme.textColor
                    rotation: 45
                    antialiasing: true
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: Kirigami.Units.iconSizes.small * 0.78
                    height: Math.max(2, Kirigami.Units.smallSpacing / 2)
                    radius: height / 2
                    color: Kirigami.Theme.textColor
                    rotation: -45
                    antialiasing: true
                }
            }

            Accessible.name: i18n("Close %1", root.title)

            onClicked: {
                // childIndex < 0 = single window (parent row IS the window)
                let modelIndex = (root.childIndex < 0)
                    ? DockModel.tasksModel.index(root.parentIndex, 0)
                    : DockModel.tasksModel.makeModelIndex(root.parentIndex, root.childIndex)
                DockModel.tasksModel.requestClose(modelIndex)
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
            Accessible.ignored: true

            onClicked: {
                // childIndex < 0 = single window (parent row IS the window)
                let modelIndex = (root.childIndex < 0)
                    ? DockModel.tasksModel.index(root.parentIndex, 0)
                    : DockModel.tasksModel.makeModelIndex(root.parentIndex, root.childIndex)
                DockModel.tasksModel.requestActivate(modelIndex)
                PreviewController.hidePreview()
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
