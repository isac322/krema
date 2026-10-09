// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Shapes
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0
import ".."

SettingsPage {
    id: page
    title: i18n("Monitors & Desktops")
    subtitle: i18n("Which screens show the dock, and which desktops' windows appear on it")

    // MonitorMode: 0 Primary, 1 All, 2 Follow active, 3 Selected outputs.
    readonly property var monitorModeNames: [
        i18n("Primary monitor only"),
        i18n("All monitors"),
        i18n("Follow active screen"),
        i18n("Selected monitors")
    ]
    // FollowActiveTrigger: 0 Mouse, 1 Focus, 2 Composite.
    readonly property var followTriggerNames: [
        i18n("Mouse position"),
        i18n("Active window focus"),
        i18n("Composite (focus + mouse)")
    ]
    readonly property var followTriggerDescriptions: [
        i18n("Moves when the pointer reaches the dock edge of another screen"),
        i18n("Moves to the screen of the focused window"),
        i18n("Moves on window focus or when the pointer reaches the dock edge")
    ]
    readonly property var followTriggerIcons: ["input-mouse", "window", "merge"]
    // ScreenTransition: 0 Fade, 1 Slide, 2 Instant.
    readonly property var screenTransitionNames: [
        i18n("Fade"),
        i18n("Slide"),
        i18n("Instant")
    ]
    // VirtualDesktopMode: 0 Show all, 1 Dim others, 2 Current only.
    readonly property var desktopModeNames: [
        i18n("Show all windows"),
        i18n("Dim other desktops"),
        i18n("Current desktop only")
    ]

    // Looping previews only run while the page is shown; zero animation
    // durations (Plasma "Instant" speed) would make an infinite loop spin.
    readonly property bool animationsEnabled: windowActive && Kirigami.Units.longDuration > 0

    // Screens drawn on the stage and in the picker tiles: the connected
    // outputs (at most three; saved-but-disconnected outputs can host no
    // dock), or a generic pair when fewer than two are connected.
    readonly property var stageScreens: {
        const connected = SettingsWindow.availableScreens.filter(screen => screen.available)
        if (connected.length >= 2) {
            return connected.slice(0, 3)
        }
        return [
            { name: "primary", label: "primary", primary: true, available: true },
            { name: "secondary", label: "secondary", primary: false, available: true }
        ]
    }
    readonly property var tileScreens: stageScreens.slice(0, 2)

    // Dock apps on the stage monitors: MiniDock's default set, kept here so
    // the arrangement and the desktop-mode tiles can share one list.
    readonly property var stageApps: [
        { icon: "org.kde.dolphin", fallback: "system-file-manager", state: "inactive", badge: 0, padding: 1.0 },
        { icon: "org.kde.konsole", fallback: "utilities-terminal", state: "active", badge: 0, padding: 1.0 },
        { icon: "firefox", fallback: "internet-web-browser", state: "inactive", badge: 0, padding: 1.0 },
        { icon: "org.kde.discover", fallback: "system-software-install", state: "launcher", badge: 3, padding: 1.0 },
        { icon: "systemsettings", fallback: "preferences-system", state: "launcher", badge: 0, padding: 1.0 }
    ]

    // Apps for the display-mode tiles: current-desktop windows first, then
    // the other desktops' windows — full opacity for "show all", dimmed via
    // opacityMinimized for "dim others" and dropped for "current only".
    function desktopApps(mode) {
        const current = [
            { icon: "org.kde.konsole", fallback: "utilities-terminal", state: "active", badge: 0, padding: 1.0 },
            { icon: "org.kde.dolphin", fallback: "system-file-manager", state: "inactive", badge: 0, padding: 1.0 }
        ]
        if (mode === 2) {
            return current
        }
        const otherState = mode === 1 ? "minimized" : "inactive"
        return current.concat([
            { icon: "firefox", fallback: "internet-web-browser", state: otherState, badge: 0, padding: 1.0 },
            { icon: "org.kde.kate", fallback: "accessories-text-editor", state: otherState, badge: 0, padding: 1.0 }
        ])
    }

    // Indices of `infos` whose output names are in the "Selected monitors"
    // selection.
    function selectedIndicesFor(infos) {
        const selected = DockSettings.selectedOutputs
        const indices = []
        for (let i = 0; i < infos.length; ++i) {
            if (selected.indexOf(infos[i].name) !== -1) {
                indices.push(i)
            }
        }
        return indices
    }

    readonly property color frameColor: Kirigami.ColorUtils.linearInterpolation(
        Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.5)
    // Monitor bezel and stand: darkened theme colors, quieter than text.
    readonly property color bezelColor: Kirigami.ColorUtils.linearInterpolation(
        Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.16)
    readonly property color standColor: Kirigami.ColorUtils.linearInterpolation(
        Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.28)
    // Translucent menu-bar strip marking the primary screen, like the Plasma
    // panel on a real desktop.
    readonly property color menuBarColor: Qt.alpha(Kirigami.Theme.backgroundColor, 0.6)

    // A small app window on a mini desktop.
    component MiniWindow: Rectangle {
        property alias iconName: windowIcon.source

        radius: 1
        color: Kirigami.Theme.backgroundColor
        border.width: 1
        border.color: page.frameColor

        Kirigami.Icon {
            id: windowIcon
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height) * 0.7
            height: width
        }
    }

    // A row of miniature monitors (theme-colored bezel and stand, the real
    // wallpaper on each screen) with a MiniDock on the screens the monitor
    // mode selects. Mode 2 and the transition demos move one dock between
    // the screens with the configured screen transition.
    component MonitorArrangement: Item {
        id: arrange

        // MonitorMode value; -1 = transition demo (dock travelling with the
        // transition picked by the card).
        property int mode: DockSettings.monitorMode
        property var screenInfos: page.tileScreens
        /// Check badges over the screens ("Selected monitors" preview).
        property bool showSelection: false
        /// ScreenTransition index overriding the setting, or -1.
        property int forcedTransition: -1
        /// Move the dock between the screens in a loop.
        property bool travelling: false
        property bool active: page.windowActive
        property bool dockPulse: false
        property var apps: page.stageApps
        /// Opacity of "minimized"-state icons (display-mode tiles use the
        /// other-desktop opacity here).
        property real otherOpacity: DockSettings.iconOpacityMinimized
        /// Bound the screen height by the item's height (picker tiles).
        property bool fitHeight: false
        /// Upper bound for drawing the dock larger than its real proportion;
        /// in practice the ~45%-of-the-edge cap below decides, so the dock
        /// and its icons stay readable on a miniature screen.
        property real dockBoost: fitHeight ? 8 : 6

        readonly property int count: Math.max(1, screenInfos.length)
        readonly property int primaryIndex: {
            for (let i = 0; i < screenInfos.length; ++i) {
                if (screenInfos[i].primary) {
                    return i
                }
            }
            return 0
        }
        readonly property var selectedIndices: page.selectedIndicesFor(screenInfos)
        readonly property int transitionKind: forcedTransition >= 0 ? forcedTransition : DockSettings.screenTransition

        readonly property real gap: fitHeight ? Kirigami.Units.smallSpacing * 2 : Kirigami.Units.largeSpacing * 2
        readonly property real bezel: fitHeight ? 2 : Math.max(3, Kirigami.Units.smallSpacing)
        readonly property real aspect: SettingsWindow.screenAspect
        readonly property real screenHeightMax: fitHeight && height > 0
            ? Math.max(1, (height - bezel * 2) / 1.14)
            : Kirigami.Units.gridUnit * 9.5
        readonly property real screenWidth: Math.max(1, Math.min(
            (width - gap * (count - 1)) / count - bezel * 2,
            screenHeightMax * aspect))
        readonly property real screenHeight: Math.round(screenWidth / aspect)
        readonly property real standHeight: Math.max(4, Math.round(screenHeight * 0.12))

        // Natural size derives from the width only, so a layout reading it
        // can never feed the item's height back into its own implicit size.
        readonly property real _naturalScreenHeight: Math.round(Math.max(1, Math.min(
            (width - gap * (count - 1)) / count - bezel * 2,
            Kirigami.Units.gridUnit * 9.5 * aspect)) / aspect)

        implicitWidth: Kirigami.Units.gridUnit * 20
        implicitHeight: _naturalScreenHeight + bezel * 2 + Math.max(4, Math.round(_naturalScreenHeight * 0.12))

        Accessible.ignored: true

        // Continuous position of the travelling dock (fractional during a
        // transition): the screen index currently hosting it.
        // The animation drives followProgress over a constant 0 -> 1 range;
        // nothing about the animation itself changes while it runs.
        readonly property real followPos: followFrom + (followTarget - followFrom) * followProgress
        property int followFrom: 0
        property int followTarget: 0
        property real followProgress: 1
        property int _followDir: 1

        Component.onCompleted: _resetFollow()
        onTravellingChanged: if (!travelling) _resetFollow()
        onModeChanged: _resetFollow()

        function _resetFollow() {
            followFrom = followTarget = (mode === -1 ? 0 : primaryIndex)
            followProgress = 1
            _followDir = 1
        }

        function _advance() {
            followFrom = followTarget
            followProgress = 0
            if (count < 2) {
                return
            }
            let next = followTarget + _followDir
            if (next < 0 || next >= count) {
                _followDir = -_followDir
                next = followTarget + _followDir
            }
            followTarget = next
        }

        // 1 = dock fully on screen `index`, 0 = absent; fractions while the
        // travelling dock crosses between screens.
        function dockShownOn(index) {
            switch (mode) {
            case 0:
                return index === primaryIndex ? 1 : 0
            case 1:
                return 1
            case 3:
                if (selectedIndices.length === 0) {
                    // Temporary dock on the primary display.
                    return index === primaryIndex ? 1 : 0
                }
                return selectedIndices.indexOf(index) !== -1 ? 1 : 0
            default:
                return Math.max(0, 1 - Math.abs(index - followPos))
            }
        }

        Row {
            anchors.centerIn: parent
            spacing: arrange.gap

            Repeater {
                model: arrange.screenInfos

                delegate: Column {
                    required property int index
                    required property var modelData

                    spacing: 0

                    // Bezel around the screen.
                    Rectangle {
                        width: arrange.screenWidth + arrange.bezel * 2
                        height: arrange.screenHeight + arrange.bezel * 2
                        radius: Math.max(4, arrange.bezel * 1.5)
                        color: page.bezelColor
                        border.width: 1
                        border.color: Qt.alpha(Kirigami.Theme.textColor, 0.15)

                        DesktopStage {
                            id: miniStage
                            anchors.fill: parent
                            anchors.margins: arrange.bezel
                            elevated: false
                            radius: Math.max(1, arrange.bezel * 0.5)
                            active: arrange.active
                            // The whole screen fits inside the screen area.
                            unit: Math.min(width / screenSize.width, height / screenSize.height)

                            // Menu bar: marks the primary screen like the
                            // Plasma panel on a real desktop.
                            Rectangle {
                                visible: modelData.primary
                                anchors.top: parent.top
                                anchors.left: parent.left
                                anchors.right: parent.right
                                height: Math.max(2, Math.round(parent.height * 0.05))
                                color: page.menuBarColor
                            }

                            MiniWindow {
                                x: Math.round(parent.width * 0.10)
                                y: Math.round(parent.height * 0.14)
                                width: Math.round(parent.width * 0.42)
                                height: Math.round(parent.height * 0.40)
                                iconName: ["utilities-terminal", "internet-web-browser", "system-file-manager"][index % 3]
                            }

                            MiniWindow {
                                x: Math.round(parent.width * 0.48)
                                y: Math.round(parent.height * 0.38)
                                width: Math.round(parent.width * 0.42)
                                height: Math.round(parent.height * 0.38)
                                iconName: ["system-file-manager", "accessories-text-editor", "utilities-terminal"][index % 3]
                            }

                            // The dock, shown where the mode puts it. For
                            // follow active and the transition demos the
                            // whole host slides/fades with `shown`.
                            Item {
                                id: dockHost
                                anchors.fill: parent

                                readonly property real shown: arrange.dockShownOn(index)
                                readonly property real slide: arrange.transitionKind === 1 ? 1 - shown : 0
                                readonly property real slideDist: miniDock.thickness + miniDock.floatPx + 2

                                visible: shown > 0.001
                                opacity: arrange.transitionKind === 0 ? shown : 1

                                transform: Translate {
                                    x: dockHost.slide * (miniDock.vertical
                                        ? (miniDock.edge === 2 ? -dockHost.slideDist : dockHost.slideDist) : 0)
                                    y: dockHost.slide * (miniDock.vertical ? 0
                                        : (miniDock.edge === 0 ? -dockHost.slideDist : dockHost.slideDist))
                                }

                                MiniDock {
                                    id: miniDock
                                    // Real proportions, enlarged up to
                                    // dockBoost and to about 45% of the
                                    // screen edge, so which screens have a
                                    // dock reads at a glance.
                                    unit: Math.max(miniStage.unit, Math.min(miniStage.unit * arrange.dockBoost,
                                        0.45 * (vertical ? miniStage.desktop.height : miniStage.desktop.width) / restLengthReal))
                                    backdrop: miniStage.backdrop
                                    active: arrange.active
                                    apps: arrange.apps
                                    opacityMinimized: arrange.otherOpacity
                                    pulse: arrange.dockPulse
                                }
                            }
                        }

                        // Selection badge for the "Selected monitors"
                        // preview: checked where the output is selected.
                        Rectangle {
                            readonly property bool marked: arrange.selectedIndices.indexOf(index) !== -1

                            visible: arrange.showSelection
                            width: Math.max(8, Math.round(arrange.screenHeight * 0.28))
                            height: width
                            radius: width / 2
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: arrange.bezel + Math.max(2, Math.round(arrange.screenHeight * 0.05))
                            color: marked ? Kirigami.Theme.highlightColor : Qt.alpha(Kirigami.Theme.backgroundColor, 0.6)
                            border.width: marked ? 0 : 1
                            border.color: Kirigami.Theme.disabledTextColor

                            // Drawn check mark: icon themes may lack a
                            // "checkmark" icon.
                            Shape {
                                id: checkMark
                                visible: parent.marked
                                anchors.fill: parent
                                antialiasing: true

                                ShapePath {
                                    strokeColor: Kirigami.Theme.highlightedTextColor
                                    strokeWidth: Math.max(1.5, checkMark.width * 0.14)
                                    fillColor: Qt.alpha(Kirigami.Theme.highlightedTextColor, 0)
                                    capStyle: ShapePath.RoundCap
                                    joinStyle: ShapePath.RoundJoin
                                    startX: checkMark.width * 0.28; startY: checkMark.height * 0.52
                                    PathLine { x: checkMark.width * 0.44; y: checkMark.height * 0.68 }
                                    PathLine { x: checkMark.width * 0.74; y: checkMark.height * 0.34 }
                                }
                            }
                        }
                    }

                    // Stand: neck and foot under the bezel.
                    Item {
                        width: arrange.screenWidth + arrange.bezel * 2
                        height: arrange.standHeight

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            width: Math.max(4, Math.round(arrange.screenWidth * 0.08))
                            height: Math.round(parent.height * 0.55)
                            color: page.standColor
                        }

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            width: Math.round(arrange.screenWidth * 0.3)
                            height: Math.max(3, Math.round(parent.height * 0.4))
                            radius: height / 2
                            color: page.standColor
                        }
                    }
                }
            }
        }

        SequentialAnimation {
            running: arrange.travelling && arrange.visible && page.animationsEnabled
            loops: Animation.Infinite

            ScriptAction {
                script: arrange._advance()
            }
            PauseAnimation {
                duration: Kirigami.Units.veryLongDuration * 2
            }
            NumberAnimation {
                target: arrange
                property: "followProgress"
                from: 0
                to: 1
                duration: arrange.transitionKind === 2 ? 0 : Kirigami.Units.longDuration * 2
                easing.type: Easing.InOutQuad
            }
        }
    }

    // Picker tile for the display modes: the desktop cropped around a dock
    // sized to fit the tile (like IconsPage's DockTile), whose icons show how
    // other desktops' windows appear.
    component DesktopTile: DesktopStage {
        id: tile

        property int desktopMode: 0

        anchors.fill: parent
        elevated: false
        radius: 0
        active: page.windowActive
        // Fit the resting dock along the edge and the zoomed icons across it.
        unit: Math.min(0.85 * (tileDock.vertical ? height : width) / tileDock.restLengthReal,
                       0.7 * (tileDock.vertical ? width : height)
                           / (tileDock.iconSize * Math.max(1.0, tileDock.maxZoomFactor) + 2 * Kirigami.Units.largeSpacing + 8))
        Accessible.ignored: true

        // Windows of the current desktop, placed in the visible (cropped)
        // part of the screen: frame coordinates minus the screen's offset.
        MiniWindow {
            x: Math.round(-tile.desktop.x + tile.width * 0.08)
            y: Math.round(-tile.desktop.y + tile.height * 0.10)
            width: Math.round(tile.width * 0.44)
            height: Math.round(tile.height * 0.34)
            iconName: "utilities-terminal"
        }

        MiniWindow {
            x: Math.round(-tile.desktop.x + tile.width * 0.46)
            y: Math.round(-tile.desktop.y + tile.height * 0.18)
            width: Math.round(tile.width * 0.44)
            height: Math.round(tile.height * 0.30)
            iconName: "system-file-manager"
        }

        MiniDock {
            id: tileDock
            unit: tile.unit
            edge: tile.edge
            backdrop: tile.backdrop
            active: tile.active
            apps: page.desktopApps(tile.desktopMode)
            // Other desktops' windows ride the "minimized" opacity slot.
            opacityMinimized: DockSettings.otherDesktopOpacity
        }
    }

    // --- Stage: the user's monitors with the dock where it really is ---

    stage: Component {
        Item {
            implicitHeight: monitors.implicitHeight

            Accessible.role: Accessible.Graphic
            Accessible.name: i18n("Monitor arrangement preview")
            Accessible.description: i18n("Miniature monitors showing on which screens the dock appears")

            MonitorArrangement {
                id: monitors
                anchors.fill: parent
                screenInfos: page.stageScreens
                travelling: DockSettings.monitorMode === 2
                dockPulse: true
                active: page.windowActive
            }
        }
    }

    // --- Monitor mode ---
    FormCard.FormHeader {
        title: i18n("Multi-Monitor")
    }

    FormCard.FormCard {
        FormCard.AbstractFormDelegate {
            background: null
            // The cards are the focusable controls; the wrapper only groups them.
            focusPolicy: Qt.NoFocus
            Accessible.role: Accessible.Grouping
            Accessible.name: i18n("Monitor mode")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Monitor mode")
                    wrapMode: Text.Wrap
                    Accessible.ignored: true
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: Math.max(1, Math.min(4, Math.floor(width / (Kirigami.Units.gridUnit * 9))))
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.largeSpacing

                    Repeater {
                        model: 4

                        ChoiceCard {
                            id: monitorCard
                            required property int index

                            Layout.fillWidth: true
                            text: page.monitorModeNames[index]

                            Binding {
                                target: monitorCard
                                property: "checked"
                                value: DockSettings.monitorMode === monitorCard.index
                            }
                            onChosen: DockSettings.monitorMode = index

                            MonitorArrangement {
                                anchors.fill: parent
                                mode: monitorCard.index
                                screenInfos: page.tileScreens
                                showSelection: monitorCard.index === 3
                                travelling: monitorCard.index === 2
                                fitHeight: true
                                active: page.windowActive
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Selected monitors (mode 3) ---
    FormCard.FormHeader {
        visible: DockSettings.monitorMode === 3
        title: i18n("Selected monitors")
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.maximumWidth: page.maximumContentWidth
        Layout.alignment: Qt.AlignHCenter
        visible: DockSettings.monitorMode === 3 && SettingsWindow.hasSelectedMonitorFallback
        type: Kirigami.MessageType.Warning
        text: i18n("No selected monitors are connected. Showing a temporary dock on the primary display.")
    }

    FormCard.FormCard {
        visible: DockSettings.monitorMode === 3
        Accessible.role: Accessible.Grouping
        Accessible.name: i18n("Selected monitors")

        Repeater {
            model: SettingsWindow.availableScreens

            delegate: FormCard.FormSwitchDelegate {
                required property var modelData

                text: modelData.label
                description: !modelData.available
                    ? i18n("Disconnected — turn off to remove from the selection")
                    : (modelData.primary ? i18n("Primary monitor") : "")
                Accessible.name: modelData.name
                checked: DockSettings.selectedOutputs.indexOf(modelData.name) !== -1
                onToggled: {
                    const name = modelData.name
                    let selected = DockSettings.selectedOutputs.slice()
                    if (checked) {
                        if (selected.indexOf(name) === -1) {
                            selected.push(name)
                        }
                    } else {
                        selected = selected.filter(output => output !== name)
                    }
                    DockSettings.selectedOutputs = selected
                }
            }
        }
    }

    // --- Follow active screen (mode 2) ---
    FormCard.FormHeader {
        visible: DockSettings.monitorMode === 2
        title: i18n("Follow trigger")
    }

    FormCard.FormCard {
        visible: DockSettings.monitorMode === 2
        Accessible.role: Accessible.Grouping
        Accessible.name: i18n("Follow trigger")

        Repeater {
            model: 3

            FormCard.FormRadioDelegate {
                id: triggerRadio
                required property int index

                text: page.followTriggerNames[index]
                description: page.followTriggerDescriptions[index]
                Accessible.name: text
                Accessible.description: description
                icon.name: page.followTriggerIcons[index]
                icon.width: Kirigami.Units.iconSizes.medium
                icon.height: Kirigami.Units.iconSizes.medium

                Binding {
                    target: triggerRadio
                    property: "checked"
                    value: DockSettings.followActiveTrigger === triggerRadio.index
                }
                onToggled: if (checked) DockSettings.followActiveTrigger = index
            }
        }
    }

    FormCard.FormHeader {
        visible: DockSettings.monitorMode === 2
        title: i18n("Screen transition")
    }

    FormCard.FormCard {
        visible: DockSettings.monitorMode === 2

        FormCard.AbstractFormDelegate {
            background: null
            focusPolicy: Qt.NoFocus
            Accessible.role: Accessible.Grouping
            Accessible.name: i18n("Screen transition")

            contentItem: GridLayout {
                columns: Math.max(1, Math.min(3, Math.floor(width / (Kirigami.Units.gridUnit * 9))))
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.largeSpacing

                Repeater {
                    model: 3

                    ChoiceCard {
                        id: transitionCard
                        required property int index

                        Layout.fillWidth: true
                        text: page.screenTransitionNames[index]

                        Binding {
                            target: transitionCard
                            property: "checked"
                            value: DockSettings.screenTransition === transitionCard.index
                        }
                        onChosen: DockSettings.screenTransition = index

                        MonitorArrangement {
                            anchors.fill: parent
                            mode: -1
                            screenInfos: page.tileScreens
                            forcedTransition: transitionCard.index
                            travelling: true
                            fitHeight: true
                            active: page.windowActive
                        }
                    }
                }
            }
        }
    }

    // --- Virtual desktops ---
    FormCard.FormHeader {
        title: i18n("Virtual Desktops")
    }

    FormCard.FormCard {
        FormCard.AbstractFormDelegate {
            background: null
            focusPolicy: Qt.NoFocus
            Accessible.role: Accessible.Grouping
            Accessible.name: i18n("Display mode")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Display mode")
                    wrapMode: Text.Wrap
                    Accessible.ignored: true
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: Math.max(1, Math.min(3, Math.floor(width / (Kirigami.Units.gridUnit * 9))))
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.largeSpacing

                    Repeater {
                        model: 3

                        ChoiceCard {
                            id: desktopCard
                            required property int index

                            Layout.fillWidth: true
                            text: page.desktopModeNames[index]
                            previewHeight: Kirigami.Units.gridUnit * 5

                            Binding {
                                target: desktopCard
                                property: "checked"
                                value: DockSettings.virtualDesktopMode === desktopCard.index
                            }
                            onChosen: DockSettings.virtualDesktopMode = index

                            DesktopTile {
                                desktopMode: desktopCard.index
                            }
                        }
                    }
                }
            }
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.virtualDesktopMode === 1
        }

        SliderDelegate {
            visible: DockSettings.virtualDesktopMode === 1
            text: i18n("Other desktop opacity")
            from: 0.1; to: 0.9; stepSize: 0.05
            value: DockSettings.otherDesktopOpacity
            valueText: i18nc("@label percentage", "%1%", Math.round(value * 100))
            onMoved: (value) => DockSettings.otherDesktopOpacity = value
        }
    }
}
