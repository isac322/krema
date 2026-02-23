// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard

FormCard.FormCardPage {
    title: i18n("Appearance")

    FormCard.FormHeader {
        title: i18n("Appearance")
    }

    FormCard.FormCard {
        FormCard.FormSpinBoxDelegate {
            label: i18n("Icon size")
            from: 24; to: 96; stepSize: 4
            value: dockSettings.iconSize
            onValueChanged: dockSettings.iconSize = value
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSpinBoxDelegate {
            label: i18n("Icon spacing")
            from: 0; to: 16
            value: dockSettings.iconSpacing
            onValueChanged: dockSettings.iconSpacing = value
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
                    value: dockSettings.maxZoomFactor
                    onMoved: dockSettings.maxZoomFactor = value
                }
            }
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSpinBoxDelegate {
            label: i18n("Corner radius")
            from: 0; to: 24
            value: dockSettings.cornerRadius
            onValueChanged: dockSettings.cornerRadius = value
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSwitchDelegate {
            text: i18n("Floating")
            checked: dockSettings.floating
            onToggled: dockSettings.floating = checked
        }

        FormCard.FormDelegateSeparator {}

        FormCard.AbstractFormDelegate {
            id: opacityDelegate
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Background opacity")
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
                    from: 0.1; to: 1.0; stepSize: 0.05
                    value: dockSettings.backgroundOpacity
                    onMoved: dockSettings.backgroundOpacity = value
                }
            }
        }
    }
}
