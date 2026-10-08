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
    title: i18n("Panel Style")

    // Responsive vertical padding: scales with page width
    topPadding: Math.round(Kirigami.Units.gridUnit * Math.max(0.5, Math.min(1.5, width / 800)))
    bottomPadding: topPadding

    // Background style indices (BackgroundStyleType): 0 Panel Inherit,
    // 1 Transparent, 2 Tinted, 3 Acrylic.
    readonly property var styleNames: [
        i18n("Panel Inherit"),
        i18n("Transparent"),
        i18n("Tinted"),
        i18n("Acrylic")
    ]
    readonly property var styleDescriptions: [
        i18n("Matches the Plasma panel"),
        i18n("No background"),
        i18n("Custom color overlay"),
        i18n("Frosted glass blur")
    ]

    // Miniature dock drawn in one background style. Mirrors
    // computeBackgroundColor() (src/style/backgroundstyle.cpp): Header color,
    // or the Selection (accent) color when "Use accent color" is on, or the
    // custom tint for Tinted without "Use system color"; alpha is the
    // background opacity. Panel Inherit and Acrylic blur what is behind the
    // dock; Acrylic adds the dock's tint + noise shader.
    component StylePreview: Item {
        id: preview

        required property int styleIndex

        readonly property bool blurred: styleIndex === 0 || styleIndex === 3
        readonly property color baseColor: styleIndex === 2 && !DockSettings.useSystemColor
            ? DockSettings.tintColor
            : (DockSettings.useAccentColor ? accentColors.Kirigami.Theme.backgroundColor
                                           : headerColors.Kirigami.Theme.backgroundColor)
        readonly property color panelColor: Qt.alpha(baseColor, styleIndex === 1 ? 0.0 : DockSettings.backgroundOpacity)
        readonly property real iconSize: Math.round(Kirigami.Units.gridUnit * 1.2)
        readonly property real panelPadding: Kirigami.Units.smallSpacing

        Accessible.ignored: true

        Item {
            id: headerColors
            visible: false
            Kirigami.Theme.colorSet: Kirigami.Theme.Header
            Kirigami.Theme.inherit: false
        }

        Item {
            id: accentColors
            visible: false
            Kirigami.Theme.colorSet: Kirigami.Theme.Selection
            Kirigami.Theme.inherit: false
        }

        // Stand-in wallpaper with shapes behind the dock so transparency
        // and blur are visible.
        Rectangle {
            id: backdrop
            anchors.fill: parent
            radius: Kirigami.Units.cornerRadius
            clip: true
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Kirigami.Theme.highlightColor }
                GradientStop { position: 1.0; color: Kirigami.Theme.alternateBackgroundColor }
            }

            Rectangle {
                width: backdrop.height * 0.7
                height: width
                radius: width / 2
                x: backdrop.width * 0.12
                y: backdrop.height * 0.45
                color: Kirigami.Theme.positiveTextColor
            }

            Rectangle {
                width: backdrop.height * 0.5
                height: width
                radius: width / 2
                x: backdrop.width * 0.5
                y: backdrop.height * 0.15
                color: Kirigami.Theme.neutralTextColor
            }

            Rectangle {
                width: backdrop.height * 0.6
                height: width
                radius: width / 2
                x: backdrop.width * 0.68
                y: backdrop.height * 0.55
                color: Kirigami.Theme.negativeTextColor
            }
        }

        Item {
            id: dock
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Kirigami.Units.smallSpacing
            width: Math.min(parent.width - 2 * Kirigami.Units.smallSpacing, iconRow.implicitWidth + 2 * preview.panelPadding)
            height: preview.iconSize + 2 * preview.panelPadding

            // Corner radius scaled from the real dock (cornerRadius at iconSize).
            readonly property real radius: Math.min(height / 2,
                DockSettings.cornerRadius * preview.iconSize / Math.max(1, DockSettings.iconSize))

            ShaderEffectSource {
                id: backdropSource
                visible: false
                sourceItem: preview.blurred ? backdrop : null
                sourceRect: Qt.rect(dock.x, dock.y, dock.width, dock.height)
            }

            Rectangle {
                id: dockMask
                anchors.fill: parent
                visible: false
                radius: dock.radius
                color: Kirigami.Theme.textColor
                layer.enabled: preview.blurred
            }

            MultiEffect {
                anchors.fill: parent
                visible: preview.blurred
                source: backdropSource
                autoPaddingEnabled: false
                blurEnabled: true
                blur: 1.0
                blurMax: 24
                maskEnabled: true
                maskSource: dockMask
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1.0
            }

            Rectangle {
                anchors.fill: parent
                visible: preview.styleIndex !== 3
                radius: dock.radius
                color: preview.panelColor
                // Transparent has no background; outline the dock bounds faintly.
                border.width: preview.styleIndex === 1 ? 1 : 0
                border.color: Qt.alpha(Kirigami.Theme.textColor, 0.35)
            }

            // Acrylic: the dock's own tint + noise overlay (see main.qml).
            ShaderEffect {
                anchors.fill: parent
                visible: preview.styleIndex === 3
                property real tintR: preview.panelColor.r
                property real tintG: preview.panelColor.g
                property real tintB: preview.panelColor.b
                property real tintOpacity: preview.panelColor.a
                property real noiseStrength: 0.02
                property real resX: width
                property real resY: height
                property real cornerRadius: dock.radius
                fragmentShader: "qrc:/qml/shaders/acrylic_overlay.frag.qsb"
            }

            Row {
                id: iconRow
                anchors.centerIn: parent
                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: ["system-file-manager", "internet-web-browser", "utilities-terminal", "preferences-system"]

                    Kirigami.Icon {
                        required property string modelData
                        width: preview.iconSize
                        height: preview.iconSize
                        source: modelData
                    }
                }
            }
        }
    }

    // --- Style Card Picker ---
    FormCard.FormHeader {
        title: i18n("Background")
    }

    FormSection {
        FormCard.AbstractFormDelegate {
            id: stylePicker
            background: null
            // The cards are the focusable controls; the wrapper only groups them.
            focusPolicy: Qt.NoFocus
            Accessible.role: Accessible.Grouping
            Accessible.name: i18n("Style")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Style")
                    wrapMode: Text.Wrap
                    Accessible.ignored: true
                }

                GridLayout {
                    id: styleGrid
                    Layout.fillWidth: true
                    columns: Math.max(1, Math.min(4, Math.floor(width / (Kirigami.Units.gridUnit * 9))))
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.largeSpacing

                    Repeater {
                        model: 4

                        ChoiceCard {
                            id: styleCard
                            required property int index

                            Layout.fillWidth: true
                            text: page.styleNames[index]
                            description: page.styleDescriptions[index]
                            available: SettingsWindow.isStyleAvailable(index)
                            unavailableReason: i18n("Unavailable: not supported in this session")

                            Binding {
                                target: styleCard
                                property: "checked"
                                value: DockSettings.backgroundStyle === styleCard.index
                            }
                            onChosen: DockSettings.backgroundStyle = index

                            StylePreview {
                                anchors.fill: parent
                                styleIndex: styleCard.index
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Controls for the selected style (none for Transparent) ---
    FormSection {
        visible: DockSettings.backgroundStyle !== 1

        // Opacity (hidden for Transparent only)
        SliderDelegate {
            text: i18n("Opacity")
            from: 0; to: 100; stepSize: 5
            value: Math.round(DockSettings.backgroundOpacity * 100)
            valueText: i18nc("@label percentage", "%1%", value)
            onMoved: (value) => DockSettings.backgroundOpacity = value / 100
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.backgroundStyle === 2  // Tinted
        }

        // Use system color (Tinted only)
        FormCard.FormSwitchDelegate {
            visible: DockSettings.backgroundStyle === 2
            text: i18n("Use system color")
            description: i18n("Use the system header color instead of a custom tint color")
            checked: DockSettings.useSystemColor
            onToggled: DockSettings.useSystemColor = checked
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.backgroundStyle === 2 && !DockSettings.useSystemColor
        }

        // Tint color (Tinted with custom color)
        FormCard.AbstractFormDelegate {
            id: tintDelegate
            visible: DockSettings.backgroundStyle === 2 && !DockSettings.useSystemColor
            background: null
            // The swatch is the focusable, accessible control; clicking the
            // row opens the same dialog.
            focusPolicy: Qt.NoFocus
            Accessible.ignored: true
            onClicked: tintSwatch.clicked()
            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Tint color")
                    elide: Text.ElideRight
                    color: tintDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    Accessible.ignored: true
                }

                ColorSwatchButton {
                    id: tintSwatch
                    accessibleName: i18n("Tint color")
                    dialogTitle: i18n("Choose tint color")
                    color: DockSettings.tintColor
                    onPicked: (color) => DockSettings.tintColor = color.toString()
                }
            }
        }

        FormCard.FormDelegateSeparator {
            // Use accent color: Panel Inherit, Acrylic, or Tinted + system color
            visible: DockSettings.backgroundStyle !== 2 || DockSettings.useSystemColor
        }

        FormCard.FormSwitchDelegate {
            visible: DockSettings.backgroundStyle !== 1
                     && (DockSettings.backgroundStyle !== 2 || DockSettings.useSystemColor)
            text: i18n("Use accent color")
            description: i18n("Use the system accent color instead of the default panel color")
            checked: DockSettings.useAccentColor
            onToggled: DockSettings.useAccentColor = checked
        }
    }
}
