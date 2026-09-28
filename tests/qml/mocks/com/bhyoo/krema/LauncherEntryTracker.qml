// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Mock of LauncherEntryTracker (Unity LauncherEntry state per launcher URL).
// Entries are keyed by the task's LauncherUrlWithoutIcon role; tests drive them
// with setEntry(url, { count, countVisible, urgent, progress, progressVisible }).
// A URL without an entry reports "no data", so DockItem falls back to
// NotificationTracker.
pragma Singleton
import QtQuick

QtObject {
    property int revision: 0
    property var entries: ({})

    function _entry(url) { return entries[url.toString()] || {} }
    function count(url) { return _entry(url).count || 0 }
    function countVisible(url) { return !!_entry(url).countVisible }
    function urgent(url) { return !!_entry(url).urgent }
    function progress(url) { return _entry(url).progress || 0 }
    function progressVisible(url) { return !!_entry(url).progressVisible }

    function setEntry(url, props) {
        let e = Object.assign({}, entries)
        e[url] = Object.assign({}, e[url] || {}, props)
        entries = e
        revision++
    }

    function reset() {
        entries = {}
        revision++
    }
}
