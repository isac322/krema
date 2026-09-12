// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0
import "../components"

QQC2.ScrollView {
   id: visibilityPage
   contentWidth: availableWidth
   clip: true

   topPadding: 16
   bottomPadding: 32
   leftPadding: 16
   rightPadding: 16

   ColumnLayout {
       width: parent.width
       spacing: 32

       ColumnLayout {
           Layout.fillWidth: true
           spacing: 8
           
           QQC2.Label { 
               text: i18n("Visibility Behavior")
               color: theme.textDim
               font.bold: true; font.letterSpacing: 1.1; font.pixelSize: 12
               Layout.leftMargin: 8
           }
           
           KremaCard {
               RowLayout {
                   Layout.fillWidth: true
                   ColumnLayout {
                       Layout.fillWidth: true; spacing: 2
                       QQC2.Label { text: i18n("Visibility Mode"); color: theme.text; font.bold: true }
                       QQC2.Label { 
                           text: i18n("Choose how the dock interacts with other windows."); 
                           color: theme.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true 
                       }
                   }

                   RowLayout {
                       spacing: 4
                       Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                       Repeater {
                           model: [
                               { icon: "view-visible", label: i18n("Always"), value: 0 },
                               { icon: "view-hidden", label: i18n("Auto Hide"), value: 1 },
                               { icon: "window-keep-below", label: i18n("Dodge"), value: 2 }
                           ]

                           delegate: QQC2.Button {
                               id: visBtn
                               property bool isSelected: DockSettings.visibilityMode === modelData.value
                               leftPadding: 12; rightPadding: 12

                               contentItem: RowLayout {
                                   spacing: 8
                                   Kirigami.Icon {
                                       source: modelData.icon
                                       color: visBtn.isSelected ? theme.text : theme.textDim
                                       implicitWidth: 16; implicitHeight: 16
                                   }
                                   QQC2.Label {
                                       text: modelData.label
                                       color: visBtn.isSelected ? theme.text : theme.textDim
                                       font.pointSize: 9; font.bold: visBtn.isSelected
                                   }
                               }

                               background: Rectangle {
                                   implicitHeight: 34; radius: 8
                                   color: visBtn.isSelected ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.25) : (visBtn.hovered ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.12) : "transparent")
                                   border.color: visBtn.isSelected ? theme.accent : "transparent"; border.width: 1
                                   Behavior on color { ColorAnimation { duration: 150 } }
                               }

                               onClicked: {
                                   DockSettings.visibilityMode = modelData.value
                                   DockSettings.save()
                               }
                           }
                       }
                   }
               }

               Rectangle { 
                   Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A"
                   visible: DockSettings.visibilityMode === 0 || DockSettings.visibilityMode === 2
               }

               KremaSwitch {
                   Layout.fillWidth: true
                   visible: DockSettings.visibilityMode === 0
                   text: i18n("Reserve Screen Space")
                   checked: DockSettings.reserveSpace
                   onToggled: { DockSettings.reserveSpace = checked; DockSettings.save(); }
               }

               // RESERVE MODE SELECTION (Rule 6: UI Blindness Prevention)
               ColumnLayout {
                   Layout.fillWidth: true; Layout.leftMargin: 32
                   visible: DockSettings.visibilityMode === 0 && DockSettings.reserveSpace
                   spacing: 8

                   RowLayout {
                       spacing: 8
                       Repeater {
                           model: [
                               { label: i18n("Panel Background"), value: 0, desc: i18n("Windows touch the dock pill. Icons can overlap.") },
                               { label: i18n("Icon Extents"), value: 1, desc: i18n("Windows stop at the icons. No overlap.") }
                           ]
                           delegate: QQC2.Button {
                               id: modeBtn
                               text: modelData.label
                               checkable: true
                               checked: DockSettings.reserveMode === modelData.value
                               onClicked: { DockSettings.reserveMode = modelData.value; DockSettings.save(); }
                               
                               leftPadding: 16; rightPadding: 16 // Added breathing room

                               contentItem: QQC2.Label {
                                   text: modeBtn.text; font.pixelSize: 11; font.bold: modeBtn.checked
                                   color: modeBtn.checked ? theme.accent : theme.textDim
                                   horizontalAlignment: Text.AlignHCenter
                                   verticalAlignment: Text.AlignVCenter // Fix off-center look
                               }
                               background: Rectangle {
                                   implicitHeight: 32; radius: 8 // Unified with rest of UI
                                   color: modeBtn.checked ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.15) : "transparent"
                                   border.color: modeBtn.checked ? theme.accent : "#3D3B3D"; border.width: 1
                               }
                           }
                       }
                   }
               }

               KremaSwitch {
                   Layout.fillWidth: true
                   visible: DockSettings.visibilityMode === 2
                   text: i18n("Only Dodge Active Window")
                   checked: DockSettings.dodgeActiveOnly
                   onToggled: { DockSettings.dodgeActiveOnly = checked; DockSettings.save(); }
               }

                Rectangle { 
                    Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A"
                    visible: DockSettings.visibilityMode !== 0
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: DockSettings.visibilityMode !== 0
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        QQC2.Label { text: i18n("Show Delay"); color: theme.text; font.bold: true }
                        QQC2.Label { text: i18n("How long to wait before the dock appears (ms)."); color: theme.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                    ColumnLayout {
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        Layout.preferredWidth: 150
                        RowLayout {
                            Layout.fillWidth: true
                            Item { Layout.fillWidth: true }
                            QQC2.Label { text: showDelaySlider.value + "ms"; color: theme.textDim; font.bold: true }
                        }
                        QQC2.Slider {
                            id: showDelaySlider; Layout.fillWidth: true; 
                            from: 0; to: 1000; stepSize: 50
                            value: DockSettings.showDelay; 
                            onMoved: DockSettings.showDelay = value
                            onPressedChanged: {
                                if (!pressed) {
                                    DockSettings.showDelay = value;
                                    DockSettings.save();
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: DockSettings.visibilityMode !== 0
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        QQC2.Label { text: i18n("Hide Delay"); color: theme.text; font.bold: true }
                        QQC2.Label { text: i18n("How long to wait before the dock disappears (ms)."); color: theme.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                    ColumnLayout {
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        Layout.preferredWidth: 150
                        RowLayout {
                            Layout.fillWidth: true
                            Item { Layout.fillWidth: true }
                            QQC2.Label { text: hideDelaySlider.value + "ms"; color: theme.textDim; font.bold: true }
                        }
                        QQC2.Slider {
                            id: hideDelaySlider; Layout.fillWidth: true; 
                            from: 0; to: 2000; stepSize: 50
                            value: DockSettings.hideDelay; 
                            onMoved: DockSettings.hideDelay = value
                            onPressedChanged: {
                                if (!pressed) {
                                    DockSettings.hideDelay = value;
                                    DockSettings.save();
                                }
                            }
                        }
                    }
                }
            }
       }

       ColumnLayout {
           Layout.fillWidth: true
           spacing: 8
           
           QQC2.Label { 
               text: i18n("Placement")
               color: theme.textDim
               font.bold: true; font.letterSpacing: 1.1; font.pixelSize: 12
               Layout.leftMargin: 8
           }
           
           KremaCard {
               RowLayout {
                   Layout.fillWidth: true
                   ColumnLayout {
                       Layout.fillWidth: true; spacing: 2
                       QQC2.Label { text: i18n("Screen Edge"); color: theme.text; font.bold: true }
                       QQC2.Label { 
                           text: i18n("Which side of the monitor the dock is attached to."); 
                           color: theme.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true 
                       }
                   }

                   RowLayout {
                       spacing: 4
                       Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                       Repeater {
                           model: [
                               { icon: "align-horizontal-top-out-symbolic", label: i18n("Top"), value: 0 },
                               { icon: "align-horizontal-bottom-out-symbolic", label: i18n("Bottom"), value: 1 },
                               { icon: "align-horizontal-left-out-symbolic", label: i18n("Left"), value: 2 },
                               { icon: "align-horizontal-right-out-symbolic", label: i18n("Right"), value: 3 }
                           ]

                           delegate: QQC2.Button {
                               id: edgeBtn
                               property bool isSelected: DockSettings.edge === modelData.value
                               leftPadding: 12; rightPadding: 12

                               contentItem: RowLayout {
                                   spacing: 8
                                   Kirigami.Icon {
                                       source: modelData.icon
                                       color: edgeBtn.isSelected ? theme.text : theme.textDim
                                       implicitWidth: 16; implicitHeight: 16
                                   }
                                   QQC2.Label {
                                       text: modelData.label
                                       color: edgeBtn.isSelected ? theme.text : theme.textDim
                                       font.pointSize: 9; font.bold: edgeBtn.isSelected
                                   }
                               }

                               background: Rectangle {
                                   implicitHeight: 34; radius: 8
                                   color: edgeBtn.isSelected ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.25) : (edgeBtn.hovered ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.12) : "transparent")
                                   border.color: edgeBtn.isSelected ? theme.accent : "transparent"; border.width: 1
                                   Behavior on color { ColorAnimation { duration: 150 } }
                               }

                               onClicked: {
                                   DockSettings.edge = modelData.value
                                   DockSettings.save()
                                   if (typeof DockView !== "undefined" && DockView.screenSettings) {
                                       DockView.screenSettings.clearOverride("Edge")
                                   }
                               }
                           }
                       }
                   }
               }

               Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

               RowLayout {
                   Layout.fillWidth: true
                   ColumnLayout {
                       Layout.fillWidth: true; spacing: 2
                       QQC2.Label { text: i18n("Alignment"); color: theme.text; font.bold: true }
                       QQC2.Label { 
                           text: i18n("Position of the dock along the chosen screen edge."); 
                           color: theme.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true 
                       }
                   }

                   RowLayout {
                       spacing: 4
                       Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                       
                       property bool isVertical: DockSettings.edge === 2 || DockSettings.edge === 3

                       Repeater {
                           model: [
                               { icon: parent.isVertical ? "align-vertical-top-out-symbolic" : "align-horizontal-left-out-symbolic", label: parent.isVertical ? i18n("Top") : i18n("Left"), value: 1 },
                               { icon: parent.isVertical ? "align-vertical-center-symbolic" : "align-horizontal-center-symbolic", label: i18n("Center"), value: 0 },
                               { icon: parent.isVertical ? "align-vertical-bottom-out-symbolic" : "align-horizontal-right-out-symbolic", label: parent.isVertical ? i18n("Bottom") : i18n("Right"), value: 2 }
                           ]

                           delegate: QQC2.Button {
                               id: alignBtn
                               property bool isSelected: DockSettings.alignment === modelData.value
                               leftPadding: 12; rightPadding: 12

                               contentItem: RowLayout {
                                   spacing: 8
                                   Kirigami.Icon {
                                       source: modelData.icon
                                       color: alignBtn.isSelected ? theme.text : theme.textDim
                                       implicitWidth: 16; implicitHeight: 16
                                   }
                                   QQC2.Label {
                                       text: modelData.label
                                       color: alignBtn.isSelected ? theme.text : theme.textDim
                                       font.pointSize: 9; font.bold: alignBtn.isSelected
                                   }
                               }

                               background: Rectangle {
                                   implicitHeight: 34; radius: 8
                                   color: alignBtn.isSelected ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.25) : (alignBtn.hovered ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.12) : "transparent")
                                   border.color: alignBtn.isSelected ? theme.accent : "transparent"; border.width: 1
                                   Behavior on color { ColorAnimation { duration: 150 } }
                               }

                               onClicked: {
                                   DockSettings.alignment = modelData.value
                                   DockSettings.save()
                                   if (typeof DockView !== "undefined" && DockView.screenSettings) {
                                       DockView.screenSettings.clearOverride("Alignment")
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
