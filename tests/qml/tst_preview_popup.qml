// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// The real PreviewPopup.qml + PreviewThumbnail.qml: thumbnail model built
// from the task tree, incremental updates, activation/close, geometry.
// PipeWire/ScreencastingRequest are inert mocks (no live frames).
import QtQuick
import QtQuick.Window
import QtTest
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0
import "../../src/qml" as Krema
import "TestUtils.js" as T

// TestCase is an invisible Item: visual fixtures live under `stage`, a visible
// sibling, so effective visibility, layout and pointer delivery are real.
Item {
    id: root
    width: 900
    height: 400

    Item {
        id: stage
        anchors.fill: parent
    }

    TestCase {
        id: tc
        name: "PreviewPopup"
        when: windowShown

        Component {
            id: popupComponent
            Krema.PreviewPopup {}
        }

        function init() {
            KremaMocks.resetAll()
            // Row 0: single-window app. Row 1: grouped app with three windows.
            DockModel.tasksModel.addTask({
                display: "Kate", AppId: "org.kde.kate", IsWindow: true,
                IsActive: true, WinIdList: ["kate-1"]
            })
            DockModel.tasksModel.addTask({
                display: "Konsole", AppId: "org.kde.konsole", IsWindow: true,
                WinIdList: ["k-1", "k-2", "k-3"]
            })
            DockModel.tasksModel.addChildTask(1, { display: "Konsole 1", IsActive: true })
            DockModel.tasksModel.addChildTask(1, { display: "Konsole 2" })
            DockModel.tasksModel.addChildTask(1, { display: "Konsole 3", IsMinimized: true })
            // Row 2: launcher without windows.
            DockModel.tasksModel.addTask({
                display: "Okular", AppId: "org.kde.okular", IsWindow: false
            })
        }

        function notification(overrides) {
            return Object.assign({
                id: 101,
                desktopEntry: "org.kde.konsole",
                appName: "Konsole",
                summary: "Build finished",
                body: "The build is ready.",
                actions: ["open", "Open"],
                timeout: 5000,
                urgency: 1,
                active: true,
                read: false,
                dismissed: false,
                closeReason: "",
            }, overrides || {})
        }

        function makePopup() {
            let popup = createTemporaryObject(popupComponent, stage)
            verify(popup)
            return popup
        }

        function show(index, name) {
            PreviewController.appName = name
            PreviewController.parentIndex = index
            PreviewController.visible = true
        }

        // Thumbnail delegates in model order.
        function thumbs(popup) {
            return T.findAll(popup, o => o.retryScreencast !== undefined && o.childIndex !== undefined)
                    .sort((a, b) => a.childIndex - b.childIndex)
        }

        // Waits for `n` thumbnails laid out left-to-right by the popup's Row
        // (new delegates sit at x=0 until the Row polishes).
        function waitThumbs(popup, n) {
            tryVerify(() => {
                let ts = thumbs(popup)
                if (ts.length !== n) return false
                for (let i = 1; i < ts.length; i++)
                    if (ts[i].x <= ts[i - 1].x) return false
                return true
            }, 2000, "expected " + n + " laid-out thumbnails, have " + thumbs(popup).length)
            // A single delegate has no sibling x-order check to wait for layout.
            verify(waitForPolish(stage.Window.window))
            return thumbs(popup)
        }

        function container(popup) {
            return T.directChildren(popup, c => c.radius !== undefined)[0]
        }

        function test_hiddenUntilControllerShows() {
            let popup = makePopup()
            verify(!container(popup).visible)
            compare(thumbs(popup).length, 0)
            show(1, "Konsole")
            tryVerify(() => container(popup).visible)
            PreviewController.visible = false
            tryVerify(() => !container(popup).visible)
        }

        function test_singleWindowUsesParentRow() {
            let popup = makePopup()
            show(0, "Kate")
            waitThumbs(popup, 1)
            let t = thumbs(popup)[0]
            compare(t.title, "Kate")
            compare(t.isActive, true)
            compare(t.childIndex, -1)
            compare(t.winId, "kate-1")
            compare(t.parentIndex, 0)
        }

        function test_groupedWindowsOneThumbnailEach() {
            let popup = makePopup()
            show(1, "Konsole")
            waitThumbs(popup, 3)
            let ts = thumbs(popup)
            compare(ts.map(t => t.title), ["Konsole 1", "Konsole 2", "Konsole 3"])
            compare(ts.map(t => t.childIndex), [0, 1, 2])
            compare(ts.map(t => t.winId), ["k-1", "k-2", "k-3"])
            compare(ts.map(t => t.isActive), [true, false, false])
            compare(ts.map(t => t.isMinimized), [false, false, true])
            let header = T.findFirst(popup, o => o.text === "Konsole" && o.font !== undefined)
            verify(header && header.visible, "app name header missing")
        }

        function test_launcherWithoutWindowsHasNoThumbnails() {
            let popup = makePopup()
            show(1, "Konsole")
            waitThumbs(popup, 3)
            show(2, "Okular")
            tryVerify(() => thumbs(popup).length === 0)
        }

        function test_winIdListLagLimitsThumbnails() {
            let popup = makePopup()
            show(1, "Konsole")
            waitThumbs(popup, 3)
            // KWin updated the child rows before WinIdList: show only windows with an id.
            DockModel.tasksModel.setTaskData(1, "WinIdList", ["k-1", "k-2"])
            waitThumbs(popup, 2)
        }

        function test_incrementalUpdatePreservesDelegates() {
            let popup = makePopup()
            show(1, "Konsole")
            waitThumbs(popup, 3)
            let first = thumbs(popup)[0]
            DockModel.tasksModel.addChildTask(1, { display: "Konsole 4" })
            DockModel.tasksModel.setTaskData(1, "WinIdList", ["k-1", "k-2", "k-3", "k-4"])
            waitThumbs(popup, 4)
            // Existing delegates (and their PipeWire streams) survive the update.
            verify(thumbs(popup)[0] === first, "first thumbnail delegate was recreated")
            compare(thumbs(popup)[3].title, "Konsole 4")
        }

        function test_switchingAppRebuildsThumbnails() {
            let popup = makePopup()
            show(1, "Konsole")
            waitThumbs(popup, 3)
            show(0, "Kate")
            waitThumbs(popup, 1)
            compare(thumbs(popup)[0].title, "Kate")
        }

        function test_removedParentHidesPreview() {
            let popup = makePopup()
            show(2, "Okular")
            DockModel.tasksModel.removeTask(2)
            tryVerify(() => PreviewController.callsTo("hidePreview").length === 1)
        }

        function test_clickThumbnailActivatesThatWindow_data() {
            return [
                { tag: "grouped-child", parent: 1, thumb: 1, count: 3, row: 1, child: 1 },
                { tag: "single-window", parent: 0, thumb: 0, count: 1, row: 0, child: -1 },
            ]
        }

        function test_clickThumbnailActivatesThatWindow(data) {
            let popup = makePopup()
            show(data.parent, "App")
            waitThumbs(popup, data.count)
            let t = thumbs(popup)[data.thumb]
            // Lower-left area of the thumbnail (away from the close button).
            mouseClick(t, t.width * 0.25, t.thumbnailHeight * 0.75)
            tryCompare(DockModel.tasksModel, "activateRequests", 1)
            compare(DockModel.tasksModel.lastRequestRow, data.row)
            compare(DockModel.tasksModel.lastRequestChild, data.child)
            compare(PreviewController.callsTo("hidePreview").length, 1)
            compare(DockModel.tasksModel.closeRequests, 0)
        }

        function test_closeButtonClosesThatWindow() {
            let popup = makePopup()
            show(1, "Konsole")
            waitThumbs(popup, 3)
            let t = thumbs(popup)[2]
            let close = T.findFirst(t, o => o.icon !== undefined && o.icon.name === "window-close")
            verify(close, "close button not found")
            mouseClick(close)
            tryCompare(DockModel.tasksModel, "closeRequests", 1)
            compare(DockModel.tasksModel.lastRequestRow, 1)
            compare(DockModel.tasksModel.lastRequestChild, 2)
            compare(DockModel.tasksModel.activateRequests, 0)
        }

        function test_thumbnailSizeFollowsSetting() {
            let popup = makePopup()
            show(1, "Konsole")
            waitThumbs(popup, 3)
            let t = thumbs(popup)[0]
            compare(t.thumbnailWidth, 200)
            compare(t.thumbnailHeight, 140)
            let before = container(popup).width
            DockSettings.previewThumbnailSize = 300
            compare(t.thumbnailWidth, 300)
            compare(t.thumbnailHeight, 210)
            tryVerify(() => container(popup).width > before)
            // The popup reports its size for the input region.
            tryVerify(() => PreviewController.contentWidth === container(popup).width)
            tryVerify(() => PreviewController.contentHeight === container(popup).height)
        }

        function test_keyboardFocusRingFollowsController() {
            let popup = makePopup()
            show(1, "Konsole")
            waitThumbs(popup, 3)
            PreviewController.focusedThumbnailIndex = 1
            PreviewController.previewKeyboardActive = true
            compare(thumbs(popup).map(t => t.isKeyboardFocused), [false, true, false])
            PreviewController.previewKeyboardActive = false
            compare(thumbs(popup).map(t => t.isKeyboardFocused), [false, false, false])
        }

        function test_popupPlacementPerEdge_data() {
            return [
                { tag: "top", edge: 0 },
                { tag: "bottom", edge: 1 },
                { tag: "left", edge: 2 },
                { tag: "right", edge: 3 },
            ]
        }

        function test_popupPlacementPerEdge(data) {
            DockView.edge = data.edge
            PreviewController.contentX = 123
            PreviewController.contentY = 45
            let popup = makePopup()
            show(1, "Konsole")
            waitThumbs(popup, 3)
            let c = container(popup)
            switch (data.edge) {
            case 0: compare(c.y, 0); compare(c.x, 123); break
            case 1: tryCompare(c, "y", popup.height - c.height); compare(c.x, 123); break
            case 2: compare(c.x, 0); compare(c.y, 45); break
            case 3: tryCompare(c, "x", popup.width - c.width); compare(c.y, 45); break
            }
        }

        function test_hoverKeepsPreviewAndCancelsKeyboardNav() {
            let popup = makePopup()
            show(1, "Konsole")
            waitThumbs(popup, 3)
            PreviewController.previewKeyboardActive = true
            let c = container(popup)
            mouseMove(popup, c.x + c.width / 2, c.y + c.height / 2)
            tryCompare(PreviewController, "previewHovered", true)
            tryCompare(PreviewController, "previewKeyboardActive", false)
            mouseMove(stage, -1, -1)
        }
        function inbox(popup) {
            return T.findFirst(popup, o => o.objectName === "previewNotificationInbox")
        }

        // B1/B4: one mixed surface, scoped to the current app and active unread rows.
        function test_notificationCardsFilterAndRetargetAlongsideThumbnails() {
            NotificationTracker.setNotifications([
                notification(),
                notification({ id: 102, read: true }),
                notification({ id: 103, dismissed: true }),
                notification({ id: 104, active: false }),
                notification({ id: 105, desktopEntry: "org.kde.okular" }),
            ])
            let popup = makePopup()
            show(1, "Konsole")
            waitThumbs(popup, 3)
            compare(inbox(popup).count, 1)
            verify(T.findFirst(popup, o => o.objectName === "notificationCard-101"))
            verify(popup.hasWindowContent && popup.hasNotificationContent)
            show(2, "Okular")
            tryCompare(inbox(popup), "appFilter", "org.kde.okular")
            tryCompare(inbox(popup), "count", 1)
            verify(T.findFirst(popup, o => o.objectName === "notificationCard-105"))
            verify(!T.findFirst(popup, o => o.objectName === "notificationCard-101"))
            verify(!popup.hasWindowContent && popup.hasNotificationContent)
        }

        // B5/B6: preserve real payload, exact actions, and the local-read contract.
        function test_notificationPayloadActionsAndLocalDisclaimer() {
            NotificationTracker.setNotifications([notification()])
            let popup = makePopup()
            show(1, "Konsole")
            waitThumbs(popup, 3)
            let card = T.findFirst(popup, o => o.objectName === "notificationCard-101")
            verify(card)
            compare(T.findFirst(card, o => o.objectName === "notificationSummary").text,
                    "Build finished")
            compare(T.findFirst(card, o => o.objectName === "notificationBody").text,
                    "The build is ready.")
            compare(T.findFirst(card, o => o.objectName === "notificationApp").text, "Konsole")
            let action = T.findFirst(card, o => o.objectName === "notificationActionButton")
            compare(action.Accessible.name, "Notification action: Open")
            action.clicked()
            compare(NotificationTracker.callsTo("invokeAction")[0].args, [101, "open"])
            let notice = T.findFirst(popup, o => o.objectName === "notificationInboxSyncNotice")
            verify(notice.visible)
            verify(notice.text.indexOf("not synchronized") >= 0)
        }

        // B7: losing the last window keeps cards, but never an empty shell.
        function test_lastWindowClose_data() {
            return [{ tag: "cards-remain", cards: true }, { tag: "no-cards", cards: false }]
        }

        function test_lastWindowClose(data) {
            if (data.cards)
                NotificationTracker.setNotifications([notification({ desktopEntry: "org.kde.kate" })])
            let popup = makePopup()
            show(0, "Kate")
            waitThumbs(popup, 1)
            if (data.cards)
                PreviewController.startPreviewKeyboardNav(0)
            DockModel.tasksModel.setTaskData(0, "WinIdList", [])
            DockModel.tasksModel.setTaskData(0, "IsWindow", false)
            tryVerify(() => !popup.hasWindowContent)
            tryCompare(PreviewController, "visible", data.cards)
            if (data.cards)
                tryCompare(PreviewController, "previewKeyboardActive", false)
        }

        // B8: Mark read, Dismiss, and server close remove the last local card.
        function test_lastNotificationClosesNotificationOnlyPopup_data() {
            return [{ tag: "mark-read", operation: "read" },
                    { tag: "dismiss", operation: "dismiss" },
                    { tag: "server-close", operation: "close" }]
        }

        function test_lastNotificationClosesNotificationOnlyPopup(data) {
            NotificationTracker.setNotifications([
                notification({ desktopEntry: "org.kde.okular" }),
                notification({ id: 102, desktopEntry: "org.kde.okular" }),
            ])
            let popup = makePopup()
            show(2, "Okular")
            tryCompare(inbox(popup), "count", 2)
            NotificationTracker.markRead(102)
            tryCompare(inbox(popup), "count", 1)
            verify(PreviewController.visible)
            if (data.operation === "close") {
                NotificationTracker._replaceNotification(101, { active: false })
            } else {
                let buttonName = data.operation === "read"
                    ? "markNotificationReadButton" : "dismissNotificationButton"
                T.findFirst(popup, o => o.objectName === buttonName).clicked()
            }
            tryCompare(PreviewController, "visible", false)
            compare(PreviewController.callsTo("hidePreviewImmediate").length, 1)
        }

        // B9: task and watcher signals settle together before checking emptiness.
        function test_notificationAndWindowRevisionOrder_data() {
            return [{ tag: "notification-first", notificationFirst: true },
                    { tag: "window-first", notificationFirst: false }]
        }

        function test_notificationAndWindowRevisionOrder(data) {
            let popup = makePopup()
            show(0, "Kate")
            waitThumbs(popup, 1)
            let rows = [notification({ desktopEntry: "org.kde.kate" })]
            if (data.notificationFirst) NotificationTracker.setNotifications(rows)
            DockModel.tasksModel.setTaskData(0, "WinIdList", [])
            if (!data.notificationFirst) NotificationTracker.setNotifications(rows)
            tryCompare(inbox(popup), "count", 1)
            tryVerify(() => !popup.hasWindowContent)
            verify(PreviewController.visible)
        }

        // B10: cards stay bounded and controller/input-region size tracks layout.
        function test_manyNotificationCardsAreScrollableAndBounded() {
            let rows = []
            for (let i = 0; i < 20; i++)
                rows.push(notification({ id: i + 100, desktopEntry: "org.kde.okular" }))
            NotificationTracker.setNotifications(rows)
            let popup = makePopup()
            show(2, "Okular")
            tryCompare(inbox(popup), "count", 20)
            let section = T.findFirst(popup, o => o.objectName === "previewNotificationSection")
            verify(waitForPolish(stage.Window.window))
            verify(section.height < inbox(popup).implicitHeight)
            verify(section.height <= Kirigami.Units.gridUnit * 14)
            compare(PreviewController.contentWidth, container(popup).width)
            compare(PreviewController.contentHeight, container(popup).height)
        }
        // B2/B3: launcher-only entries are valid only while unread cards exist.
        function test_launcherOnlyNotificationPopupEntryAndEmptyClose() {
            NotificationTracker.setNotifications([
                notification({ desktopEntry: "org.kde.okular" }),
            ])
            let popup = makePopup()
            show(2, "Okular")
            tryCompare(inbox(popup), "count", 1)
            verify(!popup.hasWindowContent)
            verify(T.findFirst(popup, o => o.objectName === "previewNotificationSection").visible)
            NotificationTracker.setNotifications([])
            tryCompare(PreviewController, "visible", false)
        }

        // B11/B12: the popup remains an accessible popup surface while hovered.
        function test_notificationPopupAccessibilityAndHoverLifecycle() {
            NotificationTracker.setNotifications([notification({ desktopEntry: "org.kde.okular" })])
            let popup = makePopup()
            show(2, "Okular")
            tryVerify(() => container(popup).visible)
            compare(container(popup).Accessible.name, "Preview for Okular")
            let card = T.findFirst(popup, o => o.objectName === "notificationCard-101")
            compare(card.Accessible.name, "Build finished")
            let action = T.findFirst(card, o => o.objectName === "notificationActionButton")
            compare(action.Accessible.name, "Notification action: Open")
            mouseMove(popup, container(popup).x + container(popup).width / 2,
                      container(popup).y + container(popup).height / 2)
            tryCompare(PreviewController, "previewHovered", true)
        }
        function test_emptyAppIdDoesNotLeakOtherAppsNotifications() {
            DockModel.tasksModel.setTaskData(0, "AppId", "")
            NotificationTracker.setNotifications([
                notification({ desktopEntry: "org.example.other" }),
            ])
            let popup = makePopup()
            show(0, "Kate")
            waitThumbs(popup, 1)
            compare(popup.currentAppId, "")
            compare(inbox(popup).count, 0)
            verify(!popup.hasNotificationContent)
            verify(!T.findFirst(popup, o => o.objectName === "notificationCard-101"))
        }

        function test_blacklistedOrClearedUnreadCountSuppressesCards() {
            NotificationTracker.setNotifications([notification()])
            NotificationTracker.setUnread("org.kde.konsole", 0)
            let popup = makePopup()
            show(1, "Konsole")
            waitThumbs(popup, 3)
            compare(inbox(popup).count, 1)
            verify(!popup.hasUnreadForCurrentApp)
            verify(!popup.hasNotificationContent)
            verify(!T.findFirst(popup, o => o.objectName === "previewNotificationSection").visible)
        }
        function test_pendingWinIdListDoesNotPrematurelyCloseWindowPreview() {
            DockModel.tasksModel.setTaskData(0, "WinIdList", [])
            let popup = makePopup()
            show(0, "Kate")
            wait(20)
            compare(PreviewController.visible, true)
            verify(!popup.hasWindowContent)
            DockModel.tasksModel.setTaskData(0, "WinIdList", ["kate-1"])
            waitThumbs(popup, 1)
            verify(PreviewController.visible)
        }
    }
}
