// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.taskmanager as TaskManager
import com.bhyoo.krema 1.0
import "components"

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

    // --- DEBUG PROTOCOL (Rule 12) ---
    readonly property bool _debugAll: Qt.application.arguments.indexOf("--debug-all") !== -1
    readonly property bool _debugConfig: KremaDebug.configEnabled
    readonly property bool _debugGeom: KremaDebug.geomEnabled

    Timer {
        running: true
        interval: 3000
        onTriggered: {
            console.log("[TEST] Timer triggered! Simulating edge hover.")
            if (typeof DockVisibility !== "undefined") DockVisibility.setHovered(true)
        }
    }

    readonly property bool _debugHit: _debugAll || Qt.application.arguments.indexOf("--debug-hit") !== -1
    readonly property bool _debugZoom: _debugAll || Qt.application.arguments.indexOf("--debug-zoom") !== -1
    readonly property bool _debugNotif: _debugAll || Qt.application.arguments.indexOf("--debug-notif") !== -1

    // --- Phase 2: Tier 2 Island Alias Functions ---
    property var _currentDockRepeater: null
    property int appIconCount: _currentDockRepeater ? _currentDockRepeater.count : 0
    function getAppIcon(index) { return _currentDockRepeater ? _currentDockRepeater.itemAt(index) : null }

    // State Tracking: Monitor zoom slider changes
    Connections {
        target: DockSettings
        function onMaxZoomFactorChanged() {
        }
    }

    MouseArea {
        id: dockMouseArea
        Accessible.ignored: true
        
        // --- Thickness Lead Fix (Rule 3) ---
        // Do NOT anchor to dockPanel (parent). Fill the root to cover 
        // the entire Wayland input region. This ensures we catch the mouse
        // even if the visual panel is thin (e.g. 10px).
        anchors.fill: root

        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton

        onExited: {
            if (!PreviewController.visible) {
                dockPanel.mouseX = -1
                dockPanel.mouseY = -1
                root._zoomActive = false
            }
            PreviewController.hidePreviewDelayed()
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
            DockVisibility.setHovered(false)
        }

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
                if (root._dragTargetIndex >= 0 && root._dragTargetIndex !== root._dragSourceIndex) {
                    let sourceIsPinned = DockModel.isPinned(root._dragSourceIndex);
                    let targetIsPinned = DockModel.isPinned(root._dragTargetIndex);
                    let finalTarget = root._dragTargetIndex;
                    if (sourceIsPinned && !targetIsPinned) {
                        for (let i = appIconCount - 1; i >= 0; i--) {
                            if (DockModel.isPinned(i)) { finalTarget = i; break; }
                        }
                    } else if (!sourceIsPinned && targetIsPinned) {
                        for (let i = 0; i < appIconCount; i++) {
                            if (!DockModel.isPinned(i)) { finalTarget = i; break; }
                        }
                    }
                    if (finalTarget !== root._dragSourceIndex) {
                        let item = getAppIcon(root._dragSourceIndex);
                        let name = item ? item.displayName : "";
                        DockActions.moveTask(root._dragSourceIndex, finalTarget);
                        Accessible.announce(i18n("Moved %1 to position %2", name, finalTarget + 1), Accessible.Polite);
                    }
                }
                root._dragActive = false
                root._dragPending = false
                root._dragSourceIndex = -1
                root._dragTargetIndex = -1
                DockVisibility.setInteracting(false)
            } else {
                root._dragPending = false
            }
        }

        onClicked: function(mouse) {
            if (root._dragWasActive) {
                root._dragWasActive = false
                return
            }
            if (root.hoveredIndex < 0) return
            
            if (_debugHit) {
                KremaDebug.input("Clicked index: " + root.hoveredIndex + " button: " + mouse.button)
            }

            if (mouse.button === Qt.LeftButton) {
                DockActions.activate(root.hoveredIndex)
            } else if (mouse.button === Qt.MiddleButton) {
                DockActions.newInstance(root.hoveredIndex)
            } else if (mouse.button === Qt.RightButton) {
                DockContextMenu.showForTask(root.hoveredIndex)
            }
        }

        onWheel: function(wheel) {
            if (root.hoveredIndex < 0) return
            
            let item = getAppIcon(root.hoveredIndex)
            if (!item || !item.model || !item.model.IsWindow) return
            
            if (_debugHit) {
                KremaDebug.input("Wheel on index: " + root.hoveredIndex + " delta: " + wheel.angleDelta.y)
            }

            if (wheel.angleDelta.y > 0) {
                DockActions.cycleWindows(root.hoveredIndex, false)
            } else if (wheel.angleDelta.y < 0) {
                DockActions.cycleWindows(root.hoveredIndex, true)
            }
        }

        onPositionChanged: function(mouse) {
            if (typeof DockContextMenu !== "undefined" && DockContextMenu.visible) return

            // Hit-test diagnostic (active with --debug-hit)
            if (_debugHit) {
                KremaDebug.input("Hovering at " + mouse.x.toFixed(1) + " " + mouse.y.toFixed(1))
            }

            let isVisible = DockVisibility.dockVisible
            let triggerDepth = (typeof DockVisibility !== "undefined" && DockVisibility.hovered) ? 64 : 8

            let iconSize = DockView.screenSettings.iconSize
            let spacing = DockSettings.iconSpacing
            let slotSize = iconSize + spacing
            let totalUnscaled = (appIconCount * slotSize) - spacing
            
            let unscaledStart = DockView.isVertical 
                ? (dockPanel.y + dockRow.y) 
                : (dockPanel.x + dockRow.x)

            // --- Interaction Orbit (Rule 3 & 17) ---
            // The orbit is anchored to the visual center of the icons, 
            // ensuring hit-testing matches visual pixels exactly.
            let sample = getAppIcon(0)
            let floorUnits = sample ? sample._unitIconBaseOffset : 0
            let unitCenter = floorUnits + (iconSize / 2)
            
            let secondaryAxisCenter = 0
            if (DockView.isVertical) {
                secondaryAxisCenter = (DockView.edge === 2) 
                    ? dockPanel.x + unitCenter 
                    : (dockPanel.x + dockPanel.width - unitCenter)
            } else {
                secondaryAxisCenter = (DockView.edge === 0) 
                    ? dockPanel.y + unitCenter 
                    : (dockPanel.y + dockPanel.height - unitCenter)
            }

            // Dual-Orbit Hysteresis: prevents flickering between unzoomed/zoomed states
            let enterOrbit = (iconSize * 0.5) + 5
            let exitOrbit = enterOrbit // fallback
            if (sample) {
                let visualRadius = (dockRow._maxIconThickness * 0.5) + sample._unitIconBaseOffset
                exitOrbit = visualRadius + 10
            }

            let currentOrbit = root._zoomActive ? exitOrbit : enterOrbit
            let secondaryAxisDist = Math.abs((DockView.isVertical ? mouse.x : mouse.y) - secondaryAxisCenter)
            let isInside = isVisible && (secondaryAxisDist <= currentOrbit)

            // FIX: The Ghost Hover Bug (Bug #41 Part 4)
            // Even if the dock is visible, if the mouse is touching the physical screen edge
            // (within triggerDepth), it MUST remain inside. Otherwise, the small orbit radius
            // rejects the mouse and instantly hides the dock before the user can move up.
            if (!isVisible || !isInside) {
                switch (DockView.edge) {
                    case 0: isInside = isInside || (mouse.y <= triggerDepth); break
                    case 1: isInside = isInside || (mouse.y >= root.height - triggerDepth); break
                    case 2: isInside = isInside || (mouse.x <= triggerDepth); break
                    case 3: isInside = isInside || (mouse.x >= root.width - triggerDepth); break
                }
            }

            DockVisibility.setHovered(isInside);

            if (!isInside) {
                if (!PreviewController.visible) {
                    dockPanel.mouseX = -1
                    dockPanel.mouseY = -1
                    root._zoomActive = false
                }
                root.hoveredIndex = -1
                root.hoveredName = ""
                return 
            }

            // 3. ACTIVE ZOOM SIGNAL
            root._zoomActive = true
            dockPanel.mouseX = DockView.isVertical ? mouse.y : mouse.x
            dockPanel.mouseY = DockView.isVertical ? mouse.x : mouse.y
            updateZoomFactors()

            // 4. PIXEL-PERFECT HIT-TESTING
            let hitIndex = -1;
            let mPos = DockView.isVertical ? mouse.y : mouse.x;

            for (let i = 0; i < appIconCount; i++) {
                let item = getAppIcon(i);
                if (!item) continue;

                // Primary Axis Filter: Is the mouse within the visual width of this icon?
                // By using the actual visual position (x/y) instead of an unscaled grid,
                // the hitbox perfectly follows the icon as it shifts away during parabolic zoom.
                let visualCenter = DockView.isVertical 
                    ? (dockPanel.y + dockRow.y + item.y + (item.height / 2))
                    : (dockPanel.x + dockRow.x + item.x + (item.width / 2));
                
                let zoomedSize = DockView.isVertical ? item.height : item.width;
                let slotStart = visualCenter - (zoomedSize / 2);
                let slotEnd = visualCenter + (zoomedSize / 2);

                if (mPos >= slotStart - 5 && mPos <= slotEnd + 5) {
                    let localPos = item.iconImage.mapFromItem(dockMouseArea, mouse.x, mouse.y);

                    // Secondary Axis Precision: Distance from visual icon center
                    let dx = localPos.x - (item.iconSize / 2)
                    let dy = localPos.y - (item.iconSize / 2)
                    let dist = Math.sqrt(dx*dx + dy*dy)

                    // Trigger hover ONLY if over actual icon pixels
                    if (dist <= (item.iconSize / 2)) {
                        hitIndex = i;
                        break;
                    }
                }
            }

            if (hitIndex >= 0) {
                if (root.hoveredIndex !== hitIndex) {
                    root.hoveredIndex = hitIndex;
                    root.hoveredName = getAppIcon(hitIndex).displayName;
                    tooltipTimer.restart();
                }
            } else {
                root.hoveredIndex = -1;
                root.hoveredName = "";
            }

            if (root.keyboardNavigating) {
                root.keyboardNavigating = false
                DockVisibility.setKeyboardActive(false)
            }

            if (root._dragPending && !root._dragActive) {
                let dx = mouse.x - root._dragStartX
                let dy = mouse.y - root._dragStartY
                if (Math.sqrt(dx * dx + dy * dy) > root._dragThreshold) {
                    root._dragActive = true
                    root._dragWasActive = true
                    DockVisibility.setInteracting(true) 
                    tooltipItem.show = false
                    tooltipTimer.stop()
                }
            }

            if (root._dragActive) {
                root._dragCurrentX = mouse.x
                root._dragCurrentY = mouse.y
                root._dragTargetIndex = computeDropIndex(DockView.isVertical ? mouse.y : mouse.x)
            }
        }
    }

    // --- Interaction: hoveredIndex (The ID of the icon currently "under" the cursor) ---
    property int hoveredIndex: -1
    // --- Interaction: hoveredName (The display name of the hovered icon) ---
    property string hoveredName: ""
    property bool keyboardNavigating: false
    property bool _zoomActive: false

    // Kinetic Zoom Physics (Milestone 9)
    // We animate this value to create a 'liquid' exit instead of an instant snap.
    // --- Kinetic: _zoomIntensity (Bridge for transition smoothing: 0.0 - 1.0) ---
    property real _zoomIntensity: _zoomActive ? 1.0 : 0.0
    Behavior on _zoomIntensity {
        NumberAnimation {
            duration: Kirigami.Units.longDuration
            easing.type: Easing.OutBack // Provides a subtle bounce/liquid feel
        }
    }
    on_ZoomIntensityChanged: updateZoomFactors()

    // --- Hybrid Zoom Engine ---
    // Computes parabolic zoom for all icons using their ACTUAL visual centers.
    // This guarantees max zoom when cursor is dead-center on the visual icon,
    // unlike the old approach which used unzoomed grid positions.
    function updateZoomFactors() {
        let maxZoom = 1.0 + (DockView.screenSettings.maxZoomFactor - 1.0) * root._zoomIntensity
        if (maxZoom <= 1.0 || appIconCount === 0) {
            for (let i = 0; i < appIconCount; i++) {
                let item = getAppIcon(i)
                if (item) item.zoomFactor = 1.0
            }
            return
        }

        let sigma = DockView.screenSettings.iconSize * 1.2
        let sigma2 = sigma * sigma
        let mPos = dockPanel.mouseX

        for (let i = 0; i < appIconCount; i++) {
            let item = getAppIcon(i)
            if (!item) continue

            // Compute icon's actual visual center in root coordinates
            let visualCenter
            if (DockView.isVertical) {
                visualCenter = dockPanel.y + dockRow.y + item.y + item.height / 2
            } else {
                visualCenter = dockPanel.x + dockRow.x + item.x + item.width / 2
            }

            let distance = Math.abs(mPos - visualCenter)
            item.zoomFactor = 1.0 + (maxZoom - 1.0) * Math.exp(-(distance * distance) / (2.0 * sigma2))
        }
    }

    focus: true

    function announceLaunch(name) {
        Accessible.announce(i18n("Starting %1", name), Accessible.Assertive)
    }

    function startKeyboardNavigation() {
        keyboardNavigating = true
        tooltipTimer.stop()
        tooltipItem.show = false
        if (appIconCount > 0) {
            if (hoveredIndex < 0) hoveredIndex = 0
            let item = getAppIcon(hoveredIndex)
            if (item) {
                dockPanel.mouseX = item.itemCenterX
                dockPanel.mouseY = dockRow.y + item.height / 2
            }
        }
        root.forceActiveFocus()
    }

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
        let count = appIconCount
        if (count === 0) return
        if (hoveredIndex < 0) {
            hoveredIndex = delta > 0 ? 0 : count - 1
        } else {
            hoveredIndex = Math.max(0, Math.min(count - 1, hoveredIndex + delta))
        }
        hoveredName = getAppIcon(hoveredIndex)?.displayName ?? ""
        let item = getAppIcon(hoveredIndex)
        if (item) {
            dockPanel.mouseX = item.itemCenterX
            dockPanel.mouseY = dockRow.y + item.height / 2
            updateZoomFactors()
            let msg = item.displayName + ", " + i18n("%1 of %2", hoveredIndex + 1, count)
            Accessible.announce(msg, Accessible.Polite)
        }
    }

    function announcePreviewThumbnail() {
        let title = PreviewController.focusedThumbnailTitle()
        if (!title) return
        let msg = title + ", " + i18n("%1 of %2",
            PreviewController.focusedThumbnailIndex + 1,
            PreviewController.previewThumbnailCount())
        Accessible.announce(msg, Accessible.Polite)
    }

    Keys.onPressed: function(event) {
        if (!keyboardNavigating) return
        if (PreviewController.previewKeyboardActive) {
            let thumbPrev = DockView.isVertical ? Qt.Key_Up : Qt.Key_Left
            let thumbNext = DockView.isVertical ? Qt.Key_Down : Qt.Key_Right
            let backKey = DockView.isVertical ? (DockView.edge === 2 ? Qt.Key_Left : Qt.Key_Right) : (DockView.edge === 0 ? Qt.Key_Up : Qt.Key_Down)
            switch (event.key) {
            case thumbPrev: PreviewController.navigatePreviewThumbnail(-1); announcePreviewThumbnail(); event.accepted = true; break
            case thumbNext: PreviewController.navigatePreviewThumbnail(1); announcePreviewThumbnail(); event.accepted = true; break
            case Qt.Key_Return: case Qt.Key_Enter: PreviewController.activatePreviewThumbnail(); endKeyboardNavigation(); event.accepted = true; break
            case Qt.Key_Delete: PreviewController.closePreviewThumbnail(); announcePreviewThumbnail(); event.accepted = true; break
            case Qt.Key_Escape: case backKey: PreviewController.endPreviewKeyboardNav(); event.accepted = true; break
            }
            return
        }
        let navPrev = DockView.isVertical ? Qt.Key_Up : Qt.Key_Left
        let navNext = DockView.isVertical ? Qt.Key_Down : Qt.Key_Right
        let previewKey = DockView.isVertical ? (DockView.edge === 2 ? Qt.Key_Right : Qt.Key_Left) : (DockView.edge === 0 ? Qt.Key_Down : Qt.Key_Up)
        switch (event.key) {
        case navPrev: navigateItem(-1); event.accepted = true; break
        case navNext: navigateItem(1); event.accepted = true; break
        case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space: if (hoveredIndex >= 0) { DockActions.activate(hoveredIndex); endKeyboardNavigation(); } event.accepted = true; break
        case Qt.Key_Escape: if (PreviewController.visible) PreviewController.hidePreview(); endKeyboardNavigation(); event.accepted = true; break
        case previewKey:
            if (hoveredIndex >= 0) {
                let idx = DockModel.tasksModel.index(hoveredIndex, 0)
                if (DockModel.tasksModel.data(idx, TaskManager.AbstractTasksModel.IsWindow)) {
                    let item = getAppIcon(hoveredIndex)
                    if (item) {
                        let globalPos = item.mapToGlobal(0, 0)
                        PreviewController.showPreview(hoveredIndex, globalPos.x, globalPos.y, item.width, item.height)
                        PreviewController.startPreviewKeyboardNav(); announcePreviewThumbnail();
                    }
                }
            }
            event.accepted = true; break
        case Qt.Key_Menu: if (hoveredIndex >= 0) DockContextMenu.showForTask(hoveredIndex); event.accepted = true; break
        }
    }

    // --- Internal drag state ---
    property bool _dragActive: false
    property int _dragSourceIndex: -1
    property int _dragTargetIndex: -1
    property real _dragCurrentX: 0
    property real _dragCurrentY: 0
    property real _dragStartX: 0
    property real _dragStartY: 0
    property bool _dragPending: false
    property bool _dragWasActive: false
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

    function computeDropIndex(globalMousePos) {
        let panelRel = globalMousePos - (DockView.isVertical ? dockPanel.y : dockPanel.x)
        let items = []
        for (let i = 0; i < appIconCount; i++) {
            let item = getAppIcon(i)
            if (item) items.push({ idx: i, cx: (DockView.isVertical ? item.y + item.height/2 : item.x + item.width/2) + (DockView.isVertical ? dockRow.y : dockRow.x) })
        }
        if (items.length === 0) return -1
        let bestIdx = items[0].idx, bestDist = Math.abs(panelRel - items[0].cx)
        for (let j = 1; j < items.length; j++) {
            let d = Math.abs(panelRel - items[j].cx)
            if (d < bestDist) { bestDist = d; bestIdx = items[j].idx }
        }
        return bestIdx
    }

    function computeExternalDropIndex(dropX) {
        for (let i = 0; i < appIconCount; i++) {
            let item = getAppIcon(i)
            if (item && dropX >= dockRow.x + item.x && dropX <= dockRow.x + item.x + item.width) return i
        }
        return -1
    }

    function _tryAutoPreview() {
        if (!DockSettings.previewEnabled || root.hoveredIndex < 0 || PreviewController.visible || (typeof DockContextMenu !== "undefined" && DockContextMenu.visible)) return
        
        let idx = DockModel.tasksModel.index(root.hoveredIndex, 0)
        let isWindow = DockModel.tasksModel.data(idx, TaskManager.AbstractTasksModel.IsWindow)
        
        if (isWindow) {
            tooltipItem.show = false; tooltipTimer.stop()
            root.updatePreviewGeometry(true)
        }
    }

    function traceHitTest(mX_abs, mY_abs) {
        let mPos = DockView.isVertical ? mY_abs : mX_abs
        let iconSize = DockView.screenSettings.iconSize, spacing = DockSettings.iconSpacing, slot = iconSize + spacing
        let totalUnscaled = (appIconCount * slot) - spacing
        let unscaledStart = (DockView.isVertical ? root.height : root.width) / 2 - (totalUnscaled / 2)
        for (let i = 0; i < appIconCount; i++) {
            let item = getAppIcon(i)
            if (!item) continue
            let unscaledCenter = unscaledStart + (i * slot) + (iconSize / 2)
            let zoomedSize = iconSize * item.currentScale
            if (mPos >= unscaledCenter - zoomedSize/2 && mPos <= unscaledCenter + zoomedSize/2) {
                let localPos = item.iconImage.mapFromItem(dockMouseArea, mX_abs, mY_abs)
                return `ICON-${i}: Local(${localPos.x.toFixed(1)},${localPos.y.toFixed(1)})`
            }
        }
        return "BETWEEN-ICONS"
    }

    // Projective SDF drop shadow
    ShaderEffect {
        id: dockShadow
        visible: DockSettings.shadowEnabled
        z: dockPanel.z - 1
        property real panelWidth: dockPanel.width
        property real panelHeight: dockPanel.height
        property real cornerRadius: dockPanel.radius
        // --- Shadow: elevation (The "height" of the dock above the wallpaper) ---
        property real elevation: DockSettings.shadowElevation
        property real lightX: DockSettings.shadowLightX
        property real lightY: DockSettings.shadowLightY
        property real lightZ: DockSettings.shadowLightZ
        property real lightRadius: DockSettings.shadowLightRadius
        property color _shadowColor: DockSettings.shadowColor
        property real shadowR: _shadowColor.r
        property real shadowG: _shadowColor.g
        property real shadowB: _shadowColor.b
        // --- Shadow: shadowA (Final opacity of the drop shadow) ---
        property real shadowA: DockSettings.shadowIntensity
        // --- Shadow: margin (Buffer calculated to ensure shadows aren't clipped) ---
        property real margin: Math.min(64, Math.max((Math.sqrt(lightX*lightX+lightY*lightY)+lightRadius)*elevation/Math.max(lightZ-elevation,1), lightRadius*3)+10)
        
        // --- Shadow Alignment Fix ---
        // The width/height and offsets MUST match the margin property exactly
        // because the shader uses this uniform to define its coordinate space.
        x: dockPanel.x - margin; y: dockPanel.y - margin; width: dockPanel.width + margin * 2; height: dockPanel.height + margin * 2
        
        fragmentShader: "qrc:/qml/shaders/outer_shadow.frag.qsb"
    }

    // --- Layer 4: Blueprint Ghost Grid (Live Edit Alignment) ---
    Item {
        id: blueprintGhost
        visible: typeof DockVisibility !== "undefined" && DockVisibility.liveEditMode
        z: dockPanel.z - 1
        width: DockView.isVertical ? 180 : parent.width
        height: DockView.isVertical ? parent.height : 180
        x: DockView.edge === 3 ? parent.width - width : 0
        y: DockView.edge === 1 ? parent.height - height : 0
        enabled: false
        Rectangle {
            anchors.fill: parent; color: "transparent"; border.color: Qt.rgba(1, 1, 1, 0.3); border.width: 1
            Canvas {
                anchors.fill: parent; opacity: 0.4
                onPaint: {
                    var ctx = getContext("2d"); ctx.clearRect(0, 0, width, height)
                    ctx.beginPath(); ctx.lineWidth = 0.5; ctx.strokeStyle = "rgba(255, 255, 255, 0.15)"
                    for (let x = (width/2)%10; x <= width; x += 10) { ctx.moveTo(x+0.5, 0); ctx.lineTo(x+0.5, height) }
                    for (let y = (height/2)%10; y <= height; y += 10) { ctx.moveTo(0, y+0.5); ctx.lineTo(width, y+0.5) }
                    ctx.stroke(); ctx.beginPath(); ctx.lineWidth = 1.0; ctx.strokeStyle = "rgba(255, 255, 255, 0.40)"
                    for (let x = (width/2)%50; x <= width; x += 50) { ctx.moveTo(x+0.5, 0); ctx.lineTo(x+0.5, height) }
                    for (let y = (height/2)%50; y <= height; y += 50) { ctx.moveTo(0, y+0.5); ctx.lineTo(width, y+0.5) }
                    ctx.stroke()
                }
            }
        }
    }

    // --- Layer 0: Main Dock Panel (Visual container) ---
    VisualPanel {
        id: dockPanel

        visible: opacity > 0.01

        // --- Placement: visualIconTop (Absolute boundary used to align window previews) ---
        readonly property real visualIconTop: {
            let sample = getAppIcon(0)
            let floorUnits = sample ? sample._unitIconBaseOffset : 0
            
            // Current visual icon height = unzoomedSize * (1.0 + zoomAmount)
            let currentIconHeight = DockView.screenSettings.iconSize * (1.0 + (DockView.screenSettings.maxZoomFactor - 1.0) * root._zoomIntensity)
            
            return _screenFlooring + floorUnits + currentIconHeight
        }
        // --- Placement: currentVisualOverflow (Dynamic visual territory for surface sizing) ---
        property real currentVisualOverflow: {
            let sample = getAppIcon(0)
            let floorUnits = sample ? sample._unitIconBaseOffset : 0
            
            // --- Dynamic Overflow (Bug #7 Fix) ---
            // Calculate the actual visual overflow based on the kinetic zoom intensity.
            // This ensures the Wayland surface and Previews track the icons perfectly.
            let maxPotentialZoom = (DockView.screenSettings.iconSize * (DockView.screenSettings.maxZoomFactor - 1.0))
            let currentZoomOverflow = maxPotentialZoom * root._zoomIntensity
            
            let baseOverflow = Math.max(0, (DockView.isVertical ? dockRow.implicitWidth - width : dockRow.implicitHeight - height))
            
            return currentZoomOverflow + baseOverflow + floorUnits + 16
        }
        onCurrentVisualOverflowChanged: updateWaylandInputRegion()
        
        // --- Interaction: mouseX / mouseY (Raw coordinates on the interaction surface) ---
        property real mouseX: -9999
        property real mouseY: -9999
        property bool mouseInside: dockMouseArea.containsMouse && !root._dragActive
        property real _actualContentWidth: {
            let baseW = Math.max(dockRow.implicitWidth + 32, Kirigami.Units.gridUnit * 6)
            if (DockSettings.panelLengthMode === 1) {
                let effectiveMax = DockSettings.floating ? Math.min(99, DockSettings.maxLength) : DockSettings.maxLength;
                return Math.max(baseW, root.width * (effectiveMax / 100.0))
            }
            return baseW
        }
        property real _actualContentHeight: {
            let baseH = Math.max(dockRow.implicitHeight + 32, Kirigami.Units.gridUnit * 6)
            if (DockSettings.panelLengthMode === 1) {
                let effectiveMax = DockSettings.floating ? Math.min(99, DockSettings.maxLength) : DockSettings.maxLength;
                return Math.max(baseH, root.height * (effectiveMax / 100.0))
            }
            return baseH
        }
        
        width: !DockView.isVertical ? _actualContentWidth : DockView.screenSettings.panelHeight
        height: DockView.isVertical ? _actualContentHeight : DockView.screenSettings.panelHeight
        radius: Math.min(DockView.screenSettings.cornerRadius, Math.min(width, height) / 2)
        
        property real _panelAlignmentPos: {
            if (typeof DockView === "undefined" || typeof DockView.screenSettings === "undefined") return 0;
            let align = DockView.screenSettings.alignment;
            if (align === 1) return _screenFlooring;
            if (align === 2) return DockView.isVertical ? (parent.height - height - _screenFlooring) : (parent.width - width - _screenFlooring);
            return DockView.isVertical ? (parent.height - height) / 2 : (parent.width - width) / 2;
        }
        
        x: DockView.isVertical ? _panelEdgePos : _panelAlignmentPos
        y: DockView.isVertical ? _panelAlignmentPos : _panelEdgePos
        // --- Derived: _screenFlooring (External distance from screen edge to panel) ---
        readonly property real _screenFlooring: DockView.floatingPadding

        // --- Placement: _panelEdgePos (Final calculated X/Y coordinate of the dock panel) ---
        property real _panelEdgePos: {
            if (typeof DockVisibility === "undefined") return 0
            let pfb = _screenFlooring
            switch (DockView.edge) {
            case 0: return DockVisibility.dockVisible ? pfb : -(height + 20)
            case 1: return DockVisibility.dockVisible ? (parent.height - height - pfb) : (parent.height + height)
            case 2: return DockVisibility.dockVisible ? pfb : -(width + 20)
            case 3: return DockVisibility.dockVisible ? (parent.width - width - pfb) : (parent.width + width)
            }
            return 0
        }

        function updateWaylandInputRegion() {
            if (typeof DockVisibility !== "undefined") {
                DockVisibility.setPanelRect(dockPanel.x, dockPanel.y, dockPanel.width, dockPanel.height)
                DockVisibility.setZoomOverflowHeight(dockPanel.currentVisualOverflow)
            }
            if (typeof _debugGeom !== "undefined" && _debugGeom) {
                console.log("[GEOM_DOCK] dockPanel X: " + dockPanel.x + " Y: " + dockPanel.y + " W: " + dockPanel.width + " H: " + dockPanel.height)
            }
        }

        onXChanged: updateWaylandInputRegion(); onYChanged: updateWaylandInputRegion()
        onWidthChanged: updateWaylandInputRegion(); onHeightChanged: updateWaylandInputRegion()
        
        onVisualIconTopChanged: {
            if (typeof PreviewController !== "undefined") {
                PreviewController.setDockHeight(visualIconTop)
            }
        }

        Component.onCompleted: Qt.callLater(function() {
            updateWaylandInputRegion()
            if (DockVisibility) DockVisibility.setContentDimensions(dockRow.implicitWidth, dockRow.implicitHeight)
        })

        Behavior on x { enabled: !dockPanel.mouseInside && DockView.isVertical; NumberAnimation { duration: Kirigami.Units.longDuration; easing.type: Easing.InOutQuad } }
        Behavior on y { enabled: !dockPanel.mouseInside && !DockView.isVertical; NumberAnimation { duration: Kirigami.Units.longDuration; easing.type: Easing.InOutQuad } }
        opacity: DockVisibility.dockVisible ? 1.0 : 0.0; Behavior on opacity { NumberAnimation { duration: Kirigami.Units.longDuration } }

        Item {
            id: dockRow; z: 2; readonly property real baseSpacing: DockSettings.iconSpacing
            onXChanged: if (typeof _debugGeom !== "undefined" && _debugGeom) console.log("[GEOM_ROW] dockRow X: " + x + " Y: " + y + " W: " + implicitWidth + " H: " + implicitHeight)
            property int layoutTrigger: 0

            Connections {
                target: root._currentDockRepeater
                function onItemAdded() { Qt.callLater(() => dockRow.layoutTrigger++) }
                function onItemRemoved() { Qt.callLater(() => dockRow.layoutTrigger++) }
            }

            // Rule 15 & 16: Content dimensions account for dynamic expansion.
            // [STABILITY]: We use a stable base width to prevent startup "Identity Crisis" gaps.
            readonly property real baseWidth: (appIconCount === 0) ? 0 : (appIconCount * (DockView.screenSettings.iconSize + baseSpacing)) - baseSpacing
            readonly property real baseHeight: (appIconCount === 0) ? 0 : (appIconCount * (DockView.screenSettings.iconSize + baseSpacing)) - baseSpacing

            implicitWidth: {
                let dummy = layoutTrigger
                return DockView.isVertical ? _maxIconThickness : Math.max(baseWidth, (appIconCount === 0 ? 0 : (getAppIcon(appIconCount-1)?.x + getAppIcon(appIconCount-1)?.width || 0)))
            }
            implicitHeight: {
                let dummy = layoutTrigger
                return !DockView.isVertical ? _maxIconThickness : Math.max(baseHeight, (appIconCount === 0 ? 0 : (getAppIcon(appIconCount-1)?.y + getAppIcon(appIconCount-1)?.height || 0)))
            }
            readonly property real _maxIconThickness: {
                let size = DockView.screenSettings.iconSize
                let floor = Math.max(4, Math.round(size * 0.25))
                let ind = Math.max(2, Math.round(size * 0.10))
                let gap = Math.max(2, Math.round(size * 0.125) + Math.round(size * 0.15 * (1.0 - DockSettings.indicatorOffset)))
                return size + floor + ind + gap + floor // Rule 2: Ceiling mirrors floor
            }
            onImplicitWidthChanged: if (DockVisibility) DockVisibility.setContentDimensions(implicitWidth, implicitHeight)
            onImplicitHeightChanged: if (DockVisibility) DockVisibility.setContentDimensions(implicitWidth, implicitHeight)
            // Dynamic Compressing Bumper: Perfect vertical/horizontal symmetry when there's space,
            // but allows shrinking down to 0 margin (hugging the glass pill) if the panel is scaled down.
            readonly property real _panelInternalMargin: {
                let emptySpace = DockView.isVertical ? (dockPanel.width - implicitWidth) : (dockPanel.height - implicitHeight);
                return Math.max(0, emptySpace / 2);
            }
            x: {
                if (DockView.isVertical) {
                    return DockView.edge === 2 ? _panelInternalMargin : (dockPanel.width - implicitWidth - _panelInternalMargin)
                }
                let align = typeof DockView !== "undefined" && DockView.screenSettings ? DockView.screenSettings.alignment : 0
                if (align === 1) return _panelInternalMargin
                if (align === 2) return dockPanel.width - implicitWidth - _panelInternalMargin
                return (dockPanel.width - implicitWidth) / 2
            }
            y: {
                if (!DockView.isVertical) {
                    return DockView.edge === 0 ? _panelInternalMargin : (dockPanel.height - implicitHeight - _panelInternalMargin)
                }
                let align = typeof DockView !== "undefined" && DockView.screenSettings ? DockView.screenSettings.alignment : 0
                if (align === 1) return _panelInternalMargin
                if (align === 2) return dockPanel.height - implicitHeight - _panelInternalMargin
                return (dockPanel.height - implicitHeight) / 2
            }

            Repeater {
                id: islandRepeater
                model: DockModel.islandsVariant
                
                IslandModule {
                    id: currentIsland
                    islandData: modelData

                    Repeater {
                        id: dockRepeater
                        model: currentIsland.islandData.tasksModel
                        Component.onCompleted: root._currentDockRepeater = dockRepeater
                AppIcon {
                    // --- Interaction: virtualCenter (The mathematical center of a slot per Rule 3) ---
                    readonly property real virtualCenter: {
                        let slot = iconSize + dockRow.baseSpacing, total = (appIconCount * slot) - dockRow.baseSpacing
                        let start = (DockView.isVertical ? root.height : root.width) / 2 - (total / 2)
                        return start + (index * slot) + (iconSize / 2)
                    }

                    onCurrentScaleChanged: dockRow.layoutTrigger++

                    // --- State-Aware Layout (Rule 15) ---
                    // [STABILITY]: Use stable grid when idle to prevent startup gaps.
                    // [INTERACTION]: Use recursive displacement when zooming to push neighbors.
                    // FIX: Uses stateless mathematical sum instead of p.x to prevent evaluation race conditions when resizing!
                    x: {
                        let trigger = dockRow.layoutTrigger
                        if (DockView.isVertical) return (dockRow._maxIconThickness - width) / 2
                        if (index === 0) return 0
                        
                        let slotSize = iconSize + dockRow.baseSpacing
                        if (root._zoomIntensity <= 0) return index * slotSize
                        
                        let sum = 0
                        for (let j = 0; j < index; j++) {
                            let it1 = getAppIcon(j)
                            let it2 = getAppIcon(j+1)
                            let sc1 = it1 ? it1.currentScale : 1.0
                            let sc2 = it2 ? it2.currentScale : 1.0
                            sum += (iconSize * sc1) + (dockRow.baseSpacing * (sc1 + sc2) / 2)
                        }
                        return sum
                    }
                    y: {
                        let trigger = dockRow.layoutTrigger
                        if (!DockView.isVertical) return (dockRow._maxIconThickness - height) / 2
                        if (index === 0) return 0

                        let slotSize = iconSize + dockRow.baseSpacing
                        if (root._zoomIntensity <= 0) return index * slotSize
                        
                        let sum = 0
                        for (let j = 0; j < index; j++) {
                            let it1 = getAppIcon(j)
                            let it2 = getAppIcon(j+1)
                            let sc1 = it1 ? it1.currentScale : 1.0
                            let sc2 = it2 ? it2.currentScale : 1.0
                            sum += (iconSize * sc1) + (dockRow.baseSpacing * (sc1 + sc2) / 2)
                        }
                        return sum
                    }

                    z: (root.hoveredIndex === index) ? 1 : 0
                    isHovered: (root.hoveredIndex === index)
                    isKeyboardFocused: root.keyboardNavigating && root.hoveredIndex === index
                    iconSize: DockView.screenSettings.iconSize
                    maxZoomFactor: 1.0 + (DockView.screenSettings.maxZoomFactor - 1.0) * root._zoomIntensity

                    spacing: DockSettings.iconSpacing
                    itemCenterX: virtualCenter
                    isDragSource: root._dragActive && root._dragSourceIndex === index
                    isExternalDropTarget: externalDropArea.containsDrag && externalDropArea.dropTargetIndex === index
                }
            }
            } // End IslandModule
            } // End islandRepeater
        }

        Item {
            id: pinnedSeparator
            z: 10
            property int _rt: 0
            Connections {
                target: DockModel.tasksModel
                function onLayoutChanged() { pinnedSeparator._rt++ }
                function onRowsInserted() { pinnedSeparator._rt++ }
                function onRowsRemoved() { pinnedSeparator._rt++ }
                function onModelReset() { pinnedSeparator._rt++ }
            }
            property int boundaryIndex: {
                let p = pinnedSeparator._rt
                return DockModel.pinnedBoundaryIndex()
            }
            // Visible only if there are pinned apps followed by at least one unpinned app
            visible: boundaryIndex >= 0 && boundaryIndex < (DockModel.tasksModel.rowCount() - 1)
            opacity: DockView.screenSettings.separatorOpacity
            property real at: Math.max(1, Math.round(DockView.screenSettings.iconSize * 0.05))
            property real al: Math.round(DockView.screenSettings.iconSize * 0.7)
            width: Math.round(!DockView.isVertical ? at : al); height: Math.round(DockView.isVertical ? at : al)
            x: { 
                let trigger = dockRow.layoutTrigger
                if (!visible) return 0
                if (root._zoomIntensity <= 0) {
                    let slotSize = DockView.screenSettings.iconSize + dockRow.baseSpacing
                    return Math.round(DockView.isVertical ? dockRow.x + (dockRow.implicitWidth - width) / 2 : dockRow.x + (boundaryIndex * slotSize) + DockView.screenSettings.iconSize + (dockRow.baseSpacing / 2) - (width / 2))
                }
                let i = getAppIcon(boundaryIndex)
                let n = getAppIcon(boundaryIndex + 1)
                if (!i || !n) return 0
                let g = dockRow.baseSpacing * (i.currentScale + n.currentScale) / 2
                return Math.round(DockView.isVertical ? dockRow.x + (dockRow.implicitWidth - width) / 2 : dockRow.x + i.x + i.width + (g / 2) - (width / 2))
            }
            y: { 
                let trigger = dockRow.layoutTrigger
                if (!visible) return 0
                if (root._zoomIntensity <= 0) {
                    let slotSize = DockView.screenSettings.iconSize + dockRow.baseSpacing
                    return Math.round(DockView.isVertical ? dockRow.y + (boundaryIndex * slotSize) + DockView.screenSettings.iconSize + (dockRow.baseSpacing / 2) - (height / 2) : dockRow.y + (dockRow.implicitHeight - height) / 2)
                }
                let i = getAppIcon(boundaryIndex)
                let n = getAppIcon(boundaryIndex + 1)
                if (!i || !n) return 0
                let g = dockRow.baseSpacing * (i.currentScale + n.currentScale) / 2
                return Math.round(DockView.isVertical ? dockRow.y + i.y + i.height + (g / 2) - (height / 2) : dockRow.y + (dockRow.implicitHeight - height) / 2)
            }

            Rectangle { anchors.fill: parent; visible: DockView.screenSettings.separatorStyle === 0; color: Kirigami.Theme.textColor; radius: width/2 }
            Rectangle {
                anchors.fill: parent
                visible: DockView.screenSettings.separatorStyle === 2
                radius: width / 2
                gradient: Gradient {
                    orientation: DockView.isVertical ? Gradient.Horizontal : Gradient.Vertical
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.5; color: Kirigami.Theme.textColor }
                    GradientStop { position: 1.0; color: "transparent" }
                }
            }
            Grid {
                anchors.centerIn: parent; visible: DockView.screenSettings.separatorStyle === 1; spacing: Math.max(2, Math.round(DockView.screenSettings.iconSize * 0.15)); rows: DockView.isVertical ? 1 : 3; columns: DockView.isVertical ? 3 : 1
                Repeater { model: 3; Rectangle { width: Math.max(3, Math.round(DockView.screenSettings.iconSize * 0.12)); height: width; color: "white"; radius: width/2 } }
            }
        }

        DropArea {
            id: externalDropArea; anchors.fill: parent; property int dropTargetIndex: -1
            onEntered: function(drag) { drag.accepted = true }
            onPositionChanged: function(drag) { dropTargetIndex = root.computeExternalDropIndex(drag.x) }
            onDropped: function(drop) {
                let urls = []; if(drop.hasUrls) for(let i=0; i<drop.urls.length; i++) urls.push(drop.urls[i])
                if(urls.length === 0) { drop.accepted = false; dropTargetIndex = -1; return }
                if(DockModel.isDesktopFile(urls[0])) DockActions.addLauncher(urls[0])
                else if(dropTargetIndex >= 0) DockActions.openUrlsWithTask(dropTargetIndex, urls)
                drop.accepted = true; dropTargetIndex = -1
            }
            onExited: { dropTargetIndex = -1 }
        }
    }

    // --- Layer 5: Drag & Drop Ghost (Follows mouse during drag) ---
    Kirigami.Icon {
        id: dragGhost; Accessible.ignored: true; visible: root._dragActive && root._dragSourceIndex >= 0
        width: DockView.screenSettings.iconSize; height: DockView.screenSettings.iconSize
        source: visible ? DockModel.iconData(root._dragSourceIndex) : ""
        x: root._dragCurrentX - width/2; y: root._dragCurrentY - height/2; opacity: 0.8; z: 200
    }

    Rectangle {
        id: dropIndicator; Accessible.ignored: true; visible: root._dragActive && root._dragTargetIndex >= 0 && root._dragTargetIndex !== root._dragSourceIndex
        width: DockView.isVertical ? DockView.screenSettings.iconSize : 2; height: DockView.isVertical ? 2 : DockView.screenSettings.iconSize; color: Kirigami.Theme.highlightColor; radius: 1; z: 150
        x: { if(!visible||root._dragTargetIndex<0) return 0; if(DockView.isVertical) return dockPanel.x+dockRow.x; let t=getAppIcon(root._dragTargetIndex); if(!t) return 0; let ix=dockPanel.x+dockRow.x+t.x; return root._dragTargetIndex>root._dragSourceIndex ? ix+t.width+DockSettings.iconSpacing/2-1 : ix-DockSettings.iconSpacing/2-1 }
        y: { if(!visible||root._dragTargetIndex<0) return 0; if(!DockView.isVertical) return dockPanel.y+dockRow.y; let t=getAppIcon(root._dragTargetIndex); if(!t) return 0; let iy=dockPanel.y+dockRow.y+t.y; return root._dragTargetIndex>root._dragSourceIndex ? iy+t.height+DockSettings.iconSpacing/2-1 : iy-DockSettings.iconSpacing/2-1 }
    }

    Connections {
        target: DockActions
        function onTaskLaunching(index) {
            let it = getAppIcon(index); if(!it) return; root.announceLaunch(it.displayName)
            if(it.model.IsWindow) it.manualLaunching = true
        }
    }

    Timer { id: autoPreviewTimer; interval: Math.min(200, DockSettings.previewHoverDelay); repeat: true; running: tooltipItem.visible && !PreviewController.visible && (!DockContextMenu || !DockContextMenu.visible); onTriggered: root._tryAutoPreview() }

    Connections {
        target: PreviewController
        function onVisibleChanged() { if(!PreviewController.visible && !dockMouseArea.containsMouse) { dockPanel.mouseX = -1; dockPanel.mouseY = -1; root._zoomActive = false; DockVisibility.setHovered(false) } }
    }

    function updatePreviewGeometry(force = false) {
        if (root.hoveredIndex < 0) return
        
        // --- Activation Guard (Rule 18) ---
        // If the preview isn't visible yet, we ONLY allow it to show if explicitly 
        // forced by the tooltipTimer or the auto-preview logic. 
        // This prevents the zoom animation from triggering it instantly.
        if (!force && !PreviewController.visible) return
        
        // --- Filter: Only show previews for actual Windows (Rule 18) ---
        let idx = DockModel.tasksModel.index(root.hoveredIndex, 0)
        let isWindow = DockModel.tasksModel.data(idx, TaskManager.AbstractTasksModel.IsWindow)
        
        if (!isWindow) {
            PreviewController.hidePreviewDelayed()
            return
        }

        let it = getAppIcon(root.hoveredIndex)
        if (!it) {
            KremaDebug.preview("FAILED: Item at index " + root.hoveredIndex + " is NULL")
            return
        }
        
        // --- Absolute Sync: The Full Chain (Rule 17) ---
        let localX = dockPanel.x + dockRow.x + it.x + it.visualIconX
        let localY = dockPanel.y + dockRow.y + it.y + it.visualIconY
        
        // Capture the real zoomed size
        let vW = it.visualIconWidth > 0 ? it.visualIconWidth : it.width
        let vH = it.visualIconHeight > 0 ? it.visualIconHeight : it.height
        
        KremaDebug.preview("Trigger: Index=" + root.hoveredIndex + " Pos=[" + localX + "," + localY + "] Size=" + vW + "x" + vH)
        PreviewController.showPreview(root.hoveredIndex, localX, localY, vW, vH)
    }

    // --- Absolute Sync: Zoom Tracking ---
    // Reactively follow the hovered icon's visual expansion.
    // This handles both mouse-driven zoom and kinetic animation cycles.
    Connections {
        target: (root.hoveredIndex >= 0) ? getAppIcon(root.hoveredIndex) : null
        ignoreUnknownSignals: true
        function onVisualIconYChanged() { root.updatePreviewGeometry() }
        function onVisualIconXChanged() { root.updatePreviewGeometry() }
        function onXChanged() { root.updatePreviewGeometry() }
        function onYChanged() { root.updatePreviewGeometry() }
    }

    Timer {
        id: tooltipTimer; interval: DockSettings.previewHoverDelay
        onTriggered: {
            if(root.hoveredIndex < 0 || (DockContextMenu && DockContextMenu.visible)) return
            let idx = DockModel.tasksModel.index(root.hoveredIndex, 0)
            if(DockModel.tasksModel.data(idx, TaskManager.AbstractTasksModel.IsWindow) && DockSettings.previewEnabled) {
                updatePreviewGeometry(true) // FORCE activation after timer expires
            } else { tooltipItem.show = true }
        }
    }

    Rectangle {
        id: tooltipItem; Accessible.ignored: true; property bool show: false; visible: show && root.hoveredName.length > 0; onVisibleChanged: if(!visible) show = false
        
        // --- Absolute Sync: Tooltip Alignment (Rule 17) ---
        // Labels now track the VISUAL pixels of zoomed icons, not just the static grid slot.
        x: {
            if (root.hoveredIndex < 0 || root.hoveredIndex >= appIconCount) return 0
            let it = getAppIcon(root.hoveredIndex)
            if (!it) return 0
            let sp = Kirigami.Units.mediumSpacing
            
            let absX = dockPanel.x + dockRow.x + it.x + it.visualIconX
            let vW = it.visualIconWidth > 0 ? it.visualIconWidth : it.width
            
            if (DockView.edge === 2) return absX + vW + sp // Left: right of icon
            if (DockView.edge === 3) return absX - width - sp // Right: left of icon
            return absX + vW/2 - width/2 // Top/Bottom: centered horizontally
        }
        y: {
            if (root.hoveredIndex < 0 || root.hoveredIndex >= appIconCount) return 0
            let it = getAppIcon(root.hoveredIndex)
            if (!it) return 0
            let sp = Kirigami.Units.mediumSpacing
            
            let absY = dockPanel.y + dockRow.y + it.y + it.visualIconY
            let vH = it.visualIconHeight > 0 ? it.visualIconHeight : it.height
            
            if (DockView.edge === 0) return absY + vH + sp // Top: below icon
            if (DockView.edge === 1) return absY - height - sp // Bottom: above icon
            return absY + vH/2 - height/2 // Left/Right: centered vertically
        }
        
        Kirigami.Theme.colorSet: Kirigami.Theme.Tooltip; Kirigami.Theme.inherit: false; width: tooltipLabel.implicitWidth+Kirigami.Units.largeSpacing*2; height: tooltipLabel.implicitHeight+Kirigami.Units.largeSpacing; radius: Kirigami.Units.smallSpacing; color: Kirigami.Theme.backgroundColor; z: 100
        QQC2.Label { id: tooltipLabel; anchors.centerIn: parent; text: root.hoveredName; Accessible.ignored: true }
        Connections { target: root; function onHoveredIndexChanged() { tooltipItem.show = false; tooltipTimer.stop(); if(root.hoveredIndex < 0) PreviewController.hidePreviewDelayed(); else if(!root.keyboardNavigating) { PreviewController.hidePreviewDelayed(); tooltipTimer.restart() } } }
    }


    // --- Layer 8: Settings Dialog Container (Configuration UI) ---
    Loader {
        id: settingsUnifiedLoader
        x: { if(DockView.edge===2) return blueprintGhost.width; if(DockView.edge===3) return parent.width-blueprintGhost.width-width; return (parent.width-width)/2 }
        y: { if(DockView.edge===0) return blueprintGhost.height; if(DockView.edge===1) return parent.height-blueprintGhost.height-height; return (parent.height-height)/2 }
        active: SettingsController ? SettingsController.visible : false; visible: active; source: active ? "qrc:/qml/SettingsDialog.qml" : ""
        Connections {
            target: SettingsController || null
            function onVisibleChanged() {
                if(DockVisibility) { if(SettingsController.visible){ DockVisibility.liveEditMode=true } else { DockVisibility.liveEditMode=false; DockVisibility.setSettingsRect(0,0,0,0) } }
            }
        }
        onXChanged: updateSettingsHitbox(); onYChanged: updateSettingsHitbox(); onWidthChanged: updateSettingsHitbox(); onHeightChanged: updateSettingsHitbox()
        function updateSettingsHitbox() { if(item && active && DockVisibility) DockVisibility.setSettingsRect(x,y,width,height) }
        onLoaded: { if(item && SettingsController) { item.open(SettingsController.module); updateSettingsHitbox() } }
    }
}
