import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property real lightX: sliderLX.value
    property real lightY: sliderLY.value
    property real lightZ: sliderLZ.value
    property real elevation: sliderElev.value
    property real lightRadius: sliderLR.value
    property real shadowIntensity: sliderIntensity.value
    property real opacity_: sliderOpac.value

    function computeMargin() {
        var denom = lightZ - elevation
        if (denom < 1) denom = 1
        var lightDist = Math.sqrt(lightX * lightX + lightY * lightY)
        var physicalMargin = (lightDist + lightRadius) * elevation / denom
        var softnessMargin = lightRadius * 3.0
        return Math.min(Math.max(physicalMargin, softnessMargin) + 10, 200)
    }

    // Background checkerboard
    Rectangle { anchors.fill: parent; color: "#c8c8c8" }
    Grid {
        anchors.fill: parent; columns: Math.ceil(width / 24); rows: Math.ceil(height / 24)
        Repeater {
            model: Math.ceil(root.width / 24) * Math.ceil(root.height / 24)
            Rectangle {
                width: 24; height: 24
                color: (Math.floor(index / Math.ceil(root.width / 24)) + index) % 2 === 0
                       ? "#c0c0c0" : "#d0d0d0"
            }
        }
    }

    // ═══ Panel + Shadow ═══
    Item {
        id: stage
        anchors.horizontalCenter: parent.horizontalCenter
        y: 10; width: parent.width; height: parent.height - controlPane.height

        ShaderEffect {
            id: shadow
            property real _margin: root.computeMargin()

            x: panel.x - _margin
            y: panel.y - _margin
            width:  panel.width  + _margin * 2
            height: panel.height + _margin * 2

            property real panelWidth:  panel.width
            property real panelHeight: panel.height
            property real cornerRadius: panel.radius
            property real elevation: root.elevation
            property real lightX: root.lightX
            property real lightY: root.lightY
            property real lightZ: root.lightZ
            property real lightRadius: root.lightRadius
            property real shadowR: 0.0
            property real shadowG: 0.0
            property real shadowB: 0.0
            property real shadowA: root.shadowIntensity
            property real margin: _margin

            fragmentShader: "qrc:/shaders/outer_shadow.frag.qsb"
        }

        Rectangle {
            id: panel
            anchors.centerIn: parent
            width: 320; height: 80; radius: 16
            color: Qt.rgba(1, 1, 1, root.opacity_)
            border.color: Qt.rgba(0, 0, 0, 0.12); border.width: 1
            Text {
                anchors.centerIn: parent; font.pixelSize: 14; color: "#444"
                text: "Panel  (elev " + root.elevation.toFixed(0) + ")"
            }
        }

        Rectangle {
            width: 22; height: 22; radius: 11
            color: "#FFD700"; border.color: "#E08000"; border.width: 2
            x: panel.x + panel.width/2 + root.lightX * 0.4 - 11
            y: panel.y + panel.height/2 + root.lightY * 0.4 - 11
            scale: Math.max(0.4, 1.5 - root.lightZ / 300)
            z: 10
            Text { anchors.centerIn: parent; text: "☀"; font.pixelSize: 11 }
        }
    }

    // ═══ Controls ═══
    Rectangle {
        id: controlPane
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: 340; color: "#f0f0f0"

        GridLayout {
            anchors.fill: parent; anchors.margins: 14
            columns: 4; columnSpacing: 10; rowSpacing: 4

            Label { text: "Light X"; font.bold: true; Layout.preferredWidth: 120 }
            Slider { id: sliderLX; Layout.fillWidth: true; from: -300; to: 300; value: 0 }
            Label { text: sliderLX.value.toFixed(0); Layout.preferredWidth: 50; font.family: "monospace" }
            Label { text: "shadow direction"; color: "#888"; Layout.preferredWidth: 170 }

            Label { text: "Light Y"; font.bold: true }
            Slider { id: sliderLY; Layout.fillWidth: true; from: -300; to: 300; value: -150 }
            Label { text: sliderLY.value.toFixed(0); font.family: "monospace" }
            Label { text: "above(-) / below(+)"; color: "#888" }

            Label { text: "Light Z (height)"; font.bold: true }
            Slider { id: sliderLZ; Layout.fillWidth: true; from: 100; to: 2000; value: 500 }
            Label { text: sliderLZ.value.toFixed(0); font.family: "monospace" }
            Label { text: "projection scale"; color: "#888" }

            Label { text: "Elevation"; font.bold: true }
            Slider { id: sliderElev; Layout.fillWidth: true; from: 1; to: 50; value: 15 }
            Label { text: sliderElev.value.toFixed(0); font.family: "monospace" }
            Label { text: "dock float height"; color: "#888" }

            Label { text: "Light Radius"; font.bold: true; color: "#c04000" }
            Slider { id: sliderLR; Layout.fillWidth: true; from: 0.5; to: 20; stepSize: 0.5; value: 5.0 }
            Label { text: sliderLR.value.toFixed(1) + "px"; font.family: "monospace"; color: "#c04000" }
            Label { text: "bigger = softer shadow (σ)"; color: "#c04000" }

            Label { text: "Intensity"; font.bold: true }
            Slider { id: sliderIntensity; Layout.fillWidth: true; from: 0; to: 1; stepSize: 0.05; value: 0.30 }
            Label { text: (sliderIntensity.value * 100).toFixed(0) + "%"; font.family: "monospace" }
            Label { text: "shadow darkness"; color: "#888" }

            Label { text: "Panel Opacity"; font.bold: true }
            Slider { id: sliderOpac; Layout.fillWidth: true; from: 0; to: 1; stepSize: 0.05; value: 0.75 }
            Label { text: (sliderOpac.value * 100).toFixed(0) + "%"; font.family: "monospace" }
            Label { text: ""; color: "#888" }

            Rectangle { Layout.columnSpan: 4; Layout.fillWidth: true; height: 1; color: "#ccc" }
            Label { text: "Result"; font.bold: true; color: "#06c" }
            Label {
                Layout.columnSpan: 3; font.family: "monospace"; color: "#06c"
                text: {
                    var sigma = Math.max(root.lightRadius, 0.5)
                    "σ=" + sigma.toFixed(1)
                    + "  3σ=" + (sigma * 3).toFixed(0) + "px spread"
                    + "  |  margin=" + root.computeMargin().toFixed(0)
                }
            }
        }
    }
}
