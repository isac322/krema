import QtQuick
import QtQuick.Window
import org.kde.layershell

Window {
    visible: true
    width: 100
    height: 100
    
    // Use the exported name LayerShell
    LayerShell.layer: LayerShell.LayerTop
    LayerShell.anchors: LayerShell.AnchorBottom
}
