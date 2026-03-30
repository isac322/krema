// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

FormCard.FormCardPage {
    title: i18n("Shadow")

    // Responsive vertical padding: scales with page width
    topPadding: Math.round(Kirigami.Units.gridUnit * Math.max(0.5, Math.min(1.5, width / 800)))
    bottomPadding: topPadding

    FormSection {
        FormCard.FormSwitchDelegate {
            text: i18n("Enable shadow")
            checked: DockSettings.shadowEnabled
            onToggled: DockSettings.shadowEnabled = checked
        }
    }

    // --- 2D Light Source Canvas ---
    FormCard.FormHeader {
        title: i18n("Light & Shadow")
        visible: DockSettings.shadowEnabled
    }

    FormSection {
        visible: DockSettings.shadowEnabled

        FormCard.AbstractFormDelegate {
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                    // Top-down canvas: light source + dock
                    Rectangle {
                        id: canvasContainer
                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 14
                        color: Kirigami.Theme.alternateBackgroundColor
                        radius: Kirigami.Units.largeSpacing
                        clip: true

                        // Canvas coordinate system:
                        // Center = (0,0) maps to Light X=0, Y=0
                        // Light X: -300..300 maps to canvas left..right
                        // Light Y: -300..300 maps to canvas top..bottom
                        property real canvasCenterX: width / 2
                        property real canvasCenterY: height / 2
                        property real scaleX: (width - 40) / 600  // 600 = range of -300..300
                        property real scaleY: (height - 40) / 600

                        // Dock rectangle (centered, bottom half)
                        Rectangle {
                            id: dockRect
                            width: parent.width * 0.35
                            height: 12
                            x: (parent.width - width) / 2
                            y: parent.height * 0.65
                            radius: DockSettings.cornerRadius * 0.2
                            color: Kirigami.Theme.highlightColor
                            opacity: 0.7

                            QQC2.Label {
                                anchors.centerIn: parent
                                text: i18n("Dock")
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                color: Kirigami.Theme.highlightedTextColor
                            }
                        }

                        // Shadow projection preview (simplified)
                        Rectangle {
                            id: shadowProjection
                            width: dockRect.width * 1.1
                            height: dockRect.height + 4
                            radius: dockRect.radius
                            color: DockSettings.shadowColor
                            opacity: DockSettings.shadowIntensity * 0.5

                            // Shadow offset from light position
                            property real offsetX: -DockSettings.shadowLightX * 0.05 * DockSettings.shadowElevation * 0.1
                            property real offsetY: -DockSettings.shadowLightY * 0.05 * DockSettings.shadowElevation * 0.1

                            x: dockRect.x + (dockRect.width - width) / 2 + offsetX
                            y: dockRect.y + 2 + Math.abs(offsetY) * 0.5 + offsetY

                            Behavior on x { NumberAnimation { duration: 50 } }
                            Behavior on y { NumberAnimation { duration: 50 } }
                        }

                        // Light source (draggable)
                        Rectangle {
                            id: lightSource
                            property real lightRadius: Math.max(12, 8 + DockSettings.shadowLightRadius * 1.5)

                            width: lightRadius * 2
                            height: lightRadius * 2
                            radius: lightRadius
                            color: "#FFD700"
                            opacity: 0.8
                            border.color: "#FFA500"
                            border.width: 2

                            // Position from settings
                            x: canvasContainer.canvasCenterX + DockSettings.shadowLightX * canvasContainer.scaleX - lightRadius
                            y: canvasContainer.canvasCenterY + DockSettings.shadowLightY * canvasContainer.scaleY - lightRadius

                            // Z indicator: inner circle size
                            Rectangle {
                                anchors.centerIn: parent
                                width: Math.max(6, parent.width * (1.0 - DockSettings.shadowLightZ / 2000))
                                height: width
                                radius: width / 2
                                color: "#FFFFFF"
                                opacity: 0.6
                            }

                            // Sun icon
                            QQC2.Label {
                                anchors.centerIn: parent
                                text: "☀"
                                font.pointSize: Math.max(8, lightSource.lightRadius * 0.6)
                            }

                            MouseArea {
                                id: lightDrag
                                anchors.fill: parent
                                anchors.margins: -8 // easier to grab
                                cursorShape: Qt.OpenHandCursor
                                drag.target: parent
                                drag.minimumX: -lightSource.lightRadius
                                drag.minimumY: -lightSource.lightRadius
                                drag.maximumX: canvasContainer.width - lightSource.lightRadius
                                drag.maximumY: canvasContainer.height - lightSource.lightRadius

                                onPositionChanged: {
                                    if (drag.active) {
                                        let cx = lightSource.x + lightSource.lightRadius
                                        let cy = lightSource.y + lightSource.lightRadius
                                        DockSettings.shadowLightX = Math.round((cx - canvasContainer.canvasCenterX) / canvasContainer.scaleX)
                                        DockSettings.shadowLightY = Math.round((cy - canvasContainer.canvasCenterY) / canvasContainer.scaleY)
                                    }
                                }
                                onPressed: cursorShape = Qt.ClosedHandCursor
                                onReleased: cursorShape = Qt.OpenHandCursor
                            }
                        }

                        // Axis labels
                        QQC2.Label {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottomMargin: 2
                            text: i18n("Drag the light source to adjust shadow direction")
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            color: Kirigami.Theme.disabledTextColor
                        }
                    }

                    // Shadow result preview
                    Rectangle {
                        id: shadowPreview
                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 4
                        color: Kirigami.Theme.alternateBackgroundColor
                        radius: Kirigami.Units.largeSpacing

                        QQC2.Label {
                            anchors.centerIn: parent
                            text: i18n("Shadow Preview")
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            color: Kirigami.Theme.disabledTextColor
                        }

                        // Simplified dock with shadow
                        Rectangle {
                            id: previewDock
                            anchors.centerIn: parent
                            width: parent.width * 0.5
                            height: Kirigami.Units.gridUnit * 1.5
                            radius: DockSettings.cornerRadius * 0.3
                            color: Kirigami.Theme.backgroundColor

                            Row {
                                anchors.centerIn: parent
                                spacing: 4
                                Repeater {
                                    model: 5
                                    Rectangle { width: 10; height: 10; radius: 2; color: Kirigami.Theme.textColor; opacity: 0.4 }
                                }
                            }
                        }

                        // Shadow under preview dock
                        Rectangle {
                            id: shadowUnder
                            width: previewDock.width * 1.05
                            height: previewDock.height + 2
                            radius: previewDock.radius
                            color: DockSettings.shadowColor
                            opacity: DockSettings.shadowIntensity

                            x: previewDock.x + (-DockSettings.shadowLightX * 0.02 * DockSettings.shadowElevation * 0.05)
                            y: previewDock.y + 3 + (DockSettings.shadowElevation * 0.1)
                            z: previewDock.z - 1

                            // Blur approximation via scale + opacity
                            property real blurFactor: DockSettings.shadowLightRadius / 10.0
                            transform: Scale {
                                xScale: 1.0 + shadowUnder.blurFactor * 0.05
                                yScale: 1.0 + shadowUnder.blurFactor * 0.1
                                origin.x: shadowUnder.width / 2
                                origin.y: 0
                            }
                        }
                    }
                }
            }
        }

    // --- Advanced Settings (expandable) ---
    FormCard.FormHeader {
        title: i18n("Fine Tuning")
        visible: DockSettings.shadowEnabled
    }

    FormSection {
        visible: DockSettings.shadowEnabled

        // Collapsible section
        FormCard.AbstractFormDelegate {
            id: advancedToggle
            background: null
            property bool expanded: false
            contentItem: RowLayout {
                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Advanced controls")
                    color: Kirigami.Theme.textColor
                }
                Kirigami.Icon {
                    source: advancedToggle.expanded ? "arrow-up" : "arrow-down"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                }
            }
            onClicked: expanded = !expanded
        }

        FormCard.FormDelegateSeparator { visible: advancedToggle.expanded }

        // Light Z (height)
        FormCard.AbstractFormDelegate {
            visible: advancedToggle.expanded
            Accessible.name: i18n("Light height")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label { Layout.fillWidth: true; text: i18n("Light height (Z)") }
                    QQC2.Label { text: lightZSlider.value; color: Kirigami.Theme.disabledTextColor }
                }
                QQC2.Slider {
                    id: lightZSlider
                    Layout.fillWidth: true
                    from: 100; to: 2000; stepSize: 20
                    value: DockSettings.shadowLightZ
                    onMoved: DockSettings.shadowLightZ = value
                }
            }
        }

        FormCard.FormDelegateSeparator { visible: advancedToggle.expanded }

        // Light Radius
        FormCard.AbstractFormDelegate {
            visible: advancedToggle.expanded
            Accessible.name: i18n("Light radius")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label { Layout.fillWidth: true; text: i18n("Light radius") }
                    QQC2.Label { text: lightRadiusSlider.value.toFixed(1); color: Kirigami.Theme.disabledTextColor }
                }
                QQC2.Slider {
                    id: lightRadiusSlider
                    Layout.fillWidth: true
                    from: 0.5; to: 20.0; stepSize: 0.5
                    value: DockSettings.shadowLightRadius
                    onMoved: DockSettings.shadowLightRadius = value
                }
            }
        }

        FormCard.FormDelegateSeparator { visible: advancedToggle.expanded }

        // Elevation
        FormCard.AbstractFormDelegate {
            visible: advancedToggle.expanded
            Accessible.name: i18n("Elevation")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label { Layout.fillWidth: true; text: i18n("Elevation") }
                    QQC2.Label { text: elevationSlider.value; color: Kirigami.Theme.disabledTextColor }
                }
                QQC2.Slider {
                    id: elevationSlider
                    Layout.fillWidth: true
                    from: 1; to: 50; stepSize: 1
                    value: DockSettings.shadowElevation
                    onMoved: DockSettings.shadowElevation = value
                }
            }
        }

        FormCard.FormDelegateSeparator { visible: advancedToggle.expanded }

        // Intensity
        FormCard.AbstractFormDelegate {
            visible: advancedToggle.expanded
            Accessible.name: i18n("Shadow intensity")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label { Layout.fillWidth: true; text: i18n("Shadow intensity") }
                    QQC2.Label { text: Math.round(intensitySlider.value * 100) + "%"; color: Kirigami.Theme.disabledTextColor }
                }
                QQC2.Slider {
                    id: intensitySlider
                    Layout.fillWidth: true
                    from: 0.0; to: 1.0; stepSize: 0.05
                    value: DockSettings.shadowIntensity
                    onMoved: DockSettings.shadowIntensity = value
                }
            }
        }

        FormCard.FormDelegateSeparator { visible: advancedToggle.expanded }

        // Shadow Color
        FormCard.AbstractFormDelegate {
            visible: advancedToggle.expanded
            background: null
            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing
                QQC2.Label { Layout.fillWidth: true; text: i18n("Shadow color") }
                Rectangle {
                    width: Kirigami.Units.gridUnit * 2
                    height: Kirigami.Units.gridUnit * 1.5
                    radius: Kirigami.Units.smallSpacing
                    color: DockSettings.shadowColor
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: shadowColorDialog.open()
                    }
                }
            }
        }

        ColorDialog {
            id: shadowColorDialog
            title: i18n("Choose shadow color")
            selectedColor: DockSettings.shadowColor
            onAccepted: DockSettings.shadowColor = selectedColor
        }
    }
}
