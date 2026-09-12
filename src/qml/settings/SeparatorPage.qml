// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0
import "../components"

QQC2.ScrollView {
   id: separatorPage
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
               text: i18n("Appearance")
               color: theme.textDim
               font.bold: true; font.letterSpacing: 1.1; font.pixelSize: 12
               Layout.leftMargin: 8
           }
           
           KremaCard {
               RowLayout {
                   Layout.fillWidth: true
                   ColumnLayout {
                       Layout.fillWidth: true; spacing: 2
                       QQC2.Label { text: i18n("Style"); color: theme.text; font.bold: true }
                       QQC2.Label { text: i18n("The visual design of the divider."); color: theme.textDim; font.pixelSize: 12 }
                   }

                   RowLayout {
                       spacing: 4
                       Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                       Repeater {
                           model: [
                               { icon: "list-add-separator", label: i18n("Classic"), value: 0 },
                               { icon: "view-grid", label: i18n("Dots"), value: 1 },
                               { icon: "view-sort-descending", label: i18n("Gradient"), value: 2 }
                           ]
                           delegate: QQC2.Button {
                               id: sepBtn
                               property bool isSelected: DockSettings.separatorStyle === modelData.value
                               leftPadding: 12; rightPadding: 12

                               contentItem: RowLayout {
                                   spacing: 8
                                   Kirigami.Icon {
                                       source: modelData.icon
                                       color: sepBtn.isSelected ? theme.text : theme.textDim
                                       implicitWidth: 16; implicitHeight: 16
                                   }
                                   QQC2.Label {
                                       text: modelData.label
                                       color: sepBtn.isSelected ? theme.text : theme.textDim
                                       font.pointSize: 9; font.bold: sepBtn.isSelected
                                   }
                               }

                               background: Rectangle {
                                   implicitHeight: 34; radius: 8
                                   color: sepBtn.isSelected ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.25) : (sepBtn.hovered ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.12) : "transparent")
                                   border.color: sepBtn.isSelected ? theme.accent : "transparent"; border.width: 1
                                   Behavior on color { ColorAnimation { duration: 150 } }
                               }
                               onClicked: { DockSettings.separatorStyle = modelData.value; DockSettings.save(); }
                           }
                       }
                   }
               }

               Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

               ColumnLayout {
                   Layout.fillWidth: true
                   RowLayout {
                       QQC2.Label { Layout.fillWidth: true; text: i18n("Opacity"); color: theme.text; font.bold: true }
                       QQC2.Label { text: Math.round(sepOpacitySlider.value * 100) + "%"; color: theme.textDim; font.bold: true }
                   }
                   QQC2.Slider {
                       id: sepOpacitySlider; Layout.fillWidth: true
                       from: 0.0; to: 1.0; stepSize: 0.05
                       value: DockSettings.separatorOpacity
                       onMoved: { DockSettings.separatorOpacity = value; DockSettings.save(); }
                   }
               }
           }
       }
   }
}
