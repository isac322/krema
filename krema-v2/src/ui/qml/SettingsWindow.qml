import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Settings Window for configuring Krema Dock.
 * Real-time bindings to kremaSettings.
 */
ApplicationWindow {
    id: root
    width: 400
    height: 500
    title: "Krema Settings"
    visible: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10

        Text { text: "Geometry"; font.bold: true }

        RowLayout {
            Text { text: "Dock Height" }
            Slider {
                from: 32; to: 128
                value: kremaSettings.dockHeight
                onValueChanged: kremaSettings.dockHeight = value
            }
        }

        RowLayout {
            Text { text: "Item Spacing" }
            Slider {
                from: 0; to: 32
                value: kremaSettings.itemSpacing
                onValueChanged: kremaSettings.itemSpacing = value
            }
        }

        Text { text: "Behavior"; font.bold: true }

        CheckBox {
            text: "Label Mode"
            checked: kremaSettings.layoutMode === 1
            onClicked: kremaSettings.layoutMode = checked ? 1 : 0
        }
        
        CheckBox {
            text: "Geometry Debug"
            checked: kremaSettings.debugGeom
            onClicked: kremaSettings.debugGeom = checked
        }
    }
}
