// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0

// Import our custom UI Kit
import "../components"

QQC2.ScrollView {
    id: virtualDesktopsPage
    contentWidth: availableWidth
    clip: true

    // Standard spacing for a consistent "Krema" feel
    topPadding: 16
    bottomPadding: 32
    leftPadding: 16
    rightPadding: 16

    ColumnLayout {
        width: parent.width
        spacing: 32

        // --- SECTION 1: DESKTOP WORKFLOW ---
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            
            QQC2.Label { 
                text: i18n("Desktop Workflow")
                color: theme.textDim // 50% opacity Krema Accent
                font.bold: true; font.letterSpacing: 1.1; font.pixelSize: 12
                Layout.leftMargin: 8
            }
            
            KremaCard {
                // DISPLAY MODE
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        QQC2.Label { text: i18n("Icon Display Mode"); color: theme.text; font.bold: true }
                        QQC2.Label { 
                            text: i18n("Filter which windows are shown based on your current workspace."); 
                            color: theme.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true 
                        }
                    }
		    RowLayout {
                           spacing: 4
                           Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                           Repeater {
                               model: [
                                   { icon: "view-list-details", label: i18n("Show All"), value: 0 },
                                   { icon: "view-preview", label: i18n("Dim Others"), value: 1 },
                                   { icon: "preferences-desktop-virtual", label: i18n("Current Only"), value: 2 }
                               ]
                               delegate: QQC2.Button {
                                   id: vdBtn
                                   property bool isSelected: DockSettings.virtualDesktopMode === modelData.value
                                   QQC2.ToolTip.visible: hovered; QQC2.ToolTip.text: modelData.label
                                   contentItem: Kirigami.Icon { source: modelData.icon; color: vdBtn.isSelected ? theme.text : theme.textDim; implicitWidth: 18; implicitHeight: 18 }
                                   background: Rectangle {
                                       implicitWidth: 50; implicitHeight: 34; radius: 8
                                       color: vdBtn.isSelected ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.25) : (vdBtn.hovered ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.12) : "transparent")
                                       border.color: vdBtn.isSelected ? theme.accent : "transparent"; border.width: 1
                                   }
                                   onClicked: { DockSettings.virtualDesktopMode = modelData.value; DockSettings.save(); }
                               }
                           }
                       }
                }

                Rectangle { 
                    Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A"
                    visible: DockSettings.virtualDesktopMode === 1 // Only show if "Dim other desktops" is active
                }

                // CONDITIONAL: DIM OPACITY SLIDER
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: DockSettings.virtualDesktopMode === 1
                    RowLayout {
                        QQC2.Label { Layout.fillWidth: true; text: i18n("Inactive Desktop Opacity"); color: theme.text; font.bold: true }
                        QQC2.Label { text: Math.round(dimOpacitySlider.value * 100) + "%"; color: theme.textDim; font.bold: true }
                    }
                    QQC2.Slider {
                        id: dimOpacitySlider; Layout.fillWidth: true; 
                        from: 0.1; to: 0.9; stepSize: 0.05; 
                        value: DockSettings.otherDesktopOpacity; 
			onMoved: {
                                 DockSettings.otherDesktopOpacity = value;
                                 DockSettings.save();
                             }
                    }
                }
            }
        }
    }
}
