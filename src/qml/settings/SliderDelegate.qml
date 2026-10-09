// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard

// Labelled slider row: label on the left, current value on the right in
// tabular figures, the slider below and optional captions under its ends.
// Bind `value` to the setting and write it back in onMoved; `valueText`
// formats the shown value. `active` is true while the slider is hovered,
// pressed or keyboard-focused, so a page can show the stage measurement.
//
//     SliderDelegate {
//         text: i18n("Icon size")
//         from: 24; to: 96; stepSize: 4
//         value: DockSettings.iconSize
//         valueText: i18nc("@label pixels", "%1 px", value)
//         minLabel: i18nc("@label slider minimum", "Small")
//         maxLabel: i18nc("@label slider maximum", "Large")
//         onMoved: (value) => DockSettings.iconSize = value
//     }
FormCard.AbstractFormDelegate {
    id: root

    property string description
    property real from: 0
    property real to: 1
    property real stepSize: 0
    property real value: 0
    property string valueText: String(value)
    /// Captions under the slider's start and end (optional).
    property string minLabel
    property string maxLabel

    /// The slider is hovered, pressed or has keyboard focus.
    readonly property bool active: slider.hovered || slider.pressed || slider.visualFocus

    /// The user moved the slider to `value`.
    signal moved(real value)

    readonly property alias slider: slider

    readonly property color secondaryTextColor: Kirigami.ColorUtils.linearInterpolation(
        Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.7)

    background: null
    Accessible.name: text
    Accessible.description: description

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: root.text
                elide: Text.ElideRight
                color: root.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                Accessible.ignored: true
            }

            QQC2.Label {
                text: root.valueText
                font.features: ({ "tnum": 1 })
                color: root.enabled ? root.secondaryTextColor : Kirigami.Theme.disabledTextColor
                Accessible.ignored: true
            }
        }

        QQC2.Slider {
            id: slider
            Layout.fillWidth: true
            from: root.from
            to: root.to
            // Breeze draws a tick mark per step whenever stepSize > 0, which
            // turns fine ranges (e.g. 0-2000 ms in 50 ms steps) into a comb,
            // and Kirigami.StyleHints.tickMarkStepSize needs KF > 6.13. So
            // the slider itself has no step: values are snapped here and the
            // keys step by root.stepSize.
            stepSize: 0
            value: root.value

            function snapped(v) {
                const s = root.stepSize > 0 ? Math.round((v - root.from) / root.stepSize) * root.stepSize + root.from : v
                return Math.max(root.from, Math.min(root.to, s))
            }
            function stepBy(steps) {
                const step = root.stepSize > 0 ? root.stepSize : (root.to - root.from) / 100
                root.moved(snapped(root.value + steps * step))
            }

            onMoved: root.moved(snapped(value))
            Keys.onLeftPressed: event => { stepBy(mirrored ? 1 : -1); event.accepted = true }
            Keys.onRightPressed: event => { stepBy(mirrored ? -1 : 1); event.accepted = true }
            Keys.onDownPressed: event => { stepBy(-1); event.accepted = true }
            Keys.onUpPressed: event => { stepBy(1); event.accepted = true }
            Keys.onPressed: event => {
                if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
                    stepBy(event.key === Qt.Key_PageUp ? 10 : -10)
                    event.accepted = true
                } else if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                    root.moved(event.key === Qt.Key_Home ? root.from : root.to)
                    event.accepted = true
                }
            }
            Accessible.name: root.text
            Accessible.description: root.valueText
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.minLabel.length > 0 || root.maxLabel.length > 0
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: root.minLabel
                elide: Text.ElideRight
                font: Kirigami.Theme.smallFont
                color: root.secondaryTextColor
                Accessible.ignored: true
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: root.maxLabel
                elide: Text.ElideLeft
                horizontalAlignment: Text.AlignRight
                font: Kirigami.Theme.smallFont
                color: root.secondaryTextColor
                Accessible.ignored: true
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: root.description.length > 0
            text: root.description
            wrapMode: Text.Wrap
            font: Kirigami.Theme.smallFont
            color: root.secondaryTextColor
            Accessible.ignored: true
        }
    }
}
