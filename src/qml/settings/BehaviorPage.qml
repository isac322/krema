// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

FormCard.FormCardPage {
    title: i18n("Behavior")

    // Responsive vertical padding: scales with page width
    topPadding: Math.round(Kirigami.Units.gridUnit * Math.max(0.5, Math.min(1.5, width / 800)))
    bottomPadding: topPadding

    // --- Visibility Mode Card Picker ---
    FormCard.FormHeader {
        title: i18n("Visibility")
    }

    FormSection {
        FormCard.AbstractFormDelegate {
            background: null
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Repeater {
                    model: ListModel {
                        ListElement { modeIndex: 0; modeName: "Always Visible"; modeDesc: "Dock is always shown" }
                        ListElement { modeIndex: 1; modeName: "Auto Hide"; modeDesc: "Hides after inactivity" }
                        ListElement { modeIndex: 2; modeName: "Dodge Windows"; modeDesc: "Hides when windows overlap" }
                    }

                    Rectangle {
                        required property int modeIndex
                        required property string modeName
                        required property string modeDesc

                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 6
                        radius: Kirigami.Units.smallSpacing
                        color: Kirigami.Theme.backgroundColor
                        border.color: DockSettings.visibilityMode === modeIndex
                            ? Kirigami.Theme.highlightColor
                            : "transparent"
                        border.width: DockSettings.visibilityMode === modeIndex ? 2 : 0

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: Kirigami.Units.smallSpacing

                            // Mini animated preview
                            Item {
                                Layout.alignment: Qt.AlignHCenter
                                width: Kirigami.Units.gridUnit * 5
                                height: Kirigami.Units.gridUnit * 3

                                // Monitor
                                Rectangle {
                                    anchors.fill: parent
                                    color: "transparent"
                                    border.color: Kirigami.Theme.textColor
                                    border.width: 1
                                    radius: 2

                                    // Window (for dodge mode)
                                    Rectangle {
                                        id: windowRepr
                                        visible: modeIndex === 2
                                        width: parent.width * 0.5
                                        height: parent.height * 0.6
                                        x: (parent.width - width) / 2
                                        color: Kirigami.Theme.alternateBackgroundColor
                                        border.color: Kirigami.Theme.separatorColor ?? Kirigami.Theme.disabledTextColor
                                        border.width: 1
                                        radius: 1

                                        SequentialAnimation on y {
                                            running: DockSettings.visibilityMode === modeIndex && modeIndex === 2
                                            loops: Animation.Infinite
                                            NumberAnimation { from: 2; to: parent.height * 0.4; duration: 1000; easing.type: Easing.InOutQuad }
                                            PauseAnimation { duration: 800 }
                                            NumberAnimation { from: parent.height * 0.4; to: 2; duration: 1000; easing.type: Easing.InOutQuad }
                                            PauseAnimation { duration: 800 }
                                        }
                                    }

                                    // Dock bar
                                    Rectangle {
                                        id: miniDock
                                        width: parent.width * 0.4
                                        height: 4
                                        x: (parent.width - width) / 2
                                        color: Kirigami.Theme.highlightColor
                                        radius: 1

                                        property real baseY: parent.height - height - 2
                                        property real hiddenY: parent.height + 2
                                        y: baseY

                                        // AutoHide animation
                                        SequentialAnimation on y {
                                            running: DockSettings.visibilityMode === modeIndex && modeIndex === 1
                                            loops: Animation.Infinite
                                            PauseAnimation { duration: 1500 }
                                            NumberAnimation { to: miniDock.hiddenY; duration: 300; easing.type: Easing.InQuad }
                                            PauseAnimation { duration: 1000 }
                                            NumberAnimation { to: miniDock.baseY; duration: 300; easing.type: Easing.OutQuad }
                                        }

                                        // Dodge animation
                                        SequentialAnimation on y {
                                            running: DockSettings.visibilityMode === modeIndex && modeIndex === 2
                                            loops: Animation.Infinite
                                            PauseAnimation { duration: 1000 }
                                            NumberAnimation { to: miniDock.hiddenY; duration: 200; easing.type: Easing.InQuad }
                                            PauseAnimation { duration: 1600 }
                                            NumberAnimation { to: miniDock.baseY; duration: 200; easing.type: Easing.OutQuad }
                                            PauseAnimation { duration: 600 }
                                        }
                                    }
                                }
                            }

                            QQC2.Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: i18n(modeName)
                                font.bold: DockSettings.visibilityMode === modeIndex
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: DockSettings.visibilityMode = modeIndex
                        }
                    }
                }
            }
        }

        // Dodge-only options
        FormCard.FormDelegateSeparator { visible: DockSettings.visibilityMode === 2 }

        FormCard.FormSwitchDelegate {
            visible: DockSettings.visibilityMode === 2
            text: i18n("Only dodge active window")
            description: i18n("When off, hides for any overlapping window")
            checked: DockSettings.dodgeActiveOnly
            onCheckedChanged: DockSettings.dodgeActiveOnly = checked
        }

        // Delay controls (AutoHide/Dodge)
        FormCard.FormDelegateSeparator { visible: DockSettings.visibilityMode !== 0 }

        FormCard.AbstractFormDelegate {
            visible: DockSettings.visibilityMode !== 0
            Accessible.name: i18n("Show delay")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label { Layout.fillWidth: true; text: i18n("Show delay") }
                    QQC2.Label { text: showDelaySlider.value + "ms"; color: Kirigami.Theme.disabledTextColor }
                }
                QQC2.Slider {
                    id: showDelaySlider
                    Layout.fillWidth: true
                    from: 0; to: 2000; stepSize: 50
                    value: DockSettings.showDelay
                    onMoved: DockSettings.showDelay = value
                }
            }
        }

        FormCard.FormDelegateSeparator { visible: DockSettings.visibilityMode !== 0 }

        FormCard.AbstractFormDelegate {
            visible: DockSettings.visibilityMode !== 0
            Accessible.name: i18n("Hide delay")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label { Layout.fillWidth: true; text: i18n("Hide delay") }
                    QQC2.Label { text: hideDelaySlider.value + "ms"; color: Kirigami.Theme.disabledTextColor }
                }
                QQC2.Slider {
                    id: hideDelaySlider
                    Layout.fillWidth: true
                    from: 0; to: 2000; stepSize: 50
                    value: DockSettings.hideDelay
                    onMoved: DockSettings.hideDelay = value
                }
            }
        }
    }

    // --- Multi-Monitor ---
    FormCard.FormHeader {
        title: i18n("Multi-Monitor")
    }

    FormSection {
        FormCard.AbstractFormDelegate {
            background: null
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Repeater {
                    model: ListModel {
                        ListElement { modeIdx: 0; name: "Primary Only" }
                        ListElement { modeIdx: 1; name: "All Monitors" }
                        ListElement { modeIdx: 2; name: "Follow Active" }
                    }

                    Rectangle {
                        required property int modeIdx
                        required property string name

                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 5
                        radius: Kirigami.Units.smallSpacing
                        color: Kirigami.Theme.backgroundColor
                        border.color: DockSettings.monitorMode === modeIdx
                            ? Kirigami.Theme.highlightColor
                            : "transparent"
                        border.width: DockSettings.monitorMode === modeIdx ? 2 : 0

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: Kirigami.Units.smallSpacing

                            // Dual monitor diagram
                            Row {
                                Layout.alignment: Qt.AlignHCenter
                                spacing: 3

                                // Monitor 1
                                Rectangle {
                                    width: Kirigami.Units.gridUnit * 2.5; height: Kirigami.Units.gridUnit * 1.8
                                    color: "transparent"; border.color: Kirigami.Theme.textColor; border.width: 1; radius: 1
                                    Rectangle {
                                        width: parent.width * 0.5; height: 3; radius: 1
                                        x: (parent.width - width) / 2; y: parent.height - height - 2
                                        color: (modeIdx === 0 || modeIdx === 1) ? Kirigami.Theme.highlightColor : "transparent"
                                        border.color: modeIdx === 2 ? Kirigami.Theme.disabledTextColor : "transparent"
                                        border.width: modeIdx === 2 ? 1 : 0
                                    }
                                }
                                // Monitor 2
                                Rectangle {
                                    width: Kirigami.Units.gridUnit * 2.5; height: Kirigami.Units.gridUnit * 1.8
                                    color: "transparent"; border.color: Kirigami.Theme.textColor; border.width: 1; radius: 1
                                    Rectangle {
                                        width: parent.width * 0.5; height: 3; radius: 1
                                        x: (parent.width - width) / 2; y: parent.height - height - 2
                                        color: modeIdx === 1 ? Kirigami.Theme.highlightColor : "transparent"
                                        border.color: (modeIdx === 0 || modeIdx === 2) ? Kirigami.Theme.disabledTextColor : "transparent"
                                        border.width: (modeIdx === 0 || modeIdx === 2) ? 1 : 0
                                    }
                                }
                            }

                            QQC2.Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: i18n(name)
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                font.bold: DockSettings.monitorMode === modeIdx
                            }
                        }

                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: DockSettings.monitorMode = modeIdx
                        }
                    }
                }
            }
        }

        // Follow Active options
        FormCard.FormDelegateSeparator { visible: DockSettings.monitorMode === 2 }

        FormCard.FormComboBoxDelegate {
            visible: DockSettings.monitorMode === 2
            text: i18n("Follow trigger")
            displayMode: FormCard.FormComboBoxDelegate.Dialog
            model: [i18n("Mouse position"), i18n("Active window focus"), i18n("Composite (focus + mouse)")]
            currentIndex: DockSettings.followActiveTrigger
            onActivated: function(index) { DockSettings.followActiveTrigger = index }
        }

        FormCard.FormDelegateSeparator { visible: DockSettings.monitorMode === 2 }

        FormCard.FormComboBoxDelegate {
            visible: DockSettings.monitorMode === 2
            text: i18n("Screen transition")
            displayMode: FormCard.FormComboBoxDelegate.Dialog
            model: [i18n("Fade"), i18n("Slide"), i18n("Instant")]
            currentIndex: DockSettings.screenTransition
            onActivated: function(index) { DockSettings.screenTransition = index }
        }
    }

    // --- Virtual Desktops ---
    FormCard.FormHeader {
        title: i18n("Virtual Desktops")
    }

    FormSection {
        FormCard.AbstractFormDelegate {
            background: null
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Repeater {
                    model: ListModel {
                        ListElement { vdIndex: 0; vdName: "Show All" }
                        ListElement { vdIndex: 1; vdName: "Dim Other" }
                        ListElement { vdIndex: 2; vdName: "Current Only" }
                    }

                    Rectangle {
                        required property int vdIndex
                        required property string vdName

                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 5
                        radius: Kirigami.Units.smallSpacing
                        color: Kirigami.Theme.backgroundColor
                        border.color: DockSettings.virtualDesktopMode === vdIndex
                            ? Kirigami.Theme.highlightColor
                            : "transparent"
                        border.width: DockSettings.virtualDesktopMode === vdIndex ? 2 : 0

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: Kirigami.Units.smallSpacing

                            // Mini icon row with dimming
                            Row {
                                Layout.alignment: Qt.AlignHCenter
                                spacing: 3
                                // "Current desktop" icons
                                Rectangle { width: 10; height: 10; radius: 2; color: Kirigami.Theme.textColor }
                                Rectangle { width: 10; height: 10; radius: 2; color: Kirigami.Theme.textColor }
                                // "Other desktop" icons
                                Rectangle {
                                    width: 10; height: 10; radius: 2
                                    color: Kirigami.Theme.textColor
                                    opacity: vdIndex === 0 ? 1.0 : (vdIndex === 1 ? DockSettings.otherDesktopOpacity : 0.0)
                                    visible: vdIndex !== 2
                                }
                                Rectangle {
                                    width: 10; height: 10; radius: 2
                                    color: Kirigami.Theme.textColor
                                    opacity: vdIndex === 0 ? 1.0 : (vdIndex === 1 ? DockSettings.otherDesktopOpacity : 0.0)
                                    visible: vdIndex !== 2
                                }
                            }

                            QQC2.Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: i18n(vdName)
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                font.bold: DockSettings.virtualDesktopMode === vdIndex
                            }
                        }

                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: DockSettings.virtualDesktopMode = vdIndex
                        }
                    }
                }
            }
        }

        FormCard.FormDelegateSeparator { visible: DockSettings.virtualDesktopMode === 1 }

        FormCard.AbstractFormDelegate {
            visible: DockSettings.virtualDesktopMode === 1
            Accessible.name: i18n("Other desktop opacity")
            background: null
            contentItem: ColumnLayout {
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label { text: i18n("Other desktop opacity"); Layout.fillWidth: true }
                    QQC2.Label { text: Math.round(dimSlider.value * 100) + "%"; color: Kirigami.Theme.disabledTextColor }
                }
                QQC2.Slider {
                    id: dimSlider
                    Layout.fillWidth: true
                    from: 0.1; to: 0.9; stepSize: 0.05
                    value: DockSettings.otherDesktopOpacity
                    onMoved: DockSettings.otherDesktopOpacity = value
                }
            }
        }
    }
}
