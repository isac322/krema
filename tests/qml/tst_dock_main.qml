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

        function addGroupTask(name = "Grouped") {
            let row = DockModel.tasksModel.addTask({
                display: name,
                AppId: name.toLowerCase(),
                IsWindow: true,
                IsGroupParent: true,
            })
            DockModel.tasksModel.addChildTask(row, {
                display: name + " child 1",
                AppId: (name + " child 1").toLowerCase(),
                IsWindow: true,
                IsActive: true,
            })
            DockModel.tasksModel.addChildTask(row, {
                display: name + " child 2",
                AppId: (name + " child 2").toLowerCase(),
                IsWindow: true,
                IsActive: false,
            })
            return row
        }

        function dockTooltip(dock) {
            return T.findFirst(dock, o => o.objectName === "dockTooltip")
        }

        function items(dock) {
            return T.findAll(dock, T.isDockItem).sort((a, b) => a.index - b.index)
        }

        property int settledTurns: 0

        // Lets deferred (Qt.callLater) post-layout delegate setup run.
        function settleDeferredCallbacks() {
            let next = settledTurns + 1
            Qt.callLater(function() {
                Qt.callLater(function() { tc.settledTurns = next })
            })
            tryCompare(tc, "settledTurns", next)
        }

        function makeDock(count) {
            let comp = Qt.createComponent(Qt.resolvedUrl("../../src/qml/main.qml"))
            compare(comp.status, Component.Ready, comp.errorString())
            let dock = createTemporaryObject(comp, stage)
            verify(dock)
            tryVerify(() => items(dock).length === count, 2000, "expected " + count + " dock items")
            for (let it of items(dock))
                tryVerify(() => it._delegateGeometryReady, 2000)
            settleDeferredCallbacks()
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
        function dockEdges() {
            return [
                { tag: "top", edge: 0 },
                { tag: "bottom", edge: 1 },
                { tag: "left", edge: 2 },
                { tag: "right", edge: 3 },
            ]
        }

        // Painted delegate bounds include Scale/Translate, unlike Flow x/y.
        function drawnBounds(item, target = stage) {
            let near = item.mapToItem(target, 0, 0)
            let far = item.mapToItem(target, item.width, item.height)
            return { x: near.x, y: near.y, width: far.x - near.x, height: far.y - near.y }
        }

        function primaryNear(rect) {
            return DockView.isVertical ? rect.y : rect.x
        }

        function primaryExtent(rect) {
            return DockView.isVertical ? rect.height : rect.width
        }

        function secondaryCenter(rect) {
            return DockView.isVertical ? rect.x + rect.width / 2 : rect.y + rect.height / 2
        }

        function addSeparatedTasks() {
            addTasks(["Pinned A", "Pinned B"], { IsLauncher: true })
            addTasks(["Running A", "Running B"])
            DockModel.pinnedTaskCount = 2
        }

        function separatorOf(dock) {
            return T.findFirst(dock, o => o.objectName === "taskZonesSeparator")
        }

        function verifyRestProxies(dock, its, restBounds) {
            for (let i = 0; i < its.length; ++i) {
                let proxy = drawnBounds(its[i].delegateGeometryTarget, dock)
                fuzzyCompare(proxy.x, restBounds[i].x, 1e-6)
                fuzzyCompare(proxy.y, restBounds[i].y, 1e-6)
                fuzzyCompare(proxy.width, restBounds[i].width, 1e-6)
                fuzzyCompare(proxy.height, restBounds[i].height, 1e-6)
            }
            // A minimize/restore consumer receives the painted REST slot,
            // including separation, never the transient hover translation.
            tryVerify(() => {
                return its.every((it, index) => {
                    let requests = DockModel.delegateGeometryRequests.filter(r => r.index === index)
                    if (requests.length === 0) return false
                    let actual = requests[requests.length - 1].geometry
                    let expected = restBounds[index]
                    return Math.abs(actual.x - expected.x) < 1e-6
                        && Math.abs(actual.y - expected.y) < 1e-6
                        && Math.abs(actual.width - expected.width) < 1e-6
                        && Math.abs(actual.height - expected.height) < 1e-6
                })
            }, 3000, "published window targets did not match the allocated rest slots")
        }

        function verifySeparatorClearance(dock, its) {
            let before = drawnBounds(its[1])
            let after = drawnBounds(its[2])
            let separator = drawnBounds(separatorOf(dock))
            fuzzyCompare(primaryExtent(separator), 1, 1e-6)
            fuzzyCompare(primaryNear(separator) - primaryNear(before) - primaryExtent(before),
                         DockSettings.iconSpacing, 0.51)
            fuzzyCompare(primaryNear(after) - primaryNear(separator) - primaryExtent(separator),
                         DockSettings.iconSpacing, 0.51)
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
        function test_separatedLayoutToggleKeepsSpacingAndSecondaryCenter_data() {
            return dockEdges()
        }

        function test_separatedLayoutToggleKeepsSpacingAndSecondaryCenter(data) {
            DockView.edge = data.edge
            DockSettings.iconSize = 32
            DockSettings.iconSpacing = 7
            DockSettings.maxZoomFactor = 1
            DockSettings.previewEnabled = false
            addSeparatedTasks()
            let dock = makeDock(4)
            let its = items(dock)
            let ordinary = its.map(it => drawnBounds(it))
            let ordinarySpan = primaryNear(ordinary[3]) + primaryExtent(ordinary[3]) - primaryNear(ordinary[0])
            compare(ordinarySpan, 4 * 32 + 3 * 7)
            verify(!separatorOf(dock).visible)

            DockSettings.separateLaunchers = true
            tryVerify(() => {
                let before = drawnBounds(its[1])
                let after = drawnBounds(its[2])
                let first = drawnBounds(its[0])
                let last = drawnBounds(its[3])
                return Math.abs(primaryNear(after) - primaryNear(before) - primaryExtent(before) - 15) < 1e-6
                    && Math.abs((primaryNear(first) + primaryNear(last) + primaryExtent(last)) / 2
                        - (DockView.isVertical ? stage.height : stage.width) / 2) < 1e-6
            }, 3000, "separated row did not settle into its centered stroke slot")
            verify(separatorOf(dock).visible)
            verifySeparatorClearance(dock, its)
            let resting = its.map(it => drawnBounds(it, dock))
            verifyRestProxies(dock, its, resting)
            for (let i = 0; i < its.length; ++i)
                fuzzyCompare(secondaryCenter(drawnBounds(its[i])), secondaryCenter(ordinary[i]), 1e-6)

            // The nearest-drop midpoint and trailing hit edge both move with
            // the running zone. Raw Flow coordinates select the wrong task.
            dock._dragSourceIndex = 2
            let a = drawnBounds(its[2], dock)
            let b = drawnBounds(its[3], dock)
            let midpoint = (primaryNear(a) + primaryExtent(a) / 2
                + primaryNear(b) + primaryExtent(b) / 2) / 2
            compare(dock.computeDropIndex(midpoint - 1), 2)
            compare(dock.computeDropIndex(midpoint + 1), 3)
            if (!DockView.isVertical) {
                let panelBounds = drawnBounds(its[2], its[2].parent.parent)
                compare(dock.computeExternalDropIndex(panelBounds.x + panelBounds.width - 1), 2)
                compare(dock.computeExternalDropIndex(panelBounds.x - 1), -1)
            }
            let drawn = drawnBounds(its[2])
            let point = DockView.isVertical
                ? Qt.point(drawn.x + drawn.width / 2, drawn.y + drawn.height - 1)
                : Qt.point(drawn.x + drawn.width - 1, drawn.y + drawn.height / 2)
            mouseMove(stage, point.x, point.y)
            tryCompare(dock, "hoveredIndex", 2)
            mouseMove(stage, -1, -1)

            DockSettings.separateLaunchers = false
            tryVerify(() => {
                let first = drawnBounds(its[0])
                let last = drawnBounds(its[3])
                return Math.abs(primaryNear(last) + primaryExtent(last) - primaryNear(first) - ordinarySpan) < 1e-6
                    && Math.abs(primaryNear(first) - primaryNear(ordinary[0])) < 1e-6
            }, 3000, "ordinary row did not return after disabling separation")
            verify(!separatorOf(dock).visible)
            for (let i = 0; i < its.length; ++i)
                fuzzyCompare(secondaryCenter(drawnBounds(its[i])), secondaryCenter(ordinary[i]), 1e-6)
        }

        function test_singleZoneAndEmptyModelHaveNoSeparatorSlot_data() {
            return [
                { tag: "no-pinned", edge: 1, count: 4, pinned: 0 },
                { tag: "only-pinned-vertical", edge: 2, count: 4, pinned: 4 },
                { tag: "empty-model", edge: 1, count: 0, pinned: 0 },
            ]
        }

        function test_singleZoneAndEmptyModelHaveNoSeparatorSlot(data) {
            DockView.edge = data.edge
            DockSettings.iconSize = 32
            DockSettings.iconSpacing = 7
            DockSettings.maxZoomFactor = 1
            for (let i = 0; i < data.count; ++i)
                addTasks(["Task " + i], { IsLauncher: i < data.pinned })
            DockModel.pinnedTaskCount = data.pinned
            let dock = makeDock(data.count)
            let its = items(dock)
            let ordinary = its.map(it => drawnBounds(it))
            DockSettings.separateLaunchers = true
            wait(Kirigami.Units.longDuration + 50)
            verify(!separatorOf(dock).visible)
            for (let i = 0; i < its.length; ++i) {
                let actual = drawnBounds(its[i])
                fuzzyCompare(primaryNear(actual), primaryNear(ordinary[i]), 1e-6)
                fuzzyCompare(secondaryCenter(actual), secondaryCenter(ordinary[i]), 1e-6)
            }
            let expected = Math.max(data.count === 0 ? 0 : data.count * 32 + (data.count - 1) * 7,
                                    0) + Kirigami.Units.largeSpacing * 2
            expected = Math.max(expected, Kirigami.Units.gridUnit * 6)
            tryVerify(() => Math.abs(primaryExtent(DockVisibility.panelRect) - expected) <= 1)
        }

        function test_separatedParabolicHoverPreservesGapAndRestProxy_data() {
            return dockEdges()
        }

        function test_separatedParabolicHoverPreservesGapAndRestProxy(data) {
            DockView.edge = data.edge
            DockSettings.iconSize = 32
            DockSettings.iconSpacing = 7
            DockSettings.separateLaunchers = true
            DockSettings.previewEnabled = false
            addSeparatedTasks()
            let dock = makeDock(4)
            let its = items(dock)
            let resting = its.map(it => drawnBounds(it, dock))
            verifyRestProxies(dock, its, resting)
            // Qt's synthetic pointer uses integer coordinates, while the
            // centered odd-width row has half-pixel rest centres. A rounded
            // cursor must follow the Gaussian, not be treated as its peak.
            let restCenter = centerOf(its[2])
            let cursor = Qt.point(Math.round(restCenter.x), Math.round(restCenter.y))
            mouseMove(stage, cursor.x, cursor.y)
            tryCompare(dock, "hoveredIndex", 2)
            let panel = its[2].parent.parent
            let distance = panel.zoomCursor - its[2].itemCenterX
            verify(Math.abs(distance) <= 0.5, "actual cursor left the rounded rest-centre pixel")
            let sigma = DockSettings.iconSize * 1.2
            let expectedScale = 1 + (DockSettings.maxZoomFactor - 1)
                * Math.exp(-distance * distance / (sigma * sigma))
            tryVerify(() => Math.abs(its[2].currentScale - expectedScale) < 1e-9,
                      2000, "rounded pointer did not reach its expected Gaussian scale")
            verifySeparatorClearance(dock, its)
            verifyRestProxies(dock, its, resting)
            let drawn = drawnBounds(its[2])
            let point = Qt.point(drawn.x + drawn.width / 2, drawn.y + drawn.height / 2)
            mouseClick(stage, point.x, point.y, Qt.LeftButton)
            let activations = DockActions.callsTo("activate")
            compare(activations[activations.length - 1].args[0], 2)
            mouseMove(stage, -1, -1)
            for (let it of its)
                tryCompare(it, "currentOffset", 0)
            verifyRestProxies(dock, its, resting)
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
            // Leaving eases zoomAmount out: every icon settles back at rest.
            mouseMove(stage, stage.width / 2, 2)
            for (let it of its) tryCompare(it, "currentScale", 1.0)
            for (let it of its) compare(it.currentOffset, 0)
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
            verify(!PreviewController.visible)
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
            verify(!PreviewController.visible)
        }

        function test_groupPreviewClickShowsPopupAndSuppressesTooltip() {
            // Grouped explicit preview must work independently of hover preview.
            DockSettings.previewEnabled = false
            DockSettings.groupedWindowClickAction = 1
            addGroupTask()
            let dock = makeDock(1)
            let item = items(dock)[0]
            let c = hoverItem(item)
            let tooltip = dockTooltip(dock)
            tryVerify(() => tooltip && tooltip.visible, 2000,
                      "group tooltip never appeared before the click")
            verify(!PreviewController.visible)

            mouseClick(stage, c.x, c.y, Qt.LeftButton)
            tryCompare(PreviewController, "visible", true, 2000)
            compare(PreviewController.parentIndex, 0)
            verify(!tooltip.visible, "explicit preview must hide the text tooltip")

            // Repeated clicks keep the same popup open rather than toggling it.
            mouseClick(stage, c.x, c.y, Qt.LeftButton)
            tryCompare(PreviewController, "visible", true, 2000)
            compare(PreviewController.parentIndex, 0)
            verify(!tooltip.visible)
        }

        function test_groupPreviewClickStopsDelayedTooltip() {
            DockSettings.previewEnabled = false
            DockSettings.previewHoverDelay = 500
            DockSettings.groupedWindowClickAction = 1
            addGroupTask()
            let dock = makeDock(1)
            let item = items(dock)[0]
            let c = hoverItem(item)
            let tooltip = dockTooltip(dock)

            // Click before the hover timer fires.
            mouseClick(stage, c.x, c.y, Qt.LeftButton)
            tryCompare(PreviewController, "visible", true, 2000)
            let deadline = Date.now() + DockSettings.previewHoverDelay + 100
            tryVerify(() => tooltip.visible || Date.now() >= deadline, 2000)
            verify(!tooltip.visible, "the delayed text tooltip reopened after explicit preview")
        }

        function test_groupPreviewClickReenterDuringHideKeepsTooltipHidden() {
            DockSettings.previewEnabled = false
            DockSettings.groupedWindowClickAction = 1
            addGroupTask()
            let dock = makeDock(1)
            let item = items(dock)[0]
            let c = hoverItem(item)
            let tooltip = dockTooltip(dock)
            tryVerify(() => tooltip && tooltip.visible, 2000)

            mouseClick(stage, c.x, c.y, Qt.LeftButton)
            tryCompare(PreviewController, "visible", true, 2000)
            verify(!tooltip.visible)

            // The mock keeps the preview surface alive during its delayed hide.
            mouseMove(stage, -1, -1)
            mouseMove(stage, c.x, c.y)
            let deadline = Date.now() + DockSettings.previewHoverDelay + 100
            tryVerify(() => tooltip.visible || Date.now() >= deadline, 2000)
            verify(!tooltip.visible, "re-entering while preview hide is pending must not overlap text")
        }

        function test_previewCloseWhilePointerOverItemRestartsTextTooltip() {
            DockSettings.previewEnabled = false
            DockSettings.groupedWindowClickAction = 1
            addGroupTask()
            let dock = makeDock(1)
            let item = items(dock)[0]
            let c = hoverItem(item)
            let tooltip = dockTooltip(dock)
            tryVerify(() => tooltip && tooltip.visible, 2000)

            mouseClick(stage, c.x, c.y, Qt.LeftButton)
            tryCompare(PreviewController, "visible", true, 2000)
            verify(!tooltip.visible)

            // Closing the preview while the pointer remains over the item must
            // resume the ordinary launcher/text tooltip path.
            PreviewController.hidePreview()
            tryCompare(tooltip, "visible", true, 2000)
        }

        function test_fastLauncherTooltipAtZeroDelay() {
            DockSettings.previewHoverDelay = 0
            DockModel.tasksModel.addTask({
                display: "Fast Launcher",
                IsWindow: false,
                IsLauncher: true,
            })
            let dock = makeDock(1)
            hoverItem(items(dock)[0])
            tryCompare(dockTooltip(dock), "visible", true, 2000)
            verify(!PreviewController.visible)
        }

        function test_fastLauncherTooltipRecoversAfterPreviewClose() {
            DockSettings.previewHoverDelay = 0
            addTasks(["Window"])
            DockModel.tasksModel.addTask({
                display: "Fast Launcher",
                IsWindow: false,
                IsLauncher: true,
            })
            let dock = makeDock(2)
            let its = items(dock)
            hoverItem(its[0])
            tryCompare(PreviewController, "visible", true, 2000)
            hoverItem(its[1])
            tryCompare(its[1], "currentScale", DockSettings.maxZoomFactor, 2000)

            // Only recovery is asserted here. Overlap while a surface is
            // pending hide is covered by the separate explicit-preview test.
            PreviewController.hidePreview()
            tryCompare(dockTooltip(dock), "visible", true, 2000)
        }

        function test_previewClosePreservesPendingHoverDeadline() {
            DockSettings.previewHoverDelay = 1000
            addTasks(["A", "B"])
            let dock = makeDock(2)
            let its = items(dock)
            hoverItem(its[0])
            tryCompare(PreviewController, "visible", true, 2000)
            compare(PreviewController.parentIndex, 0)

            let started = Date.now()
            hoverItem(its[1])
            tryCompare(dock, "hoveredIndex", 1, 2000)
            tryVerify(() => Date.now() - started >= 750, 2000)
            verify(PreviewController.visible && PreviewController.parentIndex === 0,
                "runner was too slow: B appeared before A could close while its hover was pending")

            // Most of B's original hover delay has elapsed before A closes.
            // Restarting that delay would make B wait a full second again.
            let closedAt = Date.now()
            PreviewController.hidePreview()
            let observedAfterClose = -1
            tryVerify(() => {
                if (!PreviewController.visible || PreviewController.parentIndex !== 1)
                    return false
                if (observedAfterClose < 0)
                    observedAfterClose = Date.now() - closedAt
                return true
            }, 3000, "B's hover preview never appeared after A closed")
            verify(observedAfterClose < 750,
                "closing A restarted B's pending hover delay: B appeared after "
                + observedAfterClose + " ms")
            verify(!dockTooltip(dock).visible)
        }

        function test_previewInvalidationDuringDragDoesNotReopenTooltip() {
            DockSettings.previewEnabled = false
            DockSettings.groupedWindowClickAction = 1
            addGroupTask()
            addTasks(["Fallback"])
            let dock = makeDock(2)
            let item = items(dock)[0]
            let c = hoverItem(item)
            mouseClick(stage, c.x, c.y, Qt.LeftButton)
            tryCompare(PreviewController, "visible", true, 2000)

            mousePress(stage, c.x, c.y, Qt.LeftButton)
            let delta = 20
            tryVerify(() => {
                mouseMove(stage, c.x + delta, c.y, -1, Qt.LeftButton)
                delta = delta === 20 ? 21 : 20
                return item.isDragSource
            }, 2000, "drag source visual feedback never appeared")
            PreviewController.hidePreview()
            DockModel.tasksModel.removeTask(0)
            tryVerify(() => items(dock).length === 1, 2000)
            let tooltip = dockTooltip(dock)
            let deadline = Date.now() + DockSettings.previewHoverDelay + 100
            tryVerify(() => tooltip.visible || PreviewController.visible || Date.now() >= deadline, 2000)
            verify(!tooltip.visible, "preview invalidation reopened tooltip during drag")
            verify(!PreviewController.visible, "preview invalidation reopened popup during drag")
            mouseRelease(stage, c.x + delta, c.y, Qt.LeftButton)
            tryVerify(() => items(dock).every(it => !it.isDragSource), 2000)
            verify(!PreviewController.visible)
        }

        function test_group0ClickDoesNotOpenPreview() {
            DockSettings.groupedWindowClickAction = 0
            DockSettings.previewEnabled = false
            addGroupTask()
            let dock = makeDock(1)
            let c = hoverItem(items(dock)[0])
            mouseClick(stage, c.x, c.y, Qt.LeftButton)
            compare(PreviewController.visible, false)
        }

        function test_singleClickDoesNotOpenPreview() {
            DockSettings.singleWindowClickAction = 1
            DockSettings.groupedWindowClickAction = 1
            DockSettings.previewEnabled = false
            addTasks(["Single"])
            let dock = makeDock(1)
            let c = hoverItem(items(dock)[0])
            mouseClick(stage, c.x, c.y, Qt.LeftButton)
            compare(PreviewController.visible, false)
        }

        function test_membershipTransitionsUseCurrentGroupingAction() {
            DockSettings.singleWindowClickAction = 1
            DockSettings.groupedWindowClickAction = 1
            DockSettings.previewEnabled = false
            let row = DockModel.tasksModel.addTask({
                display: "Adaptive",
                AppId: "adaptive",
                IsWindow: true,
                IsGroupParent: false,
            })
            DockModel.tasksModel.addChildTask(row, {
                display: "Adaptive child 1",
                IsWindow: true,
                IsActive: true,
            })
            let dock = makeDock(1)
            let c = hoverItem(items(dock)[0])

            // One child: single-window setting, so no explicit preview.
            mouseClick(stage, c.x, c.y, Qt.LeftButton)
            compare(PreviewController.visible, false)

            // Two children: grouped setting, so the same click opens preview.
            DockModel.tasksModel.addChildTask(row, {
                display: "Adaptive child 2",
                IsWindow: true,
                IsActive: false,
            })
            DockModel.tasksModel.setTaskData(row, "IsGroupParent", true)
            mouseClick(stage, c.x, c.y, Qt.LeftButton)
            tryCompare(PreviewController, "visible", true, 2000)

            // Back to one child: leave the popup and return to single behavior.
            PreviewController.hidePreview()
            DockModel.tasksModel.removeChildTask(row, 1)
            DockModel.tasksModel.setTaskData(row, "IsGroupParent", false)
            mouseClick(stage, c.x, c.y, Qt.LeftButton)
            compare(PreviewController.visible, false)
        }

        function test_nonLeftKeyboardAndDragNeverOpenPreview_data() {
            return [
                { tag: "single0-group0", single: 0, grouped: 0 },
                { tag: "single0-group1", single: 0, grouped: 1 },
                { tag: "single0-group2", single: 0, grouped: 2 },
                { tag: "single1-group0", single: 1, grouped: 0 },
                { tag: "single1-group1", single: 1, grouped: 1 },
                { tag: "single1-group2", single: 1, grouped: 2 },
            ]
        }

        function test_nonLeftKeyboardAndDragNeverOpenPreview(data) {
            DockSettings.singleWindowClickAction = data.single
            DockSettings.groupedWindowClickAction = data.grouped
            DockSettings.previewEnabled = false
            DockSettings.previewHoverDelay = 1000
            addGroupTask()
            let dock = makeDock(1)
            let c = hoverItem(items(dock)[0])

            mouseClick(stage, c.x, c.y, Qt.MiddleButton)
            compare(PreviewController.visible, false)
            mouseClick(stage, c.x, c.y, Qt.RightButton)
            compare(PreviewController.visible, false)
            mouseWheel(stage, c.x, c.y, 0, 120)
            mouseWheel(stage, c.x, c.y, 0, -120)
            compare(PreviewController.visible, false)

            dock.startKeyboardNavigation()
            keyClick(Qt.Key_Return)
            compare(PreviewController.visible, false)
            dock.startKeyboardNavigation()
            keyClick(Qt.Key_Space)
            compare(PreviewController.visible, false)

            // Hold and move far enough to become an internal drag. The left
            // release must not be reinterpreted as an explicit preview click.
            mouseMove(stage, c.x + 1, c.y)
            tryCompare(dock, "hoveredIndex", 0, 2000)
            mousePress(stage, c.x + 1, c.y, Qt.LeftButton)
            let delta = 20
            tryVerify(() => {
                mouseMove(stage, c.x + delta, c.y, -1, Qt.LeftButton)
                delta = delta === 20 ? 21 : 20
                return items(dock)[0].isDragSource
            }, 2000, "drag source visual feedback never appeared")
            mouseRelease(stage, c.x + delta, c.y, Qt.LeftButton)
            tryCompare(items(dock)[0], "isDragSource", false, 2000)
            compare(PreviewController.visible, false)
            verify(!dockTooltip(dock).visible)
        }

        function test_dragExitAndReentryReleaseDoesNotOpenPreview() {
            DockSettings.previewEnabled = false
            DockSettings.previewHoverDelay = 1000
            DockSettings.groupedWindowClickAction = 1
            addGroupTask()
            let dock = makeDock(1)

            // Give the actual MouseArea a smaller surface so leaving it still
            // delivers events inside the visible test window.
            dock.anchors.fill = null
            dock.width = stage.width / 2
            dock.height = stage.height / 2
            dock.x = stage.width / 4
            dock.y = stage.height / 4
            let item = items(dock)[0]
            let settledCenter = null
            let settledSince = 0
            tryVerify(() => {
                let c = centerOf(item)
                let now = Date.now()
                let origin = dock.mapToItem(stage, 0, 0)
                let inside = c.x > origin.x && c.x < origin.x + dock.width
                    && c.y > origin.y && c.y < origin.y + dock.height
                if (settledCenter
                    && Math.abs(c.x - settledCenter.x) < 0.1
                    && Math.abs(c.y - settledCenter.y) < 0.1) {
                    settledSince = settledSince || now
                } else {
                    settledCenter = c
                    settledSince = now
                }
                return inside && now - settledSince >= 250
            }, 2000, "icon did not settle inside the test dock surface")

            let from = hoverItem(item)
            mousePress(stage, from.x, from.y, Qt.LeftButton)
            let delta = 20
            tryVerify(() => {
                mouseMove(stage, from.x + delta, from.y, -1, Qt.LeftButton)
                delta = delta === 20 ? 21 : 20
                return item.isDragSource
            }, 2000, "drag source visual feedback never appeared")

            let origin = dock.mapToItem(stage, 0, 0)
            let outside = dock.mapToItem(stage, -DockSettings.iconSize, dock.height / 2)
            verify(outside.x < origin.x)
            verify(outside.x >= 0 && outside.x < stage.width
                && outside.y >= 0 && outside.y < stage.height)
            mouseMove(stage, outside.x, outside.y, -1, Qt.LeftButton)
            tryCompare(item, "isDragSource", false, 2000)
            verify(!PreviewController.visible)
            verify(!dockTooltip(dock).visible)

            // The same held-button sequence returns to the icon and releases.
            // It is still a cancelled drag, not a fresh explicit-preview click.
            let reentry = centerOf(item)
            mouseMove(stage, reentry.x, reentry.y, -1, Qt.LeftButton)
            mouseRelease(stage, reentry.x, reentry.y, Qt.LeftButton)
            verify(!item.isDragSource)
            compare(PreviewController.visible, false)
            verify(!dockTooltip(dock).visible)

            // A new normal click must still open the group's explicit preview.
            let fresh = centerOf(item)
            mouseClick(stage, fresh.x, fresh.y, Qt.LeftButton)
            tryCompare(PreviewController, "visible", true, 2000)
            compare(PreviewController.parentIndex, 0)
            verify(!dockTooltip(dock).visible)
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

        function test_keyboardNavigationMovesFocusAndReturnEndsNavigation() {
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
            keyClick(Qt.Key_Up) // launcher: nothing to preview
            verify(!PreviewController.visible)
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
        function test_separatedDragIndicatorUsesRunningRestSlots_data() {
            return dockEdges()
        }

        function test_separatedDragIndicatorUsesRunningRestSlots(data) {
            DockView.edge = data.edge
            DockSettings.iconSize = 32
            DockSettings.iconSpacing = 7
            DockSettings.maxZoomFactor = 1
            DockSettings.separateLaunchers = true
            DockSettings.previewEnabled = false
            addSeparatedTasks()
            let dock = makeDock(4)
            let its = items(dock)
            for (let direction of [{ from: 2, to: 3 }, { from: 3, to: 2 }]) {
                // The last drag ended at the next source without updating
                // hover. Leave the dock first so re-entry delivers a genuine
                // motion event instead of repeating an identical cursor point.
                mouseMove(stage, stage.width / 2, stage.height / 2)
                tryCompare(dock, "hoveredIndex", -1)
                let source = its[direction.from]
                tryCompare(source, "currentScale", 1)
                tryCompare(source, "currentOffset", 0)
                let sourceCenter = centerOf(source)
                let targetCenter = centerOf(its[direction.to])
                let from = Qt.point(Math.round(sourceCenter.x), Math.round(sourceCenter.y))
                let to = Qt.point(Math.round(targetCenter.x), Math.round(targetCenter.y))
                mouseMove(stage, from.x, from.y)
                tryCompare(dock, "hoveredIndex", direction.from)
                mousePress(stage, from.x, from.y, Qt.LeftButton)
                // Model a real hold: the production gesture arms at 300 ms,
                // then movement beyond its 10 px threshold starts the drag.
                wait(350)
                mouseMove(stage, from.x + (DockView.isVertical ? 0 : 12),
                          from.y + (DockView.isVertical ? 12 : 0), -1, Qt.LeftButton)
                tryCompare(source, "isDragSource", true)
                mouseMove(stage, to.x, to.y, -1, Qt.LeftButton)
                let indicator = null
                tryVerify(() => {
                    indicator = T.findFirst(dock, o => o.visible && o.color !== undefined
                        && primaryExtent(o) === 2
                        && (DockView.isVertical ? o.width : o.height) === DockSettings.iconSize)
                    return indicator !== null
                }, 2000, "real running-zone drag did not draw an insertion indicator")
                let targetBounds = drawnBounds(its[direction.to], dock)
                let marker = drawnBounds(indicator, dock)
                let expected = direction.to > direction.from
                    ? primaryNear(targetBounds) + primaryExtent(targetBounds) + DockSettings.iconSpacing / 2
                    : primaryNear(targetBounds) - DockSettings.iconSpacing / 2
                fuzzyCompare(primaryNear(marker) + primaryExtent(marker) / 2, expected, 1e-6)
                dock.forceActiveFocus()
                keyClick(Qt.Key_Escape)
                tryCompare(source, "isDragSource", false)
                mouseRelease(stage, to.x, to.y, Qt.LeftButton)
                verify(!indicator.visible)
            }
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

        function test_escapeCancelsDragVisualFeedback() {
            DockSettings.previewEnabled = false
            addTasks(["A", "B", "C"])
            let dock = makeDock(3)
            let its = items(dock)
            let from = centerOf(its[0])
            let to = centerOf(its[2])
            mouseMove(stage, from.x, from.y)
            tryCompare(dock, "hoveredIndex", 0)
            mousePress(stage, from.x, from.y, Qt.LeftButton)
            let delta = 20
            tryVerify(() => {
                mouseMove(stage, from.x + delta, from.y, -1, Qt.LeftButton)
                delta = delta === 20 ? 21 : 20
                return its[0].isDragSource
            }, 2000, "drag source visual feedback never appeared")
            mouseMove(stage, to.x, to.y, -1, Qt.LeftButton)
            dock.forceActiveFocus()
            keyClick(Qt.Key_Escape)
            tryCompare(its[0], "isDragSource", false, 2000)
            compare(DockVisibility.dragActive, false)
            compare(DockVisibility.interacting, false)
            // Moving with the button still held must not restart visual drag.
            mouseMove(stage, centerOf(its[1]).x, to.y, -1, Qt.LeftButton)
            verify(!its[0].isDragSource)
            mouseRelease(stage, centerOf(its[1]).x, to.y, Qt.LeftButton)
            verify(!PreviewController.visible)
        }
    }
}
