import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import "../src/ui/qml" as KremaUI

/**
 * @brief Demo for the Attention Engine visuals.
 * Allows testing Dots, Numbers, and all kinetic animations.
 */
Kirigami.ApplicationWindow {
    id: window
    width: 600
    height: 400
    title: "Krema Attention Engine Demo"

    Column {
        anchors.centerIn: parent
        spacing: 50

        Row {
            spacing: 50
            anchors.horizontalCenter: parent.horizontalCenter

            // --- Demo Item 1: Discord Style ---
            Rectangle {
                id: item1
                width: 64
                height: 64
                radius: 12
                color: "#5865F2" // Discord Blue
                
                Text {
                    anchors.centerIn: parent
                    text: "DIS"
                    color: "white"
                    font.bold: true
                }

                KremaUI.AttentionOverlay {
                    anchors.fill: parent
                    urgency: level2Toggle.checked ? 2 : (level1Toggle.checked ? 1 : 0)
                    notificationCount: countSlider.value
                    badgeStyle: badgeMode.currentIndex + 1
                    level2Animation: animMode.currentText
                }
            }

            // --- Demo Item 2: Battery Style ---
            Rectangle {
                id: item2
                width: 64
                height: 64
                radius: 32
                color: "#2ecc71" // Battery Green
                
                Text {
                    anchors.centerIn: parent
                    text: "BAT"
                    color: "white"
                    font.bold: true
                }

                KremaUI.AttentionOverlay {
                    anchors.fill: parent
                    urgency: level3Toggle.checked ? 3 : 0
                    level3Animation: criticalAnimMode.currentText
                }
            }
        }

        // --- Controls ---
        GridLayout {
            columns: 2
            rowSpacing: 10
            columnSpacing: 20
            anchors.horizontalCenter: parent.horizontalCenter

            Label { text: "Level 1 (Informational)" }
            CheckBox { id: level1Toggle; text: "Media / Track Change" }

            Label { text: "Level 2 (Active)" }
            CheckBox { id: level2Toggle; text: "Discord / Unread"; checked: true }

            Label { text: "Level 3 (Critical)" }
            CheckBox { id: level3Toggle; text: "Battery / VOIP" }

            Label { text: "Badge Style" }
            ComboBox {
                id: badgeMode
                model: ["Dot", "Number"]
                currentIndex: 1
            }

            Label { text: "Unread Count" }
            Slider {
                id: countSlider
                from: 0
                to: 99
                value: 5
                stepSize: 1
            }

            Label { text: "Level 2 Animation" }
            ComboBox {
                id: animMode
                model: ["Static", "Pulse", "Shake"]
                currentIndex: 1
            }

            Label { text: "Level 3 Animation" }
            ComboBox {
                id: criticalAnimMode
                model: ["Bounce", "Glow", "Shake"]
                currentIndex: 0
            }
        }
    }
}
