import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    width: 600; height: 400; visible: true; title: "Krema Icon Sandbox"

    GridLayout {
        anchors.centerIn: parent
        columns: 4
        rowSpacing: 20; columnSpacing: 20

        // Test 1: Standard System Theme
        Image { source: "image://taskicon/firefox"; width: 64; height: 64 }
        
        // Test 2: Steam Game (Marvel Rivals)
        Image { source: "image://taskicon/steam_app_2767030"; width: 64; height: 64 }
        
        // Test 3: Dragged Steam Shortcut (The dirty ID)
        Image { source: "image://taskicon/steam_app_2767030.desktop"; width: 64; height: 64 }
        
        // Test 4: Absolute Garbage (To test your fallback logic)
        Image { source: "image://taskicon/this_app_does_not_exist"; width: 64; height: 64 }
    }
}
