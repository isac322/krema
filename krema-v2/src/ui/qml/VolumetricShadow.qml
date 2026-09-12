import QtQuick

/**
 * @brief Volumetric Shadow effect using SDF-based GPU shaders.
 * Follows Rule 11 (GPU-Accelerated Rendering).
 */
ShaderEffect {
    id: root

    property alias panelWidth: root.width
    property alias panelHeight: root.height
    
    property real cornerRadius: height / 2
    property real elevation: 15.0
    property real lightX: 0.0
    property real lightY: -200.0
    property real lightZ: 300.0
    property real lightRadius: 10.0
    property color shadowColor: Qt.rgba(0, 0, 0, 0.4)
    property real margin: 50.0

    // Shader Inputs
    property real shadowR: shadowColor.r
    property real shadowG: shadowColor.g
    property real shadowB: shadowColor.b
    property real shadowA: shadowColor.a

    anchors.centerIn: parent
    width: parent.width
    height: parent.height
    z: -1

    fragmentShader: "qrc:/outer_shadow.frag.qsb"
}
