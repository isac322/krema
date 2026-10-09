// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

SettingsPage {
    id: page
    title: i18n("Layout & Position")
    subtitle: i18n("Which screen edge the dock sits on, and how the panel is shaped")

    // Edge indices follow the kcfg Edge enum: 0 Top, 1 Bottom, 2 Left, 3 Right.
    readonly property var edgeNames: [
        i18n("Top"),
        i18n("Bottom"),
        i18n("Left"),
        i18n("Right")
    ]

    // --- Stage: the miniature screen is the edge picker itself ---
    //
    // Four quarter-strip zones on the screen edges select the dock edge;
    // the MiniDock follows DockSettings.edge live. Hovering a zone shows a
    // translucent accent band where the dock would sit, with the pill label.
    stage: Component {
        DesktopStage {
            id: stage

            // Every edge must be visible and clickable.
            fitScreen: true
            maximumHeight: Kirigami.Units.gridUnit * 15
            active: page.windowActive

            MiniDock {
                id: dock
                unit: stage.unit
                backdrop: stage.backdrop
                active: stage.active
            }

            // Screen edge picker: four focusable edge zones covering the
            // miniature screen, exposed as radio buttons inside the
            // "Screen edge" group. Tab enters at the selected edge; arrow
            // keys move the selection spatially; Space/Return select.
            Item {
                id: edgeGroup
                anchors.fill: parent

                Accessible.role: Accessible.Grouping
                Accessible.name: i18n("Screen edge")

                // Applies the edge live; moves focus to its zone when a
                // focus reason is given (keyboard reasons show the focus ring).
                function select(edgeIdx, focusReason) {
                    DockSettings.edge = edgeIdx
                    if (focusReason !== undefined)
                        edgeZones.itemAt(edgeIdx).forceActiveFocus(focusReason)
                }

                Repeater {
                    id: edgeZones
                    model: 4

                    QQC2.AbstractButton {
                        id: zone
                        required property int index
                        readonly property int edgeIdx: index
                        readonly property bool isSelected: DockSettings.edge === edgeIdx
                        readonly property bool isHorizontalEdge: edgeIdx < 2

                        text: page.edgeNames[edgeIdx]
                        checkable: false
                        checked: isSelected
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        // Radio-group convention: only the selected edge is a Tab stop.
                        activeFocusOnTab: isSelected

                        // Click zones: top/bottom quarter strips across the full
                        // width, left/right quarter strips of the middle half.
                        x: edgeIdx === 3 ? edgeGroup.width * 0.75 : 0
                        y: edgeIdx === 0 ? 0 : (edgeIdx === 1 ? edgeGroup.height * 0.75 : edgeGroup.height * 0.25)
                        width: isHorizontalEdge ? edgeGroup.width : edgeGroup.width * 0.25
                        height: isHorizontalEdge ? edgeGroup.height * 0.25 : edgeGroup.height * 0.5

                        Accessible.role: Accessible.RadioButton
                        Accessible.name: text
                        Accessible.checkable: true
                        Accessible.checked: isSelected
                        Accessible.onPressAction: edgeGroup.select(edgeIdx)
                        Accessible.onToggleAction: edgeGroup.select(edgeIdx)

                        onClicked: edgeGroup.select(edgeIdx)
                        Keys.onReturnPressed: edgeGroup.select(edgeIdx)
                        Keys.onEnterPressed: edgeGroup.select(edgeIdx)
                        Keys.onUpPressed: edgeGroup.select(0, Qt.TabFocusReason)
                        Keys.onDownPressed: edgeGroup.select(1, Qt.TabFocusReason)
                        Keys.onLeftPressed: edgeGroup.select(2, Qt.TabFocusReason)
                        Keys.onRightPressed: edgeGroup.select(3, Qt.TabFocusReason)

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        background: Item {}
                        contentItem: Item {}

                        // Translucent band along the zone's screen edge, sized
                        // like the dock, previewing where it would sit. The
                        // selected edge already shows the dock itself.
                        Rectangle {
                            readonly property real band: Math.min(
                                dock.thickness + dock.floatPx + Kirigami.Units.smallSpacing,
                                (zone.isHorizontalEdge ? zone.height : zone.width) * 0.7)

                            x: zone.edgeIdx === 3 ? zone.width - width : 0
                            y: zone.edgeIdx === 1 ? zone.height - height : 0
                            width: zone.isHorizontalEdge ? zone.width : band
                            height: zone.isHorizontalEdge ? band : zone.height
                            color: Kirigami.Theme.highlightColor
                            opacity: !zone.isSelected && (zone.hovered || pillMouse.containsMouse || zone.visualFocus) ? 0.3 : 0
                            Behavior on opacity {
                                NumberAnimation { duration: Kirigami.Units.shortDuration; easing.type: Easing.OutCubic }
                            }
                        }

                        // Pill-shaped label; shifts inward past the dock when selected.
                        Rectangle {
                            id: pill
                            readonly property real inset: zone.isSelected
                                ? dock.thickness + dock.floatPx + Kirigami.Units.smallSpacing
                                : Kirigami.Units.smallSpacing

                            width: edgeLabel.implicitWidth + Kirigami.Units.largeSpacing * 2
                            height: edgeLabel.implicitHeight + Kirigami.Units.smallSpacing * 2
                            radius: height / 2
                            // Readable over any wallpaper: theme background
                            // behind the label, accent fill when selected.
                            color: zone.isSelected ? Kirigami.Theme.highlightColor
                                : Qt.alpha(Kirigami.Theme.backgroundColor, zone.hovered || pillMouse.containsMouse || zone.visualFocus ? 0.95 : 0.8)
                            // The selected pill sits where the dock's measurement
                            // draws its value; step aside while it is shown.
                            opacity: zone.isSelected && stage.measureTarget !== null ? 0 : 1
                            Behavior on opacity {
                                NumberAnimation { duration: Kirigami.Units.shortDuration; easing.type: Easing.OutCubic }
                            }

                            x: {
                                if (zone.isHorizontalEdge)
                                    return (zone.width - width) / 2
                                if (zone.edgeIdx === 2)
                                    return inset
                                return zone.width - width - inset
                            }
                            y: {
                                if (!zone.isHorizontalEdge)
                                    return (zone.height - height) / 2
                                if (zone.edgeIdx === 0)
                                    return inset
                                return zone.height - height - inset
                            }

                            QQC2.Label {
                                id: edgeLabel
                                anchors.centerIn: parent
                                text: zone.text
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                font.bold: zone.isSelected
                                color: zone.isSelected ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                                Accessible.ignored: true
                            }

                            // Keyboard focus ring, separated from the pill by a small gap
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -Math.round(Kirigami.Units.smallSpacing * 0.75)
                                radius: height / 2
                                color: "transparent"
                                border.color: Kirigami.Theme.highlightColor
                                border.width: 2
                                visible: zone.visualFocus
                            }

                            // A selected pill can extend past its zone into the
                            // free middle of the screen; keep it clickable there.
                            MouseArea {
                                id: pillMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: edgeGroup.select(zone.edgeIdx, Qt.MouseFocusReason)
                            }
                        }
                    }
                }
            }

            // Measurement overlay: the corner radius is measured on the dock
            // panel; while the Floating row is hovered and the dock floats,
            // the gap between panel and screen edge is measured instead.
            measureTarget: radiusSlider.active ? dock.panelItem
                : (floatingRow.hovered || floatingRow.visualFocus) && dock.floating ? dock.floatingMarker
                : null
            measureOrientation: radiusSlider.active
                ? (dock.vertical ? Qt.Vertical : Qt.Horizontal)
                : (dock.vertical ? Qt.Horizontal : Qt.Vertical)
            measureText: radiusSlider.active
                ? i18nc("@label pixels", "%1 px", DockSettings.cornerRadius)
                : i18nc("@label pixels", "%1 px", Math.round(dock.floatPx / Math.max(dock.unit, 0.001)))
        }
    }

    // --- Position ---
    FormCard.FormHeader {
        title: i18n("Position")
    }

    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            id: floatingRow
            text: i18n("Floating")
            description: i18n("Detach the dock from the screen edge")
            checked: DockSettings.floating
            onToggled: DockSettings.floating = checked
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
            id: radiusSlider
            text: i18n("Corner radius")
            from: 0
            to: 24
            stepSize: 1
            value: DockSettings.cornerRadius
            valueText: i18nc("@label pixels", "%1 px", value)
            onMoved: (value) => DockSettings.cornerRadius = Math.round(value)
        }
    }
}
