// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Running indicators, badges, attention and accessible state of DockItem,
// driven by task model roles and notification sources.
import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
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

        // Unity LauncherEntry state is keyed by the task's launcher URL.
        readonly property string launcherUrl: "applications:org.kde.dolphin.desktop"

        function makeItem(roles) {
            DockModel.tasksModel.addTask(Object.assign({ display: "Dolphin", AppId: "org.kde.dolphin", LauncherUrlWithoutIcon: launcherUrl }, roles))
            let row = createTemporaryObject(rowComponent, stage)
            verify(row)
            tryCompare(row, "count", 1)
            return row.itemAt(0)
        }

        function dockEdges() {
            return [
                { tag: "top", edge: 0 },
                { tag: "bottom", edge: 1 },
                { tag: "left", edge: 2 },
                { tag: "right", edge: 3 },
            ]
        }

        function makeBadgedItem(roles) {
            // Geometry assertions must not sample a notification bounce.
            DockSettings.attentionAnimation = 0
            let item = makeItem(roles || { IsWindow: true })
            NotificationTracker.setUnread("org.kde.dolphin", 5)
            tryCompare(item, "_badgeCount", 5)
            verify(T.iconImage(item), "icon image not found")
            verify(T.badge(item), "badge not found")
            verify(T.badgeLabel(item), "badge label not found")
            return item
        }

        function verifyBadgeCorner(item, onLeft) {
            let icon = T.iconImage(item)
            let badge = T.badge(item)
            let expectedX = () => onLeft ? icon.x - badge.width * 0.2
                                        : icon.x + icon.width - badge.width * 0.8
            tryVerify(() => Math.abs(badge.x - expectedX()) < 0.01
                && Math.abs(badge.y - (icon.y - badge.height * 0.2)) < 0.01,
                2000, "badge did not anchor to the expected icon corner")
            fuzzyCompare(badge.x, expectedX(), 0.01)
            fuzzyCompare(badge.y, icon.y - badge.height * 0.2, 0.01)
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

        function test_indicatorDotCenteredInIndicatorSpace_data() {
            let rows = []
            for (let edge of dockEdges()) {
                for (let active of [false, true])
                    rows.push({ tag: edge.tag + (active ? "-active" : "-inactive"),
                                edge: edge.edge, active: active })
            }
            return rows
        }

        function test_indicatorDotCenteredInIndicatorSpace(data) {
            DockView.edge = data.edge
            let item = makeItem({ IsWindow: true, IsActive: data.active })
            tryVerify(() => T.indicatorDots(item).length === 1)
            let dot = T.indicatorDots(item)[0]
            let icon = T.iconImage(item)
            verify(icon)
            compare(dot.width, data.active ? 4 : 3)
            compare(dot.height, data.active ? 4 : 3)
            let p = dot.mapToItem(item, 0, 0)
            let iconClearance, edgeClearance
            switch (data.edge) {
            case 0:
                iconClearance = icon.y - p.y - dot.height
                edgeClearance = p.y
                break
            case 1:
                iconClearance = p.y - icon.y - icon.height
                edgeClearance = item.height - p.y - dot.height
                break
            case 2:
                iconClearance = icon.x - p.x - dot.width
                edgeClearance = p.x
                break
            case 3:
                iconClearance = p.x - icon.x - icon.width
                edgeClearance = item.width - p.x - dot.width
                break
            }
            if (data.active) {
                compare(iconClearance, 2)
                compare(edgeClearance, 2)
            } else {
                verify(iconClearance >= 2 && iconClearance <= 3,
                       "inactive dot clearance from icon: " + iconClearance)
                verify(edgeClearance >= 2 && edgeClearance <= 3,
                       "inactive dot clearance from screen edge: " + edgeClearance)
                compare(iconClearance + edgeClearance, 5)
            }
        }

        function test_iconPositionStationaryAcrossWindowTransitions_data() {
            return dockEdges()
        }

        function test_iconPositionStationaryAcrossWindowTransitions(data) {
            DockView.edge = data.edge
            let item = makeItem({ IsWindow: false, IsLauncher: true })
            let icon = T.iconImage(item)
            verify(icon)
            let initial = icon.mapToItem(stage, 0, 0)
            for (let isWindow of [true, false]) {
                DockModel.tasksModel.setTaskData(0, "IsWindow", isWindow)
                tryVerify(() => T.indicatorDots(item).length === (isWindow ? 1 : 0))
                let position = icon.mapToItem(stage, 0, 0)
                compare(position.x, initial.x)
                compare(position.y, initial.y)
            }
        }

        // --- Badge ---

        function test_legacyConfigDefaults() {
            // The fixture leaves the new keys untouched, as a legacy config does.
            let item = makeBadgedItem()
            verifyBadgeCorner(item, false)
            verify(Qt.colorEqual(T.badge(item).color, Kirigami.Theme.highlightColor))
            verify(Qt.colorEqual(T.badgeLabel(item).color, Kirigami.Theme.highlightedTextColor))
        }

        function test_badgePositionTopRight_data() {
            return dockEdges()
        }

        function test_badgePositionTopRight(data) {
            DockView.edge = data.edge
            DockSettings.badgePosition = 0
            let item = makeBadgedItem()
            verify(T.badge(item).visible)
            verifyBadgeCorner(item, false)
        }

        function test_badgePositionTopLeft_data() {
            return dockEdges()
        }

        function test_badgePositionTopLeft(data) {
            DockView.edge = data.edge
            DockSettings.badgePosition = 1
            let item = makeBadgedItem()
            verify(T.badge(item).visible)
            verifyBadgeCorner(item, true)
        }

        function test_badgePositionAuto_data() {
            return dockEdges()
        }

        function test_badgePositionAuto(data) {
            DockView.edge = data.edge
            DockSettings.badgePosition = 2
            let item = makeBadgedItem()
            verifyBadgeCorner(item, data.edge === 2)
            let icon = T.iconImage(item)
            if (data.edge === 2)
                verify(T.badge(item).x < icon.x)
            else
                verify(T.badge(item).x > icon.x + icon.width / 2)
        }

        function test_badgePositionDotMode_data() {
            let rows = []
            for (let edge of dockEdges()) {
                for (let position of [0, 1, 2])
                    rows.push({ tag: edge.tag + "-position-" + position,
                                edge: edge.edge, position: position })
            }
            return rows
        }

        function test_badgePositionDotMode(data) {
            DockView.edge = data.edge
            DockSettings.badgePosition = data.position
            DockSettings.badgeDisplayMode = 1
            let item = makeBadgedItem()
            let badge = T.badge(item)
            verify(badge.visible)
            compare(T.badgeLabel(item).visible, false)
            compare(badge.width, Math.round(DockSettings.iconSize * 0.18))
            compare(badge.height, Math.round(DockSettings.iconSize * 0.18))
            verifyBadgeCorner(item, data.position === 1 || (data.position === 2 && data.edge === 2))
        }

        function test_badgeOffModeSuppressesBadge() {
            DockSettings.badgeDisplayMode = 2
            let item = makeBadgedItem()
            for (let edge of dockEdges()) {
                DockView.edge = edge.edge
                for (let position of [0, 1, 2]) {
                    DockSettings.badgePosition = position
                    compare(item._badgeCount, 5)
                    compare(T.badge(item).visible, false)
                    compare(T.badgeLabel(item).visible, false)
                }
            }
        }

        function test_sniMetadataBadgeAndAccessibleDescription() {
            let item = makeItem({ IsWindow: true })
            let icon = T.sniBadgeIcon(item)
            verify(icon)
            verify(!icon.visible)

            NotificationTracker.setSniMetadata("org.kde.dolphin", {
                title: "Dolphin tray",
                attentionIconName: "dialog-warning",
                overlayIconName: "mail-unread",
                toolTipTitle: "Dolphin tray",
                toolTipSubTitle: "New message"
            })
            NotificationTracker.setSniAttention("org.kde.dolphin", true)
            tryVerify(() => icon.visible)
            compare(icon.source, "dialog-warning")
            verify(item.accessibleDescription.indexOf("New message") >= 0)
            let iconImage = T.iconImage(item)
            let badge = T.badge(item)
            verify(icon.y > iconImage.y + iconImage.height * 0.5)
            verify(badge.y < iconImage.y + iconImage.height * 0.5)

            DockSettings.badgeDisplayMode = 2
            NotificationTracker.setSniAttention("org.kde.dolphin", false)
            tryVerify(() => !icon.visible)
            NotificationTracker.setSniAttention("org.kde.dolphin", true)
            tryVerify(() => icon.visible)
            compare(icon.source, "dialog-warning")
        }

        function test_badgePositionDynamicSwitching() {
            DockView.edge = 2
            let item = makeBadgedItem()
            let badge = T.badge(item)
            verifyBadgeCorner(item, false)
            DockSettings.badgePosition = 1
            verifyBadgeCorner(item, true)
            DockSettings.badgePosition = 2
            verifyBadgeCorner(item, true)
            for (let edge of [0, 1, 3, 2]) {
                DockView.edge = edge
                verifyBadgeCorner(item, edge === 2)
                compare(T.badge(item), badge, "position changes must preserve the badge object")
            }
            DockSettings.badgePosition = 0
            verifyBadgeCorner(item, false)
        }

        function test_badgeAccentColorMode() {
            DockSettings.badgeUseAccentColor = true
            DockSettings.badgeCustomColor = "#000000"
            let item = makeBadgedItem()
            verify(Qt.colorEqual(T.badge(item).color, Kirigami.Theme.highlightColor))
            verify(Qt.colorEqual(T.badgeLabel(item).color, Kirigami.Theme.highlightedTextColor))
            DockSettings.badgeCustomColor = "#ffff00"
            verify(Qt.colorEqual(T.badge(item).color, Kirigami.Theme.highlightColor))
            verify(Qt.colorEqual(T.badgeLabel(item).color, Kirigami.Theme.highlightedTextColor))
            DockSettings.badgeUseAccentColor = false
            tryVerify(() => Qt.colorEqual(T.badge(item).color, "#ffff00"))
            DockSettings.badgeUseAccentColor = true
            tryVerify(() => Qt.colorEqual(T.badge(item).color, Kirigami.Theme.highlightColor))
            verify(Qt.colorEqual(T.badgeLabel(item).color, Kirigami.Theme.highlightedTextColor))
        }

        function test_badgeCustomColorContrast_data() {
            return [
                { tag: "default-red", hex: "#e93a40", expectedTextColor: "#ffffff" },
                { tag: "dark-blue", hex: "#1e3a5f", expectedTextColor: "#ffffff" },
                { tag: "black", hex: "#000000", expectedTextColor: "#ffffff" },
                { tag: "yellow", hex: "#ffff00", expectedTextColor: "#000000" },
                { tag: "white", hex: "#ffffff", expectedTextColor: "#000000" },
                { tag: "light-green", hex: "#a8ffb2", expectedTextColor: "#000000" },
                { tag: "trimmed", hex: "  #1e3a5f  ", expectedTextColor: "#ffffff" },
                { tag: "short-hex", hex: "#fff", expectedTextColor: "#000000" },
            ]
        }

        function test_badgeCustomColorContrast(data) {
            DockSettings.badgeUseAccentColor = false
            DockSettings.badgeCustomColor = data.hex
            let item = makeBadgedItem()
            verify(Qt.colorEqual(T.badge(item).color, data.hex.trim()))
            verify(Qt.colorEqual(T.badgeLabel(item).color, data.expectedTextColor))
        }

        function test_badgeCustomColorInvalidFallback_data() {
            return [
                { tag: "empty", hex: "" },
                { tag: "invalid-name", hex: "invalid-hex" },
                { tag: "invalid-hex", hex: "#xyz" },
                { tag: "alpha-hex", hex: "#801e3a5f" },
                { tag: "transparent-alpha-hex", hex: "#00ffffff" },
            ]
        }

        function test_badgeCustomColorInvalidFallback(data) {
            DockSettings.badgeUseAccentColor = false
            DockSettings.badgeCustomColor = data.hex
            let item = makeBadgedItem()
            verify(Qt.colorEqual(T.badge(item).color, Kirigami.Theme.highlightColor))
        }

        function test_badgeZOrderAboveIndicatorRowOnLeftDock() {
            DockView.edge = 2
            DockSettings.badgePosition = 1
            let item = makeBadgedItem({ IsWindow: true, ChildCount: 3 })
            tryVerify(() => T.indicatorDots(item).length === 3)
            let indicatorRow = T.findFirst(item, o => o.flow !== undefined)
            verify(indicatorRow, "indicator row not found")
            verifyBadgeCorner(item, true)
            verify(T.badge(item).visible)
            compare(T.badge(item).z, 2)
            verify(T.badge(item).z > indicatorRow.z)
        }

        function test_badgeRapidColorAndCountChurn() {
            let item = makeBadgedItem()
            let badge = T.badge(item)
            let colors = [
                { hex: "#e93a40", text: "#ffffff" },
                { hex: "#ffffff", text: "#000000" },
                { hex: "#1e3a5f", text: "#ffffff" },
                { hex: "#ffff00", text: "#000000" },
            ]
            for (let n = 0; n < 24; ++n) {
                let color = colors[n % colors.length]
                let count = n % 4 === 0 ? 0 : n * 7
                DockView.edge = n % 4
                DockSettings.badgePosition = n % 3
                DockSettings.badgeUseAccentColor = false
                DockSettings.badgeCustomColor = color.hex
                NotificationTracker.setUnread("org.kde.dolphin", count)
                tryCompare(item, "_badgeCount", count)
                compare(badge.visible, count > 0)
                compare(T.badgeLabel(item).text, count > 99 ? "99+" : count.toString())
                verify(Qt.colorEqual(badge.color, color.hex))
                verify(Qt.colorEqual(T.badgeLabel(item).color, color.text))
                verify(Number.isFinite(badge.x) && Number.isFinite(badge.y)
                    && Number.isFinite(badge.width) && Number.isFinite(badge.height))
                verifyBadgeCorner(item, n % 3 === 1 || (n % 3 === 2 && n % 4 === 2))
                compare(T.badge(item), badge)
            }
        }

        function test_miniDockBadgePreviewParity() {
            let comp = Qt.createComponent(Qt.resolvedUrl("../../src/qml/settings/MiniDock.qml"))
            compare(comp.status, Component.Ready, comp.errorString())
            let mini = createTemporaryObject(comp, stage, {
                apps: [{ name: "Konsole", icon: "utilities-terminal", windowState: "active", badge: 8 }]
            })
            verify(mini)
            let badge = T.findFirst(mini, o => o.objectName === "miniDockBadge")
            let label = T.findFirst(mini, o => o.objectName === "miniDockBadgeLabel")
            verify(badge, "miniDockBadge not found")
            verify(label, "miniDockBadgeLabel not found")
            let box = badge.parent.iconBox
            verify(box, "iconBox not found")

            // Top-Right (0)
            DockSettings.badgePosition = 0
            fuzzyCompare(badge.x, box.x + box.width - badge.width * 0.8, 0.01)
            // Top-Left (1)
            DockSettings.badgePosition = 1
            fuzzyCompare(badge.x, box.x - badge.width * 0.2, 0.01)
            // Auto (2): Left edge (2) -> Top-Left; Bottom (1), Top (0), Right (3) -> Top-Right
            DockSettings.badgePosition = 2
            for (let edge of [0, 1, 2, 3]) {
                mini.edge = edge
                let expectLeft = (edge === 2)
                fuzzyCompare(badge.x, expectLeft ? box.x - badge.width * 0.2 : box.x + box.width - badge.width * 0.8, 0.01)
            }

            // Accent vs custom dark/light contrast & invalid fallback
            DockSettings.badgeUseAccentColor = true
            verify(Qt.colorEqual(badge.color, Kirigami.Theme.highlightColor))
            verify(Qt.colorEqual(label.color, Kirigami.Theme.highlightedTextColor))
            DockSettings.badgeUseAccentColor = false
            DockSettings.badgeCustomColor = "#1e3a5f"
            verify(Qt.colorEqual(badge.color, "#1e3a5f"))
            verify(Qt.colorEqual(label.color, "#ffffff"))
            DockSettings.badgeCustomColor = "#ffff00"
            verify(Qt.colorEqual(badge.color, "#ffff00"))
            verify(Qt.colorEqual(label.color, "#000000"))
            DockSettings.badgeCustomColor = "invalid-hex"
            verify(Qt.colorEqual(badge.color, Kirigami.Theme.highlightColor))
            DockSettings.badgeCustomColor = "#00ffffff"
            verify(Qt.colorEqual(badge.color, Kirigami.Theme.highlightColor))
        }

        function test_animationsPageChoiceCardBadgePreviewParity() {
            let comp = Qt.createComponent(Qt.resolvedUrl("../../src/qml/settings/AnimationsPage.qml"))
            compare(comp.status, Component.Ready, comp.errorString())
            let page = createTemporaryObject(comp, stage, { width: 720, height: 600 })
            verify(page)
            let badges = T.findAll(page, o => o.objectName === "choiceCardBadge")
            compare(badges.length, 3)
            let numberBadge = badges[0]
            let dotBadge = badges[1]
            let offBadge = badges[2]
            verify(numberBadge.visible)
            verify(dotBadge.visible)
            verify(!offBadge.visible)
            let numberLabel = T.findFirst(numberBadge, o => o.objectName === "choiceCardBadgeLabel")
            verify(numberLabel)

            // Position 0 (Top-Right) vs Position 1 (Top-Left) vs Position 2 (Auto on bottom tile -> Top-Right)
            DockSettings.badgePosition = 0
            let rightX = numberBadge.x
            DockSettings.badgePosition = 1
            verify(numberBadge.x < rightX)
            DockSettings.badgePosition = 2
            fuzzyCompare(numberBadge.x, rightX, 0.01)

            // Color & contrast parity
            DockSettings.badgeUseAccentColor = false
            DockSettings.badgeCustomColor = "#1e3a5f"
            verify(Qt.colorEqual(numberBadge.color, "#1e3a5f"))
            verify(Qt.colorEqual(dotBadge.color, "#1e3a5f"))
            verify(Qt.colorEqual(numberLabel.color, "#ffffff"))
            DockSettings.badgeCustomColor = "#ffff00"
            verify(Qt.colorEqual(numberBadge.color, "#ffff00"))
            verify(Qt.colorEqual(numberLabel.color, "#000000"))
            DockSettings.badgeCustomColor = "invalid-hex"
            verify(Qt.colorEqual(numberBadge.color, Kirigami.Theme.highlightColor))
            DockSettings.badgeCustomColor = "#00ffffff"
            verify(Qt.colorEqual(numberBadge.color, Kirigami.Theme.highlightColor))
        }

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

        function test_launcherEntryCountTakesPriority() {
            let item = makeItem({ IsWindow: true })
            NotificationTracker.setUnread("org.kde.dolphin", 3)
            tryCompare(item, "_badgeCount", 3)
            LauncherEntryTracker.setEntry(launcherUrl, { count: 7, countVisible: true })
            tryCompare(item, "_badgeCount", 7)
            compare(T.badgeLabel(item).text, "7")
            LauncherEntryTracker.setEntry(launcherUrl, { countVisible: false })
            tryCompare(item, "_badgeCount", 3)
        }

        function test_launcherEntryIsPerApp() {
            let item = makeItem({ IsWindow: true })
            LauncherEntryTracker.setEntry("applications:org.kde.konsole.desktop", { count: 5, countVisible: true, progress: 40, progressVisible: true })
            compare(item._badgeCount, 0)
            verify(!T.badge(item).visible)
            verify(!T.progressBar(item).visible)
        }

        function test_launcherEntryBadgeStaysVisibleDuringDoNotDisturb() {
            NotificationTracker.dndActive = true
            let item = makeItem({ IsWindow: true })
            LauncherEntryTracker.setEntry(launcherUrl, { count: 2, countVisible: true })
            tryCompare(item, "_badgeCount", 2)
            verify(T.badge(item).visible)
        }

        function test_progressBarFollowsLauncherEntry() {
            let item = makeItem({ IsWindow: true })
            let bar = T.progressBar(item)
            verify(bar, "progress bar not found")
            verify(!bar.visible)
            LauncherEntryTracker.setEntry(launcherUrl, { progress: 50, progressVisible: true })
            tryVerify(() => bar.visible)
            let fill = bar.children[0]
            tryCompare(fill, "width", bar.width * 0.5)
            LauncherEntryTracker.setEntry(launcherUrl, { progressVisible: false })
            tryVerify(() => !bar.visible)
        }

        function test_activatingWindowClearsTrackerBadge() {
            let item = makeItem({ IsWindow: true, IsActive: false })
            NotificationTracker.setUnread("org.kde.dolphin", 4)
            tryCompare(item, "_badgeCount", 4)
            let clearRevision = NotificationTracker.revision
            DockModel.tasksModel.setTaskData(0, "IsActive", true)
            tryCompare(item, "_badgeCount", 0)
            compare(NotificationTracker.revision, clearRevision + 1)
            compare(NotificationTracker.callsTo("clearUnreadNotifications").length, 1)
            compare(NotificationTracker.callsTo("clearUnreadNotifications")[0].args[0], "org.kde.dolphin")
            verify(!T.badge(item).visible)
        }

        function test_clearingWithoutUnreadDoesNotAdvanceTrackerRevision() {
            let item = makeItem({ IsWindow: true })
            let revision = NotificationTracker.revision
            NotificationTracker.clearUnreadNotifications("org.kde.dolphin")
            compare(NotificationTracker.revision, revision)
            compare(item._badgeCount, 0)
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
            return [{ tag: "badge-increase" }, { tag: "sni-needs-attention" }, { tag: "launcher-entry-urgent" }]
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
                LauncherEntryTracker.setEntry(launcherUrl, { urgent: true })
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
