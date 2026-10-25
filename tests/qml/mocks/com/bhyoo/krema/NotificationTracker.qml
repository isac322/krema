// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Mock of NotificationTracker. Tests drive state via setUnread(), setSniAttention(),
// and the chat-state setters below.
pragma Singleton
import QtQuick

QtObject {
    // Recorded calls: [{ name, args }]. Reassigned (not mutated) so bindings notify.
    property var calls: []
    function _record(name, args) { calls = calls.concat([{ name: name, args: Array.from(args) }]) }
    function callsTo(name) { return calls.filter(c => c.name === name) }
    property int revision: 0
    property bool dndActive: false
    property bool badgesEnabledDefault: true
    property var badgeEnabledByAppId: ({})
    property var unread: ({})
    property var sni: ({})
    property var sniChatState: ({})
    property var sniChatMentionCounts: ({})
    property var sniMetadata: ({})
    function badgesEnabled(appId) {
        return Object.prototype.hasOwnProperty.call(badgeEnabledByAppId, appId)
            ? !!badgeEnabledByAppId[appId] : badgesEnabledDefault
    }
    function unreadCount(appId) { return unread[appId] || 0 }
    function sniNeedsAttention(appId) { return !!sni[appId] }
    function sniChatUnreadState(appId) { return sniChatState[appId] || 0 }
    function sniChatMentionCount(appId) { return sniChatMentionCounts[appId] || 0 }
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
    function setBadgesEnabled(enabled) {
        badgesEnabledDefault = enabled
        revision++
    }
    function setBadgesEnabledForApp(appId, enabled) {
        let values = Object.assign({}, badgeEnabledByAppId)
        values[appId] = enabled
        badgeEnabledByAppId = values
        revision++
    }
    function setSniChatState(appId, state) {
        let values = Object.assign({}, sniChatState)
        values[appId] = state
        sniChatState = values
        revision++
    }
    function setSniChatMentionCount(appId, count) {
        let values = Object.assign({}, sniChatMentionCounts)
        values[appId] = count
        sniChatMentionCounts = values
        revision++
    }
    function setSniChat(appId, state, count) {
        let states = Object.assign({}, sniChatState)
        let counts = Object.assign({}, sniChatMentionCounts)
        states[appId] = state
        counts[appId] = count
        sniChatState = states
        sniChatMentionCounts = counts
        revision++
    }
    function setSniMetadata(appId, metadata) {
        let values = Object.assign({}, sniMetadata)
        values[appId] = metadata
        sniMetadata = values
        revision++
    }

    // Restores every property listed in _resettable to its declared value.
    readonly property var _resettable: ["calls", "dndActive", "badgesEnabledDefault",
        "badgeEnabledByAppId", "unread", "sni", "sniChatState", "sniChatMentionCounts",
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
