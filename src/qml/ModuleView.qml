import QtQuick
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0

Item {
    id: moduleRoot

    // --- 🏗️ INDEPENDENT SETTINGS ---
    // The "Blueprint" says it's for apps, but we can re-label it later
    property string moduleType: "apps" 
    
    // The "Blueprint" says height is 40, but we can resize this later
    property real trayHeight: 40 
    
    // Width still hugs the content automatically
    width: contentRow.implicitWidth + (Kirigami.Units.largeSpacing * 2)
    height: Math.max(trayHeight, contentRow.implicitHeight)

    // --- 🎨 THE VISUAL TRAY ---
    Rectangle {
        id: trayBackground
        anchors.bottom: parent.bottom
        width: parent.width
        height: moduleRoot.trayHeight
        
        color: "#AA000000" // Transparent black
        radius: height / 2
        border.color: "#33FFFFFF" // Subtle white border
        border.width: 1
        z: 0
    }

    // --- 📦 THE CONTENT HOLDER ---
    Row {
        id: contentRow
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Kirigami.Units.smallSpacing
        spacing: DockSettings.iconSpacing 
        
        // Items will be added here
    }
}
