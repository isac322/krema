// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Mock of DockActions (production: context property). Records every call.
pragma Singleton
import QtQuick

QtObject {
    // Recorded calls: [{ name, args }]. Reassigned (not mutated) so bindings notify.
    property var calls: []
    function _record(name, args) { calls = calls.concat([{ name: name, args: Array.from(args) }]) }
    function callsTo(name) { return calls.filter(c => c.name === name) }
    signal taskLaunching(int index)
    function activate(i) { _record("activate", arguments) }
    function activateOrMinimize(i) { _record("activateOrMinimize", arguments) }
    function newInstance(i) { _record("newInstance", arguments) }
    function cycleWindows(i, forward) { _record("cycleWindows", arguments) }
    function moveTask(from, to) { _record("moveTask", arguments) }
    function addLauncher(url) { _record("addLauncher", arguments) }
    function removeLauncher(i) { _record("removeLauncher", arguments) }
    function openUrlsWithTask(i, urls) { _record("openUrlsWithTask", arguments) }

    // Restores every property listed in _resettable to its declared value.
    readonly property var _resettable: ["calls"]
    property var _defaults: ({})
    Component.onCompleted: {
        for (const k of _resettable) _defaults[k] = this[k]
    }
    function reset() {
        for (const k of _resettable) this[k] = _defaults[k]
    }
}
