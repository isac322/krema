// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Mock of NotificationTracker. Tests drive state via setUnread()/setSniAttention().
pragma Singleton
import QtQuick

QtObject {
    // Recorded calls: [{ name, args }]. Reassigned (not mutated) so bindings notify.
    property var calls: []
    function _record(name, args) { calls = calls.concat([{ name: name, args: Array.from(args) }]) }
    function callsTo(name) { return calls.filter(c => c.name === name) }
    property int revision: 0
    property bool dndActive: false
    property var unread: ({})
    property var sni: ({})
    // Stage 2 notification records mirror NotificationTracker.notifications().
    property var notificationRows: []
    function unreadCount(appId) { return unread[appId] || 0 }
    function sniNeedsAttention(appId) { return !!sni[appId] }
    function notifications(appId, activeOnly) {
        let rows = notificationRows
        if (appId) rows = rows.filter(r => r.desktopEntry === appId)
        if (activeOnly) rows = rows.filter(r => !!r.active)
        return rows
    }
    function _replaceNotification(id, changes) {
        let rows = notificationRows.slice()
        for (let i = 0; i < rows.length; ++i) {
            if (Number(rows[i].id) === Number(id)) {
                rows[i] = Object.assign({}, rows[i], changes)
                notificationRows = rows
                revision++
                return
            }
        }
    }
    function markRead(id) {
        _record("markRead", arguments)
        _replaceNotification(id, { read: true })
    }
    function markDismissed(id) {
        _record("markDismissed", arguments)
        _replaceNotification(id, { dismissed: true })
    }
    function invokeAction(id, action) {
        _record("invokeAction", arguments)
    }
    function setNotifications(rows) { notificationRows = rows; revision++ }
    function clearUnreadNotifications(appId) {
        _record("clearUnreadNotifications", arguments)
        if (!Object.prototype.hasOwnProperty.call(unread, appId) || unread[appId] <= 0) return
        let u = Object.assign({}, unread); delete u[appId]; unread = u; revision++
    }
    function setUnread(appId, n) { let u = Object.assign({}, unread); u[appId] = n; unread = u; revision++ }
    function setSniAttention(appId, v) { let s = Object.assign({}, sni); s[appId] = v; sni = s; revision++ }

    // Restores every property listed in _resettable to its declared value.
    readonly property var _resettable: ["calls", "dndActive", "unread", "sni",
        "notificationRows"]
    property var _defaults: ({})
    Component.onCompleted: {
        for (const k of _resettable) _defaults[k] = this[k]
    }
    function reset() {
        for (const k of _resettable) this[k] = _defaults[k]
        revision++
    }
}
