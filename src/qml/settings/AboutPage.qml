// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0
import "../components"

QQC2.ScrollView {
    id: aboutPage
    contentWidth: availableWidth
    clip: true

    topPadding: 40
    bottomPadding: 40
    leftPadding: 16
    rightPadding: 16

    ColumnLayout {
        // Use the full available width of the ScrollView
        width: aboutPage.availableWidth - (aboutPage.leftPadding + aboutPage.rightPadding)
        spacing: 40

        // --- HERO SECTION ---
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            spacing: 16
            
            Kirigami.Icon {
                source: "com.bhyoo.krema"
                Layout.preferredWidth: 96
                Layout.preferredHeight: 96
                Layout.alignment: Qt.AlignHCenter
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 4
                QQC2.Label {
                    text: "Krema"
                    font.bold: true
                    font.pixelSize: 32
                    color: theme.text 
                    Layout.alignment: Qt.AlignHCenter
                }
                QQC2.Label {
                    text: i18n("A fast, modular dock for KDE Plasma 6")
                    color: theme.textDim
                    font.pixelSize: 14
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        // --- METADATA CARD ---
        KremaCard {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(parent.width, 400)
            
            RowLayout {
                Layout.fillWidth: true
                QQC2.Label { text: i18n("Version"); color: theme.textDim; Layout.fillWidth: true }
                QQC2.Label { text: "1.0.0-beta"; color: theme.text; font.bold: true }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

            RowLayout {
                Layout.fillWidth: true
                QQC2.Label { text: i18n("Author"); color: theme.textDim; Layout.fillWidth: true }
                QQC2.Label { text: "Byeonghoon Yoo"; color: theme.text; font.bold: true }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2A282A" }

            RowLayout {
                Layout.fillWidth: true
                QQC2.Label { text: i18n("License"); color: theme.textDim; Layout.fillWidth: true }
                QQC2.Label { text: "GPL-3.0-or-later"; color: theme.text; font.bold: true }
            }
        }

        // --- FOOTER ACTION ---
	QQC2.Button { id: bugButton
            text: i18n("Report a Bug")
            icon.name: "tools-report-bug"
            icon.color: theme.text
            flat: true
            Layout.alignment: Qt.AlignHCenter
            
            // Using standard contentItem to ensure clean centering without overlap
            contentItem: RowLayout {
                spacing: 8
                Kirigami.Icon { 
		    source: bugButton.icon.name
                    implicitWidth: 18
                    implicitHeight: 18
                    color: theme.text
                }
                QQC2.Label { 
		    text: bugButton.text
                    color: theme.text
                    font.bold: true 
                }
            }
        }
    }
}
