// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// DockItem layout and delegate-geometry publication at the QML boundary.
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
        name: "DockItemGeometry"
        when: windowShown

        Component {
            id: rowComponent
            DockItemRow {}
        }

        property int settledTurns: 0

        function settleGeometryCallbacks() {
            let next = settledTurns + 1
            Qt.callLater(function() {
                Qt.callLater(function() { tc.settledTurns = next })
            })
            tryCompare(tc, "settledTurns", next)
        }

        function latestPublication(index) {
            let publications = DockModel.delegateGeometryRequests
            for (let i = publications.length - 1; i >= 0; i--) {
                if (publications[i].index === index)
                    return publications[i]
            }
            return null
        }

        function comparePublication(publication, item) {
            verify(publication !== null, "delegate geometry was not published")
            compare(publication.index, item.index)
            verify(publication.isWindow)
        }

        function init() {
            KremaMocks.resetAll()
        }

        function makeRow(tasks) {
            for (let t of tasks) DockModel.tasksModel.addTask(t)
            let row = createTemporaryObject(rowComponent, stage)
            verify(row)
            tryCompare(row, "count", tasks.length)
            return row
        }

        // Space reserved toward the screen edge for the indicator dots.
        function indicatorSpace() {
            return 4 + Kirigami.Units.smallSpacing
        }

        function test_iconSizeSetsItemSize_data() {
            return [
                { tag: "24-bottom", size: 24, edge: 1 },
                { tag: "48-bottom", size: 48, edge: 1 },
                { tag: "96-top", size: 96, edge: 0 },
                { tag: "48-left", size: 48, edge: 2 },
                { tag: "64-right", size: 64, edge: 3 },
            ]
        }

        function test_iconSizeSetsItemSize(data) {
            DockSettings.iconSize = data.size
            DockView.edge = data.edge
            let item = makeRow([{ display: "Dolphin", IsWindow: true }]).itemAt(0)
            if (DockView.isVertical) {
                compare(item.width, data.size + indicatorSpace())
                compare(item.height, data.size)
            } else {
                compare(item.width, data.size)
                compare(item.height, data.size + indicatorSpace())
            }
            let icon = T.iconImage(item)
            verify(icon, "icon Image not found")
            compare(icon.width, data.size)
            compare(icon.height, data.size)
        }

        function test_iconSizeChangeResizesLiveItems() {
            let item = makeRow([{ display: "Dolphin", IsWindow: true }]).itemAt(0)
            compare(item.width, 48)
            DockSettings.iconSize = 72
            compare(item.width, 72)
            compare(item.height, 72 + indicatorSpace())
            compare(T.iconImage(item).width, 72)
        }

        // The icon is rasterised at the maximum zoomed size so zoom never upscales a bitmap.
        function test_iconSourceSizeCoversMaxZoom_data() {
            return [
                { tag: "48x1.6", size: 48, zoom: 1.6 },
                { tag: "40x1.3", size: 40, zoom: 1.3 },
                { tag: "96x2.0", size: 96, zoom: 2.0 },
            ]
        }

        function test_iconSourceSizeCoversMaxZoom(data) {
            DockSettings.iconSize = data.size
            DockSettings.maxZoomFactor = data.zoom
            let icon = T.iconImage(makeRow([{ display: "Kate", IsWindow: true }]).itemAt(0))
            compare(icon.sourceSize.width, Math.ceil(data.size * data.zoom))
            compare(icon.sourceSize.height, Math.ceil(data.size * data.zoom))
        }

        function test_iconSourceUsesIconProvider() {
            let row = makeRow([
                { display: "Dolphin", IsWindow: true, IconName: "system-file-manager" },
                { display: "NoIcon", IsWindow: true },
            ])
            compare(T.iconImage(row.itemAt(0)).source.toString(), "image://icon/system-file-manager?v=0")
            compare(T.iconImage(row.itemAt(1)).source.toString(), "")
            DockView.iconCacheVersion = 3
            compare(T.iconImage(row.itemAt(0)).source.toString(), "image://icon/system-file-manager?v=3")
        }

        // Without a loadable icon the item shows the app's upper-cased initial.
        function test_placeholderShowsInitial() {
            let row = makeRow([{ display: "dolphin", IsWindow: true }, { display: "", IsWindow: true }])
            let label0 = T.placeholderLabel(row.itemAt(0))
            verify(label0, "placeholder label not found")
            verify(label0.parent.visible, "placeholder hidden although no icon is loaded")
            compare(label0.text, "D")
            let label1 = T.placeholderLabel(row.itemAt(1))
            compare(label1.text, "?")
        }

        function test_iconOpacityReflectsWindowState_data() {
            return [
                { tag: "active", roles: { IsActive: true }, opacity: 1.0 },
                { tag: "minimized", roles: { IsMinimized: true }, opacity: 0.5 },
                { tag: "background", roles: {}, opacity: 0.8 },
            ]
        }

        function test_iconOpacityReflectsWindowState(data) {
            let roles = Object.assign({ display: "App", IsWindow: true }, data.roles)
            let icon = T.iconImage(makeRow([roles]).itemAt(0))
            tryCompare(icon, "opacity", data.opacity)
        }

        function test_dragSourceIsDimmed() {
            let item = makeRow([{ display: "App", IsWindow: true, IsActive: true }]).itemAt(0)
            let icon = T.iconImage(item)
            tryCompare(icon, "opacity", 1.0)
            item.isDragSource = true
            tryCompare(icon, "opacity", 0.3)
        }

        function test_otherDesktopDimming_data() {
            return [
                { tag: "dim-mode-other-desktop", mode: 1, onCurrent: false, opacity: 0.4 },
                { tag: "dim-mode-current-desktop", mode: 1, onCurrent: true, opacity: 1.0 },
                { tag: "show-all-other-desktop", mode: 0, onCurrent: false, opacity: 1.0 },
            ]
        }

        function test_otherDesktopDimming(data) {
            DockModel.virtualDesktopMode = data.mode
            let item = makeRow([{ display: "App", IsWindow: true, IsOnCurrentDesktop: data.onCurrent }]).itemAt(0)
            tryCompare(item, "opacity", data.opacity)
        }

        function test_otherDesktopOpacitySetting() {
            DockModel.virtualDesktopMode = 1
            DockSettings.otherDesktopOpacity = 0.25
            let item = makeRow([{ display: "App", IsWindow: true, IsOnCurrentDesktop: false }]).itemAt(0)
            tryCompare(item, "opacity", 0.25)
        }

        function test_geometryPublicationWaitsForDelegateLayout() {
            DockSettings.iconSpacing = 12
            let row = makeRow([
                { display: "Dolphin", IsWindow: true },
                { display: "Kate", IsWindow: true },
            ])
            // Creation is synchronous; publication must wait for Row positioning.
            compare(DockModel.delegateGeometryRequests.length, 0)
            tryVerify(() => latestPublication(0) !== null && latestPublication(1) !== null)
            settleGeometryCallbacks()
            compare(row.itemAt(1).x, DockSettings.iconSize + DockSettings.iconSpacing)
            for (let publication of DockModel.delegateGeometryRequests)
                comparePublication(publication, row.itemAt(publication.index))
        }

        function test_geometryPublicationWaitsForNonzeroLayoutSlot() {
            DockSettings.iconSize = 0
            let item = makeRow([{ display: "Kate", IsWindow: true }]).itemAt(0)
            compare(item.width, 0)
            tryVerify(() => item._zoomAnimReady)
            settleGeometryCallbacks()
            compare(DockModel.delegateGeometryRequests.length, 0,
                    "a window task must not publish an empty layout slot")

            DockSettings.iconSize = 48
            tryVerify(() => latestPublication(0) !== null)
            comparePublication(latestPublication(0), item)
        }


        function test_geometryPublicationStartsWhenTaskBecomesWindow_data() {
            return [
                { tag: "launcher-to-window", role: "IsLauncher" },
                { tag: "startup-to-window", role: "IsStartup" },
            ]
        }

        function test_geometryPublicationStartsWhenTaskBecomesWindow(data) {
            let task = { display: "Kate", IsWindow: false }
            task[data.role] = true
            let item = makeRow([task]).itemAt(0)
            tryVerify(() => item._zoomAnimReady)
            settleGeometryCallbacks()
            compare(DockModel.delegateGeometryRequests.length, 0,
                    "launcher/startup delegates must not request window geometry")

            DockModel.tasksModel.setTaskData(0, data.role, false)
            DockModel.tasksModel.setTaskData(0, "IsWindow", true)
            compare(DockModel.delegateGeometryRequests.length, 0,
                    "window transition must defer until layout has settled")
            tryVerify(() => latestPublication(0) !== null)
            comparePublication(latestPublication(0), item)

            settleGeometryCallbacks()
            DockModel.resetDelegateGeometryRequests()
            DockModel.tasksModel.setTaskData(0, "IsWindow", false)
            DockModel.tasksModel.setTaskData(0, data.role, true)
            DockModel.tasksModel.setTaskData(0, "ChildCount", 2)
            settleGeometryCallbacks()
            compare(DockModel.delegateGeometryRequests.length, 0,
                    "non-window role changes must not publish geometry")
        }

        function test_geometryPublicationRefreshesGroupParentForNewChildren() {
            let item = makeRow([{
                display: "Kate", IsWindow: true, IsGroupParent: true, ChildCount: 2,
            }]).itemAt(0)
            tryVerify(() => latestPublication(0) !== null)
            settleGeometryCallbacks()
            DockModel.resetDelegateGeometryRequests()

            DockModel.tasksModel.setTaskData(0, "ChildCount", 3)
            tryVerify(() => latestPublication(0) !== null)
            let publication = latestPublication(0)
            comparePublication(publication, item)
            verify(publication.isGroupParent, "publish the group parent, not a synthetic child")
            compare(publication.childCount, 3)
        }

        function test_hoverZoomDoesNotRepublishDelegateGeometry_data() {
            return [
                { tag: "parabolic", style: 0 },
                { tag: "in-place", style: 1 },
            ]
        }

        function test_hoverZoomDoesNotRepublishDelegateGeometry(data) {
            DockSettings.zoomStyle = data.style
            let row = makeRow([
                { display: "Dolphin", IsWindow: true },
                { display: "Kate", IsWindow: true },
                { display: "Konsole", IsWindow: true },
            ])
            tryVerify(() => latestPublication(0) !== null
                      && latestPublication(1) !== null && latestPublication(2) !== null)
            settleGeometryCallbacks()
            let restingPublications = DockModel.delegateGeometryRequests
            DockModel.resetDelegateGeometryRequests()

            row.mouseX = row.itemAt(1).itemCenterX
            row.mouseInside = true
            tryCompare(row.itemAt(1), "currentScale", DockSettings.maxZoomFactor)
            if (data.style === 0)
                verify(row.itemAt(2).currentOffset > 0, "Parabolic zoom must move the visual icon")
            settleGeometryCallbacks()
            compare(DockModel.delegateGeometryRequests.length, 0,
                    "hover animation must not send per-frame geometry requests")
            for (let publication of restingPublications)
                comparePublication(publication, row.itemAt(publication.index))

            row.mouseX = row.itemAt(2).itemCenterX
            tryCompare(row.itemAt(2), "currentScale", DockSettings.maxZoomFactor)
            row.mouseInside = false
            for (let i = 0; i < row.count; i++) {
                tryCompare(row.itemAt(i), "currentScale", 1.0)
                tryCompare(row.itemAt(i), "currentOffset", 0.0)
            }
            settleGeometryCallbacks()
            compare(DockModel.delegateGeometryRequests.length, 0,
                    "moving the hover target and zooming out must not republish")
        }
    }
}
