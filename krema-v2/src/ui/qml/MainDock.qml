import QtQuick
import QtQuick.Window
import QtQuick.Controls
import org.krema.ui
import org.kde.kirigami as Kirigami

/**
 * @brief Tier 1: Global Panel.
 * Orchestrates the full 3-Tier dock.
 */
Window {
    id: root

    // shellProtocol and globalPanel are injected via contextProperty (Main.cpp)
    readonly property var protocol: shellProtocol
    readonly property BasePanel panel: globalPanel
    readonly property LayoutManager brain: layoutBrain

    // FIXED: Sync with C++ HOVER_BUFFER (200px each side)
    readonly property int hoverBuffer: 200

    width: panel ? panel.width + (hoverBuffer * 2) : 0
    height: {
        let pillHeight = panel ? panel.thickness : 64;
        let pillOffset = panel ? panel.floatingOffset : 8;
        let maxZoom = brain ? brain.maxZoomFactor : 1.6;
        // Rule 12: Dynamically calculate window height to prevent clipping
        // (pillHeight * maxZoom + floatOffset + buffer)
        return (pillHeight * maxZoom) + pillOffset + 20; 
    }

    visible: false // Must be false for C++ to apply LayerShell properties!
    color: "transparent"
    flags: Qt.FramelessWindowHint 

    // --- Global Components ---
    WorkspaceSelector {
        id: workspaceSelector
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 10
    }

    // --- Context Menu (Right Click) ---
    Menu {
        id: dockMenu
        MenuItem {
            text: "Settings"
            onTriggered: {
                var component = Qt.createComponent("SettingsWindow.qml");
                if (component.status === Component.Ready) {
                    component.createObject(root);
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: dockMenu.open()
    }

    // --- Kinetic Bridge (Reference Project Zoom Physics) ---
    // Smooths the overall dock scale from 0 to 1 upon mouse entry/exit
    property bool _zoomActive: globalHover.hovered
    property real zoomIntensity: _zoomActive ? 1.0 : 0.0
    property real lastMouseX: -1000
    property real lastMouseY: -1000
    
    DockAnimator { id: zoomAnimator }
    
    onZoomIntensityChanged: {
        zoomAnimator.apply(root, "zoomIntensity", zoomIntensity)
        
        if (brain) {
            // Keep rendering the wave at the last known position while fading out
            if (zoomIntensity === 0.0) {
                brain.processHover(-1000, -1000, 0.0);
            } else {
                brain.processHover(lastMouseX, lastMouseY, zoomIntensity);
            }
        }
    }

    // --- Global Interaction Layer (Rule 5: Continuity) ---
    // Modern Pointer Handler: tracks hover without stealing clicks
    HoverHandler {
        id: globalHover
        
        onPointChanged: {
            if (brain) {
                // FIXED: Normalizing coordinate relative to visual dock start
                let mouseX = point.position.x - hoverBuffer;
                
                let pillHeight = panel ? panel.thickness : 64;
                let pillOffset = panel ? panel.floatingOffset : 8;
                let centerY = parent.height - pillOffset - (pillHeight / 2);
                
                lastMouseX = mouseX;
                lastMouseY = point.position.y - centerY;
                
                brain.processHover(lastMouseX, lastMouseY, zoomIntensity);
            }
        }
        
        onHoveredChanged: {
            if (isGeomDebug) {
                if (hovered) {
                    console.log("\u001b[32m[GEOM-CURSOR]\u001b[0m Cursor ENTERED Input Region");
                } else {
                    console.log("\u001b[31m[GEOM-CURSOR]\u001b[0m Cursor LEFT Input Region");
                }
            }
        }
    }

    // Diagnostic TapHandler: Catches taps and forwards them for mathematical hit-testing
    TapHandler {
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onTapped: (eventPoint) => {
            if (brain) {
                let mouseX = eventPoint.position.x - hoverBuffer;
                
                let pillHeight = panel ? panel.thickness : 64;
                let pillOffset = panel ? panel.floatingOffset : 8;
                let centerY = parent.height - pillOffset - (pillHeight / 2);
                
                let mouseY = eventPoint.position.y - centerY;
                
                // C++ performs circular Pythagorean hit-testing to prevent overlap bugs
                brain.processClick(mouseX, mouseY);
            }
        }
    }

    // --- Layer 0: Background ---
    VisualPanel {
        id: mainBackground
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: panel ? panel.floatingOffset : 8
        
        width: panel ? panel.width : 0
        height: panel ? panel.thickness : 64
        panelStyle: 2 // Acrylic
        
        // --- Kinetic Easing: Unified DockAnimator ---
        DockAnimator { id: panelAnimator }
        // onWidthChanged: panelAnimator.apply(mainBackground, "width", width)

        onYChanged: {
            if (brain && root.protocol && root.protocol.initialized) {
                // FIXED: Map to global and calculate absolute distance from TOP of screen
                let p = mapToGlobal(0, 0);
                let actualFloor = p.y + height;
                // Bi-Directional Telemetry (Rule 14): Yellow
                console.log("\u001b[33m[VISUAL]\u001b[0m Screen Y Floor: " + actualFloor);
            }
        }

        // --- Tier 1 Container ---
        Item {
            id: islandContainer
            anchors.fill: parent
            
            Repeater {
                model: panel ? panel.islandsVariant : []
                delegate: IslandModule {
                    islandData: modelData
                    x: modelData ? modelData.x : 0
                    width: modelData ? modelData.width : 0
                    height: parent.height
                    
                    Component.onCompleted: console.log("[QML] Island Created: " + (islandData ? islandData.islandId : "null"));
                }
                
                onCountChanged: console.log("[QML] Panel Islands Count: " + count);
            }
        }
    }

    // --- Layer 99: Debug Overlays ---
    // Rule 12: Blueprint Ghost Grid (Absolute Ruler)
    // Provides a static 10px/50px measuring grid to verify centering and drift.
    Canvas {
        id: blueprintGrid
        anchors.fill: parent
        visible: isGeomDebug
        z: 998
        opacity: 0.3
        
        onPaint: {
            let ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            
            ctx.strokeStyle = "#00ffff"; // Cyan
            ctx.lineWidth = 1;
            
            // Draw 50px major grid
            for (let x = 0; x <= width; x += 50) {
                ctx.beginPath();
                ctx.moveTo(x, 0); ctx.lineTo(x, height);
                ctx.stroke();
            }
            
            // Draw 10px minor grid
            ctx.setLineDash([2, 2]);
            for (let x = 0; x <= width; x += 10) {
                if (x % 50 === 0) continue;
                ctx.beginPath();
                ctx.moveTo(x, 0); ctx.lineTo(x, height);
                ctx.stroke();
            }
        }
    }

    Rectangle {
        id: inputRegionDebug
        visible: isGeomDebug
        color: "#44ff00ff" // Semi-transparent magenta
        border.color: "#ff00ff"
        border.width: 1
        
        // This now correctly represents the full buffered interaction area
        anchors.fill: parent
        
        z: 999
        
        Text {
            text: "INPUT REGION (BUFFERED)"
            color: "white"
            font.pixelSize: 10
            anchors.centerIn: parent
        }
    }
    
    Component.onCompleted: {
        if (brain) {
            brain.updateLayout();
        }
    }
}
