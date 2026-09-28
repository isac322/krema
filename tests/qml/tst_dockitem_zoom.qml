// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Hover zoom of the real DockItem.qml as a function of pointer position, fed
// by the production zoom layout (DockView.zoomLayout, see DockItemRow.qml).
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

        // Gaussian spread as a multiple of the icon size (krema::kDefaultZoomSigmaFactor).
        readonly property real sigmaFactor: 1.2

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

        // Pointer at panel x, then wait until the Parabolic zoom-in has eased in.
        function hover(row, x) {
            row.mouseInside = true
            row.mouseX = x
            tryCompare(row, "zoomAmount", 1.0)
        }

        function zooms(row) {
            let z = []
            for (let i = 0; i < row.count; i++) z.push(row.itemAt(i).zoomScale)
            return z
        }

        // Painted extent of an item along the dock axis, in stage coordinates
        // (through DockItem's real Scale + Translate transforms).
        function painted(item) {
            return { left: item.mapToItem(stage, 0, 0).x, right: item.mapToItem(stage, item.width, 0).x }
        }

        function test_noZoomWhilePointerOutside() {
            let row = makeRow(5)
            for (let i = 0; i < 5; i++) {
                compare(row.itemAt(i).zoomScale, 1.0)
                compare(row.itemAt(i).currentScale, 1.0)
                compare(row.itemAt(i).currentOffset, 0.0)
            }
            // A pointer position alone does not zoom while the panel reports "not inside".
            row.mouseX = row.itemAt(2).itemCenterX
            compare(row.zoomAmount, 0.0)
            compare(row.itemAt(2).zoomScale, 1.0)
            // ...nor does "inside" with the -1 sentinel position.
            row.mouseX = -1
            row.mouseInside = true
            compare(row.zoomAmount, 0.0)
            compare(row.itemAt(2).zoomScale, 1.0)
        }

        function test_peakAtHoveredItemEqualsMaxZoom() {
            let row = makeRow(5)
            hover(row, row.itemAt(2).itemCenterX)
            fuzzyCompare(row.itemAt(2).zoomScale, DockSettings.maxZoomFactor, 1e-9)
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
            // Approaching the item's rest centre from the left strictly increases its zoom.
            for (let dx = 120; dx >= 0; dx -= 20) {
                hover(row, item.itemCenterX - dx)
                verify(item.zoomScale > prev, "zoom at dx=" + dx + " not increasing")
                prev = item.zoomScale
            }
        }

        function test_sigmaScalesWithIconSize_data() {
            return [{ tag: "32px", iconSize: 32 }, { tag: "48px", iconSize: 48 }, { tag: "96px", iconSize: 96 }]
        }

        // The falloff width is proportional to icon size: at exactly one sigma
        // (iconSize * sigmaFactor) from the pointer, the zoom boost is 1/e.
        function test_sigmaScalesWithIconSize(data) {
            DockSettings.iconSize = data.iconSize
            let row = makeRow(2)
            let item = row.itemAt(0)
            hover(row, item.itemCenterX + data.iconSize * sigmaFactor)
            fuzzyCompare(item.zoomScale - 1.0, (DockSettings.maxZoomFactor - 1.0) / Math.E, 1e-9)
        }

        function test_noEffectBeyondRange() {
            let row = makeRow(8)
            hover(row, row.itemAt(0).itemCenterX)
            let far = row.itemAt(7)
            let distance = far.itemCenterX - row.itemAt(0).itemCenterX
            verify(distance > 5 * DockSettings.iconSize * sigmaFactor, "fixture must place item 7 beyond 5 sigma")
            fuzzyCompare(far.zoomScale, 1.0, 1e-6)
            tryCompare(far, "currentScale", far.zoomScale)
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
            fuzzyCompare(row.itemAt(1).zoomScale, data.factor, 1e-9)
            tryCompare(row.itemAt(1), "currentScale", row.itemAt(1).zoomScale)
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

        function test_scaleEasesInAndSettlesBackOnExit_data() {
            return [{ tag: "parabolic", style: 0 }, { tag: "in-place", style: 1 }]
        }

        // Zoom-in and zoom-out are animated (Parabolic eases the whole layout,
        // InPlace each icon's scale), and every item returns to exactly 1.0.
        function test_scaleEasesInAndSettlesBackOnExit(data) {
            DockSettings.zoomStyle = data.style
            let row = makeRow(3)
            let item = row.itemAt(1)
            let spy = createTemporaryObject(spyComponent, tc, { target: item, signalName: "currentScaleChanged" })
            row.mouseInside = true
            row.mouseX = item.itemCenterX
            tryCompare(item, "currentScale", DockSettings.maxZoomFactor)
            verify(spy.count >= 2, "scale jumped to the peak in " + spy.count + " step(s) instead of easing in")

            // Pointer leaves the panel: every item returns to exactly 1.0 and rest position.
            spy.clear()
            row.mouseInside = false
            row.mouseX = -1
            for (let i = 0; i < row.count; i++) {
                tryCompare(row.itemAt(i), "zoomScale", 1.0)
                tryCompare(row.itemAt(i), "currentScale", 1.0)
                tryCompare(row.itemAt(i), "currentOffset", 0.0)
            }
            verify(spy.count >= 2, "scale jumped back to 1.0 in " + spy.count + " step(s) instead of easing out")
        }

        function test_appliedTransformMatchesCurrentScaleAndOffset() {
            let row = makeRow(3)
            let item = row.itemAt(2)
            hover(row, row.itemAt(1).itemCenterX)
            tryCompare(item, "currentScale", item.zoomScale)
            let scale = item.transform[0]
            compare(scale.xScale, item.currentScale)
            compare(scale.yScale, item.currentScale)
            verify(item.currentOffset > 0, "right neighbour not pushed aside: offset " + item.currentOffset)
            let shift = item.transform[1]
            compare(shift.x, item.currentOffset)
            compare(shift.y, 0)
        }

        // Parabolic (the default): magnified icons push their neighbours aside
        // along the dock axis; the gaps between painted icons stay equal to the
        // rest spacing, so nothing overlaps, and the hovered icon stays under
        // the pointer.
        function test_parabolicPushesNeighboursAsideWithConstantGaps() {
            let row = makeRow(5)
            let pointer = row.itemAt(2).itemCenterX
            hover(row, pointer)
            // Painted geometry goes through single-precision scene transforms:
            // compare to a thousandth of a pixel.
            let p = []
            for (let i = 0; i < 5; i++) p.push(painted(row.itemAt(i)))
            for (let i = 0; i < 4; i++) {
                let gap = p[i + 1].left - p[i].right
                verify(Math.abs(gap - DockSettings.iconSpacing) < 1e-3,
                       "gap " + i + "-" + (i + 1) + " is " + gap + ", not the rest spacing " + DockSettings.iconSpacing)
            }
            fuzzyCompare((p[2].left + p[2].right) / 2, pointer, 1e-3)
            for (let d = 1; d <= 2; d++) {
                let left = row.itemAt(2 - d).currentOffset
                let right = row.itemAt(2 + d).currentOffset
                verify(left < 0 && right > 0, "neighbours at distance " + d + " not pushed outward: " + left + ", " + right)
                fuzzyCompare(-left, right, 1e-6)
            }
            verify(Math.abs(row.itemAt(0).currentOffset) > Math.abs(row.itemAt(1).currentOffset),
                   "outer icons must move further than inner ones")
        }

        // In place: icons scale around their rest centres, nothing moves, and
        // magnified neighbours overlap.
        function test_inPlaceScalesWithoutMoving() {
            DockSettings.zoomStyle = 1
            let row = makeRow(5)
            row.mouseInside = true
            row.mouseX = row.itemAt(2).itemCenterX
            for (let i = 0; i < 5; i++) tryCompare(row.itemAt(i), "currentScale", row.itemAt(i).zoomScale)
            fuzzyCompare(row.itemAt(2).currentScale, DockSettings.maxZoomFactor, 1e-9)
            for (let i = 0; i < 5; i++) {
                let item = row.itemAt(i)
                compare(item.currentOffset, 0)
                let p = painted(item)
                fuzzyCompare((p.left + p.right) / 2, item.itemCenterX, 1e-3)
            }
            let hovered = painted(row.itemAt(2))
            verify(painted(row.itemAt(1)).right > hovered.left, "left neighbour does not overlap the magnified icon")
            verify(painted(row.itemAt(3)).left < hovered.right, "right neighbour does not overlap the magnified icon")
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
