// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami

// Round color swatch that opens a color dialog. `accessibleName` names the
// setting; the current color is appended for assistive technology. Writes the
// picked color through `picked(color)`.
//
//     ColorSwatchButton {
//         accessibleName: i18n("Tint color")
//         color: DockSettings.tintColor
//         onPicked: (color) => DockSettings.tintColor = color
//     }
QQC2.AbstractButton {
    id: root

    property color color
    property string accessibleName
    property bool showAlphaChannel: false
    property string dialogTitle: accessibleName

    signal picked(color color)

    implicitWidth: Kirigami.Units.gridUnit * 2
    implicitHeight: implicitWidth
    activeFocusOnTab: true
    hoverEnabled: true

    Accessible.role: Accessible.Button
    Accessible.name: i18nc("@action:button color setting name and current value", "%1: %2", accessibleName, root.color.toString())
    Accessible.onPressAction: colorDialog.open()

    onClicked: colorDialog.open()
    Keys.onReturnPressed: colorDialog.open()
    Keys.onEnterPressed: colorDialog.open()

    QQC2.ToolTip.visible: hovered
    QQC2.ToolTip.text: accessibleName
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    background: Rectangle {
        radius: width / 2
        color: root.color
        border.width: root.visualFocus ? 2 : 1
        border.color: root.visualFocus || root.hovered
            ? Kirigami.Theme.highlightColor
            : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.3)
    }

    ColorDialog {
        id: colorDialog
        title: root.dialogTitle
        selectedColor: root.color
        options: root.showAlphaChannel ? ColorDialog.ShowAlphaChannel : 0
        onAccepted: root.picked(selectedColor)
    }
}
