import QtQuick
import org.krema.ui
import org.kde.kirigami as Kirigami

/**
 * @brief Tier 2: Logical Island.
 * Groups items for organizational logic.
 */
Item {
    id: root
    
    property BaseIsland islandData
    
    width: islandData ? islandData.width : 0
    height: parent.height 
    
    // --- Kinetic Easing: Unified DockAnimator ---
    DockAnimator { id: animator }
    
    // onWidthChanged: animator.apply(root, "width", width)
    // onXChanged: animator.apply(root, "x", x)
    
    // --- Layer 1: Island Glass (Rule 18: legibility) ---
    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        radius: height / 2
        color: Qt.rgba(255, 255, 255, 0.05) // Very subtle glass
        border.color: Qt.rgba(255, 255, 255, 0.1)
        border.width: 1
        visible: islandData ? islandData.items.length > 0 : false
    }

    // --- Layer 2: Item Group ---
    Repeater {
        model: islandData ? islandData.items : []
        delegate: ItemCapsule {
            itemData: modelData
            x: modelData ? modelData.x : 0
            // FIXED: Anchor to bottom of island to fulfill Mandate Rule 6 (Visual Overflow)
            anchors.bottom: parent.bottom
        }
        
        onCountChanged: console.log("[QML] Island Items Count: " + count);
    }

    // --- Layer 99: Debug Overlay ---
    Rectangle {
        id: islandDebug
        visible: isGeomDebug
        anchors.fill: parent
        color: "#2200ffff" // Semi-transparent cyan
        border.color: "#00ffff"
        border.width: 1
        z: 999
        
        Text {
            text: "ISLAND: " + (islandData ? islandData.islandId : "?")
            color: "#00ffff"
            font.pixelSize: 8
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    HoverHandler {
        enabled: isGeomDebug
        onHoveredChanged: {
            if (hovered) {
                console.log("\u001b[36m[GEOM-ISLAND]\u001b[0m Cursor ENTERED Island: " + (islandData ? islandData.islandId : "?"));
            } else {
                console.log("\u001b[36m[GEOM-ISLAND]\u001b[0m Cursor LEFT Island: " + (islandData ? islandData.islandId : "?"));
            }
        }
    }
}
