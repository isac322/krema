// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import ".."

SettingsPage {
    id: page

    title: i18n("Notifications")
    subtitle: i18n("Review notifications tracked locally by Krema")

    FormCard.FormHeader {
        title: i18n("Inbox")
    }

    FormCard.FormCard {
        objectName: "notificationsInboxCard"

        FormCard.AbstractFormDelegate {
            background: null
            hoverEnabled: false
            focusPolicy: Qt.NoFocus
            Accessible.role: Accessible.Grouping
            Accessible.name: i18n("Notification inbox")
            contentItem: NotificationInbox {
                objectName: "notificationInbox"
            }
        }
    }
}
