// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// The real dock root (src/qml/main.qml) with mocked backends: layout from
// settings, pointer-driven zoom, hover/tooltip/preview, clicks, wheel,
// keyboard navigation and drag reorder.
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
    height: 240

    Item {
        id: stage
        anchors.fill: parent
    }

    TestCase {
        id: tc
        name: "DockMain"
        when: windowShown

        function init() {
            KremaMocks.resetAll()
            // Keep hover timers short so condition waits finish quickly.
            DockSettings.previewHoverDelay = 50
        }

        function cleanup() {
            // Park the pointer outside the stage so the next test starts un-hovered.
            mouseMove(stage, -1, -1)
        }

        function addTasks(names, extra) {
            for (let n of names)
                DockModel.tasksModel.addTask(Object.assign({ display: n, AppId: n.toLowerCase(), IsWindow: true }, extra || {}))
        }

        function items(dock) {
            return T.findAll(dock, T.isDockItem).sort((a, b) => a.index - b.index)
        }

        function makeDock(count) {
            let comp = Qt.createComponent(Qt.resolvedUrl("../../src/qml/main.qml"))
            compare(comp.status, Component.Ready, comp.errorString())
            let dock = createTemporaryObject(comp, stage)
            verify(dock)
            tryVerify(() => items(dock).length === count, 2000, "expected " + count + " dock items")
            for (let it of items(dock))
                tryVerify(() => it._zoomAnimReady, 2000)
            tryVerify(() => DockVisibility.panelRect.width > 0)
            return dock
        }

        // Item center in stage coordinates (valid while the item is at rest scale 1).
        function centerOf(item) {
            return item.mapToItem(stage, item.width / 2, item.height / 2)
        }

        function hoverItem(item) {
            let c = centerOf(item)
            mouseMove(stage, c.x, c.y)
            return c
        }

        // --- Layout from settings ---

        function test_oneDelegatePerTask() {
            addTasks(["Dolphin", "Konsole", "Kate"])
            let dock = makeDock(3)
            let its = items(dock)
            compare(its.map(i => i.displayName), ["Dolphin", "Konsole", "Kate"])
            DockModel.tasksModel.addTask({ display: "Okular", IsWindow: true })
            tryVerify(() => items(dock).length === 4)
            DockModel.tasksModel.removeTask(0)
            tryVerify(() => items(dock).length === 3)
            compare(items(dock)[0].displayName, "Konsole")
        }

        function test_iconSizeAndSpacingDriveLayout_data() {
            return [
                { tag: "defaults", size: 48, spacing: 4 },
                { tag: "small-tight", size: 32, spacing: 0 },
                { tag: "large-wide", size: 72, spacing: 16 },
            ]
        }

        function test_iconSizeAndSpacingDriveLayout(data) {
            DockSettings.iconSize = data.size
            DockSettings.iconSpacing = data.spacing
            addTasks(["A", "B", "C"])
            let its = items(makeDock(3))
            for (let i = 0; i < 3; i++) compare(its[i].width, data.size)
            tryCompare(its[1], "x", its[0].x + data.size + data.spacing)
            tryCompare(its[2], "x", its[1].x + data.size + data.spacing)
            compare(its[0].y, its[1].y)
        }

        function test_spacingChangeRelayoutsLive() {
            addTasks(["A", "B"])
            let its = items(makeDock(2))
            tryCompare(its[1], "x", its[0].x + 48 + 4)
            DockSettings.iconSpacing = 12
            tryCompare(its[1], "x", its[0].x + 48 + 12)
            DockSettings.iconSize = 64
            tryCompare(its[1], "x", its[0].x + 64 + 12)
        }

        function test_panelHugsContent() {
            addTasks(["A", "B", "C", "D", "E"])
            makeDock(5)
            // Panel = content + large spacing on both sides (5*48 + 4*4 = 256).
            let expected = Math.max(5 * 48 + 4 * 4 + Kirigami.Units.largeSpacing * 2, Kirigami.Units.gridUnit * 6)
            tryVerify(() => DockVisibility.panelRect.width === expected, 3000,
                      "panel width " + DockVisibility.panelRect.width + " != " + expected)
            let before = DockVisibility.panelRect.width
            DockSettings.iconSize = 64
            tryVerify(() => DockVisibility.panelRect.width > before, 3000, "panel did not grow with icon size")
        }

        function test_panelIsCenteredOnBottomEdge() {
            addTasks(["A", "B", "C"])
            makeDock(3)
            tryVerify(() => Math.abs(DockVisibility.panelRect.x + DockVisibility.panelRect.width / 2 - stage.width / 2) < 1)
            verify(DockVisibility.panelRect.y + DockVisibility.panelRect.height <= stage.height)
            verify(DockVisibility.panelRect.y > stage.height / 2, "bottom dock panel must sit in the lower half")
        }

        function test_verticalDockStacksItems() {
            DockView.edge = 2
            addTasks(["A", "B", "C"])
            let its = items(makeDock(3))
            compare(its[0].height, 48)
            compare(its[0].x, its[1].x)
            tryCompare(its[1], "y", its[0].y + 48 + 4)
            verify(DockVisibility.panelRect.x < stage.width / 2, "left dock panel must sit on the left")
        }

        // --- Pointer-driven zoom through main.qml's hit testing ---

        function test_hoverZoomsItemUnderPointer() {
            addTasks(["A", "B", "C", "D", "E"])
            let dock = makeDock(5)
            let its = items(dock)
            hoverItem(its[2])
            tryCompare(dock, "hoveredIndex", 2)
            compare(dock.hoveredName, "C")
            tryCompare(its[2], "currentScale", DockSettings.maxZoomFactor)
            fuzzyCompare(its[1].zoomScale, its[3].zoomScale, 1e-6)
            verify(its[1].zoomScale > its[0].zoomScale && its[0].zoomScale > 1.0)
            compare(its[2].z, 1)
            compare(its[1].z, 0)
        }

        function panelEdges() {
            let r = DockVisibility.panelRect
            return { left: r.x, right: r.x + r.width }
        }

        // Parabolic (default): the magnified row pushes neighbours aside and the
        // reported dock background grows by the row's growth, evenly on both
        // sides for a middle icon, then returns to its rest extent.
        function test_parabolicHoverGrowsBackgroundAndPushesNeighbours() {
            addTasks(["A", "B", "C", "D", "E"])
            let dock = makeDock(5)
            let its = items(dock)
            let rest = panelEdges()
            hoverItem(its[2])
            tryCompare(its[2], "currentScale", DockSettings.maxZoomFactor)
            let growth = 0
            for (let it of its) growth += DockSettings.iconSize * (it.zoomScale - 1)
            let zoomed = panelEdges()
            // Integer rect, rounded outward: at most 1 px per side.
            verify(Math.abs((rest.left - zoomed.left) - growth / 2) <= 1, "left edge grew by " + (rest.left - zoomed.left) + ", expected " + growth / 2)
            verify(Math.abs((zoomed.right - rest.right) - growth / 2) <= 1, "right edge grew by " + (zoomed.right - rest.right) + ", expected " + growth / 2)
            for (let i = 0; i + 1 < its.length; i++) {
                let a = its[i], b = its[i + 1]
                let aRight = a.itemCenterX + a.currentOffset + a.width * a.currentScale / 2
                let bLeft = b.itemCenterX + b.currentOffset - b.width * b.currentScale / 2
                verify(Math.abs((bLeft - aRight) - DockSettings.iconSpacing) < 1e-6, "gap " + i + " is " + (bLeft - aRight))
            }
            verify(its[0].currentOffset < 0 && its[4].currentOffset > 0, "outer icons not pushed aside")

            mouseMove(stage, stage.width / 2, 2)
            for (let it of its) tryCompare(it, "currentOffset", 0)
            tryVerify(() => panelEdges().left === rest.left && panelEdges().right === rest.right, 2000, "background did not return to rest")
        }

        // In place: icons magnify over their neighbours; nothing moves and the
        // background keeps its rest extent.
        function test_inPlaceHoverKeepsBackgroundAndPositions() {
            DockSettings.zoomStyle = 1
            addTasks(["A", "B", "C", "D", "E"])
            let dock = makeDock(5)
            let its = items(dock)
            let rest = panelEdges()
            hoverItem(its[2])
            tryCompare(its[2], "currentScale", DockSettings.maxZoomFactor)
            for (let it of its) compare(it.currentOffset, 0)
            compare(panelEdges().left, rest.left)
            compare(panelEdges().right, rest.right)
        }

        function test_zoomSettlesWhenPointerLeavesPanel() {
            addTasks(["A", "B", "C"])
            let dock = makeDock(3)
            let its = items(dock)
            hoverItem(its[1])
            tryCompare(its[1], "currentScale", DockSettings.maxZoomFactor)
            // Far above the panel's zoom zone.
            mouseMove(stage, stage.width / 2, 2)
            tryCompare(dock, "hoveredIndex", -1)
            for (let it of its) tryCompare(it, "currentScale", 1.0)
        }

        function test_zoomDisabledByUnitFactor() {
            DockSettings.maxZoomFactor = 1.0
            addTasks(["A", "B", "C"])
            let dock = makeDock(3)
            let its = items(dock)
            hoverItem(its[1])
            tryCompare(dock, "hoveredIndex", 1)
            for (let it of its) compare(it.currentScale, 1.0)
        }

        function test_hoverReportsToVisibilityController() {
            addTasks(["A"])
            let dock = makeDock(1)
            hoverItem(items(dock)[0])
            tryCompare(DockVisibility, "hovered", true)
        }

        // --- Tooltip / preview trigger ---

        function test_launcherHoverShowsTooltipNotPreview() {
            DockModel.tasksModel.addTask({ display: "Krema Launcher Fixture", IsWindow: false, IsLauncher: true })
            let dock = makeDock(1)
            hoverItem(items(dock)[0])
            tryVerify(() => T.findFirst(dock, o => o.text === "Krema Launcher Fixture" && o.visible) !== null,
                      2000, "tooltip with the app name never appeared")
            compare(PreviewController.callsTo("showPreview").length, 0)
        }

        function test_windowHoverOpensPreview() {
            addTasks(["A", "B"])
            let dock = makeDock(2)
            hoverItem(items(dock)[1])
            tryCompare(PreviewController, "visible", true)
            compare(PreviewController.parentIndex, 1)
        }

        function test_previewDisabledFallsBackToTooltip() {
            DockSettings.previewEnabled = false
            addTasks(["Konsole"])
            let dock = makeDock(1)
            hoverItem(items(dock)[0])
            tryVerify(() => T.findFirst(dock, o => o.text === "Konsole" && o.visible) !== null)
            compare(PreviewController.callsTo("showPreview").length, 0)
        }

        // --- Mouse buttons and wheel ---

        function test_mouseButtonsDispatchActions_data() {
            return [
                { tag: "left-activates", button: Qt.LeftButton, target: "DockActions", call: "activate" },
                { tag: "middle-new-instance", button: Qt.MiddleButton, target: "DockActions", call: "newInstance" },
                { tag: "right-context-menu", button: Qt.RightButton, target: "DockContextMenu", call: "showForTask" },
            ]
        }

        function test_mouseButtonsDispatchActions(data) {
            addTasks(["A", "B", "C"])
            let dock = makeDock(3)
            let c = hoverItem(items(dock)[1])
            tryCompare(dock, "hoveredIndex", 1)
            mouseClick(stage, c.x, c.y, data.button)
            let mock = data.target === "DockActions" ? DockActions : DockContextMenu
            tryVerify(() => mock.callsTo(data.call).length === 1)
            compare(mock.callsTo(data.call)[0].args[0], 1)
        }

        function test_wheelCyclesWindows() {
            addTasks(["A", "B"])
            let dock = makeDock(2)
            let c = hoverItem(items(dock)[0])
            tryCompare(dock, "hoveredIndex", 0)
            mouseWheel(stage, c.x, c.y, 0, 120)
            mouseWheel(stage, c.x, c.y, 0, -120)
            let calls = DockActions.callsTo("cycleWindows")
            compare(calls.length, 2)
            compare(calls[0].args, [0, false])
            compare(calls[1].args, [0, true])
        }

        // --- Launch feedback ---

        function test_launchSignalBouncesRunningApp() {
            addTasks(["A", "B"])
            let dock = makeDock(2)
            let its = items(dock)
            DockActions.taskLaunching(1)
            verify(its[1].launching)
            verify(!its[0].launching)
            compare(its[1].accessibleDescription, "Starting")
            // No startup task arrives (TasksModel filters them for apps with a
            // window): the feedback outlives the 500 ms handoff bridge...
            wait(800)
            verify(its[1].launching)
            // ...and ends when the new window joins the group.
            DockModel.tasksModel.setTaskData(1, "ChildCount", 2)
            tryCompare(its[1], "launching", false)
        }

        function test_launchSignalOnActiveAppEndsWithoutNewWindow() {
            addTasks(["A"], { IsActive: true })
            let dock = makeDock(1)
            let item = items(dock)[0]
            DockActions.taskLaunching(0)
            verify(item.launching)
            // A single-instance app ignoring the request opens no window: the
            // no-op detection ends the feedback after the launch feedback timeout.
            wait(3000)
            verify(item.launching)
            tryCompare(item, "launching", false, 5000)
        }

        function test_launchSignalBouncesLauncherUntilItsWindowMaps() {
            // A pinned launcher with no startup task (a plain KWin session never
            // reports one): the click alone must drive the feedback, past the
            // 500 ms startup handoff, until the launcher row gives way to the app.
            DockModel.tasksModel.addTask({ display: "Launcher", IsWindow: false, IsLauncher: true })
            let item = items(makeDock(1))[0]
            DockActions.taskLaunching(0)
            verify(item.launching)
            compare(item.accessibleDescription, "Pinned, Starting")
            wait(1000)
            verify(item.launching, "launch feedback dropped before the app's window mapped")
        }

        function test_startupNotificationDrivesLaunchState() {
            addTasks(["A"])
            let item = items(makeDock(1))[0]
            DockModel.tasksModel.setTaskData(0, "IsStartup", true)
            tryCompare(item, "launching", true)
            // A new window appearing ends the launch state.
            DockModel.tasksModel.setTaskData(0, "IsStartup", false)
            DockModel.tasksModel.setTaskData(0, "ChildCount", 2)
            tryCompare(item, "launching", false)
        }

        // --- Keyboard navigation ---

        function test_keyboardNavigationMovesFocusAndActivates() {
            addTasks(["A", "B", "C"])
            let dock = makeDock(3)
            let its = items(dock)
            dock.startKeyboardNavigation()
            compare(dock.hoveredIndex, 0)
            verify(its[0].isKeyboardFocused)
            tryCompare(its[0], "currentScale", DockSettings.maxZoomFactor)
            keyClick(Qt.Key_Right)
            tryCompare(dock, "hoveredIndex", 1)
            verify(its[1].isKeyboardFocused && !its[0].isKeyboardFocused)
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Right) // clamps at the last item
            tryCompare(dock, "hoveredIndex", 2)
            keyClick(Qt.Key_Left)
            tryCompare(dock, "hoveredIndex", 1)
            keyClick(Qt.Key_Return)
            tryVerify(() => DockActions.callsTo("activate").length === 1)
            compare(DockActions.callsTo("activate")[0].args[0], 1)
            compare(dock.keyboardNavigating, false)
            compare(dock.hoveredIndex, -1)
            for (let it of its) tryCompare(it, "currentScale", 1.0)
        }

        function test_keyboardEscapeEndsNavigation() {
            addTasks(["A", "B"])
            let dock = makeDock(2)
            dock.startKeyboardNavigation()
            keyClick(Qt.Key_Right)
            tryCompare(dock, "hoveredIndex", 1)
            keyClick(Qt.Key_Escape)
            tryCompare(dock, "keyboardNavigating", false)
            compare(DockVisibility.keyboardActive, false)
            compare(DockActions.callsTo("activate").length, 0)
        }

        function test_pointerMotionOffDockEndsKeyboardNavigation() {
            addTasks(["A", "B"])
            let dock = makeDock(2)
            let its = items(dock)
            dock.startKeyboardNavigation()
            DockVisibility.setKeyboardActive(true)
            PreviewController.startPreviewKeyboardNav()
            verify(its[0].isKeyboardFocused)
            DockVisibility.pointerMovedDuringKeyboardNavigation()
            compare(dock.keyboardNavigating, false)
            compare(dock.hoveredIndex, -1)
            verify(!its[0].isKeyboardFocused)
            compare(DockVisibility.keyboardActive, false)
            compare(PreviewController.previewKeyboardActive, false)
            // Keys no longer navigate.
            keyClick(Qt.Key_Right)
            compare(dock.hoveredIndex, -1)
            for (let it of its) tryCompare(it, "currentScale", 1.0)
        }

        function test_keyboardVerticalDockUsesUpDown() {
            DockView.edge = 3
            addTasks(["A", "B"])
            let dock = makeDock(2)
            dock.startKeyboardNavigation()
            keyClick(Qt.Key_Right) // not a navigation key on a vertical dock
            compare(dock.hoveredIndex, 0)
            keyClick(Qt.Key_Down)
            tryCompare(dock, "hoveredIndex", 1)
        }

        function test_keyboardOpensPreviewForWindows() {
            addTasks(["A"])
            DockModel.tasksModel.addTask({ display: "Launcher", IsWindow: false })
            let dock = makeDock(2)
            dock.startKeyboardNavigation()
            keyClick(Qt.Key_Up) // bottom dock: Up opens the preview
            tryCompare(PreviewController, "visible", true)
            compare(PreviewController.parentIndex, 0)
            compare(PreviewController.previewKeyboardActive, true)
            keyClick(Qt.Key_Escape) // back to dock navigation, preview stays
            compare(PreviewController.previewKeyboardActive, false)
            compare(dock.keyboardNavigating, true)
            PreviewController.hidePreview()
            keyClick(Qt.Key_Right)
            tryCompare(dock, "hoveredIndex", 1)
            let before = PreviewController.callsTo("showPreview").length
            keyClick(Qt.Key_Up) // launcher: nothing to preview
            compare(PreviewController.callsTo("showPreview").length, before)
        }

        // --- Drag reorder and drop helpers ---

        function test_computeDropIndexPicksNearestIcon() {
            addTasks(["A", "B", "C", "D"])
            let dock = makeDock(4)
            let its = items(dock)
            for (let i = 0; i < its.length; i++) {
                let cx = its[i].mapToItem(dock, its[i].width / 2, 0).x
                compare(dock.computeDropIndex(cx), i)
                compare(dock.computeDropIndex(cx + its[i].width * 0.4), i)
            }
            compare(dock.computeDropIndex(-1000), 0)
            compare(dock.computeDropIndex(100000), 3)
        }

        function test_isDesktopFileUrl_data() {
            return [
                { tag: "file-desktop", url: "file:///usr/share/applications/org.kde.kate.desktop", expect: true },
                { tag: "applications-scheme", url: "applications:org.kde.dolphin.desktop", expect: true },
                { tag: "plain-file", url: "file:///home/user/notes.txt", expect: false },
                { tag: "web", url: "https://kde.org", expect: false },
            ]
        }

        function test_isDesktopFileUrl(data) {
            addTasks(["A"])
            let dock = makeDock(1)
            compare(dock.isDesktopFileUrl(data.url), data.expect)
        }

        function test_pressHoldDragReordersTask() {
            addTasks(["A", "B", "C"])
            let dock = makeDock(3)
            let its = items(dock)
            let from = centerOf(its[0])
            let to = centerOf(its[2])
            mouseMove(stage, from.x, from.y)
            tryCompare(dock, "hoveredIndex", 0)
            mousePress(stage, from.x, from.y, Qt.LeftButton)
            // Press-and-hold arms the drag (300 ms hold timer).
            tryCompare(dock, "_dragPending", true, 2000)
            mouseMove(stage, from.x + 20, from.y, -1, Qt.LeftButton)
            tryCompare(dock, "_dragActive", true)
            verify(its[0].isDragSource)
            compare(DockVisibility.interacting, true)
            mouseMove(stage, to.x, to.y, -1, Qt.LeftButton)
            tryCompare(dock, "_dragTargetIndex", 2)
            mouseRelease(stage, to.x, to.y, Qt.LeftButton)
            tryVerify(() => DockActions.callsTo("moveTask").length === 1)
            compare(DockActions.callsTo("moveTask")[0].args, [0, 2])
            compare(dock._dragActive, false)
            compare(DockVisibility.interacting, false)
            // The drag must not also count as a click.
            compare(DockActions.callsTo("activate").length, 0)
        }
    }
}
