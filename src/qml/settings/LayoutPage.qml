// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

FormCard.FormCardPage {
    id: page
    title: i18n("Layout & Position")

    // Responsive vertical padding: scales with page width
    topPadding: Math.round(Kirigami.Units.gridUnit * Math.max(0.5, Math.min(1.5, width / 800)))
    bottomPadding: topPadding

    // Edge indices follow the kcfg Edge enum: 0 Top, 1 Bottom, 2 Left, 3 Right.
    readonly property var edgeNames: [
        i18n("Top"),
        i18n("Bottom"),
        i18n("Left"),
        i18n("Right")
    ]

    // --- Monitor Schematic (Screen edge picker) ---
    FormCard.FormCard {
        FormCard.AbstractFormDelegate {
            background: null
            // Focus belongs to the edge zones, not to this row.
            focusPolicy: Qt.NoFocus
            activeFocusOnTab: false
            // The row is the radio group holding the four edge zones.
            Accessible.role: Accessible.Grouping
            Accessible.name: i18n("Screen edge")

            contentItem: Item {
                implicitHeight: monitorSchematic.height + Kirigami.Units.largeSpacing * 2

                Item {
                    id: monitorSchematic
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    width: Kirigami.Units.gridUnit * 18
                    height: Kirigami.Units.gridUnit * 12

                    // Monitor: bezel + screen + stand
                    Item {
                        id: monitor
                        anchors.fill: parent

                        // Monitor bezel (rounded, dark)
                        Rectangle {
                            id: bezel
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: parent.height - stand.height - standNeck.height
                            radius: Kirigami.Units.largeSpacing
                            color: Qt.alpha(Kirigami.Theme.textColor, 0.12)

                            // Screen area (inner)
                            Rectangle {
                                id: screenArea
                                anchors.fill: parent
                                anchors.margins: Math.round(Kirigami.Units.smallSpacing * 1.5)
                                radius: Kirigami.Units.smallSpacing
                                color: Kirigami.Theme.alternateBackgroundColor
                                clip: true
                            }
                        }

                        // Stand neck
                        Rectangle {
                            id: standNeck
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: bezel.bottom
                            width: Kirigami.Units.gridUnit * 2
                            height: Kirigami.Units.gridUnit * 0.5
                            color: Qt.alpha(Kirigami.Theme.textColor, 0.10)
                        }

                        // Stand base
                        Rectangle {
                            id: stand
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: standNeck.bottom
                            width: Kirigami.Units.gridUnit * 5
                            height: Kirigami.Units.gridUnit * 0.4
                            radius: Kirigami.Units.smallSpacing / 2
                            color: Qt.alpha(Kirigami.Theme.textColor, 0.10)
                        }
                    }

                    // Dock inside screen area
                    Item {
                        id: dockRepr
                        property int edge: DockSettings.edge
                        property bool isHorizontal: edge < 2
                        // Attached docks touch the screen edge; floating docks keep a gap.
                        property real floatMargin: DockSettings.floating ? Kirigami.Units.largeSpacing : 0
                        // Sized so the internal padding is equal on all sides:
                        // 5 icons + 4 gaps along the dock, icon centred across it.
                        property real iconSize: Math.round(Kirigami.Units.gridUnit * 0.875)
                        property real iconGap: Kirigami.Units.smallSpacing
                        property real dockThickness: Math.round(Kirigami.Units.gridUnit * 2.25)
                        property real dockPadding: (dockThickness - iconSize) / 2
                        property real dockLength: 5 * iconSize + 4 * iconGap + dockPadding * 2

                        // Map position relative to screenArea
                        parent: screenArea

                        states: [
                            State {
                                name: "top"; when: dockRepr.edge === 0
                                PropertyChanges {
                                    target: dockRepr
                                    width: dockRepr.dockLength; height: dockRepr.dockThickness
                                    x: (screenArea.width - width) / 2; y: dockRepr.floatMargin
                                }
                            },
                            State {
                                name: "bottom"; when: dockRepr.edge === 1
                                PropertyChanges {
                                    target: dockRepr
                                    width: dockRepr.dockLength; height: dockRepr.dockThickness
                                    x: (screenArea.width - width) / 2
                                    y: screenArea.height - dockRepr.dockThickness - dockRepr.floatMargin
                                }
                            },
                            State {
                                name: "left"; when: dockRepr.edge === 2
                                PropertyChanges {
                                    target: dockRepr
                                    width: dockRepr.dockThickness; height: dockRepr.dockLength
                                    x: dockRepr.floatMargin; y: (screenArea.height - height) / 2
                                }
                            },
                            State {
                                name: "right"; when: dockRepr.edge === 3
                                PropertyChanges {
                                    target: dockRepr
                                    width: dockRepr.dockThickness; height: dockRepr.dockLength
                                    x: screenArea.width - dockRepr.dockThickness - dockRepr.floatMargin
                                    y: (screenArea.height - height) / 2
                                }
                            }
                        ]

                        // Rough panel tint: custom tint color, accent color or system color.
                        readonly property color panelBaseColor: !DockSettings.useSystemColor
                            ? DockSettings.tintColor
                            : (DockSettings.useAccentColor ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor)

                        // Dock panel background. The real radius is scaled down to the
                        // schematic (mini icons are roughly a third of a real icon).
                        Rectangle {
                            anchors.fill: parent
                            radius: Math.min(DockSettings.cornerRadius * 0.5, Math.min(width, height) / 2)
                            color: Qt.alpha(dockRepr.panelBaseColor, 0.10 + 0.25 * DockSettings.backgroundOpacity)
                            border.color: Kirigami.Theme.highlightColor
                            border.width: 1.5
                        }

                        // Mini icons inside the dock
                        Flow {
                            anchors.centerIn: parent
                            flow: dockRepr.isHorizontal ? Flow.LeftToRight : Flow.TopToBottom
                            spacing: dockRepr.iconGap

                            Repeater {
                                model: [
                                    "system-file-manager",
                                    "internet-web-browser",
                                    "utilities-terminal",
                                    "accessories-text-editor",
                                    "preferences-system"
                                ]
                                Kirigami.Icon {
                                    required property string modelData
                                    width: dockRepr.iconSize
                                    height: dockRepr.iconSize
                                    source: modelData
                                    Accessible.ignored: true
                                }
                            }
                        }
                    }

                    // Screen edge picker: four focusable edge zones on the screen area,
                    // exposed as radio buttons inside the "Screen edge" group above.
                    // Tab enters at the selected edge; arrow keys move the selection
                    // spatially; Space/Return select.
                    Item {
                        id: edgeGroup
                        parent: screenArea
                        anchors.fill: parent

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

                                // Subtle hover feedback over the whole zone
                                background: Rectangle {
                                    color: Kirigami.Theme.highlightColor
                                    opacity: zone.hovered && !zone.isSelected ? 0.08 : 0
                                }

                                contentItem: Item {}

                                // Pill-shaped label; shifts inward past the dock when selected.
                                Rectangle {
                                    id: pill
                                    readonly property real inset: zone.isSelected
                                        ? dockRepr.dockThickness + dockRepr.floatMargin + Kirigami.Units.smallSpacing
                                        : Kirigami.Units.smallSpacing

                                    width: edgeLabel.implicitWidth + Kirigami.Units.largeSpacing * 2
                                    height: edgeLabel.implicitHeight + Kirigami.Units.smallSpacing * 2
                                    radius: height / 2
                                    color: zone.isSelected ? Kirigami.Theme.highlightColor : "transparent"
                                    opacity: zone.isSelected || zone.visualFocus ? 1.0 : (zone.hovered || pillMouse.containsMouse ? 0.8 : 0.6)

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
                }
            }
        }
    }

    // --- Position ---
    FormCard.FormHeader {
        title: i18n("Position")
    }

    FormSection {
        FormCard.FormSwitchDelegate {
            text: i18n("Floating")
            description: i18n("Detach the dock from the screen edge")
            checked: DockSettings.floating
            onToggled: DockSettings.floating = checked
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
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
