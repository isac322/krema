// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard

FormCard.FormCardPage {
    title: i18n("Window Preview")

    FormCard.FormHeader {
        title: i18n("Window Preview")
    }

    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            id: previewEnabledSwitch
            text: i18n("Enable window preview")
            description: i18n("Show window thumbnails when hovering dock items")
            checked: dockSettings.previewEnabled
            onToggled: dockSettings.previewEnabled = checked
        }

        FormCard.FormDelegateSeparator {
            above: previewEnabledSwitch
        }

        FormCard.FormSpinBoxDelegate {
            label: i18n("Thumbnail width (px)")
            from: 120; to: 320; stepSize: 20
            value: dockSettings.previewThumbnailSize
            onValueChanged: dockSettings.previewThumbnailSize = value
            enabled: dockSettings.previewEnabled
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSpinBoxDelegate {
            label: i18n("Hover delay (ms)")
            from: 0; to: 2000; stepSize: 50
            value: dockSettings.previewHoverDelay
            onValueChanged: dockSettings.previewHoverDelay = value
            enabled: dockSettings.previewEnabled
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSpinBoxDelegate {
            label: i18n("Hide delay (ms)")
            from: 0; to: 1000; stepSize: 50
            value: dockSettings.previewHideDelay
            onValueChanged: dockSettings.previewHideDelay = value
            enabled: dockSettings.previewEnabled
        }
    }
}
