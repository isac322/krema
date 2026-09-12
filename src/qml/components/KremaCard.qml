// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    Layout.fillWidth: true
    implicitHeight: contentLayout.implicitHeight + 32
    
    // The "Elevated" look: slightly lighter than the main theme base
    color: (typeof theme !== "undefined") ? theme.card : "#121112" 
    radius: 12
    border.color: (typeof theme !== "undefined") ? theme.border : "#2A282A" 
    border.width: 1

    // This allows us to put anything inside the card when we use it
    default property alias content: contentLayout.data

    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16
    }
}
