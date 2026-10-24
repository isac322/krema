// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0

/**
 * Presentation for the desktop notification lifecycle tracked by Krema.
 *
 * The inbox deliberately keeps local read/dismiss state separate from the
 * sending application's server state. Actions are dispatched through the
 * tracker's official KDE NotificationManager bridge.
 */
Item {
    id: root

    property string appFilter: ""
    property bool activeOnly: false
    property bool unreadOnly: false
    property bool requireAppFilter: false
    property bool compact: false
    property bool showHeader: true
    readonly property var rows: {
        // revision is intentionally read to make the QVariantList reactive.
        let revision = NotificationTracker.revision
        if (requireAppFilter && appFilter.trim().length === 0)
            return []
        let result = NotificationTracker.notifications(appFilter, activeOnly)
        return unreadOnly ? result.filter(item => !item.read && !item.dismissed) : result
    }
    readonly property int count: rows.length

    implicitWidth: Kirigami.Units.gridUnit * (compact ? 18 : 24)
    implicitHeight: inboxColumn.implicitHeight
    Accessible.role: Accessible.List
    Accessible.name: i18n("Notification inbox")

    Column {
        id: inboxColumn
        anchors.fill: parent
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading {
            objectName: "notificationInboxHeading"
            visible: root.showHeader
            level: 3
            text: root.appFilter.length > 0
                ? i18n("Notifications for %1", root.appFilter)
                : i18n("Notifications")
        }

        QQC2.Label {
            objectName: "notificationInboxSyncNotice"
            visible: root.showHeader || root.compact
            width: parent.width
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.disabledTextColor
            text: i18n("Read and dismissed state is local to Krema; it is not synchronized with the app's server.")
            Accessible.name: text
        }

        QQC2.Label {
            objectName: "notificationInboxEmptyLabel"
            visible: root.count === 0
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            color: Kirigami.Theme.disabledTextColor
            text: i18n("No notifications")
        }

        Repeater {
            id: inboxRepeater
            model: root.rows

            delegate: Kirigami.ShadowedRectangle {
                id: notificationCard
                objectName: "notificationCard-" + modelData.id
                required property var modelData
                width: inboxColumn.width
                implicitHeight: cardColumn.implicitHeight
                    + (root.compact ? Kirigami.Units.smallSpacing : Kirigami.Units.largeSpacing) * 2
                radius: Kirigami.Units.smallSpacing
                color: modelData.dismissed
                    ? Qt.alpha(Kirigami.Theme.backgroundColor, 0.45)
                    : Qt.alpha(Kirigami.Theme.backgroundColor, modelData.read ? 0.72 : 1.0)
                border.color: modelData.urgency > 1
                    ? Kirigami.Theme.negativeTextColor
                    : (modelData.read ? Qt.alpha(Kirigami.Theme.textColor, 0.25)
                                       : Kirigami.Theme.highlightColor)
                border.width: modelData.read ? 1 : 2
                opacity: modelData.dismissed || !modelData.active ? 0.68 : 1.0
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.summary

                Column {
                    id: cardColumn
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        margins: root.compact ? Kirigami.Units.smallSpacing : Kirigami.Units.largeSpacing
                    }
                    spacing: Kirigami.Units.smallSpacing

                    Row {
                        width: parent.width
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            objectName: "notificationSummary"
                            width: Math.max(0, parent.width - stateLabel.implicitWidth - Kirigami.Units.smallSpacing)
                            text: notificationCard.modelData.summary
                            font.bold: !notificationCard.modelData.read
                            elide: Text.ElideRight
                            Accessible.name: i18n("Summary: %1", text)
                        }

                        QQC2.Label {
                            id: stateLabel
                            objectName: "notificationState"
                            text: notificationCard.modelData.dismissed
                                ? i18n("Dismissed")
                                : (!notificationCard.modelData.active
                                    ? i18n("Closed")
                                    : (notificationCard.modelData.read ? i18n("Read") : i18n("Unread")))
                            color: notificationCard.modelData.dismissed || !notificationCard.modelData.active
                                ? Kirigami.Theme.disabledTextColor
                                : (notificationCard.modelData.read
                                    ? Kirigami.Theme.textColor : Kirigami.Theme.highlightColor)
                        }
                    }

                    QQC2.Label {
                        objectName: "notificationBody"
                        width: parent.width
                        visible: text.length > 0
                        text: notificationCard.modelData.body
                        wrapMode: Text.WordWrap
                        color: Kirigami.Theme.textColor
                        Accessible.name: i18n("Body: %1", text)
                    }

                    Row {
                        spacing: Kirigami.Units.smallSpacing
                        QQC2.Label {
                            objectName: "notificationApp"
                            text: notificationCard.modelData.appName || notificationCard.modelData.desktopEntry
                            color: Kirigami.Theme.disabledTextColor
                        }
                        QQC2.Label {
                            objectName: "notificationUrgency"
                            text: i18n("Urgency: %1", notificationCard.modelData.urgency)
                            color: notificationCard.modelData.urgency > 1
                                ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.disabledTextColor
                        }
                        QQC2.Label {
                            objectName: "notificationTimeout"
                            visible: !root.compact
                            text: i18n("Timeout: %1 ms", notificationCard.modelData.timeout)
                            color: Kirigami.Theme.disabledTextColor
                        }
                    }

                    QQC2.Label {
                        objectName: "notificationCloseReason"
                        visible: !root.compact && !notificationCard.modelData.active
                        text: i18n("Closed: %1", notificationCard.modelData.closeReason || i18n("unknown"))
                        color: Kirigami.Theme.disabledTextColor
                    }

                    Row {
                        objectName: "notificationActions"
                        spacing: Kirigami.Units.smallSpacing
                        Repeater {
                            model: {
                                let actions = notificationCard.modelData.actions || []
                                return Math.floor(actions.length / 2)
                            }
                            delegate: QQC2.Button {
                                required property int index
                                objectName: "notificationActionButton"
                                text: notificationCard.modelData.actions[index * 2 + 1]
                                enabled: notificationCard.modelData.active
                                onClicked: NotificationTracker.invokeAction(
                                    notificationCard.modelData.id,
                                    notificationCard.modelData.actions[index * 2])
                                Accessible.name: i18n("Notification action: %1", text)
                            }
                        }
                    }

                    Row {
                        spacing: Kirigami.Units.smallSpacing
                        QQC2.Button {
                            objectName: "markNotificationReadButton"
                            visible: !notificationCard.modelData.read && !notificationCard.modelData.dismissed
                            text: i18n("Mark read")
                            onClicked: NotificationTracker.markRead(notificationCard.modelData.id)
                        }
                        QQC2.Button {
                            objectName: "dismissNotificationButton"
                            visible: notificationCard.modelData.active
                            text: i18n("Dismiss")
                            onClicked: NotificationTracker.markDismissed(notificationCard.modelData.id)
                        }
                    }
                }
            }
        }
    }
}
