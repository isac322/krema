// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Mock of PreviewController (production: context property).
pragma Singleton
import QtQuick

QtObject {
    // Recorded calls: [{ name, args }]. Reassigned (not mutated) so bindings notify.
    property var calls: []
    function _record(name, args) { calls = calls.concat([{ name: name, args: Array.from(args) }]) }
    function callsTo(name) { return calls.filter(c => c.name === name) }
    property bool visible: false
    property int parentIndex: -1
    property string appName: ""
    property real contentX: 0
    property real contentY: 0
    property real contentWidth: 0
    property real contentHeight: 0
    property bool previewHovered: false
    property bool previewKeyboardActive: false
    property int focusedThumbnailIndex: -1
    function showPreview(i, pos, ext) { _record("showPreview", arguments); parentIndex = i; visible = true }
    function hidePreview() { _record("hidePreview", arguments); visible = false }
    function hidePreviewDelayed() { _record("hidePreviewDelayed", arguments) }
    function setPreviewHovered(v) { previewHovered = v }
    function setContentSize(w, h) { contentWidth = w; contentHeight = h }
    function startPreviewKeyboardNav() { _record("startPreviewKeyboardNav", arguments); previewKeyboardActive = true; focusedThumbnailIndex = 0 }
    function endPreviewKeyboardNav() { _record("endPreviewKeyboardNav", arguments); previewKeyboardActive = false }
    function navigatePreviewThumbnail(d) { _record("navigatePreviewThumbnail", arguments) }
    function activatePreviewThumbnail() { _record("activatePreviewThumbnail", arguments) }
    function closePreviewThumbnail() { _record("closePreviewThumbnail", arguments) }
    function focusedThumbnailTitle() { return "" }
    function focusedThumbnailIsActive() { return false }
    function focusedThumbnailIsMinimized() { return false }
    function previewThumbnailCount() { return 0 }

    // Restores every property listed in _resettable to its declared value.
    readonly property var _resettable: ["calls", "visible", "parentIndex", "appName", "contentX", "contentY", "contentWidth", "contentHeight", "previewHovered", "previewKeyboardActive", "focusedThumbnailIndex"]
    property var _defaults: ({})
    Component.onCompleted: {
        for (const k of _resettable) _defaults[k] = this[k]
    }
    function reset() {
        for (const k of _resettable) this[k] = _defaults[k]
    }
}
