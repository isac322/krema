// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import QtQuick.Window
import com.bhyoo.krema 1.0
import "../components"

QQC2.ScrollView {
   id: bgPage
   contentWidth: availableWidth
   clip: true

   topPadding: 16
   bottomPadding: 32
   leftPadding: 16
   rightPadding: 16

   ColumnLayout {
       width: parent.width
       spacing: 32

       // --- PILLAR 0: THEME ---
       ColumnLayout {
           Layout.fillWidth: true
           spacing: 8
           
           QQC2.Label { 
               text: i18n("Interface Theme")
               color: theme.textDim
               font.bold: true
               font.letterSpacing: 1.1
               font.pixelSize: 12
               Layout.leftMargin: 8
           }
           
           KremaCard {
               ColumnLayout {
                   Layout.fillWidth: true
                   spacing: 12

                   ColumnLayout {
                       Layout.fillWidth: true
                       spacing: 2
                       QQC2.Label { 
                           text: i18n("Color Mode")
                           color: theme.text
                           font.bold: true 
                           Layout.fillWidth: true
                       }
                       QQC2.Label { 
                           text: i18n("Switch between light and dark aesthetics.")
                           color: theme.textDim
                           font.pixelSize: 11
                           wrapMode: Text.WordWrap
                           Layout.fillWidth: true
                       }
                   }

                   RowLayout {
                       spacing: 4
                       Repeater {
                           model: [
                               { icon: "view-brightness", label: i18n("Light"), value: 0 },
                               { icon: "view-night", label: i18n("Dark"), value: 1 }
                           ]
                           delegate: QQC2.Button {
                               id: themeBtn
                               property bool isSelected: DockSettings.settingsThemeMode === modelData.value
                               leftPadding: 16; rightPadding: 16

                               contentItem: RowLayout {
                                   spacing: 8
                                   Kirigami.Icon {
                                       source: modelData.icon
                                       color: themeBtn.isSelected ? theme.text : theme.textDim
                                       implicitWidth: 16; implicitHeight: 16
                                   }
                                   QQC2.Label {
                                       text: modelData.label
                                       color: themeBtn.isSelected ? theme.text : theme.textDim
                                       font.pointSize: 9; font.bold: themeBtn.isSelected
                                   }
                               }

                               background: Rectangle {
                                   implicitHeight: 36; radius: 18
                                   color: themeBtn.isSelected ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.25) : (themeBtn.hovered ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.12) : "transparent")
                                   border.color: themeBtn.isSelected ? theme.accent : "transparent"; border.width: 1
                                   Behavior on color { ColorAnimation { duration: 150 } }
                               }
                               onClicked: { 
                                   DockSettings.settingsThemeMode = modelData.value; 
                                   DockSettings.save(); 
                               }
                           }
                       }
                   }
               }
           }
       }

       // --- PILLAR 1: MATERIAL ---
       ColumnLayout {
           Layout.fillWidth: true
           spacing: 8
           
           QQC2.Label { 
               text: i18n("Base Material")
               color: theme.textDim // 50% opacity Krema Accent
               font.bold: true
               font.letterSpacing: 1.1
               font.pixelSize: 12
               Layout.leftMargin: 8
           }
           
           KremaCard {
               ColumnLayout {
                   Layout.fillWidth: true
                   spacing: 12

                   ColumnLayout {
                       Layout.fillWidth: true
                       spacing: 2
                       QQC2.Label { 
                           text: i18n("Style")
                           color: theme.text
                           font.bold: true 
                           Layout.fillWidth: true
                       }
                       QQC2.Label { 
                           text: i18n("The visual texture of the dock material.")
                           color: theme.textDim
                           font.pixelSize: 11
                           wrapMode: Text.WordWrap
                           Layout.fillWidth: true
                       }
                   }

                   Flow {
                       Layout.fillWidth: true
                       spacing: 4
                       Repeater {
                           model: [
                               { icon: "color-management", label: i18n("System Adaptive"), value: 0 },
                               { icon: "view-hidden", label: i18n("Transparent"), value: 1 },
                               { icon: "format-fill-color", label: i18n("Solid"), value: 2 },
                               { icon: "view-glass", label: i18n("Acrylic"), value: 3 },
                               { icon: "window-duplicate", label: i18n("Accent Tint"), value: 4 }
                           ]
                           delegate: QQC2.Button {
                               id: bgBtn
                               property bool isSelected: DockSettings.backgroundStyle === modelData.value
                               leftPadding: 16; rightPadding: 16

                               contentItem: RowLayout {
                                   spacing: 8
                                   Kirigami.Icon {
                                       source: modelData.icon
                                       color: bgBtn.isSelected ? theme.text : theme.textDim
                                       implicitWidth: 16; implicitHeight: 16
                                   }
                                   QQC2.Label {
                                       text: modelData.label
                                       color: bgBtn.isSelected ? theme.text : theme.textDim
                                       font.pointSize: 9; font.bold: bgBtn.isSelected
                                   }
                               }

                               background: Rectangle {
                                   implicitHeight: 34; radius: 8
                                   color: bgBtn.isSelected ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.25) : (bgBtn.hovered ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.12) : "transparent")
                                   border.color: bgBtn.isSelected ? theme.accent : "transparent"; border.width: 1
                                   Behavior on color { ColorAnimation { duration: 150 } }
                               }
                               onClicked: { DockSettings.backgroundStyle = modelData.value; DockSettings.save(); }
                           }
                       }
                   }
               }
           }
       }

        // --- PILLAR 2: COLOR & OPACITY ---
        // Visibility rules per style:
        // System Adaptive (0): Opacity slider only
        // Transparent (1): No controls
        // Solid (2): Color picker + opacity slider
        // Acrylic (3): Tint color + tint opacity
        // Accent Tint (4): Opacity slider only (accent color)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: DockSettings.backgroundStyle !== 1 // Hide entirely for Transparent
            
            QQC2.Label { 
                text: DockSettings.backgroundStyle === 4 ? i18n("Tint Intensity") : i18n("Color & Opacity")
                color: theme.textDim
                font.bold: true
                font.letterSpacing: 1.1
                font.pixelSize: 12
                Layout.leftMargin: 8
            }
            
            KremaCard {
                // USE SYSTEM COLOR SWITCH — only for Solid (2) and Acrylic (3)
                KremaSwitch {
                    Layout.fillWidth: true
                    text: i18n("Use System Accent Color")
                    checked: DockSettings.useSystemColor
                    visible: DockSettings.backgroundStyle === 2 || DockSettings.backgroundStyle === 3
                    onToggled: { DockSettings.useSystemColor = checked; DockSettings.save(); }
                }

                Rectangle { 
                    Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A"
                    visible: !DockSettings.useSystemColor && (DockSettings.backgroundStyle === 2 || DockSettings.backgroundStyle === 3)
                }

                // CUSTOM COLOR PICKER — only for Solid (2) and Acrylic (3) when not using system color
                RowLayout {
                    Layout.fillWidth: true
                    visible: !DockSettings.useSystemColor && (DockSettings.backgroundStyle === 2 || DockSettings.backgroundStyle === 3)
                    QQC2.Label { Layout.fillWidth: true; text: i18n("Custom Tint Color"); color: theme.text; font.bold: true }
                    Rectangle {
                        width: 48; height: 28; radius: 6
                        color: DockSettings.tintColor
                        border.color: "#333133"; border.width: 1
                        MouseArea { 
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: colorPickerPopup.open() 
                        }
                    }
                }

                Rectangle { 
                    Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A"
                    visible: DockSettings.backgroundStyle !== 1
                }

                // OPACITY SLIDER — visible for all styles except Transparent (1)
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: DockSettings.backgroundStyle !== 1
                    RowLayout {
                        QQC2.Label { 
                            Layout.fillWidth: true
                            text: DockSettings.backgroundStyle === 4 ? i18n("Accent Intensity") : i18n("Background Opacity")
                            color: theme.text; font.bold: true 
                        }
                        QQC2.Label { text: Math.round(opacitySlider.value * 100) + "%"; color: theme.textDim; font.bold: true }
                    }
                    QQC2.Slider {
                        id: opacitySlider
                        Layout.fillWidth: true
                        from: 0.3; to: 1.0; stepSize: 0.05
                        value: DockSettings.backgroundOpacity
                        onMoved: { DockSettings.backgroundOpacity = value; DockSettings.save(); }
                    }
                }
            }
        }

        // --- PILLAR 3: RIM LIGHT ---
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            QQC2.Label {
                text: i18n("Edge Highlight")
                color: theme.textDim
                font.bold: true; font.letterSpacing: 1.1; font.pixelSize: 12
                Layout.leftMargin: 8
            }

            KremaCard {
                KremaSwitch {
                    Layout.fillWidth: true
                    text: i18n("Rim Light")
                    checked: DockSettings.rimLightEnabled
                    onToggled: { DockSettings.rimLightEnabled = checked; DockSettings.save(); }
                }
                QQC2.Label {
                    text: i18n("Adds a subtle glowing border to visually separate the panel from the wallpaper.")
                    color: theme.textDim; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A"; visible: DockSettings.rimLightEnabled }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: DockSettings.rimLightEnabled
                    RowLayout {
                        QQC2.Label { Layout.fillWidth: true; text: i18n("Intensity"); color: theme.text; font.bold: true }
                        QQC2.Label { text: Math.round(rimLightSlider.value * 100) + "%"; color: theme.textDim; font.bold: true }
                    }
                    QQC2.Slider {
                        id: rimLightSlider; Layout.fillWidth: true
                        from: 0.05; to: 0.5; stepSize: 0.05
                        value: DockSettings.rimLightOpacity
                        onMoved: { DockSettings.rimLightOpacity = value; DockSettings.save(); }
                    }
                }
            }
        }

       // --- CUSTOM COLOR PICKER POPUP ---
       QQC2.Popup {
           id: colorPickerPopup
           parent: bgPage.Window.window ? bgPage.Window.window.contentItem : bgPage
           x: Math.round((parent.width - width) / 2)
           y: Math.round((parent.height - height) / 2)
           width: Kirigami.Units.gridUnit * 18
           modal: true
           focus: true
           closePolicy: QQC2.Popup.CloseOnEscape | QQC2.Popup.CloseOnPressOutside

           // Krema Theming for the Popup background
           background: Rectangle {
               color: "#1C1A1C"
               radius: 12
               border.color: "#333133"
               border.width: 1
           }

           property real rVal: 0
           property real gVal: 0
           property real bVal: 0
           property color tempColor: Qt.rgba(rVal, gVal, bVal, 1.0)

           onOpened: {
               let tint = DockSettings.tintColor;
               let c = (tint && tint.length > 0) ? Qt.color(tint) : Qt.color("white");
               rVal = c.r; gVal = c.g; bVal = c.b;
           }

           contentItem: ColumnLayout {
               spacing: Kirigami.Units.largeSpacing

               QQC2.Label {
                   text: i18n("Custom Tint Color")
                   font.weight: Font.Bold
                   color: theme.text
                   Layout.alignment: Qt.AlignHCenter
               }

               Rectangle {
                   Layout.fillWidth: true
                   height: Kirigami.Units.gridUnit * 4
                   radius: 8
                   color: colorPickerPopup.tempColor
                   border.color: "#333133"
                   border.width: 1
               }

               GridLayout {
                   columns: 2
                   Layout.fillWidth: true
                   QQC2.Label { text: "R:"; color: theme.textDim; font.bold: true }
                   QQC2.Slider { Layout.fillWidth: true; from: 0; to: 1; value: colorPickerPopup.rVal; onMoved: colorPickerPopup.rVal = value }
                   QQC2.Label { text: "G:"; color: theme.textDim; font.bold: true }
                   QQC2.Slider { Layout.fillWidth: true; from: 0; to: 1; value: colorPickerPopup.gVal; onMoved: colorPickerPopup.gVal = value }
                   QQC2.Label { text: "B:"; color: theme.textDim; font.bold: true }
                   QQC2.Slider { Layout.fillWidth: true; from: 0; to: 1; value: colorPickerPopup.bVal; onMoved: colorPickerPopup.bVal = value }
               }

               RowLayout {
                   Layout.fillWidth: true
                   Item { Layout.fillWidth: true }
                   QQC2.Button { 
                       text: i18n("Cancel")
                       onClicked: colorPickerPopup.close() 
                   }
                   QQC2.Button {
                       text: i18n("Save")
                       highlighted: true
                       onClicked: {
                           DockSettings.tintColor = colorPickerPopup.tempColor.toString();
                           DockSettings.save();
                           colorPickerPopup.close();
                       }
                   }
               }
           }
       }
   }
}
