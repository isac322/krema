// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Parabolic zoom of the real DockItem.qml as a function of pointer position.
import QtQuick
import QtTest
import com.bhyoo.krema 1.0

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
        name: "DockItemZoom"
        when: windowShown

        Component {
            id: rowComponent
            DockItemRow {}
        }

        function init() {
            KremaMocks.resetAll()
        }

        // Creates `n` window tasks and waits until every delegate has finished its
        // deferred post-layout setup (after which currentScale animates).
        function makeRow(n) {
            for (let i = 0; i < n; i++)
                DockModel.tasksModel.addTask({ display: "App" + i, IsWindow: true, ChildCount: 1 })
            let row = createTemporaryObject(rowComponent, stage)
            verify(row)
            tryCompare(row, "count", n)
            for (let i = 0; i < n; i++) {
                let item = row.itemAt(i)
                tryVerify(() => item._zoomAnimReady, 2000, "delegate " + i + " never became animation-ready")
            }
            return row
        }

        function hover(row, x) {
            row.mouseInside = true
            row.mouseX = x
        }

        function zooms(row) {
            let z = []
            for (let i = 0; i < row.count; i++) z.push(row.itemAt(i).zoomFactor)
            return z
        }

        function test_noZoomWhilePointerOutside() {
            let row = makeRow(5)
            for (let i = 0; i < 5; i++) {
                compare(row.itemAt(i).zoomFactor, 1.0)
                compare(row.itemAt(i).currentScale, 1.0)
            }
            // A pointer position alone does not zoom while the panel reports "not inside".
            row.mouseX = row.itemAt(2).itemCenterX
            compare(row.itemAt(2).zoomFactor, 1.0)
            // ...nor does "inside" with the -1 sentinel position.
            row.mouseX = -1
            row.mouseInside = true
            compare(row.itemAt(2).zoomFactor, 1.0)
        }

        function test_peakAtHoveredItemEqualsMaxZoom() {
            let row = makeRow(5)
            hover(row, row.itemAt(2).itemCenterX)
            fuzzyCompare(row.itemAt(2).zoomFactor, DockSettings.maxZoomFactor, 1e-9)
            let z = zooms(row)
            for (let i = 0; i < z.length; i++) {
                if (i !== 2) verify(z[i] < z[2], "item " + i + " (" + z[i] + ") not below peak " + z[2])
            }
        }

        function test_symmetricMonotonicFalloff() {
            let row = makeRow(5)
            hover(row, row.itemAt(2).itemCenterX)
            let z = zooms(row)
            fuzzyCompare(z[1], z[3], 1e-9)
            fuzzyCompare(z[0], z[4], 1e-9)
            verify(z[2] > z[1], "peak > first neighbours")
            verify(z[1] > z[0], "first neighbours > second neighbours")
            verify(z[0] > 1.0, "second neighbours still attenuated-zoomed, got " + z[0])
        }

        function test_pointerBetweenItemsZoomsBothEqually() {
            let row = makeRow(4)
            let mid = (row.itemAt(1).itemCenterX + row.itemAt(2).itemCenterX) / 2
            hover(row, mid)
            let z = zooms(row)
            fuzzyCompare(z[1], z[2], 1e-9)
            verify(z[1] < DockSettings.maxZoomFactor, "no item reaches the peak between icons")
            verify(z[1] > z[0])
        }

        function test_zoomFollowsPointerContinuously() {
            let row = makeRow(5)
            let item = row.itemAt(2)
            verify(item.itemCenterX - 120 >= 0, "sweep must stay at non-negative panel x (-1 means outside)")
            let prev = 0
            // Approaching the item's center from the left strictly increases its zoom.
            for (let dx = 120; dx >= 0; dx -= 20) {
                hover(row, item.itemCenterX - dx)
                verify(item.zoomFactor > prev, "zoom at dx=" + dx + " not increasing")
                prev = item.zoomFactor
            }
        }

        function test_sigmaScalesWithIconSize_data() {
            return [{ tag: "32px", iconSize: 32 }, { tag: "48px", iconSize: 48 }, { tag: "96px", iconSize: 96 }]
        }

        // The falloff width is proportional to icon size: at exactly one sigma
        // (iconSize * zoomSigmaFactor) from the pointer, the zoom boost is 1/e.
        function test_sigmaScalesWithIconSize(data) {
            DockSettings.iconSize = data.iconSize
            let row = makeRow(2)
            let item = row.itemAt(0)
            compare(item.zoomSigma, data.iconSize * item.zoomSigmaFactor)
            hover(row, item.itemCenterX + item.zoomSigma)
            fuzzyCompare(item.zoomFactor - 1.0, (DockSettings.maxZoomFactor - 1.0) / Math.E, 1e-9)
        }

        function test_noEffectBeyondRange() {
            let row = makeRow(8)
            hover(row, row.itemAt(0).itemCenterX)
            let far = row.itemAt(7)
            let distance = far.itemCenterX - row.itemAt(0).itemCenterX
            verify(distance > 5 * far.zoomSigma, "fixture must place item 7 beyond 5 sigma")
            fuzzyCompare(far.zoomFactor, 1.0, 1e-6)
            tryCompare(far, "currentScale", far.zoomFactor)
            verify(Math.abs(far.currentScale - 1.0) < 1e-6)
        }

        function test_maxZoomFollowsSetting_data() {
            return [
                { tag: "off", factor: 1.0 },
                { tag: "1.3", factor: 1.3 },
                { tag: "default", factor: 1.6 },
                { tag: "max", factor: 2.0 },
            ]
        }

        function test_maxZoomFollowsSetting(data) {
            DockSettings.maxZoomFactor = data.factor
            let row = makeRow(3)
            hover(row, row.itemAt(1).itemCenterX)
            fuzzyCompare(row.itemAt(1).zoomFactor, data.factor, 1e-9)
            tryCompare(row.itemAt(1), "currentScale", row.itemAt(1).zoomFactor)
            let z = zooms(row)
            for (let v of z) verify(v >= 1.0 && v <= data.factor + 1e-9, "zoom " + v + " outside [1, " + data.factor + "]")
            if (data.factor === 1.0) {
                for (let v of z) compare(v, 1.0)
            }
        }

        function test_changingMaxZoomWhileHoveredUpdatesLive() {
            let row = makeRow(3)
            hover(row, row.itemAt(1).itemCenterX)
            tryCompare(row.itemAt(1), "currentScale", 1.6)
            DockSettings.maxZoomFactor = 2.0
            tryCompare(row.itemAt(1), "currentScale", 2.0)
        }

        function test_scaleAnimatesUpAndSettlesBackOnExit() {
            let row = makeRow(3)
            let item = row.itemAt(1)
            let spy = createTemporaryObject(spyComponent, tc, { target: item, signalName: "currentScaleChanged" })
            hover(row, item.itemCenterX)
            tryCompare(item, "currentScale", DockSettings.maxZoomFactor)
            verify(spy.count >= 1)

            // Pointer leaves the panel: every item returns to exactly 1.0.
            row.mouseInside = false
            row.mouseX = -1
            for (let i = 0; i < row.count; i++) {
                compare(row.itemAt(i).zoomFactor, 1.0)
                tryCompare(row.itemAt(i), "currentScale", 1.0)
            }
        }

        function test_appliedTransformMatchesCurrentScale() {
            let row = makeRow(3)
            let item = row.itemAt(1)
            hover(row, item.itemCenterX)
            tryCompare(item, "currentScale", DockSettings.maxZoomFactor)
            let scale = item.transform[0]
            compare(scale.xScale, item.currentScale)
            compare(scale.yScale, item.currentScale)
        }

        function test_growsAwayFromScreenEdge_data() {
            return [
                { tag: "top", edge: 0, ox: "center", oy: "start" },
                { tag: "bottom", edge: 1, ox: "center", oy: "end" },
                { tag: "left", edge: 2, ox: "start", oy: "center" },
                { tag: "right", edge: 3, ox: "end", oy: "center" },
            ]
        }

        function test_growsAwayFromScreenEdge(data) {
            DockView.edge = data.edge
            let row = makeRow(1)
            let item = row.itemAt(0)
            let origin = item.transform[0].origin
            let expect = (k, size) => k === "start" ? 0 : (k === "end" ? size : size / 2)
            compare(origin.x, expect(data.ox, item.width))
            compare(origin.y, expect(data.oy, item.height))
        }

        Component {
            id: spyComponent
            SignalSpy {}
        }
    }
}
