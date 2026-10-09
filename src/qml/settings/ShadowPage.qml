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
    title: i18n("Shadow")
    subtitle: i18n("Where the light sits decides where the dock's shadow falls and how soft it is")

    // Clamps to the kcfg range (the 3D scene has its own copy of the setters).
    function setElevation(e) {
        DockSettings.shadowElevation = Math.max(1, Math.min(50, Math.round(e)))
    }

    // The dock's own projective shadow shader (see main.qml dockShadow), drawn
    // around a panel of panelWidth x panelHeight centered in this item.
    // Uniforms are real pixels; scale the item to fit a miniature.
    component ProjectedShadow: ShaderEffect {
        property real panelWidth
        property real panelHeight
        property real cornerRadius
        property real elevation: DockSettings.shadowElevation
        property real lightX: DockSettings.shadowLightX
        property real lightY: DockSettings.shadowLightY
        property real lightZ: DockSettings.shadowLightZ
        property real lightRadius: DockSettings.shadowLightRadius
        property color _shadowColor: DockSettings.shadowColor
        property real shadowR: _shadowColor.r
        property real shadowG: _shadowColor.g
        property real shadowB: _shadowColor.b
        property real shadowA: DockSettings.shadowIntensity
        property real margin: {
            const denom = Math.max(lightZ - elevation, 1)
            const lightDist = Math.sqrt(lightX * lightX + lightY * lightY)
            const physicalMargin = (lightDist + lightRadius) * elevation / denom
            const softnessMargin = lightRadius * 3.0
            return Math.min(Math.max(physicalMargin, softnessMargin) + 10, 200)
        }

        width: panelWidth + margin * 2
        height: panelHeight + margin * 2
        fragmentShader: "qrc:/qml/shaders/outer_shadow.frag.qsb"
        Accessible.ignored: true
    }

    // --- Stage: the 3D light scene with the real shadow as an inset ---
    // Only loaded while the shadow is on (no scene, no GPU work otherwise).

    stage: DockSettings.shadowEnabled ? shadowStage : null

    readonly property Component shadowStage: Component {
        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Item {
                id: sceneFrame
                readonly property real radius: Kirigami.Units.cornerRadius * 3

                Layout.fillWidth: true
                Layout.preferredHeight: Kirigami.Units.gridUnit * 17

                // Same soft offset drop shadow as DesktopStage.
                Kirigami.ShadowedRectangle {
                    anchors.fill: parent
                    radius: sceneFrame.radius
                    color: Kirigami.Theme.alternateBackgroundColor
                    shadow.size: Kirigami.Units.gridUnit * 0.75
                    shadow.yOffset: 2
                    shadow.color: Qt.alpha(Qt.darker(Kirigami.Theme.backgroundColor, 4.0), 0.35)
                }

                Item {
                    id: sceneClip
                    anchors.fill: parent
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: sceneMask
                        maskThresholdMin: 0.5
                        maskSpreadAtMin: 1.0
                    }

                    ShadowScene3D {
                        anchors.fill: parent
                        active: page.windowActive
                    }
                }

                Rectangle {
                    id: sceneMask
                    anchors.fill: parent
                    radius: sceneFrame.radius
                    visible: false
                    layer.enabled: true
                }
                // Result card: the dock's real projective shadow under a
                // miniature dock on the user's wallpaper. Bottom-right, so it
                // leaves the light marker and the drag targets free.
                DesktopStage {
                    id: inset

                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.margins: Kirigami.Units.largeSpacing
                    width: Math.round(Math.max(Kirigami.Units.gridUnit * 7,
                                               Math.min(Kirigami.Units.gridUnit * 11, sceneFrame.width * 0.28)))
                    height: Math.round(width * 0.42)
                    // Card chrome: hairline + soft shadow.
                    elevated: true
                    radius: Kirigami.Units.cornerRadius * 2
                    magnification: 1.5
                    active: page.windowActive

                    Accessible.role: Accessible.Graphic
                    Accessible.name: i18n("Shadow preview")
                    Accessible.description: i18n("The shadow the real dock casts on the desktop")

                    MiniDock {
                        id: insetDock

                        // Real-pixel resting length of the dock, then the
                        // preview scale that fits it into the inset.
                        readonly property real realLength: count * DockSettings.iconSize
                            + Math.max(0, count - 1) * DockSettings.iconSpacing
                            + 2 * Kirigami.Units.largeSpacing

                        unit: 0.8 * (vertical ? inset.height : inset.width) / Math.max(1, realLength)
                        backdrop: inset.backdrop
                        active: inset.active
                        maxZoomFactor: 1.0

                        // Under the panel (z below the background), over the
                        // wallpaper. Centered on the panel and scaled from
                        // real pixels, so it is the shadow the dock draws.
                        ProjectedShadow {
                            z: -1
                            panelWidth: insetDock.panelItem.width / Math.max(0.001, insetDock.unit)
                            panelHeight: insetDock.panelItem.height / Math.max(0.001, insetDock.unit)
                            cornerRadius: Math.min(DockSettings.cornerRadius, panelHeight / 2)
                            scale: insetDock.unit
                            x: insetDock.panelItem.x + insetDock.panelItem.width / 2 - width / 2
                            y: insetDock.panelItem.y + insetDock.panelItem.height / 2 - height / 2
                        }
                    }
                }

                // "Result" caption chip on the card's top edge.
                Rectangle {
                    x: inset.x + Kirigami.Units.smallSpacing
                    y: inset.y - height / 2
                    width: resultLabel.implicitWidth + Kirigami.Units.smallSpacing * 2
                    height: resultLabel.implicitHeight + Kirigami.Units.smallSpacing * 0.5
                    radius: height / 2
                    color: Kirigami.Theme.backgroundColor
                    border.width: 1
                    border.color: Qt.alpha(Kirigami.Theme.textColor, 0.2)

                    QQC2.Label {
                        id: resultLabel
                        anchors.centerIn: parent
                        text: i18nc("@label chip on the real-shadow inset", "Result")
                        font: Kirigami.Theme.smallFont
                        color: page.secondaryTextColor
                        Accessible.ignored: true
                    }
                }

                // Hairline edge of the stage.
                Rectangle {
                    anchors.fill: parent
                    radius: sceneFrame.radius
                    color: Qt.alpha(Kirigami.Theme.backgroundColor, 0)
                    border.width: 1
                    border.color: Qt.alpha(Kirigami.Theme.textColor, 0.15)
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Drag the light to move it, scroll for its height, Shift+scroll for softness, and drag the dock for its elevation.")
                wrapMode: Text.Wrap
                font: Kirigami.Theme.smallFont
                color: page.secondaryTextColor
                Accessible.ignored: true
            }
        }
    }

    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18n("Enable shadow")
            checked: DockSettings.shadowEnabled
            onToggled: DockSettings.shadowEnabled = checked
        }
    }

    // --- Shadow ---
    FormCard.FormHeader {
        title: i18n("Shadow")
        visible: DockSettings.shadowEnabled
    }

    FormCard.FormCard {
        visible: DockSettings.shadowEnabled

        SliderDelegate {
            text: i18n("Shadow intensity")
            from: 0.0; to: 1.0; stepSize: 0.05
            value: DockSettings.shadowIntensity
            valueText: i18nc("@label percentage", "%1%", Math.round(value * 100))
            onMoved: (value) => DockSettings.shadowIntensity = value
        }

        FormCard.FormDelegateSeparator {}

        FormCard.AbstractFormDelegate {
            id: shadowColorDelegate
            background: null
            // The swatch is the focusable, accessible control; clicking the
            // row opens the same dialog.
            focusPolicy: Qt.NoFocus
            Accessible.ignored: true
            onClicked: shadowSwatch.clicked()
            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Shadow color")
                    elide: Text.ElideRight
                    color: shadowColorDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    Accessible.ignored: true
                }

                ColorSwatchButton {
                    id: shadowSwatch
                    accessibleName: i18n("Shadow color")
                    dialogTitle: i18n("Choose shadow color")
                    showAlphaChannel: true
                    color: DockSettings.shadowColor
                    onPicked: (color) => DockSettings.shadowColor = color.toString()
                }
            }
        }
    }

    // --- Advanced settings (progressive disclosure: exact values) ---
    FormCard.FormCard {
        visible: DockSettings.shadowEnabled

        FormCard.AbstractFormDelegate {
            id: advancedToggle
            property bool expanded: false

            text: i18n("Advanced settings")
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.Button
            Accessible.name: text
            Accessible.description: expanded ? i18nc("@info:status expander", "Expanded")
                                             : i18nc("@info:status expander", "Collapsed")
            onClicked: expanded = !expanded
            Keys.onReturnPressed: expanded = !expanded
            Keys.onEnterPressed: expanded = !expanded

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: advancedToggle.text
                        color: Kirigami.Theme.textColor
                        Accessible.ignored: true
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Exact values for the light position, height, radius and the panel elevation")
                        wrapMode: Text.Wrap
                        font: Kirigami.Theme.smallFont
                        color: page.secondaryTextColor
                        Accessible.ignored: true
                    }
                }
                Kirigami.Icon {
                    source: advancedToggle.expanded ? "arrow-up" : "arrow-down"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                    Accessible.ignored: true
                }
            }
        }

        FormCard.FormDelegateSeparator { visible: advancedToggle.expanded }

        SliderDelegate {
            visible: advancedToggle.expanded
            text: i18n("Light X")
            from: -300; to: 300; stepSize: 10
            value: DockSettings.shadowLightX
            onMoved: (value) => DockSettings.shadowLightX = value
        }

        FormCard.FormDelegateSeparator { visible: advancedToggle.expanded }

        SliderDelegate {
            visible: advancedToggle.expanded
            text: i18n("Light Y")
            from: -300; to: 300; stepSize: 10
            value: DockSettings.shadowLightY
            onMoved: (value) => DockSettings.shadowLightY = value
        }

        FormCard.FormDelegateSeparator { visible: advancedToggle.expanded }

        SliderDelegate {
            visible: advancedToggle.expanded
            text: i18n("Light height")
            from: 100; to: 2000; stepSize: 50
            value: DockSettings.shadowLightZ
            onMoved: (value) => DockSettings.shadowLightZ = value
        }

        FormCard.FormDelegateSeparator { visible: advancedToggle.expanded }

        SliderDelegate {
            visible: advancedToggle.expanded
            text: i18n("Light radius")
            from: 0.5; to: 20.0; stepSize: 0.5
            value: DockSettings.shadowLightRadius
            valueText: value.toFixed(1)
            onMoved: (value) => DockSettings.shadowLightRadius = value
        }

        FormCard.FormDelegateSeparator { visible: advancedToggle.expanded }

        SliderDelegate {
            visible: advancedToggle.expanded
            text: i18n("Panel elevation")
            from: 1; to: 50; stepSize: 1
            value: DockSettings.shadowElevation
            onMoved: (value) => page.setElevation(value)
        }
    }
}
