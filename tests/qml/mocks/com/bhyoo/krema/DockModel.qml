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
    // Bumped when task chat state/count changes so DockItem bindings refresh.
    property int chatStateRevision: 0
    // Fixtures supply prepartitioned task rows and their pinned boundary;
    // production DockModel's sorting is exercised by the model/native suites.
    property int pinnedTaskCount: 0
    property var delegateGeometryRequests: []

    function _role(i, name) { return tasksModel.get(i, name) }
    function isPinned(i) { return !!_role(i, "IsLauncher") }
    function appId(i) { return _role(i, "AppId") || "" }
    function iconName(i) { return _role(i, "IconName") || "" }
    function launcherUrl(i) { return _role(i, "LauncherUrl") || "" }
    function isOnCurrentDesktop(i) { let v = _role(i, "IsOnCurrentDesktop"); return v === undefined ? true : !!v }
    function childCount(i) { return tasksModel.rowCount(tasksModel.index(i, 0)) }
    function isDesktopFile(url) { return url.toString().endsWith(".desktop") }
    // Presentation fixtures provide title-classifier output as roles, including
    // independent maxima across grouped children.
    function _taskChatMaximum(i, role) {
        let maximum = _role(i, "IsWindow") ? Number(_role(i, role) || 0) : 0
        for (let child = 0; child < childCount(i); ++child)
            maximum = Math.max(maximum, Number(tasksModel.get(i, role, child) || 0))
        return maximum
    }
    function taskChatUnreadState(i) { return _taskChatMaximum(i, "ChatUnreadState") }
    function taskChatMentionCount(i) { return _taskChatMaximum(i, "ChatMentionCount") }
    function setTaskChatUnreadState(i, state) {
        tasksModel.setTaskData(i, "ChatUnreadState", state)
        chatStateRevision++
    }
    function setTaskChatMentionCount(i, count) {
        tasksModel.setTaskData(i, "ChatMentionCount", count)
        chatStateRevision++
    }
    function setTaskChat(i, state, count) {
        tasksModel.setTaskData(i, "ChatUnreadState", state)
        tasksModel.setTaskData(i, "ChatMentionCount", count)
        chatStateRevision++
    }

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
        chatStateRevision = 0
        pinnedTaskCount = 0
        resetDelegateGeometryRequests()
    }
}
