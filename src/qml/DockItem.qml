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

    // Launch animation state
    property bool manualLaunching: false
    readonly property bool launching: manualLaunching || (model.IsStartup ?? false)

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

    // Track ChildCount to detect new instances appearing from ANY source:
    // dock middle-click, keyboard shortcuts (Ctrl+Alt+T), app menus, etc.
    // When ChildCount increases, start a bounce animation.
    // A timer ensures the bounce is visible for at least ~2 cycles (800ms)
    // before stopping gracefully.
    readonly property int _childCount: model.ChildCount || 0
    property int _prevChildCount: 0

    Timer {
        id: bounceStopTimer
        interval: 800  // ~2 full bounce cycles (400ms each)
        onTriggered: dockItem.manualLaunching = false
    }

    on_ChildCountChanged: {
        if (_childCount > _prevChildCount) {
            if (!launching) {
                // New instance from any source — start bounce
                manualLaunching = true
                launchFallbackTimer.restart()  // 3s safety net
            }
            // Schedule graceful bounce stop after visible duration.
            // restart() extends the timer if multiple instances spawn quickly.
            bounceStopTimer.restart()
        }
        _prevChildCount = _childCount
    }

    // Animated scale
    property real currentScale: 1.0
    property bool _zoomAnimReady: false

    Behavior on currentScale {
        enabled: dockItem._zoomAnimReady
        NumberAnimation {
            duration: 150
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

        // Bounce transform for launch animation
        transform: Translate { id: bounceTranslate; y: 0 }

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

    // Fallback timer: clear manualLaunching after 3s if IsStartup never fires
    Timer {
        id: launchFallbackTimer
        interval: 3000
        onTriggered: dockItem.manualLaunching = false
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

    // Click handling is done in main.qml's dockMouseArea using
    // hoveredIndex-based hit testing to match the zoomed icon geometry.
}
