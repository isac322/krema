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

    title: i18n("Behavior")

    // Responsive vertical padding: scales with page width
    topPadding: Math.round(Kirigami.Units.gridUnit * Math.max(0.5, Math.min(1.5, width / 800)))
    bottomPadding: topPadding

    // Looping previews run only while the page is shown in a visible window.
    readonly property bool previewsActive: page.visible
        && page.Window.window !== null && page.Window.window.visible

    // Simplified screen with a dock at its bottom edge. `mode` follows the
    // VisibilityMode enum: 0 = static dock, 1 = auto hide (dock slides out
    // and back), 2 = dodge (a window descends onto the dock, which hides).
    component VisibilityPreview: Item {
        id: preview

        property int mode: 0
        property bool running: false
        // 0 = dock fully shown, 1 = dock fully hidden below the screen edge
        property real dockHidden: 0
        // 0 = window at the top of the screen, 1 = window overlapping the dock
        property real windowDown: 0

        Rectangle {
            id: screen
            anchors.centerIn: parent
            height: parent.height
            width: Math.min(parent.width, height * 1.6)
            radius: Kirigami.Units.smallSpacing
            clip: true
            color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.06)
            border.width: 1
            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.3)

            // Application window (dodge mode only)
            Rectangle {
                visible: preview.mode === 2
                width: screen.width * 0.6
                height: screen.height * 0.55
                x: (screen.width - width) / 2
                y: Kirigami.Units.smallSpacing + preview.windowDown * (screen.height - height - Kirigami.Units.smallSpacing * 2)
                radius: 2
                color: Kirigami.Theme.alternateBackgroundColor
                border.width: 1
                border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.4)

                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 1
                    height: Math.max(3, parent.height * 0.15)
                    color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.alternateBackgroundColor, Kirigami.Theme.textColor, 0.15)
                }
            }

            // Dock
            Rectangle {
                id: dock
                readonly property real shownY: screen.height - height - Kirigami.Units.smallSpacing
                readonly property real hiddenY: screen.height + 1

                width: dockIcons.implicitWidth + Kirigami.Units.smallSpacing * 2
                height: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                x: (screen.width - width) / 2
                y: shownY + preview.dockHidden * (hiddenY - shownY)
                radius: Kirigami.Units.smallSpacing
                color: Qt.alpha(Kirigami.Theme.highlightColor, 0.35)
                border.width: 1
                border.color: Kirigami.Theme.highlightColor

                Row {
                    id: dockIcons
                    anchors.centerIn: parent
                    spacing: 2

                    Repeater {
                        model: ["system-file-manager", "internet-web-browser", "utilities-terminal"]

                        Kirigami.Icon {
                            required property string modelData
                            width: Kirigami.Units.iconSizes.small - 2
                            height: width
                            source: modelData
                        }
                    }
                }
            }
        }

        // Auto hide: dock slides away after inactivity, then comes back.
        SequentialAnimation {
            running: preview.running && preview.mode === 1
            loops: Animation.Infinite
            onStopped: preview.dockHidden = 0

            PauseAnimation { duration: 1500 }
            NumberAnimation { target: preview; property: "dockHidden"; to: 1; duration: 300; easing.type: Easing.InQuad }
            PauseAnimation { duration: 1000 }
            NumberAnimation { target: preview; property: "dockHidden"; to: 0; duration: 300; easing.type: Easing.OutQuad }
        }

        // Dodge: a window moves down onto the dock, the dock hides; the
        // window moves away again and the dock returns.
        SequentialAnimation {
            running: preview.running && preview.mode === 2
            loops: Animation.Infinite
            onStopped: {
                preview.dockHidden = 0
                preview.windowDown = 0
            }

            PauseAnimation { duration: 800 }
            NumberAnimation { target: preview; property: "windowDown"; to: 1; duration: 900; easing.type: Easing.InOutQuad }
            NumberAnimation { target: preview; property: "dockHidden"; to: 1; duration: 200; easing.type: Easing.InQuad }
            PauseAnimation { duration: 1000 }
            NumberAnimation { target: preview; property: "windowDown"; to: 0; duration: 900; easing.type: Easing.InOutQuad }
            NumberAnimation { target: preview; property: "dockHidden"; to: 0; duration: 200; easing.type: Easing.OutQuad }
        }
    }

    // --- Visibility ---
    FormCard.FormHeader {
        title: i18n("Visibility")
    }

    FormSection {
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
                    id: visibilityGrid
                    Layout.fillWidth: true
                    columns: Math.max(1, Math.floor(width / (Kirigami.Units.gridUnit * 9)))
                    columnSpacing: Kirigami.Units.largeSpacing
                    rowSpacing: Kirigami.Units.largeSpacing

                    ChoiceCard {
                        id: alwaysCard
                        Layout.fillWidth: true
                        text: i18n("Always visible")
                        Binding { target: alwaysCard; property: "checked"; value: DockSettings.visibilityMode === 0 }
                        onChosen: DockSettings.visibilityMode = 0

                        VisibilityPreview {
                            anchors.fill: parent
                            mode: 0
                        }
                    }

                    ChoiceCard {
                        id: autoHideCard
                        Layout.fillWidth: true
                        text: i18n("Auto hide")
                        Binding { target: autoHideCard; property: "checked"; value: DockSettings.visibilityMode === 1 }
                        onChosen: DockSettings.visibilityMode = 1

                        VisibilityPreview {
                            anchors.fill: parent
                            mode: 1
                            running: page.previewsActive
                                && (autoHideCard.checked || autoHideCard.hovered || autoHideCard.activeFocus)
                        }
                    }

                    ChoiceCard {
                        id: dodgeCard
                        Layout.fillWidth: true
                        text: i18n("Dodge windows")
                        Binding { target: dodgeCard; property: "checked"; value: DockSettings.visibilityMode === 2 }
                        onChosen: DockSettings.visibilityMode = 2

                        VisibilityPreview {
                            anchors.fill: parent
                            mode: 2
                            running: page.previewsActive
                                && (dodgeCard.checked || dodgeCard.hovered || dodgeCard.activeFocus)
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

    FormSection {
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
