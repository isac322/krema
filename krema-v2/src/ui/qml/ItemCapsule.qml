import QtQuick
import QtQuick.Controls
import org.krema.ui
import org.kde.kirigami as Kirigami

/**
 * @brief Tier 3: Atomic Capsule.
 * Renders a single item (App/Widget) with zoom and notifications.
 */
Item {
    id: root
    
    // Linked C++ Object
    property BaseItem itemData
    
    width: itemData ? itemData.contentSize * itemData.scaleFactor : 48
    height: width
    
    x: itemData ? itemData.x : 0
    y: itemData ? itemData.y : 0
    
    // --- Animation: Kinetic Easing ---
    // Rule 19: Smooths out instant C++ layout updates for Pixel-Perfect hitboxes
    DockAnimator { id: animator }
    
    // onWidthChanged: animator.apply(root, "width", width)
    
    // Rule 14: Discrepancy Alarm (Truth vs. Reality Check)
    onXChanged: {
        animator.apply(root, "x", x)
        
        // Suppress alarm if we are animating toward the target
        /*
        if (isGeomDebug && itemData) {
            let intendedX = itemData.x;
            let actualX = x;
            let delta = Math.abs(intendedX - actualX);
            if (delta > 1.0) {
                console.log("\u001b[31;1m[GEOM-ALARM]\u001b[0m DISCREPANCY DETECTED! Task: " + itemData.taskId + " | Intended: " + intendedX + " | Actual: " + actualX + " | Delta: " + delta + "px");
            }
        }
        */
    }

    // --- Layer 0: Slot Background ---
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        visible: true 

        // DEBUG: Text fallback if icon fails
        Text {
            anchors.centerIn: parent
            text: itemData ? (itemData.taskName ? itemData.taskName[0] : "!") : "?"
            color: "white"
            font.pixelSize: 20
            visible: iconLayer.status !== Kirigami.Icon.Ready
        }
    }
    
    // --- Layer 99: Debug Overlay ---
    Rectangle {
        id: itemDebug
        visible: isGeomDebug
        anchors.fill: parent
        color: "#44ff00ff" // Semi-transparent magenta
        border.color: "magenta"
        border.width: 1
        z: 999
        
        Text {
            text: itemData ? itemData.taskId : "?"
            color: "magenta"
            font.pixelSize: 8
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    Rectangle {
        id: hitDebug
        visible: isHitDebug
        anchors.fill: parent
        color: "#22ffff00" // Transparent yellow
        border.color: "yellow"
        border.width: 1
        z: 1000

        // Rule 3: Stable Virtual Origin Marker
        Rectangle {
            width: 2; height: parent.height + 20
            color: "yellow"
            anchors.centerIn: parent
            opacity: 0.5
        }

        Text {
            text: "DIST: " + Math.round(Math.abs(globalHover.point.position.x - (root.x + root.width/2))) + "px"
            color: "yellow"
            font.pixelSize: 8
            font.bold: true
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    Component.onCompleted: {
        // Rule 12: The Math Auditor (5-Unit Stack Dump)
        if (isGeomDebug && itemData) {
            let pillHeight = panel ? panel.thickness : 64;
            let pillOffset = panel ? panel.floatingOffset : 8;
            let visualH = itemData.contentSize * itemData.scaleFactor;
            
            console.log("\u001b[34;1m[MATH-AUDITOR]\u001b[0m '" + itemData.taskName + "' | " +
                "SLOT X:" + Math.round(x) + " W:" + Math.round(width) + " | " +
                "ICON Size:" + itemData.contentSize + " Scale:" + itemData.scaleFactor.toFixed(2) + " | " +
                "FLOOR:" + pillOffset + " PILL:" + pillHeight + " TOTAL_H:" + Math.round(root.height));
        }
        
        if (isGeomDebug) {
            console.log("[QML] ItemCapsule Created: " + (itemData ? itemData.taskId : "null"));
        }
    }

    // --- Layer 1: Content (The Real Icon) ---
    Kirigami.Icon {
        id: iconLayer
        
        // FIXED: Rasterization Trap (Rule 1: Fixed Ceiling)
        // We force a high-res 128px render once and let the GPU handle the 
        // sub-pixel scaling. This prevents the "snapping" jitter.
        width: 128
        height: 128
        anchors.centerIn: parent
        
        source: itemData ? (itemData.taskIcon || itemData.taskId || "application-x-executable") : "unknown"
        smooth: true
        antialiasing: true
        
        // GPU-Native Transform for liquid smoothness
        transform: Scale {
            id: iconScale
            origin.x: 64; origin.y: 64
            xScale: ((itemData ? itemData.contentSize * itemData.scaleFactor - 8 : 40) * (itemData ? itemData.normalizationScale : 1.0)) / 128
            yScale: xScale
        }
        
        // Rule 18: Legibility and visual state
        opacity: itemData && itemData.isRunning ? 1.0 : 0.6
        Behavior on opacity { NumberAnimation { duration: 200 } }
        
        onStatusChanged: {
            if (status === Kirigami.Icon.Error) {
                console.log("[ICON ERROR] Failed to load: " + source);
            }
        }
    }

    // M7: Icon-Label mode
    Text {
        anchors.top: iconLayer.bottom
        anchors.horizontalCenter: iconLayer.horizontalCenter
        anchors.topMargin: -20
        text: itemData ? itemData.taskName : ""
        visible: kremaSettings.layoutMode === 1
        color: "white"
        font.pixelSize: 12
        font.bold: true
        z: 10
    }

    // --- Layer 2: Attention & Badges ---
    AttentionOverlay {
        anchors.fill: parent
        itemData: root.itemData
    }

    // --- Interaction Layer ---
    // Rule 5: Hit-testing is now centralized in C++ (LayoutManager) for Pixel-Perfect circular gaps.
    // Standard QML HoverHandler/TapHandler are removed here to prevent rectangular overlap bugs.

    // Tooltip for task name
    ToolTip.visible: brain && brain.hoveredItem === itemData && itemData.taskName
    ToolTip.text: itemData ? itemData.taskName : ""
    ToolTip.delay: 500
}
