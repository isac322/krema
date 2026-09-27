// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Running indicators, badges, attention and accessible state of DockItem,
// driven by task model roles and notification sources.
import QtQuick
import QtTest
import com.bhyoo.krema 1.0
import "TestUtils.js" as T

// TestCase is an invisible Item: visual fixtures live under `stage`, a visible
// sibling, so effective visibility, layout and pointer delivery are real.
Item {
    id: root
    width: 800
    height: 200

    Item {
        id: stage
        anchors.fill: parent
    }

    TestCase {
        id: tc
        name: "DockItemIndicators"
        when: windowShown

        Component {
            id: rowComponent
            DockItemRow {}
        }

        function init() {
            KremaMocks.resetAll()
        }

        function makeItem(roles) {
            DockModel.tasksModel.addTask(Object.assign({ display: "Dolphin", AppId: "org.kde.dolphin" }, roles))
            let row = createTemporaryObject(rowComponent, stage)
            verify(row)
            tryCompare(row, "count", 1)
            return row.itemAt(0)
        }

        // --- Running indicator dots ---

        function test_dotCountFollowsWindowCount_data() {
            return [
                { tag: "launcher", roles: { IsWindow: false }, dots: 0 },
                { tag: "one-window", roles: { IsWindow: true, ChildCount: 0 }, dots: 1 },
                { tag: "two-windows", roles: { IsWindow: true, ChildCount: 2 }, dots: 2 },
                { tag: "three-windows", roles: { IsWindow: true, ChildCount: 3 }, dots: 3 },
                { tag: "capped-at-three", roles: { IsWindow: true, ChildCount: 7 }, dots: 3 },
            ]
        }

        function test_dotCountFollowsWindowCount(data) {
            let item = makeItem(data.roles)
            tryCompare(T.indicatorDots(item), "length", data.dots)
        }

        function test_dotsUpdateWhenModelRolesChange() {
            let item = makeItem({ IsWindow: false })
            compare(T.indicatorDots(item).length, 0)
            DockModel.tasksModel.setTaskData(0, "IsWindow", true)
            tryVerify(() => T.indicatorDots(item).length === 1)
            DockModel.tasksModel.setTaskData(0, "ChildCount", 2)
            tryVerify(() => T.indicatorDots(item).length === 2)
            DockModel.tasksModel.setTaskData(0, "IsWindow", false)
            tryVerify(() => T.indicatorDots(item).length === 0)
        }

        function test_activeWindowHasLargerDot() {
            let item = makeItem({ IsWindow: true, IsActive: false })
            let dot = T.indicatorDots(item)[0]
            compare(dot.width, 3)
            DockModel.tasksModel.setTaskData(0, "IsActive", true)
            tryCompare(T.indicatorDots(item)[0], "width", 4)
        }

        function test_minimizedWindowDimsDot() {
            let item = makeItem({ IsWindow: true })
            tryCompare(T.indicatorDots(item)[0], "opacity", 0.8)
            DockModel.tasksModel.setTaskData(0, "IsMinimized", true)
            tryCompare(T.indicatorDots(item)[0], "opacity", 0.4)
        }

        // --- Badge ---

        function test_noBadgeWithoutNotifications() {
            let item = makeItem({ IsWindow: true })
            compare(item._badgeCount, 0)
            verify(T.badge(item), "badge element not found")
            verify(!T.badge(item).visible)
        }

        function test_badgeShowsUnreadCount_data() {
            return [
                { tag: "1", count: 1, text: "1" },
                { tag: "42", count: 42, text: "42" },
                { tag: "99", count: 99, text: "99" },
                { tag: "overflow", count: 150, text: "99+" },
            ]
        }

        function test_badgeShowsUnreadCount(data) {
            let item = makeItem({ IsWindow: true })
            NotificationTracker.setUnread("org.kde.dolphin", data.count)
            tryCompare(item, "_badgeCount", data.count)
            verify(T.badge(item).visible)
            compare(T.badgeLabel(item).visible, true)
            compare(T.badgeLabel(item).text, data.text)
        }

        function test_badgeOnlyForMatchingAppId() {
            let item = makeItem({ IsWindow: true })
            NotificationTracker.setUnread("org.kde.konsole", 3)
            compare(item._badgeCount, 0)
            verify(!T.badge(item).visible)
        }

        function test_badgeDisplayMode_data() {
            return [
                { tag: "number", mode: 0, visible: true, label: true, sizeFactor: 0.38 },
                { tag: "dot", mode: 1, visible: true, label: false, sizeFactor: 0.18 },
                { tag: "off", mode: 2, visible: false, label: false, sizeFactor: 0.18 },
            ]
        }

        function test_badgeDisplayMode(data) {
            DockSettings.badgeDisplayMode = data.mode
            let item = makeItem({ IsWindow: true })
            NotificationTracker.setUnread("org.kde.dolphin", 5)
            tryCompare(item, "_badgeCount", 5)
            let badge = T.badge(item)
            compare(badge.visible, data.visible)
            if (data.visible) {
                compare(T.badgeLabel(item).visible, data.label)
                compare(badge.width, Math.round(DockSettings.iconSize * data.sizeFactor))
                compare(badge.height, badge.width)
            }
        }

        function test_badgeScalesWithIconSize() {
            DockSettings.iconSize = 96
            let item = makeItem({ IsWindow: true })
            NotificationTracker.setUnread("org.kde.dolphin", 2)
            tryCompare(item, "_badgeCount", 2)
            compare(T.badge(item).width, Math.round(96 * 0.38))
        }

        function test_smartLauncherCountTakesPriority() {
            let item = makeItem({ IsWindow: true })
            verify(item._smartLauncherItem !== null, "SmartLauncherItem mock was not instantiated")
            NotificationTracker.setUnread("org.kde.dolphin", 3)
            tryCompare(item, "_badgeCount", 3)
            item._smartLauncherItem.count = 7
            item._smartLauncherItem.countVisible = true
            tryCompare(item, "_badgeCount", 7)
            compare(T.badgeLabel(item).text, "7")
            item._smartLauncherItem.countVisible = false
            tryCompare(item, "_badgeCount", 3)
        }

        function test_progressBarFollowsSmartLauncher() {
            let item = makeItem({ IsWindow: true })
            let bar = T.progressBar(item)
            verify(bar, "progress bar not found")
            verify(!bar.visible)
            item._smartLauncherItem.progress = 50
            item._smartLauncherItem.progressVisible = true
            verify(bar.visible)
            let fill = bar.children[0]
            tryCompare(fill, "width", bar.width * 0.5)
        }

        function test_activatingWindowClearsTrackerBadge() {
            let item = makeItem({ IsWindow: true, IsActive: false })
            NotificationTracker.setUnread("org.kde.dolphin", 4)
            tryCompare(item, "_badgeCount", 4)
            DockModel.tasksModel.setTaskData(0, "IsActive", true)
            tryCompare(item, "_badgeCount", 0)
            compare(NotificationTracker.callsTo("clearUnreadNotifications").length, 1)
            compare(NotificationTracker.callsTo("clearUnreadNotifications")[0].args[0], "org.kde.dolphin")
            verify(!T.badge(item).visible)
        }

        // --- Attention ---

        function test_demandingAttentionStartsAnimation() {
            DockSettings.attentionAnimation = 1
            let item = makeItem({ IsWindow: true })
            verify(!item._showAttentionAnim)
            DockModel.tasksModel.setTaskData(0, "IsDemandingAttention", true)
            tryCompare(item, "_showAttentionAnim", true)
        }

        function test_attentionSourcesTrigger_data() {
            return [{ tag: "badge-increase" }, { tag: "sni-needs-attention" }, { tag: "smartlauncher-urgent" }]
        }

        function test_attentionSourcesTrigger(data) {
            DockSettings.attentionAnimation = 3
            let item = makeItem({ IsWindow: true })
            verify(!item._showAttentionAnim)
            if (data.tag === "badge-increase")
                NotificationTracker.setUnread("org.kde.dolphin", 1)
            else if (data.tag === "sni-needs-attention")
                NotificationTracker.setSniAttention("org.kde.dolphin", true)
            else
                item._smartLauncherItem.urgent = true
            tryCompare(item, "_showAttentionAnim", true)
        }

        function test_attentionAnimationDisabledBySetting() {
            DockSettings.attentionAnimation = 0
            let item = makeItem({ IsWindow: true })
            DockModel.tasksModel.setTaskData(0, "IsDemandingAttention", true)
            tryCompare(item, "_isDemandingAttention", true)
            verify(!item._showAttentionAnim)
        }

        function test_doNotDisturbSuppressesAnimationButKeepsBadge() {
            DockSettings.attentionAnimation = 1
            NotificationTracker.dndActive = true
            let item = makeItem({ IsWindow: true })
            NotificationTracker.setUnread("org.kde.dolphin", 2)
            tryCompare(item, "_badgeCount", 2)
            verify(T.badge(item).visible)
            DockModel.tasksModel.setTaskData(0, "IsDemandingAttention", true)
            tryCompare(item, "_isDemandingAttention", true)
            verify(!item._showAttentionAnim)
        }

        function test_attentionAnimationStopsAfterDuration() {
            DockSettings.attentionAnimation = 2
            DockSettings.attentionAnimationDuration = 1
            let item = makeItem({ IsWindow: true })
            DockModel.tasksModel.setTaskData(0, "IsDemandingAttention", true)
            tryCompare(item, "_showAttentionAnim", true)
            tryCompare(item, "_showAttentionAnim", false, 3000)
        }

        function test_blinkAnimationRestoresIconOpacity() {
            DockSettings.attentionAnimation = 6
            DockSettings.attentionAnimationDuration = 1
            let item = makeItem({ IsWindow: true, IsActive: true })
            let icon = T.iconImage(item)
            DockModel.tasksModel.setTaskData(0, "IsDemandingAttention", true)
            tryVerify(() => item._blinkOpacity < 1.0, 2000, "blink never dimmed the icon")
            tryCompare(item, "_showAttentionAnim", false, 3000)
            compare(item._blinkOpacity, 1.0)
            tryCompare(icon, "opacity", 1.0)
        }

        // --- Accessibility summary ---

        function test_accessibleDescription_data() {
            return [
                { tag: "none", roles: { IsWindow: true }, unread: 0, expect: "" },
                { tag: "pinned-active", roles: { IsWindow: true, IsLauncher: true, IsActive: true }, unread: 0, expect: "Pinned, Active" },
                { tag: "minimized", roles: { IsWindow: true, IsMinimized: true }, unread: 0, expect: "Minimized" },
                { tag: "windows", roles: { IsWindow: true, ChildCount: 3 }, unread: 0, expect: "3 windows" },
                { tag: "notifications", roles: { IsWindow: true }, unread: 4, expect: "4 notifications" },
                { tag: "attention", roles: { IsWindow: true, IsDemandingAttention: true }, unread: 0, expect: "Attention requested" },
            ]
        }

        function test_accessibleDescription(data) {
            let item = makeItem(data.roles)
            if (data.unread > 0) NotificationTracker.setUnread("org.kde.dolphin", data.unread)
            tryCompare(item, "accessibleDescription", data.expect)
            compare(item.displayName, "Dolphin")
        }
    }
}
