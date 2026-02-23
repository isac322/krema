// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

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
            checked: DockSettings.previewEnabled
            onToggled: DockSettings.previewEnabled = checked
        }

        FormCard.FormDelegateSeparator {
            above: previewEnabledSwitch
        }

        FormCard.FormSpinBoxDelegate {
            label: i18n("Thumbnail width (px)")
            from: 120; to: 320; stepSize: 20
            value: DockSettings.previewThumbnailSize
            onValueChanged: DockSettings.previewThumbnailSize = value
            enabled: DockSettings.previewEnabled
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSpinBoxDelegate {
            label: i18n("Hover delay (ms)")
            from: 0; to: 2000; stepSize: 50
            value: DockSettings.previewHoverDelay
            onValueChanged: DockSettings.previewHoverDelay = value
            enabled: DockSettings.previewEnabled
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSpinBoxDelegate {
            label: i18n("Hide delay (ms)")
            from: 0; to: 1000; stepSize: 50
            value: DockSettings.previewHideDelay
            onValueChanged: DockSettings.previewHideDelay = value
            enabled: DockSettings.previewEnabled
        }
    }
}
