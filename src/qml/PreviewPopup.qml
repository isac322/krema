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
 */
Item {
    id: root
    anchors.fill: parent

    // C++ singletons: PreviewController, DockModel

    // Surface-level hover detection: keep preview visible when mouse is anywhere
    // on this surface. The C++ input region (updateInputRegion) already constrains
    // which events reach this surface to the popup area + margins, so detecting
    // hover at the surface root is equivalent to detecting hover on the popup.
    // This is more reliable than a popup-child MouseArea because it doesn't depend
    // on popup geometry, which can shift during layout.
    HoverHandler {
        id: surfaceHover
        onHoveredChanged: PreviewController.setPreviewHovered(hovered)
    }

    // Parent's window IDs — used by both grouped and single preview.
    // On Wayland these are UUID strings matching Plasma's WinIdList format.
    // Kept as a direct QVariantList from the model (like Plasma's approach).
    property var parentWinIds: []

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
        x: PreviewController.contentX
        width: popupContent.implicitWidth + 2 * Kirigami.Units.largeSpacing
        height: popupContent.implicitHeight + 2 * Kirigami.Units.largeSpacing
        // Anchor to bottom of the surface (which sits above the dock)
        y: parent.height - height
        radius: Kirigami.Units.cornerRadius
        color: Kirigami.Theme.backgroundColor

        Kirigami.Theme.colorSet: Kirigami.Theme.Window
        Kirigami.Theme.inherit: false

        // Shadow-like border (guard against undefined during theme init)
        border.color: Kirigami.Theme.separatorColor ?? "transparent"
        border.width: 1

        // Report size to controller for input region + positioning
        onWidthChanged: PreviewController.setContentSize(width, height)
        onHeightChanged: PreviewController.setContentSize(width, height)

        ColumnLayout {
            id: popupContent
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
                        parentIndex: PreviewController.parentIndex
                        childIndex: model.childIndex
                    }
                }
            }

        }
    }
}
