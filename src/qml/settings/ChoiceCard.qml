// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// One option of a visual picker: a selectable card with a preview area above
// its label. Exposed to assistive technology as a radio button named after
// `text`. Cards sharing a parent are auto-exclusive. The owner binds `checked`
// to the setting with a Binding element (re-applied whenever the setting
// changes, also after a click toggled the card) and writes the setting in
// onChosen, which only fires for available cards.
//
//     ChoiceCard {
//         id: alwaysCard
//         text: i18n("Always visible")
//         Binding { target: alwaysCard; property: "checked"; value: DockSettings.visibilityMode === 0 }
//         onChosen: DockSettings.visibilityMode = 0
//         Rectangle { anchors.fill: parent }   // preview content
//     }
QQC2.AbstractButton {
    id: card

    /// Secondary line under the label; also the accessible description.
    property string description
    /// False dims the card, blocks selection and shows `unavailableReason`.
    property bool available: true
    property string unavailableReason
    /// Height of the preview area; children of the card fill it.
    property real previewHeight: Kirigami.Units.gridUnit * 4

    default property alias previewData: previewArea.data

    /// The user selected this card (click, Space, Return or AT-SPI action).
    signal chosen()

    // An auto-exclusive card cannot be unchecked by clicking it again, which
    // keeps the owner's Binding on `checked` intact.
    checkable: available
    autoExclusive: true
    activeFocusOnTab: true
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true

    implicitWidth: Kirigami.Units.gridUnit * 8
    implicitHeight: column.implicitHeight

    // Cards of one picker row share the tallest card's height, with their
    // previews and labels aligned to the top.
    Layout.fillWidth: true
    Layout.fillHeight: true

    opacity: available ? 1.0 : 0.45

    Accessible.role: Accessible.RadioButton
    Accessible.name: text
    Accessible.description: available ? description : unavailableReason
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.onPressAction: card.activate()
    Accessible.onToggleAction: card.activate()

    function activate() {
        if (card.available) {
            card.checked = true
            card.chosen()
        }
    }

    onClicked: if (available) chosen()
    Keys.onReturnPressed: card.activate()
    Keys.onEnterPressed: card.activate()

    QQC2.ToolTip.visible: !available && hovered && unavailableReason.length > 0
    QQC2.ToolTip.text: unavailableReason
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    background: Rectangle {
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false
        radius: Kirigami.Units.cornerRadius
        color: card.checked
            ? Qt.alpha(Kirigami.Theme.highlightColor, 0.12)
            : (card.hovered && card.available ? Qt.alpha(Kirigami.Theme.highlightColor, 0.05) : Kirigami.Theme.backgroundColor)
        border.width: card.checked || card.visualFocus ? 2 : 1
        border.color: card.checked || card.visualFocus
            ? Kirigami.Theme.highlightColor
            : (card.hovered && card.available
                ? Qt.alpha(Kirigami.Theme.highlightColor, 0.5)
                : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.2))

        Behavior on border.color {
            ColorAnimation { duration: Kirigami.Units.shortDuration }
        }
    }

    contentItem: ColumnLayout {
        id: column
        spacing: Kirigami.Units.smallSpacing

        Item {
            id: previewArea
            Layout.fillWidth: true
            Layout.preferredHeight: card.previewHeight
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            clip: true
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: descriptionLabel.visible ? 0 : Kirigami.Units.smallSpacing
            text: card.text
            font.bold: card.checked
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            Accessible.ignored: true
        }

        QQC2.Label {
            id: descriptionLabel
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            visible: text.length > 0
            text: card.available ? card.description : card.unavailableReason
            font: Kirigami.Theme.smallFont
            color: Kirigami.Theme.disabledTextColor
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            Accessible.ignored: true
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
