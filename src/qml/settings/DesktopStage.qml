// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Effects
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0

// Hero picture of a settings page: a rounded miniature of the user's desktop
// (the real Plasma wallpaper, else a theme gradient) at the real screen
// proportions. Children are laid over the miniature screen `desktop`, whose
// edges are the screen edges, in preview pixels (`unit` per real pixel); a
// MiniDock child sits on the dock's edge.
//
// The stage shows the whole screen width at `magnification` 1; a larger
// magnification (or a smaller height than the screen aspect needs) crops the
// screen around the middle of `edge`, where the dock is. `fitScreen` instead
// shows the whole screen, centred, for pickers that need every edge.
//
// Measurement overlay: set `measureTarget` to an item inside the stage (e.g.
// MiniDock.focusIcon) and `measureText` to the value; the stage outlines the
// item and draws accent guide lines and the value along `measureOrientation`.
// Clear `measureTarget` to hide it. showMeasure()/hideMeasure() do the same
// imperatively.
//
//     DesktopStage {
//         id: stage
//         active: page.windowActive
//         measureTarget: sizeSlider.active ? dock.focusIcon : null
//         measureText: i18nc("@label pixels", "%1 px", DockSettings.iconSize)
//         MiniDock { id: dock; unit: stage.unit; backdrop: stage.backdrop; active: stage.active }
//     }
Item {
    id: stage

    /// Screen edge the crop centers on: 0 Top, 1 Bottom, 2 Left, 3 Right.
    property int edge: DockSettings.edge
    /// Looping animations of the stage content may run.
    property bool active: true
    /// Zoom into the screen around the middle of `edge` (at least this much
    /// over a stage that just covers the screen).
    property real magnification: 1.0
    /// Real screen pixels shown along `edge` by an elevated stage, so the
    /// default dock (about 320 px long) covers roughly half of it whatever
    /// the screen resolution.
    property real framedLength: 650
    /// Show the whole screen, centred in the stage, instead of cropping.
    property bool fitScreen: false
    /// Default scale: elevated stages frame `framedLength` along the edge
    /// (never showing less than the whole screen across it), tiles cover
    /// the screen; `fitScreen` stages contain it.
    readonly property real framingUnit: {
        if (fitScreen) {
            return Math.min(width / screenSize.width, height / screenSize.height)
        }
        const cover = Math.max(width / screenSize.width, height / screenSize.height) * magnification
        if (!elevated) {
            return cover
        }
        return Math.max(cover, (_verticalEdge ? height : width) / Math.max(1, framedLength))
    }
    /// Preview pixels per real screen pixel.
    property real unit: framingUnit
    /// Height limit when laid out at its implicit height.
    property real maximumHeight: Kirigami.Units.gridUnit * 11
    /// Shape used for the implicit height.
    property real aspectRatio: screenAspect
    property real radius: Kirigami.Units.cornerRadius * 3
    /// Hairline border and soft drop shadow; off for picker thumbnails.
    property bool elevated: true

    /// Logical size of the dock's (primary) screen.
    readonly property size screenSize: SettingsWindow.screenSize
    readonly property real screenAspect: screenSize.width / Math.max(1, screenSize.height)
    /// The miniature screen; parent of the stage's children.
    readonly property Item desktop: screenItem
    /// The wallpaper, to blur behind a MiniDock (MiniDock.backdrop).
    readonly property Item backdrop: wallpaper

    default property alias content: screenItem.data

    /// Item whose bounds are measured (inside the stage); null hides.
    property Item measureTarget: null
    /// Value shown on the measurement, e.g. "48 px".
    property string measureText
    /// Qt.Horizontal measures the target's width, Qt.Vertical its height.
    property int measureOrientation: Qt.Horizontal

    function showMeasure(text, target, orientation) {
        measureText = text
        measureOrientation = orientation === undefined ? Qt.Horizontal : orientation
        measureTarget = target
    }

    /// Hides the measurement, or only the one of `target` when given.
    function hideMeasure(target) {
        if (target === undefined || target === measureTarget) {
            measureTarget = null
        }
    }

    implicitWidth: Kirigami.Units.gridUnit * 20
    implicitHeight: Math.round(Math.min(maximumHeight, (width > 0 ? width : implicitWidth) / aspectRatio))
    Layout.fillWidth: true

    readonly property bool _verticalEdge: edge === 2 || edge === 3
    readonly property color _shadowColor: Qt.alpha(Qt.darker(Kirigami.Theme.backgroundColor, 4.0), 0.35)

    // Soft offset drop shadow behind the miniature.
    Kirigami.ShadowedRectangle {
        anchors.fill: parent
        visible: stage.elevated
        radius: stage.radius
        color: Kirigami.Theme.backgroundColor
        shadow.size: Kirigami.Units.gridUnit * 0.75
        shadow.yOffset: 2
        shadow.color: stage._shadowColor
    }

    Item {
        id: frame
        anchors.fill: parent

        // Rounded corners need the mask; square stages (picker tiles,
        // rounded by ChoiceCard) only clip.
        clip: stage.radius <= 0
        layer.enabled: stage.radius > 0
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: frameMask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }

        Item {
            id: screenItem
            width: stage.screenSize.width * stage.unit
            height: stage.screenSize.height * stage.unit
            x: stage.fitScreen || !stage._verticalEdge ? (frame.width - width) / 2 : (stage.edge === 2 ? 0 : frame.width - width)
            y: stage.fitScreen || stage._verticalEdge ? (frame.height - height) / 2 : (stage.edge === 0 ? 0 : frame.height - height)

            Item {
                id: wallpaper
                anchors.fill: parent

                // Theme-derived stand-in while no wallpaper image is known.
                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.darker(Kirigami.Theme.highlightColor, 2.2) }
                        GradientStop { position: 0.6; color: Kirigami.Theme.highlightColor }
                        GradientStop { position: 1.0; color: Qt.lighter(Kirigami.Theme.highlightColor, 1.35) }
                    }
                }

                Image {
                    // Decoding size in 128 px steps, so resizing the window
                    // rarely reloads and stages of one size share the cache.
                    readonly property real step: 128
                    anchors.fill: parent
                    source: SettingsWindow.wallpaperUrl
                    visible: status === Image.Ready
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: Math.ceil(width * Screen.devicePixelRatio / step) * step
                    sourceSize.height: Math.ceil(height * Screen.devicePixelRatio / step) * step
                }
            }
        }

        // --- Measurement overlay ---

        Item {
            id: measure

            anchors.fill: parent
            readonly property real margin: Kirigami.Units.smallSpacing
            readonly property real lineOffset: Kirigami.Units.smallSpacing * 1.5
            readonly property bool horizontal: stage.measureOrientation !== Qt.Vertical

            // Target bounds in frame coordinates. Summing positions up the
            // parent chain keeps the binding live while the target moves
            // (mapFromItem() is not observable).
            readonly property rect targetRect: {
                const target = stage.measureTarget
                if (!target) {
                    return Qt.rect(0, 0, 0, 0)
                }
                let x = 0
                let y = 0
                let item = target
                while (item && item !== frame) {
                    x += item.x
                    y += item.y
                    item = item.parent
                }
                if (!item) {
                    const point = frame.mapFromItem(target, 0, 0)
                    x = point.x
                    y = point.y
                }
                return Qt.rect(x, y, target.width, target.height)
            }
            // Last shown bounds, kept while fading out.
            property rect shownRect: Qt.rect(0, 0, 0, 0)
            Binding on shownRect {
                when: stage.measureTarget !== null
                value: measure.targetRect
                restoreMode: Binding.RestoreNone
            }
            property string shownText
            Binding on shownText {
                when: stage.measureTarget !== null
                value: stage.measureText
                restoreMode: Binding.RestoreNone
            }

            readonly property rect r: shownRect
            // Dimension line above (or left of) the target when it is in the
            // far half of the frame, else below (or right of) it.
            readonly property bool before: horizontal ? r.y + r.height / 2 > height / 2 : r.x + r.width / 2 > width / 2
            readonly property real linePos: horizontal
                ? (before ? r.y - lineOffset : r.y + r.height + lineOffset)
                : (before ? r.x - lineOffset : r.x + r.width + lineOffset)

            opacity: stage.measureTarget ? 1 : 0
            visible: opacity > 0
            Behavior on opacity {
                NumberAnimation { duration: Kirigami.Units.shortDuration; easing.type: Easing.OutCubic }
            }

            // Measured bounds.
            Rectangle {
                x: measure.r.x
                y: measure.r.y
                width: Math.max(1, measure.r.width)
                height: Math.max(1, measure.r.height)
                radius: Math.min(2, width / 2, height / 2)
                color: Qt.alpha(Kirigami.Theme.highlightColor, 0.18)
                border.width: 1
                border.color: Kirigami.Theme.highlightColor
            }

            // Extension lines from the bounds to the dimension line.
            Repeater {
                model: 2
                Rectangle {
                    required property int index
                    readonly property real edgePos: measure.horizontal
                        ? measure.r.x + (index === 0 ? 0 : measure.r.width)
                        : measure.r.y + (index === 0 ? 0 : measure.r.height)
                    readonly property real from: measure.horizontal
                        ? (measure.before ? measure.linePos : measure.r.y + measure.r.height)
                        : (measure.before ? measure.linePos : measure.r.x + measure.r.width)
                    readonly property real to: measure.horizontal
                        ? (measure.before ? measure.r.y : measure.linePos)
                        : (measure.before ? measure.r.x : measure.linePos)
                    x: measure.horizontal ? Math.round(edgePos) : from
                    y: measure.horizontal ? from : Math.round(edgePos)
                    width: measure.horizontal ? 1 : Math.max(0, to - from)
                    height: measure.horizontal ? Math.max(0, to - from) : 1
                    color: Qt.alpha(Kirigami.Theme.highlightColor, 0.6)
                }
            }

            // Dimension line with end ticks.
            Rectangle {
                x: measure.horizontal ? measure.r.x : Math.round(measure.linePos)
                y: measure.horizontal ? Math.round(measure.linePos) : measure.r.y
                width: measure.horizontal ? Math.max(1, measure.r.width) : 1
                height: measure.horizontal ? 1 : Math.max(1, measure.r.height)
                color: Kirigami.Theme.highlightColor
            }
            Repeater {
                model: 2
                Rectangle {
                    required property int index
                    readonly property real tick: Kirigami.Units.smallSpacing
                    readonly property real edgePos: measure.horizontal
                        ? measure.r.x + (index === 0 ? 0 : measure.r.width)
                        : measure.r.y + (index === 0 ? 0 : measure.r.height)
                    x: measure.horizontal ? Math.round(edgePos) : Math.round(measure.linePos - tick)
                    y: measure.horizontal ? Math.round(measure.linePos - tick) : Math.round(edgePos)
                    width: measure.horizontal ? 1 : tick * 2 + 1
                    height: measure.horizontal ? tick * 2 + 1 : 1
                    color: Kirigami.Theme.highlightColor
                }
            }

            // Value label beyond the dimension line, kept inside the frame.
            Rectangle {
                id: valuePill
                readonly property real centerAlong: measure.horizontal
                    ? measure.r.x + measure.r.width / 2
                    : measure.r.y + measure.r.height / 2
                width: valueLabel.implicitWidth + Kirigami.Units.largeSpacing
                height: valueLabel.implicitHeight + Kirigami.Units.smallSpacing
                radius: height / 2
                color: Kirigami.Theme.highlightColor
                x: Math.max(measure.margin, Math.min(measure.width - width - measure.margin,
                    measure.horizontal
                        ? centerAlong - width / 2
                        : (measure.before ? measure.linePos - measure.lineOffset - width : measure.linePos + measure.lineOffset)))
                y: Math.max(measure.margin, Math.min(measure.height - height - measure.margin,
                    measure.horizontal
                        ? (measure.before ? measure.linePos - measure.lineOffset - height : measure.linePos + measure.lineOffset)
                        : centerAlong - height / 2))

                QQC2.Label {
                    id: valueLabel
                    anchors.centerIn: parent
                    text: measure.shownText
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    font.weight: Font.DemiBold
                    font.features: ({ "tnum": 1 })
                    color: Kirigami.Theme.highlightedTextColor
                    Accessible.ignored: true
                }
            }
        }
    }

    Rectangle {
        id: frameMask
        anchors.fill: frame
        radius: stage.radius
        visible: false
        layer.enabled: stage.radius > 0
    }

    // Hairline edge of the miniature screen.
    Rectangle {
        anchors.fill: parent
        visible: stage.elevated
        radius: stage.radius
        color: Qt.alpha(Kirigami.Theme.backgroundColor, 0)
        border.width: 1
        border.color: Qt.alpha(Kirigami.Theme.textColor, 0.15)
    }
}
