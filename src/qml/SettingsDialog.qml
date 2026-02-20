// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: settingsWindow

    title: i18n("Krema Settings")
    width: Kirigami.Units.gridUnit * 26
    height: Kirigami.Units.gridUnit * 22
    minimumWidth: Kirigami.Units.gridUnit * 20
    minimumHeight: Kirigami.Units.gridUnit * 16

    // Close window on Escape
    Shortcut {
        sequence: "Escape"
        onActivated: settingsWindow.close()
    }

    pageStack.initialPage: Kirigami.ScrollablePage {
        title: i18n("Settings")

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            // --- Appearance Section ---
            Kirigami.Heading {
                text: i18n("Appearance")
                level: 4
                Layout.fillWidth: true
            }

            Kirigami.Separator { Layout.fillWidth: true }

            GridLayout {
                columns: 2
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.smallSpacing
                Layout.fillWidth: true

                QQC2.Label { text: i18n("Icon size:") }
                QQC2.SpinBox {
                    Layout.fillWidth: false
                    from: 24; to: 96; stepSize: 4
                    value: dockSettings.iconSize
                    onValueModified: dockSettings.iconSize = value
                }

                QQC2.Label { text: i18n("Icon spacing:") }
                QQC2.SpinBox {
                    Layout.fillWidth: false
                    from: 0; to: 16
                    value: dockSettings.iconSpacing
                    onValueModified: dockSettings.iconSpacing = value
                }

                QQC2.Label { text: i18n("Zoom factor:") }
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    QQC2.Slider {
                        id: zoomSlider
                        Layout.fillWidth: true
                        from: 1.0; to: 2.0; stepSize: 0.1
                        value: dockSettings.maxZoomFactor
                        onMoved: dockSettings.maxZoomFactor = value
                    }
                    QQC2.Label {
                        text: zoomSlider.value.toFixed(1) + "x"
                        Layout.minimumWidth: Kirigami.Units.gridUnit * 2
                    }
                }

                QQC2.Label { text: i18n("Corner radius:") }
                QQC2.SpinBox {
                    Layout.fillWidth: false
                    from: 0; to: 24
                    value: dockSettings.cornerRadius
                    onValueModified: dockSettings.cornerRadius = value
                }

                QQC2.Label { text: i18n("Floating:") }
                QQC2.CheckBox {
                    checked: dockSettings.floating
                    onToggled: dockSettings.floating = checked
                }
            }

            // --- Behavior Section ---
            Item { height: Kirigami.Units.largeSpacing }

            Kirigami.Heading {
                text: i18n("Behavior")
                level: 4
                Layout.fillWidth: true
            }

            Kirigami.Separator { Layout.fillWidth: true }

            GridLayout {
                columns: 2
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.smallSpacing
                Layout.fillWidth: true

                QQC2.Label { text: i18n("Visibility mode:") }
                QQC2.ComboBox {
                    Layout.fillWidth: true
                    model: [
                        i18n("Always visible"),
                        i18n("Always hidden"),
                        i18n("Dodge windows"),
                        i18n("Smart hide")
                    ]
                    currentIndex: dockSettings.visibilityMode
                    onActivated: function(index) {
                        dockSettings.visibilityMode = index
                    }
                }

                QQC2.Label { text: i18n("Screen edge:") }
                QQC2.ComboBox {
                    Layout.fillWidth: true
                    model: [
                        i18n("Top"),
                        i18n("Bottom"),
                        i18n("Left"),
                        i18n("Right")
                    ]
                    currentIndex: dockSettings.edge
                    onActivated: function(index) {
                        dockSettings.edge = index
                    }
                }

                // Show/hide delay controls — only visible in hide-capable modes
                QQC2.Label {
                    text: i18n("Show delay (ms):")
                    visible: dockSettings.visibilityMode !== 0
                }
                QQC2.SpinBox {
                    Layout.fillWidth: false
                    from: 0; to: 2000; stepSize: 50
                    value: dockSettings.showDelay
                    onValueModified: dockSettings.showDelay = value
                    visible: dockSettings.visibilityMode !== 0
                }

                QQC2.Label {
                    text: i18n("Hide delay (ms):")
                    visible: dockSettings.visibilityMode !== 0
                }
                QQC2.SpinBox {
                    Layout.fillWidth: false
                    from: 0; to: 2000; stepSize: 50
                    value: dockSettings.hideDelay
                    onValueModified: dockSettings.hideDelay = value
                    visible: dockSettings.visibilityMode !== 0
                }
            }
        }
    }
}
