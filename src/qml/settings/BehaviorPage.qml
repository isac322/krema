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

    FormCard.FormHeader {
        title: i18n("Behavior")
    }

    FormCard.FormCard {
        FormCard.FormComboBoxDelegate {
            text: i18n("Visibility mode")
            displayMode: FormCard.FormComboBoxDelegate.Dialog
            model: [
                i18n("Always visible"),
                i18n("Auto hide"),
                i18n("Dodge windows")
            ]
            currentIndex: DockSettings.visibilityMode
            onActivated: function(index) {
                DockSettings.visibilityMode = index
            }
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.visibilityMode === 2
        }

        FormCard.FormSwitchDelegate {
            text: i18n("Only dodge active window")
            description: i18n("When off, hides for any overlapping window")
            checked: DockSettings.dodgeActiveOnly
            onCheckedChanged: DockSettings.dodgeActiveOnly = checked
            visible: DockSettings.visibilityMode === 2
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
            currentIndex: DockSettings.edge
            onActivated: function(index) {
                DockSettings.edge = index
            }
        }

        // Show/hide delay controls — only visible in hide-capable modes
        FormCard.FormDelegateSeparator {
            visible: DockSettings.visibilityMode !== 0
        }

        FormCard.FormSpinBoxDelegate {
            label: i18n("Show delay (ms)")
            from: 0; to: 2000; stepSize: 50
            value: DockSettings.showDelay
            onValueChanged: DockSettings.showDelay = value
            visible: DockSettings.visibilityMode !== 0
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.visibilityMode !== 0
        }

        FormCard.FormSpinBoxDelegate {
            label: i18n("Hide delay (ms)")
            from: 0; to: 2000; stepSize: 50
            value: DockSettings.hideDelay
            onValueChanged: DockSettings.hideDelay = value
            visible: DockSettings.visibilityMode !== 0
        }
    }

    FormCard.FormHeader {
        title: i18n("Multi-Monitor")
    }

    FormCard.FormCard {
        FormCard.FormComboBoxDelegate {
            text: i18n("Monitor mode")
            displayMode: FormCard.FormComboBoxDelegate.Dialog
            model: [
                i18n("Primary monitor only"),
                i18n("All monitors"),
                i18n("Follow active screen")
            ]
            currentIndex: DockSettings.monitorMode
            onActivated: function(index) {
                DockSettings.monitorMode = index
            }
        }
    }

    FormCard.FormDelegateSeparator {
        visible: DockSettings.monitorMode === 2
    }

    FormCard.FormCard {
        visible: DockSettings.monitorMode === 2

        FormCard.FormComboBoxDelegate {
            text: i18n("Follow trigger")
            displayMode: FormCard.FormComboBoxDelegate.Dialog
            model: [
                i18n("Mouse position"),
                i18n("Active window focus"),
                i18n("Composite (focus + mouse)")
            ]
            currentIndex: DockSettings.followActiveTrigger
            onActivated: function(index) {
                DockSettings.followActiveTrigger = index
            }
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormComboBoxDelegate {
            text: i18n("Screen transition")
            displayMode: FormCard.FormComboBoxDelegate.Dialog
            model: [
                i18n("Fade"),
                i18n("Slide"),
                i18n("Instant")
            ]
            currentIndex: DockSettings.screenTransition
            onActivated: function(index) {
                DockSettings.screenTransition = index
            }
        }
    }

    FormCard.FormHeader {
        title: i18n("Virtual Desktops")
    }

    FormCard.FormCard {
        FormCard.FormComboBoxDelegate {
            text: i18n("Display mode")
            displayMode: FormCard.FormComboBoxDelegate.Dialog
            model: [
                i18n("Show all windows"),
                i18n("Dim other desktops"),
                i18n("Current desktop only")
            ]
            currentIndex: DockSettings.virtualDesktopMode
            onActivated: function(index) {
                DockSettings.virtualDesktopMode = index
            }
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.virtualDesktopMode === 1
        }

        FormCard.AbstractFormDelegate {
            id: dimOpacityDelegate
            visible: DockSettings.virtualDesktopMode === 1
            Accessible.name: i18n("Other desktop opacity")
            background: null

            contentItem: ColumnLayout {
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label {
                        text: i18n("Other desktop opacity")
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        color: dimOpacityDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    }

                    QQC2.Label {
                        text: Math.round(dimOpacitySlider.value * 100) + "%"
                        color: Kirigami.Theme.disabledTextColor
                    }
                }

                QQC2.Slider {
                    id: dimOpacitySlider
                    Layout.fillWidth: true
                    from: 0.1; to: 0.9; stepSize: 0.05
                    value: DockSettings.otherDesktopOpacity
                    onMoved: DockSettings.otherDesktopOpacity = value
                    Accessible.name: i18n("Other desktop opacity")
                }
            }
        }
    }
}
