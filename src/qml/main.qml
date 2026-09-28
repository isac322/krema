// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.taskmanager as TaskManager
import com.bhyoo.krema 1.0

/**
 * Main dock container.
 *
 * The root item fills the layer-shell surface. The visible dock panel
 * is a centered rounded rectangle that hugs its content.
 * The panel slides in/out based on DockVisibility.dockVisible.
 */
Item {
    id: root
    anchors.fill: parent

    Accessible.role: Accessible.ToolBar
    Accessible.name: i18n("Krema Dock")

    // C++ singletons: DockView, DockModel, DockActions, DockContextMenu, DockVisibility, DockSettings, PreviewController

    // Hovered item tracking (for custom tooltip and click targeting)
    property int hoveredIndex: -1
    property string hoveredName: ""

    // Keyboard navigation state
    property bool keyboardNavigating: false

    focus: true

    function announceLaunch(name) {
        Accessible.announce(i18n("Starting %1", name), Accessible.Assertive)
    }

    function startKeyboardNavigation() {
        keyboardNavigating = true
        // Suppress tooltip and preview auto-triggers during keyboard nav
        tooltipTimer.stop()
        tooltipItem.show = false
        if (dockRepeater.count > 0) {
            if (hoveredIndex < 0)
                hoveredIndex = 0
            // Set zoom position to the focused item's center
            let item = dockRepeater.itemAt(hoveredIndex)
            if (item) {
                dockPanel.mouseX = item.itemCenterX
                dockPanel.mouseY = dockRow.y + item.height / 2
                _zoomActive = true
            }
        }
        root.forceActiveFocus()
    }

    // Retry forceActiveFocus when the window becomes active from compositor.
    // Layer-shell keyboard interactivity is async (Wayland round-trip),
    // so forceActiveFocus() may fail if called before the window is active.
    Connections {
        target: root.Window.window
        function onActiveChanged() {
            if (root.Window.window && root.Window.window.active
                    && root.keyboardNavigating && !root.activeFocus) {
                root.forceActiveFocus()
            }
        }
    }

    function endKeyboardNavigation() {
        keyboardNavigating = false
        hoveredIndex = -1
        hoveredName = ""
        _zoomActive = false
        dockPanel.mouseX = -1
        dockPanel.mouseY = -1
        DockVisibility.setKeyboardActive(false)
    }

    function navigateItem(delta) {
        keyboardNavigating = true
        let count = dockRepeater.count
        if (count === 0) return

        if (hoveredIndex < 0) {
            hoveredIndex = delta > 0 ? 0 : count - 1
        } else {
            hoveredIndex = Math.max(0, Math.min(count - 1, hoveredIndex + delta))
        }
        hoveredName = dockRepeater.itemAt(hoveredIndex)?.displayName ?? ""

        // Reuse zoom logic: move the zoom cursor to the focused item's rest center
        let item = dockRepeater.itemAt(hoveredIndex)
        if (item) {
            dockPanel.mouseX = item.itemCenterX
            dockPanel.mouseY = dockRow.y + item.height / 2
            _zoomActive = true

            // Announce to screen reader
            let msg = item.displayName
            if (item.accessibleDescription)
                msg += ", " + item.accessibleDescription
            msg += ", " + i18n("%1 of %2", hoveredIndex + 1, count)
            Accessible.announce(msg, Accessible.Polite)
        }
    }

    // Announce preview thumbnail navigation (called after C++ state changes)
    function announcePreviewThumbnail() {
        let title = PreviewController.focusedThumbnailTitle()
        if (!title) return
        let msg = title
        if (PreviewController.focusedThumbnailIsActive())
            msg += ", " + i18n("Active")
        if (PreviewController.focusedThumbnailIsMinimized())
            msg += ", " + i18n("Minimized")
        msg += ", " + i18n("%1 of %2",
            PreviewController.focusedThumbnailIndex + 1,
            PreviewController.previewThumbnailCount())
        Accessible.announce(msg, Accessible.Polite)
    }

    Keys.onPressed: function(event) {
        if (!keyboardNavigating) return

        // Preview keyboard mode: route keys to PreviewController
        if (PreviewController.previewKeyboardActive) {
            // Thumbnail navigation follows dock axis (Left/Right for horizontal, Up/Down for vertical)
            let thumbPrev = DockView.isVertical ? Qt.Key_Up : Qt.Key_Left
            let thumbNext = DockView.isVertical ? Qt.Key_Down : Qt.Key_Right
            // Return to dock: key toward the dock edge
            let backKey = DockView.isVertical
                ? (DockView.edge === 2 ? Qt.Key_Left : Qt.Key_Right)
                : (DockView.edge === 0 ? Qt.Key_Up : Qt.Key_Down)

            switch (event.key) {
            case thumbPrev:
                PreviewController.navigatePreviewThumbnail(-1)
                announcePreviewThumbnail()
                event.accepted = true
                break
            case thumbNext:
                PreviewController.navigatePreviewThumbnail(1)
                announcePreviewThumbnail()
                event.accepted = true
                break
            case Qt.Key_Return:
            case Qt.Key_Enter:
                PreviewController.activatePreviewThumbnail()
                endKeyboardNavigation()
                event.accepted = true
                break
            case Qt.Key_Delete:
                PreviewController.closePreviewThumbnail()
                announcePreviewThumbnail()
                event.accepted = true
                break
            case Qt.Key_Escape:
            case backKey:
                // Return to dock navigation (keep preview visible)
                PreviewController.endPreviewKeyboardNav()
                event.accepted = true
                break
            }
            return
        }

        // Normal dock navigation — keys depend on orientation
        let navPrev = DockView.isVertical ? Qt.Key_Up : Qt.Key_Left
        let navNext = DockView.isVertical ? Qt.Key_Down : Qt.Key_Right
        // Preview open key: perpendicular to dock axis, away from edge
        let previewKey = DockView.isVertical
            ? (DockView.edge === 2 ? Qt.Key_Right : Qt.Key_Left)   // Left→Right, Right→Left
            : (DockView.edge === 0 ? Qt.Key_Down : Qt.Key_Up)      // Top→Down, Bottom→Up (was Key_Down for bottom)

        switch (event.key) {
        case navPrev:
            navigateItem(-1)
            event.accepted = true
            break
        case navNext:
            navigateItem(1)
            event.accepted = true
            break
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space:
            if (hoveredIndex >= 0) {
                DockActions.activate(hoveredIndex)
                endKeyboardNavigation()
            }
            event.accepted = true
            break
        case Qt.Key_Escape:
            // If preview is visible, hide it first
            if (PreviewController.visible) {
                PreviewController.hidePreview()
            }
            endKeyboardNavigation()
            event.accepted = true
            break
        case previewKey:
            // Open preview for the focused item (if it has windows)
            if (hoveredIndex >= 0) {
                let idx = DockModel.tasksModel.index(hoveredIndex, 0)
                let isWindow = DockModel.tasksModel.data(
                    idx, TaskManager.AbstractTasksModel.IsWindow)
                if (isWindow) {
                    let item = dockRepeater.itemAt(hoveredIndex)
                    if (item) {
                        showPreviewForItem(hoveredIndex, item)
                        PreviewController.startPreviewKeyboardNav()
                        announcePreviewThumbnail()
                    }
                }
            }
            event.accepted = true
            break
        case Qt.Key_Menu:
            if (hoveredIndex >= 0) {
                DockContextMenu.showForTask(hoveredIndex)
            }
            event.accepted = true
            break
        }
    }

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
    function computeDropIndex(globalMousePos) {
        if (DockView.isVertical) {
            let panelRelY = globalMousePos - dockPanel.y
            let items = []
            for (let i = 0; i < dockRepeater.count; i++) {
                let item = dockRepeater.itemAt(i)
                if (!item) continue
                items.push({ idx: i, cx: item.y + item.height / 2 + dockRow.y })
            }
            if (items.length === 0) return -1
            let bestIdx = items[0].idx
            let bestDist = Math.abs(panelRelY - items[0].cx)
            for (let j = 1; j < items.length; j++) {
                let d = Math.abs(panelRelY - items[j].cx)
                if (d < bestDist) { bestDist = d; bestIdx = items[j].idx }
            }
            return bestIdx
        } else {
            let panelRelX = globalMousePos - dockPanel.x
            let items = []
            for (let i = 0; i < dockRepeater.count; i++) {
                let item = dockRepeater.itemAt(i)
                if (!item) continue
                items.push({ idx: i, cx: item.x + item.width / 2 + dockRow.x })
            }
            if (items.length === 0) return -1
            let bestIdx = items[0].idx
            let bestDist = Math.abs(panelRelX - items[0].cx)
            for (let j = 1; j < items.length; j++) {
                let d = Math.abs(panelRelX - items[j].cx)
                if (d < bestDist) { bestDist = d; bestIdx = items[j].idx }
            }
            return bestIdx
        }
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

    function _tryAutoPreview() {
        if (!DockSettings.previewEnabled) return
        if (hoveredIndex < 0 || PreviewController.visible) return
        let idx = DockModel.tasksModel.index(hoveredIndex, 0)
        let isWindow = DockModel.tasksModel.data(
            idx, TaskManager.AbstractTasksModel.IsWindow)
        if (isWindow) {
            tooltipItem.show = false
            tooltipTimer.stop()
            let item = dockRepeater.itemAt(hoveredIndex)
            if (item) showPreviewForItem(hoveredIndex, item)
        }
    }

    // Show the preview popup centred on the item's VISUAL (zoomed, pushed)
    // centre; mapToGlobal includes the item's transforms. The extent passed is
    // the base icon extent so the popup anchors symmetrically around the icon.
    function showPreviewForItem(index, item) {
        let centre = item.mapToGlobal(item.width / 2, item.height / 2)
        let ext = DockView.isVertical ? item.height : item.width
        let pos = (DockView.isVertical ? centre.y : centre.x) - ext / 2
        PreviewController.showPreview(index, pos, ext)
    }

    // Scaled extent of an item on the SECONDARY axis, in dockPanel coords.
    // dockPanel.mouseY is the secondary-axis cursor (y for horizontal docks,
    // x for vertical docks). Icons grow away from the screen edge — the Scale
    // transform origin pins the edge-facing side (DockItem.qml):
    // Bottom: bottom fixed, grows up; Top: top fixed, grows down;
    // Left: left fixed, grows right; Right: right fixed, grows left.
    // Returns {near, far} in ascending secondary-axis coordinates.
    function _secondarySpan(item, scale) {
        let restNear, extent
        if (DockView.isVertical) {
            restNear = dockRow.x + item.x
            extent = item.width
        } else {
            restNear = dockRow.y + item.y
            extent = item.height
        }
        // Edge 1 = Bottom, 3 = Right: far side pinned to the screen edge.
        let farPinned = DockView.edge === 1 || DockView.edge === 3
        if (farPinned) {
            let far = restNear + extent
            return { near: far - extent * scale, far: far }
        }
        return { near: restNear, far: restNear + extent * scale }
    }

    function updateHoveredItem() {
        if (dockPanel.mouseX < 0) {
            hoveredIndex = -1
            hoveredName = ""
            _zoomActive = false
            return
        }
        // Hit-test only a pointer the dock has. After the pointer left for the
        // preview, onExited keeps mouseX/mouseY as the zoom anchor; icons that
        // move under that stale position (re-centring for a new task row) must
        // not re-hover an item, or its preview reopens with the pointer away.
        if (!dockMouseArea.containsMouse)
            return

        // Rough secondary-axis check: outside the dockRow + zoom extension on
        // the side icons grow toward (away from the screen edge) → reset zoom.
        let maxExt = DockSettings.iconSize * (DockSettings.maxZoomFactor - 1.0)
        let rowNear, rowFar
        if (DockView.isVertical) {
            rowNear = dockRow.x
            rowFar = dockRow.x + dockRow.width
        } else {
            rowNear = dockRow.y
            rowFar = dockRow.y + dockRow.height
        }
        // Edge 1 = Bottom, 3 = Right: far side pinned; icons grow toward "near".
        // Edge 0 = Top, 2 = Left: near side pinned; icons grow toward "far".
        if (DockView.edge === 0 || DockView.edge === 2) {
            rowFar += maxExt
        } else {
            rowNear -= maxExt
        }
        if (dockPanel.mouseY < rowNear || dockPanel.mouseY > rowFar) {
            hoveredIndex = -1
            hoveredName = ""
            _zoomActive = false
            return
        }

        if (dockPanel.zoomStyle !== 1) {
            // Parabolic: the item whose VISIBLE slot contains the cursor on the
            // primary axis — rest centre + current offset, current scale — so
            // tooltip and click follow what is on screen, including while
            // zoomAmount eases in or out.
            let slot = -1
            let bestDist = Infinity
            for (let i = 0; i < dockRepeater.count; i++) {
                let it = dockRepeater.itemAt(i)
                if (!it) continue
                let dist = Math.abs(dockPanel.mouseX - (it.itemCenterX + it.currentOffset))
                let half = DockSettings.iconSize * it.currentScale / 2 + DockSettings.iconSpacing / 2
                if (dist <= half && dist < bestDist) { slot = i; bestDist = dist }
            }
            let slotItem = slot >= 0 ? dockRepeater.itemAt(slot) : null
            let hit = false
            if (slotItem) {
                // Secondary axis: scaled extent honours the edge-pinned Scale origin
                let span = _secondarySpan(slotItem, slotItem.currentScale)
                hit = dockPanel.mouseY >= span.near && dockPanel.mouseY <= span.far
            }
            if (hit) {
                _zoomActive = true  // Activate zoom; stays until mouse leaves panel
                if (hoveredIndex !== slot) {
                    hoveredIndex = slot
                    hoveredName = slotItem.displayName
                    tooltipTimer.restart()
                }
            } else {
                // _zoomActive kept, same hysteresis as in-place mode below.
                hoveredIndex = -1
                hoveredName = ""
            }
            return
        }

        // In-place mode: find the closest icon whose SCALED 2D bounds contain the mouse.
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

            // Primary axis: normalized distance (0 = center, 1 = edge of scaled
            // icon). Primary extent is item.width (horizontal) / item.height
            // (vertical); itemCenterX is already axis-aware.
            let dist = Math.abs(dockPanel.mouseX - item.itemCenterX)
            let scaledHalfWidth = ((DockView.isVertical ? item.height : item.width)
                                   * item.currentScale) / 2
            let normDist = dist / scaledHalfWidth

            // Hysteresis: currently-hovered icon uses wider exit threshold
            let maxNorm = (i === hoveredIndex) ? (1.0 + hysteresisFactor) : 1.0
            if (normDist >= maxNorm) continue

            // Secondary-axis check: scaled extent honours the edge-pinned
            // Scale origin (grows away from the screen edge)
            let span = _secondarySpan(item, item.currentScale)
            if (dockPanel.mouseY < span.near || dockPanel.mouseY > span.far) continue

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
            // _zoomActive intentionally NOT reset here for primary-axis gap hysteresis.
            // When mouse crosses tiny gaps between icons, zoom stays active to prevent
            // flickering. Zoom deactivates only when mouse leaves the depth zone
            // (rough check above) or the panel zone entirely (mouseX becomes -1).
        }
    }

    MouseArea {
        id: dockMouseArea
        Accessible.ignored: true
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton

        // Track hover state for visibility controller
        onEntered: DockVisibility.setHovered(true)
        onExited: {
            // Keep zoom state when preview is visible (dock→preview mouse transition)
            if (!PreviewController.visible) {
                dockPanel.mouseX = -1
                dockPanel.mouseY = -1
                root._zoomActive = false
            }
            root.hoveredIndex = -1
            root.hoveredName = ""
            // Hide preview with delay (allows mouse to move to preview surface)
            PreviewController.hidePreviewDelayed()
            // Drag-out: if an active drag leaves the dock, unpin the launcher
            if (root._dragActive) {
                if (DockModel.isPinned(root._dragSourceIndex)) {
                    DockActions.removeLauncher(root._dragSourceIndex)
                }
                root._dragActive = false
                root._dragPending = false
                root._dragWasActive = false
                root._dragSourceIndex = -1
                root._dragTargetIndex = -1
                DockVisibility.setInteracting(false)
            } else if (root._dragPending) {
                root._dragPending = false
                root._dragWasActive = false
                root._dragSourceIndex = -1
                root._dragTargetIndex = -1
                dragHoldTimer.stop()
            }
            // preview visible이면 dock hover 상태 유지 (입력 영역 축소 방지)
            if (!PreviewController.visible) {
                DockVisibility.setHovered(false)
            }
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
                    let item = dockRepeater.itemAt(root._dragSourceIndex)
                    let name = item ? item.displayName : ""
                    DockActions.moveTask(root._dragSourceIndex, root._dragTargetIndex)
                    Accessible.announce(
                        i18n("Moved %1 to position %2", name, root._dragTargetIndex + 1),
                        Accessible.Polite)
                }
                // Reset drag state
                root._dragActive = false
                root._dragPending = false
                root._dragSourceIndex = -1
                root._dragTargetIndex = -1
                DockVisibility.setInteracting(false)
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
                DockActions.activate(root.hoveredIndex)
            } else if (mouse.button === Qt.MiddleButton) {
                DockActions.newInstance(root.hoveredIndex)
            } else if (mouse.button === Qt.RightButton) {
                DockContextMenu.showForTask(root.hoveredIndex)
            }
        }

        // Mouse wheel: cycle through child windows of the hovered app
        onWheel: function(wheel) {
            if (root.hoveredIndex < 0) return
            if (wheel.angleDelta.y > 0) {
                DockActions.cycleWindows(root.hoveredIndex, false)
            } else if (wheel.angleDelta.y < 0) {
                DockActions.cycleWindows(root.hoveredIndex, true)
            }
        }

        // Track mouse position for parabolic zoom + drag handling
        onPositionChanged: function(mouse) {
            // Mouse movement cancels keyboard navigation mode
            if (root.keyboardNavigating) {
                root.keyboardNavigating = false
                DockVisibility.setKeyboardActive(false)
            }

            // --- Drag handling ---
            if (root._dragPending && !root._dragActive) {
                let dx = mouse.x - root._dragStartX
                let dy = mouse.y - root._dragStartY
                if (Math.sqrt(dx * dx + dy * dy) > root._dragThreshold) {
                    root._dragActive = true
                    root._dragWasActive = true
                    DockVisibility.setInteracting(true)  // Prevent dock hide during drag
                    tooltipItem.show = false
                    tooltipTimer.stop()
                }
            }

            if (root._dragActive) {
                root._dragCurrentX = mouse.x
                root._dragCurrentY = mouse.y
                root._dragTargetIndex = computeDropIndex(DockView.isVertical ? mouse.y : mouse.x)
                return  // Skip normal zoom handling during drag
            }

            // --- Normal zoom tracking ---
            // Remap mouse coordinates: mouseX = primary axis (along dock),
            // mouseY = secondary axis (depth). This lets all zoom/hover logic
            // work identically regardless of orientation.
            let zoomExtension = DockSettings.iconSize * (DockSettings.maxZoomFactor - 1.0)

            if (DockView.isVertical) {
                // Vertical: primary = Y screen axis, secondary = X screen axis
                let panelNear = dockPanel.x
                let panelFar = dockPanel.x + dockPanel.width
                let inZone = (DockView.edge === 2)
                    ? (mouse.x >= panelNear && mouse.x <= panelFar + zoomExtension)    // Left
                    : (mouse.x >= panelNear - zoomExtension && mouse.x <= panelFar)    // Right
                if (inZone) {
                    dockPanel.mouseX = mouse.y - dockPanel.y   // primary = Y
                    dockPanel.mouseY = mouse.x - dockPanel.x   // secondary = X
                } else {
                    dockPanel.mouseX = -1
                    dockPanel.mouseY = -1
                }
            } else {
                // Horizontal: primary = X screen axis, secondary = Y screen axis
                let panelTop = dockPanel.y
                let panelBottom = dockPanel.y + dockPanel.height
                let inZone = (DockView.edge === 0)
                    ? (mouse.y >= panelTop && mouse.y <= panelBottom + zoomExtension)   // Top
                    : (mouse.y >= panelTop - zoomExtension && mouse.y <= panelBottom)   // Bottom
                if (inZone) {
                    dockPanel.mouseX = mouse.x - dockPanel.x
                    dockPanel.mouseY = mouse.y - dockPanel.y
                } else {
                    dockPanel.mouseX = -1
                    dockPanel.mouseY = -1
                }
            }
            root.updateHoveredItem()
        }
    }

    // Projective SDF drop shadow via ShaderEffect.
    // Each pixel projects a ray from the light source through the ground plane
    // to determine shadow intensity — no blur/offset needed.
    // Shadow renders within available surface space; overflow clips naturally at screen edges.
    ShaderEffect {
        id: dockShadow
        visible: DockSettings.shadowEnabled
        z: dockPanel.z - 1
        Accessible.ignored: true

        // Shader uniforms (names must match outer_shadow.frag UBO fields)
        property real panelWidth: dockBackground.width
        property real panelHeight: dockBackground.height
        property real cornerRadius: dockBackground.radius
        property real elevation: DockSettings.shadowElevation
        property real lightX: DockSettings.shadowLightX
        property real lightY: DockSettings.shadowLightY
        property real lightZ: DockSettings.shadowLightZ
        property real lightRadius: DockSettings.shadowLightRadius
        property color _shadowColor: DockSettings.shadowColor
        property real shadowR: _shadowColor.r
        property real shadowG: _shadowColor.g
        property real shadowB: _shadowColor.b
        property real shadowA: DockSettings.shadowIntensity
        property real margin: _margin

        // Compute shadow margin: how far the shadow can extend beyond the panel
        // Takes the larger of physical projection margin and Gaussian 3-sigma spread
        property real _margin: {
            let denom = Math.max(lightZ - elevation, 1)
            let lightDist = Math.sqrt(lightX * lightX + lightY * lightY)
            let physicalMargin = (lightDist + lightRadius) * elevation / denom
            // Gaussian decays to ~0.1% at 3*sigma
            let softnessMargin = lightRadius * 3.0
            return Math.min(Math.max(physicalMargin, softnessMargin) + 10, 200)
        }

        // Centered on the visible background, expanded by margin on each side
        x: dockPanel.x + dockBackground.x - _margin
        y: dockPanel.y + dockBackground.y - _margin
        width: dockBackground.width + _margin * 2
        height: dockBackground.height + _margin * 2

        fragmentShader: "qrc:/qml/shaders/outer_shadow.frag.qsb"
    }

    // The dock panel: rest-size container (positioned per edge, fits content).
    // The visible background is dockBackground, which also covers zoom growth.
    Rectangle {
        id: dockPanel

        // Panel size: primary axis stretches to content, secondary axis = icon + padding
        width: DockView.isVertical
            ? (DockSettings.iconSize + Kirigami.Units.largeSpacing * 2)
            : Math.max(dockRow.implicitWidth + Kirigami.Units.largeSpacing * 2, Kirigami.Units.gridUnit * 6)
        height: DockView.isVertical
            ? Math.max(dockRow.implicitHeight + Kirigami.Units.largeSpacing * 2, Kirigami.Units.gridUnit * 6)
            : (DockSettings.iconSize + Kirigami.Units.largeSpacing * 2)
        color: "transparent"

        // Position: center on the non-edge axis, slide on the edge axis
        x: DockView.isVertical ? _panelEdgePos : (parent.width - width) / 2
        y: DockView.isVertical ? (parent.height - height) / 2 : _panelEdgePos

        property real _panelEdgePos: {
            let fp = DockView.floatingPadding
            let sp = Kirigami.Units.largeSpacing
            switch (DockView.edge) {
            case 0: // Top
                return DockVisibility.dockVisible ? fp : -height - sp
            case 1: // Bottom
                return DockVisibility.dockVisible ? parent.height - height - fp : parent.height + sp
            case 2: // Left
                return DockVisibility.dockVisible ? fp : -width - sp
            case 3: // Right
                return DockVisibility.dockVisible ? parent.width - width - fp : parent.width + sp
            }
            return 0
        }

        // Visible background. Rests on the panel extent and grows by the
        // zoom layout's edge-clamped growth on each side (Parabolic only;
        // InPlace never grows it). Tracks the layout directly: the growth is
        // already smoothed by dockPanel.zoomAmount.
        Rectangle {
            id: dockBackground
            z: -1
            readonly property real leadingGrowth: dockPanel.zoomLayout.leadingGrowth ?? 0
            readonly property real trailingGrowth: dockPanel.zoomLayout.trailingGrowth ?? 0

            x: DockView.isVertical ? 0 : -leadingGrowth
            y: DockView.isVertical ? -leadingGrowth : 0
            width: parent.width + (DockView.isVertical ? 0 : leadingGrowth + trailingGrowth)
            height: parent.height + (DockView.isVertical ? leadingGrowth + trailingGrowth : 0)
            radius: DockSettings.cornerRadius
            color: DockView.backgroundStyleType === 3
                   ? "transparent"
                   : DockView.backgroundColor

            onXChanged: dockPanel.reportPanelRect()
            onYChanged: dockPanel.reportPanelRect()
            onWidthChanged: dockPanel.reportPanelRect()
            onHeightChanged: dockPanel.reportPanelRect()

            // Acrylic overlay: tint + noise via GPU shader, composited over KWin blur.
            // Shader handles rounded corners via SDF mask — no clip wrapper needed.
            ShaderEffect {
                anchors.fill: parent
                visible: DockView.backgroundStyleType === 3
                property real tintR: DockView.backgroundColor.r
                property real tintG: DockView.backgroundColor.g
                property real tintB: DockView.backgroundColor.b
                property real tintOpacity: DockView.backgroundColor.a
                property real noiseStrength: 0.02
                property real resX: width
                property real resY: height
                property real cornerRadius: dockBackground.radius
                fragmentShader: "qrc:/qml/shaders/acrylic_overlay.frag.qsb"
            }
        }

        // Delay enabling animations until after initial layout to avoid startup flicker
        property bool animationsReady: false
        Component.onCompleted: {
            // Guaranteed initial panel-rect report; coalesces with any already-queued call.
            reportPanelRect()
            Qt.callLater(function() { animationsReady = true })
        }

        Behavior on width {
            enabled: dockPanel.animationsReady
            NumberAnimation {
                duration: Kirigami.Units.longDuration
                easing.type: Easing.InOutQuad
            }
        }

        Behavior on height {
            enabled: dockPanel.animationsReady && DockView.isVertical
            NumberAnimation {
                duration: Kirigami.Units.longDuration
                easing.type: Easing.InOutQuad
            }
        }

        Behavior on x {
            enabled: dockPanel.animationsReady && DockView.isVertical
            NumberAnimation {
                duration: Kirigami.Units.longDuration
                easing.type: Easing.InOutQuad
            }
        }

        Behavior on y {
            enabled: dockPanel.animationsReady && !DockView.isVertical
            NumberAnimation {
                duration: Kirigami.Units.longDuration
                easing.type: Easing.InOutQuad
            }
        }

        // Fade animation
        opacity: DockVisibility.dockVisible ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: Kirigami.Units.longDuration }
        }

        // Mouse position relative to the panel, -1 when outside
        property real mouseX: -1
        property real mouseY: -1
        // Zoom activates when mouse hits an icon (_zoomActive=true) and stays
        // active until mouse leaves the panel zone (mouseX=-1). This hysteresis
        // prevents zoom flickering when crossing tiny gaps between icons.
        // Zoom is disabled during drag so all icons return to base scale.
        property bool mouseInside: mouseX >= 0 && root._zoomActive && !root._dragActive

        // --- Hover zoom ---
        // ZoomStyle (krema.kcfg): 0 = Parabolic (Gaussian magnification;
        // neighbours move aside and the background grows), 1 = InPlace (icons
        // scale over their neighbours, nothing moves).
        readonly property int zoomStyle: DockSettings.zoomStyle

        // Last valid primary-axis cursor. Kept when mouseX becomes -1 so the
        // zoom-out animation collapses around the point the cursor left from.
        property real zoomCursor: 0
        onMouseXChanged: if (mouseX >= 0) zoomCursor = mouseX
        Behavior on zoomCursor {
            enabled: root.keyboardNavigating
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.OutCubic
            }
        }

        // Global zoom amount (0 = rest, 1 = full zoom). For Parabolic this is
        // the only animated zoom quantity: it eases in when the pointer enters
        // and out when it leaves, and icons/background track the layout
        // directly. InPlace smooths per-item scales instead.
        property real zoomAmount: mouseInside ? 1.0 : 0.0
        Behavior on zoomAmount {
            enabled: dockPanel.zoomStyle !== 1
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.OutCubic
            }
        }
        // A drag snaps icons back to rest at once (drop targeting and the drop
        // indicator use rest coordinates), whatever the animation is doing.
        readonly property real _layoutZoomAmount: root._dragActive ? 0.0 : zoomAmount

        // Scales/offsets/growth computed from REST geometry only (dockRow
        // position and settings), so zoom never feeds back into layout.
        // Parabolic output is a direct function of the cursor and zoomAmount.
        readonly property real _restStart: DockView.isVertical ? dockRow.y : dockRow.x
        // Surface bounds in this panel's primary-axis frame: the grown dock
        // background may not cross them.
        readonly property real _minZoomEdge: DockView.isVertical ? -y : -x
        readonly property real _maxZoomEdge: DockView.isVertical
            ? root.height - y
            : root.width - x
        readonly property var zoomLayout: DockView.zoomLayout(
            dockRepeater.count, _restStart,
            DockSettings.iconSize, DockSettings.iconSpacing,
            0, DockView.isVertical ? height : width,
            zoomStyle === 1
                ? DockSettings.maxZoomFactor
                : 1.0 + (DockSettings.maxZoomFactor - 1.0) * _layoutZoomAmount,
            zoomStyle,
            zoomStyle === 1 ? mouseInside : _layoutZoomAmount > 0,
            zoomCursor, _minZoomEdge, _maxZoomEdge)
        // Parabolic icons keep moving under a still pointer (zoom-in/out via
        // zoomAmount, edge clamping), so the icon under it can change without a
        // mouse move: re-run the hit test, coalesced to once per event-loop turn.
        function scheduleHoverUpdate() {
            if (zoomStyle !== 1 && mouseX >= 0 && !root._dragActive && !root.keyboardNavigating)
                Qt.callLater(root.updateHoveredItem)
        }

        // Report the visible background rect (surface frame) for input region + blur.
        // Deferred via Qt.callLater: multiple geometry signals fire per pointer
        // motion in push mode, and callLater coalesces them into a single call
        // per event-loop turn, avoiding Wayland input-region + KWin blur-region
        // rebuild churn.
        function reportPanelRect() {
            Qt.callLater(dockPanel._reportPanelRectNow)
        }
        function _reportPanelRectNow() {
            // Outward-round to integers: the C++ side truncates each component,
            // so floor/ceil avoids losing up to ~2px off the trailing edge.
            // Clamp the primary axis to the surface: the edge-clamped zoom layout can
            // overshoot by float error. The secondary axis is left alone because the
            // hide/show slide legitimately moves the panel past the surface edge.
            let left = Math.floor(x + dockBackground.x)
            let top = Math.floor(y + dockBackground.y)
            let right = Math.ceil(x + dockBackground.x + dockBackground.width)
            let bottom = Math.ceil(y + dockBackground.y + dockBackground.height)
            if (DockView.isVertical) {
                top = Math.max(0, top)
                bottom = Math.min(root.height, bottom)
            } else {
                left = Math.max(0, left)
                right = Math.min(root.width, right)
            }
            DockVisibility.setPanelRect(left, top, right - left, bottom - top)
        }
        onXChanged: reportPanelRect()
        onYChanged: reportPanelRect()
        onWidthChanged: reportPanelRect()
        onHeightChanged: reportPanelRect()

        // Main icon layout (Flow switches between horizontal/vertical)
        Flow {
            id: dockRow
            flow: DockView.isVertical ? Flow.TopToBottom : Flow.LeftToRight
            spacing: DockSettings.iconSpacing

            // Animate content extent to sync centering with panel size animation.
            property real animatedContentWidth: implicitWidth
            property real animatedContentHeight: implicitHeight
            Behavior on animatedContentWidth {
                enabled: dockPanel.animationsReady && !DockView.isVertical
                NumberAnimation {
                    duration: Kirigami.Units.longDuration
                    easing.type: Easing.InOutQuad
                }
            }
            Behavior on animatedContentHeight {
                enabled: dockPanel.animationsReady && DockView.isVertical
                NumberAnimation {
                    duration: Kirigami.Units.longDuration
                    easing.type: Easing.InOutQuad
                }
            }
            x: DockView.isVertical
                ? (parent.width - animatedContentWidth) / 2
                : (parent.width - animatedContentWidth) / 2
            y: DockView.isVertical
                ? (parent.height - animatedContentHeight) / 2
                : (parent.height - animatedContentHeight) / 2

            // Animate existing items displaced by add/remove within the Flow.
            // Disabled during hover zoom (mouseInside) to avoid lagging sibling
            // repositioning — zoom needs immediate response.
            move: Transition {
                enabled: dockPanel.animationsReady && !dockPanel.mouseInside
                NumberAnimation {
                    properties: "x,y"
                    duration: Kirigami.Units.longDuration
                    easing.type: Easing.InOutQuad
                }
            }

            Repeater {
                id: dockRepeater
                model: DockModel.tasksModel

                DockItem {
                    // index and model are injected by Repeater into
                    // DockItem's own required properties

                    z: (root.hoveredIndex === index) ? 1 : 0
                    isKeyboardFocused: root.keyboardNavigating && root.hoveredIndex === index
                    iconSize: DockSettings.iconSize
                    maxZoomFactor: DockSettings.maxZoomFactor
                    spacing: DockSettings.iconSpacing
                    zoomScale: dockPanel.zoomLayout.scales?.[index] ?? 1.0
                    zoomOffset: dockPanel.zoomLayout.offsets?.[index] ?? 0.0
                    zoomStyle: dockPanel.zoomStyle
                    onCurrentOffsetChanged: dockPanel.scheduleHoverUpdate()
                    onCurrentScaleChanged: dockPanel.scheduleHoverUpdate()

                    // Compute this item's rest center on the primary axis relative to the panel.
                    // For vertical docks, the primary axis is Y (remapped to mouseX).
                    itemCenterX: DockView.isVertical
                        ? (y + height / 2 + dockRow.y)
                        : (x + width / 2 + dockRow.x)

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
                let isLauncher = DockModel.isDesktopFile(firstUrl)

                if (isLauncher) {
                    // .desktop file → add as pinned launcher
                    DockActions.addLauncher(firstUrl)
                } else if (dropTargetIndex >= 0) {
                    // Regular file(s) on an app icon → open with that app
                    DockActions.openUrlsWithTask(dropTargetIndex, urls)
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
        Accessible.ignored: true
        visible: root._dragActive && root._dragSourceIndex >= 0
        width: DockSettings.iconSize
        height: DockSettings.iconSize
        source: {
            if (!visible) return ""
            let name = DockModel.iconName(root._dragSourceIndex)
            return (name && name.length > 0) ? "image://icon/" + name + "?v=" + DockView.iconCacheVersion : ""
        }
        sourceSize: Qt.size(DockSettings.iconSize, DockSettings.iconSize)
        x: root._dragCurrentX - width / 2
        y: root._dragCurrentY - height / 2
        opacity: 0.8
        z: 200
    }

    // Drop position indicator line (shown during internal reorder drag)
    Rectangle {
        id: dropIndicator
        Accessible.ignored: true
        visible: root._dragActive && root._dragTargetIndex >= 0
                 && root._dragTargetIndex !== root._dragSourceIndex
        width: DockView.isVertical ? DockSettings.iconSize : 2
        height: DockView.isVertical ? 2 : DockSettings.iconSize
        color: Kirigami.Theme.highlightColor
        radius: 1
        z: 150

        x: {
            if (!visible || root._dragTargetIndex < 0) return 0
            if (DockView.isVertical) return dockPanel.x + dockRow.x
            let targetItem = dockRepeater.itemAt(root._dragTargetIndex)
            if (!targetItem) return 0
            let itemX = dockPanel.x + dockRow.x + targetItem.x
            if (root._dragTargetIndex > root._dragSourceIndex) {
                return itemX + targetItem.width + DockSettings.iconSpacing / 2 - 1
            } else {
                return itemX - DockSettings.iconSpacing / 2 - 1
            }
        }
        y: {
            if (!visible || root._dragTargetIndex < 0) return 0
            if (!DockView.isVertical) return dockPanel.y + dockRow.y
            let targetItem = dockRepeater.itemAt(root._dragTargetIndex)
            if (!targetItem) return 0
            let itemY = dockPanel.y + dockRow.y + targetItem.y
            if (root._dragTargetIndex > root._dragSourceIndex) {
                return itemY + targetItem.height + DockSettings.iconSpacing / 2 - 1
            } else {
                return itemY - DockSettings.iconSpacing / 2 - 1
            }
        }
    }

    // Handle launch bounce trigger from C++ signal.
    // Only sets manualLaunching for already-running apps (IsWindow): their
    // delegate stays alive, so manualLaunching persists through the bounce.
    // For launchers (first launch), we skip manualLaunching entirely:
    // IsStartup fires within ~5ms and, being model data, survives the
    // delegate recreation caused by hideActivatedLaunchers.
    Connections {
        target: DockActions
        function onTaskLaunching(index) {
            let item = dockRepeater.itemAt(index)
            if (!item) return

            // Announce launch to screen reader (must call on root Item, not Connections)
            root.announceLaunch(item.displayName)

            // Skip for launcher items — IsStartup will drive the bounce.
            if (!item.model.IsWindow) return

            item.manualLaunching = true
        }
    }

    // Auto-trigger preview when a hovered launcher becomes a window.
    // Polls only while the text tooltip is visible (launcher hover state).
    // When IsWindow becomes true → switches from text tooltip to preview popup.
    Timer {
        id: autoPreviewTimer
        interval: 200
        repeat: true
        running: tooltipItem.visible && !PreviewController.visible
        onTriggered: root._tryAutoPreview()
    }

    // Sync dock hover state when preview closes:
    // If preview was keeping dock hovered and mouse is no longer on dock,
    // release hover so dock can hide.
    Connections {
        target: PreviewController
        function onVisibleChanged() {
            if (!PreviewController.visible && !dockMouseArea.containsMouse) {
                // Preview closed and mouse not on dock → release zoom smoothly
                dockPanel.mouseX = -1
                dockPanel.mouseY = -1
                root._zoomActive = false
                DockVisibility.setHovered(false)
            }
        }
    }

    // Custom tooltip / preview trigger timer.
    // For window tasks: shows the preview popup (separate layer-shell surface).
    // For launcher-only tasks: shows the in-scene text tooltip.
    Timer {
        id: tooltipTimer
        interval: DockSettings.previewHoverDelay
        onTriggered: {
            if (root.hoveredIndex < 0) return
            let idx = DockModel.tasksModel.index(root.hoveredIndex, 0)
            let isWindow = DockModel.tasksModel.data(
                idx, TaskManager.AbstractTasksModel.IsWindow)
            if (isWindow && DockSettings.previewEnabled) {
                // Window task → show preview popup
                let item = dockRepeater.itemAt(root.hoveredIndex)
                if (item) root.showPreviewForItem(root.hoveredIndex, item)
            } else {
                // Launcher-only → show text tooltip
                tooltipItem.show = true
            }
        }
    }

    // Vertical docks open the tooltip beside the panel, inside a fixed surface
    // reserve. Publish that reserve (gap + max tooltip width) so DockView sizes
    // the surface to fit; the input region stays on the panel.
    Binding {
        target: DockView
        property: "sideTooltipReserve"
        value: Math.ceil(Kirigami.Units.largeSpacing + tooltipItem.maxSideWidth)
    }

    Rectangle {
        id: tooltipItem
        objectName: "dockTooltip"
        Accessible.ignored: true
        property bool show: false
        visible: show && root.hoveredName.length > 0

        // Reset when hover changes
        onVisibleChanged: if (!visible) show = false

        // Longer names elide on vertical docks instead of clipping at the surface edge
        readonly property real maxSideWidth: Kirigami.Units.gridUnit * 15

        // Position on the opposite side of the dock edge
        x: {
            if (root.hoveredIndex < 0 || root.hoveredIndex >= dockRepeater.count)
                return 0
            let item = dockRepeater.itemAt(root.hoveredIndex)
            if (!item) return 0
            let sp = Kirigami.Units.largeSpacing
            if (DockView.edge === 2) return dockPanel.x + dockPanel.width + sp  // Left → right
            if (DockView.edge === 3) return dockPanel.x - width - sp            // Right → left
            // Visual centre: rest centre plus the animated zoom offset (Scale keeps the centre)
            return dockPanel.x + dockRow.x + item.x + item.width / 2 + item.currentOffset - width / 2
        }
        y: {
            if (root.hoveredIndex < 0 || root.hoveredIndex >= dockRepeater.count)
                return 0
            let item = dockRepeater.itemAt(root.hoveredIndex)
            if (!item) return 0
            let sp = Kirigami.Units.largeSpacing
            if (DockView.edge === 0) return dockPanel.y + dockPanel.height + sp  // Top → below
            if (DockView.edge === 1) return dockPanel.y - height - sp            // Bottom → above
            return dockPanel.y + dockRow.y + item.y + item.height / 2 + item.currentOffset - height / 2
        }

        Kirigami.Theme.colorSet: Kirigami.Theme.Tooltip
        Kirigami.Theme.inherit: false

        width: tooltipLabel.width + Kirigami.Units.largeSpacing * 2
        height: tooltipLabel.implicitHeight + Kirigami.Units.largeSpacing
        radius: Kirigami.Units.smallSpacing
        color: Kirigami.Theme.backgroundColor
        z: 100

        QQC2.Label {
            id: tooltipLabel
            anchors.centerIn: parent
            width: DockView.isVertical
                ? Math.min(implicitWidth, tooltipItem.maxSideWidth - Kirigami.Units.largeSpacing * 2)
                : implicitWidth
            elide: Text.ElideRight
            text: root.hoveredName
            Accessible.ignored: true
        }

        // Hide tooltip and manage preview when hover changes
        Connections {
            target: root
            function onHoveredIndexChanged() {
                tooltipItem.show = false
                tooltipTimer.stop()
                if (root.hoveredIndex < 0) {
                    // Mouse left dock: start delayed preview hide
                    PreviewController.hidePreviewDelayed()
                } else if (!root.keyboardNavigating) {
                    // Moved to different icon (mouse): restart tooltip timer,
                    // hide preview with delay (allows moving to adjacent icon)
                    PreviewController.hidePreviewDelayed()
                    tooltipTimer.restart()
                }
                // In keyboard mode: don't auto-trigger tooltip/preview
            }
        }
    }

    // Auto-trigger preview when a hovered launcher's window appears.
    // Reacts to TasksModel row insertion — more responsive than polling.
    // _tryAutoPreview() honours the "Enable window preview" setting.
    Connections {
        target: DockModel.tasksModel
        function onRowsInserted() {
            root._tryAutoPreview()
        }
    }

}
