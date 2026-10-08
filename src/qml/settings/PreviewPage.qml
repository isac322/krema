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

    title: i18n("Window Preview")

    // Responsive vertical padding: scales with page width
    topPadding: Math.round(Kirigami.Units.gridUnit * Math.max(0.5, Math.min(1.5, width / 800)))
    bottomPadding: topPadding

    // Clicking a grouped dock item can open the preview popup (GroupedWindowClickAction 1 =
    // "Show previews") even when hover previews are off, so the popup size and hide delay
    // stay meaningful in that case.
    readonly property bool groupedClickShowsPreviews: DockSettings.groupedWindowClickAction === 1
    readonly property bool popupInUse: DockSettings.previewEnabled || groupedClickShowsPreviews

    FormSection {
        FormCard.FormSwitchDelegate {
            text: i18n("Show window previews on hover")
            description: i18n("Show window thumbnails when hovering dock items")
            checked: DockSettings.previewEnabled
            onToggled: DockSettings.previewEnabled = checked
        }

        FormCard.FormDelegateSeparator {
            visible: groupedClickNote.visible
        }

        FormCard.FormTextDelegate {
            id: groupedClickNote
            visible: !DockSettings.previewEnabled && page.groupedClickShowsPreviews
            text: i18n("Previews still open on click")
            description: i18n("Clicking an app with several windows shows previews, so the thumbnail width and hide delay below still apply.")
        }
    }

    // --- Sample Preview Mockup ---
    FormCard.FormHeader {
        title: i18n("Preview Appearance")
    }

    FormSection {
        FormCard.AbstractFormDelegate {
            id: mockupDelegate
            background: null
            enabled: page.popupInUse
            focusPolicy: Qt.NoFocus
            Accessible.role: Accessible.Graphic
            Accessible.name: i18nc("@info accessible description of the sample preview popup",
                                   "Sample window preview, %1 pixels wide", DockSettings.previewThumbnailSize)

            contentItem: Item {
                id: mockupArea
                implicitHeight: previewMockup.height + Kirigami.Units.largeSpacing * 2
                opacity: mockupDelegate.enabled ? 1.0 : 0.4

                Behavior on opacity {
                    NumberAnimation { duration: Kirigami.Units.shortDuration; easing.type: Easing.InOutQuad }
                }

                // Preview popup mockup: the thumbnail is as wide as the configured width,
                // like the real popup (clamped so it never overflows the settings page).
                Rectangle {
                    id: previewMockup
                    anchors.centerIn: parent
                    width: Math.min(DockSettings.previewThumbnailSize + Kirigami.Units.largeSpacing * 2,
                                    mockupArea.width)
                    height: mockupColumn.implicitHeight + Kirigami.Units.largeSpacing * 2
                    radius: Kirigami.Units.cornerRadius
                    color: Kirigami.Theme.backgroundColor
                    border.width: 1
                    border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor,
                                                                          Kirigami.Theme.textColor, 0.2)

                    Behavior on width {
                        NumberAnimation { duration: Kirigami.Units.longDuration; easing.type: Easing.InOutQuad }
                    }

                    ColumnLayout {
                        id: mockupColumn
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        // App title row
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                source: "internet-web-browser"
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: i18nc("@label title of the sample window in the preview mockup", "Sample Window")
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Kirigami.Icon {
                                source: "window-close"
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                                opacity: 0.5
                            }
                        }

                        // Thumbnail placeholder (16:10 like a typical window)
                        Rectangle {
                            id: thumbnailArea
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.round(width * 0.625)
                            radius: Kirigami.Units.smallSpacing
                            color: Kirigami.Theme.alternateBackgroundColor
                            clip: true

                            // Fake window header bar
                            Rectangle {
                                anchors.top: parent.top
                                anchors.left: parent.left
                                anchors.right: parent.right
                                height: Math.max(Kirigami.Units.smallSpacing * 2, Math.round(parent.height * 0.12))
                                color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.alternateBackgroundColor,
                                                                               Kirigami.Theme.highlightColor, 0.35)
                            }

                            // Fake window content
                            Column {
                                anchors.centerIn: parent
                                anchors.verticalCenterOffset: Math.round(thumbnailArea.height * 0.06)
                                spacing: Kirigami.Units.smallSpacing

                                Repeater {
                                    model: [0.6, 0.8, 0.5, 0.7]
                                    delegate: Rectangle {
                                        required property real modelData
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: Math.round(thumbnailArea.width * modelData)
                                        height: Math.max(2, Math.round(Kirigami.Units.smallSpacing))
                                        radius: height / 2
                                        color: Kirigami.Theme.disabledTextColor
                                        opacity: 0.3
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // --- Settings ---
    FormCard.FormHeader {
        title: i18n("Settings")
    }

    FormSection {
        SliderDelegate {
            text: i18n("Thumbnail width (px)")
            from: 120; to: 320; stepSize: 20
            value: DockSettings.previewThumbnailSize
            valueText: i18nc("@label pixels", "%1 px", value)
            enabled: page.popupInUse
            onMoved: (value) => DockSettings.previewThumbnailSize = value
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
            text: i18n("Hover delay (ms)")
            from: 0; to: 2000; stepSize: 50
            value: DockSettings.previewHoverDelay
            valueText: i18nc("@label milliseconds", "%1 ms", value)
            enabled: DockSettings.previewEnabled
            onMoved: (value) => DockSettings.previewHoverDelay = value
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
            text: i18n("Hide delay (ms)")
            from: 0; to: 1000; stepSize: 50
            value: DockSettings.previewHideDelay
            valueText: i18nc("@label milliseconds", "%1 ms", value)
            enabled: page.popupInUse
            onMoved: (value) => DockSettings.previewHideDelay = value
        }
    }
}
