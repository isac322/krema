// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// A transparent replacement for FormCard.FormCard.
// Provides the same layout structure (ColumnLayout with _roundCorners)
// without the card background, border, or shadow.
Item {
    id: root

    default property alias delegates: internalColumn.data

    property real maximumWidth: Kirigami.Units.gridUnit * 30
    readonly property bool cardWidthRestricted: root.width > root.maximumWidth

    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    Layout.fillWidth: true

    implicitHeight: internalColumn.implicitHeight

    ColumnLayout {
        id: internalColumn
        readonly property bool _roundCorners: root.cardWidthRestricted
        spacing: 0
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            leftMargin: root.cardWidthRestricted ? Math.round((root.width - root.maximumWidth) / 2) : 0
            rightMargin: root.cardWidthRestricted ? Math.round((root.width - root.maximumWidth) / 2) : 0
        }
    }
}
