// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// The layer surface must contain the real DockItem's transformed icon while
// hover zoom and the launch bounce run together.  This fixture sizes its
// surface through the same DockView.surfaceHeight() contract as production,
// then samples mapped icon bounds instead of inspecting implementation state.
import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0
import "TestUtils.js" as T

Item {
    id: root
    width: 800
    height: 800

    Item {
        id: stage
        anchors.fill: parent
    }

    TestCase {
        id: tc
        name: "DockItemSurfaceGeometry"
        when: windowShown

        Component {
            id: rowComponent
            DockItemRow {}
        }

        function init() {
            KremaMocks.resetAll()
            stage.anchors.fill = root
        }

        function makeRow() {
            DockModel.tasksModel.addTask({ display: "App", IsWindow: true, ChildCount: 1 })
            let row = createTemporaryObject(rowComponent, stage)
            verify(row)
            tryCompare(row, "count", 1)
            let item = row.itemAt(0)
            tryVerify(() => item._delegateGeometryReady, 2000)
            return row
        }

        function surfaceBounds(edge, factor) {
            // These are DockView's production values: 8 px panel padding,
            // 36 px tooltip reserve, and the default 8 px floating reserve.
            let surface = DockView.surfaceHeight(48, 8, factor, 36, 8)
            let overflow = surface - (48 + 8 * 2) - 8
            stage.anchors.fill = null
            stage.x = 0
            stage.y = 0
            stage.width = edge === 1 ? 800 : surface
            stage.height = edge === 1 ? surface : 800
            return { surface: surface, overflow: overflow }
        }

        function mappedBounds(item) {
            let icon = T.iconImage(item)
            verify(icon, "DockItem icon image not found")
            // Launch bounce is a translation, but map all corners so this
            // remains a visible-surface contract if another child transform
            // is added later.
            let points = [
                icon.mapToItem(stage, 0, 0),
                icon.mapToItem(stage, icon.width, 0),
                icon.mapToItem(stage, 0, icon.height),
                icon.mapToItem(stage, icon.width, icon.height),
            ]
            let left = points[0].x
            let right = points[0].x
            let top = points[0].y
            let bottom = points[0].y
            for (let p of points) {
                left = Math.min(left, p.x)
                right = Math.max(right, p.x)
                top = Math.min(top, p.y)
                bottom = Math.max(bottom, p.y)
            }
            return { left: left, right: right, top: top, bottom: bottom }
        }

        function test_launchBounceStaysInsideSurface_data() {
            let cases = [
                { tag: "bottom-1.5-control", edge: 1, factor: 1.5 },
                { tag: "bottom-1.6", edge: 1, factor: 1.6 },
                { tag: "bottom-2.0", edge: 1, factor: 2.0 },
                { tag: "right-1.5-control", edge: 3, factor: 1.5 },
                { tag: "right-1.6", edge: 3, factor: 1.6 },
                { tag: "right-2.0", edge: 3, factor: 2.0 },
            ]
            return cases.concat(cases.map(data => ({
                tag: data.tag + "-launch-to-attention",
                edge: data.edge,
                factor: data.factor,
                finishIntoAttention: true,
            })))
        }

        function test_launchBounceStaysInsideSurface(data) {
            DockView.edge = data.edge
            DockSettings.iconSize = 48
            DockSettings.maxZoomFactor = data.factor
            DockSettings.zoomAnimationPreset = 3 // sample the peak zoom immediately
            let geometry = surfaceBounds(data.edge, data.factor)
            let row = makeRow()
            // DockItemRow is a reusable horizontal/vertical host.  Shrink it
            // to one production cell so the edge-anchored Scale has the same
            // centered rest position as a one-icon dock.
            row.width = data.edge === 1 ? 48 : 56
            row.height = data.edge === 1 ? 56 : 48
            if (data.edge === 1) {
                row.y = geometry.overflow + 8
                row.x = (stage.width - row.width) / 2
            } else {
                row.x = geometry.overflow + 8
                row.y = (stage.height - row.height) / 2
            }

            let item = row.itemAt(0)
            row.mouseInside = true
            row.mouseX = item.itemCenterX
            tryVerify(() => item.currentScale > 1.0, 1000,
                      data.tag + ": zoom did not activate, scale=" + item.currentScale)

            item.manualLaunching = true
            if (data.finishIntoAttention) {
                // Attention arrives during launch and must wait for the
                // final launch cycle before using its own bounce reserve.
                DockSettings.attentionAnimation = 1
                item._animationActive = true
                wait(Kirigami.Units.longDuration)
                item.manualLaunching = false
            }
            for (let sample = 0; sample < 60; sample++) {
                wait(10)
                let bounds = mappedBounds(item)
                verify(bounds.left >= -0.001,
                       data.tag + ": icon left escaped surface at sample " + sample + ": " + bounds.left)
                verify(bounds.top >= -0.001,
                       data.tag + ": icon top escaped surface at sample " + sample + ": " + bounds.top)
                verify(bounds.right <= stage.width + 0.001,
                       data.tag + ": icon right escaped surface at sample " + sample + ": " + bounds.right)
                verify(bounds.bottom <= stage.height + 0.001,
                       data.tag + ": icon bottom escaped surface at sample " + sample + ": " + bounds.bottom)
            }
            item.manualLaunching = false
            if (data.finishIntoAttention)
                tryVerify(() => item._showAttentionAnim, 1000,
                          data.tag + ": attention did not resume after launch bounce")
        }
    }
}
