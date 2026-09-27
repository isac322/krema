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
    function unreadCount(appId) { return unread[appId] || 0 }
    function sniNeedsAttention(appId) { return !!sni[appId] }
    function clearUnreadNotifications(appId) {
        _record("clearUnreadNotifications", arguments)
        let u = Object.assign({}, unread); delete u[appId]; unread = u; revision++
    }
    function setUnread(appId, n) { let u = Object.assign({}, unread); u[appId] = n; unread = u; revision++ }
    function setSniAttention(appId, v) { let s = Object.assign({}, sni); s[appId] = v; sni = s; revision++ }

    // Restores every property listed in _resettable to its declared value.
    readonly property var _resettable: ["calls", "dndActive", "unread", "sni"]
    property var _defaults: ({})
    Component.onCompleted: {
        for (const k of _resettable) _defaults[k] = this[k]
    }
    function reset() {
        for (const k of _resettable) this[k] = _defaults[k]
        revision++
    }
}
