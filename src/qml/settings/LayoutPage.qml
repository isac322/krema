// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

FormCard.FormCardPage {
    title: i18n("Layout & Position")

    // Responsive vertical padding: scales with page width
    topPadding: Math.round(Kirigami.Units.gridUnit * Math.max(0.5, Math.min(1.5, width / 800)))
    bottomPadding: topPadding

    // --- Monitor Schematic ---
    FormCard.FormCard {
        FormCard.AbstractFormDelegate {
            background: null
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
                            color: Qt.rgba(Kirigami.Theme.textColor.r,
                                           Kirigami.Theme.textColor.g,
                                           Kirigami.Theme.textColor.b, 0.12)

                            // Screen area (inner)
                            Rectangle {
                                id: screenArea
                                anchors.fill: parent
                                anchors.margins: 6
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
                            color: Qt.rgba(Kirigami.Theme.textColor.r,
                                           Kirigami.Theme.textColor.g,
                                           Kirigami.Theme.textColor.b, 0.10)
                        }

                        // Stand base
                        Rectangle {
                            id: stand
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: standNeck.bottom
                            width: Kirigami.Units.gridUnit * 5
                            height: Kirigami.Units.gridUnit * 0.4
                            radius: 2
                            color: Qt.rgba(Kirigami.Theme.textColor.r,
                                           Kirigami.Theme.textColor.g,
                                           Kirigami.Theme.textColor.b, 0.10)
                        }
                    }

                    // Dock inside screen area
                    Item {
                        id: dockRepr
                        property int edge: DockSettings.edge
                        property bool isHorizontal: edge < 2
                        property bool floating: DockSettings.floating
                        property real floatMargin: floating ? 6 : 2
                        // Dock dimensions: sized so internal padding looks equal on all sides.
                        // 5 icons × 14px + 4 gaps × 4px = 86px content.
                        // Thickness 36px → (36-14)/2 = 11px padding on thin axis.
                        // Length = 86 + 11*2 = 108px → ~equal padding on all 4 sides.
                        property real dockThickness: 36
                        property real dockContentLength: 5 * 14 + 4 * 4  // 86px
                        property real dockPadding: (dockThickness - 14) / 2  // 11px
                        property real dockLength: dockContentLength + dockPadding * 2

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

                        // Dock panel background
                        Rectangle {
                            anchors.fill: parent
                            radius: DockSettings.cornerRadius * 0.5
                            color: Kirigami.Theme.highlightColor
                            opacity: 0.15
                            border.color: Kirigami.Theme.highlightColor
                            border.width: 1.5
                        }

                        // Mini icons inside the dock
                        Flow {
                            anchors.centerIn: parent
                            flow: dockRepr.isHorizontal ? Flow.LeftToRight : Flow.TopToBottom
                            spacing: 4

                            Repeater {
                                model: 5
                                Rectangle {
                                    width: 14
                                    height: 14
                                    radius: 3
                                    color: Kirigami.Theme.highlightColor
                                    opacity: 0.5
                                }
                            }
                        }
                    }

                    // Click zones on screen area for 4 edges
                    MouseArea {
                        parent: screenArea
                        width: parent.width; height: parent.height * 0.25
                        x: 0; y: 0
                        cursorShape: Qt.PointingHandCursor
                        onClicked: DockSettings.edge = 0
                    }
                    MouseArea {
                        parent: screenArea
                        width: parent.width; height: parent.height * 0.25
                        x: 0; y: parent.height * 0.75
                        cursorShape: Qt.PointingHandCursor
                        onClicked: DockSettings.edge = 1
                    }
                    MouseArea {
                        parent: screenArea
                        width: parent.width * 0.25; height: parent.height * 0.5
                        x: 0; y: parent.height * 0.25
                        cursorShape: Qt.PointingHandCursor
                        onClicked: DockSettings.edge = 2
                    }
                    MouseArea {
                        parent: screenArea
                        width: parent.width * 0.25; height: parent.height * 0.5
                        x: parent.width * 0.75; y: parent.height * 0.25
                        cursorShape: Qt.PointingHandCursor
                        onClicked: DockSettings.edge = 3
                    }

                    // Clickable edge labels
                    Repeater {
                        model: ListModel {
                            ListElement { edgeIdx: 0; label: "Top"; anchorH: "center"; anchorV: "top" }
                            ListElement { edgeIdx: 1; label: "Bottom"; anchorH: "center"; anchorV: "bottom" }
                            ListElement { edgeIdx: 2; label: "Left"; anchorH: "left"; anchorV: "center" }
                            ListElement { edgeIdx: 3; label: "Right"; anchorH: "right"; anchorV: "center" }
                        }

                        // Pill-shaped clickable label
                        Rectangle {
                            required property int edgeIdx
                            required property string label
                            required property string anchorH
                            required property string anchorV

                            parent: screenArea
                            property bool isSelected: DockSettings.edge === edgeIdx
                            width: edgeLabel.implicitWidth + Kirigami.Units.largeSpacing * 2
                            height: edgeLabel.implicitHeight + Kirigami.Units.smallSpacing * 2
                            radius: height / 2
                            color: isSelected ? Kirigami.Theme.highlightColor : "transparent"
                            opacity: isSelected ? 1.0 : (edgeMouse.containsMouse ? 0.8 : 0.6)

                            // Position: shifts away from dock when selected
                            x: {
                                if (anchorH === "center") return (screenArea.width - width) / 2
                                if (anchorH === "left") {
                                    // If dock is on left edge, move label inward past dock
                                    if (isSelected) return dockRepr.dockThickness + dockRepr.floatMargin + Kirigami.Units.smallSpacing
                                    return Kirigami.Units.smallSpacing
                                }
                                // right
                                if (isSelected) return screenArea.width - width - dockRepr.dockThickness - dockRepr.floatMargin - Kirigami.Units.smallSpacing
                                return screenArea.width - width - Kirigami.Units.smallSpacing
                            }
                            y: {
                                if (anchorV === "center") return (screenArea.height - height) / 2
                                if (anchorV === "top") {
                                    if (isSelected) return dockRepr.dockThickness + dockRepr.floatMargin + Kirigami.Units.smallSpacing
                                    return Kirigami.Units.smallSpacing
                                }
                                // bottom
                                if (isSelected) return screenArea.height - height - dockRepr.dockThickness - dockRepr.floatMargin - Kirigami.Units.smallSpacing
                                return screenArea.height - height - Kirigami.Units.smallSpacing
                            }

                            QQC2.Label {
                                id: edgeLabel
                                anchors.centerIn: parent
                                text: i18n(label)
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                font.bold: isSelected
                                color: isSelected ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                            }

                            MouseArea {
                                id: edgeMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: DockSettings.edge = edgeIdx
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

        FormCard.AbstractFormDelegate {
            id: cornerRadiusDelegate
            Accessible.name: i18n("Corner radius")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Corner radius")
                        color: cornerRadiusDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    }
                    QQC2.Label {
                        text: cornerSlider.value + "px"
                        color: Kirigami.Theme.disabledTextColor
                    }
                }
                QQC2.Slider {
                    id: cornerSlider
                    Layout.fillWidth: true
                    from: 0; to: 24; stepSize: 1
                    value: DockSettings.cornerRadius
                    onMoved: DockSettings.cornerRadius = value
                    Accessible.name: i18n("Corner radius")
                }
            }
        }
    }
}
