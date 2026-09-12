// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0
import "../components"

QQC2.ScrollView {
   id: behaviorPage
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
               text: i18n("App Attention & Badges")
               color: theme.textDim
               font.bold: true; font.letterSpacing: 1.1; font.pixelSize: 12
               Layout.leftMargin: 8
           }
           
           KremaCard {
               RowLayout {
                   Layout.fillWidth: true
                   ColumnLayout {
                       Layout.fillWidth: true; spacing: 2
                       QQC2.Label { text: i18n("Attention Animation"); color: theme.text; font.bold: true }
                       QQC2.Label { 
                          text: i18n("Visual feedback when an app needs you.")
                          color: theme.textDim; font.pixelSize: 12
                          wrapMode: Text.WordWrap; Layout.fillWidth: true 
                      }
                  }

                  RowLayout {
                      spacing: 4
                      Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                      Repeater {
                          model: [
                              { icon: "dialog-close", label: i18n("None"), value: 0 },
                              { icon: "go-up", label: i18n("Bounce"), value: 1 },
                              { icon: "fill-color", label: i18n("Glow"), value: 4 }
                          ]
                          delegate: QQC2.Button {
                              id: attBtn
                              property bool isSelected: DockSettings.attentionAnimation === modelData.value
                              leftPadding: 12; rightPadding: 12

                              contentItem: RowLayout {
                                  spacing: 8
                                  Kirigami.Icon {
                                      source: modelData.icon
                                      color: attBtn.isSelected ? theme.text : theme.textDim
                                      implicitWidth: 16; implicitHeight: 16
                                  }
                                  QQC2.Label {
                                      text: modelData.label
                                      color: attBtn.isSelected ? theme.text : theme.textDim
                                      font.pointSize: 9; font.bold: attBtn.isSelected
                                  }
                              }

                              background: Rectangle {
                                  implicitHeight: 34; radius: 8
                                  color: attBtn.isSelected ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.25) : (attBtn.hovered ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.12) : "transparent")
                                  border.color: attBtn.isSelected ? theme.accent : "transparent"; border.width: 1
                                  Behavior on color { ColorAnimation { duration: 150 } }
                              }
                              onClicked: { DockSettings.attentionAnimation = modelData.value; DockSettings.save(); }
                          }
                      }
                  }
               }

               Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

               ColumnLayout {
                   Layout.fillWidth: true
                   RowLayout {
                       QQC2.Label { Layout.fillWidth: true; text: i18n("Animation Duration"); color: theme.text; font.bold: true }
                       QQC2.Label { 
                           text: attentionDurationSlider.value === 0 ? i18n("Infinite") : attentionDurationSlider.value + "s"
                           color: theme.textDim; font.bold: true 
                       }
                   }
                   QQC2.Slider {
                       id: attentionDurationSlider; Layout.fillWidth: true; 
                       from: 0; to: 60; stepSize: 1; 
                       value: DockSettings.attentionAnimationDuration; 
                       onMoved: { DockSettings.attentionAnimationDuration = value; DockSettings.save(); }
                   }
               }

               Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

               RowLayout {
                   Layout.fillWidth: true
                   ColumnLayout {
                       Layout.fillWidth: true; spacing: 2
                       QQC2.Label { text: i18n("Badge Display"); color: theme.text; font.bold: true }
                       QQC2.Label { 
                          text: i18n("Notification count styles.")
                          color: theme.textDim; font.pixelSize: 12
                          wrapMode: Text.WordWrap; Layout.fillWidth: true 
                      }
                  }

                  RowLayout {
                      spacing: 4
                      Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                      Repeater {
                          model: [
                              { icon: "dialog-close", label: i18n("Off"), value: 2 },
                              { icon: "category-dot", label: i18n("Dot"), value: 1 },
                              { icon: "format-list-ordered", label: i18n("Number"), value: 0 }
                          ]
                          delegate: QQC2.Button {
                              id: badgeBtn
                              property bool isSelected: DockSettings.badgeDisplayMode === modelData.value
                              leftPadding: 12; rightPadding: 12

                              contentItem: RowLayout {
                                  spacing: 8
                                  Kirigami.Icon {
                                      source: modelData.icon
                                      color: badgeBtn.isSelected ? theme.text : theme.textDim
                                      implicitWidth: 16; implicitHeight: 16
                                  }
                                  QQC2.Label {
                                      text: modelData.label
                                      color: badgeBtn.isSelected ? theme.text : theme.textDim
                                      font.pointSize: 9; font.bold: badgeBtn.isSelected
                                  }
                              }

                              background: Rectangle {
                                  implicitHeight: 34; radius: 8
                                  color: badgeBtn.isSelected ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.25) : (badgeBtn.hovered ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.12) : "transparent")
                                  border.color: badgeBtn.isSelected ? theme.accent : "transparent"; border.width: 1
                                  Behavior on color { ColorAnimation { duration: 150 } }
                              }
                              onClicked: { DockSettings.badgeDisplayMode = modelData.value; DockSettings.save(); }
                          }
                      }
                  }
               }
           }
       }
   }
}
