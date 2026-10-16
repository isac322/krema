// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0
import "TestUtils.js" as T

Item {
    id: root
    width: 760
    height: 900

    Item { id: stage; anchors.fill: parent }

    TestCase {
        id: tc
        name: "NotificationInbox"
        when: windowShown

        function init() {
            KremaMocks.resetAll()
            root.width = 760
            root.height = 900
        }

        function makeInbox() {
            let component = Qt.createComponent(Qt.resolvedUrl("../../src/qml/NotificationInbox.qml"))
            compare(component.status, Component.Ready, component.errorString())
            let inbox = createTemporaryObject(component, stage, { width: 720 })
            verify(inbox)
            return inbox
        }

        function card(inbox) {
            return T.findFirst(inbox, o => o.objectName === "notificationCard-101")
        }

        function textObject(inbox, name) {
            return T.findFirst(inbox, o => o.objectName === name)
        }

        function sample(overrides) {
            return Object.assign({
                id: 101,
                desktopEntry: "org.example.Mail",
                appName: "Example Mail",
                appIcon: "mail-unread",
                summary: "New message",
                body: "A message arrived in your inbox.",
                actions: ["open", "Open", "archive", "Archive"],
                hints: ({ urgency: 1 }),
                timeout: 5000,
                replacesId: 0,
                urgency: 1,
                active: true,
                read: false,
                dismissed: false,
                closeReason: "",
                broadcastReason: 0,
            }, overrides || {})
        }

        function test_metadataAndStatesAreVisible() {
            NotificationTracker.setNotifications([sample()])
            let inbox = makeInbox()
            tryVerify(() => !!card(inbox))
            compare(textObject(inbox, "notificationSummary").text, "New message")
            compare(textObject(inbox, "notificationBody").text, "A message arrived in your inbox.")
            compare(textObject(inbox, "notificationApp").text, "Example Mail")
            verify(textObject(inbox, "notificationUrgency").text.indexOf("1") >= 0)
            tryVerify(() => !!textObject(inbox, "notificationTimeout"))
            let timeoutText = textObject(inbox, "notificationTimeout").text
            verify(timeoutText.startsWith("Timeout: "))
            verify(timeoutText.indexOf("5") >= 0)
            verify(timeoutText.endsWith(" ms"))
            compare(textObject(inbox, "notificationState").text, "Unread")
            verify(textObject(inbox, "notificationActions").children.length >= 2)
            verify(T.findFirst(inbox, o => o.objectName === "notificationActionButton" && o.text === "Open"))
            verify(T.findFirst(inbox, o => o.objectName === "notificationActionButton" && o.text === "Archive"))
            verify(textObject(inbox, "notificationInboxSyncNotice").visible)
        }

        function test_markReadUpdatesLocalStateOnly() {
            NotificationTracker.setNotifications([sample()])
            let inbox = makeInbox()
            let button = T.findFirst(inbox, o => o.objectName === "markNotificationReadButton")
            verify(button)
            button.clicked()
            tryCompare(textObject(inbox, "notificationState"), "text", "Read")
            compare(NotificationTracker.callsTo("markRead").length, 1)
            compare(NotificationTracker.callsTo("markRead")[0].args[0], 101)
            verify(textObject(inbox, "notificationInboxSyncNotice").text.indexOf("not synchronized") >= 0)
        }

        function test_dismissedAndClosedStatesAreDistinct() {
            NotificationTracker.setNotifications([
                sample({ dismissed: true, active: false, closeReason: "dismissed" }),
                sample({ id: 102, dismissed: false, active: false, closeReason: "expired" }),
            ])
            let inbox = makeInbox()
            tryVerify(() => !!card(inbox))
            compare(textObject(inbox, "notificationState").text, "Dismissed")
            compare(textObject(inbox, "notificationCloseReason").text, "Closed: dismissed")
            let closedCard = T.findFirst(inbox, o => o.objectName === "notificationCard-102")
            verify(closedCard)
            compare(T.findFirst(closedCard, o => o.objectName === "notificationState").text, "Closed")
            compare(T.findFirst(closedCard, o => o.objectName === "notificationCloseReason").text,
                    "Closed: expired")
            verify(card(inbox).opacity < 1)
        }

        function test_actionButtonDispatchesOfficialTrackerAction() {
            NotificationTracker.setNotifications([sample()])
            let inbox = makeInbox()
            let button = T.findFirst(inbox, o => o.objectName === "notificationActionButton" && o.text === "Open")
            verify(button)
            button.clicked()
            compare(NotificationTracker.callsTo("invokeAction").length, 1)
            let call = NotificationTracker.callsTo("invokeAction")[0]
            compare(call.args[0], 101)
            compare(call.args[1], "open")
        }

        function test_appFilterAndActiveOnly() {
            NotificationTracker.setNotifications([
                sample(),
                sample({ id: 102, desktopEntry: "org.example.Chat", appName: "Example Chat", active: false,
                         dismissed: false, closeReason: "expired" }),
            ])
            let inbox = makeInbox()
            compare(T.findAll(inbox, o => String(o.objectName).startsWith("notificationCard-")).length, 2)
            inbox.appFilter = "org.example.Chat"
            tryVerify(() => T.findAll(inbox, o => String(o.objectName).startsWith("notificationCard-")).length === 1)
            inbox.appFilter = ""
            inbox.activeOnly = true
            tryVerify(() => T.findAll(inbox, o => String(o.objectName).startsWith("notificationCard-")).length === 1)
        }
    }
}
