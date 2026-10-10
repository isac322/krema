// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Effects
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// One option of a visual picker: a rounded thumbnail tile with the label and
// description centered below it. The selected option gets an accent ring
// around the tile and a bold label; hover shows a faint ring. Exposed to
// assistive technology as a radio button named after `text`. Cards sharing a
// parent are auto-exclusive. The owner binds `checked` to the setting with a
// Binding element (re-applied whenever the setting changes, also after a
// click toggled the card) and writes the setting in onChosen, which only
// fires for available cards.
//
//     ChoiceCard {
//         id: alwaysCard
//         text: i18n("Always visible")
//         Binding { target: alwaysCard; property: "checked"; value: DockSettings.visibilityMode === 0 }
//         onChosen: DockSettings.visibilityMode = 0
//         DesktopStage { anchors.fill: parent; elevated: false }   // tile content
//     }
//
// Children fill the tile and are clipped to its rounded corners. Cards of one
// picker row should share `previewHeight` so their tiles have one aspect.
QQC2.AbstractButton {
    id: card

    /// Secondary line under the label; also the accessible description.
    property string description
    /// False dims the card, blocks selection and shows `unavailableReason`.
    property bool available: true
    property string unavailableReason
    /// Height of the thumbnail tile; children of the card fill it.
    property real previewHeight: Kirigami.Units.gridUnit * 4

    default property alias previewData: previewArea.data

    /// The user selected this card (click, Space, Return or AT-SPI action).
    signal chosen()

    readonly property real ringWidth: 3
    // Space between the tile edge and the selection ring.
    readonly property real ringGap: 2
    readonly property real tileRadius: Kirigami.Units.cornerRadius * 2
    readonly property color secondaryTextColor: Kirigami.ColorUtils.linearInterpolation(
        Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.7)

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
    // tiles and labels aligned to the top.
    Layout.fillWidth: true
    Layout.fillHeight: true

    opacity: available && enabled ? 1.0 : 0.45

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

    background: null

    contentItem: ColumnLayout {
        id: column
        spacing: Kirigami.Units.smallSpacing

        Item {
            id: tileFrame
            Layout.fillWidth: true
            Layout.preferredHeight: card.previewHeight + 2 * (card.ringWidth + card.ringGap)

            // Selection ring (accent), keyboard focus ring (focus color) or
            // hover ring (faint accent) around the tile.
            Rectangle {
                anchors.fill: parent
                radius: card.tileRadius + card.ringWidth + card.ringGap
                color: Qt.alpha(Kirigami.Theme.highlightColor, 0)
                visible: border.width > 0
                border.width: card.checked ? card.ringWidth
                    : (card.visualFocus || (card.hovered && card.available)) ? 2 : 0
                border.color: card.visualFocus ? Kirigami.Theme.focusColor
                    : card.checked ? Kirigami.Theme.highlightColor
                    : Qt.alpha(Kirigami.Theme.highlightColor, 0.45)

                Behavior on border.color {
                    ColorAnimation { duration: Kirigami.Units.shortDuration }
                }
            }

            Item {
                id: previewArea
                anchors.fill: parent
                anchors.margins: card.ringWidth + card.ringGap

                layer.enabled: true
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: tileMask
                    maskThresholdMin: 0.5
                    maskSpreadAtMin: 1.0
                }

                // Tile ground under the preview.
                Rectangle {
                    anchors.fill: parent
                    color: Kirigami.Theme.alternateBackgroundColor
                }
            }

            Rectangle {
                id: tileMask
                anchors.fill: previewArea
                radius: card.tileRadius
                visible: false
                layer.enabled: true
            }

            // Hairline edge so light tiles stay defined on light cards.
            Rectangle {
                anchors.fill: previewArea
                radius: card.tileRadius
                color: Qt.alpha(Kirigami.Theme.backgroundColor, 0)
                border.width: 1
                border.color: Qt.alpha(Kirigami.Theme.textColor, 0.12)
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            text: card.text
            color: Kirigami.Theme.textColor
            font.bold: card.checked
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            Accessible.ignored: true
        }

        QQC2.Label {
            id: descriptionLabel
            Layout.fillWidth: true
            visible: text.length > 0
            text: card.available ? card.description : card.unavailableReason
            font: Kirigami.Theme.smallFont
            color: card.secondaryTextColor
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            Accessible.ignored: true
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
