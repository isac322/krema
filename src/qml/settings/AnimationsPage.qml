// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

FormCard.FormCardPage {
    title: i18n("Animations & Badges")

    // Responsive vertical padding: scales with page width
    topPadding: Math.round(Kirigami.Units.gridUnit * Math.max(0.5, Math.min(1.5, width / 800)))
    bottomPadding: topPadding

    // --- Attention Animation Grid Picker ---
    FormCard.FormHeader {
        title: i18n("Attention Animation")
    }

    FormSection {
        FormCard.AbstractFormDelegate {
            background: null
            contentItem: GridLayout {
                columns: 4
                columnSpacing: Kirigami.Units.smallSpacing
                rowSpacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: ListModel {
                        ListElement { animIndex: 0; animName: "None" }
                        ListElement { animIndex: 1; animName: "Bounce" }
                        ListElement { animIndex: 2; animName: "Wiggle" }
                        ListElement { animIndex: 3; animName: "Pulse" }
                        ListElement { animIndex: 4; animName: "Glow" }
                        ListElement { animIndex: 5; animName: "Dot Color" }
                        ListElement { animIndex: 6; animName: "Blink" }
                    }

                    Rectangle {
                        required property int animIndex
                        required property string animName

                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 5
                        radius: Kirigami.Units.smallSpacing
                        color: Kirigami.Theme.backgroundColor
                        border.color: DockSettings.attentionAnimation === animIndex
                            ? Kirigami.Theme.highlightColor
                            : "transparent"
                        border.width: DockSettings.attentionAnimation === animIndex ? 2 : 0

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: Kirigami.Units.smallSpacing

                            // Sample icon with animation
                            Kirigami.Icon {
                                id: animIcon
                                Layout.alignment: Qt.AlignHCenter
                                source: "preferences-system"
                                implicitWidth: Kirigami.Units.iconSizes.medium
                                implicitHeight: Kirigami.Units.iconSizes.medium

                                // Animation based on type — plays when card is selected
                                property bool isActive: DockSettings.attentionAnimation === animIndex && animIndex > 0

                                // Bounce
                                SequentialAnimation on y {
                                    running: animIcon.isActive && animIndex === 1
                                    loops: Animation.Infinite
                                    NumberAnimation { from: 0; to: -6; duration: 300; easing.type: Easing.OutQuad }
                                    NumberAnimation { from: -6; to: 0; duration: 300; easing.type: Easing.InQuad }
                                    PauseAnimation { duration: 400 }
                                }

                                // Wiggle
                                SequentialAnimation on rotation {
                                    running: animIcon.isActive && animIndex === 2
                                    loops: Animation.Infinite
                                    NumberAnimation { from: 0; to: 10; duration: 80 }
                                    NumberAnimation { from: 10; to: -10; duration: 160 }
                                    NumberAnimation { from: -10; to: 0; duration: 80 }
                                    PauseAnimation { duration: 600 }
                                }

                                // Pulse
                                SequentialAnimation on scale {
                                    running: animIcon.isActive && animIndex === 3
                                    loops: Animation.Infinite
                                    NumberAnimation { from: 1.0; to: 1.2; duration: 400; easing.type: Easing.InOutQuad }
                                    NumberAnimation { from: 1.2; to: 1.0; duration: 400; easing.type: Easing.InOutQuad }
                                    PauseAnimation { duration: 200 }
                                }

                                // Blink
                                SequentialAnimation on opacity {
                                    running: animIcon.isActive && animIndex === 6
                                    loops: Animation.Infinite
                                    NumberAnimation { from: 1.0; to: 0.2; duration: 400 }
                                    NumberAnimation { from: 0.2; to: 1.0; duration: 400 }
                                    PauseAnimation { duration: 200 }
                                }
                            }

                            QQC2.Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: i18n(animName)
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                font.bold: DockSettings.attentionAnimation === animIndex
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: DockSettings.attentionAnimation = animIndex
                        }
                    }
                }
            }
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.attentionAnimation > 0
        }

        FormCard.FormSpinBoxDelegate {
            visible: DockSettings.attentionAnimation > 0
            label: i18n("Duration (seconds)")
            from: 0; to: 60
            value: DockSettings.attentionAnimationDuration
            onValueChanged: DockSettings.attentionAnimationDuration = value
            // 0 = infinite
            textFromValue: function(value) {
                return value === 0 ? i18n("Infinite") : value.toString()
            }
        }
    }

    // --- Badge Display ---
    FormCard.FormHeader {
        title: i18n("Badge Display")
    }

    FormSection {
        FormCard.AbstractFormDelegate {
            background: null
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Repeater {
                    model: ListModel {
                        ListElement { badgeIndex: 0; badgeName: "Number"; badgePreview: "3" }
                        ListElement { badgeIndex: 1; badgeName: "Dot"; badgePreview: "●" }
                        ListElement { badgeIndex: 2; badgeName: "Off"; badgePreview: "" }
                    }

                    Rectangle {
                        required property int badgeIndex
                        required property string badgeName
                        required property string badgePreview

                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 5
                        radius: Kirigami.Units.smallSpacing
                        color: Kirigami.Theme.backgroundColor
                        border.color: DockSettings.badgeDisplayMode === badgeIndex
                            ? Kirigami.Theme.highlightColor
                            : "transparent"
                        border.width: DockSettings.badgeDisplayMode === badgeIndex ? 2 : 0

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: Kirigami.Units.smallSpacing

                            // Icon with badge
                            Item {
                                Layout.alignment: Qt.AlignHCenter
                                width: Kirigami.Units.iconSizes.medium
                                height: Kirigami.Units.iconSizes.medium

                                Kirigami.Icon {
                                    anchors.fill: parent
                                    source: "internet-web-browser"
                                }

                                // Badge overlay
                                Rectangle {
                                    visible: badgePreview !== ""
                                    anchors.top: parent.top
                                    anchors.right: parent.right
                                    anchors.topMargin: -2
                                    anchors.rightMargin: -2
                                    width: badgeIndex === 1 ? 8 : Math.max(14, badgeLabel.implicitWidth + 6)
                                    height: badgeIndex === 1 ? 8 : 14
                                    radius: height / 2
                                    color: Kirigami.Theme.negativeTextColor

                                    QQC2.Label {
                                        id: badgeLabel
                                        visible: badgeIndex === 0
                                        anchors.centerIn: parent
                                        text: badgePreview
                                        font.pointSize: 7
                                        font.bold: true
                                        color: "white"
                                    }
                                }
                            }

                            QQC2.Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: i18n(badgeName)
                                font.bold: DockSettings.badgeDisplayMode === badgeIndex
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: DockSettings.badgeDisplayMode = badgeIndex
                        }
                    }
                }
            }
        }
    }
}
