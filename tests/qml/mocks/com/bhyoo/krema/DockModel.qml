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
    property int currentDesktop: 1
    // 0=ShowAll, 1=DimOtherDesktops, 2=CurrentOnly
    property int virtualDesktopMode: 0
    property var delegateGeometryRequests: []

    function _role(i, name) { return tasksModel.get(i, name) }
    function isPinned(i) { return !!_role(i, "IsLauncher") }
    function appId(i) { return _role(i, "AppId") || "" }
    function iconName(i) { return _role(i, "IconName") || "" }
    function launcherUrl(i) { return _role(i, "LauncherUrl") || "" }
    function isOnCurrentDesktop(i) { let v = _role(i, "IsOnCurrentDesktop"); return v === undefined ? true : !!v }
    function childCount(i) { return tasksModel.rowCount(tasksModel.index(i, 0)) }
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
        currentDesktop = 1
        virtualDesktopMode = 0
        resetDelegateGeometryRequests()
    }
}
