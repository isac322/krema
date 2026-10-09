// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Effects
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

SettingsPage {
    id: page

    title: i18n("Animations & Badges")
    subtitle: i18n("How apps demand attention and how notification badges appear on the dock")

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

    // Icon used by every sample on this page; mail naturally carries badges.
    readonly property string sampleIcon: "internet-mail"
    readonly property string sampleIconFallback: "applications-internet"

    // A real theme icon playing one attention animation with the same
    // timing and look as DockItem.qml. Set `x`/`y`/`width` (height follows
    // width) to the resting bounds of the icon it stands in for. `edge`
    // decides the bounce direction away from the screen edge (DockItem's
    // _bounceProp and _attentionBounceTarget; 14 px for a 48 px icon, scaled
    // to the sample size) and `baseOpacity` mirrors the window-state opacity
    // of the icon it covers. `dotBlinkOpacity` is the animated state of the
    // "Dot color" type — bind it to the sample's running indicator.
    component AttentionIconSample: Item {
        id: sample

        property int animationType: 0
        property bool playing: false
        // 0 Top, 1 Bottom, 2 Left, 3 Right (DockSettings.edge order).
        property int edge: 1
        property real baseOpacity: 1.0
        property string iconSource: page.sampleIcon
        property string iconFallback: page.sampleIconFallback
        // Blink animation state (DockItem._blinkOpacity).
        property real blinkOpacity: 1.0
        // Dot color animation state (DockItem._dotBlinkOpacity).
        property real dotBlinkOpacity: 1.0

        readonly property real bounceDistance: width * 14 / 48
        readonly property bool verticalBounce: edge <= 1

        height: width
        Accessible.ignored: true

        function reset() {
            bounceT.x = 0
            bounceT.y = 0
            rotation = 0
            scale = 1.0
            blinkOpacity = 1.0
            dotBlinkOpacity = 1.0
        }
        onPlayingChanged: if (!playing) reset()
        onAnimationTypeChanged: reset()

        transform: Translate { id: bounceT }

        Kirigami.Icon {
            id: iconImage
            anchors.fill: parent
            source: sample.iconSource
            fallback: sample.iconFallback
            opacity: sample.baseOpacity * sample.blinkOpacity
        }

        // Type 4: Glow — the MultiEffect shadow pulse of DockItem's
        // attentionGlow, anchored on the icon like there.
        MultiEffect {
            source: iconImage
            anchors.fill: iconImage
            paddingRect: Qt.rect(16, 16, 16, 16)
            visible: sample.animationType === 4
            shadowEnabled: true
            shadowColor: Kirigami.Theme.highlightColor
            shadowBlur: 0.7
            shadowScale: 1.12
            shadowHorizontalOffset: 0
            shadowVerticalOffset: 0
            shadowOpacity: 0

            SequentialAnimation on shadowOpacity {
                running: sample.playing && sample.animationType === 4
                loops: Animation.Infinite
                NumberAnimation { to: 0.85; duration: 800; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.15; duration: 800; easing.type: Easing.InOutSine }
            }
        }

        // Type 1: Bounce — vertical on the top and bottom edges, horizontal
        // on the left and right edges (DockItem._bounceProp).
        SequentialAnimation {
            running: sample.playing && sample.animationType === 1 && sample.verticalBounce
            loops: Animation.Infinite
            onStopped: bounceT.y = 0
            NumberAnimation { target: bounceT; property: "y"; to: (sample.edge === 0 ? 1 : -1) * sample.bounceDistance; duration: 300; easing.type: Easing.OutQuad }
            NumberAnimation { target: bounceT; property: "y"; to: 0; duration: 300; easing.type: Easing.InBounce }
            PauseAnimation { duration: 800 }
        }
        SequentialAnimation {
            running: sample.playing && sample.animationType === 1 && !sample.verticalBounce
            loops: Animation.Infinite
            onStopped: bounceT.x = 0
            NumberAnimation { target: bounceT; property: "x"; to: (sample.edge === 2 ? 1 : -1) * sample.bounceDistance; duration: 300; easing.type: Easing.OutQuad }
            NumberAnimation { target: bounceT; property: "x"; to: 0; duration: 300; easing.type: Easing.InBounce }
            PauseAnimation { duration: 800 }
        }

        // Type 2: Wiggle
        SequentialAnimation {
            running: sample.playing && sample.animationType === 2
            loops: Animation.Infinite
            onStopped: sample.rotation = 0
            NumberAnimation { target: sample; property: "rotation"; to: 5; duration: 80; easing.type: Easing.InOutSine }
            NumberAnimation { target: sample; property: "rotation"; to: -5; duration: 160; easing.type: Easing.InOutSine }
            NumberAnimation { target: sample; property: "rotation"; to: 3; duration: 120; easing.type: Easing.InOutSine }
            NumberAnimation { target: sample; property: "rotation"; to: -3; duration: 120; easing.type: Easing.InOutSine }
            NumberAnimation { target: sample; property: "rotation"; to: 1; duration: 100; easing.type: Easing.InOutSine }
            NumberAnimation { target: sample; property: "rotation"; to: -1; duration: 100; easing.type: Easing.InOutSine }
            NumberAnimation { target: sample; property: "rotation"; to: 0; duration: 80; easing.type: Easing.InOutSine }
            PauseAnimation { duration: 2000 }
        }

        // Type 3: Pulse
        SequentialAnimation {
            running: sample.playing && sample.animationType === 3
            loops: Animation.Infinite
            onStopped: sample.scale = 1.0
            NumberAnimation { target: sample; property: "scale"; to: 1.15; duration: 600; easing.type: Easing.InOutSine }
            NumberAnimation { target: sample; property: "scale"; to: 1.0; duration: 600; easing.type: Easing.InOutSine }
            PauseAnimation { duration: 400 }
        }

        // Type 5: Dot color — animates only the indicator state; the dot is
        // drawn by the tile or MiniDock the sample belongs to.
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

    // A dock strip on the miniature desktop of a picker tile: one rounded
    // panel at the bottom edge holding a single real icon and its running
    // indicator, playing `animationType` (AttentionIconSample). The panel
    // tint mirrors MiniDock's background color (Header color set, or the
    // accent color when enabled, at the configured opacity).
    component AttentionTile: DesktopStage {
        id: tile

        // Attention animation type played by the sample (DockItem codes).
        property int animationType: 0
        // Whether the sample animation may loop.
        property bool playing: false

        elevated: false
        radius: 0
        edge: 1
        active: page.windowActive
        // Cover the tile with the screen so the wallpaper fills it; the
        // strip is drawn at a readable size independent of the screen scale.
        unit: Math.max(width / screenSize.width, height / screenSize.height)
        Accessible.ignored: true

        Item {
            id: tileHeaderColors
            visible: false
            Kirigami.Theme.colorSet: Kirigami.Theme.Header
            Kirigami.Theme.inherit: false
        }
        Item {
            id: tileAccentColors
            visible: false
            Kirigami.Theme.colorSet: Kirigami.Theme.Selection
            Kirigami.Theme.inherit: false
        }

        // Panel strip sized like the dock: icon plus the paddings that
        // separate it from the screen edge.
        Rectangle {
            id: strip
            readonly property real pad: Kirigami.Units.smallSpacing * 1.5
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            width: tileSample.width + 2 * pad
            height: width
            radius: Math.min(height / 2, Kirigami.Units.cornerRadius * 2)
            color: Qt.alpha(DockSettings.useAccentColor
                            ? tileAccentColors.Kirigami.Theme.backgroundColor
                            : tileHeaderColors.Kirigami.Theme.backgroundColor,
                            DockSettings.backgroundStyle === 1 ? 0.0 : DockSettings.backgroundOpacity)
        }

        AttentionIconSample {
            id: tileSample
            anchors.horizontalCenter: strip.horizontalCenter
            anchors.top: strip.top
            anchors.topMargin: strip.pad
            width: Math.min(Kirigami.Units.iconSizes.medium, Math.max(16, tile.height * 0.4))
            edge: tile.edge
            animationType: tile.animationType
            playing: tile.playing && tile.active
        }

        // Running indicator dot under the icon; "Dot color" recolors and
        // blinks it like DockItem's indicator.
        Rectangle {
            anchors.horizontalCenter: strip.horizontalCenter
            anchors.bottom: strip.bottom
            anchors.bottomMargin: (strip.pad - height) / 2
            width: Math.max(2, Math.round(tileSample.width / 10))
            height: width
            radius: width / 2
            color: tileSample.animationType === 5 ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
            opacity: tileSample.animationType === 5 ? tileSample.dotBlinkOpacity : 0.8
        }
    }

    // --- Stage: the desktop with a live dock demanding attention ---

    stage: Component {
        DesktopStage {
            id: stage

            readonly property real coverUnit: Math.max(width / screenSize.width, height / screenSize.height)

            Layout.fillWidth: true
            active: page.windowActive
            // Zoomed in around the dock like the Icons stage, shrunk when a
            // tall dock would not fit along its edge.
            unit: Math.max(coverUnit, Math.min(framingUnit,
                0.85 * (dock.vertical ? height : width) / dock.restLengthReal))

            Accessible.role: Accessible.Graphic
            Accessible.name: i18n("Animations and badges preview")
            Accessible.description: i18n("Sample dock where one icon plays the selected attention animation and another carries the selected badge")

            MiniDock {
                id: dock
                unit: stage.unit
                backdrop: stage.backdrop
                active: stage.active
                // Konsole carries the badge (shown in the selected badge
                // display mode); the magnified middle icon — mail — plays
                // the attention animation through the overlay below.
                apps: [
                    { icon: "org.kde.dolphin", fallback: "system-file-manager", state: "inactive", badge: 0, padding: 1.0 },
                    { icon: "org.kde.konsole", fallback: "utilities-terminal", state: "inactive", badge: 8, padding: 1.0 },
                    { icon: "org.kde.kate", fallback: "accessories-text-editor", state: "minimized", badge: 0, padding: 0.82 },
                    { icon: page.sampleIcon, fallback: page.sampleIconFallback, state: "active", badge: 0, padding: 1.0 },
                    { icon: "firefox", fallback: "org.kde.falkon", state: "inactive", badge: 0, padding: 1.0 },
                    { icon: "systemsettings", fallback: "preferences-system", state: "launcher", badge: 0, padding: 0.88 }
                ]
                // The middle icon is magnified as if hovered, so the
                // attention animation plays at dock-zoom size.
                hoveredIndex: middleIndex
                // The overlay below is the only copy of this icon, so a
                // bounce or blink leaves nothing behind.
                hiddenIconIndex: attentionIcon.visible ? focusIndex : -1
            }

            // Sum of positions from `item` up to this item's parent (the
            // miniature screen) — the same live-positioning trick the
            // measurement overlay uses, keeping bindings alive while the
            // icon zooms.
            function screenPos(item) {
                let px = 0
                let py = 0
                let node = item
                while (node && node !== stage.desktop) {
                    px += node.x
                    py += node.y
                    node = node.parent
                }
                return Qt.point(px, py)
            }

            // Attention overlay: a copy of the focus icon, animated like
            // DockItem.qml for the selected type, covering the real icon.
            AttentionIconSample {
                id: attentionIcon

                readonly property Item focusIcon: dock.focusIcon
                readonly property point focusPos: focusIcon ? stage.screenPos(focusIcon) : Qt.point(0, 0)
                // MiniDock draws its icon at iconScale (and simulated
                // internal padding without normalization) inside the cell;
                // the overlay covers the visible icon, not the cell.
                readonly property real drawSize: {
                    if (!focusIcon) {
                        return 0
                    }
                    const padding = dock.iconNormalization ? 1.0 : (dock.apps[dock.focusIndex].padding ?? 1.0)
                    return focusIcon.width * dock.iconScale * padding
                }
                readonly property string focusState: dock.apps[dock.focusIndex].state ?? "launcher"

                visible: focusIcon !== null && drawSize > 0
                x: focusPos.x + (focusIcon ? (focusIcon.width - drawSize) / 2 : 0)
                y: focusPos.y + (focusIcon ? (focusIcon.height - drawSize) / 2 : 0)
                width: drawSize

                baseOpacity: focusState === "active" ? dock.opacityActive
                    : focusState === "minimized" ? dock.opacityMinimized
                    : dock.opacityInactive
                edge: dock.edge
                animationType: DockSettings.attentionAnimation
                playing: stage.active
            }

            // "Dot color" recolors the focus icon's running indicator: a
            // negative-colored dot blinking over the slot's own indicator,
            // mirrored from DockItem's indicator tint and MiniDock's dot
            // placement between the icon and the screen edge.
            Rectangle {
                id: attentionDot
                readonly property Item slot: dock.focusIcon ? dock.focusIcon.parent : null
                readonly property point slotPos: slot ? stage.screenPos(slot) : Qt.point(0, 0)
                readonly property real dotGap: (dock.padPx - width) / 2

                visible: slot !== null && DockSettings.attentionAnimation === 5
                width: Math.max(2, Math.round(dock.iconPx / 10))
                height: width
                radius: width / 2
                x: dock.vertical
                    ? slotPos.x + (dock.edge === 2 ? -dock.padPx + dotGap : (slot ? slot.width : 0) + dotGap)
                    : slotPos.x + (slot ? (slot.width - width) / 2 : 0)
                y: dock.vertical
                    ? slotPos.y + (slot ? (slot.height - height) / 2 : 0)
                    : slotPos.y + (dock.edge === 0 ? -dock.padPx + dotGap : (slot ? slot.height : 0) + dotGap)
                color: Kirigami.Theme.negativeTextColor
                opacity: attentionIcon.dotBlinkOpacity
            }
        }
    }

    // --- Attention ---
    FormCard.FormHeader {
        title: i18n("Attention")
    }

    FormCard.FormCard {
        FormCard.AbstractFormDelegate {
            id: attentionPicker
            background: null
            focusPolicy: Qt.NoFocus
            activeFocusOnTab: false
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
                    color: page.secondaryTextColor
                    wrapMode: Text.Wrap
                    Accessible.ignored: true
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.largeSpacing
                    columns: Math.max(1, Math.floor(width / page.pickerColumnWidth))
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.largeSpacing

                    Repeater {
                        model: page.attentionOptions

                        ChoiceCard {
                            id: attentionCard

                            required property int index
                            required property string modelData

                            Layout.fillWidth: true
                            implicitWidth: page.pickerColumnWidth - Kirigami.Units.smallSpacing
                            text: modelData
                            previewHeight: Kirigami.Units.gridUnit * 4.5

                            Binding {
                                target: attentionCard
                                property: "checked"
                                value: DockSettings.attentionAnimation === attentionCard.index
                            }
                            onChosen: DockSettings.attentionAnimation = attentionCard.index

                            // Miniature desktop: the real wallpaper with a
                            // dock strip whose icon loops this animation —
                            // while the card is hovered or selected, never
                            // when the window is hidden.
                            AttentionTile {
                                anchors.fill: parent
                                animationType: attentionCard.index
                                playing: attentionCard.checked || attentionCard.hovered || attentionCard.activeFocus
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

    FormCard.FormCard {
        FormCard.AbstractFormDelegate {
            background: null
            focusPolicy: Qt.NoFocus
            activeFocusOnTab: false
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
                    color: page.secondaryTextColor
                    wrapMode: Text.Wrap
                    Accessible.ignored: true
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.largeSpacing
                    columns: Math.max(1, Math.min(3, Math.floor(width / page.pickerColumnWidth)))
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.largeSpacing

                    Repeater {
                        model: page.badgeOptions

                        ChoiceCard {
                            id: badgeCard

                            required property int index
                            required property string modelData

                            Layout.fillWidth: true
                            implicitWidth: page.pickerColumnWidth - Kirigami.Units.smallSpacing
                            text: modelData
                            previewHeight: Kirigami.Units.gridUnit * 4.5

                            Binding {
                                target: badgeCard
                                property: "checked"
                                value: DockSettings.badgeDisplayMode === badgeCard.index
                            }
                            onChosen: DockSettings.badgeDisplayMode = badgeCard.index

                            // Miniature desktop with a dock strip; the icon
                            // wears this badge style, sized and offset like
                            // DockItem's badge (38% pill, 18% dot, 20% bleed
                            // past the icon's top-right corner).
                            DesktopStage {
                                id: badgeTile
                                anchors.fill: parent
                                elevated: false
                                radius: 0
                                edge: 1
                                active: page.windowActive
                                unit: Math.max(width / screenSize.width, height / screenSize.height)
                                Accessible.ignored: true

                                Item {
                                    id: badgeHeaderColors
                                    visible: false
                                    Kirigami.Theme.colorSet: Kirigami.Theme.Header
                                    Kirigami.Theme.inherit: false
                                }
                                Item {
                                    id: badgeAccentColors
                                    visible: false
                                    Kirigami.Theme.colorSet: Kirigami.Theme.Selection
                                    Kirigami.Theme.inherit: false
                                }

                                Rectangle {
                                    id: badgeStrip
                                    readonly property real pad: Kirigami.Units.smallSpacing * 1.5
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.bottom: parent.bottom
                                    width: badgeIcon.width + 2 * pad
                                    height: width
                                    radius: Math.min(height / 2, Kirigami.Units.cornerRadius * 2)
                                    color: Qt.alpha(DockSettings.useAccentColor
                                                    ? badgeAccentColors.Kirigami.Theme.backgroundColor
                                                    : badgeHeaderColors.Kirigami.Theme.backgroundColor,
                                                    DockSettings.backgroundStyle === 1 ? 0.0 : DockSettings.backgroundOpacity)
                                }

                                Kirigami.Icon {
                                    id: badgeIcon
                                    anchors.horizontalCenter: badgeStrip.horizontalCenter
                                    anchors.top: badgeStrip.top
                                    anchors.topMargin: badgeStrip.pad
                                    width: Math.min(Kirigami.Units.iconSizes.medium, Math.max(16, badgeTile.height * 0.4))
                                    height: width
                                    source: page.sampleIcon
                                    fallback: page.sampleIconFallback
                                }

                                // Running indicator dot (a running app).
                                Rectangle {
                                    anchors.horizontalCenter: badgeStrip.horizontalCenter
                                    anchors.bottom: badgeStrip.bottom
                                    anchors.bottomMargin: (badgeStrip.pad - height) / 2
                                    width: Math.max(2, Math.round(badgeIcon.width / 10))
                                    height: width
                                    radius: width / 2
                                    color: Kirigami.Theme.textColor
                                    opacity: 0.8
                                }

                                Rectangle {
                                    visible: badgeCard.index !== 2
                                    anchors.right: badgeIcon.right
                                    anchors.top: badgeIcon.top
                                    anchors.rightMargin: -width * 0.2
                                    anchors.topMargin: -height * 0.2
                                    width: badgeCard.index === 1
                                        ? Math.round(badgeIcon.width * 0.18)
                                        : Math.round(badgeIcon.width * 0.38)
                                    height: width
                                    radius: width / 2
                                    color: Kirigami.Theme.highlightColor

                                    layer.enabled: badgeCard.index === 0
                                    layer.effect: MultiEffect {
                                        shadowEnabled: true
                                        shadowColor: Qt.alpha("black", 0.4)
                                        shadowBlur: 0.3
                                        shadowVerticalOffset: 1
                                    }

                                    QQC2.Label {
                                        visible: badgeCard.index === 0
                                        anchors.centerIn: parent
                                        width: parent.width - 2
                                        height: parent.height - 2
                                        text: i18nc("@label sample badge count", "8")
                                        color: Kirigami.Theme.highlightedTextColor
                                        font.pixelSize: Math.max(1, parent.height * 0.55)
                                        font.bold: true
                                        fontSizeMode: Text.Fit
                                        minimumPixelSize: 4
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        Accessible.ignored: true
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
