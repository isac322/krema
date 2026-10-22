// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.taskmanager as TaskManager
import com.bhyoo.krema 1.0

/**
 * Preview popup root component.
 *
 * Loaded into a full-width layer-shell overlay surface.
 * Only the popup rectangle receives input (via input region in C++).
 *
 * Keyboard navigation is driven entirely from the dock surface via
 * PreviewController C++ properties (previewKeyboardActive, focusedThumbnailIndex).
 * This avoids unreliable focus transfer between layer-shell surfaces.
 */
Item {
    id: root
    anchors.fill: parent

    // C++ singletons: PreviewController, DockModel

    function announcePreviewOpen(appName, count) {
        Accessible.announce(
            i18n("Preview for %1, %2 windows", appName, count),
            Accessible.Polite)
    }

    // Surface-level hover detection: keep preview visible when mouse is anywhere
    // on this surface. The C++ input region (updateInputRegion) already constrains
    // which events reach this surface to the visible popup rectangle, so detecting
    // hover at the surface root is equivalent to detecting hover on the popup.
    // This is more reliable than a popup-child MouseArea because it doesn't depend
    // on popup geometry, which can shift during layout.
    HoverHandler {
        id: surfaceHover
        onHoveredChanged: {
            PreviewController.setPreviewHovered(hovered)
            // Mouse movement cancels keyboard navigation in preview
            if (hovered && PreviewController.previewKeyboardActive) {
                PreviewController.endPreviewKeyboardNav()
            }
        }
    }

    // Parent's window IDs — used by both grouped and single preview.
    // On Wayland these are UUID strings matching Plasma's WinIdList format.
    // Kept as a direct QVariantList from the model (like Plasma's approach).
    property var parentWinIds: []

    // Keep a small number of last-seen thumbnail frames so a minimized window
    // can still show its content after its PipeWire stream stops. ItemGrabResult
    // objects are retained because their URLs remain valid while the result lives.
    property var thumbnailFrames: ({})
    property var thumbnailFrameKeys: []
    readonly property int maxThumbnailFrames: 8
    readonly property int maxConcurrentPipeWireStreams: 4

    // MPRIS state is associated with the application represented by this
    // preview. The service follows its own play/pause and track changes.
    readonly property var _mediaPlayer: {
        let _rev = MediaController.revision
        if (!PreviewController.visible || PreviewController.parentIndex < 0)
            return ({})
        const taskIndex = DockModel.tasksModel.index(PreviewController.parentIndex, 0)
        const appPid = DockModel.tasksModel.data(taskIndex, TaskManager.AbstractTasksModel.AppPid) || 0
        return MediaController.playerForApp(
            DockModel.appId(PreviewController.parentIndex), Number(appPid))
    }

    function rememberThumbnailFrame(winId, result) {
        if (winId === undefined || winId === null || !result || !result.url
                || result.url.toString() === "") {
            return
        }

        let key = String(winId)
        let frames = Object.assign({}, thumbnailFrames)
        let keys = thumbnailFrameKeys.filter(function(existingKey) {
            return existingKey !== key
        })
        frames[key] = result
        keys.push(key)

        while (keys.length > maxThumbnailFrames) {
            delete frames[keys.shift()]
        }

        thumbnailFrames = frames
        thumbnailFrameKeys = keys
    }

    // Child window model for grouped preview.
    // Uses ListModel instead of JS array so Repeater preserves delegates
    // on incremental updates — critical for keeping PipeWire streams alive.
    ListModel {
        id: childWindowModel
    }

    function rebuildChildModel() {
        let parentIdx = PreviewController.parentIndex
        if (parentIdx < 0) {
            // Keep delegates alive so PipeWire streams pause/resume naturally
            // via visibility cascade. Destroying delegates triggers
            // ScreencastingRequest race conditions on reopen.
            parentWinIds = []
            return
        }

        // Get ALL window IDs from the parent task (like Plasma does).
        let parentModelIdx = DockModel.tasksModel.index(parentIdx, 0)
        let allWinIds = DockModel.tasksModel.data(
            parentModelIdx, TaskManager.AbstractTasksModel.WinIdList) || []
        parentWinIds = allWinIds

        let childCount = DockModel.childCount(parentIdx)

        // === Single window (not grouped): parent row IS the window ===
        // KDE TasksModel tree: single window → rowCount(idx) = 0 (no children)
        // Group (2+ windows) → rowCount(idx) = N (N children)
        if (childCount === 0) {
            if (allWinIds.length > 0) {
                let entry = {
                    title: DockModel.tasksModel.data(
                        parentModelIdx, Qt.DisplayRole) || "",
                    isMinimized: DockModel.tasksModel.data(
                        parentModelIdx,
                        TaskManager.AbstractTasksModel.IsMinimized) || false,
                    isActive: DockModel.tasksModel.data(
                        parentModelIdx,
                        TaskManager.AbstractTasksModel.IsActive) || false,
                    childIndex: -1  // sentinel: use parent row directly
                }
                if (childWindowModel.count > 0) {
                    childWindowModel.set(0, entry)
                } else {
                    childWindowModel.append(entry)
                }
                while (childWindowModel.count > 1) {
                    childWindowModel.remove(childWindowModel.count - 1)
                }
            } else {
                // Launcher only (no windows) — clear model
                while (childWindowModel.count > 0) {
                    childWindowModel.remove(childWindowModel.count - 1)
                }
            }
        } else {
            // === Grouped (2+ windows): use child rows ===
            // Use the lesser of childCount and winId count to avoid undefined winIds
            // when WinIdList hasn't been updated yet (async mismatch).
            // dataChanged signal will fire when WinIdList updates, triggering another rebuild.
            let maxCount = Math.min(childCount, allWinIds.length)

            // Incremental update: set() preserves existing delegates (and their
            // PipeWire streams), append() adds new ones, remove() trims excess.
            for (let i = 0; i < maxCount; i++) {
                let childModelIndex = DockModel.tasksModel.makeModelIndex(parentIdx, i)
                let entry = {
                    title: DockModel.tasksModel.data(childModelIndex, Qt.DisplayRole) || "",
                    isMinimized: DockModel.tasksModel.data(
                        childModelIndex, TaskManager.AbstractTasksModel.IsMinimized) || false,
                    isActive: DockModel.tasksModel.data(
                        childModelIndex, TaskManager.AbstractTasksModel.IsActive) || false,
                    childIndex: i
                }

                if (i < childWindowModel.count) {
                    childWindowModel.set(i, entry)
                } else {
                    childWindowModel.append(entry)
                }
            }

            // Remove excess items from the end
            while (childWindowModel.count > maxCount) {
                childWindowModel.remove(childWindowModel.count - 1)
            }
        }

        // Retry screencast for delegates where KWin hadn't registered the window yet.
        // Called on each KDE model event (dataChanged, rowsInserted, etc.),
        // so new windows get multiple retry chances without timers.
        for (let i = 0; i < thumbnailRepeater.count; i++) {
            let item = thumbnailRepeater.itemAt(i)
            if (item) item.retryScreencast()
        }
    }

    Connections {
        target: PreviewController
        function onParentIndexChanged() {
            if (PreviewController.parentIndex >= 0) {
                // Different app = different PipeWire streams, must recreate delegates
                childWindowModel.clear()
            }
            root.rebuildChildModel()
        }
        function onVisibleChanged() {
            if (PreviewController.visible && PreviewController.parentIndex >= 0) {
                let count = childWindowModel.count
                root.announcePreviewOpen(PreviewController.appName, count)
            }
        }
    }

    // Also rebuild when the underlying model data changes (window close, title change)
    Connections {
        target: DockModel.tasksModel
        function onDataChanged() {
            if (PreviewController.visible && PreviewController.parentIndex >= 0) {
                root.rebuildChildModel()
            }
        }
        function onRowsInserted() {
            if (PreviewController.visible && PreviewController.parentIndex >= 0) {
                root.rebuildChildModel()
            }
        }
        function onRowsRemoved() {
            if (PreviewController.visible && PreviewController.parentIndex >= 0) {
                // Child removed: check if parent still valid
                let parentIdx = PreviewController.parentIndex
                let parentModelIdx = DockModel.tasksModel.index(parentIdx, 0)
                if (!parentModelIdx.valid) {
                    PreviewController.hidePreview()
                } else {
                    root.rebuildChildModel()
                }
            }
        }
    }

    // The visible popup container
    Rectangle {
        id: popup
        visible: PreviewController.visible && PreviewController.parentIndex >= 0

        Accessible.role: Accessible.PopupMenu
        Accessible.name: PreviewController.appName
            ? i18n("Preview for %1", PreviewController.appName)
            : ""
        x: {
            if (DockView.edge === 2) return 0                            // Left → left edge
            if (DockView.edge === 3) return parent.width - width         // Right → right edge
            return PreviewController.contentX                            // Top/Bottom → centered
        }
        width: popupContent.implicitWidth + 2 * Kirigami.Units.largeSpacing
        height: popupContent.implicitHeight + 2 * Kirigami.Units.largeSpacing
        y: {
            if (DockView.edge === 0) return 0                            // Top → top edge
            if (DockView.edge === 1) return parent.height - height       // Bottom → bottom edge
            return PreviewController.contentY                            // Left/Right → centered
        }
        readonly property bool acrylicBackground: DockView.backgroundStyleType === 3
        radius: acrylicBackground ? DockSettings.cornerRadius : Kirigami.Units.cornerRadius
        color: acrylicBackground ? "transparent" : Kirigami.Theme.backgroundColor

        Kirigami.Theme.colorSet: Kirigami.Theme.Window
        Kirigami.Theme.inherit: false

        // Shadow-like border (guard against undefined during theme init)
        border.color: "#808080"
        border.width: acrylicBackground ? 0 : 2

        // Match the dock's Acrylic treatment: when KWin blur-behind is
        // available, it blurs this popup region; the shared shader adds tint
        // and grain either way.
        ShaderEffect {
            anchors.fill: parent
            z: 0
            visible: popup.acrylicBackground
            property real tintR: DockView.backgroundColor.r
            property real tintG: DockView.backgroundColor.g
            property real tintB: DockView.backgroundColor.b
            property real tintOpacity: DockView.backgroundColor.a
            property real noiseStrength: 0.02
            property real resX: width
            property real resY: height
            property real cornerRadius: popup.radius
            fragmentShader: "qrc:/qml/shaders/acrylic_overlay.frag.qsb"
        }

        Rectangle {
            anchors.fill: parent
            z: 2
            visible: popup.acrylicBackground
            radius: popup.radius
            color: "transparent"
            border.color: "#808080"
            border.width: 2
            Accessible.ignored: true
        }

        // Report size to controller for input region + positioning
        onWidthChanged: PreviewController.setContentSize(width, height)
        onHeightChanged: PreviewController.setContentSize(width, height)

        ColumnLayout {
            id: popupContent
            z: 1
            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            // App name header (width capped to thumbnail row)
            QQC2.Label {
                Layout.fillWidth: true
                Layout.maximumWidth: groupedRow.implicitWidth
                text: PreviewController.appName
                font.weight: Font.Bold
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }

            // Separator
            Kirigami.Separator {
                Layout.fillWidth: true
            }

            // Grouped windows: horizontal row of thumbnails
            Row {
                id: groupedRow
                Layout.alignment: Qt.AlignHCenter
                spacing: Kirigami.Units.smallSpacing
                visible: PreviewController.visible && PreviewController.parentIndex >= 0

                Repeater {
                    id: thumbnailRepeater
                    model: childWindowModel

                    PreviewThumbnail {
                        // Get winId from QVariantList to preserve QVariant type
                        winId: (index >= 0 && index < root.parentWinIds.length)
                            ? root.parentWinIds[index] : undefined
                        title: model.title
                        isMinimized: model.isMinimized
                        isActive: model.isActive
                        liveCaptureEnabled: index < root.maxConcurrentPipeWireStreams
                        parentIndex: PreviewController.parentIndex
                        childIndex: model.childIndex
                        frameCacheOwner: root
                        // Focus ring driven by C++ PreviewController state
                        isKeyboardFocused: PreviewController.previewKeyboardActive
                            && PreviewController.focusedThumbnailIndex === index
                    }
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
                visible: mediaControls.visible
            }

            ColumnLayout {
                id: mediaControls
                Layout.fillWidth: true
                Layout.maximumWidth: groupedRow.implicitWidth
                spacing: Kirigami.Units.smallSpacing
                visible: root._mediaPlayer.available === true

                QQC2.Label {
                    Layout.fillWidth: true
                    text: root._mediaPlayer.title || root._mediaPlayer.identity || ""
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: (root._mediaPlayer.artist || "").length > 0
                    text: root._mediaPlayer.artist || ""
                    opacity: 0.75
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 2

                    QQC2.ToolButton {
                        visible: root._mediaPlayer.canSeek === true
                        width: 28
                        height: 28
                        padding: 1
                        contentItem: Item {
                            Kirigami.Icon {
                                anchors.centerIn: parent
                                width: 18
                                height: 18
                                source: "media-seek-backward"
                            }
                            QQC2.Label {
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                text: "10"
                                font.pixelSize: 8
                                font.bold: true
                                color: Kirigami.Theme.textColor
                            }
                        }
                        Accessible.name: i18n("Rewind 10 seconds")
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.text: i18n("Rewind 10 seconds")
                        onClicked: MediaController.seekBackward(root._mediaPlayer.service)
                    }

                    QQC2.ToolButton {
                        visible: root._mediaPlayer.canGoPrevious === true
                        width: 28
                        height: 28
                        padding: 1
                        icon.name: "media-skip-backward"
                        icon.width: 16
                        icon.height: 16
                        Accessible.name: i18n("Previous track")
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.text: i18n("Previous track")
                        onClicked: MediaController.previous(root._mediaPlayer.service)
                    }

                    QQC2.ToolButton {
                        visible: root._mediaPlayer.canPlay === true || root._mediaPlayer.canPause === true
                        width: 32
                        height: 32
                        padding: 1
                        icon.name: root._mediaPlayer.playbackStatus === "Playing"
                            ? "media-playback-pause" : "media-playback-start"
                        icon.width: 18
                        icon.height: 18
                        Accessible.name: root._mediaPlayer.playbackStatus === "Playing"
                            ? i18n("Pause") : i18n("Play")
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.text: Accessible.name
                        onClicked: MediaController.playPause(root._mediaPlayer.service)
                    }

                    QQC2.ToolButton {
                        visible: root._mediaPlayer.canGoNext === true
                        width: 28
                        height: 28
                        padding: 1
                        icon.name: "media-skip-forward"
                        icon.width: 16
                        icon.height: 16
                        Accessible.name: i18n("Next track")
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.text: i18n("Next track")
                        onClicked: MediaController.next(root._mediaPlayer.service)
                    }

                    QQC2.ToolButton {
                        visible: root._mediaPlayer.canSeek === true
                        width: 28
                        height: 28
                        padding: 1
                        contentItem: Item {
                            Kirigami.Icon {
                                anchors.centerIn: parent
                                width: 18
                                height: 18
                                source: "media-seek-forward"
                            }
                            QQC2.Label {
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                text: "10"
                                font.pixelSize: 8
                                font.bold: true
                                color: Kirigami.Theme.textColor
                            }
                        }
                        Accessible.name: i18n("Forward 10 seconds")
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.text: i18n("Forward 10 seconds")
                        onClicked: MediaController.seekForward(root._mediaPlayer.service)
                    }
                }
            }

        }
    }
}
