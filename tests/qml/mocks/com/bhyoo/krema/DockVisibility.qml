// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Mock of DockVisibilityController (production: context property).
pragma Singleton
import QtQuick

QtObject {
    // Recorded calls: [{ name, args }]. Reassigned (not mutated) so bindings notify.
    property var calls: []
    function _record(name, args) { calls = calls.concat([{ name: name, args: Array.from(args) }]) }
    function callsTo(name) { return calls.filter(c => c.name === name) }
    property bool dockVisible: true
    property bool hovered: false
    property bool interacting: false
    property bool keyboardActive: false
    property rect panelRect
    function setPanelRect(x, y, w, h) { panelRect = Qt.rect(x, y, w, h) }
    function setHovered(v) { _record("setHovered", arguments); hovered = v }
    function setInteracting(v) { _record("setInteracting", arguments); interacting = v }
    function setKeyboardActive(v) { _record("setKeyboardActive", arguments); keyboardActive = v }

    // Restores every property listed in _resettable to its declared value.
    readonly property var _resettable: ["calls", "dockVisible", "hovered", "interacting", "keyboardActive", "panelRect"]
    property var _defaults: ({})
    Component.onCompleted: {
        for (const k of _resettable) _defaults[k] = this[k]
    }
    function reset() {
        for (const k of _resettable) this[k] = _defaults[k]
    }
}
