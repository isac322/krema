// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Shapes
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

SettingsPage {
    id: page
    title: i18n("Behavior")
    subtitle: i18n("When the dock shows and hides, and what clicking its apps does")

    // --- Inline preview components ---

    // Miniature app window: a title bar in the Header colors with the app
    // icon, title and window buttons (Breeze order: minimize, maximize,
    // close), a toolbar and a View-colored body with a sidebar and a list.
    // Maximized windows have square corners and no shadow.
    component SceneWindow: Item {
        id: win

        property real unit: 0.1
        property bool maximized: false

        readonly property real titleHeight: Math.max(5, Math.round(Math.min(height * 0.16, Kirigami.Units.gridUnit * 2 * unit)))
        readonly property real s: titleHeight * 0.3
        readonly property real cornerRadius: maximized ? 0 : Math.max(2, titleHeight * 0.35)

        Kirigami.ShadowedRectangle {
            id: windowFrame
            anchors.fill: parent
            radius: win.cornerRadius
            color: Kirigami.Theme.backgroundColor
            border.width: win.maximized ? 0 : 1
            border.color: Qt.alpha(Kirigami.Theme.textColor, 0.2)
            shadow.size: win.maximized ? 0 : Kirigami.Units.gridUnit * 0.6
            shadow.yOffset: 2
            shadow.color: Qt.alpha(Qt.darker(Kirigami.Theme.backgroundColor, 4.0), 0.4)

            Kirigami.Theme.colorSet: Kirigami.Theme.Window
            Kirigami.Theme.inherit: false

            // Title bar and toolbar.
            Kirigami.ShadowedRectangle {
                id: header
                x: windowFrame.border.width
                y: windowFrame.border.width
                width: parent.width - 2 * x
                height: win.titleHeight * 1.9
                radius: 0
                corners.topLeftRadius: Math.max(0, win.cornerRadius - 1)
                corners.topRightRadius: Math.max(0, win.cornerRadius - 1)
                color: Kirigami.Theme.backgroundColor

                Kirigami.Theme.colorSet: Kirigami.Theme.Header
                Kirigami.Theme.inherit: false

                Item {
                    id: titleRow
                    width: parent.width
                    height: win.titleHeight

                    Kirigami.Icon {
                        id: appIcon
                        anchors.left: parent.left
                        anchors.leftMargin: win.s
                        anchors.verticalCenter: parent.verticalCenter
                        width: win.titleHeight * 0.62
                        height: width
                        source: "org.kde.dolphin"
                        fallback: "system-file-manager"
                    }

                    QQC2.Label {
                        anchors.centerIn: parent
                        width: parent.width - 2 * (buttons.width + win.s * 2)
                        text: i18nc("@title:window sample window title in the visibility preview", "Home — Dolphin")
                        font.pixelSize: Math.max(1, win.titleHeight * 0.5)
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        color: Kirigami.Theme.textColor
                        Accessible.ignored: true
                    }

                    Row {
                        id: buttons
                        anchors.right: parent.right
                        anchors.rightMargin: win.s
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: win.s * 0.8

                        Repeater {
                            model: 3
                            delegate: Rectangle {
                                required property int index
                                width: Math.max(2, win.titleHeight * 0.42)
                                height: width
                                radius: width / 2
                                color: index === 2 ? Kirigami.Theme.negativeTextColor
                                                   : Qt.alpha(Kirigami.Theme.textColor, 0.35)
                            }
                        }
                    }
                }

                // Toolbar: navigation buttons and the location bar.
                Row {
                    anchors.top: titleRow.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: win.s
                    anchors.rightMargin: win.s
                    spacing: win.s

                    Repeater {
                        model: 2
                        Rectangle {
                            width: win.s * 2.4
                            height: Math.max(1, win.s * 1.6)
                            radius: height / 2
                            color: Qt.alpha(Kirigami.Theme.textColor, 0.22)
                        }
                    }
                    Rectangle {
                        width: Math.max(win.s * 4, parent.width - win.s * 6.8)
                        height: Math.max(1, win.s * 1.6)
                        radius: height / 2
                        color: Qt.alpha(Kirigami.Theme.textColor, 0.1)
                    }
                }
            }

            // Body: places sidebar next to the file list.
            Kirigami.ShadowedRectangle {
                id: body
                anchors.top: header.bottom
                anchors.left: header.left
                anchors.right: header.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: windowFrame.border.width
                radius: 0
                corners.bottomLeftRadius: Math.max(0, win.cornerRadius - 1)
                corners.bottomRightRadius: Math.max(0, win.cornerRadius - 1)
                color: Kirigami.Theme.backgroundColor

                Kirigami.Theme.colorSet: Kirigami.Theme.View
                Kirigami.Theme.inherit: false

                Item {
                    anchors.fill: parent
                    anchors.margins: win.s * 1.2
                    clip: true

                    Column {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        width: parent.width * 0.28
                        spacing: win.s * 0.9

                        Repeater {
                            model: [0.9, 0.7, 0.8, 0.6, 0.75]
                            delegate: Rectangle {
                                required property int index
                                required property real modelData
                                width: parent.width * modelData
                                height: Math.max(1, win.s * 0.9)
                                radius: height / 2
                                color: index === 0 ? Kirigami.Theme.highlightColor
                                                   : Qt.alpha(Kirigami.Theme.textColor, 0.18)
                            }
                        }
                    }

                    Column {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        width: parent.width * 0.64
                        spacing: win.s * 0.9

                        Repeater {
                            model: [0.95, 0.7, 0.85, 0.5, 0.75, 0.6, 0.9, 0.65, 0.8, 0.55]
                            delegate: Row {
                                required property real modelData
                                width: parent.width
                                spacing: win.s * 0.6

                                Rectangle {
                                    width: Math.max(1, win.s * 0.9)
                                    height: width
                                    radius: width * 0.25
                                    color: Qt.alpha(Kirigami.Theme.highlightColor, 0.45)
                                }
                                Rectangle {
                                    width: parent.width * modelData * 0.85
                                    height: Math.max(1, win.s * 0.8)
                                    radius: height / 2
                                    color: Qt.alpha(Kirigami.Theme.textColor, 0.3)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Arrow pointer; its tip is the item's origin (the hotspot).
    component PointerGlyph: Shape {
        id: glyph

        property real size: 10

        width: size * 0.55
        height: size
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: Kirigami.Theme.backgroundColor
            strokeWidth: Math.max(1, glyph.size * 0.08)
            fillColor: Kirigami.Theme.textColor
            joinStyle: ShapePath.RoundJoin

            PathPolyline {
                path: [
                    Qt.point(0, 0),
                    Qt.point(0, glyph.size * 0.76),
                    Qt.point(glyph.size * 0.18, glyph.size * 0.6),
                    Qt.point(glyph.size * 0.31, glyph.size * 0.9),
                    Qt.point(glyph.size * 0.42, glyph.size * 0.85),
                    Qt.point(glyph.size * 0.29, glyph.size * 0.56),
                    Qt.point(glyph.size * 0.53, glyph.size * 0.56),
                    Qt.point(0, 0)
                ]
            }
        }
    }

    // The desktop scene of one visibility mode, laid out on a DesktopStage's
    // miniature screen: an app window, the dock and a pointer. `mode`
    // follows the VisibilityMode enum: 0 Always visible (maximized window
    // that stops above the dock when Reserve screen space is on), 1 Auto
    // hide (the dock slides in when the pointer reaches the edge and away
    // after it leaves), 2 Dodge windows (the dock slides away while a window
    // covers it). With `live` the mode plays in a loop timed by the show and
    // hide delays; otherwise the state properties are a still frame.
    component VisibilityScene: Item {
        id: scene

        property int mode: 0
        property bool reserve: DockSettings.reserveScreenSpace
        /// The demo loop may run.
        property bool live: false
        property real unit: 0.1
        property int edge: DockSettings.edge
        property Item backdrop: null
        /// Size of the stage frame: the visible part of the screen.
        property size viewport: Qt.size(width, height)

        // Demo state: 0..1 each.
        property real dockHidden: 0
        property real windowDodge: 0
        property real pointerTravel: 0
        property real pointerLift: 0
        property real pointerOpacity: 0

        readonly property alias dock: sceneDock

        readonly property bool verticalEdge: edge === 2 || edge === 3
        // Lengths measured from the dock edge (depth) and along it.
        readonly property real screenDepth: verticalEdge ? width : height
        readonly property real screenAlong: verticalEdge ? height : width
        readonly property real viewDepth: Math.min(screenDepth, verticalEdge ? viewport.width : viewport.height)
        readonly property real viewAlong: Math.min(screenAlong, verticalEdge ? viewport.height : viewport.width)
        readonly property real dockZone: sceneDock.thickness + sceneDock.floatPx
        /// Draw the window floating above the dock even in Always visible
        /// (picker tile, where a maximized window would hide the scene).
        property bool floatingWindow: false
        readonly property bool maximized: mode === 0 && !floatingWindow

        // Depth taken by the dock from maximized windows; animated when
        // Reserve screen space is toggled.
        property real reservedDepth: maximized && reserve ? dockZone : 0
        Behavior on reservedDepth {
            enabled: scene.live
            NumberAnimation {
                duration: Kirigami.Units.longDuration
                easing.type: Easing.OutCubic
            }
        }

        // Floating window (Auto hide, Dodge windows), sized to the visible
        // part of the screen: about half of it along the edge, as deep as
        // the free area allows, parked just off the dock zone.
        readonly property real _freeDepth: Math.max(1, viewDepth - dockZone)
        readonly property real _floatAlongSize: viewAlong * (verticalEdge ? 0.6 : 0.5)
        readonly property real _floatDepthSize: Math.min(verticalEdge ? _floatAlongSize * 1.5 : _floatAlongSize / 1.5,
                                                         _freeDepth * 0.86)
        readonly property real _floatWidth: verticalEdge ? _floatDepthSize : _floatAlongSize
        readonly property real _floatHeight: verticalEdge ? _floatAlongSize : _floatDepthSize
        readonly property real _parkedNear: dockZone + Math.max(0, _freeDepth - _floatDepthSize) * 0.4
        // Over the dock: the window's near side covers two thirds of the zone.
        readonly property real _overNear: dockZone * 0.35
        readonly property real _floatNear: _parkedNear + (_overNear - _parkedNear) * windowDodge

        // Window in depth/along terms.
        readonly property real _winDepthSize: maximized ? screenDepth - reservedDepth : _floatDepthSize
        readonly property real _winAlongSize: maximized ? screenAlong : (verticalEdge ? _floatHeight : _floatWidth)
        readonly property real _winNear: maximized ? reservedDepth : _floatNear
        readonly property real _winAlongPos: (screenAlong - _winAlongSize) / 2

        // Pointer tip: from the middle of the free area to the screen edge
        // (travel), then onto the dock icons once the dock is shown (lift).
        readonly property real _pointerStartDepth: dockZone + _freeDepth * 0.55
        readonly property real _pointerStartAlong: screenAlong / 2 + viewAlong * 0.2
        readonly property real _pointerEdgeAlong: screenAlong / 2 + sceneDock.pitch * 0.5
        readonly property real _pointerLiftDepth: sceneDock.floatPx + sceneDock.thickness * 0.5
        readonly property real _pointerDepth: _pointerStartDepth + (1 - _pointerStartDepth) * pointerTravel
                                              + (_pointerLiftDepth - 1) * pointerLift
        readonly property real _pointerAlong: _pointerStartAlong + (_pointerEdgeAlong - _pointerStartAlong) * pointerTravel

        anchors.fill: parent

        // Ends a loop without leaving another mode's state behind.
        function settle() {
            if (mode !== 1) {
                pointerOpacity = 0
                pointerTravel = 0
                pointerLift = 0
            }
            if (mode !== 2) {
                windowDodge = 0
            }
            if (mode === 0) {
                dockHidden = 0
            }
        }

        SceneWindow {
            unit: scene.unit
            maximized: scene.maximized
            width: scene.verticalEdge ? scene._winDepthSize : scene._winAlongSize
            height: scene.verticalEdge ? scene._winAlongSize : scene._winDepthSize
            x: scene.verticalEdge ? (scene.edge === 2 ? scene._winNear : scene.width - scene._winNear - width)
                                  : scene._winAlongPos
            y: scene.verticalEdge ? scene._winAlongPos
                                  : (scene.edge === 0 ? scene._winNear : scene.height - scene._winNear - height)
        }

        MiniDock {
            id: sceneDock

            // Slides past the screen edge as it hides.
            readonly property real hideShift: (thickness + floatPx + 2) * scene.dockHidden

            unit: scene.unit
            edge: scene.edge
            backdrop: scene.backdrop
            active: scene.live
            // The app of the sample window is the active one.
            apps: [
                { icon: "org.kde.dolphin", fallback: "system-file-manager", state: "active", badge: 0, padding: 1.0 },
                { icon: "org.kde.konsole", fallback: "utilities-terminal", state: "inactive", badge: 0, padding: 1.0 },
                { icon: "org.kde.kate", fallback: "accessories-text-editor", state: "minimized", badge: 0, padding: 0.82 },
                { icon: "firefox", fallback: "org.kde.falkon", state: "inactive", badge: 0, padding: 1.0 },
                { icon: "systemsettings", fallback: "preferences-system", state: "launcher", badge: 0, padding: 0.88 },
                { icon: "org.kde.discover", fallback: "system-software-install", state: "launcher", badge: 3, padding: 1.0 }
            ]
            x: parent ? (vertical ? (edge === 2 ? floatPx - hideShift : parent.width - width - floatPx + hideShift)
                                  : (parent.width - width) / 2) : 0
            y: parent ? (vertical ? (parent.height - height) / 2
                                  : (edge === 0 ? floatPx - hideShift : parent.height - height - floatPx + hideShift)) : 0
        }

        PointerGlyph {
            size: Math.max(8, 28 * scene.unit)
            visible: opacity > 0
            opacity: scene.pointerOpacity
            x: scene.verticalEdge ? (scene.edge === 2 ? scene._pointerDepth : scene.width - scene._pointerDepth)
                                  : scene._pointerAlong
            y: scene.verticalEdge ? scene._pointerAlong
                                  : (scene.edge === 0 ? scene._pointerDepth : scene.height - scene._pointerDepth)
        }

        // Auto hide: the pointer reaches the edge, the dock slides in after
        // the show delay; the pointer leaves, the dock slides away after the
        // hide delay.
        SequentialAnimation {
            running: scene.live && scene.mode === 1
            loops: Animation.Infinite
            onStopped: scene.settle()

            PropertyAction { target: scene; property: "dockHidden"; value: 1 }
            PropertyAction { target: scene; property: "pointerTravel"; value: 0 }
            PropertyAction { target: scene; property: "pointerLift"; value: 0 }
            NumberAnimation { target: scene; property: "pointerOpacity"; to: 1; duration: 250 }
            PauseAnimation { duration: 500 }
            NumberAnimation { target: scene; property: "pointerTravel"; to: 1; duration: 1000; easing.type: Easing.InOutCubic }
            PauseAnimation { duration: DockSettings.showDelay }
            ParallelAnimation {
                NumberAnimation { target: scene; property: "dockHidden"; to: 0; duration: 300; easing.type: Easing.OutCubic }
                NumberAnimation { target: scene; property: "pointerLift"; to: 1; duration: 400; easing.type: Easing.OutCubic }
            }
            PauseAnimation { duration: 1400 }
            ParallelAnimation {
                NumberAnimation { target: scene; property: "pointerTravel"; to: 0; duration: 1000; easing.type: Easing.InOutCubic }
                NumberAnimation { target: scene; property: "pointerLift"; to: 0; duration: 1000; easing.type: Easing.InOutCubic }
            }
            PauseAnimation { duration: DockSettings.hideDelay }
            NumberAnimation { target: scene; property: "dockHidden"; to: 1; duration: 300; easing.type: Easing.InCubic }
            NumberAnimation { target: scene; property: "pointerOpacity"; to: 0; duration: 250 }
            PauseAnimation { duration: 700 }
        }

        // Dodge windows: the window moves over the dock, which slides away
        // after the hide delay; the window moves off again and the dock
        // returns after the show delay.
        SequentialAnimation {
            running: scene.live && scene.mode === 2
            loops: Animation.Infinite
            onStopped: scene.settle()

            PropertyAction { target: scene; property: "dockHidden"; value: 0 }
            PropertyAction { target: scene; property: "windowDodge"; value: 0 }
            PauseAnimation { duration: 1200 }
            NumberAnimation { target: scene; property: "windowDodge"; to: 1; duration: 1100; easing.type: Easing.InOutCubic }
            PauseAnimation { duration: DockSettings.hideDelay }
            NumberAnimation { target: scene; property: "dockHidden"; to: 1; duration: 300; easing.type: Easing.InCubic }
            PauseAnimation { duration: 1400 }
            NumberAnimation { target: scene; property: "windowDodge"; to: 0; duration: 1100; easing.type: Easing.InOutCubic }
            PauseAnimation { duration: DockSettings.showDelay }
            NumberAnimation { target: scene; property: "dockHidden"; to: 0; duration: 300; easing.type: Easing.OutCubic }
        }
    }

    // Picker thumbnail: a still frame of one visibility mode.
    component VisibilityTile: DesktopStage {
        id: tile

        // Still-frame state forwarded to the scene. Plain properties, because
        // grouped assignment through an inline-component-typed alias is not
        // supported by the QML engine.
        property int mode: 0
        property real dockHidden: 0
        property real windowDodge: 0
        property real pointerTravel: 0
        property real pointerOpacity: 0

        readonly property real coverUnit: Math.max(width / screenSize.width, height / screenSize.height)

        anchors.fill: parent
        elevated: false
        radius: 0
        active: false
        unit: Math.max(coverUnit, Math.min(coverUnit * 1.3,
            0.7 * (tileScene.dock.vertical ? height : width) / tileScene.dock.restLengthReal))
        Accessible.ignored: true

        VisibilityScene {
            id: tileScene
            unit: tile.unit
            edge: tile.edge
            backdrop: tile.backdrop
            viewport: Qt.size(tile.width, tile.height)
            // A maximized window would fill the whole thumbnail.
            floatingWindow: true
            mode: tile.mode
            dockHidden: tile.dockHidden
            windowDodge: tile.windowDodge
            pointerTravel: tile.pointerTravel
            pointerOpacity: tile.pointerOpacity
        }
    }

    // --- Stage: the selected visibility mode, live ---

    stage: Component {
        DesktopStage {
            id: stage

            readonly property real coverUnit: Math.max(width / screenSize.width, height / screenSize.height)

            active: page.windowActive
            unit: Math.max(coverUnit, Math.min(framingUnit,
                0.85 * (scene.dock.vertical ? height : width) / scene.dock.restLengthReal))

            Accessible.role: Accessible.Graphic
            Accessible.name: i18n("Visibility preview")
            Accessible.description: [
                i18n("The dock always stays visible; with reserved screen space a maximized window ends above it"),
                i18n("The dock slides in when the pointer reaches the screen edge and hides again when it leaves"),
                i18n("The dock slides away while a window covers it")
            ][DockSettings.visibilityMode] ?? ""

            VisibilityScene {
                id: scene
                mode: DockSettings.visibilityMode
                live: stage.active
                unit: stage.unit
                edge: stage.edge
                backdrop: stage.backdrop
                viewport: Qt.size(stage.width, stage.height)
            }
        }
    }

    // --- Visibility ---
    FormCard.FormHeader {
        title: i18n("Visibility")
    }

    FormCard.FormCard {
        FormCard.AbstractFormDelegate {
            background: null
            hoverEnabled: false
            focusPolicy: Qt.NoFocus
            Accessible.role: Accessible.Grouping
            Accessible.name: i18n("Visibility mode")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Visibility mode")
                    wrapMode: Text.Wrap
                    Accessible.ignored: true
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    columns: Math.max(1, Math.min(3, Math.floor(width / (Kirigami.Units.gridUnit * 9))))
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.largeSpacing

                    ChoiceCard {
                        id: alwaysCard
                        text: i18n("Always visible")
                        description: i18n("Stays on screen")
                        Binding { target: alwaysCard; property: "checked"; value: DockSettings.visibilityMode === 0 }
                        onChosen: DockSettings.visibilityMode = 0

                        VisibilityTile {
                            mode: 0
                        }
                    }

                    ChoiceCard {
                        id: autoHideCard
                        text: i18n("Auto hide")
                        description: i18n("Shows at the screen edge")
                        Binding { target: autoHideCard; property: "checked"; value: DockSettings.visibilityMode === 1 }
                        onChosen: DockSettings.visibilityMode = 1

                        VisibilityTile {
                            mode: 1
                            dockHidden: 1
                            pointerOpacity: 1
                            pointerTravel: 0.7
                        }
                    }

                    ChoiceCard {
                        id: dodgeCard
                        text: i18n("Dodge windows")
                        description: i18n("Hides under windows")
                        Binding { target: dodgeCard; property: "checked"; value: DockSettings.visibilityMode === 2 }
                        onChosen: DockSettings.visibilityMode = 2

                        VisibilityTile {
                            mode: 2
                            windowDodge: 1
                            dockHidden: 0.55
                        }
                    }
                }
            }
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.visibilityMode === 0
        }

        FormCard.FormSwitchDelegate {
            text: i18n("Reserve screen space")
            description: i18n("Maximized windows avoid the dock")
            checked: DockSettings.reserveScreenSpace
            onToggled: DockSettings.reserveScreenSpace = checked
            visible: DockSettings.visibilityMode === 0
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.visibilityMode === 2
        }

        FormCard.FormSwitchDelegate {
            text: i18n("Only dodge active window")
            description: i18n("When off, hides for any overlapping window")
            checked: DockSettings.dodgeActiveOnly
            onToggled: DockSettings.dodgeActiveOnly = checked
            visible: DockSettings.visibilityMode === 2
        }

        // Show/hide delay controls — only visible in hide-capable modes
        FormCard.FormDelegateSeparator {
            visible: DockSettings.visibilityMode !== 0
        }

        SliderDelegate {
            visible: DockSettings.visibilityMode !== 0
            text: i18n("Show delay (ms)")
            from: 0; to: 2000; stepSize: 50
            value: DockSettings.showDelay
            valueText: i18nc("@label milliseconds", "%1 ms", value)
            onMoved: (value) => DockSettings.showDelay = value
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.visibilityMode !== 0
        }

        SliderDelegate {
            visible: DockSettings.visibilityMode !== 0
            text: i18n("Hide delay (ms)")
            from: 0; to: 2000; stepSize: 50
            value: DockSettings.hideDelay
            valueText: i18nc("@label milliseconds", "%1 ms", value)
            onMoved: (value) => DockSettings.hideDelay = value
        }
    }

    // --- Tasks ---
    FormCard.FormHeader {
        title: i18n("Tasks")
    }

    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18n("Separate pinned and running apps")
            description: i18n("Pinned apps, including running apps, stay together; unpinned running apps appear after the divider.")
            checked: DockSettings.separateLaunchers
            onToggled: DockSettings.separateLaunchers = checked
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormComboBoxDelegate {
            text: i18n("Single window click action")
            displayMode: FormCard.FormComboBoxDelegate.Dialog
            model: [
                i18n("Activate window"),
                i18n("Minimize active window")
            ]
            currentIndex: DockSettings.singleWindowClickAction
            onActivated: function(index) {
                DockSettings.singleWindowClickAction = index
            }
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormComboBoxDelegate {
            text: i18n("Grouped window click action")
            displayMode: FormCard.FormComboBoxDelegate.Dialog
            model: [
                i18n("Cycle through windows"),
                i18n("Show window previews"),
                i18n("Minimize active window")
            ]
            currentIndex: DockSettings.groupedWindowClickAction
            onActivated: function(index) {
                DockSettings.groupedWindowClickAction = index
            }
        }
    }
}
