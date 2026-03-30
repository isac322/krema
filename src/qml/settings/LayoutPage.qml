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

                    // Monitor bezel
                    Rectangle {
                        id: monitor
                        anchors.fill: parent
                        color: "transparent"
                        border.color: Kirigami.Theme.textColor
                        border.width: 2
                        radius: Kirigami.Units.smallSpacing

                        // Screen area
                        Rectangle {
                            id: screenArea
                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.smallSpacing * 2
                            color: Kirigami.Theme.backgroundColor
                            radius: 2

                            // Dock representation
                            Rectangle {
                                id: dockRepr
                                radius: DockSettings.cornerRadius * 0.3
                                color: Kirigami.Theme.highlightColor
                                opacity: 0.8

                                property int edge: DockSettings.edge
                                property bool floating: DockSettings.floating
                                property real floatMargin: floating ? 4 : 0

                                // Position based on edge
                                states: [
                                    State {
                                        name: "top"; when: dockRepr.edge === 0
                                        PropertyChanges {
                                            target: dockRepr
                                            width: screenArea.width * 0.4
                                            height: 8
                                            x: (screenArea.width - width) / 2
                                            y: dockRepr.floatMargin
                                        }
                                    },
                                    State {
                                        name: "bottom"; when: dockRepr.edge === 1
                                        PropertyChanges {
                                            target: dockRepr
                                            width: screenArea.width * 0.4
                                            height: 8
                                            x: (screenArea.width - width) / 2
                                            y: screenArea.height - height - dockRepr.floatMargin
                                        }
                                    },
                                    State {
                                        name: "left"; when: dockRepr.edge === 2
                                        PropertyChanges {
                                            target: dockRepr
                                            width: 8
                                            height: screenArea.height * 0.4
                                            x: dockRepr.floatMargin
                                            y: (screenArea.height - height) / 2
                                        }
                                    },
                                    State {
                                        name: "right"; when: dockRepr.edge === 3
                                        PropertyChanges {
                                            target: dockRepr
                                            width: 8
                                            height: screenArea.height * 0.4
                                            x: screenArea.width - width - dockRepr.floatMargin
                                            y: (screenArea.height - height) / 2
                                        }
                                    }
                                ]

                                Behavior on x { NumberAnimation { duration: 300; easing.type: Easing.InOutQuad } }
                                Behavior on y { NumberAnimation { duration: 300; easing.type: Easing.InOutQuad } }
                                Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.InOutQuad } }
                                Behavior on height { NumberAnimation { duration: 300; easing.type: Easing.InOutQuad } }
                            }

                            // Click zones for 4 edges
                            // Top
                            MouseArea {
                                width: parent.width; height: parent.height * 0.25
                                x: 0; y: 0
                                cursorShape: Qt.PointingHandCursor
                                onClicked: DockSettings.edge = 0
                            }
                            // Bottom
                            MouseArea {
                                width: parent.width; height: parent.height * 0.25
                                x: 0; y: parent.height * 0.75
                                cursorShape: Qt.PointingHandCursor
                                onClicked: DockSettings.edge = 1
                            }
                            // Left
                            MouseArea {
                                width: parent.width * 0.25; height: parent.height * 0.5
                                x: 0; y: parent.height * 0.25
                                cursorShape: Qt.PointingHandCursor
                                onClicked: DockSettings.edge = 2
                            }
                            // Right
                            MouseArea {
                                width: parent.width * 0.25; height: parent.height * 0.5
                                x: parent.width * 0.75; y: parent.height * 0.25
                                cursorShape: Qt.PointingHandCursor
                                onClicked: DockSettings.edge = 3
                            }

                            // Edge labels
                            QQC2.Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.top
                                anchors.topMargin: Kirigami.Units.smallSpacing
                                text: i18n("Top")
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                color: DockSettings.edge === 0 ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                                font.bold: DockSettings.edge === 0
                            }
                            QQC2.Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: Kirigami.Units.smallSpacing
                                text: i18n("Bottom")
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                color: DockSettings.edge === 1 ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                                font.bold: DockSettings.edge === 1
                            }
                            QQC2.Label {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: Kirigami.Units.smallSpacing
                                text: i18n("Left")
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                color: DockSettings.edge === 2 ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                                font.bold: DockSettings.edge === 2
                            }
                            QQC2.Label {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                anchors.rightMargin: Kirigami.Units.smallSpacing
                                text: i18n("Right")
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                color: DockSettings.edge === 3 ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                                font.bold: DockSettings.edge === 3
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
