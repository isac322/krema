// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0

// Import our custom UI Kit
import "../components"

QQC2.ScrollView {
    id: shadowPage
    contentWidth: availableWidth
    clip: true

    // Let the ScrollView handle margins natively to prevent clipping
    topPadding: 16
    bottomPadding: 32 // Premium breathing room at the bottom
    leftPadding: 16
    rightPadding: 16

    ColumnLayout {
        // ONLY fill the width, let the height stretch natively
        width: parent.width
        spacing: 32

        // --- SECTION 1: MASTER SWITCH ---
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            
            QQC2.Label { 
                text: i18n("Master Control")
                color: theme.textDim // 50% opacity Krema Accent
                font.bold: true
                font.letterSpacing: 1.1
                font.pixelSize: 12
                Layout.leftMargin: 8
            }
            
            KremaCard {
                KremaSwitch {
                    Layout.fillWidth: true
                    text: i18n("Enable High-Fidelity Shadows")
                    checked: DockSettings.shadowEnabled
		    onToggled: {
                             DockSettings.shadowEnabled = checked;
                             DockSettings.save();
                         }
                }
            }
        }

        // --- SECTION 2: LIGHT PHYSICS ---
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: DockSettings.shadowEnabled
            
            QQC2.Label { 
                text: i18n("Light Source Physics")
                color: theme.textDim
                font.bold: true
                font.letterSpacing: 1.1
                font.pixelSize: 12
                Layout.leftMargin: 8
            }
            
            KremaCard {
                // LIGHT X
                ColumnLayout {
                    Layout.fillWidth: true
                    RowLayout {
                        QQC2.Label { Layout.fillWidth: true; text: i18n("Light X (Horizontal offset)"); color: theme.text; font.bold: true }
                        QQC2.Label { text: lightXSlider.value + "px"; color: theme.textDim; font.bold: true }
                    }
                    QQC2.Slider {
                        id: lightXSlider; Layout.fillWidth: true; 
                        from: -300; to: 300; stepSize: 10; 
                        value: DockSettings.shadowLightX; 
			onMoved: {
                                 DockSettings.shadowLightX = value;
                                 DockSettings.save();
                             }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

                // LIGHT Y
                ColumnLayout {
                    Layout.fillWidth: true
                    RowLayout {
                        QQC2.Label { Layout.fillWidth: true; text: i18n("Light Y (Vertical offset)"); color: theme.text; font.bold: true }
                        QQC2.Label { text: lightYSlider.value + "px"; color: theme.textDim; font.bold: true }
                    }
                    QQC2.Slider {
                        id: lightYSlider; Layout.fillWidth: true; 
                        from: -300; to: 300; stepSize: 10; 
                        value: DockSettings.shadowLightY; 
			onMoved: {
                                 DockSettings.shadowLightY = value;
                                 DockSettings.save();
                             }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

                // LIGHT Z (HEIGHT)
                ColumnLayout {
                    Layout.fillWidth: true
                    RowLayout {
                        QQC2.Label { Layout.fillWidth: true; text: i18n("Light Z (Distance from dock)"); color: theme.text; font.bold: true }
                        QQC2.Label { text: lightZSlider.value; color: theme.textDim; font.bold: true }
                    }
                    QQC2.Slider {
                        id: lightZSlider; Layout.fillWidth: true; 
                        from: 100; to: 2000; stepSize: 20; 
                        value: DockSettings.shadowLightZ; 
			onMoved: {
                                 DockSettings.shadowLightZ = value;
                                 DockSettings.save();
                             }
                    }
                }
            }
        }

        // --- SECTION 3: APPEARANCE ---
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: DockSettings.shadowEnabled
            
            QQC2.Label { 
                text: i18n("Shadow Appearance")
                color: theme.textDim
                font.bold: true
                font.letterSpacing: 1.1
                font.pixelSize: 12
                Layout.leftMargin: 8
            }
            
            KremaCard {
                // ELEVATION
                ColumnLayout {
                    Layout.fillWidth: true
                    RowLayout {
                        QQC2.Label { Layout.fillWidth: true; text: i18n("Elevation (Depth scale)"); color: theme.text; font.bold: true }
                        QQC2.Label { text: elevationSlider.value; color: theme.textDim; font.bold: true }
                    }
                    QQC2.Slider {
                        id: elevationSlider; Layout.fillWidth: true; 
                        from: 1; to: 50; stepSize: 1; 
                        value: DockSettings.shadowElevation; 
			onMoved: {
                                 DockSettings.shadowElevation = value;
                                 DockSettings.save();
                             }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

                // RADIUS
                ColumnLayout {
                    Layout.fillWidth: true
                    RowLayout {
                        QQC2.Label { Layout.fillWidth: true; text: i18n("Blur Radius"); color: theme.text; font.bold: true }
                        QQC2.Label { text: lightRadiusSlider.value.toFixed(1) + "px"; color: theme.textDim; font.bold: true }
                    }
                    QQC2.Slider {
                        id: lightRadiusSlider; Layout.fillWidth: true; 
                        from: 0.5; to: 20.0; stepSize: 0.5; 
                        value: DockSettings.shadowLightRadius; 
			onMoved: {
                                 DockSettings.shadowLightRadius = value;
                                 DockSettings.save();
                             }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

                // INTENSITY
                ColumnLayout {
                    Layout.fillWidth: true
                    RowLayout {
                        QQC2.Label { Layout.fillWidth: true; text: i18n("Shadow Intensity"); color: theme.text; font.bold: true }
                        QQC2.Label { text: Math.round(intensitySlider.value * 100) + "%"; color: theme.textDim; font.bold: true }
                    }
                    QQC2.Slider {
                        id: intensitySlider; Layout.fillWidth: true; 
                        from: 0.0; to: 1.0; stepSize: 0.05; 
                        value: DockSettings.shadowIntensity; 
			onMoved: {
                                 DockSettings.shadowIntensity = value;
                                 DockSettings.save();
                             }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

                // SHADOW COLOR PICKER
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Label { Layout.fillWidth: true; text: i18n("Shadow Color Tint"); color: theme.text; font.bold: true }
                    Rectangle {
                        width: 48; height: 28; radius: 6
                        color: DockSettings.shadowColor
                        border.color: "#333133"; border.width: 1
                        MouseArea { 
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: shadowColorDialog.open() 
                        }
                    }
                }
            }
        }
    }

    // Native Color Dialog
    ColorDialog { 
        id: shadowColorDialog; 
        title: i18n("Choose shadow color"); 
        selectedColor: DockSettings.shadowColor; 
	onAccepted: {
                 DockSettings.shadowColor = selectedColor;
                 DockSettings.save();
             }
    }
}
