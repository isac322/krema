// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

// Visual-tree helpers. Krema's QML uses ids (not objectName) for internal
// items, so tests locate them structurally by the properties they expose.
.pragma library
.import QtQuick as QQ

function findAll(root, predicate) {
    let out = []
    let stack = [root]
    while (stack.length > 0) {
        let node = stack.shift()
        if (!node) continue
        if (node !== root && predicate(node)) out.push(node)
        let kids = node.children
        if (kids) for (let i = 0; i < kids.length; i++) stack.push(kids[i])
    }
    return out
}

function findFirst(root, predicate) {
    let all = findAll(root, predicate)
    return all.length > 0 ? all[0] : null
}

function directChildren(item, predicate) {
    let out = []
    for (let i = 0; i < item.children.length; i++) {
        if (predicate(item.children[i])) out.push(item.children[i])
    }
    return out
}

// DockItem instances: the only items exposing both zoomScale and currentScale.
function isDockItem(o) {
    return o.zoomScale !== undefined && o.currentScale !== undefined
}

// DockItem's status indicator Flow (the only Flow inside a DockItem).
function indicatorDots(dockItem) {
    let flow = directChildren(dockItem, c => c.flow !== undefined)[0]
    if (!flow) return []
    // Exclude the Repeater itself (it has no color).
    return directChildren(flow, c => c.color !== undefined)
}

// DockItem's app icon Image (direct child with sourceSize + status).
function iconImage(dockItem) {
    return directChildren(dockItem, c => c.sourceSize !== undefined && c.status !== undefined)[0]
}

function isFitLabel(o) {
    return o.fontSizeMode === QQ.Text.Fit
}

// DockItem's badge: the direct Rectangle child containing a Text.Fit label.
function badge(dockItem) {
    return directChildren(dockItem, c => c.radius !== undefined
        && directChildren(c, isFitLabel).length > 0)[0]
}

function badgeLabel(dockItem) {
    let b = badge(dockItem)
    return b ? directChildren(b, isFitLabel)[0] : null
}

// The fallback initial shown inside the icon when no icon image is loaded.
function placeholderLabel(dockItem) {
    return findFirst(iconImage(dockItem), o => o.text !== undefined)
}

// DockItem's LauncherEntry progress bar (3px high rounded track).
function progressBar(dockItem) {
    return directChildren(dockItem, c => c.radius === 1.5 && c.height === 3)[0]
}
