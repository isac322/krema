// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

SettingsPage {
    id: page
    title: i18n("Panel Style")
    subtitle: i18n("Background style, opacity and tint of the dock panel")

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

    // --- Stage: the desktop with the dock in its current style ---

    stage: Component {
        Item {
            implicitHeight: stage.implicitHeight

            DesktopStage {
                id: stage

                readonly property real coverUnit: Math.max(width / screenSize.width, height / screenSize.height)

                anchors.fill: parent
                active: page.windowActive
                // Zoomed in a little around the dock, shrunk when a large
                // dock would not fit along its edge.
                unit: Math.max(coverUnit, Math.min(framingUnit,
                    0.85 * (dock.vertical ? height : width) / dock.restLengthReal))

                Accessible.role: Accessible.Graphic
                Accessible.name: i18n("Dock preview")
                Accessible.description: i18n("Sample dock showing the panel background style, opacity and tint")

                MiniDock {
                    id: dock
                    unit: stage.unit
                    edge: stage.edge
                    backdrop: stage.backdrop
                    active: stage.active
                }
            }
        }
    }

    // Picker thumbnail: the desktop cropped around a dock drawn in one
    // background style. MiniDock renders Panel Inherit and Acrylic as
    // blurred wallpaper under a tinted panel, so `backdrop` is required.
    component StyleTile: DesktopStage {
        id: tile

        property int styleIndex: 0

        readonly property real coverUnit: Math.max(width / screenSize.width, height / screenSize.height)

        anchors.fill: parent
        elevated: false
        radius: 0
        active: page.windowActive
        // Fit the resting dock along the edge and the zoomed icons across it.
        unit: Math.max(coverUnit, Math.min(
            0.85 * (styleDock.vertical ? height : width) / styleDock.restLengthReal,
            0.7 * (styleDock.vertical ? width : height)
                / (styleDock.iconSize * Math.max(1.0, styleDock.maxZoomFactor) + 2 * Kirigami.Units.largeSpacing + 8)))
        Accessible.ignored: true

        MiniDock {
            id: styleDock
            unit: tile.unit
            edge: tile.edge
            backdrop: tile.backdrop
            active: tile.active
            backgroundStyle: tile.styleIndex
        }
    }

    // --- Style card picker ---
    FormCard.FormHeader {
        title: i18n("Background")
    }

    FormCard.FormCard {
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
                    color: stylePicker.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    Accessible.ignored: true
                }

                GridLayout {
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
                            previewHeight: Kirigami.Units.gridUnit * 4.5

                            Binding {
                                target: styleCard
                                property: "checked"
                                value: DockSettings.backgroundStyle === styleCard.index
                            }
                            onChosen: DockSettings.backgroundStyle = index

                            StyleTile {
                                styleIndex: styleCard.index
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Controls for the selected style (none for Transparent) ---
    FormCard.FormCard {
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
