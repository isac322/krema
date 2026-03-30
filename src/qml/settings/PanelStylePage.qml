// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

FormCard.FormCardPage {
    title: i18n("Panel Style")

    // Responsive vertical padding: scales with page width
    topPadding: Math.round(Kirigami.Units.gridUnit * Math.max(0.5, Math.min(1.5, width / 800)))
    bottomPadding: topPadding

    // --- Style Card Picker ---
    FormCard.FormHeader {
        title: i18n("Background Style")
    }

    FormSection {
        FormCard.AbstractFormDelegate {
            background: null
            contentItem: GridLayout {
                columns: 2
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.largeSpacing

                Repeater {
                    model: ListModel {
                        ListElement { styleIndex: 0; styleName: "Panel Inherit"; styleDesc: "Matches Plasma panel" }
                        ListElement { styleIndex: 1; styleName: "Transparent"; styleDesc: "Fully transparent" }
                        ListElement { styleIndex: 2; styleName: "Tinted"; styleDesc: "Custom color overlay" }
                        ListElement { styleIndex: 3; styleName: "Acrylic"; styleDesc: "Frosted glass blur" }
                    }

                    Rectangle {
                        required property int styleIndex
                        required property string styleName
                        required property string styleDesc

                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 5
                        radius: Kirigami.Units.smallSpacing
                        color: Kirigami.Theme.backgroundColor
                        border.color: DockSettings.backgroundStyle === styleIndex
                            ? Kirigami.Theme.highlightColor
                            : "transparent"
                        border.width: DockSettings.backgroundStyle === styleIndex ? 2 : 0
                        opacity: DockView.isStyleAvailable(styleIndex) ? 1.0 : 0.4

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: Kirigami.Units.smallSpacing

                            // Mini dock preview with this style
                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                width: Kirigami.Units.gridUnit * 6
                                height: Kirigami.Units.gridUnit * 1.5
                                radius: DockSettings.cornerRadius * 0.2
                                color: {
                                    switch (styleIndex) {
                                    case 0: return Kirigami.Theme.backgroundColor
                                    case 1: return "transparent"
                                    case 2: return DockSettings.useSystemColor
                                        ? (Kirigami.Theme.headerBackgroundColor ?? Kirigami.Theme.backgroundColor)
                                        : DockSettings.tintColor
                                    case 3: return Qt.rgba(
                                        Kirigami.Theme.backgroundColor.r,
                                        Kirigami.Theme.backgroundColor.g,
                                        Kirigami.Theme.backgroundColor.b,
                                        0.7)
                                    }
                                    return Kirigami.Theme.backgroundColor
                                }
                                opacity: styleIndex === 1 ? 0.3 : DockSettings.backgroundOpacity
                                border.color: styleIndex === 1 ? Kirigami.Theme.separatorColor ?? Kirigami.Theme.disabledTextColor : "transparent"
                                border.width: styleIndex === 1 ? 1 : 0

                                Row {
                                    anchors.centerIn: parent
                                    spacing: 3
                                    Repeater {
                                        model: 4
                                        Rectangle {
                                            width: 8; height: 8; radius: 2
                                            color: Kirigami.Theme.textColor
                                            opacity: 0.6
                                        }
                                    }
                                }
                            }

                            QQC2.Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: i18n(styleName)
                                font.bold: DockSettings.backgroundStyle === styleIndex
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (DockView.isStyleAvailable(styleIndex)) {
                                    DockSettings.backgroundStyle = styleIndex
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Opacity ---
    FormCard.FormHeader {
        title: i18n("Appearance")
        visible: DockSettings.backgroundStyle !== 1
    }

    FormSection {
        visible: DockSettings.backgroundStyle !== 1

        FormCard.AbstractFormDelegate {
            id: opacityDelegate
            Accessible.name: i18n("Opacity")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Opacity")
                        color: opacityDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    }
                    QQC2.Label {
                        text: Math.round(opacitySlider.value * 100) + "%"
                        color: Kirigami.Theme.disabledTextColor
                    }
                }
                QQC2.Slider {
                    id: opacitySlider
                    Layout.fillWidth: true
                    from: 0.0; to: 1.0; stepSize: 0.05
                    value: DockSettings.backgroundOpacity
                    onMoved: DockSettings.backgroundOpacity = value
                    Accessible.name: i18n("Opacity")
                }
            }
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.backgroundStyle === 2
        }

        FormCard.FormSwitchDelegate {
            visible: DockSettings.backgroundStyle === 2
            text: i18n("Use system color")
            description: i18n("Use the system header color instead of a custom tint color")
            checked: DockSettings.useSystemColor
            onToggled: DockSettings.useSystemColor = checked
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.backgroundStyle === 2 && !DockSettings.useSystemColor
        }

        FormCard.AbstractFormDelegate {
            id: tintDelegate
            visible: DockSettings.backgroundStyle === 2 && !DockSettings.useSystemColor
            background: null
            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing
                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Tint color")
                    color: tintDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                }
                Rectangle {
                    width: Kirigami.Units.gridUnit * 2
                    height: Kirigami.Units.gridUnit * 1.5
                    radius: Kirigami.Units.smallSpacing
                    color: DockSettings.tintColor
                    border.color: Kirigami.Theme.disabledTextColor
                    border.width: 1
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: tintColorDialog.open()
                    }
                }
            }
        }

        ColorDialog {
            id: tintColorDialog
            title: i18n("Choose tint color")
            selectedColor: DockSettings.tintColor
            onAccepted: DockSettings.tintColor = selectedColor
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.backgroundStyle !== 1
                     && (DockSettings.backgroundStyle !== 2 || DockSettings.useSystemColor)
        }

        FormCard.FormSwitchDelegate {
            visible: DockSettings.backgroundStyle !== 1
                     && (DockSettings.backgroundStyle !== 2 || DockSettings.useSystemColor)
            text: i18n("Use accent color")
            description: i18n("Use the system accent color instead of the default panel color")
            checked: DockSettings.useAccentColor
            onToggled: DockSettings.useAccentColor = checked
        }
    }
}
