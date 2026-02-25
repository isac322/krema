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

        // Reuse zoom logic: set panelMouseX to the focused item's center
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
            switch (event.key) {
            case Qt.Key_Left:
                PreviewController.navigatePreviewThumbnail(-1)
                announcePreviewThumbnail()
                event.accepted = true
                break
            case Qt.Key_Right:
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
            case Qt.Key_Up:
                // Return to dock navigation (keep preview visible)
                PreviewController.endPreviewKeyboardNav()
                event.accepted = true
                break
            }
            return
        }

        // Normal dock navigation
        switch (event.key) {
        case Qt.Key_Left:
            navigateItem(-1)
            event.accepted = true
            break
        case Qt.Key_Right:
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
        case Qt.Key_Down:
            // Open preview for the focused item (if it has windows)
            if (hoveredIndex >= 0) {
                let idx = DockModel.tasksModel.index(hoveredIndex, 0)
                let isWindow = DockModel.tasksModel.data(
                    idx, TaskManager.AbstractTasksModel.IsWindow)
                if (isWindow) {
                    let item = dockRepeater.itemAt(hoveredIndex)
                    if (item) {
                        let globalPos = item.mapToGlobal(0, 0)
                        PreviewController.showPreview(hoveredIndex, globalPos.x, item.width)
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
            if (item) {
                let globalPos = item.mapToGlobal(0, 0)
                PreviewController.showPreview(hoveredIndex, globalPos.x, item.width)
            }
        }
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
        let maxExt = DockSettings.iconSize * (DockSettings.maxZoomFactor - 1.0)
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
                root._dragTargetIndex = computeDropIndex(mouse.x)
                return  // Skip normal zoom handling during drag
            }

            // --- Normal zoom tracking ---
            // Only activate zoom when mouse is near the panel vertically.
            // Tight zone: panel area + zoom extension above (icons grow upward).
            let panelTop = dockPanel.y
            let panelBottom = dockPanel.y + dockPanel.height
            let zoomExtension = DockSettings.iconSize * (DockSettings.maxZoomFactor - 1.0)
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
        property real panelWidth: dockPanel.width
        property real panelHeight: dockPanel.height
        property real cornerRadius: dockPanel.radius
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

        // Centered on panel, expanded by margin on each side
        x: dockPanel.x - _margin
        y: dockPanel.y - _margin
        width: dockPanel.width + _margin * 2
        height: dockPanel.height + _margin * 2

        fragmentShader: "qrc:/qml/shaders/outer_shadow.frag.qsb"
    }

    // The visible dock panel (centered, fits content)
    Rectangle {
        id: dockPanel
        anchors.horizontalCenter: parent.horizontalCenter
        height: DockSettings.iconSize + Kirigami.Units.largeSpacing * 2  // icon + padding
        width: Math.max(dockRow.implicitWidth + Kirigami.Units.largeSpacing * 2, Kirigami.Units.gridUnit * 6)  // content + padding, min width
        radius: DockSettings.cornerRadius
        color: DockView.backgroundStyleType === 3
               ? "transparent"
               : DockView.backgroundColor

        // Acrylic overlay: tint + noise via GPU shader, composited over KWin blur.
        // Shader handles rounded corners via SDF mask — no clip wrapper needed.
        ShaderEffect {
            anchors.fill: parent
            z: 0
            visible: DockView.backgroundStyleType === 3
            property real tintR: DockView.backgroundColor.r
            property real tintG: DockView.backgroundColor.g
            property real tintB: DockView.backgroundColor.b
            property real tintOpacity: DockView.backgroundColor.a
            property real noiseStrength: 0.02
            property real resX: width
            property real resY: height
            property real cornerRadius: dockPanel.radius
            fragmentShader: "qrc:/qml/shaders/acrylic_overlay.frag.qsb"
        }

        // Slide animation: y position controlled by visibility.
        // floatingPadding creates a visual gap between the panel and the screen edge
        // without using layer-shell margin (which would cause surface repositioning).
        y: DockVisibility.dockVisible
           ? parent.height - height - DockView.floatingPadding
           : parent.height + Kirigami.Units.largeSpacing

        // Delay enabling animations until after initial layout to avoid startup flicker
        property bool animationsReady: false
        Component.onCompleted: Qt.callLater(function() { animationsReady = true })

        Behavior on width {
            enabled: dockPanel.animationsReady
            NumberAnimation {
                duration: Kirigami.Units.longDuration
                easing.type: Easing.InOutQuad
            }
        }

        Behavior on y {
            enabled: dockPanel.animationsReady
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

        // Report panel geometry to visibility controller for input region
        onXChanged: DockVisibility.setPanelRect(x, y, width, height)
        onWidthChanged: DockVisibility.setPanelRect(x, y, width, height)
        onYChanged: DockVisibility.setPanelRect(x, y, width, height)
        onHeightChanged: DockVisibility.setPanelRect(x, y, width, height)

        // Main icon row
        Row {
            id: dockRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: DockSettings.iconSpacing

            // Animate content width to sync Row centering with panel width animation.
            // Without this, implicitWidth changes instantly when items are added/removed,
            // causing an abrupt centering jump even though panel width animates smoothly.
            // Math proof: panel.width and animatedContentWidth change by the same delta
            // with the same easing, so x = (panel.width - animatedContentWidth) / 2
            // remains constant during animation (= padding). No jump, no flicker.
            property real animatedContentWidth: implicitWidth
            Behavior on animatedContentWidth {
                enabled: dockPanel.animationsReady
                NumberAnimation {
                    duration: Kirigami.Units.longDuration
                    easing.type: Easing.InOutQuad
                }
            }
            x: (parent.width - animatedContentWidth) / 2

            // Animate existing items displaced by add/remove within the Row.
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
                    panelMouseX: dockPanel.mouseX
                    panelMouseInside: dockPanel.mouseInside
                    spacing: DockSettings.iconSpacing

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
        width: 2
        height: DockSettings.iconSize
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
                return itemX + targetItem.width + DockSettings.iconSpacing / 2 - 1
            } else {
                return itemX - DockSettings.iconSpacing / 2 - 1
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
                if (item) {
                    let globalPos = item.mapToGlobal(0, 0)
                    PreviewController.showPreview(
                        root.hoveredIndex, globalPos.x, item.width)
                }
            } else {
                // Launcher-only → show text tooltip
                tooltipItem.show = true
            }
        }
    }

    Rectangle {
        id: tooltipItem
        Accessible.ignored: true
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
    Connections {
        target: DockModel.tasksModel
        function onRowsInserted() {
            if (root.hoveredIndex < 0) return
            if (PreviewController.visible) return
            let idx = DockModel.tasksModel.index(root.hoveredIndex, 0)
            let isWindow = DockModel.tasksModel.data(
                idx, TaskManager.AbstractTasksModel.IsWindow)
            if (isWindow) {
                tooltipItem.show = false
                let item = dockRepeater.itemAt(root.hoveredIndex)
                if (item) {
                    let globalPos = item.mapToGlobal(0, 0)
                    PreviewController.showPreview(
                        root.hoveredIndex, globalPos.x, item.width)
                }
            }
        }
    }

}
