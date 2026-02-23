// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard

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
