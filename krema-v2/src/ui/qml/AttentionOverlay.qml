import QtQuick
import org.krema.ui
import org.kde.kirigami as Kirigami

/**
 * @brief Layer 2: Attention Overlay.
 * Renders badges, running dots, and urgency glows.
 * Follows Rule 16 (Animation Consistency).
 */
Item {
    id: root

    property BaseItem itemData
    property real scaleFactor: itemData ? itemData.scaleFactor : 1.0

    // --- Layer 1: Running Indicator (The Dot/Pill) ---
    Rectangle {
        id: runningDot
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2 * scaleFactor
        anchors.horizontalCenter: parent.horizontalCenter
        
        // Liquid Pill: Width expands with scale
        width: (itemData && itemData.isRunning ? (itemData.scaleFactor > 1.1 ? 16 : 4) : 0) * scaleFactor
        height: 4 * scaleFactor
        radius: height / 2
        color: Kirigami.Theme.highlightColor
        
        opacity: itemData ? (itemData.isRunning ? 0.9 : 0.0) : 0.0
        
        Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutBack } }
        Behavior on opacity { NumberAnimation { duration: 300 } }
    }

    // --- Layer 2: Urgency Badge ---
    Rectangle {
        id: urgencyBadge
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 2 * scaleFactor
        anchors.rightMargin: 2 * scaleFactor
        
        // Liquid scale-in
        scale: (itemData && (itemData.urgency > 0 || itemData.notificationCount > 0)) ? 1.0 : 0.0
        Behavior on scale { NumberAnimation { duration: 400; easing.type: Easing.OutBack } }

        width: (countText.text.length > 1 ? 22 : 16) * scaleFactor
        height: 16 * scaleFactor
        radius: height / 2
        
        color: {
            if (!itemData) return "transparent";
            switch (itemData.urgency) {
                case 3: return "#ff4444"; // Critical (Red)
                case 2: return "#ffaa00"; // Normal (Orange)
                case 1: return "#00aaff"; // Low (Blue)
                default: return Kirigami.Theme.highlightColor;
            }
        }
        
        border.color: "white"
        border.width: 1 * scaleFactor

        // Notification Count
        Text {
            id: countText
            anchors.centerIn: parent
            text: (itemData && itemData.notificationCount > 0) ? itemData.notificationCount : ""
            color: "white"
            font.pixelSize: 10 * scaleFactor
            font.bold: true
            visible: text !== ""
        }
        
        // Pulse animation for Critical state
        SequentialAnimation on opacity {
            running: itemData ? itemData.urgency === 3 : false
            loops: Animation.Infinite
            NumberAnimation { to: 0.6; duration: 600; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutSine }
        }
    }
}
