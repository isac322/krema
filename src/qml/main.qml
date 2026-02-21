// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

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

    // --- Internal drag state ---
    property bool _dragActive: false
    property int _dragSourceIndex: -1
    property int _dragTargetIndex: -1
    property real _dragCurrentX: 0
    property real _dragCurrentY: 0
    property real _dragStartX: 0
    property real _dragStartY: 0
    property bool _dragPending: false      // press-hold started but not yet moved enough
    property bool _dragWasActive: false     // was drag active during this press cycle (suppress click)
    readonly property real _dragThreshold: 10

    Timer {
        id: dragHoldTimer
        interval: 300
        onTriggered: {
            if (root.hoveredIndex >= 0) {
                root._dragPending = true
                root._dragSourceIndex = root.hoveredIndex
            }
        }
    }

    // Compute the target index where the dragged item would be inserted.
    // Compares mouse X with each icon's center X (including the source so
    // that dropping near the original position keeps the item in place).
    function computeDropIndex(globalMouseX) {
        let panelRelX = globalMouseX - dockPanel.x
        let items = []
        for (let i = 0; i < dockRepeater.count; i++) {
            let item = dockRepeater.itemAt(i)
            if (!item) continue
            items.push({ idx: i, cx: item.x + item.width / 2 + dockRow.x })
        }
        if (items.length === 0) return -1

        // Find closest icon center
        let bestIdx = items[0].idx
        let bestDist = Math.abs(panelRelX - items[0].cx)
        for (let j = 1; j < items.length; j++) {
            let d = Math.abs(panelRelX - items[j].cx)
            if (d < bestDist) { bestDist = d; bestIdx = items[j].idx }
        }
        return bestIdx
    }

    // Compute which icon is under an external drop cursor (unscaled hit test).
    function computeExternalDropIndex(dropX) {
        for (let i = 0; i < dockRepeater.count; i++) {
            let item = dockRepeater.itemAt(i)
            if (!item) continue
            let itemLeft = dockRow.x + item.x
            let itemRight = itemLeft + item.width
            if (dropX >= itemLeft && dropX <= itemRight) return i
        }
        return -1
    }

    function isDesktopFileUrl(url) {
        let str = url.toString()
        return str.endsWith(".desktop") || str.startsWith("applications:")
    }

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
        // Uses Schmitt-trigger hysteresis: the currently-hovered icon has a wider
        // effective claim radius (exit threshold), so small mouse movements toward
        // a neighbor don't immediately switch the selection. This prevents
        // accidental clicks on the wrong icon when the hovered icon is largest.
        // Ref: Grossman & Balakrishnan (CHI 2005, Bubble Cursor), Amazon mega-menu.
        let bestIndex = -1
        let bestNormDist = Infinity
        // Hysteresis factor: 0.15 (light) – 0.35 (strong). Default 0.25.
        let hysteresisFactor = 0.25
        for (let i = 0; i < dockRepeater.count; i++) {
            let item = dockRepeater.itemAt(i)
            if (!item) continue

            // Horizontal: normalized distance (0 = center, 1 = edge of scaled icon)
            let dist = Math.abs(dockPanel.mouseX - item.itemCenterX)
            let scaledHalfWidth = (item.width * item.currentScale) / 2
            let normDist = dist / scaledHalfWidth

            // Hysteresis: currently-hovered icon uses wider exit threshold
            let maxNorm = (i === hoveredIndex) ? (1.0 + hysteresisFactor) : 1.0
            if (normDist >= maxNorm) continue

            // Vertical check: Scale origin.y = height → bottom fixed, grows upward
            let itemBottom = dockRow.y + item.y + item.height
            let itemTop = itemBottom - item.height * item.currentScale
            if (dockPanel.mouseY < itemTop || dockPanel.mouseY > itemBottom) continue

            // Comparison: currently-hovered icon gets distance bonus (sticky)
            let effectiveDist = (i === hoveredIndex) ? normDist * (1.0 - hysteresisFactor) : normDist
            if (effectiveDist < bestNormDist) { bestIndex = i; bestNormDist = effectiveDist }
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
            // Drag-out: if an active drag leaves the dock, unpin the launcher
            if (root._dragActive) {
                if (dockModel.isPinned(root._dragSourceIndex)) {
                    dockModel.removeLauncher(root._dragSourceIndex)
                }
                root._dragActive = false
                root._dragPending = false
                root._dragWasActive = false
                root._dragSourceIndex = -1
                root._dragTargetIndex = -1
                dockVisibility.setInteracting(false)
            } else if (root._dragPending) {
                root._dragPending = false
                root._dragWasActive = false
                root._dragSourceIndex = -1
                root._dragTargetIndex = -1
                dragHoldTimer.stop()
            }
            dockVisibility.setHovered(false)
        }

        // Start drag hold timer on left-button press
        onPressed: function(mouse) {
            if (mouse.button === Qt.LeftButton && root.hoveredIndex >= 0) {
                root._dragStartX = mouse.x
                root._dragStartY = mouse.y
                root._dragWasActive = false
                dragHoldTimer.restart()
            }
        }

        onReleased: function(mouse) {
            dragHoldTimer.stop()

            if (root._dragActive) {
                // Execute reorder
                if (root._dragTargetIndex >= 0 && root._dragTargetIndex !== root._dragSourceIndex) {
                    dockModel.moveTask(root._dragSourceIndex, root._dragTargetIndex)
                }
                // Reset drag state
                root._dragActive = false
                root._dragPending = false
                root._dragSourceIndex = -1
                root._dragTargetIndex = -1
                dockVisibility.setInteracting(false)
            } else {
                root._dragPending = false
            }
        }

        // Click handling: uses hoveredIndex from scaled hit testing
        // so clicks work correctly on zoomed icons
        onClicked: function(mouse) {
            // Suppress click if drag was active during this press cycle
            if (root._dragWasActive) {
                root._dragWasActive = false
                return
            }
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

        // Track mouse position for parabolic zoom + drag handling
        onPositionChanged: function(mouse) {
            // --- Drag handling ---
            if (root._dragPending && !root._dragActive) {
                let dx = mouse.x - root._dragStartX
                let dy = mouse.y - root._dragStartY
                if (Math.sqrt(dx * dx + dy * dy) > root._dragThreshold) {
                    root._dragActive = true
                    root._dragWasActive = true
                    dockVisibility.setInteracting(true)  // Prevent dock hide during drag
                    tooltipItem.show = false
                    tooltipTimer.stop()
                }
            }

            if (root._dragActive) {
                root._dragCurrentX = mouse.x
                root._dragCurrentY = mouse.y
                root._dragTargetIndex = computeDropIndex(mouse.x)
                return  // Skip normal zoom handling during drag
            }

            // --- Normal zoom tracking ---
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
        height: dockView.iconSize + Kirigami.Units.largeSpacing * 2  // icon + padding
        width: Math.max(dockRow.implicitWidth + Kirigami.Units.largeSpacing * 2, Kirigami.Units.gridUnit * 6)  // content + padding, min width
        radius: dockView.cornerRadius
        color: dockView.backgroundColor

        // Slide animation: y position controlled by visibility.
        // floatingPadding creates a visual gap between the panel and the screen edge
        // without using layer-shell margin (which would cause surface repositioning).
        y: dockVisibility.dockVisible
           ? parent.height - height - dockView.floatingPadding
           : parent.height + Kirigami.Units.largeSpacing

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
        // Zoom is disabled during drag so all icons return to base scale.
        property bool mouseInside: mouseX >= 0 && root._zoomActive && !root._dragActive

        // Report panel geometry to visibility controller for input region
        onXChanged: dockVisibility.setPanelRect(x, y, width, height)
        onWidthChanged: dockVisibility.setPanelRect(x, y, width, height)
        onYChanged: dockVisibility.setPanelRect(x, y, width, height)
        onHeightChanged: dockVisibility.setPanelRect(x, y, width, height)

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

                    // Drag and drop visual feedback
                    isDragSource: root._dragActive && root._dragSourceIndex === index
                    isExternalDropTarget: externalDropArea.containsDrag
                                          && externalDropArea.dropTargetIndex === index
                }
            }
        }

        // External drag and drop (files, .desktop, URLs from other apps)
        DropArea {
            id: externalDropArea
            anchors.fill: parent
            property int dropTargetIndex: -1

            onEntered: function(drag) {
                drag.accepted = true
            }

            onPositionChanged: function(drag) {
                dropTargetIndex = root.computeExternalDropIndex(drag.x)
            }

            onDropped: function(drop) {
                let urls = []
                if (drop.hasUrls) {
                    for (let i = 0; i < drop.urls.length; i++) {
                        urls.push(drop.urls[i])
                    }
                }

                if (urls.length === 0) {
                    drop.accepted = false
                    dropTargetIndex = -1
                    return
                }

                // Check first URL to classify the drop
                let firstUrl = urls[0]
                let isLauncher = dockModel.isDesktopFile(firstUrl)

                if (isLauncher) {
                    // .desktop file → add as pinned launcher
                    dockModel.addLauncher(firstUrl)
                } else if (dropTargetIndex >= 0) {
                    // Regular file(s) on an app icon → open with that app
                    dockModel.openUrlsWithTask(dropTargetIndex, urls)
                }
                // else: regular file on dock background → no action

                drop.accepted = true
                dropTargetIndex = -1
            }

            onExited: {
                dropTargetIndex = -1
            }
        }
    }

    // Floating drag ghost icon (follows cursor during internal reorder drag)
    Image {
        id: dragGhost
        visible: root._dragActive && root._dragSourceIndex >= 0
        width: dockView.iconSize
        height: dockView.iconSize
        source: {
            if (!visible) return ""
            let name = dockModel.iconName(root._dragSourceIndex)
            return (name && name.length > 0) ? "image://icon/" + name : ""
        }
        sourceSize: Qt.size(dockView.iconSize, dockView.iconSize)
        x: root._dragCurrentX - width / 2
        y: root._dragCurrentY - height / 2
        opacity: 0.8
        z: 200
    }

    // Drop position indicator line (shown during internal reorder drag)
    Rectangle {
        id: dropIndicator
        visible: root._dragActive && root._dragTargetIndex >= 0
                 && root._dragTargetIndex !== root._dragSourceIndex
        width: 2
        height: dockView.iconSize
        color: Kirigami.Theme.highlightColor
        radius: 1
        z: 150

        x: {
            if (!visible || root._dragTargetIndex < 0) return 0
            let targetItem = dockRepeater.itemAt(root._dragTargetIndex)
            if (!targetItem) return 0
            let itemX = dockPanel.x + dockRow.x + targetItem.x
            // Show line on the side where the item will be inserted
            if (root._dragTargetIndex > root._dragSourceIndex) {
                return itemX + targetItem.width + dockView.iconSpacing / 2 - 1
            } else {
                return itemX - dockView.iconSpacing / 2 - 1
            }
        }
        y: dockPanel.y + dockRow.y
    }

    // Handle launch bounce trigger from C++ signal.
    // Only sets manualLaunching for already-running apps (IsWindow): their
    // delegate stays alive, so manualLaunching persists through the bounce.
    // For launchers (first launch), we skip manualLaunching entirely:
    // IsStartup fires within ~5ms and, being model data, survives the
    // delegate recreation caused by hideActivatedLaunchers.
    Connections {
        target: dockModel
        function onTaskLaunching(index) {
            let item = dockRepeater.itemAt(index)
            if (!item) return

            // Skip for launcher items — IsStartup will drive the bounce.
            if (!item.model.IsWindow) return

            item.manualLaunching = true
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
        y: dockPanel.y - height - Kirigami.Units.largeSpacing

        Kirigami.Theme.colorSet: Kirigami.Theme.Tooltip
        Kirigami.Theme.inherit: false

        width: tooltipLabel.implicitWidth + Kirigami.Units.largeSpacing * 2
        height: tooltipLabel.implicitHeight + Kirigami.Units.largeSpacing
        radius: Kirigami.Units.smallSpacing
        color: Kirigami.Theme.backgroundColor
        z: 100

        QQC2.Label {
            id: tooltipLabel
            anchors.centerIn: parent
            text: root.hoveredName
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
