// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard

Kirigami.ApplicationWindow {
    id: settingsWindow

    title: i18n("Krema Settings")
    width: Kirigami.Units.gridUnit * 26
    height: Kirigami.Units.gridUnit * 22
    minimumWidth: Kirigami.Units.gridUnit * 20
    minimumHeight: Kirigami.Units.gridUnit * 16

    // Close window on Escape
    Shortcut {
        sequence: "Escape"
        onActivated: settingsWindow.close()
    }

    pageStack.initialPage: FormCard.FormCardPage {
        title: i18n("Settings")

        // --- Appearance Section ---

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
        }

        // --- Behavior Section ---

        FormCard.FormHeader {
            title: i18n("Behavior")
        }

        FormCard.FormCard {
            FormCard.FormComboBoxDelegate {
                text: i18n("Visibility mode")
                displayMode: FormCard.FormComboBoxDelegate.Dialog
                model: [
                    i18n("Always visible"),
                    i18n("Always hidden"),
                    i18n("Dodge windows"),
                    i18n("Smart hide")
                ]
                currentIndex: dockSettings.visibilityMode
                onActivated: function(index) {
                    dockSettings.visibilityMode = index
                }
            }

            FormCard.FormDelegateSeparator {}

            FormCard.FormComboBoxDelegate {
                text: i18n("Screen edge")
                displayMode: FormCard.FormComboBoxDelegate.Dialog
                model: [
                    i18n("Top"),
                    i18n("Bottom"),
                    i18n("Left"),
                    i18n("Right")
                ]
                currentIndex: dockSettings.edge
                onActivated: function(index) {
                    dockSettings.edge = index
                }
            }

            // Show/hide delay controls — only visible in hide-capable modes
            FormCard.FormDelegateSeparator {
                visible: dockSettings.visibilityMode !== 0
            }

            FormCard.FormSpinBoxDelegate {
                label: i18n("Show delay (ms)")
                from: 0; to: 2000; stepSize: 50
                value: dockSettings.showDelay
                onValueChanged: dockSettings.showDelay = value
                visible: dockSettings.visibilityMode !== 0
            }

            FormCard.FormDelegateSeparator {
                visible: dockSettings.visibilityMode !== 0
            }

            FormCard.FormSpinBoxDelegate {
                label: i18n("Hide delay (ms)")
                from: 0; to: 2000; stepSize: 50
                value: dockSettings.hideDelay
                onValueChanged: dockSettings.hideDelay = value
                visible: dockSettings.visibilityMode !== 0
            }
        }
    }
}
