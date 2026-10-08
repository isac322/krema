// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

FormCard.FormCardPage {
    id: page

    title: i18n("Shadow")

    // Responsive vertical padding: scales with page width
    topPadding: Math.round(Kirigami.Units.gridUnit * Math.max(0.5, Math.min(1.5, width / 800)))
    bottomPadding: topPadding

    // Reference panel size (real pixels) shared by the light canvas and the
    // shadow preview, so both show the same projection the dock renders.
    readonly property real refPanelWidth: Kirigami.Units.gridUnit * 13
    readonly property real refPanelHeight: Kirigami.Units.gridUnit * 2.75

    // Setters clamp to the kcfg ranges and the master slider granularity.
    function setLightPosition(lx, ly) {
        DockSettings.shadowLightX = Math.max(-300, Math.min(300, Math.round(lx)))
        DockSettings.shadowLightY = Math.max(-300, Math.min(300, Math.round(ly)))
    }
    function setLightHeight(lz) {
        DockSettings.shadowLightZ = Math.max(100, Math.min(2000, Math.round(lz)))
    }
    function setLightRadius(r) {
        DockSettings.shadowLightRadius = Math.max(0.5, Math.min(20.0, Math.round(r * 2) / 2))
    }
    function setElevation(e) {
        DockSettings.shadowElevation = Math.max(1, Math.min(50, Math.round(e)))
    }

    // The dock's own projective shadow shader (see main.qml dockShadow), drawn
    // around a panel of panelWidth x panelHeight centered in this item.
    component ProjectedShadow: ShaderEffect {
        property real panelWidth
        property real panelHeight
        property real cornerRadius
        property real elevation: DockSettings.shadowElevation
        property real lightX: DockSettings.shadowLightX
        property real lightY: DockSettings.shadowLightY
        property real lightZ: DockSettings.shadowLightZ
        property real lightRadius: DockSettings.shadowLightRadius
        property color _shadowColor: DockSettings.shadowColor
        property real shadowR: _shadowColor.r
        property real shadowG: _shadowColor.g
        property real shadowB: _shadowColor.b
        property real shadowA: DockSettings.shadowIntensity
        property real margin: {
            const denom = Math.max(lightZ - elevation, 1)
            const lightDist = Math.sqrt(lightX * lightX + lightY * lightY)
            const physicalMargin = (lightDist + lightRadius) * elevation / denom
            const softnessMargin = lightRadius * 3.0
            return Math.min(Math.max(physicalMargin, softnessMargin) + 10, 200)
        }

        width: panelWidth + margin * 2
        height: panelHeight + margin * 2
        fragmentShader: "qrc:/qml/shaders/outer_shadow.frag.qsb"
        Accessible.ignored: true
    }

    FormSection {
        FormCard.FormSwitchDelegate {
            text: i18n("Enable shadow")
            checked: DockSettings.shadowEnabled
            onToggled: DockSettings.shadowEnabled = checked
        }
    }

    // --- 2D light source canvas + preview ---
    FormCard.FormHeader {
        title: i18n("Light & Shadow")
        visible: DockSettings.shadowEnabled
    }

    FormSection {
        visible: DockSettings.shadowEnabled

        FormCard.AbstractFormDelegate {
            background: null
            focusPolicy: Qt.NoFocus
            Accessible.ignored: true
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                // Top-down view of the screen plane. The dock sits at the
                // origin; Light X/Y (-300..300 px from the panel center) map to
                // the canvas extents. Light height is the light disc's size,
                // light radius its glow ring.
                Item {
                    id: canvas
                    Layout.fillWidth: true
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 14

                    readonly property real pad: Kirigami.Units.gridUnit * 1.5
                    readonly property real centerX: width / 2
                    readonly property real centerY: height / 2
                    readonly property real scaleX: Math.max(0.01, (width - pad * 2) / 600)
                    readonly property real scaleY: Math.max(0.01, (height - pad * 2) / 600)

                    readonly property real lightCx: centerX + DockSettings.shadowLightX * scaleX
                    readonly property real lightCy: centerY + DockSettings.shadowLightY * scaleY
                    // Higher light = closer to the viewer = larger disc
                    readonly property real discRadius: Kirigami.Units.gridUnit
                        * (0.5 + 0.9 * (DockSettings.shadowLightZ - 100) / 1900)
                    readonly property real haloPerUnit: Kirigami.Units.gridUnit * 1.4 / 20
                    readonly property real outerRadius: discRadius + Math.max(2, DockSettings.shadowLightRadius * haloPerUnit)
                    readonly property real grabTolerance: Kirigami.Units.smallSpacing * 1.5

                    function toLightX(mx) { return (mx - centerX) / scaleX }
                    function toLightY(my) { return (my - centerY) / scaleY }

                    activeFocusOnTab: true
                    Accessible.role: Accessible.Slider
                    Accessible.name: i18n("Light position")
                    Accessible.description: i18nc("@info light source state and keyboard help",
                        "X %1, Y %2, height %3, radius %4. Arrow keys move the light (Shift for larger steps), Page Up and Page Down change its height, plus and minus change its radius.",
                        DockSettings.shadowLightX, DockSettings.shadowLightY,
                        DockSettings.shadowLightZ, DockSettings.shadowLightRadius.toFixed(1))
                    Accessible.focusable: true
                    Accessible.focused: activeFocus

                    Keys.onPressed: (event) => {
                        const step = (event.modifiers & Qt.ShiftModifier) ? 50 : 10
                        const lx = DockSettings.shadowLightX
                        const ly = DockSettings.shadowLightY
                        let handled = true
                        switch (event.key) {
                        case Qt.Key_Left: page.setLightPosition(lx - step, ly); break
                        case Qt.Key_Right: page.setLightPosition(lx + step, ly); break
                        case Qt.Key_Up: page.setLightPosition(lx, ly - step); break
                        case Qt.Key_Down: page.setLightPosition(lx, ly + step); break
                        case Qt.Key_PageUp: page.setLightHeight(DockSettings.shadowLightZ + 50); break
                        case Qt.Key_PageDown: page.setLightHeight(DockSettings.shadowLightZ - 50); break
                        case Qt.Key_Plus:
                        case Qt.Key_Equal: page.setLightRadius(DockSettings.shadowLightRadius + 0.5); break
                        case Qt.Key_Minus:
                        case Qt.Key_Underscore: page.setLightRadius(DockSettings.shadowLightRadius - 0.5); break
                        default: handled = false
                        }
                        event.accepted = handled
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: Kirigami.Theme.alternateBackgroundColor
                        radius: Kirigami.Units.largeSpacing
                    }

                    Item {
                        id: canvasContent
                        anchors.fill: parent
                        clip: true

                        // Mouse: drag the light (X/Y), its outer ring (radius),
                        // the dock (elevation); click empty space to place the
                        // light; wheel over the light = height, Shift+wheel =
                        // radius. Wheel elsewhere falls through to page scroll.
                        MouseArea {
                            id: canvasMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton

                            property string mode: ""
                            property string hoverMode: ""
                            property real grabDx: 0
                            property real grabDy: 0
                            property real startY: 0
                            property real startElevation: 0
                            property real wheelAccumulator: 0

                            readonly property string activeMode: pressed ? mode : hoverMode
                            cursorShape: {
                                switch (activeMode) {
                                case "move": return pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                case "radius": return Qt.SizeAllCursor
                                case "elevation": return Qt.SizeVerCursor
                                default: return Qt.CrossCursor
                                }
                            }

                            function hitTest(mx, my) {
                                const d = Math.hypot(mx - canvas.lightCx, my - canvas.lightCy)
                                if (d >= canvas.discRadius * 0.6 && Math.abs(d - canvas.outerRadius) <= canvas.grabTolerance)
                                    return "radius"
                                if (d < canvas.outerRadius)
                                    return "move"
                                if (mx >= dockRect.x && mx <= dockRect.x + dockRect.width
                                        && my >= dockRect.y - canvas.grabTolerance
                                        && my <= dockRect.y + dockRect.height + canvas.grabTolerance)
                                    return "elevation"
                                return ""
                            }

                            onPressed: (mouse) => {
                                canvas.forceActiveFocus(Qt.MouseFocusReason)
                                mode = hitTest(mouse.x, mouse.y)
                                if (mode === "") {
                                    page.setLightPosition(canvas.toLightX(mouse.x), canvas.toLightY(mouse.y))
                                    mode = "move"
                                    grabDx = 0
                                    grabDy = 0
                                } else if (mode === "move") {
                                    grabDx = mouse.x - canvas.lightCx
                                    grabDy = mouse.y - canvas.lightCy
                                } else if (mode === "elevation") {
                                    startY = mouse.y
                                    startElevation = DockSettings.shadowElevation
                                }
                            }
                            onPositionChanged: (mouse) => {
                                if (!pressed) {
                                    hoverMode = hitTest(mouse.x, mouse.y)
                                    return
                                }
                                if (mode === "move") {
                                    page.setLightPosition(canvas.toLightX(mouse.x - grabDx), canvas.toLightY(mouse.y - grabDy))
                                } else if (mode === "radius") {
                                    const d = Math.hypot(mouse.x - canvas.lightCx, mouse.y - canvas.lightCy)
                                    page.setLightRadius((d - canvas.discRadius) / canvas.haloPerUnit)
                                } else if (mode === "elevation") {
                                    page.setElevation(startElevation + (startY - mouse.y) / Math.max(1, elevationSlider.height) * 49)
                                }
                            }
                            onReleased: mode = ""
                            onCanceled: mode = ""
                            onExited: hoverMode = ""

                            onWheel: (wheel) => {
                                const d = Math.hypot(wheel.x - canvas.lightCx, wheel.y - canvas.lightCy)
                                const delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x
                                if (d > canvas.outerRadius + canvas.grabTolerance || delta === 0) {
                                    wheelAccumulator = 0
                                    wheel.accepted = false
                                    return
                                }
                                wheel.accepted = true
                                wheelAccumulator += delta
                                const notches = wheelAccumulator > 0 ? Math.floor(wheelAccumulator / 120)
                                                                     : Math.ceil(wheelAccumulator / 120)
                                if (notches === 0)
                                    return
                                wheelAccumulator -= notches * 120
                                if (wheel.modifiers & Qt.ShiftModifier)
                                    page.setLightRadius(DockSettings.shadowLightRadius + 0.5 * notches)
                                else
                                    page.setLightHeight(DockSettings.shadowLightZ + 50 * notches)
                            }
                        }

                        // Projected shadow, computed in real pixels and scaled
                        // into canvas space (the shader is evaluated per output
                        // pixel, so the scale stays exact).
                        ProjectedShadow {
                            id: canvasShadow
                            panelWidth: page.refPanelWidth
                            panelHeight: page.refPanelHeight
                            cornerRadius: Math.min(DockSettings.cornerRadius, page.refPanelHeight / 2)
                            x: canvas.centerX - width / 2 * canvas.scaleX
                            y: canvas.centerY - height / 2 * canvas.scaleY
                            transform: Scale {
                                xScale: canvas.scaleX
                                yScale: canvas.scaleY
                            }
                        }

                        // Dock at the origin (drag vertically for elevation)
                        Rectangle {
                            id: dockRect
                            width: page.refPanelWidth * canvas.scaleX
                            height: Math.max(Kirigami.Units.smallSpacing * 3, page.refPanelHeight * canvas.scaleY)
                            x: canvas.centerX - width / 2
                            y: canvas.centerY - height / 2
                            radius: Math.min(height / 2, DockSettings.cornerRadius * canvas.scaleY)
                            color: Kirigami.Theme.highlightColor
                            border.width: canvasMouse.activeMode === "elevation" ? 2 : 0
                            border.color: Kirigami.Theme.textColor

                            QQC2.Label {
                                anchors.centerIn: parent
                                text: i18n("Dock")
                                font: Kirigami.Theme.smallFont
                                color: Kirigami.Theme.highlightedTextColor
                                visible: parent.height >= implicitHeight
                                Accessible.ignored: true
                            }
                        }

                        // Vertical elevation handle beside the dock
                        QQC2.Slider {
                            id: elevationSlider
                            orientation: Qt.Vertical
                            x: dockRect.x + dockRect.width + Kirigami.Units.smallSpacing
                            anchors.verticalCenter: dockRect.verticalCenter
                            height: Kirigami.Units.gridUnit * 4.5
                            from: 1; to: 50; stepSize: 1
                            snapMode: QQC2.Slider.SnapAlways
                            value: DockSettings.shadowElevation
                            onMoved: page.setElevation(value)
                            Accessible.name: i18n("Panel elevation")
                            Accessible.description: String(DockSettings.shadowElevation)

                            QQC2.ToolTip.visible: hovered || pressed
                            QQC2.ToolTip.delay: pressed ? 0 : Kirigami.Units.toolTipDelay
                            QQC2.ToolTip.text: i18nc("@info:tooltip", "Panel elevation: %1", DockSettings.shadowElevation)
                        }

                        // Light source: glow ring (radius) + disc (height)
                        Item {
                            id: lightSource
                            x: canvas.lightCx - width / 2
                            y: canvas.lightCy - height / 2
                            width: canvas.outerRadius * 2
                            height: width

                            Rectangle {
                                id: halo
                                anchors.fill: parent
                                radius: width / 2
                                color: Kirigami.ColorUtils.adjustColor(Kirigami.Theme.neutralTextColor, { alpha: -200 })
                                border.width: canvasMouse.activeMode === "radius" ? 3 : 1.5
                                border.color: canvasMouse.activeMode === "radius"
                                    ? Kirigami.Theme.highlightColor
                                    : Kirigami.Theme.neutralTextColor
                            }

                            Rectangle {
                                id: lightDisc
                                anchors.centerIn: parent
                                width: canvas.discRadius * 2
                                height: width
                                radius: width / 2
                                color: Kirigami.Theme.neutralTextColor
                                border.width: 1
                                border.color: Kirigami.Theme.textColor

                                Kirigami.Icon {
                                    anchors.centerIn: parent
                                    width: parent.width * 0.7
                                    height: width
                                    source: "weather-clear"
                                    Accessible.ignored: true
                                }
                            }
                        }

                        // Current values readout
                        QQC2.Label {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.margins: Kirigami.Units.smallSpacing
                            text: i18nc("@info light source values", "X %1 · Y %2 · Height %3 · Radius %4 · Elevation %5",
                                        DockSettings.shadowLightX, DockSettings.shadowLightY,
                                        DockSettings.shadowLightZ, DockSettings.shadowLightRadius.toFixed(1),
                                        DockSettings.shadowElevation)
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor
                            Accessible.ignored: true
                        }
                    }

                    // Keyboard focus ring
                    Rectangle {
                        anchors.fill: parent
                        radius: Kirigami.Units.largeSpacing
                        color: "transparent"
                        border.width: 2
                        border.color: Kirigami.Theme.highlightColor
                        visible: canvas.activeFocus
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Drag the light to set its direction, drag its outer ring to change softness, and scroll over it to change its height (Shift+scroll changes softness). Drag the dock or use the slider beside it to change the panel elevation.")
                    wrapMode: Text.Wrap
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    Accessible.ignored: true
                }

                // Shadow result preview (the dock's real shader at real size)
                Rectangle {
                    id: shadowPreview
                    Layout.fillWidth: true
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 8
                    radius: Kirigami.Units.largeSpacing
                    clip: true
                    gradient: Gradient {
                        GradientStop {
                            position: 0.0
                            color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.alternateBackgroundColor, Kirigami.Theme.highlightColor, 0.15)
                        }
                        GradientStop {
                            position: 1.0
                            color: Kirigami.Theme.alternateBackgroundColor
                        }
                    }
                    Accessible.role: Accessible.Graphic
                    Accessible.name: i18n("Shadow preview")

                    ProjectedShadow {
                        panelWidth: previewPanel.width
                        panelHeight: previewPanel.height
                        cornerRadius: previewPanel.radius
                        x: previewPanel.x - margin
                        y: previewPanel.y - margin
                    }

                    Rectangle {
                        id: previewPanel
                        anchors.centerIn: parent
                        width: page.refPanelWidth
                        height: page.refPanelHeight
                        radius: Math.min(DockSettings.cornerRadius, height / 2)
                        color: Kirigami.Theme.backgroundColor
                        border.width: 1
                        border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.15)

                        Row {
                            anchors.centerIn: parent
                            spacing: Kirigami.Units.smallSpacing
                            Repeater {
                                model: ["system-file-manager", "internet-web-browser", "utilities-terminal", "applications-multimedia", "preferences-system"]
                                delegate: Kirigami.Icon {
                                    required property string modelData
                                    width: Math.round(previewPanel.height * 0.7)
                                    height: width
                                    source: modelData
                                    Accessible.ignored: true
                                }
                            }
                        }
                    }
                }
            }
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
            text: i18n("Shadow intensity")
            from: 0.0; to: 1.0; stepSize: 0.05
            value: DockSettings.shadowIntensity
            valueText: i18nc("@label percentage", "%1%", Math.round(value * 100))
            onMoved: (value) => DockSettings.shadowIntensity = value
        }

        FormCard.FormDelegateSeparator {}

        FormCard.AbstractFormDelegate {
            id: shadowColorDelegate
            background: null
            // The swatch is the focusable, accessible control; clicking the
            // row opens the same dialog.
            focusPolicy: Qt.NoFocus
            Accessible.ignored: true
            onClicked: shadowSwatch.clicked()
            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Shadow color")
                    elide: Text.ElideRight
                    color: shadowColorDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    Accessible.ignored: true
                }

                ColorSwatchButton {
                    id: shadowSwatch
                    accessibleName: i18n("Shadow color")
                    dialogTitle: i18n("Choose shadow color")
                    showAlphaChannel: true
                    color: DockSettings.shadowColor
                    onPicked: (color) => DockSettings.shadowColor = color.toString()
                }
            }
        }
    }

    // --- Advanced settings (progressive disclosure: exact light values) ---
    // Panel elevation and shadow intensity live only above (canvas slider and
    // the slider below the preview) so every setting appears exactly once.
    FormSection {
        visible: DockSettings.shadowEnabled

        FormCard.AbstractFormDelegate {
            id: advancedToggle
            property bool expanded: false

            text: i18n("Advanced settings")
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.Button
            Accessible.name: text
            Accessible.description: expanded ? i18nc("@info:status expander", "Expanded")
                                             : i18nc("@info:status expander", "Collapsed")
            onClicked: expanded = !expanded
            Keys.onReturnPressed: expanded = !expanded
            Keys.onEnterPressed: expanded = !expanded

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: advancedToggle.text
                        color: Kirigami.Theme.textColor
                        Accessible.ignored: true
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Exact values for the light position, height and radius")
                        wrapMode: Text.Wrap
                        font: Kirigami.Theme.smallFont
                        color: Kirigami.Theme.disabledTextColor
                        Accessible.ignored: true
                    }
                }
                Kirigami.Icon {
                    source: advancedToggle.expanded ? "arrow-up" : "arrow-down"
                    implicitWidth: Kirigami.Units.iconSizes.small
                    implicitHeight: Kirigami.Units.iconSizes.small
                    Accessible.ignored: true
                }
            }
        }

        FormCard.FormDelegateSeparator { visible: advancedToggle.expanded }

        SliderDelegate {
            visible: advancedToggle.expanded
            text: i18n("Light X")
            from: -300; to: 300; stepSize: 10
            value: DockSettings.shadowLightX
            onMoved: (value) => DockSettings.shadowLightX = value
        }

        FormCard.FormDelegateSeparator { visible: advancedToggle.expanded }

        SliderDelegate {
            visible: advancedToggle.expanded
            text: i18n("Light Y")
            from: -300; to: 300; stepSize: 10
            value: DockSettings.shadowLightY
            onMoved: (value) => DockSettings.shadowLightY = value
        }

        FormCard.FormDelegateSeparator { visible: advancedToggle.expanded }

        SliderDelegate {
            visible: advancedToggle.expanded
            text: i18n("Light height")
            from: 100; to: 2000; stepSize: 50
            value: DockSettings.shadowLightZ
            onMoved: (value) => DockSettings.shadowLightZ = value
        }

        FormCard.FormDelegateSeparator { visible: advancedToggle.expanded }

        SliderDelegate {
            visible: advancedToggle.expanded
            text: i18n("Light radius")
            from: 0.5; to: 20.0; stepSize: 0.5
            value: DockSettings.shadowLightRadius
            valueText: value.toFixed(1)
            onMoved: (value) => DockSettings.shadowLightRadius = value
        }
    }
}
