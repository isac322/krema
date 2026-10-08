// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Effects
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

FormCard.FormCardPage {
    id: page

    title: i18n("Animations & Badges")

    // Responsive vertical padding: scales with page width
    topPadding: Math.round(Kirigami.Units.gridUnit * Math.max(0.5, Math.min(1.5, width / 800)))
    bottomPadding: topPadding

    // Looping preview animations only run while the page is actually shown.
    readonly property bool previewsActive: page.visible
        && Window.window !== null
        && Window.window.visible
        && Window.window.visibility !== Window.Minimized

    // Option order matches the AttentionAnimation kcfg enum (0..6).
    readonly property var attentionOptions: [
        i18n("None"),
        i18n("Bounce"),
        i18n("Wiggle"),
        i18n("Pulse"),
        i18n("Glow"),
        i18n("Dot color"),
        i18n("Blink")
    ]

    // Option order matches the BadgeDisplayMode kcfg enum (0..2).
    readonly property var badgeOptions: [
        i18n("Number"),
        i18n("Dot"),
        i18n("Off")
    ]

    readonly property int pickerColumnWidth: Kirigami.Units.gridUnit * 7

    // Simplified dock tile: a strip of panel with one icon and a running
    // indicator dot, animated like DockItem.qml does for the given type.
    component AttentionSample: Item {
        id: sample

        property int animationType: 0
        property bool playing: false

        readonly property real iconSize: Kirigami.Units.iconSizes.medium
        // Mirrors DockItem's 14 px bounce for a 48 px icon, scaled to the sample.
        readonly property real bounceHeight: Math.round(iconSize * 14 / 48)

        // Dot color animation state (DockItem._dotBlinkOpacity)
        property real dotBlinkOpacity: 1.0
        // Blink animation state (DockItem._blinkOpacity)
        property real blinkOpacity: 1.0

        Accessible.ignored: true

        Rectangle {
            id: panelStrip
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            width: sample.iconSize + Kirigami.Units.largeSpacing * 2
            height: sample.iconSize + Kirigami.Units.smallSpacing * 3
            radius: Kirigami.Units.cornerRadius
            color: Kirigami.Theme.alternateBackgroundColor
            border.width: 1
            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor,
                                                                  Kirigami.Theme.textColor, 0.15)
        }

        Kirigami.Icon {
            id: sampleIcon
            anchors.horizontalCenter: panelStrip.horizontalCenter
            anchors.top: panelStrip.top
            anchors.topMargin: Kirigami.Units.smallSpacing
            width: sample.iconSize
            height: sample.iconSize
            source: "internet-mail"
            opacity: sample.blinkOpacity

            transform: [
                Translate { id: bounceT; y: 0 },
                Rotation { id: rotateT; origin.x: sample.iconSize / 2; origin.y: sample.iconSize / 2; angle: 0 },
                Scale { id: scaleT; origin.x: sample.iconSize / 2; origin.y: sample.iconSize / 2; xScale: 1.0; yScale: xScale }
            ]
        }

        // Type 4: Glow — highlight-colored halo pulsing around the icon
        MultiEffect {
            id: glow
            source: sampleIcon
            anchors.fill: sampleIcon
            paddingRect: Qt.rect(16, 16, 16, 16)
            visible: sample.animationType === 4
            shadowEnabled: true
            shadowColor: Kirigami.Theme.highlightColor
            shadowBlur: 0.7
            shadowScale: 1.12
            shadowHorizontalOffset: 0
            shadowVerticalOffset: 0
            shadowOpacity: 0.5

            SequentialAnimation on shadowOpacity {
                running: sample.playing && sample.animationType === 4
                loops: Animation.Infinite
                NumberAnimation { to: 0.85; duration: 800; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.15; duration: 800; easing.type: Easing.InOutSine }
            }
        }

        // Running indicator dot (type 5 recolors and blinks it)
        Rectangle {
            anchors.horizontalCenter: panelStrip.horizontalCenter
            anchors.bottom: panelStrip.bottom
            anchors.bottomMargin: Math.round(Kirigami.Units.smallSpacing / 2)
            width: 4
            height: width
            radius: width / 2
            color: sample.animationType === 5 ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
            opacity: sample.animationType === 5 ? sample.dotBlinkOpacity : 0.8
        }

        // Type 1: Bounce
        SequentialAnimation {
            running: sample.playing && sample.animationType === 1
            loops: Animation.Infinite
            onStopped: bounceT.y = 0
            NumberAnimation { target: bounceT; property: "y"; to: -sample.bounceHeight; duration: 300; easing.type: Easing.OutQuad }
            NumberAnimation { target: bounceT; property: "y"; to: 0; duration: 300; easing.type: Easing.InBounce }
            PauseAnimation { duration: 800 }
        }

        // Type 2: Wiggle
        SequentialAnimation {
            running: sample.playing && sample.animationType === 2
            loops: Animation.Infinite
            onStopped: rotateT.angle = 0
            NumberAnimation { target: rotateT; property: "angle"; to: 5; duration: 80; easing.type: Easing.InOutSine }
            NumberAnimation { target: rotateT; property: "angle"; to: -5; duration: 160; easing.type: Easing.InOutSine }
            NumberAnimation { target: rotateT; property: "angle"; to: 3; duration: 120; easing.type: Easing.InOutSine }
            NumberAnimation { target: rotateT; property: "angle"; to: -3; duration: 120; easing.type: Easing.InOutSine }
            NumberAnimation { target: rotateT; property: "angle"; to: 1; duration: 100; easing.type: Easing.InOutSine }
            NumberAnimation { target: rotateT; property: "angle"; to: -1; duration: 100; easing.type: Easing.InOutSine }
            NumberAnimation { target: rotateT; property: "angle"; to: 0; duration: 80; easing.type: Easing.InOutSine }
            PauseAnimation { duration: 2000 }
        }

        // Type 3: Pulse
        SequentialAnimation {
            running: sample.playing && sample.animationType === 3
            loops: Animation.Infinite
            onStopped: scaleT.xScale = 1.0
            NumberAnimation { target: scaleT; property: "xScale"; to: 1.15; duration: 600; easing.type: Easing.InOutSine }
            NumberAnimation { target: scaleT; property: "xScale"; to: 1.0; duration: 600; easing.type: Easing.InOutSine }
            PauseAnimation { duration: 400 }
        }

        // Type 5: Dot color
        SequentialAnimation {
            running: sample.playing && sample.animationType === 5
            loops: Animation.Infinite
            onStopped: sample.dotBlinkOpacity = 1.0
            NumberAnimation { target: sample; property: "dotBlinkOpacity"; to: 0.3; duration: 500; easing.type: Easing.InOutSine }
            NumberAnimation { target: sample; property: "dotBlinkOpacity"; to: 1.0; duration: 500; easing.type: Easing.InOutSine }
        }

        // Type 6: Blink
        SequentialAnimation {
            running: sample.playing && sample.animationType === 6
            loops: Animation.Infinite
            onStopped: sample.blinkOpacity = 1.0
            NumberAnimation { target: sample; property: "blinkOpacity"; to: 0.2; duration: 400; easing.type: Easing.InOutSine }
            NumberAnimation { target: sample; property: "blinkOpacity"; to: 1.0; duration: 400; easing.type: Easing.InOutSine }
        }
    }

    // --- Attention ---
    FormCard.FormHeader {
        title: i18n("Attention")
    }

    FormSection {
        FormCard.AbstractFormDelegate {
            id: attentionPicker
            background: null
            Accessible.role: Accessible.Grouping
            Accessible.name: i18n("Attention animation")
            Accessible.description: i18n("Animation when an app demands attention")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Attention animation")
                    wrapMode: Text.Wrap
                    Accessible.ignored: true
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Animation when an app demands attention")
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.Wrap
                    Accessible.ignored: true
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: Math.max(1, Math.floor(width / page.pickerColumnWidth))
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: page.attentionOptions

                        ChoiceCard {
                            id: attentionCard

                            required property int index
                            required property string modelData

                            Layout.fillWidth: true
                            implicitWidth: page.pickerColumnWidth - Kirigami.Units.smallSpacing
                            text: modelData

                            Binding {
                                target: attentionCard
                                property: "checked"
                                value: DockSettings.attentionAnimation === attentionCard.index
                            }
                            onChosen: DockSettings.attentionAnimation = attentionCard.index

                            AttentionSample {
                                anchors.fill: parent
                                animationType: attentionCard.index
                                // The selected card always plays; others preview on hover or focus.
                                playing: page.previewsActive
                                    && (attentionCard.checked || attentionCard.hovered || attentionCard.activeFocus)
                            }
                        }
                    }
                }
            }
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.attentionAnimation > 0
        }

        FormCard.FormSpinBoxDelegate {
            visible: DockSettings.attentionAnimation > 0
            label: i18n("Attention duration (seconds, 0 = infinite)")
            from: 0
            to: 60
            value: DockSettings.attentionAnimationDuration
            onValueChanged: DockSettings.attentionAnimationDuration = value
            textFromValue: function(value, locale) {
                return value === 0 ? i18n("Infinite") : Number(value).toLocaleString(locale, "f", 0)
            }
            valueFromText: function(text, locale) {
                if (text === i18n("Infinite")) {
                    return 0
                }
                const parsed = Number.fromLocaleString(locale, text)
                return isNaN(parsed) ? 0 : Math.round(parsed)
            }
        }
    }

    // --- Badges ---
    FormCard.FormHeader {
        title: i18n("Badges")
    }

    FormSection {
        FormCard.AbstractFormDelegate {
            background: null
            Accessible.role: Accessible.Grouping
            Accessible.name: i18n("Badge display")
            Accessible.description: i18n("How notification badges appear on dock icons")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Badge display")
                    wrapMode: Text.Wrap
                    Accessible.ignored: true
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("How notification badges appear on dock icons")
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.Wrap
                    Accessible.ignored: true
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: Math.max(1, Math.min(3, Math.floor(width / page.pickerColumnWidth)))
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: page.badgeOptions

                        ChoiceCard {
                            id: badgeCard

                            required property int index
                            required property string modelData

                            Layout.fillWidth: true
                            implicitWidth: page.pickerColumnWidth - Kirigami.Units.smallSpacing
                            text: modelData

                            Binding {
                                target: badgeCard
                                property: "checked"
                                value: DockSettings.badgeDisplayMode === badgeCard.index
                            }
                            onChosen: DockSettings.badgeDisplayMode = badgeCard.index

                            // Sample icon with the badge style, sized like DockItem's badge
                            Item {
                                id: badgeSample
                                anchors.centerIn: parent
                                width: Kirigami.Units.iconSizes.large
                                height: width
                                Accessible.ignored: true

                                Kirigami.Icon {
                                    id: badgeIcon
                                    anchors.fill: parent
                                    source: "internet-mail"
                                }

                                Rectangle {
                                    visible: badgeCard.index !== 2
                                    anchors.right: badgeIcon.right
                                    anchors.top: badgeIcon.top
                                    anchors.rightMargin: -width * 0.2
                                    anchors.topMargin: -height * 0.2
                                    width: badgeCard.index === 1
                                        ? Math.round(badgeSample.width * 0.18)
                                        : Math.round(badgeSample.width * 0.38)
                                    height: width
                                    radius: width / 2
                                    color: Kirigami.Theme.highlightColor

                                    QQC2.Label {
                                        visible: badgeCard.index === 0
                                        anchors.centerIn: parent
                                        text: i18nc("@label sample badge count", "8")
                                        color: Kirigami.Theme.highlightedTextColor
                                        font.pixelSize: parent.height * 0.55
                                        font.bold: true
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
