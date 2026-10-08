// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Shapes
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

FormCard.FormCardPage {
    id: page
    title: i18n("Monitors & Desktops")

    // Responsive vertical padding: scales with page width
    topPadding: Math.round(Kirigami.Units.gridUnit * Math.max(0.5, Math.min(1.5, width / 800)))
    bottomPadding: topPadding

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

    // Looping previews only run while shown and when animations are enabled
    // (zero durations would make an infinite loop spin).
    // Also paused while the window is minimized, which keeps animations ticking.
    readonly property bool animationsEnabled: Kirigami.Units.longDuration > 0
        && Window.window !== null && Window.window.visibility !== Window.Minimized

    readonly property color frameColor: Kirigami.ColorUtils.linearInterpolation(
        Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.5)
    readonly property color screenFillColor: Kirigami.ColorUtils.linearInterpolation(
        Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.08)

    // A miniature monitor with an optional dock bar at its bottom edge and an
    // optional selection mark (-1 none, 0 not selected, 1 selected).
    component MiniScreen: Rectangle {
        id: miniScreen

        property bool showDock: false
        property real dockOpacity: 1.0
        /// Downward offset of the dock bar, 1.0 = fully slid off the screen.
        property real dockSlide: 0.0
        property int selection: -1

        readonly property real dockHeight: Math.max(3, Math.round(height * 0.14))
        readonly property real dockMargin: Math.max(2, Math.round(height * 0.08))

        radius: 2
        clip: true
        color: page.screenFillColor
        border.width: 1
        border.color: page.frameColor

        Rectangle {
            visible: miniScreen.showDock
            width: Math.round(parent.width * 0.55)
            height: miniScreen.dockHeight
            radius: height / 2
            x: Math.round((parent.width - width) / 2)
            y: parent.height - miniScreen.dockMargin - height
               + miniScreen.dockSlide * (miniScreen.dockMargin + height)
            opacity: miniScreen.dockOpacity
            color: Kirigami.Theme.highlightColor
        }

        Rectangle {
            visible: miniScreen.selection >= 0
            width: Math.max(8, Math.round(parent.height * 0.32))
            height: width
            radius: width / 2
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Math.max(2, Math.round(parent.height * 0.08))
            color: miniScreen.selection === 1 ? Kirigami.Theme.highlightColor : "transparent"
            border.width: miniScreen.selection === 1 ? 0 : 1
            border.color: Kirigami.Theme.disabledTextColor

            // Drawn check mark: icon themes may lack a "checkmark" icon.
            Shape {
                id: checkMark
                visible: miniScreen.selection === 1
                anchors.fill: parent
                antialiasing: true

                ShapePath {
                    strokeColor: Kirigami.Theme.highlightedTextColor
                    strokeWidth: Math.max(1.5, checkMark.width * 0.14)
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    startX: checkMark.width * 0.28; startY: checkMark.height * 0.52
                    PathLine { x: checkMark.width * 0.44; y: checkMark.height * 0.68 }
                    PathLine { x: checkMark.width * 0.74; y: checkMark.height * 0.34 }
                }
            }
        }
    }

    // Two monitors side by side (left = primary) showing where the dock
    // appears for one monitor mode.
    component MonitorSchematic: Item {
        id: schematic

        required property int mode

        readonly property real arrowSize: Kirigami.Units.iconSizes.small
        // Follow active leaves room between the screens for the direction arrow.
        readonly property real gap: mode === 2 ? arrowSize + Kirigami.Units.smallSpacing * 2
                                               : Kirigami.Units.smallSpacing * 2
        readonly property real screenWidth: Math.max(1, Math.min((width - gap) / 2, height * 1.6))
        readonly property real screenHeight: Math.round(screenWidth / 1.6)
        readonly property real screensTop: Math.round((height - screenHeight) / 2)

        // Follow active: 0 = dock on the left screen, 1 = on the right one.
        property real followProgress: 0
        property bool movingRight: true

        Accessible.ignored: true

        Row {
            id: screens
            x: Math.round((schematic.width - width) / 2)
            y: schematic.screensTop
            spacing: schematic.gap

            MiniScreen {
                width: schematic.screenWidth
                height: schematic.screenHeight
                showDock: schematic.mode === 0 || schematic.mode === 1
                selection: schematic.mode === 3 ? 0 : -1
            }

            MiniScreen {
                width: schematic.screenWidth
                height: schematic.screenHeight
                showDock: schematic.mode === 1 || schematic.mode === 3
                selection: schematic.mode === 3 ? 1 : -1
            }
        }

        // Follow active: a single dock travelling between the screens.
        Rectangle {
            id: followDock
            visible: schematic.mode === 2
            readonly property real leftX: screens.x + (schematic.screenWidth - width) / 2
            readonly property real rightX: leftX + schematic.screenWidth + schematic.gap
            width: Math.round(schematic.screenWidth * 0.55)
            height: Math.max(3, Math.round(schematic.screenHeight * 0.14))
            radius: height / 2
            x: Math.round(leftX + schematic.followProgress * (rightX - leftX))
            y: screens.y + schematic.screenHeight - Math.max(2, Math.round(schematic.screenHeight * 0.08)) - height
            color: Kirigami.Theme.highlightColor
        }

        // Direction arrow centred in the gap between the screens; drawn so it
        // does not depend on icon theme contents.
        Shape {
            visible: schematic.mode === 2
            width: schematic.arrowSize
            height: schematic.arrowSize
            x: Math.round(screens.x + schematic.screenWidth + (schematic.gap - width) / 2)
            y: Math.round(screens.y + (schematic.screenHeight - height) / 2)
            antialiasing: true
            transform: Scale {
                origin.x: schematic.arrowSize / 2
                xScale: schematic.movingRight ? 1 : -1
            }

            ShapePath {
                strokeColor: Kirigami.Theme.highlightColor
                strokeWidth: Math.max(1.5, schematic.arrowSize * 0.14)
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                startX: schematic.arrowSize * 0.15; startY: schematic.arrowSize * 0.5
                PathLine { x: schematic.arrowSize * 0.85; y: schematic.arrowSize * 0.5 }
                PathMove { x: schematic.arrowSize * 0.55; y: schematic.arrowSize * 0.2 }
                PathLine { x: schematic.arrowSize * 0.85; y: schematic.arrowSize * 0.5 }
                PathLine { x: schematic.arrowSize * 0.55; y: schematic.arrowSize * 0.8 }
            }
        }

        SequentialAnimation {
            running: schematic.mode === 2 && schematic.visible && page.animationsEnabled
            loops: Animation.Infinite

            PropertyAction { target: schematic; property: "movingRight"; value: true }
            PauseAnimation { duration: Kirigami.Units.veryLongDuration * 2 }
            NumberAnimation {
                target: schematic; property: "followProgress"
                from: 0; to: 1
                duration: Kirigami.Units.veryLongDuration * 2
                easing.type: Easing.InOutQuad
            }
            PropertyAction { target: schematic; property: "movingRight"; value: false }
            PauseAnimation { duration: Kirigami.Units.veryLongDuration * 2 }
            NumberAnimation {
                target: schematic; property: "followProgress"
                from: 1; to: 0
                duration: Kirigami.Units.veryLongDuration * 2
                easing.type: Easing.InOutQuad
            }
        }
    }

    // The dock leaving one screen and appearing on the other with one
    // screen transition (Fade, Slide or Instant).
    component TransitionPreview: Item {
        id: transitionPreview

        required property int transition

        readonly property real gap: Kirigami.Units.smallSpacing * 2
        readonly property real screenWidth: Math.max(1, Math.min((width - gap) / 2, height * 1.6))
        readonly property real screenHeight: Math.round(screenWidth / 1.6)
        readonly property int stepDuration: transition === 2 ? 0 : Kirigami.Units.longDuration * 2

        // 1 = dock fully shown on its current screen, 0 = gone.
        property real shown: 1.0
        property bool onRight: false

        Accessible.ignored: true

        Row {
            anchors.centerIn: parent
            spacing: transitionPreview.gap

            MiniScreen {
                width: transitionPreview.screenWidth
                height: transitionPreview.screenHeight
                showDock: !transitionPreview.onRight
                dockOpacity: transitionPreview.transition === 0 ? transitionPreview.shown : 1.0
                dockSlide: transitionPreview.transition === 1 ? 1.0 - transitionPreview.shown : 0.0
            }

            MiniScreen {
                width: transitionPreview.screenWidth
                height: transitionPreview.screenHeight
                showDock: transitionPreview.onRight
                dockOpacity: transitionPreview.transition === 0 ? transitionPreview.shown : 1.0
                dockSlide: transitionPreview.transition === 1 ? 1.0 - transitionPreview.shown : 0.0
            }
        }

        SequentialAnimation {
            running: transitionPreview.visible && page.animationsEnabled
            loops: Animation.Infinite

            PauseAnimation { duration: Kirigami.Units.veryLongDuration * 2 }
            NumberAnimation {
                target: transitionPreview; property: "shown"
                to: 0; duration: transitionPreview.stepDuration
                easing.type: Easing.InQuad
            }
            PropertyAction { target: transitionPreview; property: "onRight"; value: true }
            NumberAnimation {
                target: transitionPreview; property: "shown"
                to: 1; duration: transitionPreview.stepDuration
                easing.type: Easing.OutQuad
            }
            PauseAnimation { duration: Kirigami.Units.veryLongDuration * 2 }
            NumberAnimation {
                target: transitionPreview; property: "shown"
                to: 0; duration: transitionPreview.stepDuration
                easing.type: Easing.InQuad
            }
            PropertyAction { target: transitionPreview; property: "onRight"; value: false }
            NumberAnimation {
                target: transitionPreview; property: "shown"
                to: 1; duration: transitionPreview.stepDuration
                easing.type: Easing.OutQuad
            }
        }
    }

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

    // Two virtual desktops (left = current, outlined in the highlight color)
    // above a dock whose last two icons belong to windows on the other desktop.
    component DesktopPreview: Item {
        id: desktopPreview

        required property int mode

        readonly property real gap: Kirigami.Units.smallSpacing * 2
        readonly property real iconSize: Math.round(Kirigami.Units.gridUnit * 0.9)
        readonly property real desktopWidth: Math.max(1, Math.min((width - gap) / 2,
            (height - iconSize - Kirigami.Units.smallSpacing * 3) * 1.6))
        readonly property real desktopHeight: Math.round(desktopWidth / 1.6)
        readonly property real otherIconOpacity: mode === 0 ? 1.0
            : (mode === 1 ? DockSettings.otherDesktopOpacity : 0.0)

        Accessible.ignored: true

        Column {
            anchors.centerIn: parent
            spacing: Kirigami.Units.smallSpacing

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: desktopPreview.gap

                Repeater {
                    model: [["utilities-terminal", "system-file-manager"],
                            ["internet-web-browser", "accessories-text-editor"]]

                    Rectangle {
                        required property int index
                        required property var modelData

                        width: desktopPreview.desktopWidth
                        height: desktopPreview.desktopHeight
                        radius: 2
                        color: page.screenFillColor
                        border.width: index === 0 ? 2 : 1
                        border.color: index === 0 ? Kirigami.Theme.highlightColor : page.frameColor

                        MiniWindow {
                            x: Math.round(parent.width * 0.1)
                            y: Math.round(parent.height * 0.15)
                            width: Math.round(parent.width * 0.45)
                            height: Math.round(parent.height * 0.5)
                            iconName: parent.modelData[0]
                        }

                        MiniWindow {
                            x: Math.round(parent.width * 0.45)
                            y: Math.round(parent.height * 0.35)
                            width: Math.round(parent.width * 0.45)
                            height: Math.round(parent.height * 0.5)
                            iconName: parent.modelData[1]
                        }
                    }
                }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: dockIcons.width + Kirigami.Units.smallSpacing * 2
                height: desktopPreview.iconSize + Kirigami.Units.smallSpacing
                radius: Kirigami.Units.cornerRadius
                color: Kirigami.ColorUtils.linearInterpolation(
                    Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.15)

                Behavior on width {
                    NumberAnimation { duration: Kirigami.Units.shortDuration; easing.type: Easing.OutCubic }
                }

                Row {
                    id: dockIcons
                    anchors.centerIn: parent
                    spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                    Repeater {
                        model: ["utilities-terminal", "system-file-manager",
                                "internet-web-browser", "accessories-text-editor"]

                        Kirigami.Icon {
                            required property int index
                            required property string modelData

                            readonly property bool otherDesktop: index >= 2

                            width: desktopPreview.iconSize
                            height: desktopPreview.iconSize
                            source: modelData
                            visible: !otherDesktop || desktopPreview.mode !== 2
                            opacity: otherDesktop ? desktopPreview.otherIconOpacity : 1.0

                            Behavior on opacity {
                                NumberAnimation { duration: Kirigami.Units.shortDuration }
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Monitor mode ---
    FormCard.FormHeader {
        title: i18n("Multi-Monitor")
    }

    FormSection {
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

                            MonitorSchematic {
                                anchors.fill: parent
                                mode: monitorCard.index
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
        visible: DockSettings.monitorMode === 3 && SettingsWindow.hasSelectedMonitorFallback
        type: Kirigami.MessageType.Warning
        text: i18n("No selected monitors are connected. Showing a temporary dock on the primary display.")
    }

    FormSection {
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

    FormSection {
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

    FormSection {
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

                        TransitionPreview {
                            anchors.fill: parent
                            transition: transitionCard.index
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

    FormSection {
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

                            DesktopPreview {
                                anchors.fill: parent
                                mode: desktopCard.index
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
