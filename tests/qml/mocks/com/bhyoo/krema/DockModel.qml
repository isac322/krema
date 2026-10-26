// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Mock of DockModel. Per-index lookups read roles from tasksModel so a test
// only has to populate one place: DockModel.tasksModel.addTask({ ... }).
// Mock-only roles: IconName, IsOnCurrentDesktop; IsLauncher backs isPinned().
pragma Singleton
import QtQuick
import krema.test

QtObject {
    readonly property MockTasksModel tasksModel: MockTasksModel {}
    readonly property MockTasksModel items: tasksModel
    property QtObject trashManager: QtObject {
        property int openRequests: 0
        property var trashedUrls: []
        function openTrash() { openRequests++ }
        function trashUrls(urls) { trashedUrls = trashedUrls.concat(urls) }
        function reset() {
            openRequests = 0
            trashedUrls = []
        }
    }
    property int currentDesktop: 1
    // 0=ShowAll, 1=DimOtherDesktops, 2=CurrentOnly
    property int virtualDesktopMode: 0
    property int trashSlot: -1
    // Fixtures supply prepartitioned task rows and their pinned boundary;
    // production DockModel's sorting is exercised by the model/native suites.
    property int pinnedTaskCount: 0
    property var delegateGeometryRequests: []

    function taskIndex(i) { return i >= 0 && i < tasksModel.rowCount() ? i : -1 }
    function isTrash(i) { return false }
    function trashIndex() { return -1 }
    function visualIndex(i) { return taskIndex(i) }
    function taskModelIndex(i) {
        let task = taskIndex(i)
        return task >= 0 ? tasksModel.index(task, 0) : undefined
    }
    function _role(i, name) { return tasksModel.get(i, name) }
    function isPinned(i) { return !!_role(taskIndex(i), "IsLauncher") }
    function appId(i) { return _role(taskIndex(i), "AppId") || "" }
    function iconName(i) { return _role(taskIndex(i), "IconName") || "" }
    function launcherUrl(i) { return _role(taskIndex(i), "LauncherUrl") || "" }
    function isOnCurrentDesktop(i) { let v = _role(taskIndex(i), "IsOnCurrentDesktop"); return v === undefined ? true : !!v }
    function childCount(i) { return tasksModel.rowCount(tasksModel.index(taskIndex(i), 0)) }
    function isDesktopFile(url) { return url.toString().endsWith(".desktop") }

    function publishDelegateGeometry(i, delegate) {
        delegateGeometryRequests = delegateGeometryRequests.concat([{
            index: i,
            isWindow: !!_role(i, "IsWindow"),
            isGroupParent: !!_role(i, "IsGroupParent"),
            childCount: _role(i, "ChildCount") || 0,
            geometry: {
                x: delegate.x,
                y: delegate.y,
                width: delegate.width,
                height: delegate.height,
            },
        }])
    }

    function resetDelegateGeometryRequests() {
        delegateGeometryRequests = []
    }

    function reset() {
        tasksModel.reset()
        trashManager.reset()
        currentDesktop = 1
        virtualDesktopMode = 0
        trashSlot = -1
        pinnedTaskCount = 0
        resetDelegateGeometryRequests()
    }
}
