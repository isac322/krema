// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import com.bhyoo.krema 1.0

FormCard.FormCardPage {
    title: i18n("Effects")

    // --- Parabolic Zoom ---
    FormCard.FormHeader {
        title: i18n("Parabolic Zoom")
    }

    FormCard.FormCard {
        FormCard.AbstractFormDelegate {
            id: zoomDelegate
            Accessible.name: i18n("Maximum zoom")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Maximum zoom")
                        elide: Text.ElideRight
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        color: zoomDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    }

                    QQC2.Label {
                        text: zoomSlider.value.toFixed(1) + "x"
                        color: Kirigami.Theme.disabledTextColor
                    }
                }

                QQC2.Slider {
                    id: zoomSlider
                    Layout.fillWidth: true
                    from: 1.0; to: 2.0; stepSize: 0.1
                    value: DockSettings.maxZoomFactor
                    onMoved: DockSettings.maxZoomFactor = value
                    Accessible.name: i18n("Maximum zoom")
                }
            }
        }

        FormCard.FormDelegateSeparator {}

        FormCard.AbstractFormDelegate {
            id: spreadDelegate
            Accessible.name: i18n("Zoom spread")
            background: null
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Zoom spread")
                        elide: Text.ElideRight
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        color: spreadDelegate.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    }

                    QQC2.Label {
                        text: spreadSlider.value.toFixed(1)
                        color: Kirigami.Theme.disabledTextColor
                    }
                }

                QQC2.Slider {
                    id: spreadSlider
                    Layout.fillWidth: true
                    from: 0.4; to: 3.0; stepSize: 0.1
                    value: DockSettings.zoomSpread
                    onMoved: DockSettings.zoomSpread = value
                    Accessible.name: i18n("Zoom spread")
                }
            }
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSpinBoxDelegate {
            label: i18n("Cursor activation radius (px)")
            description: i18n("Zoom activates when the cursor is within this distance of the dock, from any direction. 0 = only directly on icons")
            from: 0; to: 500; stepSize: 10
            value: DockSettings.zoomTriggerDistance
            onValueModified: DockSettings.zoomTriggerDistance = value
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSpinBoxDelegate {
            label: i18n("Zoom animation duration (ms)")
            from: 0; to: 600; stepSize: 25
            value: DockSettings.zoomAnimationDuration
            onValueModified: DockSettings.zoomAnimationDuration = value
        }
    }

    // --- Launch Bounce ---
    FormCard.FormHeader {
        title: i18n("Launch Bounce")
    }

    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18n("Always bounce on launch")
            description: i18n("Bounce even for apps without startup notification (e.g. Electron and GTK apps), until their window appears")
            checked: DockSettings.bounceAlwaysOnLaunch
            onToggled: DockSettings.bounceAlwaysOnLaunch = checked
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSpinBoxDelegate {
            label: i18n("Bounce height (px)")
            from: 2; to: 40
            value: DockSettings.bounceHeight
            onValueModified: DockSettings.bounceHeight = value
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSpinBoxDelegate {
            label: i18n("Bounce duration (ms)")
            description: i18n("Duration of one full up-down cycle")
            from: 80; to: 1000; stepSize: 20
            value: DockSettings.bounceDuration
            onValueModified: DockSettings.bounceDuration = value
        }
    }

    // --- Attention ---
    FormCard.FormHeader {
        title: i18n("Attention")
    }

    FormCard.FormCard {
        FormCard.FormComboBoxDelegate {
            text: i18n("Attention animation")
            description: i18n("Animation when an app demands attention")
            model: [
                i18n("None"),
                i18n("Bounce"),
                i18n("Wiggle"),
                i18n("Pulse"),
                i18n("Glow"),
                i18n("Dot color"),
                i18n("Blink")
            ]
            currentIndex: DockSettings.attentionAnimation
            onActivated: function(index) { DockSettings.attentionAnimation = index }
        }

        FormCard.FormDelegateSeparator {
            visible: DockSettings.attentionAnimation > 0
        }

        FormCard.FormSpinBoxDelegate {
            visible: DockSettings.attentionAnimation > 0
            label: i18n("Attention duration (seconds, 0 = infinite)")
            from: 0; to: 60
            value: DockSettings.attentionAnimationDuration
            onValueModified: DockSettings.attentionAnimationDuration = value
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormComboBoxDelegate {
            text: i18n("Badge display")
            description: i18n("How notification badges appear on dock icons")
            model: [
                i18n("Number"),
                i18n("Dot"),
                i18n("Off")
            ]
            currentIndex: DockSettings.badgeDisplayMode
            onActivated: function(index) { DockSettings.badgeDisplayMode = index }
        }
    }

    // --- Compositor Animations ---
    FormCard.FormHeader {
        title: i18n("Compositor Animations")
    }

    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18n("Animate windows into dock icons when minimizing")
            description: i18n("Lets KWin's Magic Lamp or Squash effects fly minimized windows into their dock icon. Requires the corresponding KWin desktop effect to be enabled.")
            checked: DockSettings.publishGeometryToCompositor
            onToggled: DockSettings.publishGeometryToCompositor = checked
        }
    }
}
