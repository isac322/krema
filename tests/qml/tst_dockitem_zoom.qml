// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Hover zoom of the real DockItem.qml as a function of pointer position, fed
// by the production zoom layout (DockView.zoomLayout, see DockItemRow.qml).
import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0
import "../../src/qml" as Krema

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

        property int settledTurns: 0

        // Lets deferred (Qt.callLater) post-layout delegate setup run.
        function settleDeferredCallbacks() {
            let next = settledTurns + 1
            Qt.callLater(function() {
                Qt.callLater(function() { tc.settledTurns = next })
            })
            tryCompare(tc, "settledTurns", next)
        }

        // Creates `n` window tasks and waits until every delegate has finished its
        // deferred post-layout setup.
        function makeRow(n) {
            for (let i = 0; i < n; i++)
                DockModel.tasksModel.addTask({ display: "App" + i, IsWindow: true, ChildCount: 1 })
            let row = createTemporaryObject(rowComponent, stage)
            verify(row)
            tryCompare(row, "count", n)
            for (let i = 0; i < n; i++) {
                let item = row.itemAt(i)
                tryVerify(() => item._delegateGeometryReady, 2000, "delegate " + i + " never finished its setup")
            }
            settleDeferredCallbacks()
            return row
        }

        // Pointer at panel x, then wait until the Parabolic zoom-in has eased in.
        function hover(row, x) {
            row.mouseInside = true
            row.mouseX = x
            tryVerify(() => row.zoomAmount === 1.0)
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

        // Easing curves of ZoomAnimationProfile's easing indices (Qt's formulas).
        function ease(index, t) {
            switch (index) {
            case 0: return t
            case 1: return t * t * t
            case 2: return 1.0 - Math.pow(1.0 - t, 3)
            case 4: return -(Math.cos(Math.PI * t) - 1.0) / 2.0
            default: return t < 0.5 ? 4.0 * t * t * t : 1.0 - Math.pow(-2.0 * t + 2.0, 3) / 2.0
            }
        }

        function styles() {
            return [{ tag: "parabolic", style: 0 }, { tag: "in-place", style: 1 }]
        }

        function test_scaleEasesInAndSettlesBackOnExit_data() {
            return styles()
        }

        // With the default (Natural) preset both styles ease the global
        // zoomAmount in and out, and every item returns to exactly 1.0.
        function test_scaleEasesInAndSettlesBackOnExit(data) {
            DockSettings.zoomStyle = data.style
            let row = makeRow(3)
            let item = row.itemAt(1)
            compare(row.profile.preset, 0)
            let spy = createTemporaryObject(spyComponent, tc, { target: item, signalName: "currentScaleChanged" })
            row.mouseInside = true
            row.mouseX = item.itemCenterX
            tryCompare(item, "currentScale", DockSettings.maxZoomFactor)
            if (row.profile.effectiveZoomInDuration > 0)
                verify(spy.count >= 2, "scale jumped to the peak in " + spy.count + " step(s) instead of easing in")
            else
                compare(spy.count, 1, "Plasma Instant must snap to the peak")

            // Pointer leaves the panel: every item returns to exactly 1.0 and rest position.
            spy.clear()
            row.mouseInside = false
            row.mouseX = -1
            for (let i = 0; i < row.count; i++) {
                tryCompare(row.itemAt(i), "zoomScale", 1.0)
                tryCompare(row.itemAt(i), "currentScale", 1.0)
                tryCompare(row.itemAt(i), "currentOffset", 0.0)
            }
            compare(row.zoomAmount, 0.0)
            if (row.profile.effectiveZoomOutDuration > 0)
                verify(spy.count >= 2, "scale jumped back to 1.0 in " + spy.count + " step(s) instead of easing out")
            else
                compare(spy.count, 1, "Plasma Instant must snap back to rest")
        }

        function test_zeroDurationSnapsInAndOut_data() {
            return styles()
        }

        // The Instant preset has no transition in either direction.
        function test_zeroDurationSnapsInAndOut(data) {
            DockSettings.zoomStyle = data.style
            DockSettings.zoomAnimationPreset = 3
            let row = makeRow(3)
            let item = row.itemAt(1)
            verify(!row.profile.animated, "Instant must disable the zoom transition")
            let spy = createTemporaryObject(spyComponent, tc, { target: item, signalName: "currentScaleChanged" })
            row.mouseX = item.itemCenterX
            row.mouseInside = true
            compare(row.zoomAmount, 1.0)
            compare(item.currentScale, DockSettings.maxZoomFactor)
            compare(spy.count, 1, "zero duration must snap to the peak synchronously")
            for (let i = 0; i < row.count; i++) {
                compare(row.itemAt(i).currentScale, row.itemAt(i).zoomScale)
                compare(row.itemAt(i).transform[0].xScale, row.itemAt(i).currentScale)
            }

            spy.clear()
            row.mouseInside = false
            row.mouseX = -1
            compare(row.zoomAmount, 0.0)
            for (let i = 0; i < row.count; i++) {
                compare(row.itemAt(i).zoomScale, 1.0)
                compare(row.itemAt(i).currentScale, 1.0)
                compare(row.itemAt(i).currentOffset, 0.0)
            }
            compare(spy.count, 1, "zero duration must snap back synchronously")
        }

        function test_customInOutDurationsUseConfiguredTimelines_data() {
            return styles()
        }

        // Custom zoom-in and zoom-out each follow their own duration and easing.
        function test_customInOutDurationsUseConfiguredTimelines(data) {
            DockSettings.zoomStyle = data.style
            DockSettings.zoomAnimationPreset = 4
            DockSettings.zoomInDuration = 800
            DockSettings.zoomInEasing = 1
            DockSettings.zoomOutDuration = 500
            DockSettings.zoomOutEasing = 4
            let row = makeRow(3)
            let item = row.itemAt(1)
            compare(row.profile.zoomInDuration, 800)
            compare(row.profile.zoomOutDuration, 500)
            let probe = createTemporaryObject(durationProbeComponent, tc, { targetRow: row, targetItem: item })

            let inMs = row.profile.effectiveZoomInDuration
            row.mouseX = item.itemCenterX
            row.mouseInside = true
            if (inMs === 0)
                compare(item.currentScale, DockSettings.maxZoomFactor, "Plasma Instant must snap synchronously")
            probe.start(inMs)
            tryCompare(probe, "finished", true, inMs + 2000)
            if (inMs > 0)
                verifyTimelineSamples(probe, 1.0, DockSettings.maxZoomFactor, 1)
            tryCompare(item, "currentScale", DockSettings.maxZoomFactor)

            let outMs = row.profile.effectiveZoomOutDuration
            row.mouseInside = false
            row.mouseX = -1
            if (outMs === 0)
                compare(item.currentScale, 1.0, "Plasma Instant must return to rest synchronously")
            probe.start(outMs)
            tryCompare(probe, "finished", true, outMs + 2000)
            if (outMs > 0)
                verifyTimelineSamples(probe, DockSettings.maxZoomFactor, 1.0, 4)
            for (let i = 0; i < row.count; i++) {
                tryCompare(row.itemAt(i), "zoomScale", 1.0)
                tryCompare(row.itemAt(i), "currentScale", 1.0)
                tryCompare(row.itemAt(i), "currentOffset", 0.0)
            }
        }

        function verifyTimelineSamples(probe, from, to, easing) {
            verify(probe.samples.length >= 2, "configured duration must produce intermediate zoom frames")
            for (const sample of probe.samples) {
                fuzzyCompare(sample.scale, from + (to - from) * ease(easing, sample.progress), 1e-6,
                    "zoom scale at configured timeline progress " + sample.progress)
            }
        }

        function test_presetsResolveTimingAndEasing_data() {
            return [
                { tag: "natural", preset: 0, inMs: 180, inEasing: 3, outMs: 240, outEasing: 3 },
                { tag: "quick", preset: 1, inMs: 100, inEasing: 2, outMs: 100, outEasing: 2 },
                { tag: "relaxed", preset: 2, inMs: 300, inEasing: 4, outMs: 400, outEasing: 4 },
                { tag: "instant", preset: 3, inMs: 0, outMs: 0 },
                { tag: "custom", preset: 4, inMs: 520, inEasing: 1, outMs: 760, outEasing: 0 },
                { tag: "out-of-range", preset: 7, inMs: 180, inEasing: 3, outMs: 240, outEasing: 3 },
            ]
        }

        // ZoomAnimationProfile is the single source of truth for every preset's
        // timing and easing; Custom reads the DockSettings custom entries.
        function test_presetsResolveTimingAndEasing(data) {
            const easingTypes = [Easing.Linear, Easing.InCubic, Easing.OutCubic, Easing.InOutCubic, Easing.InOutSine]
            const scaled = ms => Math.round(ms * Kirigami.Units.shortDuration / 100.0)
            DockSettings.zoomInDuration = 520
            DockSettings.zoomInEasing = 1
            DockSettings.zoomOutDuration = 760
            DockSettings.zoomOutEasing = 0
            DockSettings.zoomAnimationPreset = data.preset
            let profile = createTemporaryObject(profileComponent, tc)
            verify(profile)
            compare(profile.customPreset, 4)
            compare(profile.preset, data.preset, "preset must follow DockSettings.zoomAnimationPreset")
            compare(profile.zoomInDuration, data.inMs)
            compare(profile.zoomOutDuration, data.outMs)
            if (data.inEasing !== undefined) {
                compare(profile.zoomInEasing, data.inEasing)
                compare(profile.zoomOutEasing, data.outEasing)
                compare(profile.zoomInEasingType, easingTypes[data.inEasing])
                compare(profile.zoomOutEasingType, easingTypes[data.outEasing])
            }
            compare(profile.effectiveZoomInDuration, scaled(data.inMs))
            compare(profile.effectiveZoomOutDuration, scaled(data.outMs))
            compare(profile.animated, scaled(data.inMs) > 0 || scaled(data.outMs) > 0)

            if (data.preset >= 0 && data.preset < profile.customPreset) {
                let entry = profile.presets[data.preset]
                compare(entry.inDuration, data.inMs)
                compare(entry.outDuration, data.outMs)
                if (data.inEasing !== undefined) {
                    compare(entry.inEasing, data.inEasing)
                    compare(entry.outEasing, data.outEasing)
                }
            }
            if (data.preset === profile.customPreset) {
                DockSettings.zoomInDuration = 330
                DockSettings.zoomOutEasing = 2
                compare(profile.zoomInDuration, 330, "Custom must follow DockSettings.zoomInDuration live")
                compare(profile.zoomOutEasingType, Easing.OutCubic)
            }

            for (let i = 0; i < easingTypes.length; i++)
                compare(profile.easingType(i), easingTypes[i], "easing index " + i)
            compare(profile.easingType(99), Easing.InOutCubic, "unknown easing index falls back to InOutCubic")

            // The preset input can be overridden (as the settings page does).
            profile.preset = 1
            compare(profile.zoomInDuration, 100)
            compare(profile.zoomInEasingType, Easing.OutCubic)
        }

        // Starts a zoom-in on `item` and samples zoomAmount against the zoom-in timeline.
        function sampleZoomIn(row, item) {
            let probe = createTemporaryObject(durationProbeComponent, tc, { targetRow: row, targetItem: item })
            let inMs = row.profile.effectiveZoomInDuration
            row.mouseX = item.itemCenterX
            row.mouseInside = true
            probe.start(inMs)
            tryCompare(probe, "finished", true, inMs + 2000)
            tryVerify(() => row.zoomAmount === 1.0)
            verify(probe.samples.length >= 1, "zoom-in produced no intermediate frames")
            return probe.samples
        }

        // Natural accelerates into the zoom (InOutCubic) where Quick starts at
        // full speed (OutCubic): at the same fraction of the zoom-in timeline,
        // Natural has progressed less.
        function test_naturalEasesInSlowerThanQuick() {
            DockSettings.zoomAnimationPreset = 1
            let row = makeRow(3)
            let item = row.itemAt(1)
            if (row.profile.effectiveZoomInDuration === 0)
                skip("Plasma Instant animation speed: no zoom-in timeline to compare")

            let quick = sampleZoomIn(row, item)
            for (const s of quick)
                fuzzyCompare(s.amount, ease(2, s.progress), 1e-6, "Quick zoomAmount at progress " + s.progress)

            row.mouseInside = false
            row.mouseX = -1
            tryVerify(() => row.zoomAmount === 0.0)
            DockSettings.zoomAnimationPreset = 0
            compare(row.profile.zoomInEasingType, Easing.InOutCubic)

            let natural = sampleZoomIn(row, item)
            for (const s of natural) {
                // Sample timestamps and the animation clock can differ by a
                // fraction of a millisecond; the curves differ by orders of
                // magnitude more (OutCubic is ~100x InOutCubic this early).
                fuzzyCompare(s.amount, ease(3, s.progress), 1e-4, "Natural zoomAmount at progress " + s.progress)
                verify(s.amount < ease(2, s.progress),
                       "Natural (" + s.amount + ") not below Quick (" + ease(2, s.progress) + ") at progress " + s.progress)
            }
            fuzzyCompare(item.currentScale, DockSettings.maxZoomFactor, 1e-9)
        }

        function test_zeroZoomInDurationSnapsInButAnimatesOut_data() {
            return styles()
        }

        // A 0 ms direction snaps synchronously while the other one still animates.
        function test_zeroZoomInDurationSnapsInButAnimatesOut(data) {
            DockSettings.zoomStyle = data.style
            DockSettings.zoomAnimationPreset = 4
            DockSettings.zoomInDuration = 0
            DockSettings.zoomOutDuration = 400
            DockSettings.zoomOutEasing = 2
            let row = makeRow(3)
            let item = row.itemAt(1)
            compare(row.profile.effectiveZoomInDuration, 0)
            let spy = createTemporaryObject(spyComponent, tc, { target: item, signalName: "currentScaleChanged" })
            row.mouseX = item.itemCenterX
            row.mouseInside = true
            compare(row.zoomAmount, 1.0)
            compare(item.currentScale, DockSettings.maxZoomFactor)
            compare(spy.count, 1, "zero zoom-in duration must snap to the peak synchronously")

            spy.clear()
            let outMs = row.profile.effectiveZoomOutDuration
            row.mouseInside = false
            row.mouseX = -1
            if (outMs > 0) {
                verify(row.zoomAmount > 0.0, "zoom-out snapped instead of animating")
                // Exact rest, not tryCompare: its 1e-5 tolerance passes on the
                // animation's last frame, while offsets (px) are still nonzero.
                tryVerify(() => row.zoomAmount === 0.0, outMs + 2000)
                verify(spy.count >= 2, "scale jumped back to 1.0 in " + spy.count + " step(s) instead of easing out")
            } else {
                compare(row.zoomAmount, 0.0, "Plasma Instant must return to rest synchronously")
                compare(spy.count, 1)
            }
            for (let i = 0; i < row.count; i++) {
                compare(row.itemAt(i).currentScale, 1.0)
                compare(row.itemAt(i).currentOffset, 0.0)
            }
        }

        // InPlace no longer animates per icon: once zoomed in, moving along the
        // dock moves the magnification with the pointer in the same turn.
        function test_inPlaceTracksPointerWithoutLag() {
            DockSettings.zoomStyle = 1
            let row = makeRow(5)
            hover(row, row.itemAt(1).itemCenterX)
            tryCompare(row.itemAt(1), "currentScale", DockSettings.maxZoomFactor)
            let target = row.itemAt(3)
            let spy = createTemporaryObject(spyComponent, tc, { target: target, signalName: "currentScaleChanged" })
            row.mouseX = target.itemCenterX
            compare(spy.count, 1, "the new icon's scale must update in a single step")
            compare(target.currentScale, target.zoomScale)
            fuzzyCompare(target.currentScale, DockSettings.maxZoomFactor, 1e-9)
            for (let i = 0; i < row.count; i++) {
                compare(row.itemAt(i).currentScale, row.itemAt(i).zoomScale, "item " + i + " lags behind the pointer")
                compare(row.itemAt(i).currentOffset, 0)
            }
            verify(row.itemAt(1).currentScale < DockSettings.maxZoomFactor, "previous icon kept its peak scale")
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
            tryVerify(() => row.zoomAmount === 1.0)
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

        // Samples on Qt's shared animation clock, not after a wall-clock sleep:
        // a linear reference animation started in the same turn as the zoom
        // transition reports the timeline fraction each frame.
        Component {
            id: durationProbeComponent
            Item {
                id: probe
                property var targetRow
                property var targetItem
                property int duration: 0
                property real progress: 0.0
                property var samples: []
                property bool finished: false
                function start(ms) {
                    finished = false
                    samples = []
                    duration = ms
                    reference.start()
                }
                onProgressChanged: {
                    if (progress <= 0.0 || progress >= 1.0) return
                    // Let both animations update before observing the real item.
                    Qt.callLater(function() {
                        if (probe.progress > 0.0 && probe.progress < 1.0)
                            probe.samples.push({
                                progress: probe.progress,
                                amount: probe.targetRow.zoomAmount,
                                scale: probe.targetItem.currentScale,
                            })
                    })
                }
                NumberAnimation {
                    id: reference
                    target: probe
                    property: "progress"
                    from: 0.0
                    to: 1.0
                    duration: probe.duration
                    onFinished: probe.finished = true
                }
            }
        }

        Component {
            id: profileComponent
            Krema.ZoomAnimationProfile {}
        }

        Component {
            id: spyComponent
            SignalSpy {}
        }
    }
}
