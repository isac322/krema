// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0
import "../components"

QQC2.ScrollView {
   id: monitorPage
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
               text: i18n("Monitor Configuration")
               color: theme.textDim
               font.bold: true; font.letterSpacing: 1.1; font.pixelSize: 12
               Layout.leftMargin: 8
           }
           
           KremaCard {
               RowLayout {
                   Layout.fillWidth: true
                   ColumnLayout {
                       Layout.fillWidth: true; spacing: 2
                       QQC2.Label { text: i18n("Display Mode"); color: theme.text; font.bold: true }
                       QQC2.Label { 
                           text: i18n("Choose which monitors should display the dock."); 
                           color: theme.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true 
                       }
                   }

                   RowLayout {
                       spacing: 4
                       Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                       Repeater {
                           model: [
                               { icon: "video-display", label: i18n("Primary"), value: 0 },
                               { icon: "video-display-symbolic", label: i18n("All"), value: 1 },
                               { icon: "input-mouse", label: i18n("Follow"), value: 2 }
                           ]
                           delegate: QQC2.Button {
                               id: monBtn
                               property bool isSelected: DockSettings.monitorMode === modelData.value
                               leftPadding: 12; rightPadding: 12

                               contentItem: RowLayout {
                                   spacing: 8
                                   Kirigami.Icon {
                                       source: modelData.icon
                                       color: monBtn.isSelected ? theme.text : theme.textDim
                                       implicitWidth: 16; implicitHeight: 16
                                   }
                                   QQC2.Label {
                                       text: modelData.label
                                       color: monBtn.isSelected ? theme.text : theme.textDim
                                       font.pointSize: 9; font.bold: monBtn.isSelected
                                   }
                               }

                               background: Rectangle {
                                   implicitHeight: 34; radius: 8
                                   color: monBtn.isSelected ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.25) : (monBtn.hovered ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.12) : "transparent")
                                   border.color: monBtn.isSelected ? theme.accent : "transparent"; border.width: 1
                                   Behavior on color { ColorAnimation { duration: 150 } }
                               }
                               onClicked: { DockSettings.monitorMode = modelData.value; DockSettings.save(); }
                           }
                       }
                   }
               }
           }
       }
   }
}
