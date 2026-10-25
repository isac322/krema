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
    property var sniMetadata: ({})
    function unreadCount(appId) { return unread[appId] || 0 }
    function sniNeedsAttention(appId) { return !!sni[appId] }
    function _sniValue(appId, key) {
        let metadata = sniMetadata[appId]
        return metadata ? (metadata[key] || "") : ""
    }
    function sniTitle(appId) { return _sniValue(appId, "title") }
    function sniAttentionIconName(appId) { return _sniValue(appId, "attentionIconName") }
    function sniOverlayIconName(appId) { return _sniValue(appId, "overlayIconName") }
    function sniToolTipTitle(appId) { return _sniValue(appId, "toolTipTitle") }
    function sniToolTipSubTitle(appId) { return _sniValue(appId, "toolTipSubTitle") }
    function clearUnreadNotifications(appId) {
        _record("clearUnreadNotifications", arguments)
        if (!Object.prototype.hasOwnProperty.call(unread, appId) || unread[appId] <= 0) return
        let u = Object.assign({}, unread); delete u[appId]; unread = u; revision++
    }
    function setUnread(appId, n) { let u = Object.assign({}, unread); u[appId] = n; unread = u; revision++ }
    function setSniAttention(appId, v) { let s = Object.assign({}, sni); s[appId] = v; sni = s; revision++ }
    function setSniMetadata(appId, metadata) {
        let values = Object.assign({}, sniMetadata)
        values[appId] = metadata
        sniMetadata = values
        revision++
    }

    // Restores every property listed in _resettable to its declared value.
    readonly property var _resettable: ["calls", "dndActive", "unread", "sni",
        "sniMetadata"]
    property var _defaults: ({})
    Component.onCompleted: {
        for (const k of _resettable) _defaults[k] = this[k]
    }
    function reset() {
        for (const k of _resettable) this[k] = _defaults[k]
        revision++
    }
}
