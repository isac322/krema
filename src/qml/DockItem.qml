// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0

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

    // Launch animation state
    property bool manualLaunching: false
    readonly property bool launching:
        manualLaunching
        || (_isStartup && !_noOpOverride)
        || _waitingForWindow

    // Drag and drop visual feedback (driven from main.qml)
    property bool isDragSource: false
    property bool isExternalDropTarget: false

    // Configuration from DockView
    property int iconSize: 48
    property real maxZoomFactor: 1.6
    property real panelMouseX: -1
    property bool panelMouseInside: false
    property int spacing: 4
    property real itemCenterX: 0

    // Gaussian sigma factor for the zoom curve (sigma = iconSize * factor).
    // Controls how many neighboring icons are visibly affected by zoom.
    // Recommended range: 0.8 (tight) – 1.8 (wide). Default 1.2 ≈ macOS Dock.
    property real zoomSigmaFactor: 1.2
    readonly property real zoomSigma: iconSize * zoomSigmaFactor

    // Computed zoom factor for this item
    readonly property real zoomFactor: {
        if (!panelMouseInside || panelMouseX < 0) {
            return 1.0
        }

        let distance = Math.abs(panelMouseX - itemCenterX)
        let sigma2 = zoomSigma * zoomSigma
        return 1.0 + (maxZoomFactor - 1.0) * Math.exp(-(distance * distance) / sigma2)
    }

    // --- KDE state-driven launch tracking ---
    //
    // Launch animation lifecycle:
    //   1. manualLaunching: click → IsStartup handoff bridge (a few ms)
    //   2. _isStartup: KDE startup notification drives bounce (until window or timeout)
    //   3. _waitingForWindow: keeps bounce alive after KDE's 5s timeout for slow apps
    //
    // Bounce stops when any of:
    //   - ChildCount increases (new window created)
    //   - IsActive changes (single-instance app raised its existing window)
    //   - noOpDetectionTimer fires (already-active app, no new window in 2s → no-op)
    //   - launchSafetyTimer fires (IsStartup never arrived within 500ms)
    //   - maxLaunchTimer fires (30s absolute safety net)

    readonly property bool _isStartup: model.IsStartup ?? false
    readonly property bool _isActive: model.IsActive ?? false
    readonly property int _childCount: model.ChildCount || 0
    property int _prevChildCount: 0

    property bool _noOpOverride: false
    property bool _waitingForWindow: false
    property int _childCountAtLaunch: 0

    // --- State change handlers ---

    on_IsStartupChanged: {
        if (_isStartup) {
            // KDE acknowledged the launch → release manualLaunching bridge
            if (manualLaunching) manualLaunching = false
        } else {
            // IsStartup went false (window matched, cancelled, or KDE timeout).
            // If no new window appeared and not already active → slow app,
            // keep bouncing via _waitingForWindow.
            if (!_noOpOverride
                    && _childCount === _childCountAtLaunch
                    && !_isActive) {
                _waitingForWindow = true
                maxLaunchTimer.restart()
            }
        }
    }

    on_IsActiveChanged: {
        if (_isActive && manualLaunching) manualLaunching = false
        if (_isActive && _waitingForWindow) {
            _waitingForWindow = false
            maxLaunchTimer.stop()
        }
    }

    on_ChildCountChanged: {
        if (_childCount > _prevChildCount) {
            if (manualLaunching) manualLaunching = false
            _noOpOverride = false
            _waitingForWindow = false
            noOpDetectionTimer.stop()
            maxLaunchTimer.stop()
        }
        _prevChildCount = _childCount
    }

    // --- Timers ---

    // 500ms: fallback for the edge case where IsStartup never fires
    // (e.g. single-instance app silently ignores D-Bus activation)
    Timer {
        id: launchSafetyTimer
        interval: 500
        onTriggered: {
            if (dockItem.manualLaunching) dockItem.manualLaunching = false
        }
    }

    // 2s: detects no-op for already-active apps. If no new window appeared
    // within 2s after middle-click, override _isStartup to stop bounce.
    Timer {
        id: noOpDetectionTimer
        interval: 2000
        onTriggered: {
            if (dockItem._childCount === dockItem._childCountAtLaunch) {
                dockItem._noOpOverride = true
            }
        }
    }

    // 30s: absolute safety net for _waitingForWindow mode
    // (handles app crash/failure where window never appears)
    Timer {
        id: maxLaunchTimer
        interval: 30000
        onTriggered: {
            dockItem._waitingForWindow = false
        }
    }

    onManualLaunchingChanged: {
        if (manualLaunching) {
            _childCountAtLaunch = _childCount
            _noOpOverride = false
            _waitingForWindow = false
            launchSafetyTimer.restart()
            maxLaunchTimer.stop()

            // Start no-op detection only if app is already active
            if (_isActive) {
                noOpDetectionTimer.restart()
            }
        } else {
            launchSafetyTimer.stop()
        }
    }

    // Animated scale
    property real currentScale: 1.0
    property bool _zoomAnimReady: false

    Behavior on currentScale {
        enabled: dockItem._zoomAnimReady
        NumberAnimation {
            duration: Kirigami.Units.shortDuration
            easing.type: Easing.OutCubic
        }
    }

    // Update currentScale when zoomFactor changes
    onZoomFactorChanged: currentScale = zoomFactor

    // When item position shifts due to model reorganization (e.g. hideActivatedLaunchers
    // merges a launcher with its window, causing other delegates to shift), suppress
    // zoom animation to prevent the visual glitch where shifted icons animate their scale.
    // Normal mouse-driven zoom doesn't change itemCenterX, so this only fires during
    // model/layout changes.
    onItemCenterXChanged: {
        if (_zoomAnimReady) {
            _zoomAnimReady = false
            currentScale = zoomFactor
            Qt.callLater(function() { _zoomAnimReady = true })
        }
    }

    // On delegate creation: apply zoom instantly (no animation) to avoid glitch
    // when Repeater recreates delegates due to model changes.
    // Qt.callLater() defers _zoomAnimReady until AFTER Row layout has set the
    // delegate's x position, ensuring itemCenterX and zoomFactor are correct.
    Component.onCompleted: {
        _prevChildCount = _childCount
        currentScale = zoomFactor  // best guess pre-layout
        Qt.callLater(function() {
            currentScale = zoomFactor  // correct value after layout
            _zoomAnimReady = true
        })
    }

    // Size: base icon size, scaled by zoom
    width: iconSize
    height: iconSize + indicatorRow.height + Kirigami.Units.smallSpacing  // icon + gap + indicators

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
            // IMPORTANT: Read model.display to create a reactive dependency on
            // model data. After model reordering (move), the Repeater may update
            // delegate model data without changing index (position unchanged).
            // Without this, the binding only tracks dockItem.index and won't
            // re-evaluate when different data appears at the same position.
            let _dep = model.display
            let name = DockModel.iconName(dockItem.index)
            if (name && name.length > 0) {
                return "image://icon/" + name
            }
            return ""
        }
        sourceSize: Qt.size(Math.ceil(iconSize * maxZoomFactor), Math.ceil(iconSize * maxZoomFactor))
        smooth: true

        // Bounce transform for launch animation
        transform: Translate { id: bounceTranslate; y: 0 }

        // Highlight for active window / drag source dimming
        opacity: {
            if (dockItem.isDragSource) return 0.3
            if (dockItem.model.IsActive) return 1.0
            if (dockItem.model.IsMinimized) return 0.5
            return 0.8
        }

        Behavior on opacity {
            NumberAnimation { duration: Kirigami.Units.shortDuration }
        }

        // Fallback placeholder when icon is not available
        Rectangle {
            id: iconPlaceholder
            anchors.fill: parent
            radius: Kirigami.Units.largeSpacing
            color: Kirigami.Theme.highlightColor
            visible: iconImage.status !== Image.Ready

            QQC2.Label {
                anchors.centerIn: parent
                text: {
                    let name = dockItem.model.display || ""
                    return name.length > 0 ? name[0].toUpperCase() : "?"
                }
                font.pixelSize: iconSize * 0.4
                font.bold: true
                color: Kirigami.Theme.highlightedTextColor
            }
        }
    }

    // External drop target highlight (shown when dragging a file over this icon)
    Rectangle {
        anchors.fill: iconImage
        anchors.margins: -Kirigami.Units.smallSpacing
        radius: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing / 2
        color: "transparent"
        border.color: Kirigami.Theme.highlightColor
        border.width: 2
        visible: dockItem.isExternalDropTarget
        opacity: 0.9
    }

    // Launch bounce animation — finishes current cycle gracefully when launching ends
    property bool _finishingBounce: false

    SequentialAnimation {
        id: bounceAnim
        loops: 1

        NumberAnimation {
            target: bounceTranslate; property: "y"
            to: -8
            duration: dockItem._finishingBounce ? 170 : 200
            easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: bounceTranslate; property: "y"
            to: 0
            duration: dockItem._finishingBounce ? 170 : 200
            easing.type: Easing.InQuad
        }

        onFinished: {
            if (dockItem._finishingBounce) {
                // Finished the wind-down cycle
                dockItem._finishingBounce = false
                bounceTranslate.y = 0
            } else if (dockItem.launching) {
                // Still launching — bounce again
                bounceAnim.start()
            } else {
                bounceTranslate.y = 0
            }
        }
    }

    onLaunchingChanged: {
        if (launching) {
            _finishingBounce = false
            bounceAnim.start()
        } else if (bounceAnim.running) {
            // Let the current cycle finish at faster speed
            _finishingBounce = true
        }
    }

    // Status indicator dots
    Row {
        id: indicatorRow
        anchors.top: iconImage.bottom
        anchors.topMargin: Kirigami.Units.smallSpacing
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Kirigami.Units.smallSpacing

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
                color: Kirigami.Theme.textColor
                opacity: dockItem.model.IsMinimized ? 0.4 : 0.8

                Behavior on opacity {
                    NumberAnimation { duration: Kirigami.Units.shortDuration }
                }
            }
        }
    }

    // Click handling is done in main.qml's dockMouseArea using
    // hoveredIndex-based hit testing to match the zoomed icon geometry.
}
