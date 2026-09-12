import QtQuick
import org.kde.kirigami as Kirigami
import org.krema.ui

/**
 * @brief Layer 0: Background Panel.
 * Implements the 4 core styles: Adaptive, Tinted, Acrylic, Mica.
 * Follows Rule 18 (Visual Style Constitution).
 */
Item {
    id: root
    
    // Direct access to panel for stability
    readonly property BasePanel panel: globalPanel

    default property alias content: contentArea.data

    enum Style {
        Adaptive,
        Tinted,
        Acrylic,
        Mica
    }

    property int panelStyle: 2 // Acrylic
    property color tintColor: Kirigami.Theme.backgroundColor
    property real tintOpacity: 0.8
    
    // Decoupled Dimensions (Rule 6)
    property real panelThickness: panel ? panel.thickness : 64
    property real cornerRadius: panelThickness / 2

    // --- Layer -1: Volumetric Shadow ---
    VolumetricShadow {
        id: shadowEffect
        anchors.fill: parent
        cornerRadius: root.cornerRadius
        elevation: 15
        opacity: root.opacity
    }

    // --- Layer 0: Background Pill ---
    Rectangle {
        id: backgroundPill
        anchors.fill: parent
        radius: root.cornerRadius
        
        color: {
            if (panelStyle === 1 /* Tinted */) {
                return Qt.rgba(tintColor.r, tintColor.g, tintColor.b, tintOpacity);
            }
            if (panelStyle === 0 /* Adaptive */) {
                return Kirigami.Theme.backgroundColor;
            }
            // Fallback for Acrylic/Mica until real blur is active
            return Qt.rgba(Kirigami.Theme.backgroundColor.r, 
                           Kirigami.Theme.backgroundColor.g, 
                           Kirigami.Theme.backgroundColor.b, 0.7); 
        }
    }
    
    // --- Content Area ---
    Item {
        id: contentArea
        anchors.fill: parent
    }
}
