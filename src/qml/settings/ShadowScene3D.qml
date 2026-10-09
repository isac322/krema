// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick3D
import org.kde.kirigami as Kirigami
import com.bhyoo.krema 1.0

// Interactive 3D stage for the shadow page: the bottom of the user's screen
// (real wallpaper) seen in three-quarter perspective, the dock floating above
// it with its real icons on top, and a glowing lamp handle whose light casts
// the dock's shadow onto the wallpaper.
//
// World units are real pixels: the dock sits at the origin, Light X/Y are
// offsets from its centre on the screen plane (scene X / Z; Y negative =
// towards the top of the screen = far side), the panel elevation is scene Y.
// The key light is a directional light aimed from the configured light
// position (real height) at the dock, so the shadow offset matches the
// dock's projective shader. Only the lamp handle's drawn height is
// compressed so the 100..2000 px range stays in frame.
//
// Rendering: everything is unlit (exact wallpaper, icon and theme colors)
// except a white shadow-catcher plane multiplied over the wallpaper. The key
// light is normalized so lit catcher pixels are exactly white (no change)
// and shadowed pixels darken by the shadow factor.
//
// Mouse: drag the lamp to move it, wheel = light height, Shift+wheel =
// softness, drag the dock vertically = elevation, drag empty space to orbit,
// double-click to reset the camera.
Item {
    id: root

    // Pauses the lamp glow pulse while the window is hidden or minimized.
    property bool active: true

    // Orbit camera angles in degrees (clamped; see the mouse area below).
    readonly property real defaultYaw: 12
    readonly property real defaultPitch: 24
    property real cameraYaw: defaultYaw
    property real cameraPitch: defaultPitch

    implicitWidth: Kirigami.Units.gridUnit * 16
    implicitHeight: Kirigami.Units.gridUnit * 9

    clip: true
    activeFocusOnTab: true

    Accessible.role: Accessible.Slider
    Accessible.name: i18n("Light position")
    Accessible.description: i18nc("@info light source state and keyboard help",
        "X %1, Y %2, height %3, radius %4. Arrow keys move the light (Shift for larger steps), Page Up and Page Down change its height, plus and minus change its radius.",
        DockSettings.shadowLightX, DockSettings.shadowLightY,
        DockSettings.shadowLightZ, DockSettings.shadowLightRadius.toFixed(1))
    Accessible.focusable: true
    Accessible.focused: activeFocus

    // Setters identical to ShadowPage.qml (kcfg ranges + slider granularity).
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

    Keys.onPressed: (event) => {
        const step = (event.modifiers & Qt.ShiftModifier) ? 50 : 10
        const lx = DockSettings.shadowLightX
        const ly = DockSettings.shadowLightY
        let handled = true
        switch (event.key) {
        case Qt.Key_Left: setLightPosition(lx - step, ly); break
        case Qt.Key_Right: setLightPosition(lx + step, ly); break
        case Qt.Key_Up: setLightPosition(lx, ly - step); break
        case Qt.Key_Down: setLightPosition(lx, ly + step); break
        case Qt.Key_PageUp: setLightHeight(DockSettings.shadowLightZ + 50); break
        case Qt.Key_PageDown: setLightHeight(DockSettings.shadowLightZ - 50); break
        case Qt.Key_Plus:
        case Qt.Key_Equal: setLightRadius(DockSettings.shadowLightRadius + 0.5); break
        case Qt.Key_Minus:
        case Qt.Key_Underscore: setLightRadius(DockSettings.shadowLightRadius - 0.5); break
        default: handled = false
        }
        event.accepted = handled
    }

    // --- Scene geometry (1 unit = 1 real px) ---

    readonly property real screenAspect: SettingsWindow.screenAspect > 0 ? SettingsWindow.screenAspect : 16 / 9
    readonly property real screenW: SettingsWindow.screenSize.width > 0 ? SettingsWindow.screenSize.width : 1920
    readonly property real screenH: screenW / screenAspect

    // The dock texture is rendered at texUnit preview px per real px.
    readonly property real texUnit: 2
    readonly property real dockW: Math.max(1, dockTexture.width / texUnit)
    readonly property real dockD: Math.max(1, dockTexture.height / texUnit)
    readonly property real dockT: 3
    readonly property real dockRadius: Math.min(DockSettings.cornerRadius, dockD / 2)
    // Bottom edge of the screen. A strip of wallpaper is kept in front of
    // the dock so the shadow that falls towards the edge stays visible.
    readonly property real edgeZ: dockD + (DockSettings.floating ? 8 : 0)
    readonly property real elevation: DockSettings.shadowElevation

    // Drawn height of the lamp handle: monotone compression of 100..2000 px
    // relative to the dock size (the camera frames the dock), always above it.
    readonly property real sceneLightY: Math.max(
        elevation + 30,
        dockW * (0.14 + 0.36 * (DockSettings.shadowLightZ - 100) / 1900))
    readonly property vector3d lightPos: Qt.vector3d(DockSettings.shadowLightX, sceneLightY, DockSettings.shadowLightY)

    // Lamp colors: near-white with a hint of the theme's warm neutral.
    readonly property color lightWhite: Qt.tint(Kirigami.Theme.highlightedTextColor,
                                                Qt.alpha(Kirigami.Theme.neutralTextColor, 0.15))

    // Shadow darkness as seen on screen: an ease-out of the intensity keeps
    // the default (0.3) clearly legible; 0 = none, 1 = black.
    readonly property real shadowDarkness: 1 - Math.pow(1 - DockSettings.shadowIntensity, 1.5)
    // The catcher multiplies after the sRGB encode, so the light factor that
    // yields that on-screen darkness is 1 - linear(1 - darkness).
    function srgbToLinear(c) {
        return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)
    }

    // --- View ray helpers (guarded until the camera and size exist) ---

    readonly property bool viewReady: view.camera !== null && view.width > 0 && view.height > 0

    // Ray through view position (x, y): p0 on the near plane, p1 further in
    // (mapTo3DScene's z is the distance from the near clip plane).
    function viewRay(mx, my) {
        if (!viewReady)
            return null
        const p0 = view.mapTo3DScene(Qt.vector3d(mx, my, 0))
        const p1 = view.mapTo3DScene(Qt.vector3d(mx, my, 50))
        const d = p1.minus(p0)
        const len = d.length()
        if (len < 1e-6)
            return null
        return { origin: p0, dir: d.times(1 / len) }
    }
    // Intersect the view ray with the horizontal plane y = planeY.
    function rayHitYPlane(mx, my, planeY) {
        const ray = viewRay(mx, my)
        if (!ray || Math.abs(ray.dir.y) < 1e-4)
            return null
        const t = (planeY - ray.origin.y) / ray.dir.y
        if (t <= 0)
            return null
        return ray.origin.plus(ray.dir.times(t))
    }
    // Intersect the view ray with the vertical plane z = planeZ.
    function rayHitZPlane(mx, my, planeZ) {
        const ray = viewRay(mx, my)
        if (!ray || Math.abs(ray.dir.z) < 1e-4)
            return null
        const t = (planeZ - ray.origin.z) / ray.dir.z
        if (t <= 0)
            return null
        return ray.origin.plus(ray.dir.times(t))
    }

    // Screen-space rect of the dock for the elevation keyboard handle.
    // mapFrom3DScene is not a binding dependency, so the camera angles,
    // geometry and view size are read explicitly to trigger re-evaluation.
    readonly property rect slabRect: {
        const deps = cameraYaw + cameraPitch + elevation + dockW + dockD + view.width + view.height
        if (!viewReady || !isFinite(deps))
            return Qt.rect(0, 0, 0, 0)
        let minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9, ok = false
        const hw = dockW / 2, hd = dockD / 2
        for (const cx of [-hw, hw])
            for (const cy of [elevation, elevation + dockT])
                for (const cz of [-hd, hd]) {
                    const p = view.mapFrom3DScene(Qt.vector3d(cx, cy, cz))
                    if (p.z <= 0)
                        continue
                    ok = true
                    minX = Math.min(minX, p.x); maxX = Math.max(maxX, p.x)
                    minY = Math.min(minY, p.y); maxY = Math.max(maxY, p.y)
                }
        if (!ok)
            return Qt.rect(0, 0, 0, 0)
        const pad = 4
        return Qt.rect(minX - pad, minY - pad, maxX - minX + pad * 2, maxY - minY + pad * 2)
    }

    // Thin unlit beam between two scene points. A cylinder sits in a
    // yaw-then-pitch node pair (one rotation axis per node, so euler order
    // never matters). Inline components can't see this file's ids, so every
    // value comes in through properties.
    component Beam: Node {
        id: beam
        required property vector3d from
        required property vector3d to
        required property color color
        required property real thickness

        readonly property vector3d d: to.minus(from)
        readonly property real len: Math.max(0.001, d.length())

        position: from
        eulerRotation.y: Math.atan2(d.x, d.z) * 180 / Math.PI

        Node {
            eulerRotation.x: Math.atan2(Math.hypot(beam.d.x, beam.d.z), beam.d.y) * 180 / Math.PI
            Model {
                source: "#Cylinder"
                position: Qt.vector3d(0, beam.len / 2, 0)
                scale: Qt.vector3d(beam.thickness / 100, beam.len / 100, beam.thickness / 100)
                pickable: false
                castsShadows: false
                receivesShadows: false
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    alphaMode: beam.color.a < 1 ? PrincipledMaterial.Blend : PrincipledMaterial.Opaque
                    baseColor: beam.color
                }
            }
        }
    }

    // Flat unlit disc lying on the screen plane.
    component FloorDisc: Model {
        id: disc
        required property real diameter
        required property color color
        source: "#Cylinder"
        scale: Qt.vector3d(diameter / 100, 0.004, diameter / 100)
        pickable: false
        castsShadows: false
        receivesShadows: false
        materials: PrincipledMaterial {
            lighting: PrincipledMaterial.NoLighting
            alphaMode: PrincipledMaterial.Blend
            baseColor: disc.color
        }
    }

    View3D {
        id: view
        anchors.fill: parent
        camera: sceneCamera

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.VeryHigh
            // Linear = plain sRGB output encode, so unlit colors match the
            // 2D wallpaper exactly (None skips the encode: too dark).
            tonemapMode: SceneEnvironment.TonemapModeLinear
        }

        // Camera rig: three-quarter view from in front of and above the
        // dock. Yaw node, then pitch node, then the camera on +Z looking down
        // -Z at the target. Framing is relative to the dock: it spans ~45%
        // of the stage width, the screen's bottom edge sits near the bottom
        // of the stage and the lamp at its default height is well in frame.
        Node {
            position: Qt.vector3d(0, 0, -root.dockW * 0.5)
            eulerRotation.y: root.cameraYaw

            Node {
                eulerRotation.x: -root.cameraPitch

                PerspectiveCamera {
                    id: sceneCamera
                    z: root.dockW * 2.8
                    fieldOfView: 50
                    fieldOfViewOrientation: PerspectiveCamera.Horizontal
                    clipNear: 5
                    clipFar: root.dockW * 20 + root.screenH * 2
                }
            }
        }

        // Key light, aimed from the configured light position (real height)
        // at the dock. Lights shine along their local -Z: the yaw node then
        // the pitch-rotated light point -Z along `dir`. Only the catcher is
        // lit, so brightness 1 / cos(incidence) makes lit pixels exactly 1.
        Node {
            id: keyRig
            // Travel direction: from the light to the dock centre.
            readonly property vector3d dir: Qt.vector3d(-DockSettings.shadowLightX,
                                                        root.elevation - DockSettings.shadowLightZ,
                                                        -DockSettings.shadowLightY).normalized()
            eulerRotation.y: Math.atan2(-dir.x, -dir.z) * 180 / Math.PI

            DirectionalLight {
                eulerRotation.x: Math.asin(Math.max(-1, Math.min(1, keyRig.dir.y))) * 180 / Math.PI
                brightness: 1.02 / Math.max(0.15, -keyRig.dir.y)
                castsShadow: true
                shadowFactor: 100 * (1 - root.srgbToLinear(1 - root.shadowDarkness))
                shadowMapQuality: Light.ShadowMapQualityVeryHigh
                shadowMapFar: root.dockW * 8
                // World units are px; only the catcher receives shadows, so a
                // small bias keeps 1 px elevations attached without acne.
                shadowBias: 1
                // Softness = light radius: PCF radius in world units (px).
                softShadowQuality: Light.PCF16
                pcfFactor: DockSettings.shadowLightRadius
            }
        }

        // --- The screen: wallpaper plane, shadow catcher, bezel ---

        Model {
            id: bezel
            source: "#Cube"
            readonly property real border: Math.max(6, root.dockD * 0.12)
            position: Qt.vector3d(0, -4.5, root.edgeZ - root.screenH / 2)
            scale: Qt.vector3d((root.screenW + border * 2) / 100, 0.08, (root.screenH + border * 2) / 100)
            pickable: false
            castsShadows: false
            receivesShadows: false
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: Kirigami.ColorUtils.linearInterpolation(
                    Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.35)
            }
        }

        Model {
            id: screenPlane
            source: "#Rectangle"
            // #Rectangle lies in XY facing +Z; -90 about X lays it face up
            // with the image top towards the far (top) edge of the screen.
            eulerRotation.x: -90
            position: Qt.vector3d(0, 0, root.edgeZ - root.screenH / 2)
            scale: Qt.vector3d(root.screenW / 100, root.screenH / 100, 1)
            pickable: false
            castsShadows: false
            receivesShadows: false
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColorMap: Texture {
                    sourceItem: Item {
                        width: 2048
                        height: Math.round(2048 / root.screenAspect)

                        // Same theme-derived stand-in DesktopStage uses while
                        // no wallpaper image is known.
                        Rectangle {
                            anchors.fill: parent
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: Qt.darker(Kirigami.Theme.highlightColor, 2.2) }
                                GradientStop { position: 0.6; color: Kirigami.Theme.highlightColor }
                                GradientStop { position: 1.0; color: Qt.lighter(Kirigami.Theme.highlightColor, 1.35) }
                            }
                        }
                        Image {
                            anchors.fill: parent
                            source: SettingsWindow.wallpaperUrl
                            visible: status === Image.Ready
                            asynchronous: true
                            fillMode: Image.PreserveAspectCrop
                            sourceSize.width: width
                            sourceSize.height: height
                        }
                    }
                }
            }
        }

        // White, lit, multiplied over the wallpaper: lit = 1 (no change),
        // shadowed = darker by the shadow factor.
        Model {
            id: shadowCatcher
            source: "#Rectangle"
            eulerRotation.x: -90
            position: Qt.vector3d(0, 0.3, root.edgeZ - root.screenH / 2)
            scale: Qt.vector3d(root.screenW / 100, root.screenH / 100, 1)
            pickable: false
            castsShadows: false
            receivesShadows: true
            materials: PrincipledMaterial {
                // Multiply identity (not a visible color): lit = unchanged.
                baseColor: Qt.rgba(1, 1, 1, 1)
                blendMode: PrincipledMaterial.Multiply
                roughness: 1
                metalness: 0
                specularAmount: 0
            }
        }

        // --- The dock: a thin rounded panel floating at elevation ---

        // Opaque body: casts the shadow. Inset so its square corners stay
        // under the rounded top (0.32 x radius clears the corner arc).
        Model {
            id: dockSlab
            source: "#Cube"
            readonly property real inset: root.dockRadius * 0.32
            position: Qt.vector3d(0, root.elevation + root.dockT / 2, 0)
            scale: Qt.vector3d(Math.max(1, root.dockW - inset * 2) / 100, root.dockT / 100,
                               Math.max(1, root.dockD - inset * 2) / 100)
            pickable: true
            castsShadows: true
            receivesShadows: false
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: Kirigami.ColorUtils.linearInterpolation(
                    Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.2)
            }
        }

        // Top face: the live miniature dock (real icons, panel style).
        Model {
            id: dockTop
            source: "#Rectangle"
            eulerRotation.x: -90
            position: Qt.vector3d(0, root.elevation + root.dockT + 0.3, 0)
            scale: Qt.vector3d(root.dockW / 100, root.dockD / 100, 1)
            pickable: true
            castsShadows: false
            receivesShadows: false
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                alphaMode: PrincipledMaterial.Blend
                baseColorMap: Texture {
                    sourceItem: Item {
                        width: dockTexture.width
                        height: dockTexture.height

                        MiniDock {
                            id: dockTexture
                            unit: root.texUnit
                            edge: 1
                            floating: false
                            backdrop: null
                            active: false
                        }
                    }
                }
            }
        }

        // --- The lamp handle ---

        // Faint ray to the dock.
        Beam {
            from: root.lightPos
            to: Qt.vector3d(0, root.elevation + root.dockT, 0)
            color: Qt.alpha(root.lightWhite, 0.45)
            thickness: root.dockW * 0.004
        }

        // Vertical drop line to the screen plane: a contrasting outline
        // (theme background) around a theme-text core shows on any wallpaper.
        Beam {
            from: root.lightPos
            to: Qt.vector3d(root.lightPos.x, 0.5, root.lightPos.z)
            color: Kirigami.Theme.backgroundColor
            thickness: root.dockW * 0.011
        }
        Beam {
            from: root.lightPos
            to: Qt.vector3d(root.lightPos.x, 0.5, root.lightPos.z)
            color: Kirigami.Theme.textColor
            thickness: root.dockW * 0.005
        }

        // Where the lamp projects: a faint ring whose diameter is the
        // shadow's Gaussian spread (6 x light radius px), plus an outlined dot.
        FloorDisc {
            position: Qt.vector3d(root.lightPos.x, 0.6, root.lightPos.z)
            diameter: Math.max(root.dockW * 0.06, DockSettings.shadowLightRadius * 6)
            color: Qt.alpha(Kirigami.Theme.neutralTextColor, 0.3)
        }
        FloorDisc {
            position: Qt.vector3d(root.lightPos.x, 0.8, root.lightPos.z)
            diameter: root.dockW * 0.035
            color: Kirigami.Theme.backgroundColor
        }
        FloorDisc {
            position: Qt.vector3d(root.lightPos.x, 1.0, root.lightPos.z)
            diameter: root.dockW * 0.022
            color: Kirigami.Theme.textColor
        }

        // Soft glow: two translucent shells, the inner one also the grab
        // target. It grows with the light radius and pulses while active.
        Model {
            id: lightHalo
            source: "#Sphere"
            position: root.lightPos
            property real pulse: 0
            readonly property real dia: root.dockW * 0.13 + DockSettings.shadowLightRadius * 2
            scale: {
                const s = dia / 100 * (1 + 0.08 * pulse)
                return Qt.vector3d(s, s, s)
            }
            pickable: true
            castsShadows: false
            receivesShadows: false
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                alphaMode: PrincipledMaterial.Blend
                baseColor: Qt.alpha(root.lightWhite, 0.35)
            }

            SequentialAnimation on pulse {
                running: root.active
                loops: Animation.Infinite
                NumberAnimation { from: 0; to: 1; duration: 1600; easing.type: Easing.InOutSine }
                NumberAnimation { from: 1; to: 0; duration: 1600; easing.type: Easing.InOutSine }
            }
        }
        Model {
            source: "#Sphere"
            position: root.lightPos
            readonly property real dia: lightHalo.dia * 1.7
            scale: Qt.vector3d(dia / 100, dia / 100, dia / 100)
            pickable: false
            castsShadows: false
            receivesShadows: false
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                alphaMode: PrincipledMaterial.Blend
                baseColor: Qt.alpha(root.lightWhite, 0.16)
            }
        }

        // Bright core with a theme-colored rim (slightly larger dark shell
        // behind it reads as an outline on light wallpapers).
        Model {
            source: "#Sphere"
            position: root.lightPos
            readonly property real dia: root.dockW * 0.077
            scale: Qt.vector3d(dia / 100, dia / 100, dia / 100)
            pickable: false
            castsShadows: false
            receivesShadows: false
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: Kirigami.Theme.neutralTextColor
            }
        }
        Model {
            id: lightCore
            source: "#Sphere"
            position: root.lightPos.plus(sceneCamera.scenePosition.minus(root.lightPos).normalized().times(root.dockW * 0.01))
            readonly property real dia: root.dockW * 0.07
            scale: Qt.vector3d(dia / 100, dia / 100, dia / 100)
            pickable: true
            castsShadows: false
            receivesShadows: false
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: root.lightWhite
            }
        }
    }

    // Keyboard-focus ring around the whole stage.
    Rectangle {
        anchors.fill: parent
        radius: Kirigami.Units.largeSpacing
        color: "transparent"
        border.width: 2
        border.color: Kirigami.Theme.highlightColor
        visible: root.activeFocus
    }

    // Invisible "Panel elevation" slider over the dock, so a second focusable
    // control adjusts elevation with the keyboard.
    Item {
        id: elevationHandle
        x: root.slabRect.x
        y: root.slabRect.y
        width: Math.max(1, root.slabRect.width)
        height: Math.max(1, root.slabRect.height)
        visible: root.slabRect.width > 0
        activeFocusOnTab: true

        Accessible.role: Accessible.Slider
        Accessible.name: i18n("Panel elevation")
        Accessible.description: String(DockSettings.shadowElevation)
        Accessible.focusable: true
        Accessible.focused: activeFocus

        Keys.onPressed: (event) => {
            const step = (event.modifiers & Qt.ShiftModifier) ? 5 : 1
            let handled = true
            switch (event.key) {
            case Qt.Key_Up: root.setElevation(DockSettings.shadowElevation + step); break
            case Qt.Key_Down: root.setElevation(DockSettings.shadowElevation - step); break
            case Qt.Key_PageUp: root.setElevation(DockSettings.shadowElevation + 10); break
            case Qt.Key_PageDown: root.setElevation(DockSettings.shadowElevation - 10); break
            default: handled = false
            }
            event.accepted = handled
        }

        Rectangle {
            anchors.fill: parent
            radius: Kirigami.Units.smallSpacing
            color: "transparent"
            border.width: 2
            border.color: Kirigami.Theme.highlightColor
            visible: elevationHandle.activeFocus
        }
    }

    // Orbit reset animation (declarative; runs only on double-click).
    ParallelAnimation {
        id: orbitReset
        NumberAnimation { target: root; property: "cameraYaw"; to: root.defaultYaw; duration: 250; easing.type: Easing.OutCubic }
        NumberAnimation { target: root; property: "cameraPitch"; to: root.defaultPitch; duration: 250; easing.type: Easing.OutCubic }
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onDoubleTapped: orbitReset.restart()
    }

    MouseArea {
        id: sceneMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton

        property string mode: ""       // "light", "elevation" or "orbit" while pressed
        property string hoverMode: ""
        property real grabX: 0
        property real grabY: 0
        property real startYaw: 0
        property real startPitch: 0
        property real wheelAccumulator: 0

        readonly property string activeMode: pressed ? mode : hoverMode

        cursorShape: {
            switch (activeMode) {
            case "light": return pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            case "elevation": return Qt.SizeVerCursor
            case "orbit": return pressed ? Qt.SizeAllCursor : Qt.ArrowCursor
            default: return Qt.ArrowCursor
            }
        }

        function hitTest(mx, my) {
            if (!root.viewReady)
                return ""
            const hit = view.pick(mx, my).objectHit
            if (hit === lightCore || hit === lightHalo)
                return "light"
            if (hit === dockSlab || hit === dockTop)
                return "elevation"
            return ""
        }

        onPressed: (mouse) => {
            root.forceActiveFocus(Qt.MouseFocusReason)
            mode = hitTest(mouse.x, mouse.y)
            if (mode === "light") {
                // Keep the grab point: offset between the ray/plane hit and
                // the light's logical X/Y.
                const hit = root.rayHitYPlane(mouse.x, mouse.y, root.sceneLightY)
                grabX = hit ? hit.x - DockSettings.shadowLightX : 0
                grabY = hit ? hit.z - DockSettings.shadowLightY : 0
            } else if (mode === "elevation") {
                const hit = root.rayHitZPlane(mouse.x, mouse.y, 0)
                grabY = hit ? hit.y - DockSettings.shadowElevation : 0
            } else {
                mode = "orbit"
                grabX = mouse.x
                grabY = mouse.y
                startYaw = root.cameraYaw
                startPitch = root.cameraPitch
            }
        }
        onPositionChanged: (mouse) => {
            if (!pressed) {
                hoverMode = hitTest(mouse.x, mouse.y)
                return
            }
            if (mode === "light") {
                const hit = root.rayHitYPlane(mouse.x, mouse.y, root.sceneLightY)
                if (hit)
                    root.setLightPosition(hit.x - grabX, hit.z - grabY)
            } else if (mode === "elevation") {
                const hit = root.rayHitZPlane(mouse.x, mouse.y, 0)
                if (hit)
                    root.setElevation(hit.y - grabY)
            } else if (mode === "orbit") {
                root.cameraYaw = Math.max(-30, Math.min(45, startYaw + (mouse.x - grabX) * 0.25))
                root.cameraPitch = Math.max(12, Math.min(55, startPitch + (mouse.y - grabY) * 0.2))
            }
        }
        onReleased: mode = ""
        onCanceled: mode = ""
        onExited: hoverMode = ""

        onWheel: (wheel) => {
            const delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x
            if (delta === 0) {
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
                root.setLightRadius(DockSettings.shadowLightRadius + 0.5 * notches)
            else
                root.setLightHeight(DockSettings.shadowLightZ + 50 * notches)
        }
    }
}
