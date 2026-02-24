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
    title: i18n("Appearance")

    // --- Icons ---
    FormCard.FormHeader {
        title: i18n("Icons")
    }

    FormCard.FormCard {
        FormCard.FormSpinBoxDelegate {
            label: i18n("Icon size")
            from: 24; to: 96; stepSize: 4
            value: DockSettings.iconSize
            onValueChanged: DockSettings.iconSize = value
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSpinBoxDelegate {
            label: i18n("Icon spacing")
            from: 0; to: 16
            value: DockSettings.iconSpacing
            onValueChanged: DockSettings.iconSpacing = value
        }

        FormCard.FormDelegateSeparator {}

        FormCard.AbstractFormDelegate {
            id: zoomDelegate
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Zoom factor")
                        elide: Text.ElideRight
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        color: zoomDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    }

                    QQC2.Label {
                        text: zoomSlider.value.toFixed(1) + "x"
                        color: Kirigami.Theme.disabledTextColor
                    }
                }

                QQC2.Slider {
                    id: zoomSlider
                    Layout.fillWidth: true
                    from: 1.0; to: 2.0; stepSize: 0.1
                    value: DockSettings.maxZoomFactor
                    onMoved: DockSettings.maxZoomFactor = value
                    Accessible.name: i18n("Zoom factor")
                }
            }
        }
    }

    // --- Background ---
    FormCard.FormHeader {
        title: i18n("Background")
    }

    FormCard.FormCard {
        FormCard.FormComboBoxDelegate {
            id: styleCombo
            text: i18n("Style")

            property var styleNames: [
                i18n("Panel Inherit"),
                i18n("Transparent"),
                i18n("Tinted"),
                i18n("Acrylic")
            ]

            model: {
                let items = []
                for (let i = 0; i < styleNames.length; i++) {
                    let name = styleNames[i]
                    if (!DockView.isStyleAvailable(i)) {
                        name += " " + i18n("(unavailable)")
                    }
                    items.push(name)
                }
                return items
            }

            currentIndex: DockSettings.backgroundStyle
            onActivated: function(index) {
                if (!DockView.isStyleAvailable(index)) {
                    // Revert to current setting
                    currentIndex = DockSettings.backgroundStyle
                    return
                }
                DockSettings.backgroundStyle = index
            }
        }

        FormCard.FormDelegateSeparator {}

        // Opacity slider (hidden for Transparent only)
        FormCard.AbstractFormDelegate {
            id: opacityDelegate
            visible: DockSettings.backgroundStyle !== 1
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Opacity")
                        elide: Text.ElideRight
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
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
            visible: DockSettings.backgroundStyle === 2  // Tinted
        }

        // Use system color toggle (only for Tinted style)
        FormCard.FormSwitchDelegate {
            visible: DockSettings.backgroundStyle === 2  // Tinted
            text: i18n("Use system color")
            description: i18n("Use the system header color instead of a custom tint color")
            checked: DockSettings.useSystemColor
            onToggled: DockSettings.useSystemColor = checked
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.backgroundStyle === 2 && !DockSettings.useSystemColor  // Tinted + custom
        }

        // Tint color picker (only for Tinted style with custom color)
        FormCard.AbstractFormDelegate {
            id: tintDelegate
            visible: DockSettings.backgroundStyle === 2 && !DockSettings.useSystemColor
            background: null
            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Tint color")
                    elide: Text.ElideRight
                    color: tintDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                }

                Rectangle {
                    id: colorPreview
                    width: Kirigami.Units.gridUnit * 2
                    height: Kirigami.Units.gridUnit * 1.5
                    radius: Kirigami.Units.smallSpacing
                    color: DockSettings.tintColor
                    border.color: Kirigami.Theme.disabledTextColor
                    border.width: 1

                    Accessible.role: Accessible.Button
                    Accessible.name: i18n("Tint color: %1", DockSettings.tintColor)
                    Accessible.onPressAction: colorDialog.open()

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: colorDialog.open()
                    }
                }
            }
        }

        ColorDialog {
            id: colorDialog
            title: i18n("Choose tint color")
            selectedColor: DockSettings.tintColor
            onAccepted: DockSettings.tintColor = selectedColor
        }

        FormCard.FormDelegateSeparator {
            // UseAccentColor: visible for PanelInherit, Acrylic, or Tinted+UseSystemColor
            visible: DockSettings.backgroundStyle !== 1
                     && (DockSettings.backgroundStyle !== 2 || DockSettings.useSystemColor)
        }

        // Use accent color toggle (PanelInherit, Acrylic, or Tinted+UseSystemColor)
        FormCard.FormSwitchDelegate {
            visible: DockSettings.backgroundStyle !== 1
                     && (DockSettings.backgroundStyle !== 2 || DockSettings.useSystemColor)
            text: i18n("Use accent color")
            description: i18n("Use the system accent color instead of the default panel color")
            checked: DockSettings.useAccentColor
            onToggled: DockSettings.useAccentColor = checked
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSpinBoxDelegate {
            label: i18n("Corner radius")
            from: 0; to: 24
            value: DockSettings.cornerRadius
            onValueChanged: DockSettings.cornerRadius = value
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSwitchDelegate {
            text: i18n("Floating")
            checked: DockSettings.floating
            onToggled: DockSettings.floating = checked
        }
    }
}
