// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard

// Labelled slider row with its current value on the right. Bind `value` to the
// setting and write it back in onMoved; `valueText` formats the shown value.
//
//     SliderDelegate {
//         text: i18n("Icon size")
//         from: 24; to: 96; stepSize: 4
//         value: DockSettings.iconSize
//         valueText: i18nc("@label pixels", "%1 px", value)
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

    /// The user moved the slider to `value`.
    signal moved(real value)

    readonly property alias slider: slider

    background: null
    Accessible.name: text
    Accessible.description: description

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: root.text
                elide: Text.ElideRight
                color: root.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                Accessible.ignored: true
            }

            QQC2.Label {
                text: root.valueText
                color: Kirigami.Theme.disabledTextColor
                Accessible.ignored: true
            }
        }

        QQC2.Slider {
            id: slider
            Layout.fillWidth: true
            from: root.from
            to: root.to
            stepSize: root.stepSize
            snapMode: root.stepSize > 0 ? QQC2.Slider.SnapAlways : QQC2.Slider.NoSnap
            value: root.value
            onMoved: root.moved(value)
            Accessible.name: root.text
            Accessible.description: root.valueText
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: root.description.length > 0
            text: root.description
            wrapMode: Text.Wrap
            font: Kirigami.Theme.smallFont
            color: Kirigami.Theme.disabledTextColor
            Accessible.ignored: true
        }
    }
}
