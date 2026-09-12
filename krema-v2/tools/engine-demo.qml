import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.krema.ui as KremaUI

/**
 * @brief Demo for the 3-Tier Mathematical Engine.
 * Verifies Panel -> Island -> Item recursive layout and Parabolic Zoom.
 */
Kirigami.ApplicationWindow {
    id: window
    width: 1000
    height: 400
    title: "Krema 3-Tier Engine Demo"

    // --- C++ Data Foundation ---
    KremaUI.LayoutManager {
        id: layoutBrain
        panel: globalPanel
    }

    KremaUI.BasePanel {
        id: globalPanel
        thickness: 72
        floatingOffset: 16
        
        islands: [
            pinnedAppsIsland,
            systemTrayIsland
        ]
    }

    KremaUI.BaseIsland {
        id: pinnedAppsIsland
        islandId: 1
        padding: 12
        spacing: 8
        zoomEnabled: true
        
        items: [
            KremaUI.BaseItem { slotId: 101; contentSize: 48 },
            KremaUI.BaseItem { slotId: 102; contentSize: 48; urgency: KremaUI.Krema.Normal; notificationCount: 3 },
            KremaUI.BaseItem { slotId: 103; contentSize: 48 },
            KremaUI.BaseItem { slotId: 104; contentSize: 48 }
        ]
    }

    KremaUI.BaseIsland {
        id: systemTrayIsland
        islandId: 2
        padding: 8
        spacing: 4
        zoomEnabled: false // Tray doesn't zoom
        
        items: [
            KremaUI.BaseItem { slotId: 901; contentSize: 32 },
            KremaUI.BaseItem { slotId: 902; contentSize: 32; urgency: KremaUI.Krema.Critical }
        ]
    }

    // --- Visual Rendering ---
    KremaUI.MainDock {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        panelData: globalPanel
        layoutManager: layoutBrain
    }

    // --- Debug Controls ---
    Column {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 20
        spacing: 10

        Label { text: "3-Tier Hierarchy Debug" ; font.bold: true }
        
        Button {
            text: "Toggle Pinned Island Zoom"
            onClicked: pinnedAppsIsland.zoomEnabled = !pinnedAppsIsland.zoomEnabled
        }

        Button {
            text: "Add Item to Pinned"
            onClicked: {
                // This is a bit tricky with QList<BaseItem*> in QML directly
                // but for a demo it shows the structure
            }
        }
        
        Label { text: "Protocol: " + (globalPanel.edge === KremaUI.Krema.Bottom ? "Bottom" : "Other") }
    }
}
