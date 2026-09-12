// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick

/**
 * @brief Tier 2: Logical Island.
 * Groups items for organizational logic and provides a subtle glass background.
 */
Item {
    id: root
    property var islandData

    default property alias content: container.data

    width: implicitWidth
    height: implicitHeight

    implicitWidth: DockView.isVertical ? parent.width : container.implicitWidth + 16
    implicitHeight: DockView.isVertical ? container.implicitHeight + 16 : container.implicitHeight

    x: DockView.isVertical ? 0 : -8
    y: DockView.isVertical ? -8 : 0

    Rectangle {
        anchors.fill: parent
        // Rule 18: Island Glass - Premium Visual Separation
        
        color: Qt.rgba(255, 255, 255, 0.05)
        border.color: Qt.rgba(255, 255, 255, 0.1)
        border.width: 1
        radius: Math.min(width, height) / 2
        visible: container.children.length > 0
    }

    Item {
        id: container
        x: DockView.isVertical ? 0 : 8
        y: DockView.isVertical ? 8 : 0
        implicitWidth: childrenRect.width
        implicitHeight: childrenRect.height
    }
}
